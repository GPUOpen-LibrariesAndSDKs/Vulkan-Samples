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

/*
* Viewport and camera hints
* -------------------------
* We are using left-hand coordinate system, i.e.:
*
*      Top -1
* Left|  -Y      |Right
*  -1 |-X      +X| +1    with +Z pointing towards the eye
*     |      +Y  |
*      Bottom +1
*
* For example, vertex at Z = -1 is farther from the eye,
* and vertex at Z = 1 is closer to the eye.
*
* The camera object maintains projection and modelview matrices. To position the camera,
* it's best to think in terms of camera being fixed at origin and its modelview matrix transforming
* the world around it. For example:
* - to move the camera back from the origin (model moves away from the eye), use position (0, 0, -Z).
* - to move camera to the right (model moves left), use position (-X, 0, 0).
* - to move camera up (model moves down), use position (0, +Y, 0).
*/

#include "gpu_dispatch.h"

#include "common/vk_common.h"
#include "common/vk_initializers.h"
#include "gltf_loader.h"
#include "gui.h"
#include "platform/platform.h"
#include "scene_graph/components/sub_mesh.h"
#include "scene_graph/components/image.h"

#include <bitset>
#include <set>
#include <unordered_map>

// Entrypoints of VK_AMDX_shader_enqueue
static PFN_vkCreateExecutionGraphPipelinesAMDX			vkCreateExecutionGraphPipelinesAMDX;
static PFN_vkGetExecutionGraphPipelineScratchSizeAMDX	vkGetExecutionGraphPipelineScratchSizeAMDX;
static PFN_vkGetExecutionGraphPipelineNodeIndexAMDX      vkGetExecutionGraphPipelineNodeIndexAMDX;
static PFN_vkCmdInitializeGraphScratchMemoryAMDX			vkCmdInitializeGraphScratchMemoryAMDX;
static PFN_vkCmdDispatchGraphAMDX						vkCmdDispatchGraphAMDX;
static PFN_vkCmdDispatchGraphIndirectAMDX                vkCmdDispatchGraphIndirectAMDX;
static PFN_vkCmdDispatchGraphIndirectCountAMDX           vkCmdDispatchGraphIndirectCountAMDX;

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 joint0;
    glm::vec4 weight0;
};

// Per-instance data, for instanced draws
struct Instance
{
    glm::vec3 pos;
};

struct UniformBuffer
{
    glm::mat4 projection;
    glm::mat4 modelview;
    glm::mat4 inverseProjModelView;
    glm::vec4 lightPos;
    glm::uint highlighted_shader_permutation;
};

static void load_extension_function_pointers(VkDevice device)
{
    vkCreateExecutionGraphPipelinesAMDX        = (PFN_vkCreateExecutionGraphPipelinesAMDX)       vkGetDeviceProcAddr(device, "vkCreateExecutionGraphPipelinesAMDX");
    vkGetExecutionGraphPipelineScratchSizeAMDX = (PFN_vkGetExecutionGraphPipelineScratchSizeAMDX)vkGetDeviceProcAddr(device, "vkGetExecutionGraphPipelineScratchSizeAMDX");
    vkGetExecutionGraphPipelineNodeIndexAMDX   = (PFN_vkGetExecutionGraphPipelineNodeIndexAMDX)  vkGetDeviceProcAddr(device, "vkGetExecutionGraphPipelineNodeIndexAMDX");
    vkCmdInitializeGraphScratchMemoryAMDX      = (PFN_vkCmdInitializeGraphScratchMemoryAMDX)     vkGetDeviceProcAddr(device, "vkCmdInitializeGraphScratchMemoryAMDX");
    vkCmdDispatchGraphAMDX                     = (PFN_vkCmdDispatchGraphAMDX)                    vkGetDeviceProcAddr(device, "vkCmdDispatchGraphAMDX");
    vkCmdDispatchGraphIndirectAMDX             = (PFN_vkCmdDispatchGraphIndirectAMDX)            vkGetDeviceProcAddr(device, "PFN_vkCmdDispatchGraphIndirectAMDX");
    vkCmdDispatchGraphIndirectCountAMDX        = (PFN_vkCmdDispatchGraphIndirectCountAMDX)       vkGetDeviceProcAddr(device, "PFN_vkCmdDispatchGraphIndirectCountAMDX");
}

/// Scans the specified bit-mask for the most-significant '1' bit.
/// @returns True if the input was nonzero; false otherwise.
static bool bitmask_scan_reverse(
    uint32_t  mask,		  ///< Bit-mask to scan.
    uint32_t* out_index)  ///< [out] Index of most-significant '1' bit.  Undefined if input is zero.
{
    bool result = false;

    if (mask != 0)
    {
        uint32_t index = 31u;
        for (; (((mask >> index) & 0x1) == 0); --index);
        *out_index = index;
    }
    if (mask != 0)
    {
        result = (mask != 0);
    }
    return result;
}

GpuDispatch::GpuDispatch()
{
    set_api_version(VK_MAKE_VERSION(1, 2, 0));
}

void GpuDispatch::finish()
{
    VulkanSample::finish();

    if (device)
    {
        for (auto& frame_data : per_frame_data)
        {
            vkDestroyFramebuffer(device->get_handle(), frame_data.framebuffer, nullptr);
            vkDestroyFramebuffer(device->get_handle(), frame_data.gui_framebuffer, nullptr);
        }

        for (auto& shader_module : shader_module_cache)
        {
            vkDestroyShaderModule(device->get_handle(), shader_module.second, nullptr);
        }

        for (auto& pipeline : compose_pipelines)
        {
            vkDestroyPipeline(device->get_handle(), pipeline, nullptr);
        }

        vkDestroyPipelineCache(device->get_handle(), pipeline_cache, nullptr);
        vkDestroySampler(device->get_handle(), default_sampler, nullptr);
        vkDestroySampler(device->get_handle(), texture_sampler, nullptr);
        vkDestroyPipeline(device->get_handle(), classify_pipeline, nullptr);
        vkDestroyPipeline(device->get_handle(), graphics_pipeline, nullptr);
        vkDestroyRenderPass(device->get_handle(), render_pass, nullptr);
        vkDestroyRenderPass(device->get_handle(), gui_render_pass, nullptr);
        vkDestroyDescriptorSetLayout(device->get_handle(), descriptor_set_layout, nullptr);
        vkDestroyDescriptorSetLayout(device->get_handle(), compose_descriptor_set_layout, nullptr);
        vkDestroyDescriptorSetLayout(device->get_handle(), classify_descriptor_set_layout, nullptr);
        vkDestroyDescriptorPool(device->get_handle(), descriptor_pool, nullptr);
        vkDestroyPipelineLayout(device->get_handle(), compose_pipeline_layout, nullptr);
        vkDestroyPipelineLayout(device->get_handle(), classify_pipeline_layout, nullptr);
        vkDestroyPipelineLayout(device->get_handle(), graphics_pipeline_layout, nullptr);
    }
}

void GpuDispatch::request_gpu_features(vkb::PhysicalDevice &gpu)
{
    auto &requested_features = gpu.get_mutable_requested_features();

    // Clamp if supported, it's better performance
    requested_features.depthClamp        = gpu.get_features().depthClamp;
    requested_features.samplerAnisotropy = gpu.get_features().samplerAnisotropy;

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

    // Request the BDA extension -- this is how the framework enables support in VMA
    add_device_extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    const auto& bufferDeviceAddressFeatures = gpu.request_extension_features<VkPhysicalDeviceBufferDeviceAddressFeatures>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);

    assert(bufferDeviceAddressFeatures.bufferDeviceAddress == VK_TRUE);

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

void GpuDispatch::input_event(const vkb::InputEvent &input_event)
{
    Application::input_event(input_event);

    if (gui)
    {
        gui->input_event(input_event);
    }

    if (input_event.get_source() == vkb::EventSource::Keyboard)
    {
        const auto &key_event = static_cast<const vkb::KeyInputEvent &>(input_event);

        // [1, 127] is valid for non-material map scenes, number of permutations = 127
        // [0, 127] is valid for material map scenes, number of permutations = 128
        const uint32_t min_permutation = is_material_map_scene() ? 0 : 1;
        const uint32_t max_permutation = num_deferred_shader_permutations() - (is_material_map_scene() ? 1 : 0);

        if ((key_event.get_action() == vkb::KeyAction::Down) ||
            (key_event.get_action() == vkb::KeyAction::Repeat))
        {
            if (key_event.get_code() == vkb::KeyCode::Q)
            {
                if ((highlighted_shader_permutation == ShaderPermutationNone) ||
                    (highlighted_shader_permutation >= max_permutation))
                {
                    highlighted_shader_permutation = min_permutation;
                }
                else
                {
                    highlighted_shader_permutation += 1;
                }
            }
            else if (key_event.get_code() == vkb::KeyCode::W)
            {
                if ((highlighted_shader_permutation == ShaderPermutationNone) ||
                    (highlighted_shader_permutation <= min_permutation))
                {
                    highlighted_shader_permutation = max_permutation;
                }
                else
                {
                    highlighted_shader_permutation -= 1;
                }
            }
            else if (key_event.get_code() == vkb::KeyCode::E)
            {
                highlighted_shader_permutation = ShaderPermutationNone;
            }
        }
    }
}

