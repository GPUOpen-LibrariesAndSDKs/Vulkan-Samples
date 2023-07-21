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

#pragma once

#include "platform/plugins/plugin_base.h"

namespace plugins
{
class GenericOptions;

using GenericOptionsTags = vkb::PluginBase<GenericOptions, vkb::tags::Passive>;

/**
 * @brief Generic Options
 *
 * Additional option parameters to customize a Vulkan Sample.
 *
 * Usage: vulkan_samples sample instancing --option x y z
 *        vulkan_samples sample instancing -o x y z
 *
 */
class GenericOptions : public GenericOptionsTags
{
  public:
	GenericOptions();

	virtual ~GenericOptions() = default;

	virtual bool is_active(const vkb::CommandParser &parser) override;

	virtual void init(const vkb::CommandParser &options) override;

	vkb::FlagCommand options = {vkb::FlagType::ManyValues, "option", "o", "Various sample-specific options, --option o1 o2 ..."};

	vkb::CommandGroup generic_options_group = {"Options", {&options}};
};

}        // namespace plugins