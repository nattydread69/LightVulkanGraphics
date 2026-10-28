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

#include <lightVulkanGraphics/ui/widgets/FileListView.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "../Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace lightGraphics::ui {

namespace {
	constexpr float kDoubleClickSeconds = 0.4f;
	// Type-ahead buffer resets after this long without a keystroke -- long enough to
	// type a word, short enough that a second search starts fresh.
	constexpr float kTypeAheadResetSeconds = 1.0f;
	constexpr float kMinNameColumnWidth = 120.0f;

	// The amber every desktop uses for folders reads on the dark, light and
	// high-contrast themes alike, so it isn't a Theme colour.
	constexpr Color kFolderColor{ 0xE3, 0xB3, 0x4F, 0xFF };

	// Widest text each column normally has to hold, measured once per layout so the
	// columns follow the font size instead of hard-coding pixel widths.
	constexpr std::string_view kSizeSample = "000.0 MB";
	constexpr std::string_view kDateSample = "0000-00-00 00:00";

	char asciiLower(char c) {
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	}

	bool isDigit(char c) {
		return c >= '0' && c <= '9';
	}

	bool startsWithIgnoreCase(std::string_view text, std::string_view prefix) {
		if (prefix.size() > text.size()) {
			return false;
		}
		for (std::size_t i = 0; i < prefix.size(); ++i) {
			if (asciiLower(text[i]) != asciiLower(prefix[i])) {
				return false;
			}
		}
		return true;
	}

	void drawFolderIcon(DrawList& dl, const Rect& r) {
		float s = r.w;
		dl.addRectFilled({ r.x, r.y + s * 0.14f, s * 0.42f, s * 0.2f }, kFolderColor, 1.5f);
		dl.addRectFilled({ r.x, r.y + s * 0.26f, s, s * 0.6f }, kFolderColor, 1.5f);
	}

	void drawFileIcon(DrawList& dl, const Rect& r, Color page, Color fold) {
		float s = r.w;
		float w = s * 0.72f;
		float h = s * 0.88f;
		float x = r.x + (s - w) * 0.5f;
		float y = r.y + s * 0.06f;
		float f = w * 0.34f;
		Vec2 pagePts[5] = { { x, y }, { x + w - f, y }, { x + w, y + f }, { x + w, y + h }, { x, y + h } };
		dl.addConvexPolyFilled(pagePts, 5, page);
		dl.addTriangleFilled({ x + w - f, y }, { x + w - f, y + f }, { x + w, y + f }, fold);
	}

	// Black or white, whichever reads better on `bg`. The selected row's fill is the
	// theme's accent, and themes don't promise their text colour works on it -- the
	// high-contrast theme's white text on its yellow accent is unreadable.
	Color readableOn(Color bg) {
		float const luma = (0.299f * bg.r + 0.587f * bg.g + 0.114f * bg.b) / 255.0f;
		return luma > 0.6f ? Color{ 0x00, 0x00, 0x00, 0xFF } : Color{ 0xFF, 0xFF, 0xFF, 0xFF };
	}

	// Small filled triangle pointing up (ascending) or down (descending).
	void drawSortArrow(DrawList& dl, Vec2 centre, float size, bool ascending, Color color) {
		float hw = size * 0.5f;
		float hh = size * 0.3f;
		if (ascending) {
			dl.addTriangleFilled({ centre.x - hw, centre.y + hh }, { centre.x + hw, centre.y + hh },
			                     { centre.x, centre.y - hh }, color);
		} else {
			dl.addTriangleFilled({ centre.x - hw, centre.y - hh }, { centre.x, centre.y + hh },
			                     { centre.x + hw, centre.y - hh }, color);
		}
	}
}

FileListView::FileListView() = default;

// ---- static helpers ----------------------------------------------------------------

