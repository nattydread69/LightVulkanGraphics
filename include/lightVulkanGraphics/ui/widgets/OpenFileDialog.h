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

// A reusable "Open" file dialog -- SaveFileDialog's counterpart. Everything it shares
// with SaveFileDialog (the sortable Name/Size/Modified list, folder navigation, search,
// file-type filter, hidden-files toggle, resizable Modal panel) lives in FileDialogBase;
// see that header for the layout and for what is remembered between open() calls.
//
// Open only ever picks an EXISTING file, so there is no filename field: selecting a
// file and pressing Open, double-clicking it, or pressing Enter on it is the whole
// interaction. Open (or Enter/double-click) on a folder goes into it instead.
//
// `directory` and `extensionFilter` are constructor parameters rather than anything
// pose-file-specific, so any lightVulkanGraphics consumer can reuse this for its own
// "load from a file" flow.

#include "FileDialogBase.h"

#include <string>

namespace lightGraphics::ui {

class OpenFileDialog : public FileDialogBase {
public:
	// extensionFilter, e.g. ".pose.json" -- matched case-insensitively against the end
	// of each filename; "" lists every file. setFileTypeFilters() offers several.
	OpenFileDialog(GuiContext& context, std::string directory, std::string extensionFilter);

	// Shows the dialog and (re)scans its folder, clearing any previous selection and
	// search, with the file list focused so the arrow keys and type-ahead work straight
	// away. Safe to call again while already open, e.g. re-clicking "Open...".
	void open();

	// setOnConfirm() fires once, with the full path of the chosen file, on Open/double-
	// click/Enter -- never with nothing selected (Open then just says "Select a file
	// first.") and never for a folder.

protected:
	void onFileActivated(const FileEntry& entry) override;
	void onConfirmClicked() override;
};

} // namespace lightGraphics::ui
