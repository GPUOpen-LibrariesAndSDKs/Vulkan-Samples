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


#include "example_dynamic_state.h"

#include "common/vk_common.h"
#include "common/vk_initializers.h"
#include "gltf_loader.h"
#include "gui.h"
#include "platform/platform.h"
#include "scene_graph/components/sub_mesh.h"
#include "scene_graph/components/image.h"
#include "benchmark_mode/benchmark_mode.h"
#include "stop_after/stop_after.h"

#include <bitset>
#include <set>
#include <unordered_map>

namespace GpuDrawDispatch
{

DynamicStateExample::DynamicStateExample(GpuDrawDispatch& _parent, const Config& _config)
    : parent(_parent)
    , config(_config)
{
    if (config.drawMode == OptDraw::WORKGRAPH_COMPUTE_INTO_DRAW)
    {
        // Not implemented
        LOGE("Unsupported options combination: compute_draw_node");
        std::exit(1);
    }
}

void DynamicStateExample::free_resources()
{
    VkDevice device = parent.get_device()->get_handle();

    for (auto& frame_data : per_frame_data)
    {
        vkDestroyFramebuffer(device, frame_data.framebuffer, nullptr);
    }

    for (auto& pipeline : graphics_pipelines)
    {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    graphics_pipelines.clear();

    vkDestroyPipeline(device, workgraph_pipeline, nullptr);
    vkDestroyRenderPass(device, render_pass, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    vkDestroyPipelineLayout(device, graphics_pipeline_layout, nullptr);
}

std::string DynamicStateExample::get_gui_message() const
{
    std::ostringstream msg;

    switch (config.drawMode)
    {
    case OptDraw::WORKGRAPH_DRAW:
        msg << "GWG mesh draw";
        break;
    case OptDraw::WORKGRAPH_COMPUTE_INTO_DRAW:
        msg << "GWG compute -> mesh draw";
        break;
    };

    return msg.str();
}

void DynamicStateExample::create_static_resources()
{
    model = parent.load_model("scenes/teapot.gltf");
    mesh_shader_model = parent.load_model("scenes/teapot.gltf", 0, false, true);

    const auto num_frames = parent.get_num_frames();
    per_frame_data.resize(num_frames);

    {
        std::vector<VkDescriptorPoolSize> pool_sizes;
        pool_sizes.emplace_back(vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * num_frames));
        pool_sizes.emplace_back(vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * num_frames));

        auto descriptor_pool_create_info = vkb::initializers::descriptor_pool_create_info(pool_sizes, num_frames);
        VK_CHECK(vkCreateDescriptorPool(parent.get_device()->get_handle(), &descriptor_pool_create_info, nullptr, &descriptor_pool));
    }
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings {
            vkb::initializers::descriptor_set_layout_binding(
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0),
            vkb::initializers::descriptor_set_layout_binding(
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_MESH_BIT_EXT,
                1),
            vkb::initializers::descriptor_set_layout_binding(
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_SHADER_STAGE_MESH_BIT_EXT,
                2)
        };

        auto descriptor_set_layout_create_info = vkb::initializers::descriptor_set_layout_create_info(bindings.data(), vkb::to_u32(bindings.size()));
        VK_CHECK(vkCreateDescriptorSetLayout(parent.get_device()->get_handle(), &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout));

        auto pipeline_layout_create_info = vkb::initializers::pipeline_layout_create_info(&descriptor_set_layout);

        VK_CHECK(vkCreatePipelineLayout(parent.get_device()->get_handle(), &pipeline_layout_create_info, nullptr, &graphics_pipeline_layout));
    }
}

