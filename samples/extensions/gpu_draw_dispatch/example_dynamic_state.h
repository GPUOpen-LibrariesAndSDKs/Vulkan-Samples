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

#pragma once

#include "example.h"
#include "gpu_draw_dispatch.h"
#include <vector>

#include "camera.h"
#include "core/shader_module.h"
#include "rendering/render_pipeline.h"
#include "scene_graph/components/camera.h"
#include "vulkan_sample.h"

// Not a part of vulkan_core.h yet, so include it separately.
#include "../gpu_dispatch/vk_amdx_shader_enqueue.h"

namespace GpuDrawDispatch
{

class DynamicStateExample : public Example
{
public:
    enum class OptDraw : uint32_t
    {
        WORKGRAPH_DRAW,                 // A workgraph draw node is dispatched from the API
        WORKGRAPH_COMPUTE_INTO_DRAW,    // A compute node in a workgraph invokes a draw node
    };

    struct Config
    {
        bool        rotateAnimation = true;
        OptDraw     drawMode;
    };

    DynamicStateExample(GpuDrawDispatch& _parent, const Config& _config);

    std::string get_gui_message() const override;
    void create_static_resources() override;     // do not change with resizes, etc.
    void create_and_init_resources(vkb::CommandBuffer& cb) override;
    void record_frame_commands(vkb::CommandBuffer& cb, float delta_time) override;
    void free_resources() override;

protected:
    GpuDrawDispatch&    parent;
    const Config        config;

    vkb::Camera                                  camera;
    std::unique_ptr<vkb::sg::SubMesh>            model;
    std::unique_ptr<vkb::sg::SubMesh>            mesh_shader_model;

    // Keep the compiled modules around, to avoid glslang recompilation on resizes, etc.
    std::unordered_map<std::string, VkShaderModule> shader_module_cache;

    VkRenderPass             render_pass                = VK_NULL_HANDLE;
    VkDescriptorPool         descriptor_pool            = VK_NULL_HANDLE;
    VkDescriptorSetLayout    descriptor_set_layout      = VK_NULL_HANDLE;
    VkPipelineLayout         graphics_pipeline_layout   = VK_NULL_HANDLE;
    std::vector<VkPipeline>  graphics_pipelines         = {};
    VkPipeline               workgraph_pipeline         = VK_NULL_HANDLE;

    VkExecutionGraphPipelineScratchSizeAMDX scratch_buffer_size = {};

    struct PerFrame
    {
        std::unique_ptr<vkb::core::Buffer>  uniform_buffer;
        std::unique_ptr<vkb::core::Buffer>  scratch_buffer;
        VkDescriptorSet                     descriptor_set;
        VkFramebuffer                       framebuffer;
    };
    std::vector<PerFrame> per_frame_data;
};

}
