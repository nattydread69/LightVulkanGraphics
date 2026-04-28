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

	void VkApp::addObject(lightGraphics::pObject *newObject)
	{
		if (!newObject)
		{
			throw std::invalid_argument("addObject: object pointer is null");
		}

		_objects_.push_back(*newObject);

		// Initialize dirty tracking for new object
		dirtyObjects_.push_back(true);
		objectModelMatrixOverrides_.push_back(std::nullopt);
		instanceDataDirty_ = true;

		if (sceneFinalized_)
		{
			updateInstanceData(); // Update rendering data only if scene is finalized
		}
	}

	void VkApp::addObject(const lightGraphics::pObject& obj)
	{
		_objects_.push_back(obj);

		// Initialize dirty tracking for new object
		dirtyObjects_.push_back(true);
		objectModelMatrixOverrides_.push_back(std::nullopt);
		instanceDataDirty_ = true;

		if (sceneFinalized_)
		{
			updateInstanceData();
		}
	}

	void VkApp::addObject(lightGraphics::ShapeType type, const glm::vec3& position,
						const glm::vec3& size, const glm::vec4& color,
						const glm::quat& rotation, const std::string& name, float mass)
	{
		_objects_.emplace_back(type, position, size, color, rotation, name, mass);

		// Initialize dirty tracking for new object
		dirtyObjects_.push_back(true);
		objectModelMatrixOverrides_.push_back(std::nullopt);
		instanceDataDirty_ = true;

		if (sceneFinalized_)
		{
			updateInstanceData();
		}
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
		if (index >= _objects_.size())
		{
			throw std::out_of_range(makeObjectIndexMessage("removeObject", index, _objects_.size()));
		}

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

	void VkApp::addHexahedral(const glm::vec3& position, const glm::vec3& size,
							const glm::vec4& color,
							const glm::quat& rotation,
							const std::string& name,
							float mass)
	{
		addObject(lightGraphics::ShapeType::HEX, position, size, color, rotation, name, mass);
	}

	void VkApp::clearObjects()
	{
		const size_t removedCount = _objects_.size();
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
