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

#include "LightVulkanGraphicsLogging.h"
#include "LightVulkanGraphicsVersion.h"
#include "SceneGraph.h"
#include "VkApp.h"
#include <string>
#include <vector>
#include <tuple>
#include <utility>

namespace lightGraphics
{
	class RiggedObject;

	struct LightVulkanGraphicsCreateInfo
	{
		int width = 2000;
		int height = 1000;
		std::string shaderPath;
		bool manageGlfwLifecycle = true;
		// Process-wide switch shared across all LightVulkanGraphics instances.
		bool enableConsoleOutput = true;
		bool enableDebugOutput = false;
	};

	    class lightVulkanGraphics : public VkApp
		{
	public:
        explicit lightVulkanGraphics(std::string const title);
        lightVulkanGraphics(std::string const title, const LightVulkanGraphicsCreateInfo& createInfo);
		~lightVulkanGraphics() = default;

        // Convenience
        void setLineRenderMode();
        size_t addRiggedObject(const std::shared_ptr<RiggedObject>& riggedObject)
        {
            return VkApp::addRiggedObject(riggedObject);
        }
        SceneGraph& sceneGraph() { return VkApp::sceneGraph(); }
        const SceneGraph& sceneGraph() const { return VkApp::sceneGraph(); }

		// Camera control API (exposed for applications)
		void setKeyboardCameraEnabled(bool enabled) { VkApp::setKeyboardCameraEnabled(enabled); }
		bool getKeyboardCameraEnabled() const { return VkApp::getKeyboardCameraEnabled(); }
		// Process-wide switch shared across all LightVulkanGraphics instances.
		void setConsoleOutputEnabled(bool enabled) { lightGraphics::setConsoleOutputEnabled(enabled); }
		bool getConsoleOutputEnabled() const { return lightGraphics::getConsoleOutputEnabled(); }
		void setCameraPosition(const glm::vec3& pos) { VkApp::setCameraPosition(pos); }
		glm::vec3 getCameraPosition() const { return VkApp::getCameraPosition(); }
		void moveCameraForward(float distance) { VkApp::moveCameraForward(distance); }
		void moveCameraRight(float distance) { VkApp::moveCameraRight(distance); }
		void moveCameraUp(float distance) { VkApp::moveCameraUp(distance); }
		void setCameraYawPitch(float yawDeg, float pitchDeg) { VkApp::setCameraYawPitch(yawDeg, pitchDeg); }
		void addCameraYawPitch(float yawDeltaDeg, float pitchDeltaDeg) { VkApp::addCameraYawPitch(yawDeltaDeg, pitchDeltaDeg); }
		void setCameraLookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up = glm::vec3(0,1,0)) { VkApp::setCameraLookAt(eye, target, up); }
		void setCameraLookAtLevel(const glm::vec3& eye, const glm::vec3& target,
		                          const glm::vec3& up = glm::vec3(0,1,0))
		{
			VkApp::setCameraLookAtLevel(eye, target, up);
		}
		void setCameraFov(float fovDeg) { VkApp::setCameraFov(fovDeg); }
		float getCameraFov() const { return VkApp::getCameraFov(); }
		void setCameraPlanes(float zNear, float zFar) { VkApp::setCameraPlanes(zNear, zFar); }
		void setCameraSensitivity(float sens) { VkApp::setCameraSensitivity(sens); }
		glm::vec3 getCameraForward() const { return VkApp::getCameraForward(); }
		glm::vec3 getCameraRight() const { return VkApp::getCameraRight(); }
		glm::vec3 getCameraUp() const { return VkApp::getCameraUp(); }

		// Orbit camera API passthrough
		void setOrbitEnabled(bool enabled) { VkApp::setOrbitEnabled(enabled); }
		bool getOrbitEnabled() const { return VkApp::getOrbitEnabled(); }
		void setOrbitTarget(const glm::vec3& target) { VkApp::setOrbitTarget(target); }
		glm::vec3 getOrbitTarget() const { return VkApp::getOrbitTarget(); }
		void setOrbitRadius(float radius) { VkApp::setOrbitRadius(radius); }
		float getOrbitRadius() const { return VkApp::getOrbitRadius(); }
		void setOrbitAngles(float azimuthDeg, float elevationDeg) { VkApp::setOrbitAngles(azimuthDeg, elevationDeg); }
		void addOrbitAngles(float deltaAzimuthDeg, float deltaElevationDeg) { VkApp::addOrbitAngles(deltaAzimuthDeg, deltaElevationDeg); }
		void panOrbitTarget(float deltaRight, float deltaUp) { VkApp::panOrbitTarget(deltaRight, deltaUp); }
		void dollyOrbitRadius(float deltaRadius) { VkApp::dollyOrbitRadius(deltaRadius); }
		void setOrbitSensitivities(float rotate, float pan, float dolly) { VkApp::setOrbitSensitivities(rotate, pan, dolly); }

        // Cylinder helpers (retain for discoverability, call base)
        void addCylinderBetweenPoints(const glm::vec3& pointA, const glm::vec3& pointB,
                                    float radius, const glm::vec4& color,
                                    const std::string& name = "Cylinder", float mass = 1.0f);
        void addCylinderAlongAxis(const glm::vec3& center, const glm::vec3& axis,
                                float length, float radius, const glm::vec4& color,
                                const std::string& name = "Cylinder", float mass = 1.0f);
        void addCylinderConnectingSpheres(const glm::vec3& sphereA, const glm::vec3& sphereB,
                                        float sphereRadiusA, float sphereRadiusB,
                                        const glm::vec4& color, const std::string& name = "Connector",
                                        float mass = 1.0f);

        // Hexahedral helper
        void addHexahedral(const glm::vec3& position, const glm::vec3& size,
                           const glm::vec4& color,
                           const glm::quat& rotation = glm::quat(1,0,0,0),
                           const std::string& name = "Hexahedral",
                           float mass = 1.0f)
        {
            VkApp::addHexahedral(position, size, color, rotation, name, mass);
        }

    private:
    };

	using LightVulkanGraphics = lightVulkanGraphics;

}
