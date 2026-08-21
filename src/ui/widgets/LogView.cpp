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

#include <lightVulkanGraphics/ui/widgets/LogView.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "../TextWrap.h"

#include <algorithm>
#include <cmath>

namespace lightGraphics::ui {

LogView::LogView(std::string label) {
	setLabel(std::move(label));
}

void LogView::push(std::string line) {
	m_rawLines.push_back(std::move(line));
}

void LogView::clear() {
	m_rawLines.clear();
	m_scrollRow = 0;
	m_followBottom = true;
}

void LogView::setHeight(float px) {
	m_heightPx = px;
}

Rect LogView::contentRect(const GuiContext& ctx) const {
	return splitRow(ctx).control.inset(ctx.theme().framePadding);
}

int LogView::visibleRowCount(const GuiContext& ctx) const {
	float lh = ctx.font().lineHeight(ctx.theme().fontSize);
	return std::max(1, static_cast<int>(contentRect(ctx).h / lh));
}

Rect LogView::scrollbarTrackRect(const GuiContext& ctx, const Rect& box) const {
	const Theme& th = ctx.theme();
	return { box.right() - th.scrollbarWidth, box.y + th.framePadding, th.scrollbarWidth,
	         box.h - 2.0f * th.framePadding };
}

Rect LogView::scrollbarThumbRect(const GuiContext& ctx, const Rect& box, int totalLines, int visibleRows) const {
	Rect track = scrollbarTrackRect(ctx, box);
	float fraction = totalLines > 0 ? static_cast<float>(visibleRows) / static_cast<float>(totalLines) : 1.0f;
	// Same 20px grab-target floor Panel's own draggable scrollbar thumb uses (Panel.cpp)
	// -- larger than DropDown's popup scrollbar's 8px floor, because that one is visual
	// only; this one, like Panel's, has to be a comfortable drag target.
	float thumbH = std::min(track.h, std::max(track.h * fraction, 20.0f));
	int maxScroll = std::max(0, totalLines - visibleRows);
	float t = maxScroll > 0 ? static_cast<float>(m_scrollRow) / static_cast<float>(maxScroll) : 0.0f;
	return { track.x, track.y + t * (track.h - thumbH), track.w, thumbH };
}

std::vector<std::string_view> LogView::computeVisualLines(const GuiContext& ctx) const {
	std::vector<std::string_view> lines;
	if (!m_wordWrap) {
		lines.reserve(m_rawLines.size());
		for (const std::string& raw : m_rawLines) {
			lines.push_back(raw);
		}
		return lines;
	}

	const Theme& th = ctx.theme();
	// Always reserves the scrollbar gutter's width, whether or not a scrollbar ends up
	// being drawn this frame -- sidesteps the self-reference Panel's own scrollbar
	// solves with a cross-pass cache (docs/gui/05, "Panel", "Scrolling", the ordering
	// trap): whether THIS widget needs a scrollbar depends on total wrapped-line count,
	// which depends on wrap width, which would otherwise depend on whether a scrollbar
	// is needed. A few pixels of unused width when no scrollbar is showing costs less
	// than resolving that properly would be worth here.
	float width = contentRect(ctx).w - th.scrollbarWidth;
	for (const std::string& raw : m_rawLines) {
		auto wrapped = wrapText(ctx.font(), raw, th.fontSize, width);
		lines.insert(lines.end(), wrapped.begin(), wrapped.end());
	}
	return lines;
}

Vec2 LogView::preferredSize(const GuiContext&) const {
	return { 0.0f, m_heightPx };
}

void LogView::update(GuiContext& ctx) {
	if (!m_enabled) {
		m_dragTarget = DragTarget::None;
		return;
	}

	// Deferred ring-buffer trim (see setMaxLines()'s header comment): only while
	// following the bottom, so a user scrolled up to read history never has the content
	// under their current view silently age out.
	if (m_followBottom) {
		while (m_maxLines > 0 && m_rawLines.size() > m_maxLines) {
			m_rawLines.pop_front();
		}
	}

	auto visual = computeVisualLines(ctx);
	int total = static_cast<int>(visual.size());
	int visibleRows = visibleRowCount(ctx);
	int maxScroll = std::max(0, total - visibleRows);
	bool needsScrollbar = total > visibleRows;

	if (m_followBottom) {
		m_scrollRow = maxScroll;
	} else {
		m_scrollRow = std::clamp(m_scrollRow, 0, maxScroll);
	}

	const InputState& in = ctx.input();
	const int leftIdx = static_cast<int>(MouseButton::Left);
	Rect box = splitRow(ctx).control;

	if (ctx.activeId() == id()) {
		if (in.mousePressed[leftIdx]) {
			Rect thumb = needsScrollbar ? scrollbarThumbRect(ctx, box, total, visibleRows) : Rect{};
			m_dragTarget = (needsScrollbar && thumb.contains(in.mousePos)) ? DragTarget::ScrollbarThumb
			                                                               : DragTarget::None;
			if (m_dragTarget == DragTarget::ScrollbarThumb) {
				m_scrollbarDragStartMouseY = in.mousePos.y;
				m_scrollbarDragStartScrollRow = m_scrollRow;
				m_followBottom = false;   // grabbing the thumb always means manual control
			}
		}
		if (in.mouseDown[leftIdx] && m_dragTarget == DragTarget::ScrollbarThumb) {
			Rect track = scrollbarTrackRect(ctx, box);
			Rect thumb = scrollbarThumbRect(ctx, box, total, visibleRows);
			float travel = std::max(1.0f, track.h - thumb.h);
			float dy = in.mousePos.y - m_scrollbarDragStartMouseY;
			float rowsPerPixel = maxScroll > 0 ? static_cast<float>(maxScroll) / travel : 0.0f;
			int newRow = m_scrollbarDragStartScrollRow + static_cast<int>(std::lround(dy * rowsPerPixel));
			m_scrollRow = std::clamp(newRow, 0, maxScroll);
			m_followBottom = (m_scrollRow >= maxScroll);
		}
		if (in.mouseReleased[leftIdx]) {
			m_dragTarget = DragTarget::None;
		}
	}

	if (ctx.hoveredId() == id() && in.wheelDelta != 0.0f) {
		// Same "3 rows per tick" convention Panel/DropDown/ListBox all use.
		m_scrollRow = std::clamp(m_scrollRow - static_cast<int>(in.wheelDelta) * 3, 0, maxScroll);
		m_followBottom = (m_scrollRow >= maxScroll);
	}

	if (ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (!ev.pressed) {
				continue;
			}
			if (ev.key == Key::Home) {
				m_scrollRow = 0;
				m_followBottom = false;
			} else if (ev.key == Key::End) {
				m_scrollRow = maxScroll;
				m_followBottom = true;
			} else if (ev.key == Key::PageUp) {
				m_scrollRow = std::clamp(m_scrollRow - std::max(1, visibleRows - 1), 0, maxScroll);
				m_followBottom = (m_scrollRow >= maxScroll);
			} else if (ev.key == Key::PageDown) {
				m_scrollRow = std::clamp(m_scrollRow + std::max(1, visibleRows - 1), 0, maxScroll);
				m_followBottom = (m_scrollRow >= maxScroll);
			}
		}
	}
}