void GpuDispatch::draw_gui()
{
    if (scene == SCENE_SANITY_CHECK)
    {
        gui->show_options_window(
            /* body = */ [this]() {
                ImGui::Text(use_hlsl_shaders ? "[HLSL]" : "[GLSL]");
            },
            /* lines = */ 1);
    }
    else
    {
        gui->show_options_window(
            /* body = */ [this]() {
                {
                    constexpr uint32_t MaxBits = 10;
                    assert(num_material_bits <= MaxBits);

                    const auto bits_string = std::bitset<MaxBits>(highlighted_shader_permutation).to_string().substr(MaxBits - num_material_bits);

                    ImGui::Text(use_hlsl_shaders ? "[HLSL]" : "[GLSL]");
                    ImGui::SameLine();

                    if (highlighted_shader_permutation == ShaderPermutationNone)
                    {
                        ImGui::Text("Highlighted shader: none");
                    }
                    else
                    {
                        ImGui::Text("Highlighted shader: %s (%s)",
                            std::to_string(highlighted_shader_permutation).c_str(),
                            bits_string.c_str());
                    }
                }
            },
            /* lines = */ 1);
    }
}

void GpuDispatch::prepare_render_context()
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
        VK_IMAGE_USAGE_TRANSFER_DST_BIT,		// in case we use deferred_clear_swapchain_image
    }));
}

std::unique_ptr<vkb::RenderTarget> GpuDispatch::create_render_target(vkb::core::Image &&swapchain_image)
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

    vkb::core::Image material_image {
        device,
        extent,
        VK_FORMAT_R32_UINT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    };

    vkb::core::Image normal_image {
        device,
        extent,
        VK_FORMAT_A2R10G10B10_UNORM_PACK32,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    };

    vkb::core::Image texcoord_image {
        device,
        extent,
        VK_FORMAT_R16G16_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    };

    std::vector<vkb::core::Image> images;

    images.push_back(std::move(swapchain_image));
    images.push_back(std::move(depth_image));
    images.push_back(std::move(material_image));
    images.push_back(std::move(normal_image));
    images.push_back(std::move(texcoord_image));

    return std::make_unique<vkb::RenderTarget>(std::move(images));
}

bool GpuDispatch::prepare(vkb::Platform &platform)
{
    if (!VulkanSample::prepare(platform))
    {
        return false;
    }

    // Handle command line options
    for (auto& arg : platform.get_generic_options())
    {
        if (arg == "present_single")
        {
            present_mode = PRESENT_MODE_SINGLE;
        }
        else if (arg == "present_burst")
        {
            present_mode = PRESENT_MODE_BURST;
        }
        else if (arg == "clear_image")
        {
            deferred_clear_swapchain_image = true;
        }
        else if (arg == "scene_teapot")
        {
            scene = SCENE_TEAPOT;
        }
        else if (arg == "scene_monkeys")
        {
            scene = SCENE_MONKEYS;
        }
        else if (arg == "scene_material_1")
        {
            scene = SCENE_MATERIAL_MAP_1;
        }
        else if (arg == "scene_material_2")
        {
            scene = SCENE_MATERIAL_MAP_2;
        }
        else if (arg == "scene_sanity")
        {
            if (is_shader_enqueue_supported)
            {
                scene = SCENE_SANITY_CHECK;
            }
            else
            {
                LOGW("scene_sanity option is not supported.")
            }
        }
        else if (arg == "no_animation")
        {
            rotate_animation = false;
        }
        else if (arg == "graph_fixed_exp")
        {
            graph_type = ENQUEUE_GRAPH_TYPE_FIXED_EXPANSION;
        }
        else if (arg == "graph_dynamic_exp")
        {
            graph_type = ENQUEUE_GRAPH_TYPE_DYNAMIC_EXPANSION;
        }
        else if (arg == "graph_aggregation")
        {
            graph_type = ENQUEUE_GRAPH_TYPE_AGGREGATION;
        }
        else if (arg.rfind("materials_", 0) == 0)
        {
            // Format: materials_X
            // X is an integer between 1 and 9.

            auto prefix_len = std::string("materials_").size();
            auto value_str  = arg.substr(prefix_len);

            num_material_bits = 1 + glm::clamp(std::atoi(value_str.c_str()), 1, 9);

            // materials = 1 is the default. 1 bit for the background, 1 bit for the model material = 3 permutations
            // with 0b00 case being illegal, as we will always draw something, at least the background.
            //
            // materials = 2 is 1 bit for the background, 2 bits for the model = 7 permutations
            // materials = 3 is 1 + 3 bits = 15 permutations
            // materials = 4 is 31 permutations, etc.
            // materials = 5 is 63
            // materials = 6 is 127
            // materials = 7 is 255
            // materials = 8 is 511
            // materials = 9 is 1023
        }
        else if (arg.rfind("instances_", 0) == 0)
        {
            // Format: instances_X
            // X is an integer between 1 and 1024 to set the number of instances.

            auto prefix_len = std::string("instances_").size();
            auto value_str  = arg.substr(prefix_len);

            num_instances = glm::clamp(std::atoi(value_str.c_str()), 1, 1024);
        }
        else if (arg.rfind("alu_complexity_", 0) == 0)
        {
            // Format: alu_complexity_XXX
            // XXX is an integer between 0 and 100 and is divided by 100 to get a number in the [0.0, 1.0] range.
            auto prefix_len  = std::string("alu_complexity_").size();
            auto value_str   = arg.substr(prefix_len);
            auto value_float = static_cast<float>(std::atoi(value_str.c_str()));

            alu_complexity = glm::clamp(value_float / 100.0f, 0.0f, 1.0f);
        }
        else if (arg.rfind("textures_", 0) == 0)
        {
            // Format: textures_X
            // X is an integer between 0 and 16.
            // 0 disables texture sampling. The total number of textures in the scene is materials * textures.
            auto prefix_len = std::string("textures_").size();
            auto value_str  = arg.substr(prefix_len);

            num_textures_per_material = glm::clamp(std::atoi(value_str.c_str()), 0, 16);
        }
        else if (arg == "reset_scratch")
        {
            always_reset_scratch_buffer = true;
            reset_scratch_buffer_inline = true;
        }
        else if (arg == "glsl")
        {
            // This is the default, but add it so that it's recognized
            use_hlsl_shaders = false;
        }
        else if (arg == "hlsl")
        {
            use_hlsl_shaders = true;
        }
        else
        {
            LOGE("Unrecognized option argument: {}", arg);
        }
    }

    load_extension_function_pointers(device->get_handle());

    gui = std::make_unique<vkb::Gui>(*this, platform.get_window(), nullptr, 15.0f, true);

    textures.clear();

    if (scene == SCENE_TEAPOT)
    {
        model = load_model("scenes/teapot.gltf");

        // The teapot scene has no instances and no textures.
        num_instances = 1;
        num_textures_per_material = 0;
    }
    else if (scene == SCENE_MONKEYS)
    {
        // This model is 188,928 vertices (in an indexed draw), or around 63k triangles.
        model = load_model("../assets_local/monkey.gltf");

        source_texture = load_image("textures/checkerboard_rgba.ktx");

        const auto& mip0 = source_texture->get_mipmaps().front();

        // Create multiple textures from the first one. Subtract one for the background.
        for (uint32_t i = 0; i < (num_material_bits - 1) * num_textures_per_material; ++i)
        {
            std::vector<uint8_t> data;
            auto				 mipmaps = std::vector<vkb::sg::Mipmap>(1, mip0);

            textures.push_back(std::make_unique<vkb::sg::Image>(
                source_texture->get_name(),
                std::move(data),
                std::move(mipmaps)));

            textures[i]->create_vk_image(*device);
        }

        textures_ready = false;
    }
    else if (is_material_map_scene())
    {
        if (scene == SCENE_MATERIAL_MAP_1)
        {
            material_map = load_image("../assets_local/nanite_mat_id_01.png");
        }
        else if (scene == SCENE_MATERIAL_MAP_2)
        {
            material_map = load_image("../assets_local/nanite_mat_id_02.png");
        }

        // No instances and no textures.
        num_instances = 1;
        num_textures_per_material = 0;

        uint32_t num_unique_colors;
        material_map = convert_material_id_color_image(*material_map, &num_unique_colors);

        const auto valid_index = bitmask_scan_reverse(num_unique_colors, &num_material_bits);
        assert(valid_index);
        ++num_material_bits;	// convert from an index to a count

        material_map->create_vk_image(*device);
    }

    camera_distance = 1.0f;

    const auto num_frames = vkb::to_u32(get_render_context().get_render_frames().size());
    per_frame_data.resize(num_frames);

    {
        VkPipelineCacheCreateInfo pipeline_cache_create_info = {};
        pipeline_cache_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VK_CHECK(vkCreatePipelineCache(device->get_handle(), &pipeline_cache_create_info, nullptr, &pipeline_cache));
    }

    if (scene == SCENE_SANITY_CHECK)
    {
        {
            std::vector<VkDescriptorPoolSize> pool_sizes;
            pool_sizes.emplace_back(vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, num_frames));		// draw output

            auto descriptor_pool_create_info = vkb::initializers::descriptor_pool_create_info(pool_sizes, num_frames);
            VK_CHECK(vkCreateDescriptorPool(device->get_handle(), &descriptor_pool_create_info, nullptr, &descriptor_pool));
        }
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            bindings.push_back(vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0));  // swapchain (compose output)

            auto descriptor_set_layout_create_info = vkb::initializers::descriptor_set_layout_create_info(bindings.data(), vkb::to_u32(bindings.size()));
            VK_CHECK(vkCreateDescriptorSetLayout(device->get_handle(), &descriptor_set_layout_create_info, nullptr, &compose_descriptor_set_layout));

            auto pipeline_layout_create_info = vkb::initializers::pipeline_layout_create_info(&compose_descriptor_set_layout);
            VK_CHECK(vkCreatePipelineLayout(device->get_handle(), &pipeline_layout_create_info, nullptr, &compose_pipeline_layout));
        }
    }
    else // not SCENE_SANITY_CHECK
    {
        const auto num_textures = vkb::to_u32(textures.size());

        {
            auto sampler_create_info = vkb::initializers::sampler_create_info();
            VK_CHECK(vkCreateSampler(device->get_handle(), &sampler_create_info, nullptr, &default_sampler));
        }
        {
            auto sampler_create_info = vkb::initializers::sampler_create_info();
            sampler_create_info.magFilter		 = VK_FILTER_LINEAR;
            sampler_create_info.minFilter		 = VK_FILTER_LINEAR;
            sampler_create_info.mipmapMode		 = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sampler_create_info.addressModeU     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_create_info.addressModeV     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_create_info.addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sampler_create_info.mipLodBias		 = 0.0f;
            sampler_create_info.anisotropyEnable = device->get_gpu().get_features().samplerAnisotropy;
            sampler_create_info.maxAnisotropy    = sampler_create_info.anisotropyEnable ? get_device().get_gpu().get_properties().limits.maxSamplerAnisotropy : 1.0f;
            sampler_create_info.minLod			 = 0.0f;
            sampler_create_info.maxLod		     = VK_LOD_CLAMP_NONE;

            VK_CHECK(vkCreateSampler(device->get_handle(), &sampler_create_info, nullptr, &texture_sampler));
        }
        {
            // The descriptor pool will be used for all rendering modes
            std::vector<VkDescriptorPoolSize> pool_sizes;
            pool_sizes.emplace_back(vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,		   num_frames * 2));	// ubo (in two pipelines)
            pool_sizes.emplace_back(vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          num_frames));		// draw output
            pool_sizes.emplace_back(vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, num_frames * 4));	// 4 mrts
            pool_sizes.emplace_back(vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,		   num_frames * 2));	// indirect dispatch buffers
            pool_sizes.emplace_back(vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, num_frames));		// material id map

            if (num_textures != 0)
            {
                pool_sizes.emplace_back(vkb::initializers::descriptor_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, num_frames * num_textures));	// texture array
            }

            auto descriptor_pool_create_info = vkb::initializers::descriptor_pool_create_info(pool_sizes, 3 * num_frames);
            VK_CHECK(vkCreateDescriptorPool(device->get_handle(), &descriptor_pool_create_info, nullptr, &descriptor_pool));
        }
        {
            std::array<VkDescriptorSetLayoutBinding, 2> bindings {
                vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0),
                vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),  // material id texture
            };

            bindings[1].pImmutableSamplers = &default_sampler;

            auto descriptor_set_layout_create_info = vkb::initializers::descriptor_set_layout_create_info(bindings.data(), vkb::to_u32(bindings.size()));
            VK_CHECK(vkCreateDescriptorSetLayout(device->get_handle(), &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout));

            auto pipeline_layout_create_info = vkb::initializers::pipeline_layout_create_info(&descriptor_set_layout);
            VK_CHECK(vkCreatePipelineLayout(device->get_handle(), &pipeline_layout_create_info, nullptr, &graphics_pipeline_layout));
        }
        {
            // Used by deferred mode, but let's create them upfront
            std::vector<VkDescriptorSetLayoutBinding> bindings;

            bindings.push_back(vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_COMPUTE_BIT, 0));  // ubo
            bindings.push_back(vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_SHADER_STAGE_COMPUTE_BIT, 1));  // swapchain (compose output)
            bindings.push_back(vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 2));  // gbuffer 0 (material)
            bindings.push_back(vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 3));  // gbuffer 1 (normal)
            bindings.push_back(vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 4));  // gbuffer 2 (texcoord)
            bindings.push_back(vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 5));  // gbuffer 3 (depth)
            bindings.push_back(vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 6, num_textures));  // texture array

            auto texture_samplers_array = std::vector<VkSampler>(num_textures, texture_sampler);

            bindings[2].pImmutableSamplers = &default_sampler;
            bindings[3].pImmutableSamplers = &default_sampler;
            bindings[4].pImmutableSamplers = &default_sampler;
            bindings[5].pImmutableSamplers = &default_sampler;
            bindings[6].pImmutableSamplers = (num_textures != 0) ? texture_samplers_array.data() : nullptr;

            auto descriptor_set_layout_create_info = vkb::initializers::descriptor_set_layout_create_info(bindings.data(), vkb::to_u32(bindings.size()));
            VK_CHECK(vkCreateDescriptorSetLayout(device->get_handle(), &descriptor_set_layout_create_info, nullptr, &compose_descriptor_set_layout));

            auto pipeline_layout_create_info = vkb::initializers::pipeline_layout_create_info(&compose_descriptor_set_layout);
            VK_CHECK(vkCreatePipelineLayout(device->get_handle(), &pipeline_layout_create_info, nullptr, &compose_pipeline_layout));
        }
        {
            // Classification mode

            std::array<VkDescriptorSetLayoutBinding, 2> bindings {
                vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0),	// dispatch commands
                vkb::initializers::descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1),	// per shader combination tile classification
            };

            auto descriptor_set_layout_create_info = vkb::initializers::descriptor_set_layout_create_info(bindings.data(), vkb::to_u32(bindings.size()));
            VK_CHECK(vkCreateDescriptorSetLayout(device->get_handle(), &descriptor_set_layout_create_info, nullptr, &classify_descriptor_set_layout));

            std::array<VkDescriptorSetLayout, 2> layouts {};
            layouts[0] = compose_descriptor_set_layout;
            layouts[1] = classify_descriptor_set_layout;

            auto pipeline_layout_create_info = vkb::initializers::pipeline_layout_create_info(layouts.data(), vkb::to_u32(layouts.size()));
            VK_CHECK(vkCreatePipelineLayout(device->get_handle(), &pipeline_layout_create_info, nullptr, &classify_pipeline_layout));
        }
    }

    // Will create the remaining resources in the update loop.
    resources_ready = false;

    return true;
}

