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

// Helper header for SPV_AMDX_shader_enqueue.
// Use it to avoid having to declare the huge SPIR-V intrinsic syntax in all your shaders.
// Functions/Macros are listed below with a small example at the end of the file.

// How to use
// ----------
// See examples at the end of the file.
// All macros should be used inside the shader entry point function (e.g. main()).

// SPIR-V enums, used by the definitions below.
#define SPIRV_NodeSharesPayloadLimitsWithAMDX     5019   // Decoration
#define SPIRV_NodeMaxPayloadsAMDX                 5020   // Decoration
#define SPIRV_CoalescedInputCountAMDX             5021   // BuiltIn
#define SPIRV_ShaderEnqueueAMDX                   5067   // Capability
#define SPIRV_NodePayloadAMDX                     5068   // Storage Class
#define SPIRV_CoalescingAMDX                      5069   // Execution Mode
#define SPIRV_MaxNodeRecursionAMDX                5071   // Execution Mode
#define SPIRV_StaticNumWorkgroupsAMDX             5072   // Execution Mode
#define SPIRV_ShaderIndexAMDX                     5073   // Execution Mode, BuiltIn
#define SPIRV_OpFinalizeNodePayloadsAMDX          5075   // Instruction
#define SPIRV_NodeOutputPayloadAMDX               5076   // Storage Class
#define SPIRV_MaxNumWorkgroupsAMDX                5077   // Execution Mode
#define SPIRV_TrackFinishWritingAMDX              5078   // Decoration
#define SPIRV_OpFinishWritingNodePayloadAMDX      5078   // Instruction (reuse opcode)
#define SPIRV_OpInitializeNodePayloadsAMDX        5090   // Instruction
#define SPIRV_PayloadNodeNameAMDX                 5091   // Decoration

// Used by OpInitializeNodePayloadsAMDX to decide the scope of allocation.
#define ScopeWorkgroup  2
#define ScopeInvocation 4

// ---------------------------------------------------------------------------------------------------------------------
// Generic node properties
// ---------------------------------------------------------------------------------------------------------------------

// Set the index used by this node shader. Otherwise, the default index is 0.
#define DeclareShaderIndexAMDX(_index_) \
    [[vk::ext_capability(SPIRV_ShaderEnqueueAMDX)]] \
    [[vk::ext_extension("SPV_AMDX_shader_enqueue")]] \
    vk::ext_execution_mode_id(SPIRV_ShaderIndexAMDX, _index_)

// Declares the built-in ShaderIndexAMDX with the given variable name (read only).
// It can be used to read the shader index used by this node shader.
#define DeclareBuiltInShaderIndexAMDX(_name_) \
    [[vk::ext_decorate(/*BuiltIn*/ 11, SPIRV_ShaderIndexAMDX)]] \
    [[vk::ext_storage_class(/* Input */ 1)]] \
    uint _name_

// Declares that this shader is coalescing.
#define DeclareCoalescingAMDX() \
    [[vk::ext_capability(SPIRV_ShaderEnqueueAMDX)]] \
    [[vk::ext_extension("SPV_AMDX_shader_enqueue")]] \
    vk::ext_execution_mode(SPIRV_CoalescingAMDX)

// Declares the maximum recursion depth for this shader. Only required if the shader enqueues itself.
#define DeclareMaxNodeRecursionAMDX(_count_) \
    [[vk::ext_capability(SPIRV_ShaderEnqueueAMDX)]] \
    [[vk::ext_extension("SPV_AMDX_shader_enqueue")]] \
    vk::ext_execution_mode_id(SPIRV_MaxNodeRecursionAMDX, _count_)

// Declares the dispatch size of this shader (number of workgroups) in X, Y, Z dimensions.
// Must not be used together with CoalescingAMDX.
#define DeclareStaticNumWorkgroupsAMDX(_x_, _y_, _z_) \
    [[vk::ext_capability(SPIRV_ShaderEnqueueAMDX)]]  \
    [[vk::ext_extension("SPV_AMDX_shader_enqueue")]] \
    vk::ext_execution_mode_id(SPIRV_StaticNumWorkgroupsAMDX, _x_, _y_, _z_)

