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
        // True if some mesh actually skins vertices to this bone (i.e. `offsetMatrix`
        // and `skinningGlobalBindTransform` were populated from real skin-cluster
        // data, not just copied from the node-hierarchy walk as a fallback). Used to
        // decide, per bone, whether the authoritative skin-cluster bind pose can be
        // used for skinning instead of the node-hierarchy-derived one.
        bool hasSkinBindTransform = false;
        // For a mesh-local Bone entry (RiggedMesh::bones), the resolved index into
        // RiggedModel::bones for the global bone of the same name, cached once at
        // load time so per-frame skinning (buildRiggedFinalBoneMatrix) doesn't have
        // to repeat a string-keyed boneMapping lookup for every mesh-bone, every
        // mesh, every instance, every frame. -1 if no matching global bone exists.
        // Meaningless on a RiggedModel::bones entry itself.
        int cachedGlobalBoneIndex = -1;
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
        // Bone name -> index into channels, built once when this animation is
        // loaded (see FBXLoader::processAnimations). Lets per-frame bone-transform
        // evaluation (RiggedObject::calculateBoneTransforms) do a single map
        // lookup per bone instead of a linear scan over every channel.
        std::map<std::string, int> channelIndexByBoneName;
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
        // Diagnostic only: true if the node-hierarchy-walked bind pose disagreed
        // meaningfully with the skin cluster's own bind pose for several bones (see
        // FBXLoader::loadModel). Skinning itself no longer branches on this — the
        // skin-cluster-derived bind pose (Bone::hasSkinBindTransform) is always
        // preferred per-bone, since it is the FBX file's authoritative bind data.
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
        // The file currently being loaded, set at the top of loadModel() --
        // folded into embedded-texture cache keys (see processMesh()) since
        // VkApp's texture cache is one process-wide map keyed by string, but
        // Assimp's embedded-texture references ("*0", "*1", ...) are only
        // unique *within* one source file. Without the source path in the
        // key, loading a second glb whose material also references "*0"
        // hits the first file's cache entry and silently reuses its
        // texture (observed as a character model's texture appearing on an
        // unrelated static prop loaded afterward).
        std::string currentModelPath;

        // Assimp scene processing
        void processNode(aiNode* node, const aiScene* scene, RiggedModel& model,
                          const glm::mat4& parentTransform = glm::mat4(1.0f));
        RiggedMesh processMesh(aiMesh* mesh, const aiScene* scene,
                                const glm::mat4& nodeTransform);
        void processBones(aiMesh* mesh, RiggedMesh& riggedMesh);
        void processAnimations(const aiScene* scene, RiggedModel& model);
        void processAnimationChannel(aiNodeAnim* channel, AnimationChannel& animChannel);

        // Bone hierarchy processing
        void buildBoneHierarchy(aiNode* node, int parentIndex, RiggedModel& model);
        // Not called by this library's own runtime path today — RiggedObject computes
        // per-frame bone transforms itself (RiggedObject::calculateBoneTransforms()).
        // Kept correct and available as a private utility for future use rather than
        // removed, since it mirrors that same, already-tested composition logic.
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
