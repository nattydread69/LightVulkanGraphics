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

#include <lightVulkanGraphics/ui/widgets/DropDown.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>

namespace lightGraphics::ui {

DropDown::DropDown(std::string label, std::vector<std::string> items, int initialIndex)
	: m_items(std::move(items)) {
	setLabel(std::move(label));
	m_selectedIndex = m_items.empty() ? 0
	                                  : std::clamp(initialIndex, 0, static_cast<int>(m_items.size()) - 1);
	m_highlightIndex = m_selectedIndex;
}

std::string_view DropDown::selectedText() const {
	if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_items.size())) {
		return {};
	}
	return m_items[m_selectedIndex];
}

void DropDown::setItems(std::vector<std::string> items) {
	m_items = std::move(items);
	int maxIndex = static_cast<int>(m_items.size()) - 1;
	if (m_selectedIndex > maxIndex) {
		m_selectedIndex = std::max(0, maxIndex);
	}
	m_highlightIndex = m_selectedIndex;
	m_scrollRow = 0;
}

void DropDown::setSelectedIndex(int index, bool fireCallback) {
	if (m_items.empty()) {
		return;
	}
	index = std::clamp(index, 0, static_cast<int>(m_items.size()) - 1);
	m_selectedIndex = index;
	m_highlightIndex = index;
	if (fireCallback && m_onChange) {
		m_onChange(m_selectedIndex);
	}
}

// ---- popup geometry / hit-testing helpers ----------------------------------------

Rect DropDown::computePopupRect(const GuiContext& ctx) const {
	RowSplit split = splitRow(ctx);
	const Rect& control = split.control;
	const Theme& th = ctx.theme();

	int visibleCount = std::min<int>(static_cast<int>(m_items.size()), kMaxVisibleItems);
	float height = static_cast<float>(visibleCount) * th.rowHeight + 2.0f * th.framePadding;

	float y = control.bottom();
	if (y + height > ctx.input().displaySize.y) {
		// docs/gui/05: "flips above if it would run off the bottom of the framebuffer."
		y = control.top() - height;
	}

	return { control.x, y, control.w, height };
}

int DropDown::itemAtY(const GuiContext& ctx, const Rect& popupRect, float mouseY) const {
	const Theme& th = ctx.theme();
	float localY = mouseY - popupRect.y - th.framePadding;
	if (localY < 0.0f) {
		return -1;
	}
	int row = static_cast<int>(localY / th.rowHeight);
	int visibleCount = std::min<int>(static_cast<int>(m_items.size()), kMaxVisibleItems);
	if (row >= visibleCount) {
		return -1;
	}
	int idx = m_scrollRow + row;
	if (idx < 0 || idx >= static_cast<int>(m_items.size())) {
		return -1;
	}
	return idx;
}

void DropDown::moveHighlight(int dir) {
	// "Arrow keys wrap at both ends, or clamp -- whichever you implement, assert it."
	// Wrapping, to match RadioGroup::memberAfter's existing arrow-key convention
	// elsewhere in this library (docs/gui/05, "RadioButton and RadioGroup").
	if (m_items.empty()) {
		return;
	}
	int n = static_cast<int>(m_items.size());
	m_highlightIndex = ((m_highlightIndex + dir) % n + n) % n;
}

void DropDown::clampScrollRow() {
	int visibleCount = std::min<int>(static_cast<int>(m_items.size()), kMaxVisibleItems);
	int maxScroll = std::max(0, static_cast<int>(m_items.size()) - visibleCount);
	m_scrollRow = std::clamp(m_scrollRow, 0, maxScroll);
}

void DropDown::scrollToShowHighlight() {
	int visibleCount = std::min<int>(static_cast<int>(m_items.size()), kMaxVisibleItems);
	if (m_highlightIndex < m_scrollRow) {
		m_scrollRow = m_highlightIndex;
	} else if (m_highlightIndex >= m_scrollRow + visibleCount) {
		m_scrollRow = m_highlightIndex - visibleCount + 1;
	}
	clampScrollRow();
}

// ---- Widget overrides ---------------------------------------------------------------

Vec2 DropDown::preferredSize(const GuiContext& ctx) const {
	return { 0.0f, ctx.theme().rowHeight };
}

