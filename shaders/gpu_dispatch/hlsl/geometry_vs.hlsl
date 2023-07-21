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

#include "common.hlsl"

struct VsInput
{
	float3 position      : POSITION0;
	float3 normal        : NORMAL;
	float2 texCoord      : TEXCOORD;
	float3 instancePos   : POSITION1;
	uint   vertexIndex   : SV_VertexID;
	uint   instanceIndex : SV_InstanceID;
};

[[vk::binding(0, 0)]]
cbuffer cbUbo
{
	UboData ubo;
};

struct VsOutput
{
	float4 svPosition    : SV_Position;
	float3 position      : POSITION;
    float3 normal        : NORMAL;
	float2 texCoord      : TEXCOORD0;
	uint   vertexIndex   : TEXCOORD1;
	uint   instanceIndex : TEXCOORD2;
};

VsOutput main(const VsInput input)
{
    VsOutput output;

	float3 pos        = input.instancePos + input.position;
	output.svPosition = mul(mul(ubo.projection, ubo.modelview), float4(pos, 1.0));

	float3x3 modelview3x3 = float3x3(
		ubo.modelview[0].xyz,
		ubo.modelview[1].xyz,
		ubo.modelview[2].xyz);

	output.position      = pos;
	output.normal        = mul(modelview3x3, input.normal);
	output.texCoord      = input.texCoord;
	output.vertexIndex   = input.vertexIndex;
	output.instanceIndex = input.instanceIndex;

	return output;
}