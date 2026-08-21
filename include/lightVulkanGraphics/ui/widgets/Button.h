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

// docs/gui/05-widgets.md, "Button": fires on release-inside-after-press-inside, never on
// press. Focusable; Space and Enter activate it while focused.

#include "../Widget.h"

#include <functional>
#include <string>

namespace lightGraphics::ui {

class Button : public Widget {
public:
	explicit Button(std::string text) { setLabel(std::move(text)); }

	void setOnClick(std::function<void()> onClick) { m_onClick = std::move(onClick); }
	void setToggle(bool isToggle) { m_isToggle = isToggle; }
	bool isPressed() const { return m_pressed; }
	void setPressed(bool pressed) { m_pressed = pressed; }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return true; }

private:
	std::function<void()> m_onClick;
	bool m_isToggle = false;
	bool m_pressed = false;   // toggle state
	bool m_held = false;      // captured AND currently hovered this frame -- drives fill colour
};

} // namespace lightGraphics::ui
