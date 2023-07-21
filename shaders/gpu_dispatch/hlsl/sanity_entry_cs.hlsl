//  Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.

#include "shaderEnqueueSpirvIntrinsics.hlsl.h"

#define TILE_SIZE	    16
#define MAX_ALLOCATIONS 256	// TILE_SIZE*TILE_SIZE

struct InputPayload
{
    uint   grid_size_x;
    uint   grid_size_y;
    uint   grid_size_z;
};

struct OutputPayload
{
    uint2   coord;		// pixel coordinates (top-left corner of the tile)
};

struct OutputPayloadDynamic
{
    uint3   grid_size;
    uint2   coord;		// pixel coordinates
};

#define NODE_FIXED_EXP   0
#define NODE_DYNAMIC_EXP 1
#define NODE_AGGREGATION 2

// Workaround a dxc compiler bug: incorrect id generated
static const uint Const_1 = 1;

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(
    const uint  svGroupIndex    : SV_GroupIndex,
    const uint3 svGroupId       : SV_GroupID,
    const uint3 svGroupThreadId : SV_GroupThreadID)
{
    DeclareMaxNumWorkgroupsAMDX(512, 512, 1);				// Up to 8k-by-8k resolution

    DeclareInputPayloadAMD(InputPayload, in_payload);

    DeclareOutputPayloadAMD          ("aggregation", InitializePayloadsA,  FinalizePayloadsA,  OutputPayload,        out_payload_a,  MAX_ALLOCATIONS);
    DeclareOutputPayloadSharedWithAMD("fixed_exp",   InitializePayloadsFE, FinalizePayloadsFE, OutputPayload,        out_payload_fe, Const_1, out_payload_a);
    DeclareOutputPayloadSharedWithAMD("dynamic_exp", InitializePayloadsDE, FinalizePayloadsDE, OutputPayloadDynamic, out_payload_de, Const_1, out_payload_a);

    const int2 coord = int2(TILE_SIZE * svGroupId.xy + svGroupThreadId.xy);

    // Pick a different node type per workgroup
    const uint nodeType = (svGroupId.x + in_payload.grid_size_x * svGroupId.y) % 3;

    // We only have one array entry per node and always allocate 1 record per invocation or workgroup
    const uint recordCount = 1;
    const uint shaderIndex = 0;

    // Payload initialization must be uniform within a workgroup, i.e. every invocation must take the same branch.
    if (nodeType == NODE_FIXED_EXP)
    {
        InitializePayloadsFE(out_payload_fe, ScopeWorkgroup, recordCount, shaderIndex);

        if (svGroupIndex == 0)
        {
            out_payload_fe[0].coord = coord;
        }
    }
    else if (nodeType == NODE_DYNAMIC_EXP)
    {
        InitializePayloadsDE(out_payload_de, ScopeWorkgroup, recordCount, shaderIndex);

        if (svGroupIndex == 0)
        {
            // Dynamic expansion is always a (1, 1, 1) grid for the sake of simplicity.
            out_payload_de[0].grid_size = uint3(1, 1, 1);
            out_payload_de[0].coord     = coord;
        }
    }
    else if (nodeType == NODE_AGGREGATION)
    {
        InitializePayloadsA(out_payload_a, ScopeInvocation, recordCount, shaderIndex);
        out_payload_a[0].coord = coord;

        // We should release per-invocation allocated payloads, otherwise we may delay the processing
        // needlessly or even cause a hang.
        FinalizePayloadsA(out_payload_a);
    }

    // Payload release is implicit
}
