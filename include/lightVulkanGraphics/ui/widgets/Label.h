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

// docs/gui/05-widgets.md, "Label": static, non-interactive text.

#include "../Widget.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

class Font;
struct LinePos;   // include/internal/ui/LineSelection.h

class Label : public Widget {
public:
	explicit Label(std::string text);

	void setText(std::string text);
	// Selectable text (default on): drag to select, double-click for a word, triple-click for
	// everything, Ctrl+C to copy (docs/gui/05-widgets.md, "Text selection"). Turn it off for a
	// Label that sits over something else the user must be able to click.
	void setSelectable(bool selectable) { m_selectable = selectable; }
	bool selectable() const { return m_selectable; }
	std::string_view text() const { return m_text; }
	void setAlign(Align h) { m_align = h; }
	void setWordWrap(bool wrap) { m_wordWrap = wrap; }
	void setColor(Color color) { m_colorOverride = color; }
	// Pads digit advances to the widest digit's own width -- for a Label used to display
	// a live-updating numeric readout, so its digit columns don't jitter horizontally as
	// the value changes (docs/gui/03-text-and-fonts.md, "Tabular figures"). Off by
	// default: most Labels are static prose, not numeric readouts.
	void setTabular(bool tabular) { m_tabular = tabular; }
	// Draws with GuiContext's heading face/size instead of the primary font
	// (docs/gui/03-text-and-fonts.md, "Headings") -- e.g. a section title above a group
	// of widgets. Silently falls back to the primary font if the GuiContext this Label
	// is drawn/measured with was never configured with a heading font
	// (GuiCreateInfo::headingFontSize left at its 0.0f default): NOT every consumer
	// opts in, and a Label should never crash or render nothing just because one didn't.
	void setHeading(bool heading) { m_style = heading ? FontStyle::Heading : FontStyle::Regular; }
	// Which baked face to draw with (Regular, Heading, Bold, Monospace). A style the
	// GuiContext has no face for resolves to Regular. Supersedes setHeading().
	void setFontStyle(FontStyle style) { m_style = style; }
	FontStyle fontStyle() const { return m_style; }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsCapture() const override { return m_selectable && !m_text.empty(); }
	bool keepsFocusOnPress() const override { return true; }
	bool acceptsFocus()   const override { return false; }

private:
	struct Selection;
	struct Line { std::string_view text; float x; float y; float h; };
	std::vector<Line> layoutLines(const GuiContext&) const;
	LinePos hitPosition(const GuiContext&, Vec2) const;
	std::string textBetween(const GuiContext&, LinePos, LinePos) const;

	std::string m_text;
	Align m_align = Align::Start;
	bool  m_wordWrap = false;
	bool  m_tabular = false;
	FontStyle m_style = FontStyle::Regular;
	std::optional<Color> m_colorOverride;
	bool  m_selectable = true;
	std::shared_ptr<Selection> m_sel;   // created on first interaction; keeps this header free of internals
};

} // namespace lightGraphics::ui
