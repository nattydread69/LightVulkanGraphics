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

// docs/gui/05-widgets.md, "Spacer": invisible, non-interactive, fixed height.

#include "../Widget.h"

namespace lightGraphics::ui {

class Spacer : public Widget {
public:
	explicit Spacer(float height) : m_height(height) {}

	Vec2 preferredSize(const GuiContext&) const override { return { 0.0f, m_height }; }
	void draw(DrawList&, const GuiContext&) const override {}

	bool acceptsCapture() const override { return false; }
	bool acceptsFocus()   const override { return false; }
	bool hitTest(Vec2) const override { return false; }

private:
	float m_height;
};

} // namespace lightGraphics::ui
