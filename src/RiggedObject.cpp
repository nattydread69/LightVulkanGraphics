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

#include "RiggedObject.h"
#include "FBXLoader.h"

#include <algorithm>
#include <iostream>

namespace lightGraphics
{

RiggedObject::~RiggedObject() = default;

RiggedObject::RiggedObject(glm::vec3 const &center,
                          glm::vec3 const &size,
                          glm::quat const &rotation,
                          std::string const &name,
                          float const mass,
                          std::string const &modelPath)
    : pObject(ShapeType::HUMAN, center, size, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), rotation, name, mass)
    , currentAnimationIndex(-1)
    , currentAnimationTime(0.0f)
    , animationSpeed(1.0f)
    , isPlaying(false)
    , isPaused(false)
    , loopAnimation(true)
{
    fbxLoader = std::make_unique<FBXLoader>();
    loadModel(modelPath);
    initializeAnimation();
}

RiggedObject::RiggedObject(glm::vec3 const &center,
                          glm::vec3 const &size,
                          glm::quat const &rotation,
                          std::string const &name,
                          float const mass,
                          std::shared_ptr<RiggedModel> model)
    : pObject(ShapeType::HUMAN, center, size, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),rotation, name, mass)
    , model(model)
    , currentAnimationIndex(-1)
    , currentAnimationTime(0.0f)
    , animationSpeed(1.0f)
    , isPlaying(false)
    , isPaused(false)
    , loopAnimation(true)
{
    fbxLoader = std::make_unique<FBXLoader>();
    initializeAnimation();
}

void RiggedObject::playAnimation(int animationIndex, bool loop)
{
    if (!model || animationIndex < 0 || animationIndex >= static_cast<int>(model->animations.size()))
    {
        lastError = "Invalid animation index: " + std::to_string(animationIndex);
        return;
    }

    currentAnimationIndex = animationIndex;
    currentAnimationTime = 0.0f;
    loopAnimation = loop;
    isPlaying = true;
    isPaused = false;

    updateBoneTransforms();
}

bool RiggedObject::playAnimation(const std::string& animationName, bool loop)
{
    int animationIndex = findAnimationIndex(animationName);
    if (animationIndex >= 0)
    {
        playAnimation(animationIndex, loop);
        return true;
    }

    lastError = "Animation not found: " + animationName;
    return false;
}

void RiggedObject::stopAnimation()
{
    isPlaying = false;
    isPaused = false;
    currentAnimationIndex = -1;
    currentAnimationTime = 0.0f;
    resetBoneTransforms();
}

void RiggedObject::pauseAnimation()
{
    if (isPlaying)
    {
        isPaused = true;
    }
}

void RiggedObject::resumeAnimation()
{
    if (isPlaying && isPaused)
    {
        isPaused = false;
    }
}

void RiggedObject::updateAnimation(float deltaTime)
{
    if (!isPlaying || isPaused || !model || currentAnimationIndex < 0)
        return;

    const Animation& animation = model->animations[currentAnimationIndex];

    // Update animation time in seconds
    currentAnimationTime += deltaTime * animationSpeed;

    // Convert animation duration from ticks to seconds if possible
    float durationSeconds = (animation.ticksPerSecond > 0.0f)
                                ? (animation.duration / animation.ticksPerSecond)
                                : animation.duration;

    if (durationSeconds <= 0.0f)
    {
        currentAnimationTime = 0.0f;
        isPlaying = false;
        updateBoneTransforms();
        return;
    }

    // Check if animation should loop or stop (time in seconds)
    if (currentAnimationTime >= durationSeconds)
    {
        if (loopAnimation)
        {
            currentAnimationTime = fmod(currentAnimationTime, durationSeconds);
        }
        else
        {
            currentAnimationTime = durationSeconds;
            isPlaying = false;
        }
    }

    updateBoneTransforms();
}

void RiggedObject::setAnimationSpeed(float speed)
{
    animationSpeed = std::max(0.0f, speed);
}

