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

// What OpenFileDialog and SaveFileDialog have in common, so the two can't drift apart:
// one Modal|Closable|Movable|Resizable Panel, created once and toggled visible/hidden
// (the About-dialog pattern in docs/gui/05-widgets.md, "Panel", "Modal panels"),
// holding, top to bottom:
//
//   [^] [ /current/folder/path                              ]   navigation row
//   [ Search...                ] [ Files (*.csv)  v ] Hidden [ ]  filter row
//   +-----------------------------------------------------------+
//   | Name ^                       |     Size | Modified         |  FileListView
//   | (folder) data                |          | 2026-10-28 09:12 |
//   | (file)   frame_2.csv         |   1.5 KB | 2026-10-28 09:14 |
//   +-----------------------------------------------------------+
//   ...whatever the derived dialog adds (SaveFileDialog: the filename field)...
//   3 files, 1 folder                                            status line
//                                          [  Open  ] [ Cancel ] buttons
//
// Folders can be entered by double-click/Enter and left with the Up button/Backspace, or
// a path typed into the path field -- unless setNavigationEnabled(false) pins the dialog
// to its starting folder. The file list grows with the dialog when it is resized.
//
// Everything the user can change -- the folder, the sort column and direction, the
// file-type filter, whether hidden files are shown -- is remembered across open()
// calls on the same dialog object, the way native dialogs reopen where you left them.
// Only the search text is cleared on each open().

#include "FileListView.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

class GuiContext;
class Panel;
class Button;
class Checkbox;
class DropDown;
class Label;
class TextBox;

// One entry in the dialog's file-type dropdown.
struct FileTypeFilter {
	std::string label;                     // e.g. "CSV files (*.csv)"
	// Matched case-insensitively against the END of each filename, so multi-part
	// extensions like ".pose.json" work. Empty = every file ("All files").
	std::vector<std::string> extensions;
};

class FileDialogBase {
public:
	virtual ~FileDialogBase();

	FileDialogBase(const FileDialogBase&) = delete;
	FileDialogBase& operator=(const FileDialogBase&) = delete;

	// Fired once, with the full path of the chosen file, when the user confirms. Not
	// fired by Cancel/the title-bar close button/Escape -- see setOnCancel() for those.
	void setOnConfirm(std::function<void(const std::string& fullPath)> onConfirm) { m_onConfirm = std::move(onConfirm); }
	// Fired when the user backs out: Cancel button, the title-bar X, or Escape (all three
	// route through Panel::requestClose()).
	void setOnCancel(std::function<void()> onCancel) { m_onCancel = std::move(onCancel); }

	bool isOpen() const;

	// ---- folder ----
	// The folder currently listed, as an absolute, normalised path.
	const std::string& directory() const { return m_directory; }
	// Lists `directory` (relative paths are resolved against the current working
	// directory). Takes effect immediately if the dialog is open, otherwise at the next
	// open().
	void setDirectory(std::string directory);
	// true (the default): folders are listed and can be entered, and the Up button and
	// path field move around the filesystem. false: the dialog stays in the folder it
	// was given -- folders are not listed and the navigation row is read-only, which
	// suits an application that keeps its files in one fixed place.
	void setNavigationEnabled(bool enabled);
	bool navigationEnabled() const { return m_navigationEnabled; }
	// Moves to the parent folder (no-op at the filesystem root or with navigation
	// disabled), selecting the folder just left so the user can see where they were.
	void navigateUp();
	// Re-reads the current folder from disk, keeping the selection if the entry is
	// still there.
	void refresh();

	// ---- what is listed ----
	// Replaces the file-type dropdown's entries. The constructor's `extensionFilter`
	// seeds a single entry; call this to offer several, e.g. { "Images", {".png",
	// ".jpg"} } plus { "All files", {} }. SaveFileDialog appends the selected filter's
	// FIRST extension to a typed name that has none of them.
	void setFileTypeFilters(std::vector<FileTypeFilter> filters, int selectedIndex = 0);
	const std::vector<FileTypeFilter>& fileTypeFilters() const { return m_filters; }
	int fileTypeFilterIndex() const { return m_filterIndex; }
	void setFileTypeFilterIndex(int index);
	// Case-insensitive substring filter on names (files and folders), the same as typing
	// into the search box.
	void setSearchText(std::string text);
	// Names starting with '.' are hidden unless this is on (the "Hidden" checkbox).
	void setShowHiddenFiles(bool show);
	bool showHiddenFiles() const { return m_showHidden; }
	// Same as clicking a column header: see FileListView::setSort(). Default Name,
	// ascending.
	void setSort(FileSortKey key, bool ascending);

