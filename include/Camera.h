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

class Camera
{
public:
	glm::vec3 position{0.0f, 0.0f, 3.0f};
	float yaw   = -90.0f;
	float pitch =   0.0f;

	float fov    = 60.0f;
	float zNear  = 0.2f;
	float zFar   = 50.0f;

	float sensitivity = 1.0f;

	glm::mat4 view() const
	{
		glm::vec3 dir;
		dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
		dir.y = sin(glm::radians(pitch));
		dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));

		glm::vec3 front = glm::normalize(dir);
		glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0,1,0)));
		glm::vec3 up    = glm::normalize(glm::cross(right, front));

		return glm::lookAt(position, position + front, up);
	}

	glm::mat4 proj(float aspect) const
	{
		glm::mat4 P = glm::perspective(glm::radians(fov), aspect, zNear, zFar);
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

	void addScroll(float dy)
	{
		fov -= dy;
		if (fov < 20.0f)
			fov = 20.0f;
		if (fov > 90.0f)
			fov = 90.0f;
	}
};
