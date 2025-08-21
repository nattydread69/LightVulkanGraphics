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

#include "FBXLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/metadata.h>
#include <assimp/material.h>
#include <glm/gtx/quaternion.hpp>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <cstring>

namespace lightGraphics
{

    namespace
    {
        glm::vec3 axisToVector(int axis)
        {
            switch (axis)
            {
                case 0: return glm::vec3(1.0f, 0.0f, 0.0f);
                case 1: return glm::vec3(0.0f, 1.0f, 0.0f);
                default: return glm::vec3(0.0f, 0.0f, 1.0f);
            }
        }

        glm::mat4 buildAxisCorrection(const aiScene* scene)
        {
            if (!scene || !scene->mMetaData)
            {
                return glm::mat4(1.0f);
            }

            int upAxis = 1;
            int upSign = 1;
            int frontAxis = 2;
            int frontSign = 1;
            int coordAxis = 0;
            int coordSign = 1;

            scene->mMetaData->Get("UpAxis", upAxis);
            scene->mMetaData->Get("UpAxisSign", upSign);
            scene->mMetaData->Get("FrontAxis", frontAxis);
            scene->mMetaData->Get("FrontAxisSign", frontSign);
            scene->mMetaData->Get("CoordAxis", coordAxis);
            scene->mMetaData->Get("CoordAxisSign", coordSign);

            glm::vec3 right = axisToVector(coordAxis) * static_cast<float>(coordSign);
            glm::vec3 up = axisToVector(upAxis) * static_cast<float>(upSign);
            glm::vec3 front = axisToVector(frontAxis) * static_cast<float>(frontSign);

            if (glm::length(right) < 0.5f || glm::length(up) < 0.5f || glm::length(front) < 0.5f)
            {
                return glm::mat4(1.0f);
            }

            if (glm::dot(glm::cross(right, up), front) < 0.0f)
            {
                front = -front;
            }

            glm::mat3 fbxBasis(right, up, front);
            glm::mat3 correction = glm::transpose(fbxBasis);

            glm::mat4 correction4(1.0f);
            correction4[0] = glm::vec4(correction[0], 0.0f);
            correction4[1] = glm::vec4(correction[1], 0.0f);
            correction4[2] = glm::vec4(correction[2], 0.0f);

            return correction4;
        }

        glm::mat4 aiMatrix4x4ToGlm(const aiMatrix4x4& aiMat)
        {
            // Assimp stores matrices in row-major order:
            // [ a1  b1  c1  d1 ]
            // [ a2  b2  c2  d2 ]
            // [ a3  b3  c3  d3 ]
            // [ a4  b4  c4  d4 ]
            //
            // GLM uses column-major layout and indexes as mat[col][row].
            // We therefore map rows to columns explicitly without transposing.
            glm::mat4 m(1.0f);

            m[0][0] = aiMat.a1; m[1][0] = aiMat.a2; m[2][0] = aiMat.a3; m[3][0] = aiMat.a4;
            m[0][1] = aiMat.b1; m[1][1] = aiMat.b2; m[2][1] = aiMat.b3; m[3][1] = aiMat.b4;
            m[0][2] = aiMat.c1; m[1][2] = aiMat.c2; m[2][2] = aiMat.c3; m[3][2] = aiMat.c4;
            m[0][3] = aiMat.d1; m[1][3] = aiMat.d2; m[2][3] = aiMat.d3; m[3][3] = aiMat.d4;

            return m;
        }

        glm::vec3 aiVector3DToGlm(const aiVector3D& aiVec)
        {
            return glm::vec3(aiVec.x, aiVec.y, aiVec.z);
        }