void LogView::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	RowSplit split = splitRow(ctx);

	if (!m_label.empty()) {
		Color labelColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, split.label, labelColor, m_label, Align::Start, Align::Center);
	}

	const Rect& box = split.control;
	bool focused = ctx.focusedId() == id();

	dl.addRectFilled(box, th.frameBg, th.rounding);
	Color borderColor = (effectivelyEnabled() && focused) ? th.accent : th.border;
	dl.addRect(box, borderColor, 1.0f, th.rounding);

	auto visual = computeVisualLines(ctx);
	int total = static_cast<int>(visual.size());
	int visibleRows = visibleRowCount(ctx);
	bool needsScrollbar = total > visibleRows;

	// Content is drawn narrowed by the scrollbar gutter UNCONDITIONALLY, matching
	// computeVisualLines()'s own unconditional wrap width -- see that method's comment.
	Rect content = contentRect(ctx);
	Rect textClip{ content.x, content.y, content.w - th.scrollbarWidth, content.h };
	float lineHeight = ctx.font().lineHeight(th.fontSize);

	dl.pushClipRect(textClip);
	Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;
	float y = textClip.y;
	for (int row = 0; row < visibleRows; ++row) {
		int idx = m_scrollRow + row;
		if (idx < 0 || idx >= total) {
			break;
		}
		dl.addText(ctx.font(), th.fontSize, { textClip.x, y }, textColor, visual[static_cast<std::size_t>(idx)]);
		y += lineHeight;
	}
	dl.popClipRect();

	if (needsScrollbar) {
		Rect track = scrollbarTrackRect(ctx, box);
		dl.addRectFilled(track, th.scrollbarBg, th.rounding);
		Rect thumb = scrollbarThumbRect(ctx, box, total, visibleRows);
		bool active = ctx.activeId() == id() && m_dragTarget == DragTarget::ScrollbarThumb;
		dl.addRectFilled(thumb, active ? th.accentActive : th.scrollbarGrab, th.rounding);
	}
}

} // namespace lightGraphics::ui
