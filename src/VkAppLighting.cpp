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

#include "VkApp.h"
#include "RiggedObject.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace lightGraphics
{
	using detail::GpuLight;
	using detail::LightingBufferObject;

	namespace
	{
		std::string makeLightIndexMessage(const char* operation, size_t index, size_t size)
		{
			std::ostringstream message;
			message << operation << " index " << index << " is out of range for "
			        << size << " lights";
			return message.str();
		}
	}

	void VkApp::updateLightingBuffer(uint32_t imageIndex)
	{
		if (imageIndex >= lightingBuffersMapped_.size())
		{
			throw std::runtime_error("updateLightingBuffer: imageIndex out of range");
		}

		void* dst = lightingBuffersMapped_[imageIndex];
		if (!dst)
		{
			throw std::runtime_error("updateLightingBuffer: mapped pointer is null");
		}

		LightingBufferObject lighting = buildLightingBufferObject();
		std::memcpy(dst, &lighting, sizeof(lighting));
		lightingDataDirty_ = false;
	}

	size_t VkApp::addLight(const lightGraphics::LightSource& light)
	{
		LightSource normalizedLight = light;
		if (glm::length(normalizedLight.direction) > 0.0f)
		{
			normalizedLight.direction = glm::normalize(normalizedLight.direction);
		}
		else
		{
			normalizedLight.direction = glm::vec3(0.0f, -1.0f, 0.0f);
		}
		if (normalizedLight.outerConeAngleRadians < normalizedLight.innerConeAngleRadians)
		{
			std::swap(normalizedLight.innerConeAngleRadians, normalizedLight.outerConeAngleRadians);
		}

		lights_.push_back(normalizedLight);
		lightTransformMatrixOverrides_.push_back(std::nullopt);
		if (lights_.size() == lightGraphics::MaxForwardLights + 1)
		{
			logMessage(LogLevel::Warning,
			           "Only the first " + std::to_string(lightGraphics::MaxForwardLights) +
			           " lights are uploaded by the current forward renderer");
		}
		markLightingDirty();
		return lights_.size() - 1;
	}

	size_t VkApp::addDirectionalLight(const glm::vec3& direction,
	                                  const glm::vec3& color,
	                                  float intensity,
	                                  const std::string& name)
	{
		LightSource light;
		light.type = LightType::Directional;
		light.direction = direction;
		light.color = color;
		light.intensity = intensity;
		light.name = name;
		return addLight(light);
	}

	size_t VkApp::addPointLight(const glm::vec3& position,
	                            const glm::vec3& color,
	                            float intensity,
	                            float range,
	                            const std::string& name)
	{
		LightSource light;
		light.type = LightType::Point;
		light.position = position;
		light.color = color;
		light.intensity = intensity;
		light.range = range;
		light.name = name;
		return addLight(light);
	}

	size_t VkApp::addSpotLight(const glm::vec3& position,
	                           const glm::vec3& direction,
	                           const glm::vec3& color,
	                           float intensity,
	                           float range,
	                           float innerConeAngleRadians,
	                           float outerConeAngleRadians,
	                           const std::string& name)
	{
		LightSource light;
		light.type = LightType::Spot;
		light.position = position;
		light.direction = direction;
		light.color = color;
		light.intensity = intensity;
		light.range = range;
		light.innerConeAngleRadians = innerConeAngleRadians;
		light.outerConeAngleRadians = outerConeAngleRadians;
		light.name = name;
		return addLight(light);
	}

	void VkApp::removeLight(size_t index)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("removeLight", index, lights_.size()));
		}

		lights_.erase(lights_.begin() + static_cast<std::ptrdiff_t>(index));
		if (index < lightTransformMatrixOverrides_.size())
		{
			lightTransformMatrixOverrides_.erase(lightTransformMatrixOverrides_.begin() + static_cast<std::ptrdiff_t>(index));
		}
		sceneGraph_->onLightRemoved(index);
		markLightingDirty();
	}

	void VkApp::clearLights()
	{
		const size_t removedCount = lights_.size();
		lights_.clear();
		lightTransformMatrixOverrides_.clear();
		for (size_t i = 0; i < removedCount; ++i)
		{
			sceneGraph_->onLightRemoved(0);
		}
		markLightingDirty();
	}

	void VkApp::updateLight(size_t index, const lightGraphics::LightSource& light)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("updateLight", index, lights_.size()));
		}

		LightSource normalizedLight = light;
		if (glm::length(normalizedLight.direction) > 0.0f)
		{
			normalizedLight.direction = glm::normalize(normalizedLight.direction);
		}
		else
		{
			normalizedLight.direction = glm::vec3(0.0f, -1.0f, 0.0f);
		}
		if (normalizedLight.outerConeAngleRadians < normalizedLight.innerConeAngleRadians)
		{
			std::swap(normalizedLight.innerConeAngleRadians, normalizedLight.outerConeAngleRadians);
		}

		lights_[index] = normalizedLight;
		sceneGraph_->onLightChanged(index);
		markLightingDirty();
	}

	void VkApp::setLightPosition(size_t index, const glm::vec3& position)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightPosition", index, lights_.size()));
		}
		lights_[index].position = position;
		sceneGraph_->onLightChanged(index);
		markLightingDirty();
	}

	void VkApp::setLightDirection(size_t index, const glm::vec3& direction)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightDirection", index, lights_.size()));
		}
		lights_[index].direction = glm::length(direction) > 0.0f
			? glm::normalize(direction)
			: glm::vec3(0.0f, -1.0f, 0.0f);
		sceneGraph_->onLightChanged(index);
		markLightingDirty();
	}

	void VkApp::setLightColor(size_t index, const glm::vec3& color)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightColor", index, lights_.size()));
		}
		lights_[index].color = color;
		markLightingDirty();
	}

	void VkApp::setLightIntensity(size_t index, float intensity)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightIntensity", index, lights_.size()));
		}
		lights_[index].intensity = intensity;
		markLightingDirty();
	}

	void VkApp::setLightRange(size_t index, float range)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightRange", index, lights_.size()));
		}
		lights_[index].range = range;
		markLightingDirty();
	}

	void VkApp::setLightEnabled(size_t index, bool enabled)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightEnabled", index, lights_.size()));
		}
		lights_[index].enabled = enabled;
		markLightingDirty();
	}

	void VkApp::setAmbientLight(const glm::vec3& ambientColor)
	{
		ambientLight_ = ambientColor;
		markLightingDirty();
	}

	void VkApp::setLightTransformMatrixOverride(size_t index, const glm::mat4& transform)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("setLightTransformMatrixOverride", index, lights_.size()));
		}

		if (lightTransformMatrixOverrides_.size() < lights_.size())
		{
			lightTransformMatrixOverrides_.resize(lights_.size());
		}
		lightTransformMatrixOverrides_[index] = transform;
		markLightingDirty();
	}

	void VkApp::clearLightTransformMatrixOverride(size_t index)
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("clearLightTransformMatrixOverride", index, lights_.size()));
		}

		if (index < lightTransformMatrixOverrides_.size())
		{
			lightTransformMatrixOverrides_[index].reset();
		}
		markLightingDirty();
	}

	void VkApp::markLightingDirty()
	{
		lightingDataDirty_ = true;
	}

	LightSource VkApp::lightForUpload(size_t index) const
	{
		if (index >= lights_.size())
		{
			throw std::out_of_range(makeLightIndexMessage("lightForUpload", index, lights_.size()));
		}

		LightSource light = lights_[index];
		if (index < lightTransformMatrixOverrides_.size() && lightTransformMatrixOverrides_[index])
		{
			const glm::mat4& transform = *lightTransformMatrixOverrides_[index];
			light.position = glm::vec3(transform * glm::vec4(light.position, 1.0f));
			glm::vec3 transformedDirection = glm::mat3(transform) * light.direction;
			if (glm::length(transformedDirection) > 0.0f)
			{
				light.direction = glm::normalize(transformedDirection);
			}
		}

		if (glm::length(light.direction) > 0.0f)
		{
			light.direction = glm::normalize(light.direction);
		}
		else
		{
			light.direction = glm::vec3(0.0f, -1.0f, 0.0f);
		}
		return light;
	}

	glm::vec3 VkApp::shadowSceneCenter() const
	{
		glm::vec3 sum(0.0f);
		size_t count = 0;
		for (size_t i = 0; i < _objects_.size(); ++i)
		{
			sum += glm::vec3(getObjectModelMatrix(i) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
			++count;
		}
		for (const auto& riggedInstance : riggedInstances_)
		{
			if (!riggedInstance.object)
			{
				continue;
			}
			if (riggedInstance.transformMatrixOverride)
			{
				sum += glm::vec3(*riggedInstance.transformMatrixOverride * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
			}
			else
			{
				sum += riggedInstance.object->getPosition();
			}
			++count;
		}

		return count > 0 ? sum / static_cast<float>(count) : glm::vec3(0.0f);
	}

	glm::mat4 VkApp::shadowMatrixForLight(size_t index) const
	{
		const LightSource light = lightForUpload(index);
		glm::vec3 direction = glm::length(light.direction) > 0.0f
			? glm::normalize(light.direction)
			: glm::vec3(0.0f, -1.0f, 0.0f);
		glm::vec3 up(0.0f, 1.0f, 0.0f);
		if (std::abs(glm::dot(direction, up)) > 0.95f)
		{
			up = glm::vec3(0.0f, 0.0f, 1.0f);
		}

		if (light.type == LightType::Directional)
		{
			const glm::vec3 center = shadowSceneCenter();
			const float nearPlane = std::max(0.001f, light.shadowNear);
			const float farPlane = std::max(nearPlane + 0.001f, light.shadowFar);
			const float halfSize = std::max(0.1f, light.shadowOrthoSize);
			const glm::mat4 view = glm::lookAt(center - direction * (farPlane * 0.5f),
			                                   center,
			                                   up);
			glm::mat4 proj = glm::ortho(-halfSize, halfSize, -halfSize, halfSize, nearPlane, farPlane);
			proj[1][1] *= -1.0f;
			return proj * view;
		}

		const float nearPlane = std::max(0.001f, light.shadowNear);
		const float farPlane = std::max(nearPlane + 0.001f, light.shadowFar);
		const float outer = glm::clamp(light.outerConeAngleRadians, glm::radians(1.0f), glm::radians(89.0f));
		const glm::mat4 view = glm::lookAt(light.position, light.position + direction, up);
		glm::mat4 proj = glm::perspective(outer * 2.0f, 1.0f, nearPlane, farPlane);
		proj[1][1] *= -1.0f;
		return proj * view;
	}

	LightingBufferObject VkApp::buildLightingBufferObject() const
	{
		LightingBufferObject lighting{};
		const size_t lightCount = std::min(lights_.size(), lightGraphics::MaxForwardLights);
		lighting.ambientAndCount = glm::vec4(ambientLight_, static_cast<float>(lightCount));

		for (size_t i = 0; i < lightCount; ++i)
		{
			const LightSource light = lightForUpload(i);
			GpuLight& gpuLight = lighting.lights[i];
			const float intensity = light.enabled ? light.intensity : 0.0f;
			const float range = std::max(light.range, 0.0f);
			const float inner = glm::clamp(light.innerConeAngleRadians, 0.0f, glm::pi<float>());
			const float outer = glm::clamp(light.outerConeAngleRadians, inner, glm::pi<float>());

			gpuLight.positionRange = glm::vec4(light.position, range);
			gpuLight.directionType = glm::vec4(light.direction, static_cast<float>(light.type));
			gpuLight.colorIntensity = glm::vec4(light.color, intensity);
			gpuLight.spotAngles = glm::vec4(std::cos(inner),
			                                std::cos(outer),
			                                shadowRenderingEnabled_ && light.castsShadow ? 1.0f : 0.0f,
			                                0.0f);
			const bool canCastShadow =
				shadowRenderingEnabled_ &&
				light.enabled &&
				light.castsShadow &&
				(light.type == LightType::Directional || light.type == LightType::Spot);
			gpuLight.shadowInfo = glm::vec4(canCastShadow ? static_cast<float>(i) : -1.0f,
			                                std::max(light.shadowBias, 0.0f),
			                                std::max(light.shadowNormalBias, 0.0f),
			                                glm::clamp(light.shadowStrength, 0.0f, 1.0f));
			if (canCastShadow)
			{
				lighting.shadowMatrices[i] = shadowMatrixForLight(i);
			}
		}

		return lighting;
	}
}