void DynamicStateExample::create_and_init_resources(vkb::CommandBuffer& cmd_buf)
{
    parent.get_device()->wait_idle();

    VK_CHECK(vkResetDescriptorPool(parent.get_device()->get_handle(), descriptor_pool, 0));

    {
        std::vector<VkAttachmentDescription> all_attachments;
        std::vector<VkAttachmentReference>   color_refs;
        VkAttachmentReference                depth_ref  = {};
        VkAttachmentDescription              attachment = {};

        // Color attachments
        attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

        // Color output
        attachment.format = parent.get_render_context().get_format();
        color_refs.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        all_attachments.push_back(attachment);

        auto& first_render_target = parent.get_render_context().get_render_frames().at(0)->get_render_target();

        // Depth attachment
        attachment.format         = first_render_target.get_views().at(MRT_DEPTH).get_format();
        attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        depth_ref = { vkb::to_u32(all_attachments.size()), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        all_attachments.push_back(attachment);

        VkSubpassDescription subpass_description    = {};
        subpass_description.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass_description.colorAttachmentCount    = vkb::to_u32(all_attachments.size() - 1);
        subpass_description.pColorAttachments       = color_refs.data();
        subpass_description.pDepthStencilAttachment = &depth_ref;
        subpass_description.inputAttachmentCount    = 0;
        subpass_description.pInputAttachments       = nullptr;
        subpass_description.preserveAttachmentCount = 0;
        subpass_description.pPreserveAttachments    = nullptr;
        subpass_description.pResolveAttachments     = nullptr;

        std::vector<VkSubpassDependency> subpass_dependencies;
        {
            VkSubpassDependency subpass_dependency = {};
            subpass_dependency.srcSubpass      = 0;
            subpass_dependency.dstSubpass      = 0;
            subpass_dependency.srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            subpass_dependency.dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            subpass_dependency.srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            subpass_dependency.dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            subpass_dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            subpass_dependencies.push_back(subpass_dependency);
        }

        VkRenderPassCreateInfo render_pass_create_info = {};
        render_pass_create_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_create_info.attachmentCount        = vkb::to_u32(all_attachments.size());
        render_pass_create_info.pAttachments           = all_attachments.data();
        render_pass_create_info.subpassCount           = 1;
        render_pass_create_info.pSubpasses             = &subpass_description;
        render_pass_create_info.dependencyCount        = 0;
        render_pass_create_info.pDependencies          = nullptr;

        vkDestroyRenderPass(parent.get_device()->get_handle(), render_pass, nullptr);
        VK_CHECK(vkCreateRenderPass(parent.get_device()->get_handle(), &render_pass_create_info, nullptr, &render_pass));
    }
    {
        auto input_assembly_state = vkb::initializers::pipeline_input_assembly_state_create_info(
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                0,
                VK_FALSE);

        auto rasterization_state = vkb::initializers::pipeline_rasterization_state_create_info(
                VK_POLYGON_MODE_LINE,
                VK_CULL_MODE_BACK_BIT,
                VK_FRONT_FACE_CLOCKWISE);
        rasterization_state.depthClampEnable = parent.get_device()->get_gpu().get_features().depthClamp;

        const uint32_t num_color_attachments = 1;

        std::vector<VkPipelineColorBlendAttachmentState> blend_attachment_states;
        blend_attachment_states.resize(
            num_color_attachments, vkb::initializers::pipeline_color_blend_attachment_state(
                0xf,
                VK_FALSE));

        auto color_blend_state = vkb::initializers::pipeline_color_blend_state_create_info(
                num_color_attachments,
                blend_attachment_states.data());

        auto depth_stencil_state = vkb::initializers::pipeline_depth_stencil_state_create_info(
                VK_TRUE,
                VK_TRUE,
                VK_COMPARE_OP_GREATER);

        auto viewport_state = vkb::initializers::pipeline_viewport_state_create_info(1, 1);

        auto multisample_state = vkb::initializers::pipeline_multisample_state_create_info(
                VK_SAMPLE_COUNT_1_BIT);

        std::vector<VkDynamicState> dynamic_state_enables = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_LINE_WIDTH,
        };

        auto dynamic_state = vkb::initializers::pipeline_dynamic_state_create_info(
                dynamic_state_enables.data(),
                vkb::to_u32(dynamic_state_enables.size()));

        auto pipeline_create_info = vkb::initializers::pipeline_create_info(graphics_pipeline_layout, render_pass);

        std::vector<VkPipelineShaderStageCreateInfo> shader_stages;

        shader_stages.push_back(parent.load_spv_shader("gpu_draw_dispatch/spv/geometry_mesh_ms.spv", VK_SHADER_STAGE_MESH_BIT_EXT));
        shader_stages.push_back(parent.load_spv_shader("gpu_draw_dispatch/spv/geometry_forward_ps.spv", VK_SHADER_STAGE_FRAGMENT_BIT));

        for (uint32_t ndx = 0; ndx < graphics_pipelines.size(); ++ndx)
        {
            vkDestroyPipeline(parent.get_device()->get_handle(), graphics_pipelines[ndx], nullptr);
        }

        graphics_pipelines.clear();

        VkPipelineCreateFlags2CreateInfoKHR flags2_create_info =
        {
            VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR,
            nullptr,
            VK_PIPELINE_CREATE_2_EXECUTION_GRAPH_BIT_AMDX | VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR,
        };

        pipeline_create_info.pNext               = &flags2_create_info;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pColorBlendState    = &color_blend_state;
        pipeline_create_info.pMultisampleState   = &multisample_state;
        pipeline_create_info.pViewportState      = &viewport_state;
        pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
        pipeline_create_info.pDynamicState       = &dynamic_state;
        pipeline_create_info.stageCount          = 2;
        pipeline_create_info.pStages             = shader_stages.data();

        VkPipeline graphics_pipeline = {};
        VK_CHECK(vkCreateGraphicsPipelines(parent.get_device()->get_handle(), parent.get_pipeline_cache(), 1, &pipeline_create_info, nullptr, &graphics_pipeline));
        graphics_pipelines.push_back(graphics_pipeline);
    }

    {
        // Declare all data that may be needed for the pipeline creation.
        struct SpecData
        {
            uint32_t    view_width;
            uint32_t    view_height;
        };

        auto all_shader_stages      = std::vector<VkPipelineShaderStageCreateInfo>();
        auto node_info              = std::vector<VkPipelineShaderStageNodeCreateInfoAMDX>();
        auto specialization_data    = std::vector<SpecData>();
        auto specialization_info    = std::vector<VkSpecializationInfo>();
        auto specialization_entries = std::vector<VkSpecializationMapEntry>();

        VkPipelineLibraryCreateInfoKHR libraryCreateInfo {};
        libraryCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
        libraryCreateInfo.libraryCount = vkb::to_u32(graphics_pipelines.size());
        libraryCreateInfo.pLibraries   = graphics_pipelines.data();

        std::vector<VkPipelineShaderStageCreateInfo>         compute_stages;
        std::vector<VkPipelineShaderStageNodeCreateInfoAMDX> compute_node_infos;

        VkExecutionGraphPipelineCreateInfoAMDX pipelineCreateInfo = {};
        pipelineCreateInfo.sType              = VK_STRUCTURE_TYPE_EXECUTION_GRAPH_PIPELINE_CREATE_INFO_AMDX;
        pipelineCreateInfo.flags              = 0;
        pipelineCreateInfo.stageCount         = vkb::to_u32(compute_stages.size());
        pipelineCreateInfo.pStages            = compute_stages.empty() ? nullptr : compute_stages.data();
        pipelineCreateInfo.pLibraryInfo       = &libraryCreateInfo;
        pipelineCreateInfo.layout             = graphics_pipeline_layout;   // reuse the graphics pipeline layout
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex  = -1;

        LOGI("Creating an execution graph pipeline with a draw node...");

        vkDestroyPipeline(parent.get_device()->get_handle(), workgraph_pipeline, nullptr);

        const auto start_time = std::chrono::steady_clock::now();

        VK_CHECK(vkCreateExecutionGraphPipelinesAMDX(parent.get_device()->get_handle(), parent.get_pipeline_cache(), 1, &pipelineCreateInfo, nullptr, &workgraph_pipeline));

        const auto time_elapsed_millis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
        LOGI("Done. Compilation time: {} milliseconds", time_elapsed_millis);

        // Get required amount of scratch memory
        scratch_buffer_size.sType = VK_STRUCTURE_TYPE_EXECUTION_GRAPH_PIPELINE_SCRATCH_SIZE_AMDX;
        scratch_buffer_size.pNext = nullptr;

        VK_CHECK(vkGetExecutionGraphPipelineScratchSizeAMDX(parent.get_device()->get_handle(), workgraph_pipeline, &scratch_buffer_size));

        LOGI("Using scratch buffer size = {}", scratch_buffer_size.maxSize);
    }

    for (uint32_t frame_ndx = 0; frame_ndx < per_frame_data.size(); ++frame_ndx)
    {
        auto& frame_data = per_frame_data[frame_ndx];
        auto& frame = *parent.get_render_context().get_render_frames()[frame_ndx].get();
        auto& rt = frame.get_render_target();

        {
            std::array<VkImageView, 2> image_views {};
            image_views[0] = rt.get_views().at(MRT_SWAPCHAIN).get_handle();
            image_views[1] = rt.get_views().at(MRT_DEPTH).get_handle();

            auto framebuffer_create_info = vkb::initializers::framebuffer_create_info();
            framebuffer_create_info.renderPass      = render_pass;
            framebuffer_create_info.attachmentCount	= vkb::to_u32(image_views.size());
            framebuffer_create_info.pAttachments    = image_views.data();
            framebuffer_create_info.width           = rt.get_extent().width;
            framebuffer_create_info.height          = rt.get_extent().height;
            framebuffer_create_info.layers          = 1;

            vkDestroyFramebuffer(parent.get_device()->get_handle(), frame_data.framebuffer, nullptr);
            VK_CHECK(vkCreateFramebuffer(parent.get_device()->get_handle(), &framebuffer_create_info, nullptr, &frame_data.framebuffer));

            {
                frame_data.uniform_buffer = std::make_unique<vkb::core::Buffer>(
                    *parent.get_device(),
                    sizeof(UniformBuffer),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU,
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);

                auto descriptor_set_allocate_info = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layout, 1);
                VK_CHECK(vkAllocateDescriptorSets(parent.get_device()->get_handle(), &descriptor_set_allocate_info, &frame_data.descriptor_set));

                VkDescriptorBufferInfo descriptor_buffer_info = {};
                descriptor_buffer_info.buffer = frame_data.uniform_buffer->get_handle();
                descriptor_buffer_info.offset = 0;
                descriptor_buffer_info.range  = sizeof(UniformBuffer);

                VkDescriptorBufferInfo meshlet_descriptor  = {};
                meshlet_descriptor.buffer = (*mesh_shader_model->index_buffer).get_handle();
                meshlet_descriptor.offset = 0;
                meshlet_descriptor.range  = VK_WHOLE_SIZE;

                VkDescriptorBufferInfo vertices_descriptor = {};
                vertices_descriptor.buffer = (mesh_shader_model->vertex_buffers.at("vertex_buffer")).get_handle();
                vertices_descriptor.offset = 0;
                vertices_descriptor.range  = VK_WHOLE_SIZE;

                std::vector<VkWriteDescriptorSet> writes;
                writes.push_back(vkb::initializers::write_descriptor_set(frame_data.descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &descriptor_buffer_info));
                writes.push_back(vkb::initializers::write_descriptor_set(frame_data.descriptor_set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &meshlet_descriptor));
                writes.push_back(vkb::initializers::write_descriptor_set(frame_data.descriptor_set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, &vertices_descriptor));

                vkUpdateDescriptorSets(parent.get_device()->get_handle(), vkb::to_u32(writes.size()), writes.data(), 0, nullptr);
            }
            {
                const auto ratio = static_cast<float>(parent.get_render_context().get_surface_extent().width)
                                 / static_cast<float>(parent.get_render_context().get_surface_extent().height);

                camera.type = vkb::CameraType::LookAt;
                camera.set_perspective(60.0f, ratio, 256.0f, 1.0f);

                camera.set_translation(glm::vec3(0.0f, -0.25f, -5.0f));
                camera.set_rotation(glm::vec3(-32.0f, 20.0f, 0.0f));
            }
        }

        if (scratch_buffer_size.maxSize != 0)
        {
            frame_data.scratch_buffer = std::make_unique<vkb::core::Buffer>(
                *parent.get_device(),
                scratch_buffer_size.maxSize,
                VK_BUFFER_USAGE_EXECUTION_GRAPH_SCRATCH_BIT_AMDX | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY,
                0); // no VMA flags

            {
                auto barrier = vkb::initializers::buffer_memory_barrier();
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                barrier.buffer        = frame_data.scratch_buffer->get_handle();
                barrier.size          = VK_WHOLE_SIZE;

                vkCmdPipelineBarrier(
                    cmd_buf.get_handle(),
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,
                    0, nullptr,
                    1, &barrier,
                    0, nullptr);
            }

            vkCmdInitializeGraphScratchMemoryAMDX(cmd_buf.get_handle(),
                                                  workgraph_pipeline,
                                                  frame_data.scratch_buffer->get_device_address(),
                                                  scratch_buffer_size.maxSize);

            {
                auto barrier = vkb::initializers::buffer_memory_barrier();
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                barrier.buffer        = frame_data.scratch_buffer->get_handle();
                barrier.size          = VK_WHOLE_SIZE;

                vkCmdPipelineBarrier(
                    cmd_buf.get_handle(),
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,
                    0, nullptr,
                    1, &barrier,
                    0, nullptr);
            }
        }
    }
}