        glm::quat aiQuaternionToGlm(const aiQuaternion& aiQuat)
        {
            return glm::quat(aiQuat.w, aiQuat.x, aiQuat.y, aiQuat.z);
        }
    }

FBXLoader::FBXLoader()
{
    lastError = "";
}

FBXLoader::~FBXLoader()
{
}

std::shared_ptr<RiggedModel> FBXLoader::loadModel(const std::string& filePath)
{
    lastError = "";

    Assimp::Importer importer;

    // Import the scene with specific post-processing flags for rigged models
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_RemoveRedundantMaterials |
        aiProcess_OptimizeMeshes |
        aiProcess_OptimizeGraph |
        aiProcess_ValidateDataStructure);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        lastError = "Failed to load FBX file: " + std::string(importer.GetErrorString());
        return nullptr;
    }

    // Create the model
    auto model = std::make_shared<RiggedModel>();
    glm::mat4 rootTransform = aiMatrix4x4ToGlm(scene->mRootNode->mTransformation);
    rootTransform[0][3] = 0.0f;
    rootTransform[1][3] = 0.0f;
    rootTransform[2][3] = 0.0f;
    rootTransform[3][3] = 1.0f;
    model->axisCorrection = buildAxisCorrection(scene) * rootTransform;
    glm::vec3 correctedUp = glm::vec3(model->axisCorrection *
                                      glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    if (glm::length(correctedUp) > 0.0f)
    {
        correctedUp = glm::normalize(correctedUp);
        glm::quat alignUp = glm::rotation(correctedUp, glm::vec3(0.0f, 1.0f, 0.0f));
        model->axisCorrection = glm::mat4_cast(alignUp) * model->axisCorrection;
    }

    glm::vec3 correctedFront = glm::vec3(model->axisCorrection *
                                        glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    correctedFront -= glm::vec3(0.0f, 1.0f, 0.0f) *
                      glm::dot(correctedFront, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::length(correctedFront) > 0.0f)
    {
        correctedFront = glm::normalize(correctedFront);
        glm::quat alignFront = glm::rotation(correctedFront, glm::vec3(0.0f, 0.0f, 1.0f));
        model->axisCorrection = glm::mat4_cast(alignFront) * model->axisCorrection;
    }

    std::filesystem::path absFilePath = std::filesystem::absolute(filePath);
    currentModelDirectory_ = absFilePath.parent_path();
    embeddedTextures_.clear();

    // Store global inverse transform
    model->globalInverseTransform = aiMatrix4x4ToGlm(scene->mRootNode->mTransformation);
    model->globalInverseTransform = glm::inverse(model->globalInverseTransform);

    // Process the scene
    processNode(scene->mRootNode, scene, *model);

    // Process animations
    if (scene->mNumAnimations > 0)
    {
        processAnimations(scene, *model);
    }

    // Build bone hierarchy
    buildBoneHierarchy(scene->mRootNode, -1, *model);

    return model;
}

bool FBXLoader::isValidFBXFile(const std::string& filePath)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath, aiProcess_ValidateDataStructure);
    return (scene != nullptr && !(scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE));
}

void FBXLoader::processNode(aiNode* node, const aiScene* scene, RiggedModel& model)
{
    // Process all meshes in this node
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        RiggedMesh riggedMesh = processMesh(mesh, scene);
        model.meshes.push_back(riggedMesh);
    }

    // Process child nodes
    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        processNode(node->mChildren[i], scene, model);
    }
}

RiggedMesh FBXLoader::processMesh(aiMesh* mesh, const aiScene* scene)
{
    RiggedMesh riggedMesh;

    // Process vertices
    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        RiggedVertex vertex;

        // Position
        vertex.position = aiVector3DToGlm(mesh->mVertices[i]);

        // Normal
        if (mesh->mNormals)
            vertex.normal = aiVector3DToGlm(mesh->mNormals[i]);
        else
            vertex.normal = glm::vec3(0.0f);

        // Texture coordinates
        if (mesh->mTextureCoords[0])
        {
            vertex.texCoords = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
        }
        else
        {
            vertex.texCoords = glm::vec2(0.0f);
        }

        // Initialize bone data
        vertex.boneWeights = glm::vec4(0.0f);
        vertex.boneIndices = glm::ivec4(-1);

        riggedMesh.vertices.push_back(vertex);
    }

    // Process indices
    for (unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++)
        {
            riggedMesh.indices.push_back(face.mIndices[j]);
        }
    }

    // Process bones
    if (mesh->mNumBones > 0)
    {
        processBones(mesh, riggedMesh);
    }

    // Process material
    if (mesh->mMaterialIndex < scene->mNumMaterials)
    {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        aiString name;
        material->Get(AI_MATKEY_NAME, name);
        riggedMesh.materialName = name.C_Str();

        aiColor3D diffuseColor(1.0f, 1.0f, 1.0f);
        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == aiReturn_SUCCESS)
        {
            riggedMesh.diffuseColor = glm::vec4(diffuseColor.r, diffuseColor.g, diffuseColor.b,
                                                riggedMesh.diffuseColor.a);
        }

        float opacity = 1.0f;
        if (material->Get(AI_MATKEY_OPACITY, opacity) == aiReturn_SUCCESS)
        {
            riggedMesh.diffuseColor.a = opacity;
        }

        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0)
        {
            aiString texPath;
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == aiReturn_SUCCESS)
            {
                riggedMesh.diffuseTexturePath = resolveTexturePath(texPath.C_Str());
                auto embedded = fetchEmbeddedTexture(scene, texPath.C_Str());
                if (embedded)
                {
                    riggedMesh.embeddedTexture = embedded;
                    riggedMesh.embeddedTextureKey = "embedded:" + std::string(texPath.C_Str());
                }
            }
        }
    }

    return riggedMesh;
}

