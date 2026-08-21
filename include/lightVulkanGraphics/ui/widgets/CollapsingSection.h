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

// docs/gui/05-widgets.md, "CollapsingSection": Owns children. preferredSize returns just
// the header height when closed, header plus laid-out children when open. Header draws a
// triangle glyph (built from addTriangleFilled, not a font character) plus the title, and
// toggles on click.

#include "CompositeWidget.h"

#include <string>

namespace lightGraphics::ui {

class CollapsingSection : public CompositeWidget {
public:
	explicit CollapsingSection(std::string title, bool openInitially = true);

	void setOpen(bool open) { m_open = open; }
	bool isOpen() const { return m_open; }

	Vec2 preferredSize(const GuiContext& ctx) const override;
	void update(GuiContext& ctx) override;
	void draw(DrawList&, const GuiContext&) const override;
	void layout(const GuiContext& ctx) override;
	Widget* hitTestDeep(Vec2 p) override;  // Block children when closed

private:
	std::string m_title;
	bool m_open;
};

} // namespace lightGraphics::ui
