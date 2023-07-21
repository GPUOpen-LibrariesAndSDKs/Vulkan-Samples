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

#define TILE_SIZE 			16
#ifdef NODE_AGGREGATION
#define MAX_ALLOCATIONS 	256	// TILE_SIZE*TILE_SIZE
#else
#define MAX_ALLOCATIONS		1	// 1 payload per tile
#endif

typedef vector<uint16_t, 2> uint2_16;

struct OutputPayload
{
#ifdef NODE_DYNAMIC_EXPANSION
    uint3       grid_size;
#endif
    uint2_16    coord;		// tile or pixel coordinates
};

// All shaders must use the same resource bindings (it's the same pipeline)

[[vk::binding(2, 0)]] Texture2D<uint> inMaterial;

// NumSubgroups (number of waves in a threadgroup) is not a constant,
// but we can allocate a large enough array. The subgroup will be at least 32 invocations.
groupshared uint sharedPerSubgroupMask[(TILE_SIZE*TILE_SIZE)/32];
groupshared uint sharedShaderIndex;

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(
    const uint  svGroupIndex    : SV_GroupIndex,
    const uint3 svGroupId       : SV_GroupID,
    const uint3 svGroupThreadId : SV_GroupThreadID)
{
    DeclareMaxNumWorkgroupsAMDX(512, 512, 1);	// Up to 8k-by-8k resolution
    DeclareOutputPayloadAMD("compose", InitializePayloads, FinalizePayloads, OutputPayload, out_payload, MAX_ALLOCATIONS);

	const int3 coord    = int3(TILE_SIZE * svGroupId.xy + svGroupThreadId.xy, 0);	// Z is mip level
	const uint material = inMaterial.Load(coord).r;		// zero-based material index

#ifdef NODE_AGGREGATION
	const uint recordCount  = 1;		// one record per invocation
	const uint shaderIndex  = material;	// use the shader corresponding to that material ID

	InitializePayloads(out_payload, ScopeInvocation, recordCount, shaderIndex);

	out_payload[0].coord = uint2_16(coord.xy);

	FinalizePayloads(out_payload);

#else // not NODE_AGGREGATION

	const uint waveId   = svGroupIndex / WaveGetLaneCount();
	const uint numWaves = (TILE_SIZE*TILE_SIZE) / WaveGetLaneCount();

	sharedPerSubgroupMask[waveId] = WaveActiveBitOr(material);

	GroupMemoryBarrierWithGroupSync();

	if (svGroupIndex == 0)
	{
		sharedShaderIndex = 0;

		for (int i = 0; i < numWaves; ++i)
		{
			sharedShaderIndex = sharedShaderIndex | sharedPerSubgroupMask[i];
		}
		// sharedShaderIndex corresponds to the common mask of all materials in this tile
	}

	GroupMemoryBarrierWithGroupSync();

	const uint recordCount = 1;	// only one payload

	InitializePayloads(out_payload, ScopeWorkgroup, recordCount, sharedShaderIndex);

    if (svGroupIndex == 0)
    {
#ifdef NODE_DYNAMIC_EXPANSION
        out_payload[0].grid_size = uint3(1, 1, 1);
#endif
        out_payload[0].coord = uint2_16(svGroupId.xy);
    }

	FinalizePayloads(out_payload);

#endif // not NODE_AGGREGATION
}