void FBXLoader::processBones(aiMesh* mesh, RiggedMesh& riggedMesh)
{
    // Collect all weights per vertex so we can keep the top 4 most influential bones.
    std::vector<std::vector<std::pair<int, float>>> vertexWeights(mesh->mNumVertices);

    // Populate per-vertex bone indices and weights (up to 4 influences per vertex).
    for (unsigned int i = 0; i < mesh->mNumBones; i++)
    {
        aiBone* bone = mesh->mBones[i];
        std::string boneName = bone->mName.C_Str();

        // Add bone to mapping if not already present
        int boneIndex;
        if (riggedMesh.boneMapping.find(boneName) == riggedMesh.boneMapping.end())
        {
            boneIndex = static_cast<int>(riggedMesh.bones.size());
            riggedMesh.boneMapping[boneName] = boneIndex;

            Bone newBone;
            newBone.name = boneName;
            newBone.offsetMatrix = aiMatrix4x4ToGlm(bone->mOffsetMatrix);
            newBone.localTransform = glm::mat4(1.0f);
            newBone.parentIndex = -1; // Will be set later in hierarchy building
            riggedMesh.bones.push_back(newBone);
        }
        else
        {
            boneIndex = riggedMesh.boneMapping[boneName];
        }

        // Set bone weights for vertices
        for (unsigned int j = 0; j < bone->mNumWeights; j++)
        {
            unsigned int vertexId = bone->mWeights[j].mVertexId;
            float weight = bone->mWeights[j].mWeight;

            if (vertexId < riggedMesh.vertices.size())
            {
                vertexWeights[vertexId].emplace_back(boneIndex, weight);
            }
        }
    }

    // Keep the top 4 weights per vertex and normalize them so they sum to 1.0.
    for (size_t v = 0; v < riggedMesh.vertices.size(); ++v)
    {
        auto& weights = vertexWeights[v];
        if (weights.empty())
        {
            continue;
        }

        std::sort(weights.begin(), weights.end(),
                  [](const auto& lhs, const auto& rhs)
                  {
                      return lhs.second > rhs.second;
                  });

        RiggedVertex& vertex = riggedMesh.vertices[v];
        float totalWeight = 0.0f;
        for (size_t k = 0; k < weights.size() && k < 4; ++k)
        {
            vertex.boneIndices[k] = weights[k].first;
            vertex.boneWeights[k] = weights[k].second;
            totalWeight += weights[k].second;
        }

        if (totalWeight > 0.0f)
        {
            float invTotal = 1.0f / totalWeight;
            for (int k = 0; k < 4; ++k)
            {
                vertex.boneWeights[k] *= invTotal;
            }
        }
    }
}

