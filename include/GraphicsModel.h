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

#pragma once

#include "lightVulkanGraphics.h"

#include <glm/glm.hpp>

#include <vector>

namespace lightGraphics
{

	/** This class is designed to hold different "models" that can be switched
	between	which acts as a container for sets of graphical objects
	 */
	class GraphicsModel
	{
	public:
		explicit GraphicsModel(lightGraphics::lightVulkanGraphics &app,
								std::string name);
		virtual ~GraphicsModel();

		virtual std::string getName() { return name_; }
		lightGraphics::lightVulkanGraphics& getGraphicsApp() const { return app_; }
	protected:
		lightGraphics::lightVulkanGraphics &app_;
	private:
		std::string name_;
	};
}

