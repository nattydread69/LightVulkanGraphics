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

// Layer 1: our own mirror of the key codes and modifier bitmask the GUI needs. Nothing
// under include/lightVulkanGraphics/ui/ or src/ui/ may see a GLFW header (UiRenderer.*
// and UiPlatformGlfw.* are the sole exceptions); translation from GLFW's GLFW_KEY_*
// constants happens only in src/ui/UiPlatformGlfw.cpp. See docs/gui/04.

namespace lightGraphics::ui {

namespace Mod {
	enum : int {
		Shift = 1,
		Ctrl  = 2,
		Alt   = 4,
		Super = 8,
	};
}

namespace Key {
	enum : int {
		Unknown = -1,
		Backspace, Delete, Tab, Enter, Escape, Space,
		Left, Right, Up, Down, Home, End, PageUp, PageDown,
		A, C, V, X, Z, Y,          // for the standard editing shortcuts
		Count
	};
}

// Cursor shapes the platform layer can be asked to set. GLFW provides a standard
// cursor for each of these (see UiPlatformGlfw.cpp for the mapping).
enum class CursorShape {
	Arrow,
	TextInput,   // over a text box
	ResizeEW,
	ResizeNS,
	ResizeNWSE,  // panel resize grip
	Hand,
};

} // namespace lightGraphics::ui
