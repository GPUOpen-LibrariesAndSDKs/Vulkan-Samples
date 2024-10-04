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


#include "gpu_draw_dispatch.h"
#include "example_default.h"
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

PFN_vkCreateExecutionGraphPipelinesAMDX           vkCreateExecutionGraphPipelinesAMDX;
PFN_vkGetExecutionGraphPipelineScratchSizeAMDX    vkGetExecutionGraphPipelineScratchSizeAMDX;
PFN_vkGetExecutionGraphPipelineNodeIndexAMDX      vkGetExecutionGraphPipelineNodeIndexAMDX;
PFN_vkCmdInitializeGraphScratchMemoryAMDX         vkCmdInitializeGraphScratchMemoryAMDX;
PFN_vkCmdDispatchGraphAMDX                        vkCmdDispatchGraphAMDX;
PFN_vkCmdDispatchGraphIndirectAMDX                vkCmdDispatchGraphIndirectAMDX;
PFN_vkCmdDispatchGraphIndirectCountAMDX           vkCmdDispatchGraphIndirectCountAMDX;

static void load_extension_function_pointers(VkDevice device)
{
    vkCreateExecutionGraphPipelinesAMDX        = (PFN_vkCreateExecutionGraphPipelinesAMDX)       vkGetDeviceProcAddr(device, "vkCreateExecutionGraphPipelinesAMDX");
    vkGetExecutionGraphPipelineScratchSizeAMDX = (PFN_vkGetExecutionGraphPipelineScratchSizeAMDX)vkGetDeviceProcAddr(device, "vkGetExecutionGraphPipelineScratchSizeAMDX");
    vkGetExecutionGraphPipelineNodeIndexAMDX   = (PFN_vkGetExecutionGraphPipelineNodeIndexAMDX)  vkGetDeviceProcAddr(device, "vkGetExecutionGraphPipelineNodeIndexAMDX");
    vkCmdInitializeGraphScratchMemoryAMDX      = (PFN_vkCmdInitializeGraphScratchMemoryAMDX)     vkGetDeviceProcAddr(device, "vkCmdInitializeGraphScratchMemoryAMDX");
    vkCmdDispatchGraphAMDX                     = (PFN_vkCmdDispatchGraphAMDX)                    vkGetDeviceProcAddr(device, "vkCmdDispatchGraphAMDX");
    vkCmdDispatchGraphIndirectAMDX             = (PFN_vkCmdDispatchGraphIndirectAMDX)            vkGetDeviceProcAddr(device, "vkCmdDispatchGraphIndirectAMDX");
    vkCmdDispatchGraphIndirectCountAMDX        = (PFN_vkCmdDispatchGraphIndirectCountAMDX)       vkGetDeviceProcAddr(device, "vkCmdDispatchGraphIndirectCountAMDX");
}

void GpuDrawDispatch::finish()
{
    VulkanSample::finish();

    if (device)
    {
        if (example)
        {
            example->free_resources();
        }

        for (const auto& gui_framebuffer : per_frame_gui_framebuffer)
        {
            vkDestroyFramebuffer(device->get_handle(), gui_framebuffer, nullptr);
        }

        for (const auto& shader_module : shader_module_cache)
        {
            vkDestroyShaderModule(device->get_handle(), shader_module.second, nullptr);
        }

        vkDestroyPipelineCache(device->get_handle(), pipeline_cache, nullptr);
        vkDestroyRenderPass(device->get_handle(), gui_render_pass, nullptr);
    }
}

GpuDrawDispatch::GpuDrawDispatch()
{
    set_api_version(VK_MAKE_VERSION(1, 3, 0));
}

