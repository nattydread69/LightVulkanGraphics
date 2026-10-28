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

// docs/gui/05-widgets.md, "FileListView": the sortable, multi-column file table that
// OpenFileDialog and SaveFileDialog are built around -- Name / Size / Modified columns,
// a header row whose cells sort by that column when clicked (clicking the active column
// again reverses it), folders always grouped ahead of files, and a draggable scrollbar.
//
// It only ever DISPLAYS entries it is handed via setEntries(); it never touches the
// filesystem itself. Scanning, filtering and navigation belong to the dialog that owns
// it (FileDialogBase), which keeps this widget headless-testable with made-up entries
// and reusable for listings that don't come from a local directory at all.
//
// Interaction follows native file managers rather than ListBox's release-inside
// convention: a row is selected on PRESS, and a second press on the same row within the
// double-click window activates it (setOnActivate) -- which the dialogs use to open a
// file or enter a folder. Keyboard while focused: Up/Down/PageUp/PageDown/Home/End move
// the selection (no wrap-around), Enter activates, Backspace asks to go up a folder
// (setOnNavigateUp), and typing jumps to the first entry whose name starts with what was
// typed (type-ahead, reset after a short pause).

#include "../Theme.h"
#include "../Widget.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

struct FileEntry {
	std::string   name;
	bool          isDirectory = false;
	std::uint64_t size = 0;           // bytes; not shown for directories
	std::int64_t  modifiedTime = 0;   // seconds since the Unix epoch; 0 = unknown (shown blank)
};

enum class FileSortKey { Name, Size, Modified };

class FileListView : public Widget {
public:
	FileListView();

	// Replaces the listing. Entries are re-sorted by the current sort settings; the
	// selection is cleared and the view scrolled back to the top, since an index into the
	// old listing means nothing in the new one.
	void setEntries(std::vector<FileEntry> entries);
	// In DISPLAY order (i.e. already sorted) -- index i here is row i on screen.
	const std::vector<FileEntry>& entries() const { return m_entries; }

	// -1 = nothing selected.
	int selectedIndex() const { return m_selectedIndex; }
	// nullptr when nothing is selected.
	const FileEntry* selectedEntry() const;
	// index < 0 clears the selection; otherwise clamped to a valid row. Scrolls the
	// selected row into view.
	void setSelectedIndex(int index, bool fireCallback = false);
	// Selects the entry with exactly this name, if there is one. Returns whether it found
	// one; the selection is left untouched if not.
	bool selectName(std::string_view name, bool fireCallback = false);

	// Re-sorts in place, keeping the same ENTRY (not the same row index) selected.
	// Folders stay ahead of files in both directions. Ties -- equal sizes, equal
	// timestamps -- always fall back to ascending name order. The default is
	// Name/ascending.
	void setSort(FileSortKey key, bool ascending);
	FileSortKey sortKey() const { return m_sortKey; }
	bool sortAscending() const { return m_sortAscending; }

	// Fired whenever the selection changes through user interaction (or a setter called
	// with fireCallback = true). nullptr = selection cleared.
	void setOnSelectionChange(std::function<void(const FileEntry*)> fn) { m_onSelectionChange = std::move(fn); }
	// Double-click on a row, or Enter while the list is focused and a row is selected.
	void setOnActivate(std::function<void(const FileEntry&)> fn) { m_onActivate = std::move(fn); }
	// After a header click re-sorts the list -- for an owner that wants to remember the
	// user's preferred order (FileDialogBase keeps it across open() calls).
	void setOnSortChange(std::function<void(FileSortKey, bool ascending)> fn) { m_onSortChange = std::move(fn); }
	// Backspace while the list is focused.
	void setOnNavigateUp(std::function<void()> fn) { m_onNavigateUp = std::move(fn); }

	// Minimum number of rows tall the list is (below its header row). Default 8.
	void setVisibleRows(int rows) { m_visibleRows = rows < 1 ? 1 : rows; }
	// When true, preferredSize() grows the list to take up whatever height the owning
	// Panel has left over after its other widgets -- so a Resizable dialog's list grows
	// and shrinks with the dialog instead of leaving a gap below it. Several fill lists
	// in one panel share the leftover evenly. Never shrinks below setVisibleRows().
	void setFillPanel(bool fill) { m_fillPanel = fill; }
	// Shown, dimmed, in the middle of an empty list. Default "No files".
	void setEmptyText(std::string text) { m_emptyText = std::move(text); }

