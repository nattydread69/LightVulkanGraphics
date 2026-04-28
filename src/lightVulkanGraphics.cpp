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

#include "lightVulkanGraphics.h"

#include <string>

namespace lightGraphics
{

	lightVulkanGraphics::lightVulkanGraphics(std::string const title)
		: lightVulkanGraphics(title, LightVulkanGraphicsCreateInfo{})
	{
	}

	lightVulkanGraphics::lightVulkanGraphics(std::string const title,
	                                         const LightVulkanGraphicsCreateInfo& createInfo)
	{
		setManageGlfwLifecycle(createInfo.manageGlfwLifecycle);
		lightGraphics::setConsoleOutputEnabled(createInfo.enableConsoleOutput);
		setDebugOutput(createInfo.enableDebugOutput);
		setShaderPath(createInfo.shaderPath);
		init(createInfo.width, createInfo.height, title.c_str());
	}

	void lightVulkanGraphics::setLineRenderMode() { VkApp::setRenderMode(VkApp::RenderMode::LINE); }

	// Cylinder connection helpers
	void lightVulkanGraphics::addCylinderBetweenPoints(const glm::vec3& pointA, const glm::vec3& pointB,
														float radius, const glm::vec4& color,
														const std::string& name, float mass)
	{
		VkApp::addCylinderBetweenPoints(pointA, pointB, radius, color, name, mass);
	}

	void lightVulkanGraphics::addCylinderAlongAxis(const glm::vec3& center, const glm::vec3& axis,
													float length, float radius, const glm::vec4& color,
													const std::string& name, float mass)
	{
		VkApp::addCylinderAlongAxis(center, axis, length, radius, color, name, mass);
	}

	void lightVulkanGraphics::addCylinderConnectingSpheres(const glm::vec3& sphereA, const glm::vec3& sphereB,
															float sphereRadiusA, float sphereRadiusB,
															const glm::vec4& color, const std::string& name,
															float mass)
	{
		VkApp::addCylinderConnectingSpheres(sphereA, sphereB, sphereRadiusA, sphereRadiusB, color, name, mass);
	}

// no extra wrappers; everything is inherited
}
