// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2026 Dr. Nathanael John Inkson

#pragma once

#include "Light.h"
#include "pObject.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lightGraphics
{
	class RiggedObject;
	class VkApp;

	struct Transform
	{
		glm::vec3 position{0.0f};
		glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
		glm::vec3 scale{1.0f};

		glm::mat4 matrix() const;
		static Transform fromMatrix(const glm::mat4& matrix);
	};

	struct SceneNodeHandle
	{
		uint32_t index = UINT32_MAX;
		uint32_t generation = 0;

		bool isValid() const { return index != UINT32_MAX && generation != 0; }
	};

	bool operator==(SceneNodeHandle lhs, SceneNodeHandle rhs);
	bool operator!=(SceneNodeHandle lhs, SceneNodeHandle rhs);

	enum class ReparentMode
	{
		KeepLocalTransform,
		PreserveWorldTransform
	};

	class SceneGraph
	{
	public:
		explicit SceneGraph(VkApp& app);
		SceneGraph(const SceneGraph&) = delete;
		SceneGraph& operator=(const SceneGraph&) = delete;

		SceneNodeHandle root() const { return root_; }

		SceneNodeHandle createNode(const std::string& name = "",
		                           SceneNodeHandle parent = SceneNodeHandle{});
		SceneNodeHandle createObjectNode(const pObject& object,
		                                 SceneNodeHandle parent = SceneNodeHandle{},
		                                 const std::string& nodeName = "");
		SceneNodeHandle createObjectNode(ShapeType type,
		                                 const glm::vec3& position,
		                                 const glm::vec3& size,
		                                 const glm::vec4& color,
		                                 const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		                                 const std::string& objectName = "",
		                                 float mass = 1.0f,
		                                 SceneNodeHandle parent = SceneNodeHandle{});
		SceneNodeHandle createRiggedObjectNode(const std::shared_ptr<RiggedObject>& riggedObject,
		                                       SceneNodeHandle parent = SceneNodeHandle{},
		                                       const std::string& nodeName = "");
		SceneNodeHandle createLightNode(const LightSource& light,
		                                SceneNodeHandle parent = SceneNodeHandle{},
		                                const std::string& nodeName = "");

		void destroyNode(SceneNodeHandle node);
		bool contains(SceneNodeHandle node) const;

		const std::string& getName(SceneNodeHandle node) const;
		void setName(SceneNodeHandle node, const std::string& name);

		std::optional<SceneNodeHandle> getParent(SceneNodeHandle node) const;
		const std::vector<SceneNodeHandle>& getChildren(SceneNodeHandle node) const;
		void setParent(SceneNodeHandle child,
		               SceneNodeHandle parent,
		               ReparentMode mode = ReparentMode::KeepLocalTransform);

		Transform getLocalTransform(SceneNodeHandle node) const;
		void setLocalTransform(SceneNodeHandle node, const Transform& transform);
		void setLocalPosition(SceneNodeHandle node, const glm::vec3& position);
		void setLocalRotation(SceneNodeHandle node, const glm::quat& rotation);
		void setLocalScale(SceneNodeHandle node, const glm::vec3& scale);

		glm::mat4 getWorldTransform(SceneNodeHandle node);
		void updateWorldTransforms();
		void syncToRenderer();

		size_t attachObject(SceneNodeHandle node, const pObject& object);
		size_t attachObject(SceneNodeHandle node,
		                    ShapeType type,
		                    const glm::vec3& position,
		                    const glm::vec3& size,
		                    const glm::vec4& color,
		                    const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
		                    const std::string& name = "",
		                    float mass = 1.0f);
		size_t attachRiggedObject(SceneNodeHandle node,
		                          const std::shared_ptr<RiggedObject>& riggedObject);
		size_t attachLight(SceneNodeHandle node, const LightSource& light);

		std::optional<size_t> getAttachedObjectIndex(SceneNodeHandle node) const;
		std::optional<size_t> getAttachedRiggedObjectIndex(SceneNodeHandle node) const;
		std::optional<size_t> getAttachedLightIndex(SceneNodeHandle node) const;
		void detachObject(SceneNodeHandle node, bool removeFromRenderer = false);
		void detachRiggedObject(SceneNodeHandle node, bool removeFromRenderer = false);
		void detachLight(SceneNodeHandle node, bool removeFromRenderer = false);

	private:
		struct Node
		{
			uint32_t generation = 1;
			bool alive = false;
			std::string name;
			Transform localTransform;
			glm::mat4 worldTransform{1.0f};
			SceneNodeHandle parent{};
			std::vector<SceneNodeHandle> children;
			std::optional<size_t> objectIndex;
			std::optional<size_t> riggedObjectIndex;
			std::optional<size_t> lightIndex;
			bool worldDirty = true;
			bool rendererDirty = true;
		};

		VkApp& app_;
		std::vector<Node> nodes_;
		std::vector<uint32_t> freeList_;
		SceneNodeHandle root_{};

		Node& getNode(SceneNodeHandle node);
		const Node& getNode(SceneNodeHandle node) const;
		SceneNodeHandle allocateNode();
		SceneNodeHandle resolveParent(SceneNodeHandle parent) const;
		void detachFromParent(SceneNodeHandle node);
		void addChild(SceneNodeHandle parent, SceneNodeHandle child);
		void removeChild(SceneNodeHandle parent, SceneNodeHandle child);
		void markWorldDirty(SceneNodeHandle node);
		void updateNodeWorldTransform(SceneNodeHandle node,
		                              const glm::mat4& parentWorld,
		                              bool parentDirty);
		bool isAncestor(SceneNodeHandle possibleAncestor, SceneNodeHandle node) const;
		void onObjectChanged(size_t index);
		void onObjectRemoved(size_t index);
		void onRiggedObjectRemoved(size_t index);
		void onLightChanged(size_t index);
		void onLightRemoved(size_t index);

		friend class VkApp;
	};
}