	// The column header cell for `key`, and the on-screen rect of entries()[index] (empty
	// when that row is scrolled out of view). Valid after a layout pass -- the same
	// "geometry is read back from the last layout" convention every other widget's
	// bounds() follows; exposed so tests can click exact spots.
	Rect headerCellRect(const GuiContext& ctx, FileSortKey key) const;
	Rect rowRect(const GuiContext& ctx, int index) const;
	int firstVisibleRow() const { return m_scrollRow; }

	// "0 B", "512 B", "1.5 KB", "20 KB", "3.2 MB", ... (1024-based).
	static std::string formatSize(std::uint64_t bytes);
	// "2026-10-28 14:03" in local time; "" for 0.
	static std::string formatModifiedTime(std::int64_t unixSeconds);
	// Case-insensitive "natural" order: runs of digits compare by numeric value, so
	// "frame_2" sorts before "frame_10". Returns <0, 0 or >0 like strcmp; only names
	// that are byte-for-byte identical compare equal.
	static int compareNatural(std::string_view a, std::string_view b);

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return true; }
	// Consumes the wheel while hovered so the owning Panel doesn't also scroll underneath
	// it -- same convention as ListBox.
	bool wantsWheel() const override { return true; }

private:
	// Everything geometry depends on, computed once per update()/draw() (it measures
	// text) and handed to the helpers below.
	struct Layout {
		Rect header, list, scrollTrack;
		float nameW = 0.0f, sizeW = 0.0f, dateW = 0.0f;
		float scrollW = 0.0f;   // 0 when everything fits and there is no scrollbar
		int visibleRows = 0;
	};
	enum class PressTarget { None, Header, Row, ScrollThumb, ScrollTrack };

	Layout computeLayout(const GuiContext&) const;
	Rect headerCellIn(const Layout&, FileSortKey key) const;
	Rect rowRectIn(const Layout&, const Theme&, int index) const;
	Rect scrollThumbRect(const Layout&) const;
	int rowAt(const GuiContext&, const Layout&, Vec2 p) const;
	int headerAt(const Layout&, Vec2 p) const;   // FileSortKey as int, or -1
	int maxScroll(int visibleRows) const;
	void clampScroll();
	void scrollToShow(int index);
	void sortEntries();
	void moveSelection(int delta);   // clamps at both ends -- no wrap-around
	void activateSelected();
	void handleTypeAhead(const std::vector<std::uint32_t>& chars);
	float fillHeight(const GuiContext&) const;

	std::vector<FileEntry> m_entries;
	int m_selectedIndex = -1;
	int m_scrollRow = 0;
	int m_visibleRows = 8;
	// Rows that actually fit at the last layout -- what scrolling clamps against. Kept
	// separately from m_visibleRows (the requested minimum) because a fill-panel list
	// shows more than that, and setters like setSelectedIndex() run without a ctx to
	// recompute it from.
	int m_viewRows = 8;
	float m_wheelAccum = 0.0f;
	bool m_fillPanel = false;
	std::string m_emptyText = "No files";

	FileSortKey m_sortKey = FileSortKey::Name;
	bool m_sortAscending = true;

	// Press bookkeeping -- what the current capture started on, so release/drag know
	// whether they are finishing a header click, a row click or a scrollbar drag.
	PressTarget m_pressTarget = PressTarget::None;
	int m_pressHeader = -1;
	float m_dragStartMouseY = 0.0f;
	int m_dragStartScroll = 0;

	// Double-click and type-ahead timing, accumulated from InputState::deltaTime -- the
	// same self-clocked approach TextBox's multi-click detection uses, so no global
	// clock is needed.
	float m_time = 0.0f;
	float m_lastClickTime = -1000.0f;
	int m_lastClickIndex = -1;
	std::string m_typeAhead;
	float m_lastTypeTime = -1000.0f;

	std::function<void(const FileEntry*)> m_onSelectionChange;
	std::function<void(const FileEntry&)> m_onActivate;
	std::function<void(FileSortKey, bool)> m_onSortChange;
	std::function<void()> m_onNavigateUp;
};

} // namespace lightGraphics::ui
