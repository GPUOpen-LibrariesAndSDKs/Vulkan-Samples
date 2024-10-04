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

#include "common.hlsl"

[[vk::binding(0, 0)]]
cbuffer cbUbo
{
    UboData ubo;
};

[[vk::constant_id(0)]] const uint ShaderIndex = 0;

struct s_vertex
{
    float4 position;
    float4 normal;
};

struct s_meshlet
{
    uint unique_indices[64];
    uint primitive_indices[126 * 3];
    uint unique_index_count;
    uint primitive_index_count;
};

[[vk::binding(1, 0)]] StructuredBuffer<s_meshlet> Meshlets;
[[vk::binding(2, 0)]] StructuredBuffer<s_vertex> Vertices;

struct VertexOut
{
    float4 svPosition : SV_POSITION;
    float3 position   : POSITION;
    float3 normal     : NORMAL;
    float3 texCoord   : TEXCOORD;
    float3 color      : COLOR;
};

struct InputPayload
{
    uint3 grid_size : SV_DispatchGrid;
};

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
[Shader("node")]
[NodeID("main", ShaderIndex)]
[NodeLaunch("mesh")]
[NodeMaxDispatchGrid(512, 1, 1)]
[NodeIsProgramEntry]
#ifdef USE_INPUT_SHARING
[NodeShareInputOf("main", 0)]
#endif
void main(
    DispatchNodeInputRecord<InputPayload> nodeInput,
    uint thread_id : SV_GroupThreadID,
    uint meshlet_index : SV_GroupID,
    out indices uint3 tris[126],
    out vertices VertexOut verts[64])
{
    uint vertex_count = Meshlets[meshlet_index].unique_index_count;
    uint primitive_count = Meshlets[meshlet_index].primitive_index_count / 3;

    SetMeshOutputCounts(vertex_count, primitive_count);

    float3x3 modelview3x3 = float3x3(
        ubo.modelview[0].xyz,
        ubo.modelview[1].xyz,
        ubo.modelview[2].xyz);

    if (thread_id < vertex_count)
    {
        uint vi = Meshlets[meshlet_index].unique_indices[thread_id];

        verts[thread_id].svPosition = mul(mul(ubo.projection, ubo.modelview), float4(Vertices[vi].position.x + (float(ShaderIndex) / 10.0), Vertices[vi].position.yz, 1.0f));
        verts[thread_id].position = float3(Vertices[vi].position.x + (float(ShaderIndex) / 10.0), Vertices[vi].position.yz);
        verts[thread_id].normal = mul(modelview3x3, Vertices[vi].normal.xyz);

        if (ShaderIndex == 0)
        {
            verts[thread_id].color = float3(0.5, 0.8, 0.25);
        }
        else if (ShaderIndex == 1)
        {
            verts[thread_id].color = float3(0.9, 0.0, 0.0);
        }
        else if (ShaderIndex == 2)
        {
            verts[thread_id].color = float3(0.0, 0.9, 0.0);
        }
        else
        {
            verts[thread_id].color = float3((ShaderIndex % 11) / 10.0,
                                (ShaderIndex % 7)  / 6.0,
                                (ShaderIndex % 5)  / 4.0);
        }
    }

    if (thread_id < primitive_count)
    {
        uint vi1 = Meshlets[meshlet_index].primitive_indices[thread_id * 3];
        uint vi2 = Meshlets[meshlet_index].primitive_indices[thread_id * 3 + 1];
        uint vi3 = Meshlets[meshlet_index].primitive_indices[thread_id * 3 + 2];

        tris[thread_id] = uint3(vi1, vi2, vi3);
    }
}
