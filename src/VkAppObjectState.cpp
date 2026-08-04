// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2026 Dr. Nathanael John Inkson

#include "VkApp.h"
#include "SceneGraph.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace lightGraphics
{
	namespace
	{
		std::string makeObjectIndexMessage(const char* operation, size_t index, size_t size)
		{
			std::ostringstream message;
			message << operation << " index " << index << " is out of range for "
			        << size << " objects";
			return message.str();
		}
	}

	std::uint32_t VkApp::allocateHandleSlot(std::vector<detail::HandleSlot>& slots,
	                                        std::vector<std::uint32_t>& freeSlots,
	                                        std::uint32_t currentIndex)
	{
		std::uint32_t slot;
		if (freeSlots.empty())
		{
			slot = static_cast<std::uint32_t>(slots.size());
			slots.emplace_back();
		}
		else
		{
			slot = freeSlots.back();
			freeSlots.pop_back();
		}
		slots[slot].alive = true;
		slots[slot].currentIndex = currentIndex;
		return slot;
	}

	void VkApp::releaseHandleSlot(std::vector<detail::HandleSlot>& slots,
	                              std::vector<std::uint32_t>& freeSlots,
	                              std::uint32_t slot)
	{
		slots[slot].alive = false;
		++slots[slot].generation;
		freeSlots.push_back(slot);
	}

	void VkApp::reindexHandleSlotsFrom(std::vector<detail::HandleSlot>& slots,
	                                   const std::vector<std::uint32_t>& slotForIndex,
	                                   std::size_t startIndex)
	{
		for (std::size_t i = startIndex; i < slotForIndex.size(); ++i)
		{
			slots[slotForIndex[i]].currentIndex = static_cast<std::uint32_t>(i);
		}
	}

	bool VkApp::isHandleSlotValid(const std::vector<detail::HandleSlot>& slots,
	                              std::uint32_t index,
	                              std::uint32_t generation) noexcept
	{
		return index < slots.size() && slots[index].alive && slots[index].generation == generation;
	}

	std::size_t VkApp::resolveHandleSlot(const std::vector<detail::HandleSlot>& slots,
	                                     std::uint32_t index,
	                                     std::uint32_t generation,
	                                     const char* what)
	{
		if (!isHandleSlotValid(slots, index, generation))
		{
			throw std::out_of_range(std::string(what) + " handle is invalid or stale");
		}
		return slots[index].currentIndex;
	}

	void VkApp::addDebugOverlayNameSubstring(const std::string& substring)
	{
		debugOverlayNameSubstrings_.insert(substring);
	}

	bool VkApp::isDebugOverlayObjectName(const std::string& name) const
	{
		for (const std::string& substring : debugOverlayNameSubstrings_)
		{
			if (name.find(substring) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}

	ObjectHandle VkApp::addObject(lightGraphics::pObject *newObject)
	{
		assertOwnerThread("addObject");
		if (!newObject)
		{
			throw std::invalid_argument("addObject: object pointer is null");
		}

		_objects_.push_back(*newObject);

		// Initialize dirty tracking for new object
		dirtyObjects_.push_back(true);
		objectModelMatrixOverrides_.push_back(std::nullopt);
		instanceDataDirty_ = true;

		const std::uint32_t newIndex = static_cast<std::uint32_t>(_objects_.size() - 1);
		const std::uint32_t slot = allocateHandleSlot(objectSlots_, freeObjectSlots_, newIndex);
		objectSlotForIndex_.push_back(slot);

		if (sceneFinalized_)
		{
			updateInstanceData(); // Update rendering data only if scene is finalized
		}

		return ObjectHandle{slot, objectSlots_[slot].generation};
	}

	ObjectHandle VkApp::addObject(const lightGraphics::pObject& obj)
	{
		assertOwnerThread("addObject");
		_objects_.push_back(obj);

		// Initialize dirty tracking for new object
		dirtyObjects_.push_back(true);
		objectModelMatrixOverrides_.push_back(std::nullopt);
		instanceDataDirty_ = true;

		const std::uint32_t newIndex = static_cast<std::uint32_t>(_objects_.size() - 1);
		const std::uint32_t slot = allocateHandleSlot(objectSlots_, freeObjectSlots_, newIndex);
		objectSlotForIndex_.push_back(slot);

		if (sceneFinalized_)
		{
			updateInstanceData();
		}

		return ObjectHandle{slot, objectSlots_[slot].generation};
	}

	ObjectHandle VkApp::addObject(lightGraphics::ShapeType type, const glm::vec3& position,
						const glm::vec3& size, const glm::vec4& color,
						const glm::quat& rotation, const std::string& name, float mass)
	{
		assertOwnerThread("addObject");
		_objects_.emplace_back(type, position, size, color, rotation, name, mass);

		// Initialize dirty tracking for new object
		dirtyObjects_.push_back(true);
		objectModelMatrixOverrides_.push_back(std::nullopt);
		instanceDataDirty_ = true;

		const std::uint32_t newIndex = static_cast<std::uint32_t>(_objects_.size() - 1);
		const std::uint32_t slot = allocateHandleSlot(objectSlots_, freeObjectSlots_, newIndex);
		objectSlotForIndex_.push_back(slot);

		if (sceneFinalized_)
		{
			updateInstanceData();
		}

		return ObjectHandle{slot, objectSlots_[slot].generation};
	}

	ObjectHandle VkApp::addObject(const lightGraphics::ObjectDescription& description)
	{
		lightGraphics::pObject obj(description.type,
		                           description.position,
		                           description.size,
		                           description.color,
		                           description.rotation,
		                           description.name,
		                           description.mass);
		if (description.immovable)
		{
			obj.setImmovable();
		}
		if (!description.texturePath.empty())
		{
			obj.setTexturePath(description.texturePath);
		}
		obj.setTextureTiling(description.textureTiling);
		return addObject(obj);
	}

	ObjectDescription VkApp::getObjectDescription(size_t index) const
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("getObjectDescription", index, _objects_.size()));
		}

		const lightGraphics::pObject& obj = _objects_[index];
		ObjectDescription description;
		description.type = obj.getType();
		description.position = obj.getPosition();
		description.size = obj.getSize();
		description.color = obj.getColour();
		description.rotation = obj.getRotation();
		description.name = obj.getName();
		description.mass = obj.getMass();
		description.immovable = obj.isImmovable();
		description.texturePath = obj.getTexturePath();
		description.textureTiling = obj.getTextureTiling();
		return description;
	}

	glm::mat4 VkApp::getObjectModelMatrix(size_t index) const
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("getObjectModelMatrix", index, _objects_.size()));
		}

		if (index < objectModelMatrixOverrides_.size() && objectModelMatrixOverrides_[index])
		{
			return *objectModelMatrixOverrides_[index];
		}

		const auto& obj = _objects_[index];
		const glm::mat4 translation = glm::translate(glm::mat4(1.0f), obj.getPosition());
		const glm::mat4 rotation = glm::mat4_cast(obj.getRotation());
		const glm::mat4 scale = glm::scale(glm::mat4(1.0f), obj.getSize());
		return translation * rotation * scale;
	}

	void VkApp::setObjectModelMatrixOverride(size_t index, const glm::mat4& model)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectModelMatrixOverride", index, _objects_.size()));
		}

		if (objectModelMatrixOverrides_.size() < _objects_.size())
		{
			objectModelMatrixOverrides_.resize(_objects_.size());
		}
		objectModelMatrixOverrides_[index] = model;

		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
		else
		{
			instanceDataDirty_ = true;
		}
	}

	void VkApp::clearObjectModelMatrixOverride(size_t index)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("clearObjectModelMatrixOverride", index, _objects_.size()));
		}

		clearObjectModelMatrixOverrideInternal(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
		else
		{
			instanceDataDirty_ = true;
		}
	}

	SceneGraph& VkApp::sceneGraph()
	{
		return *sceneGraph_;
	}

	const SceneGraph& VkApp::sceneGraph() const
	{
		return *sceneGraph_;
	}

	void VkApp::setObjectPosition(size_t index, const glm::vec3& position)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectPosition", index, _objects_.size()));
		}

		_objects_[index].setPosition(position);
		clearObjectModelMatrixOverrideInternal(index);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::setObjectScale(size_t index, const glm::vec3& scale)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectScale", index, _objects_.size()));
		}

		_objects_[index].setSize(scale);
		clearObjectModelMatrixOverrideInternal(index);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::setObjectRotation(size_t index, const glm::quat& rotation)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectRotation", index, _objects_.size()));
		}

		_objects_[index].setRotation(rotation);
		clearObjectModelMatrixOverrideInternal(index);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::setObjectColor(size_t index, const glm::vec4& color)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectColor", index, _objects_.size()));
		}

		_objects_[index].setColour(color);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::setObjectTexturePath(size_t index, const std::string& path)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectTexturePath", index, _objects_.size()));
		}

		_objects_[index].setTexturePath(path);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::setObjectTextureTiling(size_t index, const glm::vec2& tiling)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("setObjectTextureTiling", index, _objects_.size()));
		}

		_objects_[index].setTextureTiling(tiling);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::updateObjectProperties(size_t index, const glm::vec3& position,
									const glm::vec3& scale, const glm::quat& rotation)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("updateObjectProperties", index, _objects_.size()));
		}

		_objects_[index].setPosition(position);
		_objects_[index].setSize(scale);
		_objects_[index].setRotation(rotation);
		clearObjectModelMatrixOverrideInternal(index);
		sceneGraph_->onObjectChanged(index);

		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::removeObject(size_t index)
	{
		assertOwnerThread("removeObject");
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("removeObject", index, _objects_.size()));
		}

		releaseHandleSlot(objectSlots_, freeObjectSlots_, objectSlotForIndex_[index]);
		objectSlotForIndex_.erase(objectSlotForIndex_.begin() + static_cast<std::ptrdiff_t>(index));
		reindexHandleSlotsFrom(objectSlots_, objectSlotForIndex_, index);

		_objects_.erase(_objects_.begin() + static_cast<std::ptrdiff_t>(index));
		if (index < dirtyObjects_.size())
		{
			dirtyObjects_.erase(dirtyObjects_.begin() + static_cast<std::ptrdiff_t>(index));
		}
		if (index < objectModelMatrixOverrides_.size())
		{
			objectModelMatrixOverrides_.erase(objectModelMatrixOverrides_.begin() + static_cast<std::ptrdiff_t>(index));
		}
		if (index < instanceDataCache_.size())
		{
			instanceDataCache_.erase(instanceDataCache_.begin() + static_cast<std::ptrdiff_t>(index));
		}
		sceneGraph_->onObjectRemoved(index);
		instanceDataDirty_ = true;
		if (sceneFinalized_)
		{
			updateInstanceData();
		}
	}

	void VkApp::removeObject(ObjectHandle handle)
	{
		removeObject(resolveObjectHandle(handle));
	}

	bool VkApp::isObjectHandleValid(ObjectHandle handle) const noexcept
	{
		return isHandleSlotValid(objectSlots_, handle.index, handle.generation);
	}

	size_t VkApp::resolveObjectHandle(ObjectHandle handle) const
	{
		return resolveHandleSlot(objectSlots_, handle.index, handle.generation, "Object");
	}

	ObjectHandle VkApp::objectHandleAt(size_t index) const
	{
		if (index >= objectSlotForIndex_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("objectHandleAt", index, objectSlotForIndex_.size()));
		}
		const std::uint32_t slot = objectSlotForIndex_[index];
		return ObjectHandle{slot, objectSlots_[slot].generation};
	}

	ObjectHandle VkApp::addHexahedral(const glm::vec3& position, const glm::vec3& size,
							const glm::vec4& color,
							const glm::quat& rotation,
							const std::string& name,
							float mass)
	{
		return addObject(lightGraphics::ShapeType::HEX, position, size, color, rotation, name, mass);
	}

	void VkApp::clearObjects()
	{
		assertOwnerThread("clearObjects");
		const size_t removedCount = _objects_.size();
		for (std::uint32_t slot : objectSlotForIndex_)
		{
			releaseHandleSlot(objectSlots_, freeObjectSlots_, slot);
		}
		objectSlotForIndex_.clear();
		_objects_.clear();
		dirtyObjects_.clear();
		objectModelMatrixOverrides_.clear();
		instanceDataCache_.clear();
		for (size_t i = 0; i < removedCount; ++i)
		{
			sceneGraph_->onObjectRemoved(0);
		}
		instanceDataDirty_ = true;
		if (sceneFinalized_)
		{
			updateInstanceData();
		}
	}

	void VkApp::updateObject(size_t index, const lightGraphics::pObject& obj)
	{
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("updateObject", index, _objects_.size()));
		}

		_objects_[index] = obj;
		clearObjectModelMatrixOverrideInternal(index);
		sceneGraph_->onObjectChanged(index);
		if (sceneFinalized_)
		{
			markObjectDirty(index);
		}
	}

	void VkApp::updateObject(size_t index, const lightGraphics::ObjectDescription& description)
	{
		lightGraphics::pObject obj(description.type,
		                           description.position,
		                           description.size,
		                           description.color,
		                           description.rotation,
		                           description.name,
		                           description.mass);
		if (description.immovable)
		{
			obj.setImmovable();
		}
		if (!description.texturePath.empty())
		{
			obj.setTexturePath(description.texturePath);
		}
		obj.setTextureTiling(description.textureTiling);
		updateObject(index, obj);
	}

	void VkApp::updateObjectPositions(const std::vector<std::pair<size_t, glm::vec3>>& updates)
	{
		for (const auto& update : updates)
		{
			if (update.first >= _objects_.size())
			{
				throw std::out_of_range(makeObjectIndexMessage("updateObjectPositions",
				                                               update.first,
				                                               _objects_.size()));
			}
		}

		for (const auto& update : updates)
		{
			_objects_[update.first].setPosition(update.second);
			clearObjectModelMatrixOverrideInternal(update.first);
			sceneGraph_->onObjectChanged(update.first);
			markObjectDirty(update.first);
		}
	}

	void VkApp::updateObjectProperties(const std::vector<std::tuple<size_t, glm::vec3, glm::vec3, glm::quat>>& updates)
	{
		for (const auto& update : updates)
		{
			size_t index = std::get<0>(update);
			if (index >= _objects_.size())
			{
				throw std::out_of_range(makeObjectIndexMessage("updateObjectProperties", index, _objects_.size()));
			}
		}

		for (const auto& update : updates)
		{
			size_t index = std::get<0>(update);
			_objects_[index].setPosition(std::get<1>(update));
			_objects_[index].setSize(std::get<2>(update));
			_objects_[index].setRotation(std::get<3>(update));
			clearObjectModelMatrixOverrideInternal(index);
			sceneGraph_->onObjectChanged(index);
			markObjectDirty(index);
		}
	}
}
