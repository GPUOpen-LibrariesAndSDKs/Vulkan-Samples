/* Copyright (c) 2019-2021, Sascha Willems
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Basic example for hardware accelerated ray tracing using VK_AMDX_dense_geometry_format, VK_KHR_ray_tracing_pipeline, and VK_KHR_acceleration_structure
 */

#pragma once

#include "api_vulkan_sample.h"
#include "glsl_compiler.h"

#include "vk_amdx_dense_geometry_format.h"

// Holds data for a scratch buffer used as a temporary storage during acceleration structure builds
struct DGFScratchBuffer
{
	uint64_t       device_address;
	VkBuffer       handle;
	VkDeviceMemory memory;
};

// Wraps all data required for an acceleration structure
struct DGFAccelerationStructure
{
	VkAccelerationStructureKHR         handle;
	uint64_t                           device_address;
	std::unique_ptr<vkb::core::Buffer> buffer;
};

class DenseGeometryFormat : public ApiVulkanSample
{
  public:
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR  ray_tracing_pipeline_properties{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features{};

	DGFAccelerationStructure bottom_level_acceleration_structure;
	DGFAccelerationStructure top_level_acceleration_structure;

#if defined(USE_DENSE_GEOMETRY_FORMAT)
	std::unique_ptr<vkb::core::Buffer>                dgf_buffer;
#else
	std::unique_ptr<vkb::core::Buffer>                vertex_buffer;
	std::unique_ptr<vkb::core::Buffer>                index_buffer;
	uint32_t                                          index_count;
#endif

	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups{};

	std::unique_ptr<vkb::core::Buffer> raygen_shader_binding_table;
	std::unique_ptr<vkb::core::Buffer> miss_shader_binding_table;
	std::unique_ptr<vkb::core::Buffer> hit_shader_binding_table;

	struct StorageImage
	{
		VkDeviceMemory memory;
		VkImage        image = VK_NULL_HANDLE;
		VkImageView    view;
		VkFormat       format;
		uint32_t       width;
		uint32_t       height;
	} storage_image;

	struct UniformData
	{
		glm::mat4 view_inverse;
		glm::mat4 proj_inverse;
	} uniform_data;
	std::unique_ptr<vkb::core::Buffer> ubo;

	VkPipeline            pipeline;
	VkPipelineLayout      pipeline_layout;
	VkDescriptorSet       descriptor_set;
	VkDescriptorSetLayout descriptor_set_layout;

	DenseGeometryFormat();
	~DenseGeometryFormat();

	void          request_gpu_features(vkb::PhysicalDevice &gpu) override;
	uint64_t      get_buffer_device_address(VkBuffer buffer);
	DGFScratchBuffer create_scratch_buffer(VkDeviceSize size);
	void          delete_scratch_buffer(DGFScratchBuffer &scratch_buffer);
	void          create_storage_image();
	void          create_bottom_level_acceleration_structure();
	void          create_top_level_acceleration_structure();
	void          delete_acceleration_structure(DGFAccelerationStructure &acceleration_structure);
	void          create_scene();
	void          create_shader_binding_tables();
	void          create_descriptor_sets();
	void          create_ray_tracing_pipeline();
	void          create_uniform_buffer();
	void          build_command_buffers() override;
	void          update_uniform_buffers();
	void          draw();
	bool          prepare(vkb::Platform &platform) override;
	virtual void  render(float delta_time) override;

#if defined(USE_DENSE_GEOMETRY_FORMAT)
	virtual void on_update_ui_overlay(vkb::Drawer &drawer) override;
	void         LoadDGFFile(VkAccelerationStructureDenseGeometryFormatTrianglesDataAMDX &DGFTriangleData);

	VkDeviceSize dgfDataSize;
	uint32_t     dgfNumTriangles;
	uint32_t     dgfNumVertices;
	uint32_t     dgfMaxPrimitiveIndex;
	uint32_t     dgfMaxGeometryIndex;
#endif
};


std::unique_ptr<vkb::VulkanSample> create_dense_geometry_format();
