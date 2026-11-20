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

#include <iosfwd>
#include <stdexcept>
#include <string>
#include <utility>

// Defined from 3.5: lightGraphics::GraphicsUnavailableError.
#define LVG_HAS_GRAPHICS_UNAVAILABLE_ERROR 1

namespace lightGraphics
{
	// Process-wide console output toggle shared by the library and any consumers.
	// Off by default. LVG_CONSOLE_OUTPUT=1 forces it on, =0 forces it off.
	void setConsoleOutputEnabled(bool enabled);
	bool getConsoleOutputEnabled();

	std::ostream& consoleInfoStream();
	std::ostream& consoleErrorStream();

	// Thrown when the window or Vulkan could not be started: no Vulkan loader or driver, no suitable GPU, no
	// display, or a surface that cannot be created. what() is a sentence meant for the end user (what is wrong and
	// what to try), and technicalDetail() keeps the underlying error for logs and bug reports. An application can
	// catch it to show a message and exit cleanly, or to fall back to a non-graphical mode.
	class GraphicsUnavailableError : public std::runtime_error
	{
	public:
		GraphicsUnavailableError(std::string const& userMessage, std::string detail)
			: std::runtime_error(userMessage), technicalDetail_(std::move(detail))
		{
		}
		[[nodiscard]] std::string const& technicalDetail() const noexcept { return technicalDetail_; }

	private:
		std::string technicalDetail_;
	};
}
