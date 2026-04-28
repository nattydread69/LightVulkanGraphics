// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2026 Dr. Nathanael John Inkson

#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace lightGraphics
{
	constexpr std::size_t MaxForwardLights = 16;
	using LightHandle = std::size_t;

	enum class LightType : int
	{
		Directional = 0,
		Point = 1,
		Spot = 2
	};

	struct LightSource
	{
		LightType type = LightType::Directional;
		glm::vec3 position{0.0f};
		glm::vec3 direction{0.0f, -1.0f, 0.0f};
		glm::vec3 color{1.0f};
		float intensity = 1.0f;
		float range = 10.0f;
		float innerConeAngleRadians = glm::radians(20.0f);
		float outerConeAngleRadians = glm::radians(30.0f);
		bool enabled = true;
		bool castsShadow = false;
		uint32_t shadowMapSize = 1024;
		float shadowBias = 0.0015f;
		float shadowNormalBias = 0.01f;
		float shadowStrength = 0.65f;
		float shadowNear = 0.1f;
		float shadowFar = 50.0f;
		float shadowOrthoSize = 20.0f;
		std::string name;
	};
}
