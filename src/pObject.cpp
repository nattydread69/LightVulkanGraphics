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

//
// Copyright(c) 2025 onwards Dr. Nathanael Inkson
//

#include "pObject.h"
#include "LightVulkanGraphicsLogging.h"

#include <glm/gtc/type_ptr.hpp>

#include <iostream>

namespace lightGraphics
{
	pObject::pObject(ShapeType type,
						glm::vec3   const &center,
						glm::vec3   const &size,
						glm::vec4   const &colour,
						glm::quat   const &rotation,
						std::string const &name,
						float       const  mass)
	: _type(type), _center(center), _size(size), _colour(colour)
	, _rotation(rotation),  _name(name)
	, _mass(mass), _immovable(false)
	{
		initializeRotationMatrix();
		// printRotationMatrix();
	}

	void
	pObject::
	initializeRotationMatrix()
	{
		//std::cout << "Rotation matrix of pObject: " << _name << ".\n";
		for (unsigned i = 0; i < 4; i++)
		{
			//std::cout << "i = " << i << std::endl;
			for (unsigned j = 0; j < 4; j++)
			{
				unsigned const index = j + 4*i;
				if(i == j)
					_R[index] = 1.0;
				else
					_R[index] = 0.0;

				//std::cout << "j = " << j << std::endl;
				//std::cout << "index = " << index << std::endl;
			}
		}
	}

	void
	pObject::
	printRotationMatrix() const
	{
		if (!lightGraphics::getConsoleOutputEnabled())
		{
			return;
		}

		auto& out = lightGraphics::consoleInfoStream();
		out << "Rotation matrix of pObject: " << _name << ".\n";
		for (unsigned i = 0; i < 16; i++)
		{
			out << _R[i] << ", ";
		}
		out << std::endl;
	}

	glm::mat4
	pObject::
	getRotationMatrix() const
	{
		glm::mat4 R_OGL = glm::make_mat4(_R);
		/*
		for (unsigned i = 0; i < 16; i++)
		{
			R_OGL[i] =  _R[i];
		}
		*/
		return R_OGL;
	}
}