void GpuDrawDispatch::request_gpu_features(vkb::PhysicalDevice &gpu)
{
    auto &requested_features = gpu.get_mutable_requested_features();

    // Clamp if supported, it's better performance
    requested_features.depthClamp         = gpu.get_features().depthClamp;
    requested_features.samplerAnisotropy  = gpu.get_features().samplerAnisotropy;
    requested_features.tessellationShader = gpu.get_features().tessellationShader;
    requested_features.geometryShader     = gpu.get_features().geometryShader;

    // Not needed
    requested_features.robustBufferAccess = VK_FALSE;

    const auto& descriptorIndexingFeatures = gpu.request_extension_features<VkPhysicalDeviceDescriptorIndexingFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES);

    // Required for non-uniform texture sampling in a workgroup
    assert(descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing == VK_TRUE);

    // Set up VK_AMDX_shader_enqueue extension
    add_device_extension(VK_AMDX_SHADER_ENQUEUE_EXTENSION_NAME);

    const auto& shaderEnqueueFeatures = gpu.request_extension_features<VkPhysicalDeviceShaderEnqueueFeaturesAMDX>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ENQUEUE_FEATURES_AMDX);
    assert(shaderEnqueueFeatures.shaderEnqueue == VK_TRUE);
    assert(shaderEnqueueFeatures.shaderMeshEnqueue == VK_TRUE);

    // Request the BDA extension -- this is how the framework enables support in VMA
    add_device_extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    const auto& bufferDeviceAddressFeatures = gpu.request_extension_features<VkPhysicalDeviceBufferDeviceAddressFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);

    assert(bufferDeviceAddressFeatures.bufferDeviceAddress == VK_TRUE);

    add_device_extension(VK_EXT_MESH_SHADER_EXTENSION_NAME);

    const auto& meshShaderFeatures = gpu.request_extension_features<VkPhysicalDeviceMeshShaderFeaturesEXT>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT);

    assert(meshShaderFeatures.meshShader == VK_TRUE);

    memset(&shader_enqueue_properties, 0, sizeof(shader_enqueue_properties));
    shader_enqueue_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ENQUEUE_PROPERTIES_AMDX;

    VkPhysicalDeviceProperties2 physicalDeviceProperties = {};
    physicalDeviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    physicalDeviceProperties.pNext = &shader_enqueue_properties;

    vkGetPhysicalDeviceProperties2(gpu.get_handle(), &physicalDeviceProperties);

    // Ensure the shader enqueue extension is available
    uint32_t device_extension_count;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(gpu.get_handle(), nullptr, &device_extension_count, nullptr));

    std::vector<VkExtensionProperties> available_device_extensions(device_extension_count);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(gpu.get_handle(), nullptr, &device_extension_count, available_device_extensions.data()));

    auto compareExt = [](const VkExtensionProperties& props){
        return std::strcmp(props.extensionName, VK_AMDX_SHADER_ENQUEUE_EXTENSION_NAME) == 0;
    };

    is_shader_enqueue_supported = (
        std::find_if(available_device_extensions.cbegin(), available_device_extensions.cend(), compareExt)
        != available_device_extensions.cend());
};

void GpuDrawDispatch::input_event(const vkb::InputEvent &input_event)
{
    Application::input_event(input_event);

    if (gui)
    {
        gui->input_event(input_event);
    }

    if (input_event.get_source() == vkb::EventSource::Keyboard)
    {
        // Implement if needed
    }
}

void GpuDrawDispatch::draw_gui()
{
    gui->show_options_window(
        /* body = */ [this]() {
            {
                ImGui::Text("%s", gui_message.c_str());
                ImGui::SameLine();
            }
        },
        /* lines = */ 1);
}

void GpuDrawDispatch::prepare_render_context()
{
    // NOTE: Not sure how to use the framework correctly to change the swapchain properties after the app has been
    //       prepared. It would leak the previous swapchain.
    //       For now we just use the formats/usage bits that work for all rendering modes.

    get_render_context().set_present_mode_priority({
        VK_PRESENT_MODE_IMMEDIATE_KHR,  // preferred
        VK_PRESENT_MODE_FIFO_KHR,
    });

    // We have to use a non-SRGB format to use STORAGE image bit
    get_render_context().set_surface_format_priority({
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    });

    get_render_context().prepare(1, [this](vkb::core::Image &&swapchain_image) { return create_render_target(std::move(swapchain_image)); });

    get_render_context().update_swapchain(std::set<VkImageUsageFlagBits>({
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    }));
}

std::unique_ptr<vkb::RenderTarget> GpuDrawDispatch::create_render_target(vkb::core::Image &&swapchain_image)
{
    auto &device = swapchain_image.get_device();
    auto &extent = swapchain_image.get_extent();

    vkb::core::Image depth_image {
        device,
        extent,
        vkb::get_suitable_depth_format(swapchain_image.get_device().get_gpu().get_handle()),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    };

    std::vector<vkb::core::Image> images;

    images.push_back(std::move(swapchain_image));
    images.push_back(std::move(depth_image));

    return std::make_unique<vkb::RenderTarget>(std::move(images));
}

