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

#include "stop_after.h"

namespace plugins
{
StopAfter::StopAfter() :
    StopAfterTags("Stop After X",
                  "A collection of flags to stop the running application after a set period.",
                  {vkb::Hook::OnUpdate},
                  {&stop_after_frame_flag, &stop_after_seconds_flag})
{
}

bool StopAfter::is_active(const vkb::CommandParser &parser)
{
	return parser.contains(&stop_after_frame_flag) || parser.contains(&stop_after_seconds_flag);
}

void StopAfter::init(const vkb::CommandParser &parser)
{
	enabled = true;

	remaining_frames = parser.as<uint32_t>(&stop_after_frame_flag);
	use_frames = (remaining_frames > 0);

	remaining_time = parser.as<float>(&stop_after_seconds_flag);
	use_time = (remaining_time > 0.0f);
}

void StopAfter::on_update(float delta_time)
{
	if (enabled)
	{
		remaining_frames--;
		remaining_time -= delta_time;

		if ((use_frames && (remaining_frames <= 0)) ||
			(use_time && (remaining_time <= 0.0f)))
		{
			platform->close();
		}
	}
}

void StopAfter::set_enabled(bool is_enabled)
{
	enabled = is_enabled;
}

}        // namespace plugins
