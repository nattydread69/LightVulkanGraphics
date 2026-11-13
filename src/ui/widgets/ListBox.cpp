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

#include <lightVulkanGraphics/ui/widgets/ListBox.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "LineSelection.h"

#include <algorithm>
#include <cmath>

namespace lightGraphics::ui {

struct ListBox::Selection {
	LineSelection sel;
	Vec2 pressPos{};
};

ListBox::ListBox(std::string label, std::vector<std::string> items, int initialIndex)
	: m_items(std::move(items)) {
	setLabel(std::move(label));
	setSelectedIndex(initialIndex, false);
}

std::string_view ListBox::selectedText() const {
	if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_items.size())) {
		return {};
	}
	return m_items[static_cast<std::size_t>(m_selectedIndex)];
}

void ListBox::setItems(std::vector<std::string> items) {
	m_items = std::move(items);
	if (m_sel) {
		m_sel->sel.clear();
	}
	int maxIndex = static_cast<int>(m_items.size()) - 1;
	if (m_selectedIndex > maxIndex) {
		m_selectedIndex = maxIndex;   // -1 when the new list is empty
	}
	m_scrollRow = 0;
}

void ListBox::setSelectedIndex(int index, bool fireCallback) {
	if (index < 0 || m_items.empty()) {
		m_selectedIndex = -1;
	} else {
		m_selectedIndex = std::clamp(index, 0, static_cast<int>(m_items.size()) - 1);
	}
	if (fireCallback && m_onChange) {
		m_onChange(m_selectedIndex);
	}
}

int ListBox::itemAtY(const GuiContext& ctx, const Rect& list, float mouseY) const {
	const Theme& th = ctx.theme();
	float localY = mouseY - list.y - th.framePadding;
	if (localY < 0.0f) {
		return -1;
	}
	int row = static_cast<int>(localY / th.rowHeight);
	int visibleCount = std::min<int>(static_cast<int>(m_items.size()), m_visibleRows);
	if (row >= visibleCount) {
		return -1;
	}
	int idx = m_scrollRow + row;
	if (idx < 0 || idx >= static_cast<int>(m_items.size())) {
		return -1;
	}
	return idx;
}

void ListBox::clampScrollRow() {
	int visibleCount = std::min<int>(static_cast<int>(m_items.size()), m_visibleRows);
	int maxScroll = std::max(0, static_cast<int>(m_items.size()) - visibleCount);
	m_scrollRow = std::clamp(m_scrollRow, 0, maxScroll);
}

void ListBox::scrollToShowSelected() {
	if (m_selectedIndex < 0) {
		return;
	}
	int visibleCount = std::min<int>(static_cast<int>(m_items.size()), m_visibleRows);
	if (m_selectedIndex < m_scrollRow) {
		m_scrollRow = m_selectedIndex;
	} else if (m_selectedIndex >= m_scrollRow + visibleCount) {
		m_scrollRow = m_selectedIndex - visibleCount + 1;
	}
	clampScrollRow();
}

void ListBox::moveSelection(int dir) {
	if (m_items.empty()) {
		return;
	}
	int n = static_cast<int>(m_items.size());
	// From "nothing selected", either direction lands on the first item -- simpler and
	// just as predictable as picking a direction-dependent wrap target, and avoids
	// working out signed-modulo arithmetic for a base of -1.
	int next = (m_selectedIndex < 0) ? 0 : ((m_selectedIndex + dir) % n + n) % n;
	setSelectedIndex(next, true);
	scrollToShowSelected();
}

Vec2 ListBox::preferredSize(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	return { 0.0f, static_cast<float>(m_visibleRows) * th.rowHeight + 2.0f * th.framePadding };
}

