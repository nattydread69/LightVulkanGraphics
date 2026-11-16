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

// A reusable "choose a folder" dialog, OpenFileDialog's counterpart for folders. It is a
// FileDialogBase (see that header for the layout, navigation, search, hidden-files
// toggle and what is remembered between open() calls) that lists only folders.
//
// Double-click or Enter on a folder goes into it; the up button, Backspace or a path
// typed into the path field move elsewhere. "Select" confirms the folder highlighted in
// the list, or -- when none is highlighted -- the folder being listed, so the usual
// flow is: navigate to the folder you want and press Select.
//
// setOnConfirm() receives the absolute, normalised path of the chosen folder.

#include "FileDialogBase.h"

#include <string>

namespace lightGraphics::ui {

class OpenFolderDialog : public FileDialogBase {
public:
	OpenFolderDialog(GuiContext& context, std::string directory);

	// Shows the dialog and (re)scans its folder, clearing any previous selection and
	// search. Safe to call again while already open.
	void open();

protected:
	void onFileActivated(const FileEntry& entry) override;
	void onConfirmClicked() override;
};

} // namespace lightGraphics::ui
