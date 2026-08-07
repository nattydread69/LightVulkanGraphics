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

	std::vector<std::uint32_t> charQueue;  // Unicode codepoints from text input
	std::vector<KeyEvent>      keyQueue;   // ordered key transitions

	float deltaTime {0};
	Vec2  displaySize {0, 0};      // logical size
	float contentScale {1.0f};
};

} // namespace lightGraphics::ui