void DynamicStateExample::record_frame_commands(vkb::CommandBuffer& cmd_buf, float delta_time)
{
    auto& frame = parent.get_render_context().get_active_frame();
    auto& rt = frame.get_render_target();
    auto& frame_data = per_frame_data.at(parent.get_render_context().get_active_frame_index());

    // Update CPU uniforms
    {
        constexpr auto _2pi = static_cast<float>(2.0 * glm::pi<double>());
        constexpr auto pi4 = static_cast<float>(glm::pi<double>() / 4.0);
        static float angle = 0.0f;
        static float angleMulti = 0.0f;

        if (config.rotateAnimation)
        {
            angle += delta_time * 0.3f;
            if (angle > _2pi)
            {
                angle -= _2pi;
            }
        }
        else
        {
            angle = 0.0f;
        }

        UniformBuffer ubo {};

        auto model_matrix = glm::rotate(glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        auto rotation_anim = glm::rotate(angle, glm::vec3(0.0f, 1.0f, 0.0f));

        ubo.projection           = camera.matrices.perspective;
        ubo.modelview            = camera.matrices.view * rotation_anim * model_matrix;
        ubo.inverseProjModelView = glm::inverse(ubo.projection * ubo.modelview);
        ubo.lightPos             = glm::vec4(5.0f, 5.0f, 0.0f, 1.0f);

        frame_data.uniform_buffer->convert_and_update(ubo);

        // CPU mappable memory is implicitly made available to the device
    }
    {
        std::vector<VkClearValue> clear_values;
        clear_values.push_back({ 0.7f, 0.7f, 1.0f, 1.0f });
        clear_values.push_back({ 0.0f, 0 });

        auto render_pass_begin_info = vkb::initializers::render_pass_begin_info();
        render_pass_begin_info.renderPass        = render_pass;
        render_pass_begin_info.framebuffer       = frame_data.framebuffer;
        render_pass_begin_info.renderArea.extent = rt.get_extent();
        render_pass_begin_info.renderArea.offset = {0, 0};
        render_pass_begin_info.clearValueCount   = vkb::to_u32(clear_values.size());
        render_pass_begin_info.pClearValues      = clear_values.data();

        vkCmdBeginRenderPass(cmd_buf.get_handle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    }

    const VkRect2D scissor = vkb::initializers::rect2D(rt.get_extent().width, rt.get_extent().height, 0, 0);
    vkCmdSetScissor(cmd_buf.get_handle(), 0, 1, &scissor);
    vkCmdSetLineWidth(cmd_buf.get_handle(), 1.0f);

    constexpr uint32_t NumDraws = 4;

    std::vector<VkViewport> small_viewports;
    {
        const float w2 = static_cast<float>(rt.get_extent().width / 2);
        const float h2 = static_cast<float>(rt.get_extent().height / 2);

        for (uint32_t i = 0; i < NumDraws; ++i)
        {
            VkViewport vp = vkb::initializers::viewport(w2, h2, 0.0f, 1.0f);
            vp.x = (i % 2) * w2;
            vp.y = (i / 2) * h2;

            small_viewports.push_back(vp);
        }
    }

    {
        // vkCmdDispatchGraphAMDX uses all parameters from the host
        // If a compute entrypoint is used, it also takes the same draw payload

        std::vector<VkDispatchGraphInfoAMDX> dispatch_infos;
        std::vector<Payload>                 payloads_dispatch;

        for (uint32_t i = 0; i < NumDraws; ++i)
        {
            payloads_dispatch.emplace_back();
            payloads_dispatch.back().dispatch_grid[0] = mesh_shader_model->vertex_indices;  // Here vertex_indices is the count of meshlets.
            payloads_dispatch.back().dispatch_grid[1] = 1;
            payloads_dispatch.back().dispatch_grid[2] = 1;
            payloads_dispatch.back().color[0] = (i == 0) ? 1.0 : 0.2;
            payloads_dispatch.back().color[1] = (i == 1) ? 1.0 : 0.2;
            payloads_dispatch.back().color[2] = (i == 2) ? 1.0 : 0.2;
        }

        // Update the opaque node index used by the dispatch function
        VkPipelineShaderStageNodeCreateInfoAMDX nodeInfo = {};
        nodeInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NODE_CREATE_INFO_AMDX;
        nodeInfo.index = 0;
        nodeInfo.pName = "main";

        uint32_t dispatchNodeIndex = 0;

        VK_CHECK(vkGetExecutionGraphPipelineNodeIndexAMDX(parent.get_device()->get_handle(), workgraph_pipeline, &nodeInfo, &dispatchNodeIndex));

        for (uint32_t i = 0; i < NumDraws; ++i)
        {
            // Each dispatch info gets a single payload from the array of payloads we prepared before.
            VkDispatchGraphInfoAMDX dispatch_info {};
            dispatch_info.nodeIndex     = dispatchNodeIndex;
            dispatch_info.payloadCount  = 1;

            dispatch_info.payloads.hostAddress = &payloads_dispatch[i];
            dispatch_info.payloadStride        = sizeof(payloads_dispatch[0]);

            dispatch_infos.push_back(dispatch_info);
        }

        vkCmdBindPipeline(cmd_buf.get_handle(), VK_PIPELINE_BIND_POINT_EXECUTION_GRAPH_AMDX, workgraph_pipeline);
        vkCmdBindDescriptorSets(cmd_buf.get_handle(), VK_PIPELINE_BIND_POINT_EXECUTION_GRAPH_AMDX, graphics_pipeline_layout, 0, 1, &frame_data.descriptor_set, 0, nullptr);

        for (uint32_t i = 0; i < NumDraws; ++i)
        {
            vkCmdSetViewport(cmd_buf.get_handle(), 0, 1, &small_viewports[i]);
            vkCmdSetLineWidth(cmd_buf.get_handle(), 1.0f + i);

            VkDispatchGraphCountInfoAMDX dispatch_count_info = {};
            dispatch_count_info.count             = 1;
            dispatch_count_info.stride            = sizeof(VkDispatchGraphInfoAMDX);
            dispatch_count_info.infos.hostAddress = &dispatch_infos[i];

            vkCmdDispatchGraphAMDX(cmd_buf.get_handle(),
                                   frame_data.scratch_buffer->get_device_address(),
                                   scratch_buffer_size.maxSize,
                                   &dispatch_count_info);
        }
    }

    const VkViewport viewport = vkb::initializers::viewport(static_cast<float>(rt.get_extent().width), static_cast<float>(rt.get_extent().height), 0.0f, 1.0f);
    vkCmdSetViewport(cmd_buf.get_handle(), 0, 1, &viewport);

    vkCmdEndRenderPass(cmd_buf.get_handle());
}

}