std::string FileListView::formatSize(std::uint64_t bytes) {
	static constexpr const char* kUnits[] = { "B", "KB", "MB", "GB", "TB", "PB" };
	if (bytes < 1024) {
		return std::to_string(bytes) + " B";
	}
	double value = static_cast<double>(bytes);
	std::size_t unit = 0;
	while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
		value /= 1024.0;
		++unit;
	}
	// One decimal place while it still carries information ("1.5 KB"), none once the
	// number is big enough that it doesn't ("20 KB", not "20.0 KB").
	char buf[32];
	if (value < 10.0) {
		std::snprintf(buf, sizeof(buf), "%.1f %s", value, kUnits[unit]);
	} else {
		std::snprintf(buf, sizeof(buf), "%.0f %s", value, kUnits[unit]);
	}
	return buf;
}

std::string FileListView::formatModifiedTime(std::int64_t unixSeconds) {
	if (unixSeconds == 0) {
		return {};
	}
	std::time_t const t = static_cast<std::time_t>(unixSeconds);
	std::tm local{};
#if defined(_WIN32)
	if (localtime_s(&local, &t) != 0) {
		return {};
	}
#else
	if (localtime_r(&t, &local) == nullptr) {
		return {};
	}
#endif
	char buf[32];
	if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local) == 0) {
		return {};
	}
	return buf;
}

int FileListView::compareNatural(std::string_view a, std::string_view b) {
	std::size_t i = 0, j = 0;
	while (i < a.size() && j < b.size()) {
		if (isDigit(a[i]) && isDigit(b[j])) {
			// Compare the two digit runs by value: skip leading zeros, then the longer
			// run is the bigger number, and equal lengths compare digit by digit.
			std::size_t ia = i, jb = j;
			while (ia < a.size() && a[ia] == '0') { ++ia; }
			while (jb < b.size() && b[jb] == '0') { ++jb; }
			std::size_t ea = ia, eb = jb;
			while (ea < a.size() && isDigit(a[ea])) { ++ea; }
			while (eb < b.size() && isDigit(b[eb])) { ++eb; }
			if (ea - ia != eb - jb) {
				return (ea - ia) < (eb - jb) ? -1 : 1;
			}
			for (std::size_t k = 0; k < ea - ia; ++k) {
				if (a[ia + k] != b[jb + k]) {
					return a[ia + k] < b[jb + k] ? -1 : 1;
				}
			}
			i = ea;
			j = eb;
			continue;
		}
		char ca = asciiLower(a[i]);
		char cb = asciiLower(b[j]);
		if (ca != cb) {
			return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb) ? -1 : 1;
		}
		++i;
		++j;
	}
	if (i < a.size() || j < b.size()) {
		return i < a.size() ? 1 : -1;   // the one with characters left over is longer
	}
	// Equal ignoring case and leading zeros ("a.txt" vs "A.txt", "f1" vs "f01"): fall
	// back to plain byte order so the result is still a strict, deterministic order.
	int const raw = a.compare(b);
	return raw < 0 ? -1 : (raw > 0 ? 1 : 0);
}

// ---- entries, selection, sorting ---------------------------------------------------

void FileListView::setEntries(std::vector<FileEntry> entries) {
	m_entries = std::move(entries);
	m_selectedIndex = -1;
	m_scrollRow = 0;
	m_lastClickIndex = -1;
	sortEntries();
}

const FileEntry* FileListView::selectedEntry() const {
	if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_entries.size())) {
		return nullptr;
	}
	return &m_entries[static_cast<std::size_t>(m_selectedIndex)];
}

void FileListView::setSelectedIndex(int index, bool fireCallback) {
	if (index < 0 || m_entries.empty()) {
		m_selectedIndex = -1;
	} else {
		m_selectedIndex = std::clamp(index, 0, static_cast<int>(m_entries.size()) - 1);
		scrollToShow(m_selectedIndex);
	}
	if (fireCallback && m_onSelectionChange) {
		m_onSelectionChange(selectedEntry());
	}
}

bool FileListView::selectName(std::string_view name, bool fireCallback) {
	for (std::size_t i = 0; i < m_entries.size(); ++i) {
		if (m_entries[i].name == name) {
			setSelectedIndex(static_cast<int>(i), fireCallback);
			return true;
		}
	}
	return false;
}

