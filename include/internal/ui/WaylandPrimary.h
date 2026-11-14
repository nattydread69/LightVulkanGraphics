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

// The Wayland PRIMARY selection ("select to copy, middle-click to paste"), which GLFW does not
// expose: its Wayland backend implements only the regular clipboard (wl_data_device).
//
// This speaks the zwp_primary_selection_unstable_v1 protocol on GLFW's own wl_display
// (glfwGetWaylandDisplay()). It cannot open a connection of its own: a compositor delivers input
// serials and selection offers only to the client that owns the focused surface, and that is
// GLFW's connection. So it binds its own wl_seat, wl_keyboard, wl_pointer and primary-selection
// device on a private event queue of that connection, and dispatches that queue from a small
// worker thread. Nothing is shared with GLFW's own objects or queue.
//
// libwayland-client is loaded with dlopen() (GLFW has already loaded it) and the protocol
// bindings are written out by hand, so there is no build or link dependency on Wayland
// headers or libraries. If the compositor lacks the protocol, create() returns null and the GUI
// keeps its in-application primary text.
//
// Set LVG_DEBUG_PRIMARY=1 to log what this does to stderr.

#include <memory>
#include <string>
#include <string_view>

namespace lightGraphics::ui {

class WaylandPrimary {
public:
	// `wlDisplay` is the wl_display* GLFW is using. Null when libwayland-client cannot be
	// loaded or the compositor offers no seat or no primary-selection manager.
	static std::shared_ptr<WaylandPrimary> create(void* wlDisplay);
	virtual ~WaylandPrimary() = default;

	// Take ownership of PRIMARY with `text` (what other programs get on middle-click). Empty
	// text releases it. Returns at once; the worker thread talks to the compositor.
	virtual void setText(std::string_view text) = 0;
	// The current PRIMARY text: ours if we own it, otherwise read from the owning program (waits
	// at most about a second). Empty when nothing is selected, or when this window has not had
	// keyboard focus, because a compositor only tells the focused client what is selected.
	virtual std::string getText() = 0;
	// Stops the worker and releases every Wayland object. Must be called while the display is
	// still connected (before glfwTerminate()); afterwards every call is a harmless no-op, so
	// copies of the shared_ptr that outlive the display are safe.
	virtual void shutdown() = 0;
};

} // namespace lightGraphics::ui
