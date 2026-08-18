#include <lightVulkanGraphics/ui/widgets/ListBox.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>

namespace lightGraphics::ui {

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

	// Release-inside, same convention Button/DropDown's popup rows use: press-then-
	// drag-away cancels rather than selecting whatever the cursor happens to end up on.
	if (ctx.activeId() == id() && in.mouseReleased[leftIdx] && list.contains(in.mousePos)) {
		int idx = itemAtY(ctx, list, in.mousePos.y);
		if (idx >= 0) {
			setSelectedIndex(idx, true);
		}
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