float RiggedObject::getAnimationDuration() const
{
    if (!model || currentAnimationIndex < 0 || currentAnimationIndex >= static_cast<int>(model->animations.size()))
        return 0.0f;

    const Animation& animation = model->animations[currentAnimationIndex];
    if (animation.ticksPerSecond <= 0.0f)
    {
        return animation.duration;
    }
    return animation.duration / animation.ticksPerSecond;
}

const std::vector<glm::mat4>& RiggedObject::getBoneTransforms() const
{
    return boneTransforms;
}

glm::mat4 RiggedObject::getBoneTransform(const std::string& boneName) const
{
    if (!model)
        return glm::mat4(1.0f);

    auto it = model->boneMapping.find(boneName);
    if (it != model->boneMapping.end() && it->second < static_cast<int>(boneTransforms.size()))
    {
        return boneTransforms[it->second];
    }

    return glm::mat4(1.0f);
}

std::vector<std::string> RiggedObject::getAnimationNames() const
{
    std::vector<std::string> names;
    if (!model)
        return names;

    for (const auto& animation : model->animations)
    {
        names.push_back(animation.name);
    }

    return names;
}

int RiggedObject::getAnimationCount() const
{
    if (!model)
        return 0;

    return static_cast<int>(model->animations.size());
}

bool RiggedObject::loadModel(const std::string& modelPath)
{
    if (!fbxLoader)
    {
        fbxLoader = std::make_unique<FBXLoader>();
    }

    model = fbxLoader->loadModel(modelPath);
    if (!model)
    {
        lastError = fbxLoader->getLastError();
        return false;
    }

    initializeAnimation();
    return true;
}

void RiggedObject::setModel(std::shared_ptr<RiggedModel> newModel)
{
    model = newModel;
    initializeAnimation();
}

void RiggedObject::setBoneTransform(const std::string& boneName, const glm::mat4& transform)
{
    if (!model)
        return;

    auto it = model->boneMapping.find(boneName);
    if (it != model->boneMapping.end() && it->second < static_cast<int>(boneTransforms.size()))
    {
        boneTransforms[it->second] = transform;
    }
}

void RiggedObject::resetBoneTransforms()
{
    if (!model)
        return;

    boneTransforms.resize(model->bones.size(), glm::mat4(1.0f));

    for (size_t i = 0; i < model->bones.size(); ++i)
    {
        const Bone& bone = model->bones[i];

        glm::mat4 translation = glm::translate(glm::mat4(1.0f), bone.bindPosition);
        glm::mat4 rotationMatrix = glm::mat4_cast(glm::normalize(bone.bindRotation));
        glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), bone.bindScale);

        glm::mat4 transform = translation * rotationMatrix * scaleMatrix;

        if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(boneTransforms.size()))
        {
            transform = boneTransforms[bone.parentIndex] * transform;
        }

        boneTransforms[i] = transform;
    }
}

void RiggedObject::initializeAnimation()
{
    currentAnimationIndex = -1;
    currentAnimationTime = 0.0f;
    isPlaying = false;
    isPaused = false;
    loopAnimation = true;

    if (model)
    {
        resetBoneTransforms();
    }
    else
    {
        boneTransforms.clear();
    }
}

void RiggedObject::updateBoneTransforms()
{
    if (!model || currentAnimationIndex < 0)
        return;

    calculateBoneTransforms();
}

int RiggedObject::findAnimationIndex(const std::string& animationName) const
{
    if (!model)
        return -1;

    for (int i = 0; i < static_cast<int>(model->animations.size()); i++)
    {
        if (model->animations[i].name == animationName)
        {
            return i;
        }
    }

    return -1;
}

