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

#ifndef LIGHT_VULKAN_GRAPHICS_FBX_LOADER_H
#define LIGHT_VULKAN_GRAPHICS_FBX_LOADER_H

#include "pHeaders.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <filesystem>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>

struct aiScene;
struct aiMesh;
struct aiNode;
struct aiNodeAnim;
struct aiTexture;

namespace lightGraphics
{
    // Structure to hold bone information
    struct Bone
    {
        std::string name;
        glm::mat4 offsetMatrix;  // Transform from mesh space to bone space
        glm::mat4 finalTransform; // Final transformation matrix
        glm::mat4 localTransform; // Bind-pose transform relative to parent
        glm::mat4 globalBindTransform = glm::mat4(1.0f);
        glm::mat4 skinningGlobalBindTransform = glm::mat4(1.0f);
        glm::mat4 skinningLocalBindTransform = glm::mat4(1.0f);
        glm::vec3 bindPosition = glm::vec3(0.0f);
        glm::quat bindRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 bindScale    = glm::vec3(1.0f);
        int parentIndex;         // Index of parent bone (-1 for root)
        std::vector<int> children; // Indices of child bones
    };

    // Structure to hold animation keyframe data
    struct AnimationKeyframe
    {
        float time;
        glm::vec3 position;
        glm::quat rotation;
        glm::vec3 scale;
    };

    // Structure to hold animation channel data
    struct AnimationChannel
    {
        std::string boneName;
        std::vector<AnimationKeyframe> positionKeys;
        std::vector<AnimationKeyframe> rotationKeys;
        std::vector<AnimationKeyframe> scaleKeys;
    };

    // Structure to hold animation data
    struct Animation
    {
        std::string name;
        float duration;  // Duration in seconds
        float ticksPerSecond;
        std::vector<AnimationChannel> channels;
    };

    // Structure to hold vertex data with bone weights
    struct RiggedVertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoords;
        glm::vec4 boneWeights;    // Weights for up to 4 bones
        glm::ivec4 boneIndices;   // Indices of the bones
    };

    struct EmbeddedTextureData
    {
        std::vector<uint8_t> data;
        uint32_t width = 0;
        uint32_t height = 0;
        bool isRawPixels = false;
        std::string formatHint;
    };

    // Structure to hold mesh data
    struct RiggedMesh
    {
        std::vector<RiggedVertex> vertices;
        std::vector<unsigned int> indices;
        std::string materialName;
        std::string nodeName;
        glm::vec4 diffuseColor = glm::vec4(1.0f);
        std::string diffuseTexturePath;
        std::shared_ptr<EmbeddedTextureData> embeddedTexture;
        std::string embeddedTextureKey;
        glm::mat4 globalBindTransform = glm::mat4(1.0f);
        std::vector<Bone> bones;
        std::map<std::string, int> boneMapping; // Maps bone names to indices
    };

    // Structure to hold complete model data
    struct RiggedModel
    {
        std::vector<RiggedMesh> meshes;
        std::vector<Animation> animations;
        std::vector<Bone> bones; // Global bone hierarchy
        std::map<std::string, int> boneMapping; // Global bone name to index mapping
        glm::mat4 axisCorrection = glm::mat4(1.0f);
        glm::mat4 globalInverseTransform;
        bool usesSkinningBindCorrection = false;
    };

    /**
     * FBXLoader class for importing FBX files with rigged models
     * Supports bones, animations, and complex mesh data
     */
    class FBXLoader
    {
    public:
        FBXLoader();
        ~FBXLoader();

        /**
         * Load an FBX file and return a RiggedModel
         * @param filePath Path to the FBX file
         * @return Shared pointer to the loaded model, or nullptr on failure
         */
        std::shared_ptr<RiggedModel> loadModel(const std::string& filePath);

        /**
         * Get the last error message
         * @return Error message string
         */
        std::string getLastError() const { return lastError; }

        /**
         * Check if a file is a valid FBX file
         * @param filePath Path to the file
         * @return True if valid FBX file
         */
        bool isValidFBXFile(const std::string& filePath);

    private:
        std::string lastError;

        // Assimp scene processing
        void processNode(aiNode* node, const aiScene* scene, RiggedModel& model);
        RiggedMesh processMesh(aiMesh* mesh, const aiScene* scene);
        void processBones(aiMesh* mesh, RiggedMesh& riggedMesh);
        void processAnimations(const aiScene* scene, RiggedModel& model);
        void processAnimationChannel(aiNodeAnim* channel, AnimationChannel& animChannel);

        // Bone hierarchy processing
        void buildBoneHierarchy(aiNode* node, int parentIndex, RiggedModel& model);
        void calculateBoneTransforms(RiggedModel& model, float animationTime, int animationIndex);
        void calculateBoneTransform(const std::string& boneName, float animationTime,
                                  const Animation& animation, const std::vector<Bone>& bones,
                                  const std::map<std::string, int>& boneMapping, glm::mat4& transform);

        // Animation interpolation
        glm::vec3 interpolatePosition(float animationTime, const std::vector<AnimationKeyframe>& keys);
        glm::quat interpolateRotation(float animationTime, const std::vector<AnimationKeyframe>& keys);
        glm::vec3 interpolateScale(float animationTime, const std::vector<AnimationKeyframe>& keys);

        // Helper to find keyframes
        int findPositionKeyframe(float animationTime, const std::vector<AnimationKeyframe>& keys);
        int findRotationKeyframe(float animationTime, const std::vector<AnimationKeyframe>& keys);
        int findScaleKeyframe(float animationTime, const std::vector<AnimationKeyframe>& keys);
        std::shared_ptr<EmbeddedTextureData> fetchEmbeddedTexture(const aiScene* scene, const std::string& texturePath);
        std::shared_ptr<EmbeddedTextureData> fetchEmbeddedTextureByShortName(const aiScene* scene, const std::string& texturePath);
        std::shared_ptr<EmbeddedTextureData> decodeEmbeddedTexture(const aiTexture* texture);

        std::filesystem::path currentModelDirectory_;
        std::unordered_map<std::string, std::shared_ptr<EmbeddedTextureData>> embeddedTextures_;
        std::string resolveTexturePath(const std::string& texturePath) const;
    };

} // namespace lightGraphics

#endif // LIGHT_VULKAN_GRAPHICS_FBX_LOADER_H
