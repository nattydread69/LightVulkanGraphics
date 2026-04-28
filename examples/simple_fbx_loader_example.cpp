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
 * Simple FBX Loader Example
 *
 * This example demonstrates the basic usage of the FBXLoader class
 * to load and inspect FBX files with rigged models.
 */

#include "FBXLoader.h"
#include "LightVulkanGraphicsLogging.h"
#include <algorithm>
#include <iomanip>
#include <iostream>

void printModelInfo(const lightGraphics::RiggedModel& model)
{
    lightGraphics::consoleInfoStream() << "\n=== Model Information ===" << std::endl;
    lightGraphics::consoleInfoStream() << "Number of meshes: " << model.meshes.size() << std::endl;
    lightGraphics::consoleInfoStream() << "Number of animations: " << model.animations.size() << std::endl;
    lightGraphics::consoleInfoStream() << "Number of bones: " << model.bones.size() << std::endl;

    // Print mesh information
    for (size_t i = 0; i < model.meshes.size(); i++)
    {
        const auto& mesh = model.meshes[i];
        lightGraphics::consoleInfoStream() << "\nMesh " << i << ":" << std::endl;
        lightGraphics::consoleInfoStream() << "  Vertices: " << mesh.vertices.size() << std::endl;
        lightGraphics::consoleInfoStream() << "  Indices: " << mesh.indices.size() << std::endl;
        lightGraphics::consoleInfoStream() << "  Bones: " << mesh.bones.size() << std::endl;
        lightGraphics::consoleInfoStream() << "  Material: " << mesh.materialName << std::endl;

        // Print bone names for this mesh
        if (!mesh.bones.empty())
        {
            lightGraphics::consoleInfoStream() << "  Bone names: ";
            for (const auto& bone : mesh.bones)
            {
                lightGraphics::consoleInfoStream() << bone.name << " ";
            }
            lightGraphics::consoleInfoStream() << std::endl;
        }
    }

    // Print animation information
    for (size_t i = 0; i < model.animations.size(); i++)
    {
        const auto& animation = model.animations[i];
        lightGraphics::consoleInfoStream() << "\nAnimation " << i << ": " << animation.name << std::endl;
        lightGraphics::consoleInfoStream() << "  Duration: " << std::fixed << std::setprecision(2)
                                           << animation.duration << " seconds" << std::endl;
        lightGraphics::consoleInfoStream() << "  Ticks per second: " << animation.ticksPerSecond << std::endl;
        lightGraphics::consoleInfoStream() << "  Channels: " << animation.channels.size() << std::endl;

        // Print channel information
        for (const auto& channel : animation.channels)
        {
            lightGraphics::consoleInfoStream() << "    Channel: " << channel.boneName << std::endl;
            lightGraphics::consoleInfoStream() << "      Position keys: " << channel.positionKeys.size() << std::endl;
            lightGraphics::consoleInfoStream() << "      Rotation keys: " << channel.rotationKeys.size() << std::endl;
            lightGraphics::consoleInfoStream() << "      Scale keys: " << channel.scaleKeys.size() << std::endl;
        }
    }

    // Print global bone hierarchy
    if (!model.bones.empty())
    {
        lightGraphics::consoleInfoStream() << "\nGlobal Bone Hierarchy:" << std::endl;
        for (size_t i = 0; i < model.bones.size(); i++)
        {
            const auto& bone = model.bones[i];
            lightGraphics::consoleInfoStream() << "  " << i << ": " << bone.name;
            if (bone.parentIndex >= 0)
            {
                lightGraphics::consoleInfoStream() << " (parent: " << model.bones[bone.parentIndex].name << ")";
            }
            else
            {
                lightGraphics::consoleInfoStream() << " (root)";
            }
            lightGraphics::consoleInfoStream() << std::endl;
        }
    }
}

