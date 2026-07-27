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

#include "GraphicsModel.h"
#include "RiggedObject.h"
#include "lightVulkanGraphics.h"
#include "pObject.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

const glm::vec3 kDemoCameraPosition(4.0f, 2.0f, -8.0f);
const glm::vec3 kDemoCameraTarget(0.0f);

glm::quat buildWorkerSpawnRotation(const glm::vec3& workerPosition)
{
	const glm::quat uprightRotation =
	    glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	glm::vec3 toCamera = kDemoCameraPosition - workerPosition;
	toCamera.y = 0.0f;
	if (glm::dot(toCamera, toCamera) < 1.0e-6f)
	{
		return uprightRotation;
	}

	toCamera = glm::normalize(toCamera);

	// Worker.fbx still needs this object-local correction before yawing in demo space.
	const float yawToCamera = std::atan2(toCamera.x, toCamera.z);
	const glm::quat faceCameraRotation =
	    glm::angleAxis(yawToCamera, glm::vec3(0.0f, 1.0f, 0.0f));

	return faceCameraRotation * uprightRotation;
}

class PhysicsModel : public lightGraphics::GraphicsModel
{
public:
	explicit PhysicsModel(lightGraphics::lightVulkanGraphics& app,
	                      const std::string& name)
	    : lightGraphics::GraphicsModel(app, name)
	{
	}

	virtual ~PhysicsModel() = default;

	virtual void initialize() = 0;
	virtual void update(float deltaTime) = 0;
	virtual void cleanup() = 0;
};

class DemoModel : public PhysicsModel
{
public:
	explicit DemoModel(lightGraphics::lightVulkanGraphics& app)
	    : PhysicsModel(app, "Demo Model")
	{
	}

	void initialize() override
	{
		lightGraphics::consoleInfoStream() << "Initializing demo model..." << std::endl;
		createDemoObjects();
		lightGraphics::consoleInfoStream() << "Demo model initialized with "
		                                   << app_.getObjectCount() << " objects" << std::endl;
	}

	void update(float deltaTime) override
	{
		if (worker_)
		{
			worker_->updateAnimation(deltaTime);
		}
	}

