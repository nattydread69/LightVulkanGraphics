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

#include "UiPlatformGlfw.h"

#include <GLFW/glfw3.h>

#include <unordered_map>

namespace lightGraphics::ui {

namespace {

// Every window we have installed callbacks on gets an entry here: the target that
// inject* calls land on, plus whatever callback GLFW had installed before us so it can
// still be chained to unconditionally (docs/gui/04, "GLFW plumbing"). We deliberately
// do not touch glfwSetWindowUserPointer -- VkApp already owns that slot for its own
// trampolines, and clobbering it would break them.
struct CallbackChain {
	UiPlatformGlfw*    target          = nullptr;
	GLFWmousebuttonfun prevMouseButton = nullptr;
	GLFWcursorposfun   prevCursorPos   = nullptr;
	GLFWscrollfun      prevScroll      = nullptr;
	GLFWkeyfun         prevKey         = nullptr;
	GLFWcharfun        prevChar        = nullptr;
};

std::unordered_map<GLFWwindow*, CallbackChain>& callbackRegistry() {
	static std::unordered_map<GLFWwindow*, CallbackChain> registry;
	return registry;
}

MouseButton translateMouseButton(int glfwButton) {
	switch (glfwButton) {
		case GLFW_MOUSE_BUTTON_LEFT:   return MouseButton::Left;
		case GLFW_MOUSE_BUTTON_RIGHT:  return MouseButton::Right;
		case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::Middle;
		default:                       return MouseButton::Count; // sentinel: ignored
	}
}

int translateMods(int glfwMods) {
	int mods = 0;
	if (glfwMods & GLFW_MOD_SHIFT)   mods |= Mod::Shift;
	if (glfwMods & GLFW_MOD_CONTROL) mods |= Mod::Ctrl;
	if (glfwMods & GLFW_MOD_ALT)     mods |= Mod::Alt;
	if (glfwMods & GLFW_MOD_SUPER)   mods |= Mod::Super;
	return mods;
}

int translateKey(int glfwKey) {
	switch (glfwKey) {
		case GLFW_KEY_BACKSPACE:  return Key::Backspace;
		case GLFW_KEY_DELETE:     return Key::Delete;
		case GLFW_KEY_TAB:        return Key::Tab;
		case GLFW_KEY_ENTER:      return Key::Enter;
		case GLFW_KEY_KP_ENTER:   return Key::Enter;
		case GLFW_KEY_ESCAPE:     return Key::Escape;
		case GLFW_KEY_SPACE:      return Key::Space;
		case GLFW_KEY_LEFT:       return Key::Left;
		case GLFW_KEY_RIGHT:      return Key::Right;
		case GLFW_KEY_UP:         return Key::Up;
		case GLFW_KEY_DOWN:       return Key::Down;
		case GLFW_KEY_HOME:       return Key::Home;
		case GLFW_KEY_END:        return Key::End;
		case GLFW_KEY_PAGE_UP:    return Key::PageUp;
		case GLFW_KEY_PAGE_DOWN:  return Key::PageDown;
		case GLFW_KEY_A:          return Key::A;
		case GLFW_KEY_C:          return Key::C;
		case GLFW_KEY_V:          return Key::V;
		case GLFW_KEY_X:          return Key::X;
		case GLFW_KEY_Z:          return Key::Z;
		case GLFW_KEY_Y:          return Key::Y;
		default:                  return Key::Unknown;
	}
}

// GLFW 3.3 (pinned by this project) has no diagonal resize cursor; GLFW_RESIZE_NWSE_
// CURSOR only arrived in 3.4. Fall back to the arrow rather than fail to compile --
// revisit when the project's GLFW floor moves to 3.4+.
GLFWcursor* standardCursor(CursorShape shape) {
	static GLFWcursor* cursors[6] = {};
	const int index = static_cast<int>(shape);
	if (!cursors[index]) {
		switch (shape) {
			case CursorShape::Arrow:      cursors[index] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);   break;
			case CursorShape::TextInput:  cursors[index] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);   break;
			case CursorShape::ResizeEW:   cursors[index] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR); break;
			case CursorShape::ResizeNS:   cursors[index] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR); break;
			case CursorShape::ResizeNWSE: cursors[index] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);   break;
			case CursorShape::Hand:       cursors[index] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);    break;
		}
	}
	return cursors[index];
}

