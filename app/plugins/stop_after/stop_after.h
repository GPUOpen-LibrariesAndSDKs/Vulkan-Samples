/* Copyright (c) 2020-2021, Arm Limited and Contributors
 * Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.
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

#pragma once

#include "platform/plugins/plugin_base.h"

namespace plugins
{
class StopAfter;

using StopAfterTags = vkb::PluginBase<StopAfter, vkb::tags::Stopping>;

/**
 * @brief Stop After
 *
 * Stop the execution of the app after a specific frame.
 *
 * Usage: vulkan_sample sample afbc --stop-after-frame 100
 *
 */
class StopAfter : public StopAfterTags
{
  public:
	StopAfter();

	virtual ~StopAfter() = default;

	virtual bool is_active(const vkb::CommandParser &parser) override;

	virtual void init(const vkb::CommandParser &parser) override;

	virtual void on_update(float delta_time) override;

	void set_enabled(bool is_enabled);

	vkb::FlagCommand stop_after_frame_flag = {vkb::FlagType::OneValue, "stop-after-frame", "", "Stop the application after a certain number of frames"};
	vkb::FlagCommand stop_after_seconds_flag = {vkb::FlagType::OneValue, "stop-after-seconds", "", "Stop the application after elapsed time in seconds"};

  private:
	bool enabled{false};

	bool use_frames{false};
	uint32_t remaining_frames{0};

	bool use_time{false};
	float remaining_time{0.0f};
};
}        // namespace plugins