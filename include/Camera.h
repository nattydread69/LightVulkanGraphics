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

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

// Data layout is part of the libLightVulkanGraphics.so.3 ABI (Camera is a VkApp
// member): add behaviour through member functions, never new data members. The
// 3.2 projection settings live in VkApp's side table (src/VkAppExtras.h).
class Camera
{
public:
	enum class Projection
	{
		Perspective,
		// Parallel projection: no foreshortening, sizes on screen do not depend on
		// distance. Typical for top-down/tactical views and 2D-style scenes. (3.2+)
		Orthographic
	};

	glm::vec3 position{0.0f, 0.0f, 3.0f};
	float yaw   = -90.0f;
	float pitch =   0.0f;

	float fov    = 60.0f;
	float zNear  = 0.2f;
	float zFar   = 50.0f;

	float sensitivity = 1.0f;

	glm::vec3 front() const
	{
		glm::vec3 dir;
		dir.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
		dir.y = std::sin(glm::radians(pitch));
		dir.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
		return glm::normalize(dir);
	}

	// Up vector of the view basis: world +Y, except when the view is within ~0.8
	// degrees of vertical, where world up is parallel to the view and cannot orient
	// the image. There it uses `verticalViewUp` (e.g. setCameraLookAt()'s up
	// argument) or, if that is zero or also vertical, the yaw direction -- looking
	// down, the top of the screen faces the way the camera was turned. (3.2+)
	glm::vec3 up(const glm::vec3& verticalViewUp = glm::vec3(0.0f)) const
	{
		const glm::vec3 f = front();
		if (std::abs(f.y) < 0.9999f)
		{
			return glm::vec3(0.0f, 1.0f, 0.0f);
		}
		glm::vec3 hint = verticalViewUp - f * glm::dot(verticalViewUp, f);
		if (glm::length(hint) < 1e-4f)
		{
			const glm::vec3 heading(std::cos(glm::radians(yaw)), 0.0f, std::sin(glm::radians(yaw)));
			hint = f.y < 0.0f ? heading : -heading;
		}
		return glm::normalize(hint);
	}

	// Well defined for every orientation, including straight up and down.
	glm::mat4 view(const glm::vec3& verticalViewUp) const
	{
		return glm::lookAt(position, position + front(), up(verticalViewUp));
	}

	glm::mat4 view() const
	{
		return view(glm::vec3(0.0f));
	}

	// Vulkan clip space: depth 0 at zNear, 1 at zFar (the *_ZO projections), Y down.
	// GLM's default projections target OpenGL's -1..1 depth instead, which Vulkan
	// clips at 0 -- that silently discarded the nearer half of the depth range
	// (everything nearer than (zNear + zFar) / 2 in orthographic views).
	glm::mat4 proj(float aspect) const
	{
		glm::mat4 P;
		if (projection == Projection::Orthographic)
		{
			const float halfHeight = std::max(orthoHeight, 1e-4f) * 0.5f;
			const float halfWidth = halfHeight * aspect;
			P = glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, zNear, zFar);
		}
		else
		{
			P = glm::perspectiveRH_ZO(glm::radians(fov), aspect, zNear, zFar);
		}
		P[1][1] *= -1.0f; // Vulkan clip-space fix
		return P;
	}

	// Perspective uses fov; orthographic shows a volume `orthoHeight` world units
	// tall (width = orthoHeight * aspect). (3.2+)
	glm::mat4 projection(float aspect, Projection mode, float orthoHeight) const
	{
		if (mode != Projection::Orthographic)
		{
			return proj(aspect);
		}
		const float halfHeight = std::max(orthoHeight, 1e-4f) * 0.5f;
		const float halfWidth = halfHeight * aspect;
		// Vulkan clip depth is [0, 1]. glm::ortho's default OpenGL [-1, 1] range would
		// clip the whole near half of the volume (depth is linear), so use the ZO
		// variant. proj() keeps its historical mapping, where only a sliver next to
		// the near plane is affected. ViewFrustum's near plane stays conservative for
		// either convention.
		glm::mat4 P = glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, zNear, zFar);
		P[1][1] *= -1.0f;
		return P;
	}

	void addMouseDelta(float dx, float dy)
	{
		yaw   += dx*sensitivity;
		pitch -= dy*sensitivity;

		if (pitch > 89.0f)
			pitch = 89.0f;
		if (pitch < -89.0f)
			pitch = -89.0f;
	}

	void addScroll(float dy)
	{
		fov -= dy;
		if (fov < 20.0f)
			fov = 20.0f;
		if (fov > 90.0f)
			fov = 90.0f;
	}
};