void onMouseButtonTrampoline(GLFWwindow* window, int button, int action, int mods) {
	auto it = callbackRegistry().find(window);
	if (it != callbackRegistry().end()) {
		if (it->second.target) {
			it->second.target->injectMouseButton(translateMouseButton(button), action == GLFW_PRESS);
		}
		if (it->second.prevMouseButton) {
			it->second.prevMouseButton(window, button, action, mods);
		}
	}
}

void onCursorPosTrampoline(GLFWwindow* window, double xpos, double ypos) {
	auto it = callbackRegistry().find(window);
	if (it != callbackRegistry().end()) {
		if (it->second.target) {
			it->second.target->injectMousePos({ static_cast<float>(xpos), static_cast<float>(ypos) });
		}
		if (it->second.prevCursorPos) {
			it->second.prevCursorPos(window, xpos, ypos);
		}
	}
}

void onScrollTrampoline(GLFWwindow* window, double xoffset, double yoffset) {
	auto it = callbackRegistry().find(window);
	if (it != callbackRegistry().end()) {
		if (it->second.target) {
			it->second.target->injectScroll(static_cast<float>(yoffset));
		}
		if (it->second.prevScroll) {
			it->second.prevScroll(window, xoffset, yoffset);
		}
	}
}

void onKeyTrampoline(GLFWwindow* window, int key, int scancode, int action, int mods) {
	auto it = callbackRegistry().find(window);
	if (it != callbackRegistry().end()) {
		if (it->second.target) {
			const bool pressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
			const bool repeat  = (action == GLFW_REPEAT);
			it->second.target->injectKey(translateKey(key), translateMods(mods), pressed, repeat);
		}
		if (it->second.prevKey) {
			it->second.prevKey(window, key, scancode, action, mods);
		}
	}
}

void onCharTrampoline(GLFWwindow* window, unsigned int codepoint) {
	auto it = callbackRegistry().find(window);
	if (it != callbackRegistry().end()) {
		if (it->second.target) {
			it->second.target->injectChar(static_cast<std::uint32_t>(codepoint));
		}
		if (it->second.prevChar) {
			it->second.prevChar(window, codepoint);
		}
	}
}

} // namespace

UiPlatformGlfw::~UiPlatformGlfw() {
	uninstallCallbacks();
}

void UiPlatformGlfw::installCallbacks(GLFWwindow* window) {
	if (!window) {
		return;
	}
	uninstallCallbacks();

	CallbackChain chain;
	chain.target          = this;
	chain.prevMouseButton = glfwSetMouseButtonCallback(window, &onMouseButtonTrampoline);
	chain.prevCursorPos   = glfwSetCursorPosCallback  (window, &onCursorPosTrampoline);
	chain.prevScroll      = glfwSetScrollCallback     (window, &onScrollTrampoline);
	chain.prevKey         = glfwSetKeyCallback        (window, &onKeyTrampoline);
	chain.prevChar        = glfwSetCharCallback       (window, &onCharTrampoline);
	callbackRegistry()[window] = chain;

	m_window = window;
}

void UiPlatformGlfw::uninstallCallbacks() {
	if (!m_window) {
		return;
	}
	auto it = callbackRegistry().find(m_window);
	if (it != callbackRegistry().end()) {
		// Restore whatever was installed before us, so tearing the GUI down mid-run
		// leaves the window exactly as it found it.
		glfwSetMouseButtonCallback(m_window, it->second.prevMouseButton);
		glfwSetCursorPosCallback  (m_window, it->second.prevCursorPos);
		glfwSetScrollCallback     (m_window, it->second.prevScroll);
		glfwSetKeyCallback        (m_window, it->second.prevKey);
		glfwSetCharCallback       (m_window, it->second.prevChar);
		callbackRegistry().erase(it);
	}
	m_window = nullptr;
}

PlatformHooks UiPlatformGlfw::makeHooks(GLFWwindow* window) const {
	PlatformHooks hooks;
	hooks.getClipboardText = [window]() { return UiPlatformGlfw::getClipboardText(window); };
	hooks.setClipboardText = [window](std::string_view text) { UiPlatformGlfw::setClipboardText(window, text); };
	hooks.setCursorShape   = [window](CursorShape shape) { UiPlatformGlfw::setCursorShape(window, shape); };
	return hooks;
}

std::string UiPlatformGlfw::getClipboardText(GLFWwindow* window) {
	const char* text = glfwGetClipboardString(window);
	return text ? std::string(text) : std::string();
}

