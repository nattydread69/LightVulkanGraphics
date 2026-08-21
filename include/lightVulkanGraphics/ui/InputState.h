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

// Layer 1: the plain-data input snapshot the GUI consumes. The GUI never calls GLFW
// directly -- this is what makes it testable and what would let another windowing
// library be swapped in later without touching layers 1-3. See docs/gui/04.

#include "Types.h"

#include <cstdint>
#include <vector>

namespace lightGraphics::ui {

enum class MouseButton { Left = 0, Right = 1, Middle = 2, Count = 3 };

struct KeyEvent {
	int  key = 0;         // one of Key:: (KeyCodes.h) -- never a raw GLFW code
	int  mods = 0;         // bitmask of Mod:: (KeyCodes.h)
	bool pressed = false;  // false = released
	bool repeat = false;
};

struct InputState {
	Vec2  mousePos       {0, 0};   // logical pixels, origin top-left
	Vec2  mousePosPrev   {0, 0};
	Vec2  mouseDelta     {0, 0};
	Vec2  mouseDownPos[3] {};      // where each button went down -- for drag thresholds

	bool  mouseDown    [3] {};     // level: currently held
	bool  mousePressed [3] {};     // edge: went down this frame
	bool  mouseReleased[3] {};     // edge: came up this frame
	// seconds held; -1.0f when up. Defaults to "up" so the first beginFrame() after a
	// press computes 0.0f rather than mistaking the zero-initialised default for an
	// already-elapsed hold.
	float mouseDownDuration[3] { -1.0f, -1.0f, -1.0f };

	float wheelDelta {0};          // vertical, positive = away from user

	// Level: bitmask of Mod:: flags (KeyCodes.h) currently held. docs/gui/04's original
	// InputState had no level view of modifiers, only the per-event KeyEvent::mods
	// snapshot -- fine for Tab/Escape, but a slider drag needs to know "is Shift down right
	// now" across many frames of an ongoing slider drag, which an edge-only queue can't
	// answer. Maintained from the `mods` argument every injectKey() call already carries
	// (GLFW reports the full current modifier set on every key event, including the
	// modifier key's own press/release), so it needs no new platform plumbing beyond
	// UiPlatformGlfw persisting it instead of discarding it. See docs/gui/04, "InputState".
	int modsDown {0};

	std::vector<std::uint32_t> charQueue;  // Unicode codepoints from text input
	std::vector<KeyEvent>      keyQueue;   // ordered key transitions

	float deltaTime {0};
	Vec2  displaySize {0, 0};      // logical size
	float contentScale {1.0f};
};

} // namespace lightGraphics::ui