void FileListView::setSort(FileSortKey key, bool ascending) {
	m_sortKey = key;
	m_sortAscending = ascending;
	sortEntries();
}

void FileListView::sortEntries() {
	// Remember WHICH entry was selected, not its row: after re-sorting it is somewhere
	// else, and the user expects the same file to stay highlighted.
	std::string selectedName;
	bool selectedIsDir = false;
	bool hadSelection = selectedEntry() != nullptr;
	if (hadSelection) {
		selectedName = selectedEntry()->name;
		selectedIsDir = selectedEntry()->isDirectory;
	}

	FileSortKey const key = m_sortKey;
	bool const ascending = m_sortAscending;
	std::stable_sort(m_entries.begin(), m_entries.end(), [key, ascending](const FileEntry& a, const FileEntry& b) {
		// Folders first in BOTH directions -- reversing a sort should reorder the files,
		// not bury every folder at the bottom.
		if (a.isDirectory != b.isDirectory) {
			return a.isDirectory;
		}
		int c = 0;
		switch (key) {
		case FileSortKey::Name:
			c = compareNatural(a.name, b.name);
			break;
		case FileSortKey::Size:
			// Folders have no meaningful size; leave them in name order.
			if (!a.isDirectory && a.size != b.size) {
				c = a.size < b.size ? -1 : 1;
			}
			break;
		case FileSortKey::Modified:
			if (a.modifiedTime != b.modifiedTime) {
				c = a.modifiedTime < b.modifiedTime ? -1 : 1;
			}
			break;
		}
		if (!ascending) {
			c = -c;
		}
		if (c == 0 && key != FileSortKey::Name) {
			c = compareNatural(a.name, b.name);   // ties: always A to Z
		}
		return c < 0;
	});

	// The pending double-click remembers a row INDEX, which now names a different entry
	// -- without this, click, re-sort, click again could open a file never clicked twice.
	m_lastClickIndex = -1;

	m_selectedIndex = -1;
	if (hadSelection) {
		for (std::size_t i = 0; i < m_entries.size(); ++i) {
			if (m_entries[i].name == selectedName && m_entries[i].isDirectory == selectedIsDir) {
				m_selectedIndex = static_cast<int>(i);
				scrollToShow(m_selectedIndex);
				break;
			}
		}
	}
}

// ---- geometry ----------------------------------------------------------------------

FileListView::Layout FileListView::computeLayout(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	const Font& font = ctx.font();
	Layout l;
	l.header = { m_bounds.x, m_bounds.y, m_bounds.w, th.rowHeight };
	l.list = { m_bounds.x, m_bounds.y + th.rowHeight, m_bounds.w, std::max(0.0f, m_bounds.h - th.rowHeight) };
	l.visibleRows = std::max(1, static_cast<int>((l.list.h - 2.0f * th.framePadding) / th.rowHeight));

	bool const scrollable = static_cast<int>(m_entries.size()) > l.visibleRows;
	l.scrollW = scrollable ? th.scrollbarWidth : 0.0f;
	if (scrollable) {
		l.scrollTrack = { l.list.right() - th.scrollbarWidth - 1.0f, l.list.y + th.framePadding,
		                  th.scrollbarWidth, l.list.h - 2.0f * th.framePadding };
	}

	float const cellPad = 2.0f * th.framePadding;
	l.sizeW = font.measureText(kSizeSample, th.fontSize, TextFlags::Tabular).x + 2.0f * cellPad;
	l.dateW = font.measureText(kDateSample, th.fontSize, TextFlags::Tabular).x + 2.0f * cellPad;
	float const usable = m_bounds.w - l.scrollW;
	// In a narrow list give Name the room: drop Modified first, then Size, rather than
	// squeezing every column until nothing in any of them is readable.
	if (usable - l.sizeW - l.dateW < kMinNameColumnWidth) {
		l.dateW = 0.0f;
	}
	if (usable - l.sizeW - l.dateW < kMinNameColumnWidth) {
		l.sizeW = 0.0f;
	}
	l.nameW = std::max(0.0f, usable - l.sizeW - l.dateW);
	return l;
}

