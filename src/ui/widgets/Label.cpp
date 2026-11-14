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
	LinePos anchor;
	bool  anchorSet = false;
	float time = 0.0f;
	float lastClick = -10.0f;
	int   clicks = 0;
};

Label::Label(std::string text) : m_text(std::move(text)) {}

void Label::setText(std::string text) {
	if (text != m_text) {
		m_text = std::move(text);
		if (m_sel) {
			m_sel->anchorSet = false;   // the old byte offsets no longer mean anything
		}
	}
}

std::vector<Label::Line> Label::layoutLines(const GuiContext& ctx) const {
	LabelFont lf = resolveLabelFont(ctx, m_style);
	TextFlags flags = m_tabular ? TextFlags::Tabular : TextFlags::None;
	const float lh = lf.font.lineHeight(lf.pixelSize);
	std::vector<Line> out;
	const bool multiLine = m_wordWrap || m_text.find('\n') != std::string::npos;
	auto originX = [&](std::string_view line) {
		if (m_align == Align::Start) {
			return m_bounds.x;
		}
		const float w = lf.font.measureText(line, lf.pixelSize, flags).x;
		return m_align == Align::Center ? m_bounds.x + (m_bounds.w - w) * 0.5f : m_bounds.x + m_bounds.w - w;
	};
	if (multiLine) {
		// Wrapped to the width when word wrap is on; otherwise only broken at newlines.
		float y = m_bounds.y;
		for (std::string_view line : wrapText(lf.font, m_text, lf.pixelSize, m_wordWrap ? m_bounds.w : 0.0f, flags)) {
			out.push_back({ line, originX(line), y, lh });
			y += lh;
		}
	} else {
		out.push_back({ m_text, originX(m_text), m_bounds.y + (m_bounds.h - lh) * 0.5f, lh });
	}
	return out;
}

namespace {
	// The Labels of `panel` that can take part in a selection, in reading (= layout) order.
	std::vector<Label*> selectableLabels(const Panel* panel) {
		std::vector<Label*> out;
		if (!panel) {
			return out;
		}
		for (std::size_t i = 0; i < panel->widgetCount(); ++i) {
			if (auto* l = dynamic_cast<Label*>(panel->widgetAt(i)); l && l->visible() && l->selectable() && !l->text().empty()) {
				out.push_back(l);
			}
		}
		return out;
	}

	float distanceToRect(const Rect& r, Vec2 p) {
		const float dx = std::max({ r.x - p.x, 0.0f, p.x - r.right() });
		const float dy = std::max({ r.y - p.y, 0.0f, p.y - r.bottom() });
		return dx * dx + dy * dy;
	}
}

// Nearest (line, byte) in this label to `p` (clamped to its first/last line and its text).
LinePos Label::hitPosition(const GuiContext& ctx, Vec2 p) const {
	LabelFont lf = resolveLabelFont(ctx, m_style);
	const std::vector<Line> lines = layoutLines(ctx);
	if (lines.empty()) {
		return {};
	}
	int li = 0;
	for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
		if (p.y >= lines[static_cast<std::size_t>(i)].y) { li = i; }
	}
	const Line& l = lines[static_cast<std::size_t>(li)];
	return LinePos{ li, lf.font.indexAtOffset(l.text, lf.pixelSize, p.x - l.x) };
}

