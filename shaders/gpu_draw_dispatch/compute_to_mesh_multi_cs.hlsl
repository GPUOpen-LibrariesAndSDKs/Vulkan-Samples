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

[[vk::constant_id(0)]] const uint MaxPayloads = 1;
[[vk::constant_id(1)]] const uint WorkgroupSizeX = 1;

[[vk::push_constant]] struct PushConstants
{
    uint nodeIndex;
    uint numPayloads;
} pc;

struct Payload
{
    uint3 grid_size : SV_DispatchGrid;
};

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(1, 1, 1)]
[NodeDispatchGrid(WorkgroupSizeX, 1, 1)]
[NodeIsProgramEntry]
void main(
    DispatchNodeInputRecord<Payload> inputRecord,

    [NodeID("main")]
    [MaxRecords(MaxPayloads)]
    NodeOutputArray<Payload> nodeOutput)
{
#ifdef USE_INPUT_SHARING
    GroupNodeOutputRecords<Payload> outputRecord = nodeOutput[0].GetGroupNodeOutputRecords(1);
    outputRecord.Get() = inputRecord.Get();
    outputRecord.OutputComplete();
#else
    const uint gfxNumPayloads = pc.numPayloads;
    const uint gfxNodeIndex   = pc.nodeIndex;

    for (uint i = 0; i < gfxNumPayloads; i++)
    {
        uint index = i;
        if (gfxNodeIndex < MaxPayloads)
        {
             index = gfxNodeIndex;
        }

        GroupNodeOutputRecords<Payload> outputRecord = nodeOutput[index].GetGroupNodeOutputRecords(1);
        outputRecord.Get() = inputRecord.Get();
        outputRecord.OutputComplete();
    }
#endif
}