Rect FileListView::headerCellRect(const GuiContext& ctx, FileSortKey key) const {
	return headerCellIn(computeLayout(ctx), key);
}

Rect FileListView::rowRect(const GuiContext& ctx, int index) const {
	return rowRectIn(computeLayout(ctx), ctx.theme(), index);
}

Rect FileListView::headerCellIn(const Layout& l, FileSortKey key) const {
	switch (key) {
	case FileSortKey::Name:
		return { l.header.x, l.header.y, l.nameW, l.header.h };
	case FileSortKey::Size:
		return { l.header.x + l.nameW, l.header.y, l.sizeW, l.header.h };
	case FileSortKey::Modified:
		// The last column's header runs on over the scrollbar, so the header strip has
		// no dead cell at its right end.
		return { l.header.x + l.nameW + l.sizeW, l.header.y, l.dateW + l.scrollW, l.header.h };
	}
	return {};
}

Rect FileListView::rowRectIn(const Layout& l, const Theme& th, int index) const {
	int const row = index - m_scrollRow;
	if (index < 0 || index >= static_cast<int>(m_entries.size()) || row < 0 || row >= l.visibleRows) {
		return {};
	}
	return { l.list.x + th.framePadding * 0.5f,
	         l.list.y + th.framePadding + static_cast<float>(row) * th.rowHeight,
	         l.list.w - l.scrollW - th.framePadding,
	         th.rowHeight };
}

Rect FileListView::scrollThumbRect(const Layout& l) const {
	if (l.scrollTrack.empty()) {
		return {};
	}
	float const total = static_cast<float>(m_entries.size());
	float const thumbH = std::min(l.scrollTrack.h,
		std::max(l.scrollTrack.h * (static_cast<float>(l.visibleRows) / total), 16.0f));
	int const maxRow = maxScroll(l.visibleRows);
	float const t = maxRow > 0 ? static_cast<float>(m_scrollRow) / static_cast<float>(maxRow) : 0.0f;
	return { l.scrollTrack.x, l.scrollTrack.y + t * (l.scrollTrack.h - thumbH), l.scrollTrack.w, thumbH };
}

int FileListView::rowAt(const GuiContext& ctx, const Layout& l, Vec2 p) const {
	const Theme& th = ctx.theme();
	if (!l.list.contains(p)) {
		return -1;
	}
	float const localY = p.y - l.list.y - th.framePadding;
	if (localY < 0.0f) {
		return -1;
	}
	int const row = static_cast<int>(localY / th.rowHeight);
	if (row >= l.visibleRows) {
		return -1;
	}
	int const idx = m_scrollRow + row;
	return idx < static_cast<int>(m_entries.size()) ? idx : -1;
}

int FileListView::headerAt(const Layout& l, Vec2 p) const {
	for (FileSortKey key : { FileSortKey::Name, FileSortKey::Size, FileSortKey::Modified }) {
		Rect const cell = headerCellIn(l, key);
		if (!cell.empty() && cell.contains(p)) {
			return static_cast<int>(key);
		}
	}
	return -1;
}

int FileListView::maxScroll(int visibleRows) const {
	return std::max(0, static_cast<int>(m_entries.size()) - visibleRows);
}

void FileListView::clampScroll() {
	m_scrollRow = std::clamp(m_scrollRow, 0, maxScroll(m_viewRows));
}

void FileListView::scrollToShow(int index) {
	if (index < 0) {
		return;
	}
	if (index < m_scrollRow) {
		m_scrollRow = index;
	} else if (index >= m_scrollRow + m_viewRows) {
		m_scrollRow = index - m_viewRows + 1;
	}
	clampScroll();
}