	void cleanup() override
	{
		lightGraphics::consoleInfoStream() << "Cleaning up demo model..." << std::endl;
		if (fogVolume_.isValid())
		{
			app_.hideVolume(fogVolume_);
			app_.destroyVolume(fogVolume_);
			fogVolume_ = {};
		}
		if (fogTransferFunction_.isValid())
		{
			app_.destroyTransferFunction(fogTransferFunction_);
			fogTransferFunction_ = {};
		}
		if (fogTexture_.isValid())
		{
			app_.destroyTexture3D(fogTexture_);
			fogTexture_ = {};
		}
	}

private:
	void createDemoObjects()
	{
		app_.addObject(lightGraphics::ShapeType::SPHERE, glm::vec3(0.0f, 0.0f, 0.0f),
		              glm::vec3(1.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
		              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Red Sphere", 1.0f);

		app_.addObject(lightGraphics::ShapeType::CUBE, glm::vec3(3.0f, 0.0f, 0.0f),
		              glm::vec3(1.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
		              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Blue Cube", 1.0f);

		app_.addObject(lightGraphics::ShapeType::CONE, glm::vec3(-3.0f, 0.0f, 0.0f),
		              glm::vec3(1.0f), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),
		              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Green Cone", 1.0f);

		app_.addObject(lightGraphics::ShapeType::CYLINDER, glm::vec3(0.0f, 3.0f, 0.0f),
		              glm::vec3(1.0f), glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
		              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Yellow Cylinder", 1.0f);

		app_.addObject(lightGraphics::ShapeType::CAPSULE, glm::vec3(0.0f, -3.0f, 0.0f),
		              glm::vec3(1.0f), glm::vec4(1.0f, 0.0f, 1.0f, 1.0f),
		              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Magenta Capsule", 1.0f);

		app_.addObject(lightGraphics::ShapeType::ARROW, glm::vec3(0.0f, 0.0f, 3.0f),
		              glm::vec3(1.0f), glm::vec4(0.0f, 1.0f, 1.0f, 1.0f),
		              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Cyan Arrow", 1.0f);

		const glm::quat rotation =
		    glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		lightGraphics::pObject rotatedCube(
		    lightGraphics::ShapeType::CUBE, glm::vec3(0.0f, 0.0f, -3.0f), glm::vec3(1.0f),
		    glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), rotation, "Rotated Cube", 1.0f);
		app_.addObject(rotatedCube);

		lightGraphics::pObject scaledSphere(
		    lightGraphics::ShapeType::SPHERE, glm::vec3(3.0f, 3.0f, 0.0f), glm::vec3(0.5f),
		    glm::vec4(1.0f, 0.5f, 0.0f, 1.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		    "Scaled Sphere", 0.5f);
		app_.addObject(scaledSphere);

		app_.addObject(lightGraphics::ShapeType::CUBE, glm::vec3(-1.5f, -1.05f, -5.0f),
		              glm::vec3(5.0f, 0.06f, 4.5f), glm::vec4(0.34f, 0.38f, 0.36f, 1.0f),
		              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Worker Shadow Ground", 0.0f);

		createRiggedWorker();
		createVolumetricFog();
		configureDemoShadows();

		for (size_t i = 0; i < app_.getObjectCount(); ++i)
		{
			const auto& obj = app_.getObject(i);
			lightGraphics::consoleInfoStream() << "Object " << i << ": " << obj.getName()
			                                   << " at position (" << obj.getPosition().x << ", "
			                                   << obj.getPosition().y << ", "
			                                   << obj.getPosition().z << ")" << std::endl;
		}

		lightGraphics::consoleInfoStream() << "Controls:" << std::endl;
		lightGraphics::consoleInfoStream() << "  WASD/QE: Move camera" << std::endl;
		lightGraphics::consoleInfoStream() << "  Mouse: Look around" << std::endl;
		lightGraphics::consoleInfoStream() << "  ESC: Exit" << std::endl;
	}

	void createVolumetricFog()
	{
		constexpr std::uint32_t resolution = 48;
		std::vector<float> field(
			static_cast<std::size_t>(resolution) * resolution * resolution);
		for (std::uint32_t z = 0; z < resolution; ++z)
		{
			for (std::uint32_t y = 0; y < resolution; ++y)
			{
				for (std::uint32_t x = 0; x < resolution; ++x)
				{
					const glm::vec3 position = glm::vec3(x, y, z) /
						static_cast<float>(resolution - 1) * 2.0f - 1.0f;
					const glm::vec3 stretched{
						position.x * 0.85f,
						position.y * 1.25f,
						position.z};
					const float cloudDistance = glm::length(stretched);
					const float cloudT = std::clamp(
						(0.48f - cloudDistance) / 0.32f,
						0.0f,
						1.0f);
					const float cloudFade =
						cloudT * cloudT * (3.0f - 2.0f * cloudT);
					const float billow = 0.82f + 0.18f *
						std::sin(position.x * 8.0f) *
						std::sin(position.y * 7.0f) *
						std::sin(position.z * 9.0f);
					// Force the scalar field smoothly to zero before every proxy face.
					// This prevents trilinear filtering and accumulated low opacity from
					// revealing the otherwise axis-aligned volume boundary.
					const float edgeDistance = std::min({
						1.0f - std::abs(position.x),
						1.0f - std::abs(position.y),
						1.0f - std::abs(position.z)});
					const float edgeT = std::clamp(edgeDistance / 0.42f, 0.0f, 1.0f);
					const float edgeFade = edgeT * edgeT * (3.0f - 2.0f * edgeT);
					const std::size_t index = x + resolution * (y + resolution * z);
					field[index] = std::clamp(
						cloudFade * cloudFade * billow *
							edgeFade * edgeFade,
						0.0f,
						1.0f);
				}
			}
		}

		lightGraphics::Texture3DDescription textureDescription;
		textureDescription.width = resolution;
		textureDescription.height = resolution;
		textureDescription.depth = resolution;
		textureDescription.format = lightGraphics::TextureFormat::R32_SFLOAT;
		fogTexture_ = app_.createTexture3D(
			textureDescription,
			field.data(),
			field.size() * sizeof(float));

		fogTransferFunction_ = app_.createTransferFunction({
			{0.0f, glm::vec4(0.0f)},
			{0.25f, glm::vec4(0.18f, 0.32f, 0.42f, 0.0f)},
			{0.55f, glm::vec4(0.30f, 0.68f, 0.82f, 0.22f)},
			{1.0f, glm::vec4(0.72f, 0.92f, 1.0f, 0.68f)}});

		lightGraphics::VolumeRenderDescription volumeDescription;
		volumeDescription.volumeTexture = fogTexture_;
		volumeDescription.transferFunction = fogTransferFunction_;
		volumeDescription.volumeMin = {-0.35f, 0.65f, -1.35f};
		volumeDescription.volumeMax = {2.35f, 3.35f, 1.35f};
		volumeDescription.opacityModel =
			lightGraphics::VolumeOpacityModel::ExponentialExtinction;
		volumeDescription.opacityScale = 1.4f;
		volumeDescription.referenceStepLength = 1.0f;
		volumeDescription.normalizeOpacityByStepLength = true;
		volumeDescription.raymarchSteps = 160;
		volumeDescription.enableJitter = true;
		volumeDescription.enableEarlyTermination = true;
		fogVolume_ = app_.createVolume(volumeDescription);
		app_.drawVolume(fogVolume_,
			{lightGraphics::RenderLayer::Volume, 0.0f});

		lightGraphics::consoleInfoStream()
			<< "Added volumetric fog to the upper-middle demo region" << std::endl;
	}

	void createRiggedWorker()
	{
		const std::filesystem::path modelPath = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "Worker.fbx";
		if (!std::filesystem::exists(modelPath))
		{
			lightGraphics::consoleErrorStream() << "Worker rigged model not found: "
			                                    << modelPath.string() << std::endl;
			return;
		}

		const glm::vec3 workerPosition(-1.5f, 0.0f, -5.0f);
		const glm::quat workerSpawnRotation = buildWorkerSpawnRotation(workerPosition);

		worker_ = std::make_shared<lightGraphics::RiggedObject>(
			workerPosition, glm::vec3(1.0f),
			workerSpawnRotation, "Worker", 1.0f,
			modelPath.string());

		if (!worker_->getModel())
		{
			lightGraphics::consoleErrorStream() << "Failed to load Worker.fbx: "
			                                    << worker_->getLastError() << std::endl;
			worker_.reset();
			return;
		}

		const int preferredAnimationIndex = 23;

		if (worker_->getAnimationCount() > 0)
		{
			auto animationNames = worker_->getAnimationNames();
			lightGraphics::consoleInfoStream() << "Loaded Worker.fbx with "
			                                   << worker_->getAnimationCount()
			                                   << " animation(s):" << std::endl;
			for (size_t i = 0; i < animationNames.size(); ++i)
			{
				lightGraphics::consoleInfoStream() << "  " << i << ": " << animationNames[i]
				                                   << std::endl;
			}

			int initialAnimationIndex = preferredAnimationIndex;
			if (preferredAnimationIndex >= static_cast<int>(worker_->getAnimationCount()))
			{
				initialAnimationIndex = 0;
				lightGraphics::consoleInfoStream()
				    << "Preferred wave startup animation 23 was not found, falling back to animation 0."
				    << std::endl;
			}

			worker_->playAnimation(initialAnimationIndex, true);
			lightGraphics::consoleInfoStream() << "Playing startup animation "
			                                   << initialAnimationIndex << ": "
			                                   << animationNames[initialAnimationIndex] << std::endl;
		}
		else
		{
			lightGraphics::consoleInfoStream()
			    << "Loaded Worker.fbx, but no animations were found." << std::endl;
		}

		app_.addRiggedObject(worker_);
		const size_t workerLightIndex = app_.addSpotLight(workerPosition + glm::vec3(1.2f, 2.4f, -2.0f),
		                                                  glm::normalize(glm::vec3(-1.2f, -1.7f, 2.0f)),
		                                                  glm::vec3(1.0f, 0.86f, 0.68f),
		                                                  10.0f,
		                                                  7.0f,
		                                                  glm::radians(18.0f),
		                                                  glm::radians(35.0f),
		                                                  "Worker Warm Key Light");
		lightGraphics::LightSource workerLight = app_.getLight(workerLightIndex);
		workerLight.castsShadow = true;
		workerLight.shadowStrength = 0.45f;
		workerLight.shadowBias = 0.0025f;
		workerLight.shadowNormalBias = 0.015f;
		workerLight.shadowFar = 12.0f;
		app_.updateLight(workerLightIndex, workerLight);
	}

	void configureDemoShadows()
	{
		if (app_.getLightCount() == 0)
		{
			return;
		}

		lightGraphics::LightSource sun = app_.getLight(0);
		sun.castsShadow = true;
		sun.shadowStrength = 0.75f;
		sun.shadowBias = 0.003f;
		sun.shadowNormalBias = 0.02f;
		sun.shadowOrthoSize = 8.0f;
		sun.shadowFar = 35.0f;
		app_.updateLight(0, sun);
	}

	std::shared_ptr<lightGraphics::RiggedObject> worker_;
	lightGraphics::Texture3DHandle fogTexture_;
	lightGraphics::TransferFunctionHandle fogTransferFunction_;
	lightGraphics::VolumeHandle fogVolume_;
};

class DemoApp
{
public:
	DemoApp(int argc, char* argv[])
	    : app_("demoVulkanGraphics")
	{
		if (argc > 1)
		{
			const std::string modelArg = argv[1];
			if (modelArg == "demo" || modelArg == "Demo" || modelArg == "DEMO")
			{
				currentModel_ = ModelType::Demo;
			}
			else
			{
				lightGraphics::consoleInfoStream()
				    << "Unknown scene argument: " << modelArg
				    << ". Available scene: demo" << std::endl;
			}
		}

		loadCurrentModel();

		app_.setCameraLookAt(kDemoCameraPosition, kDemoCameraTarget,
		                     glm::vec3(0.0f, 1.0f, 0.0f));
		lightGraphics::ScreenTextDescription welcomeText;
		welcomeText.text = "Welcome to the lightVulkanGraphics engine!";
		welcomeText.positionPixels = {24.0f, 24.0f};
		welcomeText.scale = 2.0f;
		welcomeText.color = {1.0f, 0.95f, 0.65f, 1.0f};
		welcomeText.maximumCharacters = 64;
		welcomeText_ = app_.createScreenText(welcomeText);
		app_.finalizeScene();

		window_ = app_.getWindowPointer();
		showModelMenu();

		app_.setUpdateCallback([this](float /*deltaTime*/) {
			primeInstanceBuffersIfNeeded();
			handleKeyboardInput();

			static auto lastTime = std::chrono::high_resolution_clock::now();
			const auto currentTime = std::chrono::high_resolution_clock::now();
			const float realDeltaTime =
			    std::chrono::duration<float>(currentTime - lastTime).count();
			lastTime = currentTime;

			if (currentPhysicsModel_)
			{
				currentPhysicsModel_->update(realDeltaTime);
			}
		});

		app_.run();
	}

	~DemoApp()
	{
		if (currentPhysicsModel_)
		{
			currentPhysicsModel_->cleanup();
		}
		if (welcomeText_.isValid())
		{
			app_.destroyScreenText(welcomeText_);
			welcomeText_ = {};
		}
	}

private:
	enum class ModelType
	{
		Demo
	};

	void handleKeyboardInput()
	{
		if (!window_)
		{
			return;
		}

		for (int key = 0; key <= GLFW_KEY_LAST; ++key)
		{
			const bool isPressed = glfwGetKey(window_, key) == GLFW_PRESS;
			keysJustPressed_[key] = isPressed && !keysPressed_[key];
			keysJustReleased_[key] = !isPressed && keysPressed_[key];
			keysPressed_[key] = isPressed;
		}

		if (keysJustPressed_[GLFW_KEY_F1])
		{
			switchModel(ModelType::Demo);
		}
		if (keysJustPressed_[GLFW_KEY_F5])
		{
			showModelMenu();
		}
		if (keysJustPressed_[GLFW_KEY_ESCAPE])
		{
			glfwSetWindowShouldClose(window_, GLFW_TRUE);
		}
	}

	void loadCurrentModel()
	{
		switch (currentModel_)
		{
		case ModelType::Demo:
			lightGraphics::consoleInfoStream() << "Loading demo scene..." << std::endl;
			currentPhysicsModel_ = std::make_unique<DemoModel>(app_);
			break;
		}

		if (currentPhysicsModel_)
		{
			currentPhysicsModel_->initialize();
		}
		pendingInstanceBufferPrimingFrames_ = kInstancePrimingFrames;
	}

	void switchModel(ModelType model)
	{
		if (model == currentModel_)
		{
			return;
		}

		if (currentPhysicsModel_)
		{
			currentPhysicsModel_->cleanup();
			currentPhysicsModel_.reset();
		}

		app_.clearObjects();
		currentModel_ = model;
		loadCurrentModel();

		lightGraphics::consoleInfoStream() << "Model switched to: "
		                                   << (currentPhysicsModel_ ? currentPhysicsModel_->getName() : "None")
		                                   << std::endl;
	}

	void showModelMenu() const
	{
		lightGraphics::consoleInfoStream() << "\n============================" << std::endl;
		lightGraphics::consoleInfoStream() << "demoVulkanGraphics" << std::endl;
		lightGraphics::consoleInfoStream() << "============================" << std::endl;
		lightGraphics::consoleInfoStream() << "Current Model: "
		                                   << (currentPhysicsModel_ ? currentPhysicsModel_->getName() : "None")
		                                   << std::endl;
		lightGraphics::consoleInfoStream() << "\nKeyboard Controls:" << std::endl;
		lightGraphics::consoleInfoStream() << "F1 - Demo scene" << std::endl;
		lightGraphics::consoleInfoStream() << "F5 - Show this menu" << std::endl;
		lightGraphics::consoleInfoStream() << "N/P - Cycle Worker animation (if loaded)" << std::endl;
		lightGraphics::consoleInfoStream() << "ESC - Exit application" << std::endl;
		lightGraphics::consoleInfoStream() << "============================\n" << std::endl;
	}

	void primeInstanceBuffersIfNeeded()
	{
		if (pendingInstanceBufferPrimingFrames_ <= 0)
		{
			return;
		}

		const size_t objectCount = app_.getObjectCount();
		if (objectCount == 0)
		{
			return;
		}

		for (size_t i = 0; i < objectCount; ++i)
		{
			app_.setObjectPosition(i, app_.getObject(i).getPosition());
		}

		--pendingInstanceBufferPrimingFrames_;
	}

	static constexpr int kInstancePrimingFrames = 2;
	lightGraphics::lightVulkanGraphics app_;
	lightGraphics::ScreenTextHandle welcomeText_;
	GLFWwindow* window_ = nullptr;
	std::unique_ptr<PhysicsModel> currentPhysicsModel_;
	ModelType currentModel_ = ModelType::Demo;
	int pendingInstanceBufferPrimingFrames_ = 0;
	bool keysPressed_[GLFW_KEY_LAST + 1] = {false};
	bool keysJustPressed_[GLFW_KEY_LAST + 1] = {false};
	bool keysJustReleased_[GLFW_KEY_LAST + 1] = {false};
};

} // namespace

int main(int argc, char* argv[])
{
	try
	{
		DemoApp app(argc, argv);
	}
	catch (const std::exception& e)
	{
		lightGraphics::consoleErrorStream() << "Fatal: " << e.what() << std::endl;
		return 2;
	}

	return 0;
}