void UiPlatformGlfw::setClipboardText(GLFWwindow* window, std::string_view text) {
	glfwSetClipboardString(window, std::string(text).c_str());
}

void UiPlatformGlfw::setCursorShape(GLFWwindow* window, CursorShape shape) {
	glfwSetCursor(window, standardCursor(shape));
}

void UiPlatformGlfw::beginFrame(Vec2 displaySize, float contentScale, float deltaTime) {
	// Step 1 of docs/gui/04's beginFrame list -- draining a cross-thread callback
	// queue -- belongs to GuiContext::postToMainThread, which does not exist until
	// the GUI layer. Steps 2-6 are this class's responsibility.

	// Step 2: move pending input into current.
	m_current.mousePos     = m_pending.mousePos;
	m_current.displaySize  = displaySize;
	m_current.contentScale = contentScale;
	m_current.deltaTime    = deltaTime;
	m_current.wheelDelta   = m_pending.wheelDelta;
	m_current.charQueue    = std::move(m_pending.charQueue);
	m_current.keyQueue     = std::move(m_pending.keyQueue);
	// modsDown is level state, not an edge queue -- it must survive frames with no new
	// key events at all (e.g. every frame of a slider drag after Shift was already held
	// down before the press), so unlike charQueue/keyQueue it is copied, never cleared.
	m_current.modsDown      = m_pending.modsDown;
	m_pending.charQueue.clear();
	m_pending.keyQueue.clear();
	m_pending.wheelDelta = 0.0f;

	// Step 3: mouse delta.
	m_current.mouseDelta = m_current.mousePos - m_current.mousePosPrev;

	// Step 4: edge flags from the queued press/release events. mousePressed/
	// mouseReleased were already cleared by the previous endFrame(); a
	// press-and-release queued within the same frame sets both, which is exactly
	// what keeps a fast click from being lost (docs/gui/04, "onMouseButton").
	for (const PendingButtonEvent& event : m_pendingButtonEvents) {
		const int idx = static_cast<int>(event.button);
		if (idx < 0 || idx >= 3) {
			continue; // MouseButton::Count sentinel, or a button we don't track
		}
		if (event.pressed) {
			if (!m_current.mouseDown[idx]) {
				m_current.mousePressed[idx] = true;
			}
			m_current.mouseDown[idx] = true;
			m_current.mouseDownPos[idx] = m_current.mousePos;
		} else {
			if (m_current.mouseDown[idx]) {
				m_current.mouseReleased[idx] = true;
			}
			m_current.mouseDown[idx] = false;
		}
	}
	m_pendingButtonEvents.clear();

	// Step 5: mouseDownDuration.
	for (int i = 0; i < 3; ++i) {
		if (m_current.mouseDown[i]) {
			m_current.mouseDownDuration[i] = (m_current.mouseDownDuration[i] < 0.0f)
			                                ? 0.0f
			                                : m_current.mouseDownDuration[i] + deltaTime;
		} else {
			m_current.mouseDownDuration[i] = -1.0f;
		}
	}

	// Step 6 (clearing pending charQueue/keyQueue/wheelDelta) is done above via the
	// move-and-clear on charQueue/keyQueue and the explicit reset of wheelDelta.
}

void UiPlatformGlfw::endFrame() {
	m_current.mousePosPrev = m_current.mousePos;
	for (int i = 0; i < 3; ++i) {
		m_current.mousePressed[i] = false;
		m_current.mouseReleased[i] = false;
	}
	m_requestedCursor = CursorShape::Arrow;
}

void UiPlatformGlfw::injectMousePos(Vec2 logicalPos) {
	m_pending.mousePos = logicalPos;
}

void UiPlatformGlfw::injectMouseButton(MouseButton button, bool pressed) {
	m_pendingButtonEvents.push_back({ button, pressed });
}

void UiPlatformGlfw::injectScroll(float delta) {
	m_pending.wheelDelta += delta;
}

void UiPlatformGlfw::injectKey(int key, int mods, bool pressed, bool repeat) {
	m_pending.keyQueue.push_back(KeyEvent{ key, mods, pressed, repeat });
	// `mods` is always the full current modifier bitmask (GLFW's own convention, mirrored
	// by tests calling inject* directly), so last-writer-wins is correct here even though
	// this fires for every key, not just modifier keys themselves.
	m_pending.modsDown = mods;
}

void UiPlatformGlfw::injectChar(std::uint32_t codepoint) {
	m_pending.charQueue.push_back(codepoint);
}

} // namespace lightGraphics::ui
