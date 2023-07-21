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

#version 450
#extension GL_GOOGLE_include_directive: require

#include "common.glsl.h"

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoord;
layout (location = 3) in flat uint inVertexIndex;
layout (location = 4) in flat uint inInstanceIndex;

layout (location = 0) out uint outMaterial;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec2 outTexCoord;

layout(constant_id = 0) const int MaterialCount = 1;
layout(constant_id = 1) const int InstanceCount = 1;

void main()
{
	if (InstanceCount == 1)
	{
		outMaterial = (inVertexIndex / 6) % MaterialCount;
	}
	else
	{
		outMaterial = inInstanceIndex % MaterialCount;
	}
	outNormal   = vec4(0.5 * (normalize(inNormal) + 1.0), 0.0);
	outTexCoord = inTexCoord;
}