// Declares the maximum dynamic dispatch size for this shader (defined by the payload) in X, Y, Z dimensions.
// Must not be used together with CoalescingAMDX or StaticNumWorkgroupsAMDX.
#define DeclareMaxNumWorkgroupsAMDX(_x_, _y_, _z_) \
    [[vk::ext_capability(SPIRV_ShaderEnqueueAMDX)]] \
    [[vk::ext_extension("SPV_AMDX_shader_enqueue")]] \
    vk::ext_execution_mode_id(SPIRV_MaxNumWorkgroupsAMDX, _x_, _y_, _z_)

// ---------------------------------------------------------------------------------------------------------------------
// Receive an input payload
// ---------------------------------------------------------------------------------------------------------------------

// Declares an input payload variable with the specified structure type and name.
// Must be declared at the global scope.
#define DeclareInputPayloadAMD(_payload_type_, _payload_name_) \
    [[vk::ext_storage_class(SPIRV_NodePayloadAMDX)]] \
    _payload_type_ _payload_name_

#define DeclareInputPayloadWithFinishWritingAMD(_finish_writing_func_name_, _payload_type_, _payload_name_) \
    [[vk::ext_decorate(SPIRV_TrackFinishWritingAMDX)]] \
    DeclareInputPayloadAMD(_payload_type_, _payload_name_); \
    \
    [[vk::ext_capability(SPIRV_ShaderEnqueueAMDX)]] \
    [[vk::ext_extension("SPV_AMDX_shader_enqueue")]] \
    [[vk::ext_instruction(SPIRV_OpFinishWritingNodePayloadAMDX)]] \
    bool _finish_writing_func_name_( \
        [[vk::ext_reference]] _payload_type_)

// Declares an input payload array with the specified structure type and name.
// Must be declared at the global scope.
// Must only be used with CoalescingAMDX shaders.
#define DeclareInputPayloadArrayAMD(_payload_type_, _payload_name_, _payload_count_) \
    [[vk::ext_storage_class(SPIRV_NodePayloadAMDX)]] \
    _payload_type_ _payload_name_[_payload_count_]

// Declares the built-in CoalescedInputCountAMDX with the given variable name.
// Must only be used with CoalescingAMDX shaders.
#define DeclareBuiltInCoalescedInputCountAMDX(_name_) \
    [[vk::ext_decorate(/*BuiltIn*/ 11, SPIRV_CoalescedInputCountAMDX)]] \
    [[vk::ext_storage_class(/* Input */ 1)]] \
    uint _name_

// ---------------------------------------------------------------------------------------------------------------------
// Enqueue an output payload
// ---------------------------------------------------------------------------------------------------------------------

// Declares the output payload with the given type, name and count along with it's coressponding initialization and
// release functions. If the payload count must be declared dynamically, declare this with a max payload count value.
// The real count is specified while allocating the payload.
//
// Init/release functions must be called in uniform control flow of a given scope.
// The use of release is optional, all payloads are released at the end of the shader implicitly.
//
// Usage:
// Use this macro at the global scope in the shader and call your initialization and release functions in the body
// of the shader.

#define DeclareOutputPayloadAMD( \
    _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_) \
    [[vk::ext_decorate_id(SPIRV_NodeMaxPayloadsAMDX, _payload_count_)]] \
    [[vk::ext_decorate_string(SPIRV_PayloadNodeNameAMDX, _node_name_)]] \
    [[vk::ext_storage_class(SPIRV_NodeOutputPayloadAMDX)]] \
    _payload_type_ _payload_name_[_payload_count_]; \
    \
    [[vk::ext_capability(SPIRV_ShaderEnqueueAMDX)]] \
    [[vk::ext_extension("SPV_AMDX_shader_enqueue")]] \
    [[vk::ext_instruction(SPIRV_OpInitializeNodePayloadsAMDX)]] \
    void _init_func_name_( \
        [[vk::ext_reference]] _payload_type_[_payload_count_], \
        uint visibility, \
        uint payloadCount, \
        uint nodeIndex); \
    \
    [[vk::ext_capability(SPIRV_ShaderEnqueueAMDX)]] \
    [[vk::ext_extension("SPV_AMDX_shader_enqueue")]] \
    [[vk::ext_instruction(SPIRV_OpFinalizeNodePayloadsAMDX)]] \
    void _finalize_func_name_( \
        [[vk::ext_reference]] _payload_type_[_payload_count_])

