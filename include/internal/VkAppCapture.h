// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2026 Dr. Nathanael John Inkson
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

// Internal (not installed): frame-capture state, kept out of VkApp's members so the
// installed VkApp.h -- and with it the class layout applications compile in -- stays
// as it is (the same approach as VkAppFramePacing.h). One entry per live VkApp.

#include "VkApp.h"

#include <functional>
#include <vector>

namespace lightGraphics::detail
{
	struct CaptureState
	{
		std::vector<std::function<void(const CapturedFrame&)>> pending;
		// The current swapchain was created with VK_IMAGE_USAGE_TRANSFER_SRC_BIT.
		bool swapchainReadable = false;
	};

	CaptureState& captureState(const VkApp* app);
	void forgetCaptureState(const VkApp* app);
}