void FBXLoader::processAnimations(const aiScene* scene, RiggedModel& model)
{
    for (unsigned int i = 0; i < scene->mNumAnimations; i++)
    {
        aiAnimation* animation = scene->mAnimations[i];
        Animation anim;

        anim.name = animation->mName.C_Str();
        anim.duration = static_cast<float>(animation->mDuration);
        anim.ticksPerSecond = static_cast<float>(animation->mTicksPerSecond);

        if (anim.ticksPerSecond == 0.0f)
        {
            anim.ticksPerSecond = 25.0f; // Default to 25 FPS
        }

        // Process animation channels
        for (unsigned int j = 0; j < animation->mNumChannels; j++)
        {
            aiNodeAnim* channel = animation->mChannels[j];
            AnimationChannel animChannel;

            animChannel.boneName = channel->mNodeName.C_Str();
            processAnimationChannel(channel, animChannel);

            anim.channels.push_back(animChannel);
        }

        model.animations.push_back(anim);
    }
}

void FBXLoader::processAnimationChannel(aiNodeAnim* channel, AnimationChannel& animChannel)
{
    // Process position keys
    for (unsigned int i = 0; i < channel->mNumPositionKeys; i++)
    {
        AnimationKeyframe keyframe;
        keyframe.time = static_cast<float>(channel->mPositionKeys[i].mTime);
        keyframe.position = aiVector3DToGlm(channel->mPositionKeys[i].mValue);
        animChannel.positionKeys.push_back(keyframe);
    }

    // Process rotation keys
    for (unsigned int i = 0; i < channel->mNumRotationKeys; i++)
    {
        AnimationKeyframe keyframe;
        keyframe.time = static_cast<float>(channel->mRotationKeys[i].mTime);
        keyframe.rotation = aiQuaternionToGlm(channel->mRotationKeys[i].mValue);
        animChannel.rotationKeys.push_back(keyframe);
    }

    // Process scale keys
    for (unsigned int i = 0; i < channel->mNumScalingKeys; i++)
    {
        AnimationKeyframe keyframe;
        keyframe.time = static_cast<float>(channel->mScalingKeys[i].mTime);
        keyframe.scale = aiVector3DToGlm(channel->mScalingKeys[i].mValue);
        animChannel.scaleKeys.push_back(keyframe);
    }
}

void FBXLoader::buildBoneHierarchy(aiNode* node, int parentIndex, RiggedModel& model)
{
    std::string nodeName = node->mName.C_Str();

    // Check if this node corresponds to a bone
    int boneIndex = -1;
    for (auto& mesh : model.meshes)
    {
        if (mesh.boneMapping.find(nodeName) != mesh.boneMapping.end())
        {
            boneIndex = mesh.boneMapping[nodeName];
            break;
        }
    }

    if (boneIndex != -1)
    {
        // Add to global bone list if not already present
        if (model.boneMapping.find(nodeName) == model.boneMapping.end())
        {
            int globalBoneIndex = model.bones.size();
            model.boneMapping[nodeName] = globalBoneIndex;

            Bone bone;
            bone.name = nodeName;
            bone.parentIndex = parentIndex;
            bone.offsetMatrix = glm::mat4(1.0f); // Mesh data carries offsets per mesh
            bone.localTransform = aiMatrix4x4ToGlm(node->mTransformation);

            aiVector3D scaling;
            aiQuaternion rotation;
            aiVector3D translation;
            node->mTransformation.Decompose(scaling, rotation, translation);
            bone.bindPosition = aiVector3DToGlm(translation);
            bone.bindRotation = aiQuaternionToGlm(rotation);
            bone.bindScale = aiVector3DToGlm(scaling);

            model.bones.push_back(bone);

            if (parentIndex >= 0 && parentIndex < static_cast<int>(model.bones.size()))
            {
                model.bones[parentIndex].children.push_back(globalBoneIndex);
            }
        }

        // Process children
        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            buildBoneHierarchy(node->mChildren[i], model.boneMapping[nodeName], model);
        }
    }
    else
    {
        // Process children even if this node is not a bone
        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            buildBoneHierarchy(node->mChildren[i], parentIndex, model);
        }
    }
}

void FBXLoader::calculateBoneTransforms(RiggedModel& model, float animationTime, int animationIndex)
{
    if (animationIndex < 0 || animationIndex >= static_cast<int>(model.animations.size()))
        return;

    const Animation& animation = model.animations[animationIndex];

    for (auto& bone : model.bones)
    {
        calculateBoneTransform(bone.name, animationTime, animation, model.bones, model.boneMapping, bone.finalTransform);
    }
}

