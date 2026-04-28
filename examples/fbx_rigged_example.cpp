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

/**
 * FBX Rigged Model Example
 *
 * This example demonstrates how to load and display rigged FBX models
 * with bone animations using the LightVulkanGraphics library.
 *
 * Features demonstrated:
 * - Loading FBX files with rigged models
 * - Playing animations
 * - Controlling animation playback
 * - Accessing bone transforms for rendering
 */

#include "lightVulkanGraphics.h"
#include "RiggedObject.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>

class FBXRiggedExample : public lightGraphics::lightVulkanGraphics
{
public:
    FBXRiggedExample() : lightVulkanGraphics("FBX Rigged Model Example")
    {
        // Set up camera
        setCameraLookAt(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        setCameraFov(45.0f);
        setKeyboardCameraEnabled(true);

        // Initialize timing
        lastTime = std::chrono::high_resolution_clock::now();
    }

    bool initialize()
    {
        const std::filesystem::path modelPath =
            std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "Worker.fbx";

        // Create a rigged object
        riggedHuman = std::make_shared<lightGraphics::RiggedObject>(
            glm::vec3(0.0f, 0.0f, 0.0f),  // Position
            glm::vec3(1.0f, 1.0f, 1.0f),  // Scale
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), // No rotation
            "Worker", // Name
            70.0f, // Mass (kg)
            modelPath.string() // FBX file path
        );

        // Check if model loaded successfully
        if (!riggedHuman->getModel())
        {
            lightGraphics::consoleErrorStream() << "Failed to load FBX model: "
                                                << riggedHuman->getLastError() << std::endl;
            lightGraphics::consoleErrorStream() << "Expected bundled asset: " << modelPath.string()
                                                << std::endl;
            return false;
        }

        lightGraphics::consoleInfoStream() << "Successfully loaded FBX model!" << std::endl;
        lightGraphics::consoleInfoStream() << "Number of animations: "
                                           << riggedHuman->getAnimationCount() << std::endl;

        // List available animations
        auto animationNames = riggedHuman->getAnimationNames();
        lightGraphics::consoleInfoStream() << "Available animations:" << std::endl;
        for (const auto& name : animationNames)
        {
            lightGraphics::consoleInfoStream() << "  - " << name << std::endl;
        }

        // Play the first animation if available
        if (!animationNames.empty())
        {
            lightGraphics::consoleInfoStream() << "Playing animation: " << animationNames[0]
                                               << std::endl;
            riggedHuman->playAnimation(animationNames[0], true); // Loop the animation
        }

        addRiggedObject(riggedHuman);
        setUpdateCallback([this](float deltaTime)
        {
            if (riggedHuman)
            {
                riggedHuman->updateAnimation(deltaTime);
            }
        });

        return true;

    }

    void update()
    {
        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // Update animation
        if (riggedHuman && riggedHuman->getModel())
        {
            riggedHuman->updateAnimation(deltaTime);

            // Print bone transforms for debugging (optional)
            static int frameCount = 0;
            if (frameCount % 60 == 0) // Print every 60 frames
            {
                const auto& boneTransforms = riggedHuman->getBoneTransforms();
                lightGraphics::consoleInfoStream() << "Frame " << frameCount << ": "
                                                   << boneTransforms.size() << " bones" << std::endl;

                // Example: Get specific bone transform
                glm::mat4 headTransform = riggedHuman->getBoneTransform("Head");
                lightGraphics::consoleInfoStream()
                    << "Head bone transform available: " << (headTransform != glm::mat4(1.0f))
                    << std::endl;
            }
            frameCount++;
        }
    }

    void handleInput()
    {
        // Note: Input handling would be implemented here if the base class supported it
        // This is a simplified example that focuses on the FBX loading functionality
    }

    void render()
    {
        // The base class handles the actual rendering
        // You would need to extend the rendering system to handle rigged models
        // This is a placeholder for where you would integrate the rigged model rendering
    }

    void printInstructions()
    {
        lightGraphics::consoleInfoStream() << "\n=== FBX Rigged Model Example ===" << std::endl;
        lightGraphics::consoleInfoStream()
            << "This example demonstrates FBX loading and rigged model handling." << std::endl;
        lightGraphics::consoleInfoStream()
            << "The model will be loaded and animation data will be processed." << std::endl;
        lightGraphics::consoleInfoStream() << "========================================="
                                           << std::endl;
    }

private:
    std::shared_ptr<lightGraphics::RiggedObject> riggedHuman;
    std::chrono::high_resolution_clock::time_point lastTime;
};

int main()
{
    try
    {
        FBXRiggedExample app;

        // Print instructions
        app.printInstructions();

        if (!app.initialize())
        {
            return -1;
        }

        app.finalizeScene();
        app.run();

        return 0;
    }
    catch (const std::exception& e)
    {
        lightGraphics::consoleErrorStream() << "Error: " << e.what() << std::endl;
        return -1;
    }
}
