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

[[vk::constant_id(0)]] const uint ViewWidth = 1;
[[vk::constant_id(1)]] const uint ViewHeight = 1;
[[vk::constant_id(2)]] const uint NumMaterials = 0;
[[vk::constant_id(3)]] const uint NumTexturesPerMaterial = 0;
[[vk::constant_id(4)]] const uint ShaderPermutation = 0;
[[vk::constant_id(5)]] const uint AluComplexity = 0;
[[vk::constant_id(6)]] const bool UseTextureArray = false;

#define TILE_SIZE      16
#define BACKGROUND_BIT 0
#define MODEL_BIT_BASE 1

static const uint BackgroundMask = (1u << BACKGROUND_BIT);
static const uint ModelMask      = ((1u << NumMaterials) - 1) << MODEL_BIT_BASE;

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

[[vk::image_format("rgba8")]] [[vk::binding(1, 0)]] RWTexture2D<float4> outImage;

[[vk::binding(2, 0)]] Texture2D<uint>   inMaterial;
[[vk::binding(3, 0)]] Texture2D<float3> inNormal;
[[vk::binding(4, 0)]] Texture2D<float2> inTexCoord;
[[vk::binding(5, 0)]] Texture2D<float>  inDepth;

[[vk::binding(6, 0)]] Texture2D<float4> inTextureArray[];
[[vk::binding(6, 0)]] SamplerState      inTextureSamplerArray[];

float3 mixTextureColor(float3 color, uint material, int3 coord)
{
    uint baseIndex = material * NumTexturesPerMaterial;
    float2 texCoord = inTexCoord.Load(coord).xy;
    float4 texColor = float4(0, 0, 0, 0);

    if (UseTextureArray)
    {
        for (uint i = 0; i < NumTexturesPerMaterial; ++i)
        {
            texColor += inTextureArray[NonUniformResourceIndex(baseIndex + i)].SampleLevel(
                inTextureSamplerArray[NonUniformResourceIndex(baseIndex + i)],
                texCoord,
                0);
        }
    }

    return lerp(color, texColor.rgb / float(NumTexturesPerMaterial), 0.2);
}

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
        const int2 coord    = int2(in_payload[svGroupIndex].coord);
#else
        const int2 coord    = int2(TILE_SIZE * in_payload.coord + svGroupThreadId.xy);
#endif
        const int3 coord3   = int3(coord, 0);     // Z is mip level
        const int2 viewSize = int2(ViewWidth, ViewHeight);

        float3 finalColor;

        // Background only
        if (ShaderPermutation == BackgroundMask)
        {
            finalColor = calculateBackgroundColor(ubo, coord, viewSize, AluComplexity);
        }
        // No background, model only
        else if (((ShaderPermutation & BackgroundMask) == 0) &&
                 ((ShaderPermutation & ModelMask)      != 0))
        {
            const float  depth    = inDepth.Load(coord3).r;
            const uint   material = inMaterial.Load(coord3).r;
            const float3 albedo   = getPaletteColor(material).rgb;
            const float3 normal   = normalize(2.0 * inNormal.Load(coord3).rgb - 1.0);

            finalColor = calculateModelColor(ubo, albedo, normal, coord, depth, viewSize, AluComplexity);

            if (NumTexturesPerMaterial != 0)
            {
                finalColor = mixTextureColor(finalColor, material, coord3);
            }
        }
        // Background and model
        else if ((ShaderPermutation & (ModelMask | BackgroundMask)) != 0)
        {
            const float depth = inDepth.Load(coord3).r;

            if (depth > 0.0) {
                const uint   material = inMaterial.Load(coord3).r;
                const float3 albedo   = getPaletteColor(material).rgb;
                const float3 normal   = normalize(2.0 * inNormal.Load(coord3).rgb - 1.0);

                finalColor = calculateModelColor(ubo, albedo, normal, coord, depth, viewSize, AluComplexity);

                if (NumTexturesPerMaterial != 0)
                {
                    finalColor = mixTextureColor(finalColor, material, coord3);
                }
            }
            else
            {
                finalColor = calculateBackgroundColor(ubo, coord, viewSize, AluComplexity);
            }
        }
        else
        {
            // Output red if the permutation is not handled.
            finalColor = float3(1, 0 , 0);
        }

        // Visual feedback for a specialized shader
        if ((ubo.highlightedShaderPermutation != 0) && (ShaderPermutation == ubo.highlightedShaderPermutation))
        {
            finalColor.g = 1.0;
        }

	    outImage[coord.xy] = float4(toSrgb(finalColor), 1.0);
    }
}