bool GpuDrawDispatch::prepare(vkb::Platform &platform)
{
    if (!VulkanSample::prepare(platform))
    {
        return false;
    }

    for (auto& arg : platform.get_arguments())
    {
        // platform.using_plugin<>() seems bugged in release mode
        if (arg == "--benchmark")
        {
            is_benchmarking = true;
        }
        else if (arg.substr(0, 12) == "--stop-after")
        {
            is_stop_after = true;
        }
    }

    // Keep these plugins disabled initially; we don't want to measure the resource loading time
    if (is_benchmarking)
    {
        platform.get_plugin_2<::plugins::BenchmarkMode>()->set_enabled(false);
    }
    if (is_stop_after)
    {
        platform.get_plugin_2<::plugins::StopAfter>()->set_enabled(false);
    }

    enum {
        USE_EXAMPLE_DEFAULT,
        USE_EXAMPLE_DYNAMIC_STATE,
    } example_to_use;

    example_to_use = USE_EXAMPLE_DEFAULT;

    DefaultExample::Config          example_default_cfg {};
    DynamicStateExample::Config     example_dynamic_state_cfg {};

    // Handle command line options
    for (auto& arg : platform.get_generic_options())
    {
        if (arg == "no_animation")
        {
            example_default_cfg.rotateAnimation = false;
            example_dynamic_state_cfg.rotateAnimation = false;
        }
        else if (arg == "draw_node")
        {
            example_default_cfg.drawMode = DefaultExample::OptDraw::WORKGRAPH_DRAW;
            example_dynamic_state_cfg.drawMode = DynamicStateExample::OptDraw::WORKGRAPH_DRAW;
        }
        else if (arg == "compute_draw_node")
        {
            example_default_cfg.drawMode = DefaultExample::OptDraw::WORKGRAPH_COMPUTE_INTO_DRAW;
            example_dynamic_state_cfg.drawMode = DynamicStateExample::OptDraw::WORKGRAPH_COMPUTE_INTO_DRAW;
        }
        else if (arg == "single")
        {
            example_default_cfg.instanceMode = DefaultExample::OptNodeInstance::SINGLE;
        }
        else if (arg == "multi")
        {
            example_default_cfg.instanceMode = DefaultExample::OptNodeInstance::MULTI;
        }
        else if (arg == "multi_all")
        {
            example_default_cfg.instanceMode = DefaultExample::OptNodeInstance::MULTI_ALL_AT_ONCE;
        }
        else if (arg == "node_info")
        {
            example_default_cfg.useNodeInfo = true;
        }
        else if (arg == "max_payload")
        {
            example_default_cfg.nodeLimits = DefaultExample::OptNodeLimits::MAX_SHADER_PAYLOAD_SIZE;
        }
        else if (arg == "max_draw")
        {
            example_default_cfg.nodeLimits = DefaultExample::OptNodeLimits::LARGE_NUMBER_PAYLOADS_DRAW;
        }
        else if (arg == "share_input")
        {
            example_default_cfg.instanceMode = DefaultExample::OptNodeInstance::MULTI_ALL_AT_ONCE;
            example_default_cfg.shareInput = true;
        }
        else if (arg == "dynamic_state")
        {
            example_to_use = USE_EXAMPLE_DYNAMIC_STATE;
        }
        else
        {
            LOGE("Unrecognized option argument: {}", arg);
        }
    }

    load_extension_function_pointers(device->get_handle());

    switch (example_to_use)
    {
    case USE_EXAMPLE_DEFAULT:
        example = std::make_unique<DefaultExample>(*this, example_default_cfg);
        break;
    case USE_EXAMPLE_DYNAMIC_STATE:
        example = std::make_unique<DynamicStateExample>(*this, example_dynamic_state_cfg);
        break;
    }

    if (!is_benchmarking)
    {
        gui = std::make_unique<vkb::Gui>(*this, platform.get_window(), nullptr, 15.0f, true);

        std::vector<VkPipelineShaderStageCreateInfo> shader_stages;

        shader_stages.emplace_back(load_shader("uioverlay/uioverlay.vert", VK_SHADER_STAGE_VERTEX_BIT));
        shader_stages.emplace_back(load_shader("uioverlay/uioverlay.frag", VK_SHADER_STAGE_FRAGMENT_BIT));

        {
            VkAttachmentDescription attachment = {};
            attachment.format         = get_render_context().get_format();
            attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentReference color_reference = {};
            color_reference.attachment            = 0;
            color_reference.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass_description    = {};
            subpass_description.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass_description.colorAttachmentCount    = 1;
            subpass_description.pColorAttachments       = &color_reference;
            subpass_description.pDepthStencilAttachment = nullptr;
            subpass_description.inputAttachmentCount    = 0;
            subpass_description.pInputAttachments       = nullptr;
            subpass_description.preserveAttachmentCount = 0;
            subpass_description.pPreserveAttachments    = nullptr;
            subpass_description.pResolveAttachments     = nullptr;

            VkRenderPassCreateInfo render_pass_create_info = {};
            render_pass_create_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            render_pass_create_info.attachmentCount        = 1;
            render_pass_create_info.pAttachments           = &attachment;
            render_pass_create_info.subpassCount           = 1;
            render_pass_create_info.pSubpasses             = &subpass_description;
            render_pass_create_info.dependencyCount        = 0;
            render_pass_create_info.pDependencies          = nullptr;

            vkDestroyRenderPass(device->get_handle(), gui_render_pass, nullptr);
            VK_CHECK(vkCreateRenderPass(device->get_handle(), &render_pass_create_info, nullptr, &gui_render_pass));
        }

        gui->prepare(pipeline_cache, gui_render_pass, shader_stages);

        gui_message = example->get_gui_message();

        per_frame_gui_framebuffer.resize(get_num_frames());
    }

    // Static resources

    {
        VkPipelineCacheCreateInfo pipeline_cache_create_info = {};
        pipeline_cache_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VK_CHECK(vkCreatePipelineCache(device->get_handle(), &pipeline_cache_create_info, nullptr, &pipeline_cache));
    }

    example->create_static_resources();

    return true;
}

