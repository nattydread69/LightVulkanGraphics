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
//
// Copyright(c) 2019 onwards Dr. Nathanael Inkson
//

#ifndef LIGHT_VULKAN_GRAPHICS_POBJECT_H
#define LIGHT_VULKAN_GRAPHICS_POBJECT_H

#include "pHeaders.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>

// Define pReal if not already defined
#ifndef pReal
typedef float pReal;
#endif // LIGHT_VULKAN_GRAPHICS_POBJECT_H

namespace lightGraphics
{
	enum class ShapeType : int
	{
		SPHERE = 0,
		CUBE = 1,
		CONE = 2,
		CYLINDER = 3,
		CAPSULE = 4,
		ARROW = 5,
		LINE = 6,
		HEX = 7,
		MESH = 8,
		HUMAN = 9
	};

	/// A plain-data description of an object, independent of the legacy
	/// pObject class. Preferred for new code: VkApp::addObject,
	/// VkApp::updateObject, and VkApp::getObjectDescription all accept or
	/// return this instead of requiring a pObject.
	struct ObjectDescription
	{
		ShapeType type = ShapeType::SPHERE;
		glm::vec3 position{0.0f};
		glm::vec3 size{1.0f};
		glm::vec4 color{1.0f};
		glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
		std::string name;
		float mass = 1.0f;
		bool immovable = false;
		std::string texturePath;
	};

	/** Object class to hold data for objects common to both graphics and physics geometries
	_type denotes the geometry

	*/
	class pObject
	{
	public:
		pObject(ShapeType const type,
				glm::vec3   const &center,
				glm::vec3   const &size,
				glm::vec4   const &colour,
				glm::quat   const &rotation,
				std::string const &name,
				float       const mass);
		virtual ~pObject() {}
		ShapeType getType() const { return _type; }
		glm::vec3 getPosition() const { return _center; }
		glm::vec3 getSize()     const { return _size; }
		glm::vec4 getColour()   const { return _colour; }
		pReal getMass() const { return _mass; }
		std::string getName() const { return _name; }
		std::string getTexturePath() const { return _texturePath; }
		glm::quat getRotation() const { return _rotation; }
		glm::mat4 getRotationMatrix() const;
		bool isImmovable() const { return _immovable; }

		void setPosition(glm::vec3 const &pos)  { _center = pos; }
		void setSize(glm::vec3 const &size)     { _size = size; }
		void setColour(glm::vec4 const &colour) { _colour = colour; }
		void setMass(pReal const mass) { _mass = mass; }
		void setImmovable() { _immovable = true; }
		void setTexturePath(std::string const &s) { _texturePath = s; }
		void setRotation(glm::quat const &rotation) { _rotation = rotation; }
		void initializeRotationMatrix();
		void printRotationMatrix() const;

		ShapeType _type;
	protected:
	private:
		/// Rotation matrix
		pReal _R[16];
		glm::vec3 _center;
		glm::vec3 _size;
		glm::vec4 _colour;
		glm::quat _rotation;

		std::string _name;
		pReal _mass;
		bool _immovable;
		std::string _texturePath;
	};
}

#endif
