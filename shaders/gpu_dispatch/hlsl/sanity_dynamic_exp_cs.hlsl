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
#include "common.hlsl"

#define TILE_SIZE 16

[[vk::image_format("rgba8")]] [[vk::binding(0, 0)]] RWTexture2D<float4> outImage;

struct InputPayload
{
	uint3   grid_size;
	uint2   coord;		// tile or pixel coordinates
};

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(
	const uint3 svGroupThreadId : SV_GroupThreadID)
{
	DeclareMaxNumWorkgroupsAMDX(1, 1, 1);
	DeclareInputPayloadAMD(InputPayload, in_payload);

	const int2   coord = int2(in_payload.coord + svGroupThreadId.xy);
	const float2 uv    = float2(svGroupThreadId.xy) / TILE_SIZE;

	// Dynamic expansion creates a green-shaded tile.
	float3 color = float3(0, 0.5 * (uv.x + uv.y), 0);

	outImage[coord] = float4(toSrgb(float3(color)), 1);
}
