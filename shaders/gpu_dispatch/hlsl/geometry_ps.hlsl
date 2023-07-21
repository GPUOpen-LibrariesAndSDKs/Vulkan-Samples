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

struct PsInput
{
	float3               position      : POSITION;
    float3               normal        : NORMAL;
	float2               texCoord      : TEXCOORD0;
	nointerpolation uint vertexIndex   : TEXCOORD1;
	nointerpolation uint instanceIndex : TEXCOORD2;
};

struct PsOutput
{
	uint   material : SV_Target0;
	float4 normal   : SV_Target1;
	float2 texCoord : SV_Target2;
};

[[vk::constant_id(0)]] const int MaterialCount = 1;
[[vk::constant_id(1)]] const int InstanceCount = 1;

PsOutput main(const PsInput input)
{
    PsOutput output;

	if (InstanceCount == 1)
	{
		output.material = (input.vertexIndex / 6) % MaterialCount;
	}
	else
	{
		output.material = input.instanceIndex % MaterialCount;
	}
	output.normal   = float4(0.5 * (normalize(input.normal) + 1.0), 0.0);
	output.texCoord = input.texCoord;

	return output;
}