bool GpuDrawDispatch::resize(uint32_t width, uint32_t height)
{
    auto ok = Application::resize(width, height);

    device->wait_idle();

    get_render_context().handle_surface_changes();

    if (gui)
    {
        gui->resize(width, height);
    }

    resources_ready = false;

    return ok;
}

void GpuDrawDispatch::update_gui(float delta_time)
{
    if (gui)
    {
        gui->new_frame();
        gui->show_top_window(get_name(), stats.get(), &get_debug_info());

        draw_gui();

        gui->update(delta_time);
        gui->update_buffers();
    }
}

void GpuDrawDispatch::create_and_init_resources(vkb::CommandBuffer& cmd_buf)
{
    if (gui)
    {
        for (uint32_t frame_ndx = 0; frame_ndx < get_num_frames(); ++frame_ndx)
        {
            auto& frame = *get_render_context().get_render_frames()[frame_ndx].get();
            auto& rt = frame.get_render_target();
            auto image_view = rt.get_views().at(MRT_SWAPCHAIN).get_handle();

            auto framebuffer_create_info = vkb::initializers::framebuffer_create_info();
            framebuffer_create_info.renderPass      = gui_render_pass;
            framebuffer_create_info.attachmentCount = 1;
            framebuffer_create_info.pAttachments    = &image_view;
            framebuffer_create_info.width           = rt.get_extent().width;
            framebuffer_create_info.height          = rt.get_extent().height;
            framebuffer_create_info.layers          = 1;

            vkDestroyFramebuffer(device->get_handle(), per_frame_gui_framebuffer[frame_ndx], nullptr);
            VK_CHECK(vkCreateFramebuffer(device->get_handle(), &framebuffer_create_info, nullptr, &per_frame_gui_framebuffer[frame_ndx]));
        }
    }

    example->create_and_init_resources(cmd_buf);
}

void GpuDrawDispatch::update(float delta_time)
{
    get_render_context().begin_frame();
    auto acquire_semaphore = get_render_context().consume_acquired_semaphore();

    auto& frame = get_render_context().get_active_frame();
    auto& graphics_queue = device->get_suitable_graphics_queue();

    if (!resources_ready)
    {
        auto& cmd_buf = frame.request_command_buffer(graphics_queue);
        cmd_buf.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        create_and_init_resources(cmd_buf);

        cmd_buf.end();

        auto submit_info = vkb::initializers::submit_info();
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers    = &cmd_buf.get_handle();

        auto fence = frame.request_fence();

        VK_CHECK(graphics_queue.submit({submit_info}, fence));

        vkWaitForFences(device->get_handle(), 1, &fence, VK_TRUE, ~0);

        resources_ready = true;
    }

    update_gui(delta_time);

    {
        auto& cmd_buf = frame.request_command_buffer(graphics_queue);
        cmd_buf.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        record_frame_commands(cmd_buf, delta_time);

        cmd_buf.end();

        auto present_semaphore = frame.request_semaphore();

        VkPipelineStageFlags stage_masks[] = { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };
        auto submit_info = vkb::initializers::submit_info();
        submit_info.waitSemaphoreCount   = 1;
        submit_info.pWaitDstStageMask    = stage_masks;
        submit_info.pWaitSemaphores      = &acquire_semaphore;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores    = &present_semaphore;
        submit_info.commandBufferCount   = 1;
        submit_info.pCommandBuffers      = &cmd_buf.get_handle();

        VK_CHECK(graphics_queue.submit({submit_info}, frame.request_fence()));
        // The fence will be waited on implicitly the next time we acquire this frame again.

        get_render_context().end_frame(present_semaphore);
    }

    if (frame_count == 1)
    {
        // If we're benchmarking, start the measurement after the resources have been loaded.
        if (is_benchmarking)
        {
            platform->get_plugin_2<::plugins::BenchmarkMode>()->set_enabled(true);
        }
        if (is_stop_after)
        {
            platform->get_plugin_2<::plugins::StopAfter>()->set_enabled(true);
        }
    }

    ++frame_count;

    platform->on_post_draw(get_render_context());

    // Don't call VulkanSample::update(), it depends on the RenderPipeline and Scene which we don't use.
}