// The text between two positions of this label (a wrapped label's segments rejoin with a space; the
// whole label comes back exactly as set).
std::string Label::textBetween(const GuiContext& ctx, LinePos a, LinePos b) const {
	const std::vector<Line> lines = layoutLines(ctx);
	if (lines.empty() || b < a) {
		return {};
	}
	if (a.line == 0 && a.byte == 0 && b.line >= static_cast<int>(lines.size()) - 1 && b.byte >= lines.back().text.size()) {
		return m_text;
	}
	// Wrapped segments rejoin with a space; a segment that ended at a newline in the text rejoins with one.
	LineSelection sel;
	sel.set(a, b);
	std::string out;
	for (int i = a.line; i <= b.line && i < static_cast<int>(lines.size()); ++i) {
		const Line& l = lines[static_cast<std::size_t>(i)];
		if (i > a.line) {
			const Line& previous = lines[static_cast<std::size_t>(i) - 1];
			const std::size_t end = static_cast<std::size_t>(previous.text.data() - m_text.data()) + previous.text.size();
			out += (end < m_text.size() && (m_text[end] == '\n' || m_text[end] == '\r')) ? '\n' : ' ';
		}
		std::size_t s = 0, e = 0;
		if (sel.lineRange(i, l.text.size(), s, e) && e > s) {
			out.append(l.text.substr(s, e - s));
		}
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
		s.anchorSet = false;
		return;
	}
	const std::vector<Line> lines = layoutLines(ctx);
	if (lines.empty()) {
		return;
	}
	const LinePos endOfThis{ static_cast<int>(lines.size()) - 1, lines.back().text.size() };

	auto setSpan = [&](const std::vector<Label*>& ls, std::size_t fromIdx, std::size_t toIdx, LinePos first, LinePos last) {
		GuiContext::TextSpan span;
		span.owner = id();
		for (std::size_t i = fromIdx; i <= toIdx; ++i) { span.widgets.push_back(ls[i]->id()); }
		span.firstLine = first.line; span.firstByte = first.byte;
		span.lastLine = last.line;   span.lastByte = last.byte;
		ctx.setTextSpan(span);
		std::string text;
		for (std::size_t i = fromIdx; i <= toIdx; ++i) {
			const std::vector<Line> theirs = ls[i]->layoutLines(ctx);
			if (theirs.empty()) { continue; }
			const LinePos a = (i == fromIdx) ? first : LinePos{ 0, 0 };
			const LinePos b = (i == toIdx) ? last : LinePos{ static_cast<int>(theirs.size()) - 1, theirs.back().text.size() };
			if (i != fromIdx) { text += "\n"; }
			text += ls[i]->textBetween(ctx, a, b);
		}
		if (!text.empty()) { ctx.publishSelection(id(), text); }
	};
	auto selectWithin = [&](LinePos a, LinePos b) {
		const std::vector<Label*> ls{ this };
		setSpan(ls, 0, 0, a < b ? a : b, a < b ? b : a);
	};

	if (in.mousePressed[left]) {
		s.clicks = (s.time - s.lastClick < 0.35f) ? (s.clicks % 3) + 1 : 1;
		s.lastClick = s.time;
		const LinePos p = hitPosition(ctx, in.mousePos);
		if (s.clicks == 2) {
			LineSelection word;
			word.selectWord(p, lines[static_cast<std::size_t>(p.line)].text);
			selectWithin(word.first(), word.last());
		} else if (s.clicks >= 3) {
			selectWithin({ 0, 0 }, endOfThis);
		} else {
			ctx.clearSelection(id());
			s.anchor = p;
			s.anchorSet = true;
		}
	} else if (in.mouseDown[left] && s.anchorSet) {
		// Drag: the pointer may be over this label or over another selectable Label of the same panel
		// (above or below it), in which case the selection runs across them in reading order.
		const std::vector<Label*> ls = selectableLabels(panel());
		std::size_t self = 0, target = 0;
		float best = -1.0f;
		for (std::size_t i = 0; i < ls.size(); ++i) {
			if (ls[i] == this) { self = i; }
			const float d = distanceToRect(ls[i]->bounds(), in.mousePos);
			if (best < 0.0f || d < best) { best = d; target = i; }
		}
		if (ls.empty() || ls[target] == this) {
			const LinePos here = hitPosition(ctx, in.mousePos);
			if (!(here == s.anchor)) { selectWithin(s.anchor, here); }
		} else {
			const LinePos there = ls[target]->hitPosition(ctx, in.mousePos);
			if (target > self) {
				setSpan(ls, self, target, s.anchor, there);
			} else {
				setSpan(ls, target, self, there, s.anchor);
			}
		}
	}
	if (in.mouseReleased[left]) {
		s.anchorSet = false;
	}
}

Vec2 Label::preferredSize(const GuiContext& ctx) const {
	LabelFont lf = resolveLabelFont(ctx, m_style);
	TextFlags flags = m_tabular ? TextFlags::Tabular : TextFlags::None;
	if (!m_wordWrap) {
		if (m_text.find('\n') == std::string::npos) {
			return lf.font.measureText(m_text, lf.pixelSize, flags);
		}
		// Broken at newlines only: as wide as the widest line, one line height per line.
		const auto lines = wrapText(lf.font, m_text, lf.pixelSize, 0.0f, flags);
		float widest = 0.0f;
		for (std::string_view line : lines) {
			widest = std::max(widest, lf.font.measureText(line, lf.pixelSize, flags).x);
		}
		return { widest, static_cast<float>(lines.size()) * lf.font.lineHeight(lf.pixelSize) };
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

	const GuiContext::TextSpan& span = ctx.textSpan();
	if (!span.widgets.empty() && std::find(span.widgets.begin(), span.widgets.end(), id()) != span.widgets.end()) {
		const std::vector<Line> lines = layoutLines(ctx);
		if (!lines.empty()) {
			const LinePos endOfThis{ static_cast<int>(lines.size()) - 1, lines.back().text.size() };
			const LinePos from = (id() == span.widgets.front()) ? LinePos{ span.firstLine, span.firstByte } : LinePos{ 0, 0 };
			const LinePos to = (id() == span.widgets.back()) ? LinePos{ span.lastLine, span.lastByte } : endOfThis;
			LineSelection sel;
			sel.set(from, to);
			for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
				const Line& l = lines[static_cast<std::size_t>(i)];
				std::size_t a = 0, b = 0;
				if (sel.lineRange(i, l.text.size(), a, b)) {
					dl.addRectFilled(selectionRect(lf.font, l.text, lf.pixelSize, l.x, l.y, l.h, a, b), th.selectionBg);
				}
			}
		}
	}

	if (m_wordWrap || m_text.find('\n') != std::string::npos) {
		auto lines = wrapText(lf.font, m_text, lf.pixelSize, m_wordWrap ? m_bounds.w : 0.0f, flags);
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