void FBXLoader::calculateBoneTransform(const std::string& boneName, float animationTime,
                                     const Animation& animation, const std::vector<Bone>& bones,
                                     const std::map<std::string, int>& boneMapping, glm::mat4& transform)
{
    (void) bones;
    (void) boneMapping;

    // Find the animation channel for this bone
    const AnimationChannel* channel = nullptr;
    for (const auto& ch : animation.channels)
    {
        if (ch.boneName == boneName)
        {
            channel = &ch;
            break;
        }
    }

    if (!channel)
    {
        transform = glm::mat4(1.0f);
        return;
    }

    // Interpolate position, rotation, and scale
    glm::vec3 position = interpolatePosition(animationTime, channel->positionKeys);
    glm::quat rotation = interpolateRotation(animationTime, channel->rotationKeys);
    glm::vec3 scale = interpolateScale(animationTime, channel->scaleKeys);

    // Build transformation matrix
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 rotationMatrix = glm::mat4_cast(rotation);
    glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);

    transform = translation * rotationMatrix * scaleMatrix;
}

glm::vec3 FBXLoader::interpolatePosition(float animationTime, const std::vector<AnimationKeyframe>& keys)
{
    if (keys.empty()) return glm::vec3(0.0f);
    if (keys.size() == 1) return keys[0].position;

    int keyIndex = findPositionKeyframe(animationTime, keys);
    int nextKeyIndex = keyIndex + 1;

    if (nextKeyIndex >= static_cast<int>(keys.size()))
        return keys[keyIndex].position;

    float deltaTime = keys[nextKeyIndex].time - keys[keyIndex].time;
    float factor = (animationTime - keys[keyIndex].time) / deltaTime;

    return glm::mix(keys[keyIndex].position, keys[nextKeyIndex].position, factor);
}

