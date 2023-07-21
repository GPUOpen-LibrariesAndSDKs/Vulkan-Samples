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

#extension GL_EXT_spirv_intrinsics : require

// Helper header for VK_AMDX_shader_enqueue.
// Use it to avoid having to declare the huge SPIR-V intrinsic syntax in all your shaders.
// Functions/Macros are listed below with a small example at the end of the file.

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
    spirv_execution_mode_id( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_ShaderIndexAMDX, _index_)

// Declares the built-in ShaderIndexAMDX with the given variable name (read only).
// It can be used to read the shader index used by this node shader.
#define DeclareBuiltInShaderIndexAMDX(_name_) \
    spirv_decorate( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        11 /* BuiltIn */, SPIRV_ShaderIndexAMDX) \
    spirv_storage_class(1 /*Input*/) uint _name_

// Declares that this shader is coalescing.
#define DeclareCoalescingAMDX() \
    spirv_execution_mode( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_CoalescingAMDX)

// Declares the maximum recursion depth for this shader. Only required if the shader enqueues itself.
#define DeclareMaxNodeRecursionAMDX(_count_) \
    spirv_execution_mode_id( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_MaxNodeRecursionAMDX, _count_)

// Declares the dispatch size of this shader (number of workgroups) in X, Y, Z dimensions.
// Must not be used together with CoalescingAMDX.
#define DeclareStaticNumWorkgroupsAMDX(_x_, _y_, _z_) \
    spirv_execution_mode_id( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_StaticNumWorkgroupsAMDX, _x_, _y_, _z_)

// Declares the maximum dynamic dispatch size for this shader (defined by the payload) in X, Y, Z dimensions.
// Must not be used together with CoalescingAMDX or StaticNumWorkgroupsAMDX.
#define DeclareMaxNumWorkgroupsAMDX(_x_, _y_, _z_) \
    spirv_execution_mode_id( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_MaxNumWorkgroupsAMDX, _x_, _y_, _z_)

// ---------------------------------------------------------------------------------------------------------------------
// Receive an input payload
// ---------------------------------------------------------------------------------------------------------------------

// Declares an input payload variable with the specified structure type and name.
// Must be declared at the global scope.
#define DeclareInputPayloadAMD(_payload_type_, _payload_name_) \
    spirv_storage_class( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_NodePayloadAMDX) \
    _payload_type_ _payload_name_

#define DeclareInputPayloadWithFinishWritingAMD(_finish_writing_func_name_, _payload_type_, _payload_name_) \
    spirv_decorate( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_TrackFinishWritingAMDX) \
    DeclareInputPayloadAMD(_payload_type_, _payload_name_); \
    \
    spirv_instruction( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        id = SPIRV_OpFinishWritingNodePayloadAMDX) \
    bool _finish_writing_func_name_( \
        spirv_by_reference _payload_type_ payload)

// Declares an input payload array with the specified structure type and name.
// Must be declared at the global scope.
// Must only be used with CoalescingAMDX shaders.
#define DeclareInputPayloadArrayAMD(_payload_type_, _payload_name_, _payload_count_) \
    spirv_storage_class( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_NodePayloadAMDX) \
    _payload_type_ _payload_name_[_payload_count_]

// Declares the built-in CoalescedInputCountAMDX with the given variable name.
// Must only be used with CoalescingAMDX shaders.
#define DeclareBuiltInCoalescedInputCountAMDX(_name_) \
    spirv_decorate( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        11 /* BuiltIn */, SPIRV_CoalescedInputCountAMDX) \
    spirv_storage_class(1 /*Input*/) uint _name_

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

// Used internally, this macro defines the common part of the declaration.
// NB: This is to work around the glslang bug with OpDecorateString (it must precede other decorations).
#define DeclareOutputPayloadAMD_Common(_node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_) \
    spirv_decorate_id( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_NodeMaxPayloadsAMDX, _payload_count_) \
    spirv_storage_class( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_NodeOutputPayloadAMDX) \
    _payload_type_ _payload_name_[_payload_count_]; \
    \
    spirv_instruction( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        id = SPIRV_OpInitializeNodePayloadsAMDX) \
    void _init_func_name_( \
        spirv_by_reference _payload_type_[_payload_count_] payloadArray, \
        uint visibility, \
        uint payloadCount, \
        uint nodeIndex); \
    \
    spirv_instruction( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        id = SPIRV_OpFinalizeNodePayloadsAMDX) \
    void _finalize_func_name_( \
        spirv_by_reference _payload_type_[_payload_count_] payloadArray)

#define DeclareOutputPayloadAMD(_node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_) \
    spirv_decorate_string( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_PayloadNodeNameAMDX, _node_name_) \
    DeclareOutputPayloadAMD_Common(_node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_)