float FileListView::fillHeight(const GuiContext& ctx) const {
	// Only meaningful as a direct child of a Panel -- inside a composite the composite
	// decides the height, not the panel.
	if (m_panel == nullptr || m_parent != nullptr) {
		return 0.0f;
	}
	const Theme& th = ctx.theme();
	const Panel& owner = *m_panel;
	float const titleH = hasFlag(owner.flags(), PanelFlags::NoTitleBar) ? 0.0f : th.titleBarHeight;
	// Mirrors Panel::layout()'s vertical stack: windowPadding above and below, and
	// itemSpacing after every visible widget. Counting that trailing spacing too keeps
	// the total just inside Panel::scrollableSpan(), so a Scrollable panel never sprouts
	// a scrollbar for the sake of a few pixels.
	//
	// Every fill-mode FileListView in the panel (this one included) is left out of the
	// sum and the leftover is split evenly between them. Asking another fill list for its
	// preferredSize() instead would ask it for OURS in turn -- endless recursion.
	float avail = owner.bounds().h - titleH - 2.0f * th.windowPadding;
	int fillers = 0;
	for (std::size_t i = 0; i < owner.widgetCount(); ++i) {
		const Widget* w = owner.widgetAt(i);
		if (!w->visible() || w->hasBoundsOverride()) {
			continue;
		}
		avail -= th.itemSpacing;
		const auto* list = dynamic_cast<const FileListView*>(w);
		if (list && list->m_fillPanel) {
			++fillers;
			continue;
		}
		avail -= w->preferredSize(ctx).y;
	}
	return fillers > 1 ? avail / static_cast<float>(fillers) : avail;
}

Vec2 FileListView::preferredSize(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	float h = th.rowHeight + static_cast<float>(m_visibleRows) * th.rowHeight + 2.0f * th.framePadding;
	if (m_fillPanel) {
		h = std::max(h, std::floor(fillHeight(ctx)));
	}
	return { 0.0f, h };
}

// ---- interaction -------------------------------------------------------------------

void FileListView::moveSelection(int delta) {
	if (m_entries.empty()) {
		return;
	}
	int const last = static_cast<int>(m_entries.size()) - 1;
	int next;
	if (m_selectedIndex < 0) {
		next = delta > 0 ? 0 : last;
	} else {
		next = std::clamp(m_selectedIndex + delta, 0, last);
	}
	if (next != m_selectedIndex) {
		setSelectedIndex(next, true);
	}
}

void FileListView::activateSelected() {
	const FileEntry* entry = selectedEntry();
	if (entry && m_onActivate) {
		// Copy first: the callback commonly replaces the listing (entering a folder),
		// which would leave a reference into m_entries dangling mid-call.
		FileEntry const chosen = *entry;
		m_onActivate(chosen);
	}
}

void FileListView::handleTypeAhead(const std::vector<std::uint32_t>& chars) {
	bool typed = false;
	for (std::uint32_t cp : chars) {
		if (cp < 0x20 || cp == 0x7F) {
			continue;
		}
		if (m_time - m_lastTypeTime > kTypeAheadResetSeconds) {
			m_typeAhead.clear();
		}
		m_lastTypeTime = m_time;
		utf8Encode(cp, m_typeAhead);
		typed = true;
	}
	if (!typed || m_typeAhead.empty()) {
		return;
	}
	for (std::size_t i = 0; i < m_entries.size(); ++i) {
		if (startsWithIgnoreCase(m_entries[i].name, m_typeAhead)) {
			if (static_cast<int>(i) != m_selectedIndex) {
				setSelectedIndex(static_cast<int>(i), true);
			}
			return;
		}
	}
}