// Declare and output payload array that shares its output resources with another payload variable.
// Allocated payload count will be subtracted from the other payload's NodeMaxPayloadsAMDX budget.
#define DeclareOutputPayloadSharedWithAMD( \
    _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_, _other_payload_name_) \
    [[vk::ext_decorate_id(SPIRV_NodeSharesPayloadLimitsWithAMDX, _other_payload_name_)]] \
    DeclareOutputPayloadAMD( \
        _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_)

// Same as above, but includes FinishWritingNodePayloadAMDX support.
#define DeclareOutputPayloadSharedWithFinishWritingAMD( \
    _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_, _other_payload_name_) \
    [[vk::ext_decorate_id(SPIRV_NodeSharesPayloadLimitsWithAMDX, _other_payload_name_)]] \
    [[vk::ext_decorate(SPIRV_TrackFinishWritingAMDX)]] \
    DeclareOutputPayloadAMD( \
        _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_)

// Declare and output payload array that will be used with FinishWritingNodePayloadAMDX in the receiving node shader.
#define DeclareOutputPayloadWithFinishWritingAMD( \
    _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_) \
    [[vk::ext_decorate(SPIRV_TrackFinishWritingAMDX)]] \
    DeclareOutputPayloadAMD( \
        _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_)

// ---------------------------------------------------------------------------------------------------------------------
// Example 1
// A shader that allocates two sets of payloads: "hello" and "other", with a total budged of 5 for "hello".
// "other" shares (uses) budget of "hello". At runtime, 2 payloads are allocated from "hello" and 3 from "other",
// which in total uses up the full budget.
// ---------------------------------------------------------------------------------------------------------------------
/*
#include "shaderEnqueueSpirvIntrinsics.hlsl.h"

struct MyPayload
{
    uint foo;
};

[NumThreads(1, 1, 1)]
void main()
{
    DeclareStaticNumWorkgroupsAMDX(1, 1, 1);

    DeclareOutputPayloadAMD("hello", myInitHello, myFinalizeHello, MyPayload, helloPayloads, 5);
    uint numPayloads = 2;
    uint shaderIndex = 0;
    myInitHello(helloPayloads, ScopeWorkgroup, numPayloads, shaderIndex);
    helloPayloads[0].foo = 13;
    helloPayloads[1].foo = 27;
    myFinalizeHello(helloPayloads);

    DeclareOutputPayloadSharedWithAMD("other", myInitOther, myFinalizeOther, MyPayload, otherPayloads, 3, helloPayloads);
    numPayloads = 3;
    shaderIndex = 1;
    myInitOther(otherPayloads, ScopeWorkgroup, numPayloads, shaderIndex);
    otherPayloads[0].foo = 1;
    otherPayloads[1].foo = 2;
    otherPayloads[2].foo = 3;
    myFinalizeOther(otherPayloads);
}
*/

// ---------------------------------------------------------------------------------------------------------------------
// Example 2
// A shader that accepts an input payload, writes to it, then synchronized with other workgroups.
// Then, only one of the workgroups will enqueue an output payload.
// ---------------------------------------------------------------------------------------------------------------------
/*
#include "shaderEnqueueSpirvIntrinsics.hlsl.h"

struct InPayload
{
    uint stuff;
    uint counter;
};

struct OutPayload
{
    uint alsoStuff;
};

[NumThreads(1, 1, 1)]
void main()
{
    DeclareStaticNumWorkgroupsAMDX(8, 1, 1);
    DeclareInputPayloadWithFinishWritingAMD(myFinishWriting, InPayload, input);

    InterlockedAdd(input.counter, 1);

    bool done = myFinishWriting(input);     // uniform across the workgroup

    DeclareOutputPayloadAMD("output", myInitOutput, myFinalizeOutput, OutPayload, outputs, 1);
    uint numPayloads = done ? 1 : 0;
    uint shaderIndex = 0;
    myInitOutput(outputs, ScopeWorkgroup, numPayloads, shaderIndex);

    if (numPayloads != 0)
    {
        outputs[0].alsoStuff = input.stuff + input.counter;
    }

    myFinalizeOutput(outputs);
}
*/