void DropDown::update(GuiContext& ctx) {
	if (!m_enabled) {
		return;
	}
	const InputState& in = ctx.input();
	const int leftIdx = static_cast<int>(MouseButton::Left);

	// Live read of GuiContext's popup state -- see the class-level comment on why this
	// needs no entering-state/m_wasFocused bookkeeping the way TextBox's Escape does.
	bool isOpen = (ctx.popupOwner() == this);

	// Control click: press-based toggle (docs/gui/05, "Checkbox"'s press-not-release
	// convention applied here too). hitTest(mousePos) is what tells a control press
	// apart from a press inside the popup, which ALSO sets ctx.activeId() == id() (see
	// GuiContext::update()'s popupHit branch) -- only a control press should toggle.
	bool controlPressed = ctx.activeId() == id() && in.mousePressed[leftIdx] && hitTest(in.mousePos);

	// Enter/Space open a focused-but-closed control, same baseline accessibility as
	// Button/Checkbox (docs/gui/05) -- without this, a keyboard-only user who tabs to
	// the control has no way to operate it at all.
	bool keyboardOpen = false;
	if (!isOpen && ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (ev.pressed && !ev.repeat && (ev.key == Key::Enter || ev.key == Key::Space)) {
				keyboardOpen = true;
				break;
			}
		}
	}

	if (controlPressed || keyboardOpen) {
		if (isOpen) {
			ctx.closePopup();
			isOpen = false;
		} else {
			isOpen = true;
			// "Opening the popup starts the highlight on the current selection."
			m_highlightIndex = m_selectedIndex;
			scrollToShowHighlight();
			if (keyboardOpen) {
				// Register and stop here -- do NOT fall through into this same frame's
				// Enter-handling loop below, which would otherwise reprocess the exact
				// same Enter/Space keydown as "select the just-reset highlight and
				// close" in the very frame that opened it. One frame's delay before
				// arrow keys/Enter act on the now-open popup is unobservable.
				ctx.openPopup(this, computePopupRect(ctx));
				return;
			}
		}
	}

	if (!isOpen) {
		return;
	}

	// Re-register every frame the popup stays open, recomputed from this frame's OWN
	// bounds -- see the class-level comment on why this makes the popup follow the
	// owner instead of the rect going stale.
	Rect rect = computePopupRect(ctx);
	ctx.openPopup(this, rect);

	bool activeHere = ctx.activeId() == id();
	if (activeHere) {
		if (in.mousePressed[leftIdx]) {
			// See m_pressStartedInPopup's declaration: capture spans the WHOLE click,
			// including the control click that opened the popup (already handled by
			// controlPressed above) -- only a press that itself lands inside the popup
			// should make the matching release select/close.
			m_pressStartedInPopup = rect.contains(in.mousePos);
			if (m_pressStartedInPopup) {
				int idx = itemAtY(ctx, rect, in.mousePos.y);
				if (idx >= 0) {
					m_highlightIndex = idx;
				}
			}
		}
		if (in.mouseReleased[leftIdx] && m_pressStartedInPopup) {
			// Select whatever is under the cursor at release, matching Button's
			// press-then-release-inside convention (docs/gui/05, "Button"). Releasing
			// outside any row (including outside the popup entirely, e.g. dragged off
			// after pressing inside) closes without selecting -- there is no row to
			// commit to.
			if (rect.contains(in.mousePos)) {
				int idx = itemAtY(ctx, rect, in.mousePos.y);
				if (idx >= 0) {
					setSelectedIndex(idx, true);
				}
			}
			ctx.closePopup();
			isOpen = false;
		}
	}

	if (!isOpen) {
		return;
	}

	// Hover tracks the highlight too, not just arrow keys/press-drag -- standard
	// combo-box behaviour, and lets a user see which row their cursor is over before
	// committing. Skipped while actively pressing so this doesn't fight the press-drag
	// highlight update above over the same frame's mouse position.
	if (!activeHere && ctx.hoveredId() == id() && rect.contains(in.mousePos)) {
		int idx = itemAtY(ctx, rect, in.mousePos.y);
		if (idx >= 0) {
			m_highlightIndex = idx;
		}
	}

	if (ctx.hoveredId() == id() && in.wheelDelta != 0.0f) {
		// docs/gui/05's Panel scrollbar convention ("wheel scrolls by 3 x lineHeight"),
		// applied here in row units rather than pixels.
		m_scrollRow -= static_cast<int>(in.wheelDelta) * 3;
		clampScrollRow();
	}

	if (ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (!ev.pressed) {
				continue;
			}
			if (ev.key == Key::Up) {
				moveHighlight(-1);
				scrollToShowHighlight();
			} else if (ev.key == Key::Down) {
				moveHighlight(1);
				scrollToShowHighlight();
			} else if (ev.key == Key::Enter) {
				setSelectedIndex(m_highlightIndex, true);
				ctx.closePopup();
				isOpen = false;
				break;
			}
			// Escape is deliberately NOT handled here -- GuiContext's own Escape
			// default (docs/gui/04, "Keyboard") already closed the popup before this
			// update() ran; isOpen already reflects that, and this loop is unreachable
			// by the time it matters (the `if (!isOpen) return;` above already fired).
		}
	}
}