void GpuDrawDispatch::record_frame_commands(vkb::CommandBuffer& cmd_buf, float delta_time)
{
    example->record_frame_commands(cmd_buf, delta_time);

    if (gui)
    {
        auto& rt = get_render_context().get_active_frame().get_render_target();

        auto render_pass_begin_info = vkb::initializers::render_pass_begin_info();
        render_pass_begin_info.renderPass        = gui_render_pass;
        render_pass_begin_info.framebuffer       = per_frame_gui_framebuffer[get_render_context().get_active_frame_index()];
        render_pass_begin_info.renderArea.extent = rt.get_extent();
        render_pass_begin_info.renderArea.offset = {0, 0};

        vkCmdBeginRenderPass(cmd_buf.get_handle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        gui->draw(cmd_buf.get_handle());

        vkCmdEndRenderPass(cmd_buf.get_handle());
    }
}

VkPipelineShaderStageCreateInfo GpuDrawDispatch::load_shader(
    const std::string&          file,
    VkShaderStageFlagBits       stage)
{
    VkPipelineShaderStageCreateInfo shader_stage_create_info = {};
    shader_stage_create_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage_create_info.stage  = stage;
    shader_stage_create_info.pName  = "main";

    auto module_iter = shader_module_cache.find(file);
    if (module_iter == shader_module_cache.end())
    {
        shader_stage_create_info.module = vkb::load_shader(file, device->get_handle(), stage);
        assert(shader_stage_create_info.module != VK_NULL_HANDLE);

        shader_module_cache.insert({file, shader_stage_create_info.module});
    }
    else
    {
        shader_stage_create_info.module = module_iter->second;
    }

    return shader_stage_create_info;
}

VkPipelineShaderStageCreateInfo GpuDrawDispatch::load_spv_shader(
    const std::string&          file,
    VkShaderStageFlagBits       stage)
{
    VkPipelineShaderStageCreateInfo shader_stage_create_info = {};
    shader_stage_create_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage_create_info.stage  = stage;
    shader_stage_create_info.pName  = "main";

    auto module_iter = shader_module_cache.find(file);
    if (module_iter == shader_module_cache.end())
    {
        auto buffer = vkb::fs::read_shader_binary(file);
        assert((buffer.size() % sizeof(uint32_t)) == 0);

        VkShaderModuleCreateInfo module_create_info = {};
        module_create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_create_info.codeSize = buffer.size();
        module_create_info.pCode    = reinterpret_cast<const uint32_t*>(buffer.data());

        VK_CHECK(vkCreateShaderModule(device->get_handle(), &module_create_info, NULL, &shader_stage_create_info.module));

        shader_module_cache.insert({file, shader_stage_create_info.module});
    }
    else
    {
        shader_stage_create_info.module = module_iter->second;
    }

    return shader_stage_create_info;
}

std::unique_ptr<vkb::sg::SubMesh> GpuDrawDispatch::load_model(const std::string &file, uint32_t index, bool use_indexed_draw, bool mesh_shader_buffer)
{
    vkb::GLTFLoader loader{*device};

    std::unique_ptr<vkb::sg::SubMesh> model = loader.read_model_from_file(file, index, !use_indexed_draw, mesh_shader_buffer);

    if (!model)
    {
        LOGE("Cannot load model from file: {}", file.c_str());
        throw std::runtime_error("Cannot load model from file: " + file);
    }

    return model;
}

}

std::unique_ptr<vkb::VulkanSample> create_gpu_draw_dispatch()
{
    return std::make_unique<GpuDrawDispatch::GpuDrawDispatch>();
}
