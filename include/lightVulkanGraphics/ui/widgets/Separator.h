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

// docs/gui/05-widgets.md, "Separator": a 1px horizontal rule, optionally with an inline
// caption ("line, gap, text, gap, line").

#include "../Widget.h"

#include <string>

namespace lightGraphics::ui {

class Separator : public Widget {
public:
	Separator() = default;
	explicit Separator(std::string caption) : m_caption(std::move(caption)) {}

	Vec2 preferredSize(const GuiContext&) const override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsCapture() const override { return false; }
	bool acceptsFocus()   const override { return false; }

private:
	std::string m_caption;
};

} // namespace lightGraphics::ui
