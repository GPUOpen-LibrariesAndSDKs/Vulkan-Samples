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

struct PsInput
{
    float2 position : POSITION;
};

struct PsOutput
{
    uint   material : SV_Target0;
	float2 texCoord : SV_Target2;
};

[[vk::binding(1, 0)]] [[vk::combinedImageSampler]] Texture2D<uint> inMaterialId;
[[vk::binding(1, 0)]] [[vk::combinedImageSampler]] SamplerState    inMaterialIdSampler;

PsOutput main(const PsInput input)
{
    PsOutput output;

	float2 uv  = 0.5 * (input.position + 1.0);
	output.material = inMaterialId.Sample(inMaterialIdSampler, uv).r;
	output.texCoord = uv;

	return output;
}