void printVertexInfo(const lightGraphics::RiggedVertex& vertex, int index)
{
    lightGraphics::consoleInfoStream() << "  Vertex " << index << ":" << std::endl;
    lightGraphics::consoleInfoStream() << "    Position: (" << vertex.position.x << ", " << vertex.position.y << ", " << vertex.position.z << ")" << std::endl;
    lightGraphics::consoleInfoStream() << "    Normal: (" << vertex.normal.x << ", " << vertex.normal.y << ", " << vertex.normal.z << ")" << std::endl;
    lightGraphics::consoleInfoStream() << "    TexCoords: (" << vertex.texCoords.x << ", " << vertex.texCoords.y << ")" << std::endl;
    lightGraphics::consoleInfoStream() << "    Bone weights: (" << vertex.boneWeights.x << ", " << vertex.boneWeights.y << ", "
                                       << vertex.boneWeights.z << ", " << vertex.boneWeights.w << ")" << std::endl;
    lightGraphics::consoleInfoStream() << "    Bone indices: (" << vertex.boneIndices.x << ", " << vertex.boneIndices.y << ", "
                                       << vertex.boneIndices.z << ", " << vertex.boneIndices.w << ")" << std::endl;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        lightGraphics::consoleInfoStream() << "Usage: " << argv[0] << " <path_to_fbx_file>" << std::endl;
        lightGraphics::consoleInfoStream() << "Example: " << argv[0] << " assets/Worker.fbx" << std::endl;
        return 1;
    }

    std::string filePath = argv[1];

    lightGraphics::consoleInfoStream() << "Loading FBX file: " << filePath << std::endl;

    // Create FBX loader
    lightGraphics::FBXLoader loader;

    // Check if file is valid
    if (!loader.isValidFBXFile(filePath))
    {
        lightGraphics::consoleErrorStream() << "Error: Invalid FBX file or file not found: " << filePath << std::endl;
        return 1;
    }

    // Load the model
    auto model = loader.loadModel(filePath);
    if (!model)
    {
        lightGraphics::consoleErrorStream() << "Error: Failed to load model: " << loader.getLastError() << std::endl;
        return 1;
    }

    lightGraphics::consoleInfoStream() << "Successfully loaded FBX model!" << std::endl;

    // Print model information
    printModelInfo(*model);

    // Print detailed vertex information for the first few vertices of the first mesh
    if (!model->meshes.empty() && !model->meshes[0].vertices.empty())
    {
        lightGraphics::consoleInfoStream() << "\n=== Sample Vertices (first 3) ===" << std::endl;
        int numVertices = std::min(3, static_cast<int>(model->meshes[0].vertices.size()));
        for (int i = 0; i < numVertices; i++)
        {
            printVertexInfo(model->meshes[0].vertices[i], i);
        }
    }

    // Demonstrate animation keyframe data
    if (!model->animations.empty())
    {
        const auto& animation = model->animations[0];
        lightGraphics::consoleInfoStream() << "\n=== Sample Animation Data ===" << std::endl;
        lightGraphics::consoleInfoStream() << "Animation: " << animation.name << std::endl;

        if (!animation.channels.empty())
        {
            const auto& channel = animation.channels[0];
            lightGraphics::consoleInfoStream() << "First channel: " << channel.boneName << std::endl;

            if (!channel.positionKeys.empty())
            {
                lightGraphics::consoleInfoStream() << "First position keyframe:" << std::endl;
                const auto& key = channel.positionKeys[0];
                lightGraphics::consoleInfoStream() << "  Time: " << key.time << std::endl;
                lightGraphics::consoleInfoStream() << "  Position: (" << key.position.x << ", " << key.position.y << ", " << key.position.z << ")" << std::endl;
            }

            if (!channel.rotationKeys.empty())
            {
                lightGraphics::consoleInfoStream() << "First rotation keyframe:" << std::endl;
                const auto& key = channel.rotationKeys[0];
                lightGraphics::consoleInfoStream() << "  Time: " << key.time << std::endl;
                lightGraphics::consoleInfoStream() << "  Rotation: (" << key.rotation.x << ", " << key.rotation.y << ", "
                                                   << key.rotation.z << ", " << key.rotation.w << ")" << std::endl;
            }
        }
    }

    lightGraphics::consoleInfoStream() << "\nFBX file analysis complete!" << std::endl;

    return 0;
}
