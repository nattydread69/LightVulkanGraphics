// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2025 Dr. Nathanael John Inkson

#include "SceneGraph.h"
#include "RiggedObject.h"
#include "VkApp.h"

#include <glm/gtc/matrix_transform.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace lightGraphics
{
	namespace
	{
		glm::mat4 objectLocalMatrix(const pObject& object)
		{
			const glm::mat4 translation = glm::translate(glm::mat4(1.0f), object.getPosition());
			const glm::mat4 rotation = glm::mat4_cast(object.getRotation());
			const glm::mat4 scale = glm::scale(glm::mat4(1.0f), object.getSize());
			return translation * rotation * scale;
		}

		pObject makeRenderableObject(const pObject& source)
		{
			pObject renderable(source);
			renderable.setPosition(glm::vec3(0.0f));
			renderable.setRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
			renderable.setSize(glm::vec3(1.0f));
			return renderable;
		}

		LightSource makeRenderableLight(const LightSource& source)
		{
			LightSource renderable(source);
			renderable.position = glm::vec3(0.0f);
			return renderable;
		}
	}

	glm::mat4 Transform::matrix() const
	{
		const glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);
		const glm::mat4 rotationMatrix = glm::mat4_cast(rotation);
		const glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
		return translation * rotationMatrix * scaleMatrix;
	}

	Transform Transform::fromMatrix(const glm::mat4& matrix)
	{
		Transform transform;
		glm::vec3 skew(0.0f);
		glm::vec4 perspective(0.0f);
		glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);

		if (glm::decompose(matrix, transform.scale, orientation, transform.position, skew, perspective))
		{
			transform.rotation = glm::normalize(orientation);
		}
		return transform;
	}

	bool operator==(SceneNodeHandle lhs, SceneNodeHandle rhs)
	{
		return lhs.index == rhs.index && lhs.generation == rhs.generation;
	}

	bool operator!=(SceneNodeHandle lhs, SceneNodeHandle rhs)
	{
		return !(lhs == rhs);
	}

	SceneGraph::SceneGraph(VkApp& app)
	    : app_(app)
	{
		Node rootNode;
		rootNode.alive = true;
		rootNode.name = "Root";
		rootNode.worldDirty = true;
		rootNode.rendererDirty = false;
		nodes_.push_back(std::move(rootNode));
		root_ = SceneNodeHandle{0, nodes_[0].generation};
	}

	SceneNodeHandle SceneGraph::createNode(const std::string& name, SceneNodeHandle parent)
	{
		SceneNodeHandle handle = allocateNode();
		Node& node = getNode(handle);
		node.name = name;
		node.parent = resolveParent(parent);
		node.localTransform = Transform{};
		node.worldTransform = glm::mat4(1.0f);
		node.worldDirty = true;
		node.rendererDirty = true;

		addChild(node.parent, handle);
		return handle;
	}

	SceneNodeHandle SceneGraph::createObjectNode(const pObject& object,
	                                             SceneNodeHandle parent,
	                                             const std::string& nodeName)
	{
		SceneNodeHandle handle = createNode(nodeName.empty() ? object.getName() : nodeName, parent);
		setLocalTransform(handle, Transform{
			object.getPosition(),
			object.getRotation(),
			object.getSize()
		});
		attachObject(handle, makeRenderableObject(object));
		return handle;
	}

	SceneNodeHandle SceneGraph::createObjectNode(ShapeType type,
	                                             const glm::vec3& position,
	                                             const glm::vec3& size,
	                                             const glm::vec4& color,
	                                             const glm::quat& rotation,
	                                             const std::string& objectName,
	                                             float mass,
	                                             SceneNodeHandle parent)
	{
		pObject object(type, position, size, color, rotation, objectName, mass);
		return createObjectNode(object, parent, objectName);
	}

	SceneNodeHandle SceneGraph::createRiggedObjectNode(const std::shared_ptr<RiggedObject>& riggedObject,
	                                                   SceneNodeHandle parent,
	                                                   const std::string& nodeName)
	{
		if (!riggedObject)
		{
			throw std::invalid_argument("createRiggedObjectNode: rigged object pointer is null");
		}

		SceneNodeHandle handle = createNode(nodeName.empty() ? riggedObject->getName() : nodeName, parent);
		attachRiggedObject(handle, riggedObject);
		return handle;
	}

	SceneNodeHandle SceneGraph::createLightNode(const LightSource& light,
	                                            SceneNodeHandle parent,
	                                            const std::string& nodeName)
	{
		SceneNodeHandle handle = createNode(nodeName.empty() ? light.name : nodeName, parent);
		Transform transform;
		transform.position = light.position;
		setLocalTransform(handle, transform);
		attachLight(handle, makeRenderableLight(light));
		return handle;
	}

	void SceneGraph::destroyNode(SceneNodeHandle node)
	{
		if (node == root_)
		{
			throw std::invalid_argument("destroyNode: root node cannot be destroyed");
		}

		Node& record = getNode(node);
		std::vector<SceneNodeHandle> children = record.children;
		for (SceneNodeHandle child : children)
		{
			destroyNode(child);
		}

		detachObject(node, true);
		detachRiggedObject(node, true);
		detachLight(node, true);
		detachFromParent(node);

		record.children.clear();
		record.parent = SceneNodeHandle{};
		record.alive = false;
		++record.generation;
		record.worldDirty = false;
		record.rendererDirty = false;
		freeList_.push_back(node.index);
	}

	bool SceneGraph::contains(SceneNodeHandle node) const
	{
		if (!node.isValid() || node.index >= nodes_.size())
		{
			return false;
		}

		const Node& record = nodes_[node.index];
		return record.alive && record.generation == node.generation;
	}

	const std::string& SceneGraph::getName(SceneNodeHandle node) const
	{
		return getNode(node).name;
	}

	void SceneGraph::setName(SceneNodeHandle node, const std::string& name)
	{
		getNode(node).name = name;
	}

	std::optional<SceneNodeHandle> SceneGraph::getParent(SceneNodeHandle node) const
	{
		const Node& record = getNode(node);
		if (!record.parent.isValid())
		{
			return std::nullopt;
		}
		return record.parent;
	}

	const std::vector<SceneNodeHandle>& SceneGraph::getChildren(SceneNodeHandle node) const
	{
		return getNode(node).children;
	}

	void SceneGraph::setParent(SceneNodeHandle child, SceneNodeHandle parent, ReparentMode mode)
	{
		if (child == root_)
		{
			throw std::invalid_argument("setParent: root node cannot be reparented");
		}

		Node& childNode = getNode(child);
		SceneNodeHandle newParent = resolveParent(parent);
		if (newParent == child)
		{
			throw std::invalid_argument("setParent: node cannot be parented to itself");
		}
		if (isAncestor(child, newParent))
		{
			throw std::invalid_argument("setParent: operation would create a cycle");
		}

		glm::mat4 oldWorld = getWorldTransform(child);
		detachFromParent(child);
		childNode.parent = newParent;
		addChild(newParent, child);

		if (mode == ReparentMode::PreserveWorldTransform)
		{
			glm::mat4 parentWorld = getWorldTransform(newParent);
			childNode.localTransform = Transform::fromMatrix(glm::inverse(parentWorld) * oldWorld);
		}

		markWorldDirty(child);
	}

	Transform SceneGraph::getLocalTransform(SceneNodeHandle node) const
	{
		return getNode(node).localTransform;
	}

	void SceneGraph::setLocalTransform(SceneNodeHandle node, const Transform& transform)
	{
		Node& record = getNode(node);
		record.localTransform = transform;
		markWorldDirty(node);
	}

	void SceneGraph::setLocalPosition(SceneNodeHandle node, const glm::vec3& position)
	{
		Node& record = getNode(node);
		record.localTransform.position = position;
		markWorldDirty(node);
	}

	void SceneGraph::setLocalRotation(SceneNodeHandle node, const glm::quat& rotation)
	{
		Node& record = getNode(node);
		record.localTransform.rotation = glm::normalize(rotation);
		markWorldDirty(node);
	}

	void SceneGraph::setLocalScale(SceneNodeHandle node, const glm::vec3& scale)
	{
		Node& record = getNode(node);
		record.localTransform.scale = scale;
		markWorldDirty(node);
	}

	glm::mat4 SceneGraph::getWorldTransform(SceneNodeHandle node)
	{
		updateWorldTransforms();
		return getNode(node).worldTransform;
	}

	void SceneGraph::updateWorldTransforms()
	{
		updateNodeWorldTransform(root_, glm::mat4(1.0f), false);
	}

	void SceneGraph::syncToRenderer()
	{
		updateWorldTransforms();
		for (size_t i = 0; i < nodes_.size(); ++i)
		{
			Node& node = nodes_[i];
			if (!node.alive || !node.rendererDirty)
			{
				continue;
			}

			if (node.objectIndex)
			{
				const pObject& object = app_.getObject(*node.objectIndex);
				app_.setObjectModelMatrixOverride(*node.objectIndex,
				                                  node.worldTransform * objectLocalMatrix(object));
			}
			if (node.riggedObjectIndex)
			{
				app_.setRiggedObjectTransformMatrixOverride(*node.riggedObjectIndex, node.worldTransform);
			}
			if (node.lightIndex)
			{
				app_.setLightTransformMatrixOverride(*node.lightIndex, node.worldTransform);
			}

			node.rendererDirty = false;
		}
	}

	size_t SceneGraph::attachObject(SceneNodeHandle node, const pObject& object)
	{
		Node& record = getNode(node);
		detachObject(node, true);

		app_.addObject(object);
		const size_t objectIndex = app_.getObjectCount() - 1;
		record.objectIndex = objectIndex;
		record.rendererDirty = true;
		syncToRenderer();
		return objectIndex;
	}

	size_t SceneGraph::attachObject(SceneNodeHandle node,
	                                ShapeType type,
	                                const glm::vec3& position,
	                                const glm::vec3& size,
	                                const glm::vec4& color,
	                                const glm::quat& rotation,
	                                const std::string& name,
	                                float mass)
	{
		pObject object(type, position, size, color, rotation, name, mass);
		return attachObject(node, object);
	}

	size_t SceneGraph::attachRiggedObject(SceneNodeHandle node,
	                                      const std::shared_ptr<RiggedObject>& riggedObject)
	{
		if (!riggedObject)
		{
			throw std::invalid_argument("attachRiggedObject: rigged object pointer is null");
		}

		Node& record = getNode(node);
		detachRiggedObject(node, true);

		const size_t riggedObjectIndex = app_.addRiggedObject(riggedObject);
		record.riggedObjectIndex = riggedObjectIndex;
		record.rendererDirty = true;
		syncToRenderer();
		return riggedObjectIndex;
	}

	size_t SceneGraph::attachLight(SceneNodeHandle node, const LightSource& light)
	{
		Node& record = getNode(node);
		detachLight(node, true);

		const size_t lightIndex = app_.addLight(light);
		record.lightIndex = lightIndex;
		record.rendererDirty = true;
		syncToRenderer();
		return lightIndex;
	}

	std::optional<size_t> SceneGraph::getAttachedObjectIndex(SceneNodeHandle node) const
	{
		return getNode(node).objectIndex;
	}

	std::optional<size_t> SceneGraph::getAttachedRiggedObjectIndex(SceneNodeHandle node) const
	{
		return getNode(node).riggedObjectIndex;
	}

	std::optional<size_t> SceneGraph::getAttachedLightIndex(SceneNodeHandle node) const
	{
		return getNode(node).lightIndex;
	}

	void SceneGraph::detachObject(SceneNodeHandle node, bool removeFromRenderer)
	{
		Node& record = getNode(node);
		if (!record.objectIndex)
		{
			return;
		}

		const size_t objectIndex = *record.objectIndex;
		record.objectIndex.reset();
		if (removeFromRenderer)
		{
			app_.removeObject(objectIndex);
		}
		else
		{
			app_.clearObjectModelMatrixOverride(objectIndex);
		}
	}

	void SceneGraph::detachRiggedObject(SceneNodeHandle node, bool removeFromRenderer)
	{
		Node& record = getNode(node);
		if (!record.riggedObjectIndex)
		{
			return;
		}

		const size_t riggedObjectIndex = *record.riggedObjectIndex;
		record.riggedObjectIndex.reset();
		if (removeFromRenderer)
		{
			app_.removeRiggedObject(riggedObjectIndex);
		}
		else
		{
			app_.clearRiggedObjectTransformMatrixOverride(riggedObjectIndex);
		}
	}

	void SceneGraph::detachLight(SceneNodeHandle node, bool removeFromRenderer)
	{
		Node& record = getNode(node);
		if (!record.lightIndex)
		{
			return;
		}

		const size_t lightIndex = *record.lightIndex;
		record.lightIndex.reset();
		if (removeFromRenderer)
		{
			app_.removeLight(lightIndex);
		}
		else
		{
			app_.clearLightTransformMatrixOverride(lightIndex);
		}
	}

	SceneGraph::Node& SceneGraph::getNode(SceneNodeHandle node)
	{
		if (!contains(node))
		{
			throw std::out_of_range("SceneGraph node handle is invalid or stale");
		}
		return nodes_[node.index];
	}

	const SceneGraph::Node& SceneGraph::getNode(SceneNodeHandle node) const
	{
		if (!contains(node))
		{
			throw std::out_of_range("SceneGraph node handle is invalid or stale");
		}
		return nodes_[node.index];
	}

	SceneNodeHandle SceneGraph::allocateNode()
	{
		if (!freeList_.empty())
		{
			const uint32_t index = freeList_.back();
			freeList_.pop_back();
			Node& node = nodes_[index];
			const uint32_t generation = node.generation;
			node = Node{};
			node.generation = generation;
			node.alive = true;
			return SceneNodeHandle{index, node.generation};
		}

		Node node;
		node.alive = true;
		nodes_.push_back(std::move(node));
		const uint32_t index = static_cast<uint32_t>(nodes_.size() - 1);
		return SceneNodeHandle{index, nodes_[index].generation};
	}

	SceneNodeHandle SceneGraph::resolveParent(SceneNodeHandle parent) const
	{
		if (!parent.isValid())
		{
			return root_;
		}
		if (!contains(parent))
		{
			throw std::out_of_range("SceneGraph parent handle is invalid or stale");
		}
		return parent;
	}

	void SceneGraph::detachFromParent(SceneNodeHandle node)
	{
		Node& record = getNode(node);
		if (record.parent.isValid() && contains(record.parent))
		{
			removeChild(record.parent, node);
		}
		record.parent = SceneNodeHandle{};
	}

	void SceneGraph::addChild(SceneNodeHandle parent, SceneNodeHandle child)
	{
		Node& parentNode = getNode(parent);
		parentNode.children.push_back(child);
	}

	void SceneGraph::removeChild(SceneNodeHandle parent, SceneNodeHandle child)
	{
		Node& parentNode = getNode(parent);
		auto it = std::remove(parentNode.children.begin(), parentNode.children.end(), child);
		parentNode.children.erase(it, parentNode.children.end());
	}

	void SceneGraph::markWorldDirty(SceneNodeHandle node)
	{
		Node& record = getNode(node);
		record.worldDirty = true;
		record.rendererDirty = true;
	}

	void SceneGraph::updateNodeWorldTransform(SceneNodeHandle node,
	                                          const glm::mat4& parentWorld,
	                                          bool parentDirty)
	{
		Node& record = getNode(node);
		const bool shouldUpdate = parentDirty || record.worldDirty;
		if (shouldUpdate)
		{
			record.worldTransform = parentWorld * record.localTransform.matrix();
			record.worldDirty = false;
			record.rendererDirty = true;
		}

		for (SceneNodeHandle child : record.children)
		{
			updateNodeWorldTransform(child, record.worldTransform, shouldUpdate);
		}
	}

	bool SceneGraph::isAncestor(SceneNodeHandle possibleAncestor, SceneNodeHandle node) const
	{
		if (!contains(possibleAncestor) || !contains(node))
		{
			return false;
		}

		SceneNodeHandle current = node;
		while (contains(current))
		{
			if (current == possibleAncestor)
			{
				return true;
			}

			const Node& record = getNode(current);
			if (!record.parent.isValid())
			{
				break;
			}
			current = record.parent;
		}
		return false;
	}

	void SceneGraph::onObjectChanged(size_t index)
	{
		for (Node& node : nodes_)
		{
			if (node.alive && node.objectIndex && *node.objectIndex == index)
			{
				node.rendererDirty = true;
			}
		}
	}

	void SceneGraph::onObjectRemoved(size_t index)
	{
		for (Node& node : nodes_)
		{
			if (!node.alive || !node.objectIndex)
			{
				continue;
			}
			if (*node.objectIndex == index)
			{
				node.objectIndex.reset();
			}
			else if (*node.objectIndex > index)
			{
				--(*node.objectIndex);
			}
		}
	}

	void SceneGraph::onRiggedObjectRemoved(size_t index)
	{
		for (Node& node : nodes_)
		{
			if (!node.alive || !node.riggedObjectIndex)
			{
				continue;
			}
			if (*node.riggedObjectIndex == index)
			{
				node.riggedObjectIndex.reset();
			}
			else if (*node.riggedObjectIndex > index)
			{
				--(*node.riggedObjectIndex);
			}
		}
	}

	void SceneGraph::onLightChanged(size_t index)
	{
		for (Node& node : nodes_)
		{
			if (node.alive && node.lightIndex && *node.lightIndex == index)
			{
				node.rendererDirty = true;
			}
		}
	}

	void SceneGraph::onLightRemoved(size_t index)
	{
		for (Node& node : nodes_)
		{
			if (!node.alive || !node.lightIndex)
			{
				continue;
			}
			if (*node.lightIndex == index)
			{
				node.lightIndex.reset();
			}
			else if (*node.lightIndex > index)
			{
				--(*node.lightIndex);
			}
		}
	}
}