void RiggedObject::calculateBoneTransforms()
{
    if (!model || currentAnimationIndex < 0)
        return;

    const Animation& animation = model->animations[currentAnimationIndex];

    // AnimationKeyframe::time is stored in "ticks" from Assimp.
    // Convert our current time in seconds to ticks for interpolation.
    float timeInTicks = (animation.ticksPerSecond > 0.0f)
                            ? (currentAnimationTime * animation.ticksPerSecond)
                            : currentAnimationTime;

    // Calculate transforms for each bone
    for (size_t i = 0; i < model->bones.size(); i++)
    {
        const Bone& bone = model->bones[i];
        glm::vec3 position = bone.bindPosition;
        glm::quat rotation = bone.bindRotation;
        glm::vec3 scale = bone.bindScale;

        // Find animation channel for this bone
        const AnimationChannel* channel = nullptr;
        for (const auto& ch : animation.channels)
        {
            if (ch.boneName == bone.name)
            {
                channel = &ch;
                break;
            }
        }

        if (channel)
        {
            // Interpolate position
            if (!channel->positionKeys.empty())
            {
                if (channel->positionKeys.size() == 1)
                {
                    position = channel->positionKeys[0].position;
                }
                else
                {
                    int keyIndex = 0;
                    for (int j = 0; j < static_cast<int>(channel->positionKeys.size()) - 1; j++)
                    {
                        if (timeInTicks < channel->positionKeys[j + 1].time)
                        {
                            keyIndex = j;
                            break;
                        }
                    }

                    if (keyIndex < static_cast<int>(channel->positionKeys.size()) - 1)
                    {
                        float deltaTime = channel->positionKeys[keyIndex + 1].time - channel->positionKeys[keyIndex].time;
                        float factor = (timeInTicks - channel->positionKeys[keyIndex].time) / deltaTime;
                        position = glm::mix(channel->positionKeys[keyIndex].position,
                                          channel->positionKeys[keyIndex + 1].position, factor);
                    }
                    else
                    {
                        position = channel->positionKeys[keyIndex].position;
                    }
                }
            }

            // Interpolate rotation
            if (!channel->rotationKeys.empty())
            {
                if (channel->rotationKeys.size() == 1)
                {
                    rotation = channel->rotationKeys[0].rotation;
                }
                else
                {
                    int keyIndex = 0;
                    for (int j = 0; j < static_cast<int>(channel->rotationKeys.size()) - 1; j++)
                    {
                        if (timeInTicks < channel->rotationKeys[j + 1].time)
                        {
                            keyIndex = j;
                            break;
                        }
                    }

                    if (keyIndex < static_cast<int>(channel->rotationKeys.size()) - 1)
                    {
                        float deltaTime = channel->rotationKeys[keyIndex + 1].time - channel->rotationKeys[keyIndex].time;
                        float factor = (timeInTicks - channel->rotationKeys[keyIndex].time) / deltaTime;
                        rotation = glm::slerp(channel->rotationKeys[keyIndex].rotation,
                                            channel->rotationKeys[keyIndex + 1].rotation, factor);
                    }
                    else
                    {
                        rotation = channel->rotationKeys[keyIndex].rotation;
                    }
                }
            }

            // Interpolate scale
            if (!channel->scaleKeys.empty())
            {
                if (channel->scaleKeys.size() == 1)
                {
                    scale = channel->scaleKeys[0].scale;
                }
                else
                {
                    int keyIndex = 0;
                    for (int j = 0; j < static_cast<int>(channel->scaleKeys.size()) - 1; j++)
                    {
                        if (timeInTicks < channel->scaleKeys[j + 1].time)
                        {
                            keyIndex = j;
                            break;
                        }
                    }

                    if (keyIndex < static_cast<int>(channel->scaleKeys.size()) - 1)
                    {
                        float deltaTime = channel->scaleKeys[keyIndex + 1].time - channel->scaleKeys[keyIndex].time;
                        float factor = (timeInTicks - channel->scaleKeys[keyIndex].time) / deltaTime;
                        scale = glm::mix(channel->scaleKeys[keyIndex].scale,
                                       channel->scaleKeys[keyIndex + 1].scale, factor);
                    }
                    else
                    {
                        scale = channel->scaleKeys[keyIndex].scale;
                    }
                }
            }
        }

        // Build transformation matrix
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 rotationMatrix = glm::mat4_cast(rotation);
        glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);

        glm::mat4 transform = translation * rotationMatrix * scaleMatrix;

        // Apply parent transform if this bone has a parent
        if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(boneTransforms.size()))
        {
            transform = boneTransforms[bone.parentIndex] * transform;
        }

        boneTransforms[i] = transform;
    }
}

} // namespace lightGraphics
