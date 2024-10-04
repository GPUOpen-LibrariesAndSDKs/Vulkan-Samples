//  Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.
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

struct UboData
{
    float4x4 projection;
    float4x4 modelview;
    float4x4 inverseProjModelView;
    float4   lightPos;
};

float3 toSrgb(float3 linearColor)
{
    // From Khronos Data Format spec, 13.3.2 sRGB EOTF-1
    const float InvGamma = 0.4166666666666667;  // 1.0 / 2.4
    float3 low    = 12.92 * linearColor;
    float3 high   = 1.055 * pow(linearColor, float3(InvGamma, InvGamma, InvGamma)) - 0.055;
    bool3  select = bool3(step(0.0031308, linearColor));
    return lerp(low, high, select);
}

float3 calculateModelColor(
    UboData ubo,
    float3 	albedo,
    float3 	normal,
    int2 	coord,
    float 	depth,
    int2    viewSize)
{
    float2 uv  = float2(coord) / float2(viewSize);
    float4 ndc = float4(2.0 * uv - 1.0, depth, 1.0);
    float4 pos = mul(ubo.inverseProjModelView, ndc);
    pos /= pos.w;

    float3 viewPos = mul(ubo.modelview, pos).xyz;
    float3x3 modelview3x3 = float3x3(ubo.modelview[0].xyz, ubo.modelview[1].xyz, ubo.modelview[2].xyz);
    float3 L = normalize(mul(modelview3x3, ubo.lightPos.xyz) - viewPos);
    float3 V = normalize(-viewPos);
    float3 R = reflect(-L, normal);
    float3 diffuse = max(dot(normal, L), 0.0) * albedo;
    float3 specular = pow(max(dot(R, V), 0.0), 32.0) * float3(1, 1, 1);

    return diffuse + specular;
}

float3 calculateBackgroundColor(UboData ubo, int2 coord, int2 viewSize)
{
    float2 uv = float2(coord) / float2(viewSize);
    float2 p  = 2.0 * uv - 1.0;
    p = (mul(ubo.projection, float4(2.0*p, 0.0, 1.0))).xy;
    p.y -= 2.2;
    float r = length(p);

    const float PI = 3.141592653589793;
    const float t  = 0.4;
    float3 color = float3(1, 1, 1);
    color = (r < PI) ? float3(cos(t*r), cos(t*r), cos(t*r)) : float3(cos(t*PI), cos(t*PI), cos(t*PI));

    return color;
}
