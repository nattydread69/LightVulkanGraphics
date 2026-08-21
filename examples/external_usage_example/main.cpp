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

/// @file main.cpp
/// @brief Example of using LightVulkanGraphics as an installed external library
///
/// This example demonstrates how to use LightVulkanGraphics after installation.
/// Build with:
///   cmake -B build -DCMAKE_PREFIX_PATH=~/.local
///   cmake --build build
/// Run with:
///   ./build/my_graphics_app

#include "LightVulkanGraphicsPublic.h"
#include <iostream>

int main(int argc, char* argv[])
{
    try
    {
        // Print version information
        std::cout << "LightVulkanGraphics " << lightGraphics::kLightVulkanGraphicsVersion << std::endl;
        std::cout << "Version: " << lightGraphics::kLightVulkanGraphicsVersionMajor << "."
                  << lightGraphics::kLightVulkanGraphicsVersionMinor << "."
                  << lightGraphics::kLightVulkanGraphicsVersionPatch << std::endl;

        // Create the main graphics application
        std::cout << "\nInitializing graphics engine..." << std::endl;
        lightGraphics::lightVulkanGraphics app("External Usage Example");

        // Check if graphics initialization was successful
        if (app.getWindowPointer() == nullptr)
        {
            throw std::runtime_error("Failed to initialize graphics");
        }

        std::cout << "Graphics engine initialized successfully!" << std::endl;

        // Add a simple sphere to the scene
        app.addObject(
            lightGraphics::ShapeType::SPHERE,
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(1.0f),
            glm::vec4(0.2f, 0.6f, 1.0f, 1.0f),  // Blue color
            glm::quat(1, 0, 0, 0),
            "Demo Sphere",
            1.0f
        );

        // Set up camera
        app.setCameraLookAt(
            glm::vec3(3.0f, 2.0f, 5.0f),   // Camera position
            glm::vec3(0.0f, 0.0f, 0.0f),   // Look at point
            glm::vec3(0.0f, 1.0f, 0.0f)    // Up vector
        );

        // Finalize scene and start rendering loop
        app.finalizeScene();

        std::cout << "Starting render loop..." << std::endl;
        std::cout << "Close the window to exit." << std::endl;

        app.run();

        std::cout << "\nApplication terminated successfully." << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
