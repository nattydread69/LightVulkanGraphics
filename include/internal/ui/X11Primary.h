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

// The X11 PRIMARY selection ("select to copy, middle-click to paste"), which GLFW does not expose:
// GLFW's clipboard calls only ever touch CLIPBOARD, and its event loop would answer a PRIMARY
// request with the clipboard's contents. So this opens its own X connection, owns a hidden 1x1
// window, and serves/requests PRIMARY on a small worker thread. libX11 is loaded with dlopen(), so
// there is no build or link dependency on it; if it (or an X server) is missing, create() returns
// null and the GUI just keeps its in-application primary text.
//
// Only meaningful when GLFW is running on X11 (including XWayland). Text longer than the X
// property limit (INCR transfers) is not served.

#include <memory>
#include <string>
#include <string_view>

namespace lightGraphics::ui {

class X11Primary {
public:
	// Null when libX11 cannot be loaded or no display can be opened.
	static std::shared_ptr<X11Primary> create();
	virtual ~X11Primary() = default;

	// Take ownership of PRIMARY with `text` (what other programs get on middle-click).
	virtual void setText(std::string_view text) = 0;
	// The current PRIMARY text: ours if we own it, otherwise fetched from the owning program
	// (waits at most ~150 ms). Empty when nothing is selected anywhere.
	virtual std::string getText() = 0;
};

} // namespace lightGraphics::ui