void FileListView::update(GuiContext& ctx) {
	const InputState& in = ctx.input();
	m_time += in.deltaTime;
	if (!m_enabled) {
		return;
	}

	Layout const l = computeLayout(ctx);
	m_viewRows = l.visibleRows;
	clampScroll();

	const int leftIdx = static_cast<int>(MouseButton::Left);
	Vec2 const mouse = in.mousePos;
	bool const captured = ctx.activeId() == id();

	if (captured && in.mousePressed[leftIdx]) {
		m_pressTarget = PressTarget::None;
		Rect const thumb = scrollThumbRect(l);
		if (!l.scrollTrack.empty() && l.scrollTrack.contains(mouse)) {
			if (thumb.contains(mouse)) {
				m_pressTarget = PressTarget::ScrollThumb;
				m_dragStartMouseY = mouse.y;
				m_dragStartScroll = m_scrollRow;
			} else {
				// Click in the track above/below the thumb: page, like every native list.
				m_pressTarget = PressTarget::ScrollTrack;
				m_scrollRow += (mouse.y < thumb.y ? -1 : 1) * l.visibleRows;
				clampScroll();
			}
		} else if (l.header.contains(mouse)) {
			m_pressTarget = PressTarget::Header;
			m_pressHeader = headerAt(l, mouse);
		} else if (l.list.contains(mouse)) {
			m_pressTarget = PressTarget::Row;
			int const idx = rowAt(ctx, l, mouse);
			if (idx < 0) {
				// Empty space below the last row: deselect, as file managers do.
				if (m_selectedIndex >= 0) {
					setSelectedIndex(-1, true);
				}
				m_lastClickIndex = -1;
			} else {
				bool const isDoubleClick = idx == m_lastClickIndex &&
					(m_time - m_lastClickTime) < kDoubleClickSeconds;
				if (idx != m_selectedIndex) {
					setSelectedIndex(idx, true);
				}
				if (isDoubleClick) {
					// Reset so a third quick click starts a new pair rather than
					// activating twice.
					m_lastClickIndex = -1;
					m_lastClickTime = -1000.0f;
					activateSelected();
					return;
				}
				m_lastClickIndex = idx;
				m_lastClickTime = m_time;
			}
		}
	}

	if (captured && m_pressTarget == PressTarget::ScrollThumb && in.mouseDown[leftIdx]) {
		Rect const thumb = scrollThumbRect(l);
		float const travel = l.scrollTrack.h - thumb.h;
		if (travel > 0.0f) {
			float const rowsPerPixel = static_cast<float>(maxScroll(l.visibleRows)) / travel;
			m_scrollRow = m_dragStartScroll +
				static_cast<int>(std::lround((mouse.y - m_dragStartMouseY) * rowsPerPixel));
			clampScroll();
		}
	}

	if (captured && in.mouseReleased[leftIdx]) {
		// Release-inside the SAME header cell the press started in, like Button: a press
		// dragged off the header cancels instead of sorting by whatever it ended over.
		if (m_pressTarget == PressTarget::Header && m_pressHeader >= 0 && headerAt(l, mouse) == m_pressHeader) {
			FileSortKey const key = static_cast<FileSortKey>(m_pressHeader);
			// Same column: flip it. New column: Name starts A to Z, while Size and
			// Modified start largest/newest first -- what you almost always want when
			// you reach for those columns at all.
			bool const ascending = (key == m_sortKey) ? !m_sortAscending : (key == FileSortKey::Name);
			setSort(key, ascending);
			if (m_onSortChange) {
				m_onSortChange(key, ascending);
			}
		}
		m_pressTarget = PressTarget::None;
		m_pressHeader = -1;
	}

	if (ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (!ev.pressed) {
				continue;
			}
			switch (ev.key) {
			case Key::Up:       moveSelection(-1); break;
			case Key::Down:     moveSelection(1); break;
			case Key::PageUp:   moveSelection(-l.visibleRows); break;
			case Key::PageDown: moveSelection(l.visibleRows); break;
			case Key::Home:
				if (!m_entries.empty() && m_selectedIndex != 0) {
					setSelectedIndex(0, true);
				}
				break;
			case Key::End:
				if (!m_entries.empty() && m_selectedIndex != static_cast<int>(m_entries.size()) - 1) {
					setSelectedIndex(static_cast<int>(m_entries.size()) - 1, true);
				}
				break;
			case Key::Enter:
				if (!ev.repeat) {
					activateSelected();
					return;   // the listing may have just been replaced
				}
				break;
			case Key::Backspace:
				if (m_onNavigateUp) {
					m_onNavigateUp();
					return;
				}
				break;
			default:
				break;
			}
		}
		handleTypeAhead(in.charQueue);
	}

	if (ctx.hoveredId() == id() && in.wheelDelta != 0.0f) {
		// Accumulate rather than truncate each event, so a trackpad's many small deltas
		// still add up to a scroll (ListBox/LogView drop anything under one notch).
		m_wheelAccum += in.wheelDelta * 3.0f;
		int const rows = static_cast<int>(m_wheelAccum);
		m_wheelAccum -= static_cast<float>(rows);
		m_scrollRow -= rows;
		clampScroll();
	}
}