	// ---- parts, for callers that want to restyle or retitle them, and for tests ----
	// The underlying Panel -- dock/anchor it, give it a persistence id, change its title.
	// Never null after construction.
	Panel* panel() const { return m_panel; }
	FileListView* fileList() const { return m_fileList; }
	TextBox* pathField() const { return m_pathField; }
	Button* upButton() const { return m_upButton; }
	TextBox* searchField() const { return m_searchField; }
	DropDown* fileTypeDropDown() const { return m_typeDropDown; }
	Checkbox* hiddenFilesToggle() const { return m_hiddenToggle; }
	Label* statusLabel() const { return m_statusLabel; }
	Button* confirmButton() const { return m_confirmButton; }
	Button* cancelButton() const { return m_cancelButton; }

protected:
	// `extensionFilter` (e.g. ".pose.json"; "" = all files) seeds the file-type dropdown
	// with a single entry. Builds the navigation row, filter row and file list; the
	// derived constructor adds whatever goes beneath the list and then calls
	// addFooter().
	FileDialogBase(GuiContext& context, std::string title, std::string directory,
	               std::string extensionFilter, float height);

	// Adds the status line and the [confirm] [Cancel] button row. Call once, last, from
	// the derived constructor.
	void addFooter(std::string confirmLabel);

	// Rescans, re-centres, shows and raises the dialog. The derived open() wraps this.
	void showDialog();
	// Fires onConfirm with `fullPath`, then hides the dialog WITHOUT going through
	// Panel::requestClose() -- so a successful confirm never also fires onCancel (see
	// Panel::setOnClose's doc comment for why setVisible(false) alone is silent).
	void finish(const std::string& fullPath);

	void setStatus(std::string text, bool isError);
	// The active filter, or nullptr when there is none (every file is shown).
	const FileTypeFilter* activeFilter() const;
	bool matchesTypeFilter(std::string_view filename) const;
	// m_directory joined with `name`, as a UTF-8 string.
	std::string pathInDirectory(std::string_view name) const;
	// Whether `name` exists in the current folder at all / exists and is a folder.
	bool existsInDirectory(std::string_view name) const;
	bool isFolderInDirectory(std::string_view name) const;
	// Moves into `path` (absolute, or relative to the current folder) if it is a folder,
	// selecting `selectName` afterwards when it is non-empty. Returns false, leaving
	// everything unchanged, if it isn't a readable folder.
	bool navigateTo(const std::string& path, std::string_view selectName = {});

	// ---- hooks for the derived dialog ----
	// A FILE was double-clicked or Enter was pressed on it (folders are entered here,
	// before this is ever called).
	virtual void onFileActivated(const FileEntry& entry) = 0;
	// The confirm button (Open/Save) was clicked.
	virtual void onConfirmClicked() = 0;
	virtual void onSelectionChanged(const FileEntry* entry) { (void)entry; }

	GuiContext& m_context;
	Panel* m_panel = nullptr;

private:
	void rescan();
	void applyFilters();
	void updateStatusSummary();
	void syncNavigationControls();
	void rebuildTypeDropDown();

	std::string m_directory;
	std::string m_typedPath;   // the path field's text as of its last commit
	bool m_navigationEnabled = true;
	bool m_showHidden = false;
	std::string m_searchText;
	std::vector<FileTypeFilter> m_filters;
	int m_filterIndex = 0;

	// Everything the last rescan() found, before the type/search/hidden filters --
	// re-filtering on each keystroke in the search box must not touch the disk.
	std::vector<FileEntry> m_scanned;
	std::string m_scanError;

	FileListView* m_fileList = nullptr;
	TextBox* m_pathField = nullptr;
	Button* m_upButton = nullptr;
	TextBox* m_searchField = nullptr;
	DropDown* m_typeDropDown = nullptr;
	Checkbox* m_hiddenToggle = nullptr;
	Label* m_statusLabel = nullptr;
	Button* m_confirmButton = nullptr;
	Button* m_cancelButton = nullptr;

	std::function<void(const std::string&)> m_onConfirm;
	std::function<void()> m_onCancel;
};

} // namespace lightGraphics::ui
