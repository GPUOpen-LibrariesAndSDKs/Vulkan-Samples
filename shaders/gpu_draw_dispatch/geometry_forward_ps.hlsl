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

#include "common.hlsl"

[[vk::binding(0, 0)]]
cbuffer cbUbo
{
    UboData ubo;
};

struct PsInput
{
    float4 svPosition : SV_POSITION;
    float3 position   : POSITION;
    float3 normal     : NORMAL;
    float3 texCoord   : TEXCOORD;
    float3 color      : COLOR;
};

struct PsOutput
{
    float4 fragColor : SV_Target0;
};

PsOutput main(const PsInput input)
{
    PsOutput output;

    float3x3 modelview3x3 = float3x3(
        ubo.modelview[0].xyz,
        ubo.modelview[1].xyz,
        ubo.modelview[2].xyz);

    float4 pos = mul(ubo.modelview, float4(input.position, 1.0));
    float3 lPos = mul(modelview3x3, ubo.lightPos.xyz);
    float3 lightVec = lPos - pos.xyz;
    float3 viewVec = -pos.xyz;

    float3 N = normalize(input.normal);
    float3 L = normalize(lightVec);
    float3 V = normalize(viewVec);
    float3 R = reflect(-L, N);

    float3 diffuse = mul(input.color, max(dot(N, L), 0.0));
    float3 specular = mul(float3(1, 1, 1), pow(max(dot(R, V), 0.0), 32.0));

    output.fragColor = float4(toSrgb(diffuse + specular), 1.0);

    return output;
}