void GpuDispatch::create_gui_render_pass()
{
    VkAttachmentDescription attachment = {};
    attachment.format         = get_render_context().get_format();
    attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachment.initialLayout  = VK_IMAGE_LAYOUT_GENERAL;

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

void GpuDispatch::prepare_resources()
{
    device->wait_idle();

    requires_init_commands = false;

    if ((!textures.empty() || material_map) && !textures_ready)
    {
        requires_init_commands = true;
    }

    VK_CHECK(vkResetDescriptorPool(device->get_handle(), descriptor_pool, 0));

    {
        std::vector<VkPipelineShaderStageCreateInfo> shader_stages;

        shader_stages.emplace_back(load_shader("uioverlay/uioverlay.vert", VK_SHADER_STAGE_VERTEX_BIT));
        shader_stages.emplace_back(load_shader("uioverlay/uioverlay.frag", VK_SHADER_STAGE_FRAGMENT_BIT));

        create_gui_render_pass();
        gui->prepare(pipeline_cache, gui_render_pass, shader_stages);
    }

    if (scene != SCENE_SANITY_CHECK)
    {
        {
            std::vector<VkAttachmentDescription> all_attachments;
            std::vector<VkAttachmentReference>   color_refs;
            VkAttachmentReference                depth_ref  = {};
            VkAttachmentDescription				 attachment = {};

            // Color attachments
            attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            {
                attachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;	// CS sampling

                // Material
                attachment.format = VK_FORMAT_R32_UINT;
                color_refs.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
                all_attachments.push_back(attachment);

                // Normal
                attachment.format = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
                color_refs.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
                all_attachments.push_back(attachment);

                // Texcoord
                attachment.format = VK_FORMAT_R16G16_UNORM;
                color_refs.push_back({ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
                all_attachments.push_back(attachment);
            }

            auto& first_render_target = get_render_context().get_render_frames().at(0)->get_render_target();

            // Depth attachment
            attachment.format         = first_render_target.get_views().at(MRT_DEPTH).get_format();
            attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            {
                attachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
                attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            }

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

            vkDestroyRenderPass(device->get_handle(), render_pass, nullptr);
            VK_CHECK(vkCreateRenderPass(device->get_handle(), &render_pass_create_info, nullptr, &render_pass));
        }
        {
            std::array<VkVertexInputBindingDescription, 2> vertex_bindings {
                vkb::initializers::vertex_input_binding_description(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX),
                vkb::initializers::vertex_input_binding_description(1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE),
            };
            std::array<VkVertexInputAttributeDescription, 4> vertex_attributes {
                vkb::initializers::vertex_input_attribute_description(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),
                vkb::initializers::vertex_input_attribute_description(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)),
                vkb::initializers::vertex_input_attribute_description(0, 2, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)),
                vkb::initializers::vertex_input_attribute_description(1, 3, VK_FORMAT_R32G32B32_SFLOAT, 0),
            };

            auto vertex_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
            vertex_input_state.vertexBindingDescriptionCount   = vkb::to_u32(vertex_bindings.size());
            vertex_input_state.pVertexBindingDescriptions      = vertex_bindings.data();
            vertex_input_state.vertexAttributeDescriptionCount = vkb::to_u32(vertex_attributes.size());
            vertex_input_state.pVertexAttributeDescriptions    = vertex_attributes.data();

            auto input_assembly_state = vkb::initializers::pipeline_input_assembly_state_create_info(
                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                    0,
                    VK_FALSE);

            auto rasterization_state = vkb::initializers::pipeline_rasterization_state_create_info(
                    VK_POLYGON_MODE_FILL,
                    VK_CULL_MODE_BACK_BIT,
                    VK_FRONT_FACE_CLOCKWISE);
            rasterization_state.depthClampEnable = get_device().get_gpu().get_features().depthClamp;

            const uint32_t num_color_attachments = 3;

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
                VK_DYNAMIC_STATE_SCISSOR
            };

            auto dynamic_state = vkb::initializers::pipeline_dynamic_state_create_info(
                    dynamic_state_enables.data(),
                    vkb::to_u32(dynamic_state_enables.size()));

            auto pipeline_create_info = vkb::initializers::pipeline_create_info(graphics_pipeline_layout, render_pass);

            // Specialization info used by deferred shaders
            struct SpecData
            {
                uint32_t    num_materials;
                uint32_t    num_instances;
            } specialization_data = {};  // zero-initialize

            specialization_data.num_materials = num_material_bits - 1;
            specialization_data.num_instances = num_instances;

            std::vector<VkSpecializationMapEntry> specialization_entries;
            specialization_entries.emplace_back(vkb::initializers::specialization_map_entry(0, 0 * sizeof(uint32_t), sizeof(uint32_t)));
            specialization_entries.emplace_back(vkb::initializers::specialization_map_entry(1, 1 * sizeof(uint32_t), sizeof(uint32_t)));

            auto specialization_info = vkb::initializers::specialization_info(
                vkb::to_u32(specialization_entries.size()),
                specialization_entries.data(),
                sizeof(SpecData),
                &specialization_data);

            std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages {};

            if (is_material_map_scene())
            {
                if (use_hlsl_shaders)
                {
                    shader_stages[0] = load_spv_shader("gpu_dispatch/hlsl/spv/geometry_material_map_vs.spv", VK_SHADER_STAGE_VERTEX_BIT);
                    shader_stages[1] = load_spv_shader("gpu_dispatch/hlsl/spv/geometry_material_map_ps.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
                }
                else
                {
                    shader_stages[0] = load_shader("gpu_dispatch/glsl/geometry_material_map.vert", VK_SHADER_STAGE_VERTEX_BIT);
                    shader_stages[1] = load_shader("gpu_dispatch/glsl/geometry_material_map.frag", VK_SHADER_STAGE_FRAGMENT_BIT);
                }

                // Draws a fullscreen quad instead of a proper model
                vertex_input_state.vertexAttributeDescriptionCount = 0;
                vertex_input_state.vertexBindingDescriptionCount   = 0;
                depth_stencil_state.depthTestEnable  = VK_FALSE;
                depth_stencil_state.depthWriteEnable = VK_FALSE;
            }
            else
            {
                if (use_hlsl_shaders)
                {
                    shader_stages[0] = load_spv_shader("gpu_dispatch/hlsl/spv/geometry_vs.spv", VK_SHADER_STAGE_VERTEX_BIT);
                    shader_stages[1] = load_spv_shader("gpu_dispatch/hlsl/spv/geometry_ps.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
                }
                else
                {
                    shader_stages[0] = load_shader("gpu_dispatch/glsl/geometry.vert", VK_SHADER_STAGE_VERTEX_BIT);
                    shader_stages[1] = load_shader("gpu_dispatch/glsl/geometry.frag", VK_SHADER_STAGE_FRAGMENT_BIT);
                }

                shader_stages[1].pSpecializationInfo = &specialization_info;
            }

            pipeline_create_info.pVertexInputState   = &vertex_input_state;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pColorBlendState    = &color_blend_state;
            pipeline_create_info.pMultisampleState   = &multisample_state;
            pipeline_create_info.pViewportState      = &viewport_state;
            pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
            pipeline_create_info.pDynamicState       = &dynamic_state;
            pipeline_create_info.stageCount          = vkb::to_u32(shader_stages.size());
            pipeline_create_info.pStages             = shader_stages.data();

            vkDestroyPipeline(device->get_handle(), graphics_pipeline, nullptr);
            VK_CHECK(vkCreateGraphicsPipelines(device->get_handle(), pipeline_cache, 1, &pipeline_create_info, nullptr, &graphics_pipeline));

        }
        {
            instance_buffer = std::make_unique<vkb::core::Buffer>(
                *device,
                num_instances * sizeof(Instance),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU,
                VMA_ALLOCATION_CREATE_MAPPED_BIT);

            Instance data = {};

            if (num_instances == 1)
            {
                instance_buffer->convert_and_update(data);
            }
            else
            {
                // First instance is in the center
                data.pos = glm::vec3(0.0f);
                instance_buffer->convert_and_update(data, 0);

                const float spacing = 2.0f;
                float       dist    = spacing;
                float       steps   = 6.0f;
                float       angle   = glm::radians(360.0f / steps);
                int         next    = static_cast<int>(steps);

                for (uint32_t i = 0; i < num_instances - 1; ++i)
                {
                    if (i == next)
                    {
                        steps *= (dist + spacing) / dist;
                        dist  += spacing;
                        angle  = glm::radians(360.0f / steps);
                        next   = i + static_cast<int>(steps);

                        // Adjust the camera to cover most of the scene.
                        camera_distance = 1.0f + 0.1f * (dist - spacing);
                    }

                    float x = dist * glm::cos(i * angle + glm::half_pi<float>());
                    float y = dist * glm::sin(i * angle + glm::half_pi<float>());

                    data.pos = glm::vec3(x, 0.0f, y);
                    instance_buffer->convert_and_update(data, (i + 1)*sizeof(Instance));
                }
            }
        }
    }

    {
        // Declare all data that may be needed for the pipeline creation.
        struct SpecData
        {
            uint32_t    view_width;
            uint32_t    view_height;
            uint32_t    num_materials;
            uint32_t	num_textures_per_material;
            uint32_t    shader_permutation;
            float       alu_complexity;
            bool        use_texture_array;
        };

        auto all_shader_stages      = std::vector<VkPipelineShaderStageCreateInfo>();
        auto node_info              = std::vector<VkPipelineShaderStageNodeCreateInfoAMDX>();
        auto specialization_data    = std::vector<SpecData>();
        auto specialization_info    = std::vector<VkSpecializationInfo>();
        auto specialization_entries = std::vector<VkSpecializationMapEntry>();

        if (scene == SCENE_SANITY_CHECK)
        {
            const uint32_t NumShaders = 4;

            all_shader_stages.resize(NumShaders);
            node_info.resize(NumShaders);

            node_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NODE_CREATE_INFO_AMDX;
            node_info[0].pNext = nullptr;
            node_info[0].index = 0;

            node_info[1] = node_info[0];
            node_info[2] = node_info[0];
            node_info[3] = node_info[0];

            int i = 0;

            if (use_hlsl_shaders)
            {
                all_shader_stages[i] = load_spv_shader("gpu_dispatch/hlsl/spv/sanity_entry_cs.spv", VK_SHADER_STAGE_COMPUTE_BIT);
            }
            else
            {
                all_shader_stages[i] = load_shader("gpu_dispatch/glsl/sanity_entry.comp", VK_SHADER_STAGE_COMPUTE_BIT);
            }
            all_shader_stages[i].pNext = &node_info[i];
            node_info[i].pName = "main";
            ++i;

            if (use_hlsl_shaders)
            {
                all_shader_stages[i] = load_spv_shader("gpu_dispatch/hlsl/spv/sanity_fixed_exp_cs.spv", VK_SHADER_STAGE_COMPUTE_BIT);
            }
            else
            {
                all_shader_stages[i] = load_shader("gpu_dispatch/glsl/sanity_fixed_exp.comp", VK_SHADER_STAGE_COMPUTE_BIT);
            }
            all_shader_stages[i].pNext = &node_info[i];
            node_info[i].pName = "fixed_exp";
            ++i;

            if (use_hlsl_shaders)
            {
                all_shader_stages[i] = load_spv_shader("gpu_dispatch/hlsl/spv/sanity_dynamic_exp_cs.spv", VK_SHADER_STAGE_COMPUTE_BIT);
            }
            else
            {
                all_shader_stages[i] = load_shader("gpu_dispatch/glsl/sanity_dynamic_exp.comp", VK_SHADER_STAGE_COMPUTE_BIT);
            }
            all_shader_stages[i].pNext = &node_info[i];
            node_info[i].pName = "dynamic_exp";
            ++i;

            if (use_hlsl_shaders)
            {
                all_shader_stages[i] = load_spv_shader("gpu_dispatch/hlsl/spv/sanity_aggregation_cs.spv", VK_SHADER_STAGE_COMPUTE_BIT);
            }
            else
            {
                all_shader_stages[i] = load_shader("gpu_dispatch/glsl/sanity_aggregation.comp", VK_SHADER_STAGE_COMPUTE_BIT);
            }
            all_shader_stages[i].pNext = &node_info[i];
            node_info[i].pName = "aggregation";
            ++i;
        }
        else // not SCENE_SANITY_CHECK
        {
            const uint32_t NumMaterials = num_material_bits - (is_material_map_scene() ? 0 : 1);
            const uint32_t NumShaders   = 1 + num_deferred_shader_permutations();	// entrypoint and specializations

            // Adds preprocessor defines for the shaders.
            vkb::ShaderVariant variant = {};
            std::string        hlslSuffix;      // HLSL requires precompiled variants (because we use SPV binaries)

            if (graph_type == ENQUEUE_GRAPH_TYPE_DYNAMIC_EXPANSION)
            {
                variant.add_define("NODE_DYNAMIC_EXPANSION");
                hlslSuffix = "de";
            }
            else if (graph_type == ENQUEUE_GRAPH_TYPE_AGGREGATION)
            {
                variant.add_define("NODE_AGGREGATION");
                hlslSuffix = "a";
            }
            else
            {
                hlslSuffix = "fe";
            }

            // Each shader will have a different specialization
            all_shader_stages   .resize(NumShaders);
            node_info           .resize(NumShaders);
            specialization_data .resize(NumShaders);
            specialization_info .resize(NumShaders);

            specialization_entries.clear();

            assert(sizeof(float) == sizeof(uint32_t));
            const auto extent        = get_render_context().get_surface_extent();
            const auto num_constants = sizeof(SpecData) / sizeof(uint32_t);

            for (uint32_t i = 0; i < num_constants; ++i)
            {
                specialization_entries.emplace_back(vkb::initializers::specialization_map_entry(i, i * sizeof(uint32_t), sizeof(uint32_t)));
            }

            // Entrypoint shader
            specialization_data[0].view_width                = extent.width;
            specialization_data[0].view_height               = extent.height;
            specialization_data[0].num_materials             = NumMaterials;
            specialization_data[0].num_textures_per_material = 0;							// not used by this shader
            specialization_data[0].shader_permutation        = 0;							// not used by this shader
            specialization_data[0].alu_complexity            = 0.0f;						// not used by this shader
            specialization_data[0].use_texture_array         = false;						// not used by this shader

            specialization_info[0] = vkb::initializers::specialization_info(
                vkb::to_u32(specialization_entries.size()),
                specialization_entries.data(),
                sizeof(SpecData),
                &specialization_data[0]);

            VkPipelineShaderStageCreateInfo shader_stage = {};

            if (use_hlsl_shaders)
            {
                std::ostringstream shader_name_stream;
                if (is_material_map_scene())
                {
                    shader_name_stream << "gpu_dispatch/hlsl/spv/classify_material_map_gpu_enqueue_cs_" << hlslSuffix << ".spv";
                }
                else
                {
                    shader_name_stream << "gpu_dispatch/hlsl/spv/classify_gpu_enqueue_cs_" << hlslSuffix << ".spv";
                }
                shader_stage = load_spv_shader(shader_name_stream.str(), VK_SHADER_STAGE_COMPUTE_BIT);
            }
            else
            {
                std::string shader_name = is_material_map_scene() ? "gpu_dispatch/glsl/classify_material_map_gpu_enqueue.comp"
                                                                  : "gpu_dispatch/glsl/classify_gpu_enqueue.comp";
                shader_stage = load_shader(shader_name, VK_SHADER_STAGE_COMPUTE_BIT, variant);
            }

            {
                shader_stage.pSpecializationInfo = &specialization_info[0];

                shader_stage.pNext = &node_info[0];

                node_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NODE_CREATE_INFO_AMDX;
                node_info[0].pNext = nullptr;
                node_info[0].pName = "classify";
                node_info[0].index = 0;

                all_shader_stages[0] = shader_stage;
            }

            if (num_textures_per_material != 0)
            {
                variant.add_define("USE_TEXTURE_ARRAY");
            }

            std::string shader_name;

            if (use_hlsl_shaders)
            {
                std::ostringstream shader_name_stream;
                if (is_material_map_scene())
                {
                    shader_name_stream << "gpu_dispatch/hlsl/spv/compose_material_map_gpu_enqueue_cs_" << hlslSuffix << ".spv";
                }
                else
                {
                    shader_name_stream << "gpu_dispatch/hlsl/spv/compose_gpu_enqueue_cs_" << hlslSuffix << ".spv";
                }
                shader_name = shader_name_stream.str();
            }
            else
            {
                shader_name = is_material_map_scene() ? "gpu_dispatch/glsl/compose_material_map_gpu_enqueue.comp"
                                                      : "gpu_dispatch/glsl/compose_gpu_enqueue.comp";
            }

            // Compose shaders
            for (uint32_t permutation_ndx = 1; permutation_ndx < NumShaders; ++permutation_ndx)
            {
                // Permutation is zero-based for material map case, one-based otherwise.
                const auto shader_permutation = permutation_ndx - (is_material_map_scene() ? 1 : 0);

                specialization_data[permutation_ndx].view_width                = extent.width;
                specialization_data[permutation_ndx].view_height               = extent.height;
                specialization_data[permutation_ndx].num_materials             = NumMaterials;
                specialization_data[permutation_ndx].num_textures_per_material = num_textures_per_material;
                specialization_data[permutation_ndx].shader_permutation        = shader_permutation;
                specialization_data[permutation_ndx].alu_complexity            = alu_complexity;
                specialization_data[permutation_ndx].use_texture_array         = (num_textures_per_material != 0);

                specialization_info[permutation_ndx] = vkb::initializers::specialization_info(
                    vkb::to_u32(specialization_entries.size()),
                    specialization_entries.data(),
                    sizeof(SpecData),
                    &specialization_data[permutation_ndx]);

                if (use_hlsl_shaders)
                {
                    shader_stage = load_spv_shader(shader_name, VK_SHADER_STAGE_COMPUTE_BIT);
                }
                else
                {
                    shader_stage = load_shader(shader_name, VK_SHADER_STAGE_COMPUTE_BIT, variant);
                }

                shader_stage.pSpecializationInfo = &specialization_info[permutation_ndx];

                shader_stage.pNext = &node_info[permutation_ndx];

                node_info[permutation_ndx].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NODE_CREATE_INFO_AMDX;
                node_info[permutation_ndx].pNext = nullptr;
                node_info[permutation_ndx].pName = "compose";
                node_info[permutation_ndx].index = permutation_ndx - 1;

                all_shader_stages[permutation_ndx] = shader_stage;
            }
        }

        // Create the pipeline
        {
            VkExecutionGraphPipelineCreateInfoAMDX pipelineCreateInfo = {};
            pipelineCreateInfo.sType              = VK_STRUCTURE_TYPE_EXECUTION_GRAPH_PIPELINE_CREATE_INFO_AMDX;
            pipelineCreateInfo.flags              = 0;
            pipelineCreateInfo.stageCount         = vkb::to_u32(all_shader_stages.size());
            pipelineCreateInfo.pStages            = all_shader_stages.data();
            pipelineCreateInfo.pLibraryInfo       = nullptr;
            pipelineCreateInfo.layout             = compose_pipeline_layout;    // The compose pipeline layout has the right bindings
            pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
            pipelineCreateInfo.basePipelineIndex  = -1;

            LOGI("Creating execution graph pipeline...");

            vkDestroyPipeline(device->get_handle(), classify_and_compose_pipeline, nullptr);

            const auto start_time = std::chrono::steady_clock::now();

            VK_CHECK(vkCreateExecutionGraphPipelinesAMDX(device->get_handle(), pipeline_cache, 1, &pipelineCreateInfo, nullptr, &classify_and_compose_pipeline));

            const auto time_elapsed_millis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
            LOGI("Done. Compilation time: {} milliseconds", time_elapsed_millis);

            // Get required amount of scratch memory
            enqueue_scratch_buffer_size.sType = VK_STRUCTURE_TYPE_EXECUTION_GRAPH_PIPELINE_SCRATCH_SIZE_AMDX;
            enqueue_scratch_buffer_size.pNext = nullptr;

            VK_CHECK(vkGetExecutionGraphPipelineScratchSizeAMDX(device->get_handle(), classify_and_compose_pipeline, &enqueue_scratch_buffer_size));

            LOGI("Using scratch buffer size = {}", enqueue_scratch_buffer_size.size);
        }
    }

    for (uint32_t frame_ndx = 0; frame_ndx < per_frame_data.size(); ++frame_ndx)
    {
        auto& frame_data = per_frame_data[frame_ndx];
        auto& frame = *get_render_context().get_render_frames()[frame_ndx].get();
        auto& rt = frame.get_render_target();

        frame_data.enqueue_scratch_buffer_ready = false;

        {
            auto image_view = rt.get_views().at(MRT_SWAPCHAIN).get_handle();

            auto framebuffer_create_info = vkb::initializers::framebuffer_create_info();
            framebuffer_create_info.renderPass      = gui_render_pass;
            framebuffer_create_info.attachmentCount	= 1;
            framebuffer_create_info.pAttachments    = &image_view;
            framebuffer_create_info.width           = rt.get_extent().width;
            framebuffer_create_info.height          = rt.get_extent().height;
            framebuffer_create_info.layers          = 1;

            vkDestroyFramebuffer(device->get_handle(), frame_data.gui_framebuffer, nullptr);
            VK_CHECK(vkCreateFramebuffer(device->get_handle(), &framebuffer_create_info, nullptr, &frame_data.gui_framebuffer));
        }

        if (scene == SCENE_SANITY_CHECK)
        {
            auto descriptor_set_allocate_info = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &compose_descriptor_set_layout, 1);
            VK_CHECK(vkAllocateDescriptorSets(device->get_handle(), &descriptor_set_allocate_info, &frame_data.compose_descriptor_set));

            VkDescriptorImageInfo output_descriptor_image_info = {};
            output_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            output_descriptor_image_info.imageView   = rt.get_views().at(MRT_SWAPCHAIN).get_handle();

            std::vector<VkWriteDescriptorSet> writes;
            writes.push_back(vkb::initializers::write_descriptor_set(frame_data.compose_descriptor_set, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, &output_descriptor_image_info));

            vkUpdateDescriptorSets(device->get_handle(), vkb::to_u32(writes.size()), writes.data(), 0, nullptr);
        }
        else // not SCENE_SANITY_CHECK
        {
            {
                std::array<VkImageView, 4> image_views {};
                image_views[0] = rt.get_views().at(MRT_MATERIAL).get_handle();
                image_views[1] = rt.get_views().at(MRT_NORMAL).get_handle();
                image_views[2] = rt.get_views().at(MRT_TEXCOORD).get_handle();
                image_views[3] = rt.get_views().at(MRT_DEPTH).get_handle();

                auto framebuffer_create_info = vkb::initializers::framebuffer_create_info();
                framebuffer_create_info.renderPass      = render_pass;
                framebuffer_create_info.attachmentCount	= vkb::to_u32(image_views.size());
                framebuffer_create_info.pAttachments    = image_views.data();
                framebuffer_create_info.width           = rt.get_extent().width;
                framebuffer_create_info.height          = rt.get_extent().height;
                framebuffer_create_info.layers          = 1;

                vkDestroyFramebuffer(device->get_handle(), frame_data.framebuffer, nullptr);
                VK_CHECK(vkCreateFramebuffer(device->get_handle(), &framebuffer_create_info, nullptr, &frame_data.framebuffer));
            }
            {
                frame_data.uniform_buffer = std::make_unique<vkb::core::Buffer>(
                    *device,
                    sizeof(UniformBuffer),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU,
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);

                auto descriptor_set_allocate_info = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layout, 1);
                VK_CHECK(vkAllocateDescriptorSets(device->get_handle(), &descriptor_set_allocate_info, &frame_data.descriptor_set));

                VkDescriptorBufferInfo descriptor_buffer_info = {};
                descriptor_buffer_info.buffer = frame_data.uniform_buffer->get_handle();
                descriptor_buffer_info.offset = 0;
                descriptor_buffer_info.range  = sizeof(UniformBuffer);

                std::vector<VkWriteDescriptorSet> writes;
                writes.push_back(vkb::initializers::write_descriptor_set(frame_data.descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &descriptor_buffer_info));

                if (is_material_map_scene())
                {
                    VkDescriptorImageInfo image_info = {};
                    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    image_info.imageView   = material_map->get_vk_image_view().get_handle();

                    writes.push_back(vkb::initializers::write_descriptor_set(frame_data.descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &image_info));
                }

                vkUpdateDescriptorSets(device->get_handle(), vkb::to_u32(writes.size()), writes.data(), 0, nullptr);

                {
                    descriptor_set_allocate_info.pSetLayouts = &compose_descriptor_set_layout;
                    VK_CHECK(vkAllocateDescriptorSets(device->get_handle(), &descriptor_set_allocate_info, &frame_data.compose_descriptor_set));

                    VkDescriptorImageInfo output_descriptor_image_info = {};
                    output_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    output_descriptor_image_info.imageView   = rt.get_views().at(MRT_SWAPCHAIN).get_handle();

                    VkDescriptorImageInfo material_descriptor_image_info = {};
                    material_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    material_descriptor_image_info.imageView   = rt.get_views().at(MRT_MATERIAL).get_handle();

                    VkDescriptorImageInfo normal_descriptor_image_info = {};
                    normal_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    normal_descriptor_image_info.imageView   = rt.get_views().at(MRT_NORMAL).get_handle();

                    VkDescriptorImageInfo texcoord_descriptor_image_info = {};
                    texcoord_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    texcoord_descriptor_image_info.imageView   = rt.get_views().at(MRT_TEXCOORD).get_handle();

                    VkDescriptorImageInfo depth_descriptor_image_info = {};
                    depth_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                    depth_descriptor_image_info.imageView   = rt.get_views().at(MRT_DEPTH).get_handle();

                    writes.clear();
                    writes.push_back(vkb::initializers::write_descriptor_set(frame_data.compose_descriptor_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         0, &descriptor_buffer_info));
                    writes.push_back(vkb::initializers::write_descriptor_set(frame_data.compose_descriptor_set, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, &output_descriptor_image_info));
                    writes.push_back(vkb::initializers::write_descriptor_set(frame_data.compose_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &material_descriptor_image_info));
                    writes.push_back(vkb::initializers::write_descriptor_set(frame_data.compose_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &normal_descriptor_image_info));
                    writes.push_back(vkb::initializers::write_descriptor_set(frame_data.compose_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, &texcoord_descriptor_image_info));
                    writes.push_back(vkb::initializers::write_descriptor_set(frame_data.compose_descriptor_set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5, &depth_descriptor_image_info));

                    std::vector<VkDescriptorImageInfo> texture_descriptor_image_infos;

                    if (scene == SCENE_MONKEYS)
                    {
                        for (uint32_t i = 0; i < textures.size(); ++i)
                        {
                            VkDescriptorImageInfo image_info = {};
                            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                            image_info.imageView   = textures[i]->get_vk_image_view().get_handle();

                            texture_descriptor_image_infos.push_back(image_info);
                        }
                        if (!textures.empty())
                        {
                            writes.push_back(vkb::initializers::write_descriptor_set(
                                frame_data.compose_descriptor_set,
                                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                6,
                                texture_descriptor_image_infos.data(),
                                vkb::to_u32(texture_descriptor_image_infos.size())));
                        }
                    }

                    vkUpdateDescriptorSets(device->get_handle(), vkb::to_u32(writes.size()), writes.data(), 0, nullptr);
                }
            }

            {
                const auto ratio = static_cast<float>(get_render_context().get_surface_extent().width)
                                 / static_cast<float>(get_render_context().get_surface_extent().height);

                camera.type = vkb::CameraType::LookAt;
                camera.set_perspective(60.0f, ratio, 256.0f, 1.0f);

                if (scene == SCENE_TEAPOT)
                {
                    camera.set_translation(glm::vec3(0.0f, -0.25f, -5.0f));
                    camera.set_rotation(glm::vec3(-32.0f, 20.0f, 0.0f));
                }
                else if (scene == SCENE_MONKEYS)
                {
                    camera.set_translation(camera_distance * glm::vec3(0.0f, -0.25f, -5.0f));
                    camera.set_rotation(glm::vec3(-32.0f, 140.0f, 0.0f));
                }
                else if (is_material_map_scene())
                {
                    camera.matrices.perspective = glm::mat4(1.0f);
                    camera.matrices.view        = glm::mat4(1.0f);
                }
                else
                {
                    assert(false);
                }
            }
        }

        {
            if (enqueue_scratch_buffer_size.size != 0)
            {
                frame_data.enqueue_scratch_buffer = std::make_unique<vkb::core::Buffer>(
                    *device,
                    enqueue_scratch_buffer_size.size,
                    VK_BUFFER_USAGE_EXECUTION_GRAPH_SCRATCH_BIT_AMDX | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VMA_MEMORY_USAGE_GPU_ONLY,
                    0); // no VMA flags

                if (reset_scratch_buffer_inline == false)
                {
                    requires_init_commands = true;
                }
            }
        }
    }

    resources_ready = true;
}

