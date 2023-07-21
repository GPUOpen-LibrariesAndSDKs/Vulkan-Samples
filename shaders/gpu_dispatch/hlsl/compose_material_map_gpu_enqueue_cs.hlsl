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

[[vk::constant_id(4)]] const uint ShaderPermutation = 0;
[[vk::constant_id(5)]] const uint AluComplexity = 0;

typedef vector<uint16_t, 2> uint2_16;

struct InputPayload
{
#ifdef NODE_DYNAMIC_EXPANSION
    uint3   	grid_size;
#endif
    uint2_16 	coord;		// tile or pixel coordinates
};

// All shaders must use the same resource bindings (it's the same pipeline)

[[vk::binding(0, 0)]]
cbuffer cbUbo
{
    UboData ubo;
};

[[vk::binding(1, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> outImage;
[[vk::binding(2, 0)]] Texture2D<uint> inMaterial;

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(
    const uint  svGroupIndex    : SV_GroupIndex,
    const uint3 svGroupThreadId : SV_GroupThreadID)
{
#ifdef NODE_AGGREGATION
    DeclareCoalescingAMDX();
    DeclareBuiltInCoalescedInputCountAMDX(inputPayloadCount);
    DeclareInputPayloadArrayAMD(InputPayload, in_payload, TILE_SIZE * TILE_SIZE);
#else
#ifdef NODE_DYNAMIC_EXPANSION
    DeclareMaxNumWorkgroupsAMDX(1, 1, 1);
#else
    DeclareStaticNumWorkgroupsAMDX(1, 1, 1);
#endif
    DeclareInputPayloadAMD(InputPayload, in_payload);
#endif

#ifdef NODE_AGGREGATION
    if (svGroupIndex < inputPayloadCount)
#endif
    {
#ifdef NODE_AGGREGATION
        const int2 coord  = int2(in_payload[svGroupIndex].coord);
#else
        const int2 coord  = int2(TILE_SIZE * in_payload.coord + svGroupThreadId.xy);
#endif

        // Display the original material map (albeit with different color palette).
        const uint material   = inMaterial.Load(int3(coord, 0)).r;  // Z is mip level
        float3     finalColor = getMaterialColor(material).rgb;

        // Active branches -- should be compiled out if not needed based on the specialization constant.
        if (ShaderPermutation < 256u)
        {
            const float complexity = AluComplexity / 8.0;
            float noise = 0.0;

            if ((ShaderPermutation & 1u) != 0)		// bit 0
            {
                noise += abs(iterateNoise(float3(coord, 0.1), complexity));
            }
            if ((ShaderPermutation & 2u) != 0)		// bit 1
            {
                noise += abs(iterateNoise(float3(coord, 0.12), complexity));
            }
            if ((ShaderPermutation & 4u) != 0)		// bit 2
            {
                noise += abs(iterateNoise(float3(coord, 0.45), complexity));
            }
            if ((ShaderPermutation & 8u) != 0)		// bit 3
            {
                noise += abs(iterateNoise(float3(coord, 0.75), complexity));
            }
            if ((ShaderPermutation & 16u) != 0)		// bit 4
            {
                noise += abs(iterateNoise(float3(coord, 0.98), complexity));
            }
            if ((ShaderPermutation & 32u) != 0)		// bit 5
            {
                noise += abs(iterateNoise(float3(coord, 0.27), complexity));
            }
            if ((ShaderPermutation & 64u) != 0)		// bit 6
            {
                noise += abs(iterateNoise(float3(coord, 0.63), complexity));
            }
            if ((ShaderPermutation & 128u) != 0)	// bit 7
            {
                noise += abs(iterateNoise(float3(coord, 0.32), complexity));
            }

            finalColor *= 0.95 + 0.05 * abs(sin(noise));

            // Visual feedback for a specialized shader
            if (ShaderPermutation == ubo.highlightedShaderPermutation)
            {
                finalColor.g = 1.0;
            }
        }
        else
        {
            // ERROR, we're handling only up to 256 permutations
            finalColor = float3(1, 0, 0);
        }

        // Visual feedback for a specialized shader
        if (ShaderPermutation == ubo.highlightedShaderPermutation)
        {
            finalColor.g = 1.0;
        }

        outImage[coord] = float4(toSrgb(finalColor), 1.0);
    }
}
