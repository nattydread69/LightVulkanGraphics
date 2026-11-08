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

class Camera
{
public:
	enum class Projection
	{
		Perspective,
		// Parallel projection: no foreshortening, sizes on screen do not depend on
		// distance. Typical for top-down/tactical views and 2D-style scenes.
		Orthographic
	};

	glm::vec3 position{0.0f, 0.0f, 3.0f};
	float yaw   = -90.0f;
	float pitch =   0.0f;

	float fov    = 60.0f;
	float zNear  = 0.2f;
	float zFar   = 50.0f;

	Projection projection = Projection::Perspective;
	// Orthographic only: world-space height of the visible volume. The width is
	// orthoHeight * aspect.
	float orthoHeight = 10.0f;

	// Screen-up direction used when the camera looks (almost) straight up or
	// down, where world up and the view direction are parallel and cannot define
	// a basis. Set by setCameraLookAt()'s `up` argument; a zero vector means "use
	// the camera's yaw direction" (looking down, the top of the screen then faces
	// the way the camera was turned).
	glm::vec3 verticalViewUp{0.0f};

	float sensitivity = 1.0f;

	glm::vec3 front() const
	{
		glm::vec3 dir;
		dir.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
		dir.y = std::sin(glm::radians(pitch));
		dir.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
		return glm::normalize(dir);
	}

	// Up vector for the view basis. World +Y except when the view direction is
	// within ~0.8 degrees of vertical, where it falls back to verticalViewUp (or
	// the yaw direction) so straight-down views are well defined instead of NaN.
	glm::vec3 up() const
	{
		const glm::vec3 f = front();
		const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
		if (std::abs(f.y) < 0.9999f)
		{
			return worldUp;
		}
		glm::vec3 hint = verticalViewUp - f * glm::dot(verticalViewUp, f);
		if (glm::length(hint) < 1e-4f)
		{
			// Looking down, "up" on screen is where the camera faces horizontally;
			// looking up, it is the opposite.
			const glm::vec3 heading(std::cos(glm::radians(yaw)), 0.0f, std::sin(glm::radians(yaw)));
			hint = f.y < 0.0f ? heading : -heading;
		}
		return glm::normalize(hint);
	}

	glm::mat4 view() const
	{
		const glm::vec3 f = front();
		return glm::lookAt(position, position + f, up());
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

	void addMouseDelta(float dx, float dy)
	{
		yaw   += dx*sensitivity;
		pitch -= dy*sensitivity;

		if (pitch > 89.0f)
			pitch = 89.0f;
		if (pitch < -89.0f)
			pitch = -89.0f;
	}

	// Scroll zoom: narrows the field of view in perspective, shrinks the visible
	// height in orthographic projection.
	void addScroll(float dy)
	{
		if (projection == Projection::Orthographic)
		{
			orthoHeight = std::clamp(orthoHeight * std::pow(0.9f, dy), 0.01f, 100000.0f);
			return;
		}
		fov -= dy;
		if (fov < 20.0f)
			fov = 20.0f;
		if (fov > 90.0f)
			fov = 90.0f;
	}
};