void DropDown::draw(DrawList& dl, const GuiContext& ctx) const {
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
	bool open = (ctx.popupOwner() == this);
	bool focused = ctx.focusedId() == id();

	Color bg;
	if (!effectivelyEnabled()) {
		bg = th.frameBg.withAlpha(0.4f);
	} else if (open) {
		bg = th.frameBgActive;
	} else if (ctx.hoveredId() == id()) {
		bg = th.frameBgHovered;
	} else {
		bg = th.frameBg;
	}
	dl.addRectFilled(box, bg, th.rounding);

	Color borderColor = (effectivelyEnabled() && focused) ? th.accent : th.border;
	dl.addRect(box, borderColor, 1.0f, th.rounding);

	Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;
	float triReserve = th.fontSize;
	Rect textRect = box.inset(th.framePadding);
	textRect.w -= triReserve;
	dl.addTextClipped(ctx.font(), th.fontSize, textRect, textColor, selectedText(), Align::Start, Align::Center);

	// Disclosure triangle, built from addTriangleFilled rather than a font glyph --
	// same reasoning as CollapsingSection's header glyph (docs/gui/05).
	float s = th.fontSize * 0.32f;
	Vec2 c{ box.right() - th.framePadding - s, box.centre().y };
	Vec2 p0{ c.x - s, c.y - s * 0.6f };
	Vec2 p1{ c.x + s, c.y - s * 0.6f };
	Vec2 p2{ c.x, c.y + s * 0.6f };
	dl.addTriangleFilled(p0, p1, p2, textColor);
}

void DropDown::drawPopup(DrawList& dl, const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	Rect rect = ctx.popupRect();

	int total = static_cast<int>(m_items.size());
	int visibleCount = std::min(total, kMaxVisibleItems);
	bool scrollable = total > kMaxVisibleItems;
	float scrollbarW = scrollable ? th.scrollbarWidth : 0.0f;

	dl.addRectFilled(rect, th.frameBg, th.rounding);
	dl.addRect(rect, th.accent, 1.0f, th.rounding);

	float listTop = rect.y + th.framePadding;
	float rowWidth = rect.w - scrollbarW - th.framePadding * 0.5f;

	for (int row = 0; row < visibleCount; ++row) {
		int idx = m_scrollRow + row;
		if (idx >= total) {
			break;
		}
		Rect rowRect{ rect.x + th.framePadding * 0.5f, listTop + static_cast<float>(row) * th.rowHeight,
		              rowWidth, th.rowHeight };

		if (idx == m_highlightIndex) {
			dl.addRectFilled(rowRect, th.accentHovered, th.rounding);
		} else if (idx == m_selectedIndex) {
			dl.addRectFilled(rowRect, th.frameBgActive, th.rounding);
		}

		dl.addTextClipped(ctx.font(), th.fontSize, rowRect.insetXY(th.framePadding, 0.0f), th.text,
		                   m_items[idx], Align::Start, Align::Center);
	}

	if (scrollable) {
		Rect track{ rect.right() - th.scrollbarWidth, rect.y + th.framePadding, th.scrollbarWidth,
		            rect.h - 2.0f * th.framePadding };
		dl.addRectFilled(track, th.scrollbarBg, th.rounding);

		float thumbH = std::max(track.h * (static_cast<float>(visibleCount) / static_cast<float>(total)), 8.0f);
		int maxScroll = total - visibleCount;
		float t = maxScroll > 0 ? static_cast<float>(m_scrollRow) / static_cast<float>(maxScroll) : 0.0f;
		Rect thumb{ track.x, track.y + t * (track.h - thumbH), track.w, thumbH };
		dl.addRectFilled(thumb, th.scrollbarGrab, th.rounding);
	}
}

} // namespace lightGraphics::ui
