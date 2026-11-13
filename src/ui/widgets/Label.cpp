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

#include <lightVulkanGraphics/ui/widgets/Label.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "TextWrap.h"
#include "LineSelection.h"

#include <algorithm>

namespace lightGraphics::ui {

namespace {
	// Resolves which Font/size/texture this Label actually draws with this frame:
	// the heading face (docs/gui/03-text-and-fonts.md, "Headings") only when the widget
	// asked for one AND the GuiContext it's drawn/measured with actually has one baked
	// -- a Label must never crash or silently vanish just because a consumer left
	// GuiCreateInfo::headingFontSize at its disabled 0.0f default.
	using LabelFont = StyledFont;

	LabelFont resolveLabelFont(const GuiContext& ctx, FontStyle style) {
		return ctx.styledFont(style);
	}
}

struct Label::Selection {
	LineSelection sel;
	float time = 0.0f;
	float lastClick = -10.0f;
	int   clicks = 0;
};

Label::Label(std::string text) : m_text(std::move(text)) {}

void Label::setText(std::string text) {
	if (text != m_text) {
		m_text = std::move(text);
		if (m_sel) {
			m_sel->sel.clear();   // the old byte offsets no longer mean anything
		}
	}
}

std::vector<Label::Line> Label::layoutLines(const GuiContext& ctx) const {
	LabelFont lf = resolveLabelFont(ctx, m_style);
	TextFlags flags = m_tabular ? TextFlags::Tabular : TextFlags::None;
	const float lh = lf.font.lineHeight(lf.pixelSize);
	std::vector<Line> out;
	auto originX = [&](std::string_view line) {
		if (m_align == Align::Start) {
			return m_bounds.x;
		}
		const float w = lf.font.measureText(line, lf.pixelSize, flags).x;
		return m_align == Align::Center ? m_bounds.x + (m_bounds.w - w) * 0.5f : m_bounds.x + m_bounds.w - w;
	};
	if (m_wordWrap) {
		float y = m_bounds.y;
		for (std::string_view line : wrapText(lf.font, m_text, lf.pixelSize, m_bounds.w, flags)) {
			out.push_back({ line, originX(line), y, lh });
			y += lh;
		}
	} else {
		out.push_back({ m_text, originX(m_text), m_bounds.y + (m_bounds.h - lh) * 0.5f, lh });
	}
	return out;
}

void Label::update(GuiContext& ctx) {
	if (!m_selectable || m_text.empty() || !effectivelyEnabled()) {
		return;
	}
	if (ctx.hoveredId() == id()) {
		ctx.requestCursorShape(CursorShape::TextInput);
	}
	if (!m_sel) {
		m_sel = std::make_shared<Selection>();
	}
	Selection& s = *m_sel;
	const InputState& in = ctx.input();
	s.time += in.deltaTime;
	const int left = static_cast<int>(MouseButton::Left);
	if (ctx.activeId() != id()) {
		s.sel.end();
		return;
	}
	LabelFont lf = resolveLabelFont(ctx, m_style);
	const std::vector<Line> lines = layoutLines(ctx);
	if (lines.empty()) {
		return;
	}
	auto posAt = [&](Vec2 p) {
		int li = 0;
		for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
			if (p.y >= lines[static_cast<std::size_t>(i)].y) { li = i; }
		}
		const Line& l = lines[static_cast<std::size_t>(li)];
		return LinePos{ li, lf.font.indexAtOffset(l.text, lf.pixelSize, p.x - l.x) };
	};
	auto publish = [&]() {
		std::vector<std::string_view> texts;
		for (const Line& l : lines) { texts.push_back(l.text); }
		const LinePos a = s.sel.first(), b = s.sel.last();
		std::string text = (a.line == 0 && a.byte == 0 && b.line == static_cast<int>(lines.size()) - 1 &&
		                    b.byte >= lines.back().text.size())
		                       ? m_text
		                       : s.sel.text(texts, " ");   // wrapped segments rejoin with the space they broke at
		ctx.publishSelection(id(), text);
	};
	if (in.mousePressed[left]) {
		s.clicks = (s.time - s.lastClick < 0.35f) ? (s.clicks % 3) + 1 : 1;
		s.lastClick = s.time;
		const LinePos p = posAt(in.mousePos);
		if (s.clicks == 2) {
			s.sel.selectWord(p, lines[static_cast<std::size_t>(p.line)].text);
			publish();
		} else if (s.clicks >= 3) {
			s.sel.set({ 0, 0 }, { static_cast<int>(lines.size()) - 1, lines.back().text.size() });
			publish();
		} else {
			s.sel.begin(p);
			ctx.clearSelection(id());
		}
	} else if (in.mouseDown[left] && s.sel.dragging()) {
		s.sel.extend(posAt(in.mousePos));
		if (!s.sel.empty()) { publish(); }
	}
	if (in.mouseReleased[left]) {
		s.sel.end();
	}
}

Vec2 Label::preferredSize(const GuiContext& ctx) const {
	LabelFont lf = resolveLabelFont(ctx, m_style);
	TextFlags flags = m_tabular ? TextFlags::Tabular : TextFlags::None;
	if (!m_wordWrap) {
		return lf.font.measureText(m_text, lf.pixelSize, flags);
	}

	// m_bounds.w reflects the row width from the PREVIOUS layout pass (0 the very first
	// time this panel ever laid out). GuiContext::endFrame() runs layout twice per frame
	// for exactly this reason: pass 1 gives every widget a first-cut bounds; pass 2 then
	// sees the correct width here and reports the true wrapped height. See docs/gui/05,
	// "Label".
	float lh = lf.font.lineHeight(lf.pixelSize);
	if (m_bounds.w <= 0.0f) {
		return { 0.0f, lh };
	}
	auto lines = wrapText(lf.font, m_text, lf.pixelSize, m_bounds.w, flags);
	return { 0.0f, static_cast<float>(lines.size()) * lh };
}

void Label::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	Color color = m_colorOverride ? *m_colorOverride : (effectivelyEnabled() ? th.text : th.textDisabled);
	TextFlags flags = m_tabular ? TextFlags::Tabular : TextFlags::None;
	LabelFont lf = resolveLabelFont(ctx, m_style);

	if (m_sel && !m_sel->sel.empty() && ctx.selectionOwner() == id()) {
		const std::vector<Line> lines = layoutLines(ctx);
		for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
			const Line& l = lines[static_cast<std::size_t>(i)];
			std::size_t a = 0, b = 0;
			if (m_sel->sel.lineRange(i, l.text.size(), a, b)) {
				dl.addRectFilled(selectionRect(lf.font, l.text, lf.pixelSize, l.x, l.y, l.h, a, b), th.selectionBg);
			}
		}
	}

	if (m_wordWrap) {
		auto lines = wrapText(lf.font, m_text, lf.pixelSize, m_bounds.w, flags);
		float lh = lf.font.lineHeight(lf.pixelSize);
		float y = m_bounds.y;
		for (auto& line : lines) {
			Rect lineRect{ m_bounds.x, y, m_bounds.w, lh };
			dl.addTextClipped(lf.font, lf.pixelSize, lineRect, color, line, m_align, Align::Start, flags,
			                   lf.textureId);
			y += lh;
		}
		return;
	}

	dl.addTextClipped(lf.font, lf.pixelSize, m_bounds, color, m_text, m_align, Align::Center, flags, lf.textureId);
}

} // namespace lightGraphics::ui