// Declare and output payload array that shares its output resources with another payload variable.
// Allocated payload count will be subtracted from the other payload's NodeMaxPayloadsAMDX budget.
#define DeclareOutputPayloadSharedWithAMD( \
    _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_, _other_payload_name_) \
    spirv_decorate_string( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_PayloadNodeNameAMDX, _node_name_) \
    spirv_decorate_id( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_NodeSharesPayloadLimitsWithAMDX, _other_payload_name_) \
    DeclareOutputPayloadAMD_Common( \
        _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_)

// Same as above, but includes FinishWritingNodePayloadAMDX support.
#define DeclareOutputPayloadSharedWithFinishWritingAMD( \
    _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_, _other_payload_name_) \
    spirv_decorate_string( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_PayloadNodeNameAMDX, _node_name_) \
    spirv_decorate_id( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_NodeSharesPayloadLimitsWithAMDX, _other_payload_name_) \
    spirv_decorate( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_TrackFinishWritingAMDX) \
    DeclareOutputPayloadAMD_Common( \
        _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_)

// Declare and output payload array that will be used with FinishWritingNodePayloadAMDX in the receiving node shader.
#define DeclareOutputPayloadWithFinishWritingAMD( \
    _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_) \
    spirv_decorate_string( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_PayloadNodeNameAMDX, _node_name_) \
    spirv_decorate( \
        extensions = ["SPV_AMDX_shader_enqueue"], \
        capabilities = [SPIRV_ShaderEnqueueAMDX], \
        SPIRV_TrackFinishWritingAMDX) \
    DeclareOutputPayloadAMD_Common( \
        _node_name_, _init_func_name_, _finalize_func_name_, _payload_type_, _payload_name_, _payload_count_)

// In order to enqueue an empty payload, the following SPIRV intrinsics type can be used (GLSL can't express
// an empty struct natively).
//
#define EmptyStruct spirv_type(id = 30 /*OpTypeStruct*/)


// ---------------------------------------------------------------------------------------------------------------------
// Example 1
// A simple shader that allocates payload and enques another shader may be written as follows
// ---------------------------------------------------------------------------------------------------------------------
/*
#version 460
#extension GL_GOOGLE_include_directive : require

#include "shaderEnqueueSpirvIntrinsics.glsl.h"

struct PayloadData
{
    uvec2 coord;
};

// Fixed expansion size.
DeclareStaticNumWorkgroupsAMDX(1, 1, 1);

// Declare 2 payloads for a specific node.
DeclareOutputPayloadAMD("consumer", myInitFunc, myFinalizeFunc, PayloadData, payloads, 2);

layout(local_size_x = 1) in;
void main()
{
    uint shaderIndex = 0;

    myInitFunc(payloads, ScopeWorkgroup, shaderIndex, 2);

    payloads[0].coord = uvec2(1, 2);
    payloads[1].coord = uvec2(5, 7);

    myFinalizeFunc(payloads);     // the finalize call is optional

    // Two allocated payloads mean the "consumer" shader will be dispatched twice.
}
*/

// ---------------------------------------------------------------------------------------------------------------------
// Example 2
// A fixed expansion node shader that consumes a payload may be written as follows
// ---------------------------------------------------------------------------------------------------------------------
/*
#version 460
#extension GL_GOOGLE_include_directive : require

#include "shaderEnqueueSpirvIntrinsics.glsl.h"

struct PayloadData
{
    uvec2 coord;
};

// Fixed expansion size.
DeclareStaticNumWorkgroupsAMDX(1, 1, 1);

// Declare an input_payload variable.
DeclareInputPayloadAMD(PayloadData, input_payload);

layout(local_size_x = 1) in;

void main()
{
    // Simply access the payload directly.
    uvec2 position = input_payload.coord;

    // Do something with the data...
}
*/

// ---------------------------------------------------------------------------------------------------------------------
// Example 3
// A coalescing node shader that consumes a payload may be written as follows
// ---------------------------------------------------------------------------------------------------------------------
/*
#version 460
#extension GL_GOOGLE_include_directive : require

#include "shaderEnqueueSpirvIntrinsics.glsl.h"

struct PayloadData
{
    uvec2 coord;
};

// Declare this node as a coalescing node
DeclareCoalescingAMDX();
DeclareBuiltInCoalescedInputCountAMDX(coalescedInputCount);

// Coalescing nodes always take an array of payloads.
// Use the maximum number of payloads we may have depending on our workload.
// A local workgroup with 16x16 size will have up to 256 payloads.
// input_payload is the declared name of the array.
//
DeclareInputPayloadArrayAMD(PayloadData, input_payload, 256);

layout(local_size_x = 16, local_size_y = 16) in;

void main()
{
    // Check if a payload item is available.
    if (gl_LocalInvocationIndex < coalescedInputCount)
    {
        uvec2 position = input_payload[gl_LocalInvocationIndex].coord;

        // Do something with the data...
    }
}
*/