// ---- drawing -----------------------------------------------------------------------

void FileListView::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	const Font& font = ctx.font();
	Layout const l = computeLayout(ctx);
	bool const isEnabled = effectivelyEnabled();
	bool const focused = ctx.focusedId() == id();
	Vec2 const mouse = ctx.input().mousePos;
	bool const hoveredHere = ctx.hoveredId() == id();
	float const cellPad = 2.0f * th.framePadding;

	dl.addRectFilled(m_bounds, th.frameBg, th.rounding);

	// ---- header ----
	// frameBgActive rather than a title-bar colour: it is a fill every theme already draws
	// th.text on (buttons, pressed frames), which titleBgActive is not -- it is white in
	// the high-contrast theme.
	Color const headerBg = th.frameBgActive;
	dl.addRectFilled(l.header, headerBg, th.rounding);
	int const hoveredHeader = (hoveredHere && ctx.activeId() == kInvalidWidgetId) ? headerAt(l, mouse) : -1;
	static constexpr const char* kHeaderLabels[] = { "Name", "Size", "Modified" };
	for (FileSortKey key : { FileSortKey::Name, FileSortKey::Size, FileSortKey::Modified }) {
		Rect const cell = headerCellIn(l, key);
		if (cell.empty()) {
			continue;
		}
		bool const pressed = ctx.activeId() == id() && m_pressTarget == PressTarget::Header &&
			m_pressHeader == static_cast<int>(key);
		if (pressed) {
			dl.addRectFilled(cell, Color::lerp(headerBg, th.text, 0.16f));
		} else if (hoveredHeader == static_cast<int>(key)) {
			dl.addRectFilled(cell, Color::lerp(headerBg, th.text, 0.08f));
		}
		if (cell.x > l.header.x) {
			dl.addLine({ cell.x, cell.y + 4.0f }, { cell.x, cell.bottom() - 4.0f }, th.border);
		}
		bool const active = key == m_sortKey;
		Color const labelColor = !isEnabled ? th.textDisabled : (active ? th.text : Color::lerp(th.text, th.textDisabled, 0.35f));
		float const arrowSpace = th.fontSize;
		Rect const labelRect{ cell.x + cellPad, cell.y, std::max(0.0f, cell.w - 2.0f * cellPad - arrowSpace), cell.h };
		dl.addTextClipped(font, th.fontSize, labelRect, labelColor, kHeaderLabels[static_cast<int>(key)],
		                  Align::Start, Align::Center);
		if (active) {
			float const labelW = font.measureText(kHeaderLabels[static_cast<int>(key)], th.fontSize).x;
			float const arrowX = std::min(labelRect.x + labelW + arrowSpace * 0.6f, cell.right() - arrowSpace * 0.6f);
			drawSortArrow(dl, { arrowX, cell.y + cell.h * 0.5f }, th.fontSize * 0.55f, m_sortAscending,
			              isEnabled ? th.accent : th.textDisabled);
		}
	}
	dl.addLine({ l.header.x, l.header.bottom() }, { l.header.right(), l.header.bottom() }, th.border);

	// ---- rows ----
	int const total = static_cast<int>(m_entries.size());
	int const hoveredRow = (hoveredHere && ctx.activeId() == kInvalidWidgetId) ? rowAt(ctx, l, mouse) : -1;
	Color const textColor = isEnabled ? th.text : th.textDisabled;
	Color const dimText = isEnabled ? Color::lerp(th.text, th.textDisabled, 0.45f) : th.textDisabled;
	Color const stripe = Color::lerp(th.frameBg, th.frameBgHovered, 0.35f);
	Color const pageColor = Color::lerp(th.textDisabled, th.text, 0.45f);
	float const iconSize = std::round(th.fontSize * 1.05f);

	for (int row = 0; row < l.visibleRows; ++row) {
		int const idx = m_scrollRow + row;
		if (idx >= total) {
			break;
		}
		const FileEntry& entry = m_entries[static_cast<std::size_t>(idx)];
		Rect const r = rowRectIn(l, th, idx);

		bool const selected = idx == m_selectedIndex;
		Color const selectedBg = isEnabled ? th.accentActive : th.frameBgActive;
		if (selected) {
			dl.addRectFilled(r, selectedBg, th.rounding);
		} else if (idx == hoveredRow) {
			dl.addRectFilled(r, th.frameBgHovered, th.rounding);
		} else if (idx % 2 == 1) {
			dl.addRectFilled(r, stripe, th.rounding);
		}

		Rect const icon{ r.x + th.framePadding, r.y + std::round((r.h - iconSize) * 0.5f), iconSize, iconSize };
		if (entry.isDirectory) {
			drawFolderIcon(dl, icon);
		} else {
			drawFileIcon(dl, icon, pageColor, th.textDisabled);
		}

		float const nameX = icon.right() + th.framePadding * 1.5f;
		Rect const nameRect{ nameX, r.y, std::max(0.0f, l.list.x + l.nameW - nameX - th.framePadding), r.h };
		Color const nameColor = (selected && isEnabled) ? readableOn(selectedBg) : textColor;
		dl.addTextClipped(font, th.fontSize, nameRect, nameColor, entry.name, Align::Start, Align::Center);

		Color const detailColor = selected ? nameColor : dimText;
		if (l.sizeW > 0.0f && !entry.isDirectory) {
			Rect const sizeRect{ l.list.x + l.nameW + cellPad, r.y, l.sizeW - 2.0f * cellPad, r.h };
			dl.addTextClipped(font, th.fontSize, sizeRect, detailColor, formatSize(entry.size),
			                  Align::End, Align::Center, TextFlags::Tabular);
		}
		if (l.dateW > 0.0f) {
			Rect const dateRect{ l.list.x + l.nameW + l.sizeW + cellPad, r.y, l.dateW - 2.0f * cellPad, r.h };
			dl.addTextClipped(font, th.fontSize, dateRect, detailColor, formatModifiedTime(entry.modifiedTime),
			                  Align::Start, Align::Center, TextFlags::Tabular);
		}
	}

	if (total == 0 && !m_emptyText.empty()) {
		dl.addTextClipped(font, th.fontSize, l.list, th.textDisabled, m_emptyText, Align::Center, Align::Center);
	}

	// ---- scrollbar ----
	if (!l.scrollTrack.empty()) {
		dl.addRectFilled(l.scrollTrack, th.scrollbarBg, th.rounding);
		bool const dragging = ctx.activeId() == id() && m_pressTarget == PressTarget::ScrollThumb;
		Rect const thumb = scrollThumbRect(l);
		bool const thumbHovered = hoveredHere && ctx.activeId() == kInvalidWidgetId && thumb.contains(mouse);
		Color const thumbColor = dragging ? th.accentActive : (thumbHovered ? th.frameBgActive : th.scrollbarGrab);
		dl.addRectFilled(thumb, thumbColor, th.rounding);
	}

	Color const borderColor = (isEnabled && focused) ? th.accent : th.border;
	dl.addRect(m_bounds, borderColor, 1.0f, th.rounding);
}

} // namespace lightGraphics::ui