void ListBox::update(GuiContext& ctx) {
	if (!m_enabled) {
		return;
	}
	const InputState& in = ctx.input();
	const int leftIdx = static_cast<int>(MouseButton::Left);
	Rect list = splitRow(ctx).control;

	if (!m_sel) {
		m_sel = std::make_shared<Selection>();
	}
	Selection& s = *m_sel;
	const Theme& th = ctx.theme();
	const int rowsShown = std::min<int>(static_cast<int>(m_items.size()), m_visibleRows);
	auto lines = [&]() {
		std::vector<std::string_view> v;
		v.reserve(m_items.size());
		for (const std::string& it : m_items) { v.push_back(it); }
		return v;
	};
	// Row under (or nearest to) the pointer, clamped to what is on screen, plus the byte offset in it.
	auto posAt = [&](Vec2 p) {
		int row = static_cast<int>(std::floor((p.y - list.y - th.framePadding) / th.rowHeight));
		row = std::clamp(row, 0, std::max(0, rowsShown - 1));
		const int idx = std::clamp(m_scrollRow + row, 0, std::max(0, static_cast<int>(m_items.size()) - 1));
		const float textX = list.x + th.framePadding * 0.5f + th.framePadding;
		return LinePos{ idx, m_items.empty() ? 0 : ctx.font().indexAtOffset(m_items[static_cast<std::size_t>(idx)], th.fontSize, p.x - textX) };
	};

	if (ctx.activeId() == id() && in.mousePressed[leftIdx] && !m_items.empty() && itemAtY(ctx, list, in.mousePos.y) >= 0) {
		s.sel.begin(posAt(in.mousePos));
		s.pressPos = in.mousePos;
		ctx.clearSelection(id());
	}
	if (ctx.activeId() == id() && in.mouseDown[leftIdx] && s.sel.dragging()) {
		const float dx = in.mousePos.x - s.pressPos.x, dy = in.mousePos.y - s.pressPos.y;
		if (s.sel.moved() || dx * dx + dy * dy > 16.0f) {   // a few pixels of travel turns a click into a selection
			s.sel.extend(posAt(in.mousePos));
			if (!s.sel.empty()) {
				ctx.publishSelection(id(), s.sel.text(lines(), "\n"));
			}
		}
	}

	// Release-inside, same convention Button/DropDown's popup rows use: press-then-
	// drag-away cancels rather than selecting whatever the cursor happens to end up on.
	// A drag that selected text is not a click on a row.
	if (ctx.activeId() == id() && in.mouseReleased[leftIdx]) {
		const bool selectedText = s.sel.moved() && !s.sel.empty();
		s.sel.end();
		if (!selectedText && list.contains(in.mousePos)) {
			int idx = itemAtY(ctx, list, in.mousePos.y);
			if (idx >= 0) {
				setSelectedIndex(idx, true);
			}
		} else if (!selectedText) {
			s.sel.clear();
		}
	}
	// With no text selected, Ctrl+C copies the selected row.
	if (s.sel.empty() && m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size()) &&
	    ctx.selectionOwner() != id() && ctx.focusedId() == id()) {
		ctx.publishSelection(id(), m_items[static_cast<std::size_t>(m_selectedIndex)]);
	} else if (s.sel.empty() && ctx.selectionOwner() == id() && m_selectedIndex >= 0 &&
	           m_selectedIndex < static_cast<int>(m_items.size()) && ctx.focusedId() == id() &&
	           ctx.selectedText() != m_items[static_cast<std::size_t>(m_selectedIndex)]) {
		ctx.publishSelection(id(), m_items[static_cast<std::size_t>(m_selectedIndex)]);   // selection moved by key or click
	}

	if (ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (!ev.pressed) {
				continue;
			}
			if (ev.key == Key::Up) {
				moveSelection(-1);
			} else if (ev.key == Key::Down) {
				moveSelection(1);
			}
		}
	}

	if (ctx.hoveredId() == id() && in.wheelDelta != 0.0f) {
		// docs/gui/05's Panel scrollbar convention ("wheel scrolls by 3 x lineHeight"),
		// applied here in row units rather than pixels -- same as DropDown's popup.
		m_scrollRow -= static_cast<int>(in.wheelDelta) * 3;
		clampScrollRow();
	}
}

void ListBox::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	RowSplit split = splitRow(ctx);

	if (!m_label.empty()) {
		Color labelColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, split.label, labelColor, m_label, Align::Start, Align::Center);
	}

	const Rect& list = split.control;
	bool focused = ctx.focusedId() == id();

	dl.addRectFilled(list, th.frameBg, th.rounding);
	Color borderColor = (effectivelyEnabled() && focused) ? th.accent : th.border;
	dl.addRect(list, borderColor, 1.0f, th.rounding);

	int total = static_cast<int>(m_items.size());
	int visibleCount = std::min(total, m_visibleRows);
	bool scrollable = total > m_visibleRows;
	float scrollbarW = scrollable ? th.scrollbarWidth : 0.0f;

	bool hoveredHere = ctx.hoveredId() == id() && list.contains(ctx.input().mousePos);
	int hoveredRow = hoveredHere ? itemAtY(ctx, list, ctx.input().mousePos.y) : -1;

	float listTop = list.y + th.framePadding;
	float rowWidth = list.w - scrollbarW - th.framePadding * 0.5f;

	for (int row = 0; row < visibleCount; ++row) {
		int idx = m_scrollRow + row;
		if (idx >= total) {
			break;
		}
		Rect rowRect{ list.x + th.framePadding * 0.5f, listTop + static_cast<float>(row) * th.rowHeight,
		              rowWidth, th.rowHeight };

		if (idx == m_selectedIndex) {
			dl.addRectFilled(rowRect, th.accentActive, th.rounding);
		} else if (idx == hoveredRow) {
			dl.addRectFilled(rowRect, th.frameBgHovered, th.rounding);
		}

		if (m_sel && !m_sel->sel.empty() && ctx.selectionOwner() == id()) {
			std::size_t a = 0, b = 0;
			const std::string& item = m_items[static_cast<std::size_t>(idx)];
			if (m_sel->sel.lineRange(idx, item.size(), a, b)) {
				const float lh = ctx.font().lineHeight(th.fontSize);
				dl.addRectFilled(selectionRect(ctx.font(), item, th.fontSize, rowRect.x + th.framePadding,
				                                rowRect.y + (rowRect.h - lh) * 0.5f, lh, a, b), th.selectionBg);
			}
		}
		Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, rowRect.insetXY(th.framePadding, 0.0f), textColor,
		                   m_items[static_cast<std::size_t>(idx)], Align::Start, Align::Center);
	}

	if (scrollable) {
		Rect track{ list.right() - th.scrollbarWidth, list.y + th.framePadding, th.scrollbarWidth,
		            list.h - 2.0f * th.framePadding };
		dl.addRectFilled(track, th.scrollbarBg, th.rounding);

		float thumbH = std::max(track.h * (static_cast<float>(visibleCount) / static_cast<float>(total)), 8.0f);
		int maxScroll = total - visibleCount;
		float t = maxScroll > 0 ? static_cast<float>(m_scrollRow) / static_cast<float>(maxScroll) : 0.0f;
		Rect thumb{ track.x, track.y + t * (track.h - thumbH), track.w, thumbH };
		dl.addRectFilled(thumb, th.scrollbarGrab, th.rounding);
	}
}

} // namespace lightGraphics::ui
