//  Copyright (c) 2023-2024 Advanced Micro Devices, Inc. All Rights Reserved.
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

#define TILE_SIZE	    16
#define MAX_ALLOCATIONS 256	// TILE_SIZE*TILE_SIZE

struct InputPayload
{
    uint3  grid_size : SV_DispatchGrid;
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

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeIsProgramEntry]
[NodeMaxDispatchGrid(512, 512, 1)]
[NumThreads(TILE_SIZE, TILE_SIZE, 1)]
void main(
    const uint  svGroupIndex    : SV_GroupIndex,
    const uint3 svGroupId       : SV_GroupID,
    const uint3 svGroupThreadId : SV_GroupThreadID,

    DispatchNodeInputRecord<InputPayload> in_payload,

    [MaxRecords(MAX_ALLOCATIONS)]
    [NodeID("aggregation")]
    NodeOutput<OutputPayload> out_a,

    [MaxRecordsSharedWith(out_a)]
    [NodeID("fixed_exp")]
    NodeOutput<OutputPayload> out_fe,

    [MaxRecordsSharedWith(out_a)]
    [NodeID("dynamic_exp")]
    NodeOutput<OutputPayloadDynamic> out_de)
{
    const int2 coord = int2(TILE_SIZE * svGroupId.xy + svGroupThreadId.xy);

    // Pick a different node type per workgroup
    const uint nodeType = (svGroupId.x + in_payload.Get().grid_size.x * svGroupId.y) % 3;

    // We only have one array entry per node and always allocate 1 record per invocation or workgroup
    const uint recordCount = 1;

    // Payload initialization must be uniform within a workgroup, i.e. every invocation must take the same branch.
    if (nodeType == NODE_FIXED_EXP)
    {
        GroupNodeOutputRecords<OutputPayload> out_payload_fe = out_fe.GetGroupNodeOutputRecords(recordCount);

        if (svGroupIndex == 0)
        {
            out_payload_fe[0].coord = coord;
        }

        out_payload_fe.OutputComplete();
    }
    else if (nodeType == NODE_DYNAMIC_EXP)
    {
        GroupNodeOutputRecords<OutputPayloadDynamic> out_payload_de = out_de.GetGroupNodeOutputRecords(recordCount);

        if (svGroupIndex == 0)
        {
            // Dynamic expansion is always a (1, 1, 1) grid for the sake of simplicity.
            out_payload_de[0].grid_size = uint3(1, 1, 1);
            out_payload_de[0].coord     = coord;
        }

        out_payload_de.OutputComplete();
    }
    else if (nodeType == NODE_AGGREGATION)
    {
        ThreadNodeOutputRecords<OutputPayload> out_payload_a = out_a.GetThreadNodeOutputRecords(recordCount);

        out_payload_a[0].coord = coord;

        out_payload_a.OutputComplete();
    }
}
