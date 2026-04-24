// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2025 Dr. Nathanael John Inkson
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

#include <chrono>
#include <cmath>
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

		createRiggedWorker();

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

		app_.addRiggedObject(worker_);

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

			int initialAnimationIndex = -1;
			const std::vector<std::string> preferredStartupAnimations = {
			    "CharacterArmature|Idle_Neutral",
			    "CharacterArmature|Idle",
			    "Idle",
			    "Idle_Neutral"};

			for (const auto& preferredName : preferredStartupAnimations)
			{
				for (size_t i = 0; i < animationNames.size(); ++i)
				{
					if (animationNames[i] == preferredName)
					{
						initialAnimationIndex = static_cast<int>(i);
						break;
					}
				}
				if (initialAnimationIndex >= 0)
				{
					break;
				}
			}

			if (initialAnimationIndex < 0)
			{
				initialAnimationIndex = 0;
				lightGraphics::consoleInfoStream()
				    << "No preferred idle startup animation found, falling back to animation 0."
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
	}

	std::shared_ptr<lightGraphics::RiggedObject> worker_;
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
				lightGraphics::consoleInfoStream() << "Unknown model: " << modelArg << std::endl;
			}
		}

		loadCurrentModel();

		app_.setCameraLookAt(kDemoCameraPosition, kDemoCameraTarget,
		                     glm::vec3(0.0f, 1.0f, 0.0f));
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
	}

private:
	enum class ModelType
	{
		Demo,
		Custom
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
		if (keysJustPressed_[GLFW_KEY_F2])
		{
			switchModel(ModelType::Custom);
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
		case ModelType::Custom:
			lightGraphics::consoleInfoStream() << "Custom model is not implemented yet." << std::endl;
			currentPhysicsModel_.reset();
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
		lightGraphics::consoleInfoStream() << "F2 - Custom model (not implemented)" << std::endl;
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