VkPipelineShaderStageCreateInfo GpuDispatch::load_shader(
    const std::string&          file,
    VkShaderStageFlagBits       stage,
    const vkb::ShaderVariant&   variant)
{
    VkPipelineShaderStageCreateInfo shader_stage_create_info = {};
    shader_stage_create_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage_create_info.stage  = stage;
    shader_stage_create_info.pName  = "main";

    auto module_iter = shader_module_cache.find(file);
    if (module_iter == shader_module_cache.end())
    {
        shader_stage_create_info.module = vkb::load_shader(file, device->get_handle(), stage, variant);
        assert(shader_stage_create_info.module != VK_NULL_HANDLE);

        shader_module_cache.insert({file, shader_stage_create_info.module});
    }
    else
    {
        shader_stage_create_info.module = module_iter->second;
    }

    return shader_stage_create_info;
}

VkPipelineShaderStageCreateInfo GpuDispatch::load_spv_shader(
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

bool GpuDispatch::resize(uint32_t width, uint32_t height)
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

void GpuDispatch::update_gui(float delta_time)
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

void GpuDispatch::update(float delta_time)
{
    {
        get_render_context().begin_frame();
        auto acquire_semaphore = get_render_context().consume_acquired_semaphore();

        auto& frame = get_render_context().get_active_frame();
        auto& graphics_queue = device->get_suitable_graphics_queue();

        if (!resources_ready)
        {
            prepare_resources();

            if (requires_init_commands)
            {
                auto& cmd_buf = frame.request_command_buffer(graphics_queue);
                cmd_buf.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

                record_init_commands(cmd_buf);

                cmd_buf.end();

                auto submit_info = vkb::initializers::submit_info();
                submit_info.commandBufferCount = 1;
                submit_info.pCommandBuffers    = &cmd_buf.get_handle();

                auto fence = frame.request_fence();

                VK_CHECK(graphics_queue.submit({submit_info}, fence));

                vkWaitForFences(device->get_handle(), 1, &fence, VK_TRUE, ~0);

                // Free temporary resources
                staging_buffer.reset();
                source_texture.reset();
            }
        }

        update_gui(delta_time);

        {
            auto& cmd_buf = frame.request_command_buffer(graphics_queue);
            cmd_buf.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

            record_active_frame_commands(cmd_buf, delta_time);

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

        // Optional: stagger the presents
        if (frame_count != 0)
        {
            const auto num_frames = vkb::to_u32(get_render_context().get_render_frames().size());

            if ((present_mode == PRESENT_MODE_SINGLE) ||
                ((present_mode == PRESENT_MODE_BURST) && ((frame_count % num_frames) == 0)))
            {
                std::this_thread::sleep_for(std::chrono::duration<float>(2));
            }
        }

        ++frame_count;
    }
    // Don't call VulkanSample::update(), it depends on the RenderPipeline and Scene which we don't use.
}

void GpuDispatch::record_init_commands(vkb::CommandBuffer& cmd_buf)
{
    {
        for (auto& frame_data : per_frame_data)
        {
            assert(frame_data.enqueue_scratch_buffer_ready == false);
            record_scratch_buffer_reset(cmd_buf, frame_data);
        }
    }

    if (!textures.empty() && !textures_ready)
    {
        assert(source_texture);

        staging_buffer = std::make_unique<vkb::core::Buffer>(
            cmd_buf.get_device(),
            source_texture->get_data().size(),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY);

        staging_buffer->update(source_texture->get_data());

        const auto &mipmaps = source_texture->get_mipmaps();

        VkImageSubresourceRange full_subresource_range = {};
        full_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        full_subresource_range.levelCount = 1;	// not using mipmaps
        full_subresource_range.layerCount = 1;

        std::vector<VkImageMemoryBarrier> image_barriers;

        for (uint32_t texture_ndx = 0; texture_ndx < textures.size(); ++texture_ndx)
        {
            auto barrier = vkb::initializers::image_memory_barrier();
            barrier.image            = textures[texture_ndx]->get_vk_image().get_handle();
            barrier.srcAccessMask    = 0;
            barrier.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.subresourceRange = full_subresource_range;

            image_barriers.push_back(barrier);
        }

        vkCmdPipelineBarrier(
            cmd_buf.get_handle(),
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            vkb::to_u32(image_barriers.size()), image_barriers.data());

        // Copy only the mipmap level 0.
        for (uint32_t texture_ndx = 0; texture_ndx < textures.size(); ++texture_ndx)
        {
            VkBufferImageCopy copy_region = {};
            copy_region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.mipLevel       = 0;
            copy_region.imageSubresource.baseArrayLayer = 0;
            copy_region.imageSubresource.layerCount     = 1;
            copy_region.imageExtent					    = mipmaps[0].extent;
            copy_region.bufferOffset                    = 0;

            vkCmdCopyBufferToImage(
                cmd_buf.get_handle(),
                staging_buffer->get_handle(),
                textures[texture_ndx]->get_vk_image().get_handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copy_region);
        }

        image_barriers.clear();

        for (uint32_t texture_ndx = 0; texture_ndx < textures.size(); ++texture_ndx)
        {
            auto barrier = vkb::initializers::image_memory_barrier();
            barrier.image            = textures[texture_ndx]->get_vk_image().get_handle();
            barrier.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.subresourceRange = full_subresource_range;

            image_barriers.push_back(barrier);
        }

        vkCmdPipelineBarrier(
            cmd_buf.get_handle(),
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            vkb::to_u32(image_barriers.size()), image_barriers.data());

        textures_ready = true;
    }
    else if (material_map && !textures_ready)
    {
        staging_buffer = std::make_unique<vkb::core::Buffer>(
            cmd_buf.get_device(),
            material_map->get_data().size(),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY);

        staging_buffer->update(material_map->get_data());

        const auto &mipmaps = material_map->get_mipmaps();

        VkImageSubresourceRange full_subresource_range = {};
        full_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        full_subresource_range.levelCount = 1;	// not using mipmaps
        full_subresource_range.layerCount = 1;

        {
            auto barrier = vkb::initializers::image_memory_barrier();
            barrier.image            = material_map->get_vk_image().get_handle();
            barrier.srcAccessMask    = 0;
            barrier.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.subresourceRange = full_subresource_range;

            vkCmdPipelineBarrier(
                cmd_buf.get_handle(),
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1,
                &barrier);
        }

        // Copy only the mipmap level 0.
        {
            VkBufferImageCopy copy_region = {};
            copy_region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.mipLevel       = 0;
            copy_region.imageSubresource.baseArrayLayer = 0;
            copy_region.imageSubresource.layerCount     = 1;
            copy_region.imageExtent					    = mipmaps[0].extent;
            copy_region.bufferOffset                    = 0;

            vkCmdCopyBufferToImage(
                cmd_buf.get_handle(),
                staging_buffer->get_handle(),
                material_map->get_vk_image().get_handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copy_region);
        }

        {
            auto barrier = vkb::initializers::image_memory_barrier();
            barrier.image            = material_map->get_vk_image().get_handle();
            barrier.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.subresourceRange = full_subresource_range;

            vkCmdPipelineBarrier(
                cmd_buf.get_handle(),
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1,
                &barrier);
        }

        textures_ready = true;
    }
}

void GpuDispatch::record_scratch_buffer_reset(vkb::CommandBuffer& cmd_buf, PerFrame& frame_data)
{
    {
        auto barrier = vkb::initializers::buffer_memory_barrier();
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        barrier.buffer        = frame_data.enqueue_scratch_buffer->get_handle();
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

    vkCmdBindPipeline(cmd_buf.get_handle(), VK_PIPELINE_BIND_POINT_EXECUTION_GRAPH_AMDX, classify_and_compose_pipeline);
    vkCmdInitializeGraphScratchMemoryAMDX(cmd_buf.get_handle(), frame_data.enqueue_scratch_buffer->get_device_address());

    {
        auto barrier = vkb::initializers::buffer_memory_barrier();
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        barrier.buffer        = frame_data.enqueue_scratch_buffer->get_handle();
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

    frame_data.enqueue_scratch_buffer_ready = true;
}

void GpuDispatch::record_active_frame_commands(vkb::CommandBuffer& cmd_buf, float delta_time)
{
    auto& frame = get_render_context().get_active_frame();
    auto& rt = frame.get_render_target();
    auto& frame_data = per_frame_data.at(get_render_context().get_active_frame_index());

    const VkViewport viewport = vkb::initializers::viewport(static_cast<float>(rt.get_extent().width), static_cast<float>(rt.get_extent().height), 0.0f, 1.0f);
    const VkRect2D   scissor  = vkb::initializers::rect2D(rt.get_extent().width, rt.get_extent().height, 0, 0);
    vkCmdSetViewport(cmd_buf.get_handle(), 0, 1, &viewport);
    vkCmdSetScissor(cmd_buf.get_handle(), 0, 1, &scissor);

    if (scene != SCENE_SANITY_CHECK)
    {
        // Update CPU uniforms
        {
            constexpr auto _2pi = static_cast<float>(2.0 * glm::pi<double>());
            static float angle = 0.0f;

            if (rotate_animation)
            {
                angle += delta_time * 0.3f;
                if (angle > _2pi) angle -= _2pi;
            }
            else
            {
                angle = 0.0f;
            }

            UniformBuffer ubo {};

            auto model_matrix = glm::rotate(glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            auto rotation_anim = glm::rotate(angle, glm::vec3(0.0f, 1.0f, 0.0f));

            ubo.projection = camera.matrices.perspective;
            ubo.modelview  = camera.matrices.view * rotation_anim * model_matrix;
            ubo.inverseProjModelView = glm::inverse(ubo.projection * ubo.modelview);
            ubo.lightPos   = glm::vec4(5.0f, 5.0f, 0.0f, 1.0f);
            ubo.highlighted_shader_permutation = highlighted_shader_permutation;

            frame_data.uniform_buffer->convert_and_update(ubo);

            // CPU mappable memory is implicitly made available to the device
        }
        {
            std::vector<VkClearValue> clear_values;
            {
                clear_values.push_back({ 0.0f, 0.0f, 0.0f, 0.0f });
                clear_values.push_back({ 0.0f, 0.0f, 0.0f, 0.0f });
                clear_values.push_back({ 0.0f, 0.0f, 0.0f, 0.0f });
                clear_values.push_back({ 0.0f, 0 });
            }

            auto render_pass_begin_info = vkb::initializers::render_pass_begin_info();
            render_pass_begin_info.renderPass        = render_pass;
            render_pass_begin_info.framebuffer       = frame_data.framebuffer;
            render_pass_begin_info.renderArea.extent = rt.get_extent();
            render_pass_begin_info.renderArea.offset = {0, 0};
            render_pass_begin_info.clearValueCount   = vkb::to_u32(clear_values.size());
            render_pass_begin_info.pClearValues      = clear_values.data();

            vkCmdBeginRenderPass(cmd_buf.get_handle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
        }

        draw_scene(cmd_buf, rt, frame_data);

        vkCmdEndRenderPass(cmd_buf.get_handle());

        {
            {
                auto barrier = vkb::initializers::image_memory_barrier();
                barrier.image            = rt.get_views().at(MRT_DEPTH).get_image().get_handle();
                barrier.srcAccessMask    = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                barrier.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
                barrier.oldLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                barrier.newLayout        = barrier.oldLayout;
                barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

                vkCmdPipelineBarrier(
                    cmd_buf.get_handle(),
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier);
            }
            {
                std::vector<VkImageMemoryBarrier> barriers;

                auto barrier = vkb::initializers::image_memory_barrier();
                barrier.srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barrier.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
                barrier.oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.newLayout        = barrier.oldLayout;
                barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                barrier.image            = rt.get_views().at(MRT_MATERIAL).get_image().get_handle();
                barriers.push_back(barrier);

                barrier.image            = rt.get_views().at(MRT_NORMAL).get_image().get_handle();
                barriers.push_back(barrier);

                barrier.image            = rt.get_views().at(MRT_TEXCOORD).get_image().get_handle();
                barriers.push_back(barrier);

                vkCmdPipelineBarrier(
                    cmd_buf.get_handle(),
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    vkb::to_u32(barriers.size()), barriers.data());
            }
        }

    }

    {
        if (deferred_clear_swapchain_image)
        {
            // Only for debugging, otherwise the whole image is overwritten anyway.
            {
                auto barrier = vkb::initializers::image_memory_barrier();
                barrier.srcAccessMask    = 0;
                barrier.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
                barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                barrier.image            = rt.get_views().at(MRT_SWAPCHAIN).get_image().get_handle();

                vkCmdPipelineBarrier(
                    cmd_buf.get_handle(),
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier);
            }
            {
                VkClearColorValue color = { 0.0f, 0.0f, 1.0f, 1.0f };
                VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

                vkCmdClearColorImage(
                    cmd_buf.get_handle(),
                    rt.get_views().at(MRT_SWAPCHAIN).get_image().get_handle(),
                    VK_IMAGE_LAYOUT_GENERAL,
                    &color,
                    1,
                    &range);
            }
            {
                auto barrier = vkb::initializers::image_memory_barrier();
                barrier.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.oldLayout        = VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
                barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                barrier.image            = rt.get_views().at(MRT_SWAPCHAIN).get_image().get_handle();

                vkCmdPipelineBarrier(
                    cmd_buf.get_handle(),
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier);
            }
        }
        else
        {
            auto barrier = vkb::initializers::image_memory_barrier();
            barrier.srcAccessMask    = 0;
            barrier.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barrier.image            = rt.get_views().at(MRT_SWAPCHAIN).get_image().get_handle();

            vkCmdPipelineBarrier(
                cmd_buf.get_handle(),
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier);
        }

        {
            if (reset_scratch_buffer_inline && (always_reset_scratch_buffer || (frame_data.enqueue_scratch_buffer_ready == false)))
            {
                record_scratch_buffer_reset(cmd_buf, frame_data);
            }

            vkCmdBindDescriptorSets(
                cmd_buf.get_handle(),
                VK_PIPELINE_BIND_POINT_EXECUTION_GRAPH_AMDX,
                compose_pipeline_layout,
                0,                                  // first set
                1,                                  // set count
                &frame_data.compose_descriptor_set,
                0,                                  // dynamic offsets count
                nullptr);                           // dynamic offsets

            // Classify shader is a dynamic expansion node, so we need to
            // provide the dispatch size as the first element of the payload
            VkDispatchIndirectCommand dispatch_size = {};
            dispatch_size.x = get_compute_tiles_extent().width;
            dispatch_size.y = get_compute_tiles_extent().height;
            dispatch_size.z = 1;

            // vkCmdDispatchGraphAMDX uses all parameters from the host
            VkDispatchGraphInfoAMDX dispatch_info = {};
            dispatch_info.nodeIndex              = 0;   // will be set later
            dispatch_info.payloadCount           = 1;
            dispatch_info.payloads.hostAddress   = &dispatch_size;
            dispatch_info.payloadStride          = sizeof(VkDispatchIndirectCommand);

            VkDispatchGraphCountInfoAMDX dispatch_count_info = {};

            dispatch_count_info.count             = 1;
            dispatch_count_info.stride            = sizeof(VkDispatchGraphInfoAMDX);
            dispatch_count_info.infos.hostAddress = &dispatch_info;

            // Update the opaque node index used by the dispatch function
            VkPipelineShaderStageNodeCreateInfoAMDX nodeInfo = {};
            nodeInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NODE_CREATE_INFO_AMDX;
            nodeInfo.pName = (scene == SCENE_SANITY_CHECK) ? "main" : "classify";
            nodeInfo.index = 0;

            VK_CHECK(vkGetExecutionGraphPipelineNodeIndexAMDX(device->get_handle(), classify_and_compose_pipeline, &nodeInfo, &dispatch_info.nodeIndex));

            vkCmdBindPipeline(cmd_buf.get_handle(), VK_PIPELINE_BIND_POINT_EXECUTION_GRAPH_AMDX, classify_and_compose_pipeline);
            vkCmdDispatchGraphAMDX(cmd_buf.get_handle(), frame_data.enqueue_scratch_buffer->get_device_address(), &dispatch_count_info);
        }
        {
            // A barrier for the UI draw.
            auto barrier = vkb::initializers::image_memory_barrier();
            barrier.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.oldLayout        = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout        = barrier.oldLayout;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barrier.image            = rt.get_views().at(MRT_SWAPCHAIN).get_image().get_handle();

            vkCmdPipelineBarrier(
                cmd_buf.get_handle(),
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier);
        }
    }

    if (gui)
    {
        auto render_pass_begin_info = vkb::initializers::render_pass_begin_info();
        render_pass_begin_info.renderPass        = gui_render_pass;
        render_pass_begin_info.framebuffer       = frame_data.gui_framebuffer;
        render_pass_begin_info.renderArea.extent = rt.get_extent();
        render_pass_begin_info.renderArea.offset = {0, 0};

        vkCmdBeginRenderPass(cmd_buf.get_handle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        gui->draw(cmd_buf.get_handle());

        vkCmdEndRenderPass(cmd_buf.get_handle());
    }
}

std::unique_ptr<vkb::sg::SubMesh> GpuDispatch::load_model(const std::string &file, uint32_t index)
{
    vkb::GLTFLoader loader{*device};

    std::unique_ptr<vkb::sg::SubMesh> model = loader.read_model_from_file(file, index);

    if (!model)
    {
        LOGE("Cannot load model from file: {}", file.c_str());
        throw std::runtime_error("Cannot load model from file: " + file);
    }

    return model;
}

std::unique_ptr<vkb::sg::Image> GpuDispatch::load_image(const std::string& file)
{
    auto image = vkb::sg::Image::load(file, file, vkb::sg::Image::Color);

    if (!image)
    {
        LOGE("Cannot load image from file: {}", file.c_str());
        throw std::runtime_error("Cannot load image from file: " + file);
    }

    return image;
}

/**
* Convert a material id image where each material has a unique color and output an image where the materials
* are indexed from 0 up.
*/
std::unique_ptr<vkb::sg::Image> GpuDispatch::convert_material_id_color_image(
    const vkb::sg::Image&	material_color,
    uint32_t*				out_num_colors)
{
    std::vector<uint8_t> data;
    auto mipmaps = material_color.get_mipmaps();
    const auto mip0 = mipmaps[0];
    const auto num_pixels = mip0.extent.width * mip0.extent.height;

    assert(material_color.get_data().size() % sizeof(uint32_t) == 0);
    assert(num_pixels == (material_color.get_data().size() / sizeof(uint32_t)));

    // Material ids will be stored here.
    data.resize(num_pixels * sizeof(uint32_t));

    auto color_ptr = reinterpret_cast<const uint32_t*>(material_color.get_data().data());
    auto id_ptr    = reinterpret_cast<uint32_t*>(data.data());

    // Pass 1: collect all unique colors and assign their indices
    const auto unique_colors = std::set<uint32_t>(color_ptr, color_ptr + mip0.extent.width * mip0.extent.height);
    std::unordered_map<uint32_t, uint32_t> color_lookup;
    uint32_t i = 0;
    for (auto& color : unique_colors)
    {
        color_lookup.insert({ color, i++ });
    }

    assert(out_num_colors);
    *out_num_colors = vkb::to_u32(unique_colors.size());

    // Pass 2: convert colors to indices
    for (uint32_t i = 0; i < num_pixels; ++i)
    {
        id_ptr[i] = color_lookup.find(color_ptr[i])->second;
    }

    return std::make_unique<vkb::sg::Image>(
        material_color.get_name(),
        std::move(data),
        std::move(mipmaps),
        VK_FORMAT_R32_UINT);
}

void GpuDispatch::draw_scene(vkb::CommandBuffer& cmd_buf, vkb::RenderTarget& rt, PerFrame& frame_data)
{

    vkCmdBindPipeline(cmd_buf.get_handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
    vkCmdBindDescriptorSets(cmd_buf.get_handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_layout, 0, 1, &frame_data.descriptor_set, 0, nullptr);

    if (is_material_map_scene())
    {
        vkCmdDraw(cmd_buf.get_handle(), 3, 1, 0, 0);
    }
    else
    {
        draw_model(model, cmd_buf.get_handle());
    }
}

void GpuDispatch::draw_model(std::unique_ptr<vkb::sg::SubMesh> &model, VkCommandBuffer command_buffer)
{
    VkDeviceSize offsets[2] = {0};

    auto &   index_buffer      = model->index_buffer;
    VkBuffer vertex_bindings[] =
    {
        *model->vertex_buffers.at("vertex_buffer").get(),
        *instance_buffer->get(),
    };

    vkCmdBindVertexBuffers(command_buffer, 0, 2, vertex_bindings, offsets);
    vkCmdBindIndexBuffer(command_buffer, index_buffer->get_handle(), 0, model->index_type);
    vkCmdDrawIndexed(command_buffer, model->vertex_indices, num_instances, 0, 0, 0);
}

VkExtent2D GpuDispatch::get_compute_tiles_extent()
{
    assert(TileSize > 0);
    auto extent = get_render_context().get_surface_extent();

    extent.width  = (extent.width  + TileSize - 1) / TileSize;
    extent.height = (extent.height + TileSize - 1) / TileSize;

    return extent;
}

std::unique_ptr<vkb::VulkanSample> create_gpu_dispatch()
{
    return std::make_unique<GpuDispatch>();
}
