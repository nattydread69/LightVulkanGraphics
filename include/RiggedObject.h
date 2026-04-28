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

#ifndef LIGHT_VULKAN_GRAPHICS_RIGGED_OBJECT_H
#define LIGHT_VULKAN_GRAPHICS_RIGGED_OBJECT_H

#include "pObject.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <memory>

namespace lightGraphics
{
    class FBXLoader;
    struct RiggedModel;

    /**
     * RiggedObject class for handling rigged 3D models with bones and animations
     * Extends pObject to support complex animated models loaded from FBX files
     */
    class RiggedObject : public pObject
    {
    public:
        /**
         * Constructor for rigged objects
         * @param center Position of the object
         * @param size Scale of the object
         * @param rotation Initial rotation
         * @param name Name of the object
         * @param mass Mass of the object
         * @param modelPath Path to the FBX model file
         */
        RiggedObject(glm::vec3 const &center,
                    glm::vec3 const &size,
                    glm::quat const &rotation,
                    std::string const &name,
                    float const mass,
                    std::string const &modelPath);

        /**
         * Constructor for rigged objects with pre-loaded model
         * @param center Position of the object
         * @param size Scale of the object
         * @param rotation Initial rotation
         * @param name Name of the object
         * @param mass Mass of the object
         * @param model Pre-loaded rigged model
         */
        RiggedObject(glm::vec3 const &center,
                    glm::vec3 const &size,
                    glm::quat const &rotation,
                    std::string const &name,
                    float const mass,
                    std::shared_ptr<RiggedModel> model);

        ~RiggedObject();

        // Animation control
        /**
         * Play an animation by index
         * @param animationIndex Index of the animation to play
         * @param loop Whether to loop the animation
         */
        void playAnimation(int animationIndex, bool loop = true);

        /**
         * Play an animation by name
         * @param animationName Name of the animation to play
         * @param loop Whether to loop the animation
         * @return True if animation was found and started
         */
        bool playAnimation(const std::string& animationName, bool loop = true);

        /**
         * Stop the current animation
         */
        void stopAnimation();

        /**
         * Pause the current animation
         */
        void pauseAnimation();

        /**
         * Resume the current animation
         */
        void resumeAnimation();

        /**
         * Update animation (call this every frame)
         * @param deltaTime Time elapsed since last frame
         */
        void updateAnimation(float deltaTime);

        /**
         * Set animation speed multiplier
         * @param speed Speed multiplier (1.0 = normal speed)
         */
        void setAnimationSpeed(float speed);

        /**
         * Get current animation time
         * @return Current animation time in seconds
         */
        float getAnimationTime() const { return currentAnimationTime; }

        /**
         * Get animation duration
         * @return Duration of current animation in seconds
         */
        float getAnimationDuration() const;

        /**
         * Check if an animation is currently playing
         * @return True if animation is playing
         */
        bool isAnimating() const { return isPlaying && currentAnimationIndex >= 0; }
        int getCurrentAnimationIndex() const { return currentAnimationIndex; }
        bool getAnimationLooping() const { return loopAnimation; }

        // Model access
        /**
         * Get the rigged model data
         * @return Shared pointer to the model
         */
        std::shared_ptr<RiggedModel> getModel() const { return model; }

        /**
         * Get bone transforms for rendering
         * @return Vector of bone transformation matrices
         */
        const std::vector<glm::mat4>& getBoneTransforms() const;

        /**
         * Get bone transform by name
         * @param boneName Name of the bone
         * @return Transformation matrix of the bone
         */
        glm::mat4 getBoneTransform(const std::string& boneName) const;

        /**
         * Get all available animation names
         * @return Vector of animation names
         */
        std::vector<std::string> getAnimationNames() const;

        /**
         * Get number of animations
         * @return Number of available animations
         */
        int getAnimationCount() const;

        // Model loading
        /**
         * Load a new model from file
         * @param modelPath Path to the FBX file
         * @return True if model loaded successfully
         */
        bool loadModel(const std::string& modelPath);

        /**
         * Set a new model
         * @param newModel Shared pointer to the new model
         */
        void setModel(std::shared_ptr<RiggedModel> newModel);

        // Bone manipulation
        /**
         * Set bone transform manually (overrides animation)
         * @param boneName Name of the bone
         * @param transform New transformation matrix
         */
        void setBoneTransform(const std::string& boneName, const glm::mat4& transform);

        /**
         * Reset bone transforms to bind pose
         */
        void resetBoneTransforms();

        // Utility
        /**
         * Get the last error message
         * @return Error message string
         */
        std::string getLastError() const { return lastError; }

    private:
        std::shared_ptr<RiggedModel> model;
        std::unique_ptr<FBXLoader> fbxLoader;

        // Animation state
        int currentAnimationIndex;
        float currentAnimationTime;
        float animationSpeed;
        bool isPlaying;
        bool isPaused;
        bool loopAnimation;

        // Bone transforms (updated each frame)
        std::vector<glm::mat4> boneTransforms;

        // Error handling
        std::string lastError;

        // Internal methods
        void initializeAnimation();
        void updateBoneTransforms();
        int findAnimationIndex(const std::string& animationName) const;
        void calculateBoneTransforms();
    };

} // namespace lightGraphics

#endif // LIGHT_VULKAN_GRAPHICS_RIGGED_OBJECT_H
