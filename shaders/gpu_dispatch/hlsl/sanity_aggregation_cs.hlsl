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
	uint2 coord;		// tile or pixel coordinates
};

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(
	const uint svGroupIndex : SV_GroupIndex)
{
	DeclareCoalescingAMDX();
	DeclareBuiltInCoalescedInputCountAMDX(inputPayloadCount);
	DeclareInputPayloadArrayAMD(InputPayload, in_payload, TILE_SIZE * TILE_SIZE);

	if (svGroupIndex < inputPayloadCount)
	{
		const int2   coord = int2(in_payload[svGroupIndex].coord);
		const float2 uv    = float2(coord % TILE_SIZE) / TILE_SIZE;

		// Aggregation creates a white-shaded tile.
		float color = 0.5 * (uv.x + uv.y);

		outImage[coord] = float4(toSrgb(float3(color, color, color)), 1);
	}
}
