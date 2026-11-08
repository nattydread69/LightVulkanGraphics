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

#include "Camera.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lightGraphics::detail
{
	struct ObjectRenderFlags
	{
		bool visible = true;
		float emissive = 0.0f;
	};

	// State added in 3.2 that would otherwise be new VkApp members. Keeping it in a
	// per-application side table leaves VkApp's layout -- and with it the
	// libLightVulkanGraphics.so.3 ABI -- unchanged, the same approach the 3.2 volume
	// render styles use. Only ever touched from the application's owner thread; the
	// table itself is mutex-protected because several applications may exist.
	struct AppExtras
	{
		bool mouseCameraEnabled = true;
		Camera::Projection projection = Camera::Projection::Perspective;
		float orthoHeight = 10.0f;
		// Screen-up hint for (near) vertical views; see Camera::up().
		glm::vec3 verticalViewUp{0.0f};
		// Index-aligned with VkApp::_objects_ but grown lazily: entries past the end
		// mean default flags, so the addObject() paths need no changes.
		std::vector<ObjectRenderFlags> objectFlags;
		// Pinned directional shadow centres, keyed by light handle slot (stable across
		// removal of other lights).
		std::unordered_map<std::uint32_t, glm::vec3> shadowCenters;

		[[nodiscard]] ObjectRenderFlags flagsFor(std::size_t const index) const
		{
			return index < objectFlags.size() ? objectFlags[index] : ObjectRenderFlags{};
		}
	};

	// Created on first use; references stay valid until forgetAppExtras(app).
	AppExtras& appExtras(const void* app);
	void forgetAppExtras(const void* app);
}