glm::quat FBXLoader::interpolateRotation(float animationTime, const std::vector<AnimationKeyframe>& keys)
{
    if (keys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (keys.size() == 1) return keys[0].rotation;

    int keyIndex = findRotationKeyframe(animationTime, keys);
    int nextKeyIndex = keyIndex + 1;

    if (nextKeyIndex >= static_cast<int>(keys.size()))
        return keys[keyIndex].rotation;

    float deltaTime = keys[nextKeyIndex].time - keys[keyIndex].time;
    float factor = (animationTime - keys[keyIndex].time) / deltaTime;

    return glm::slerp(keys[keyIndex].rotation, keys[nextKeyIndex].rotation, factor);
}

glm::vec3 FBXLoader::interpolateScale(float animationTime, const std::vector<AnimationKeyframe>& keys)
{
    if (keys.empty()) return glm::vec3(1.0f);
    if (keys.size() == 1) return keys[0].scale;

    int keyIndex = findScaleKeyframe(animationTime, keys);
    int nextKeyIndex = keyIndex + 1;

    if (nextKeyIndex >= static_cast<int>(keys.size()))
        return keys[keyIndex].scale;

    float deltaTime = keys[nextKeyIndex].time - keys[keyIndex].time;
    float factor = (animationTime - keys[keyIndex].time) / deltaTime;

    return glm::mix(keys[keyIndex].scale, keys[nextKeyIndex].scale, factor);
}

int FBXLoader::findPositionKeyframe(float animationTime, const std::vector<AnimationKeyframe>& keys)
{
    for (int i = 0; i < static_cast<int>(keys.size()) - 1; i++)
    {
        if (animationTime < keys[i + 1].time)
            return i;
    }
    return static_cast<int>(keys.size()) - 1;
}

int FBXLoader::findRotationKeyframe(float animationTime, const std::vector<AnimationKeyframe>& keys)
{
    for (int i = 0; i < static_cast<int>(keys.size()) - 1; i++)
    {
        if (animationTime < keys[i + 1].time)
            return i;
    }
    return static_cast<int>(keys.size()) - 1;
}

int FBXLoader::findScaleKeyframe(float animationTime, const std::vector<AnimationKeyframe>& keys)
{
    for (int i = 0; i < static_cast<int>(keys.size()) - 1; i++)
    {
        if (animationTime < keys[i + 1].time)
            return i;
    }
    return static_cast<int>(keys.size()) - 1;
}

std::shared_ptr<EmbeddedTextureData> FBXLoader::decodeEmbeddedTexture(const aiTexture* texture)
{
    if (!texture)
    {
        return nullptr;
    }

    auto data = std::make_shared<EmbeddedTextureData>();
    if (texture->mHeight == 0)
    {
        size_t byteCount = static_cast<size_t>(texture->mWidth);
        data->data.resize(byteCount);
        std::memcpy(data->data.data(), texture->pcData, byteCount);
        data->isRawPixels = false;
        if (texture->achFormatHint[0] != '\0')
        {
            data->formatHint = texture->achFormatHint;
        }
    }
    else
    {
        size_t pixelCount = static_cast<size_t>(texture->mWidth) * static_cast<size_t>(texture->mHeight);
        data->data.resize(pixelCount * 4);
        const aiTexel* texels = texture->pcData;
        for (size_t i = 0; i < pixelCount; ++i)
        {
            data->data[i * 4 + 0] = texels[i].r;
            data->data[i * 4 + 1] = texels[i].g;
            data->data[i * 4 + 2] = texels[i].b;
            data->data[i * 4 + 3] = texels[i].a;
        }
        data->isRawPixels = true;
        data->width = texture->mWidth;
        data->height = texture->mHeight;
    }
    return data;
}

std::shared_ptr<EmbeddedTextureData> FBXLoader::fetchEmbeddedTextureByShortName(const aiScene* scene, const std::string& texturePath)
{
    if (!scene)
    {
        return nullptr;
    }

    std::string shortName = texturePath;
    size_t slash = shortName.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        shortName = shortName.substr(slash + 1);
    }

    if (shortName.empty())
    {
        return nullptr;
    }

    auto cached = embeddedTextures_.find(shortName);
    if (cached != embeddedTextures_.end())
    {
        return cached->second;
    }

    const aiTexture* tex = scene->GetEmbeddedTexture(shortName.c_str());
    if (!tex)
    {
        return nullptr;
    }

    auto data = decodeEmbeddedTexture(tex);
    if (data)
    {
        embeddedTextures_[shortName] = data;
    }
    return data;
}

std::shared_ptr<EmbeddedTextureData> FBXLoader::fetchEmbeddedTexture(const aiScene* scene, const std::string& texturePath)
{
    if (!scene || texturePath.empty())
    {
        return nullptr;
    }

    auto cached = embeddedTextures_.find(texturePath);
    if (cached != embeddedTextures_.end())
    {
        return cached->second;
    }

    const aiTexture* tex = scene->GetEmbeddedTexture(texturePath.c_str());
    if (!tex && texturePath[0] == '*')
    {
        tex = scene->GetEmbeddedTexture(texturePath.c_str() + 1);
    }

    if (!tex)
    {
        return fetchEmbeddedTextureByShortName(scene, texturePath);
    }

    auto data = decodeEmbeddedTexture(tex);
    if (data)
    {
        embeddedTextures_[texturePath] = data;

        std::string shortName = texturePath;
        size_t slash = shortName.find_last_of("/\\");
        if (slash != std::string::npos)
        {
            shortName = shortName.substr(slash + 1);
        }
        if (!shortName.empty())
        {
            embeddedTextures_[shortName] = data;
        }
    }
    return data;
}

std::string FBXLoader::resolveTexturePath(const std::string& texturePath) const
{
    if (texturePath.empty())
    {
        return {};
    }

    if (!texturePath.empty() && texturePath[0] == '*')
    {
        // Embedded textures are currently unsupported; return marker directly.
        return texturePath;
    }

    std::filesystem::path rawPath = texturePath;
    if (!rawPath.is_absolute() && !currentModelDirectory_.empty())
    {
        rawPath = currentModelDirectory_ / rawPath;
    }

    return rawPath.lexically_normal().string();
}

} // namespace lightGraphics
