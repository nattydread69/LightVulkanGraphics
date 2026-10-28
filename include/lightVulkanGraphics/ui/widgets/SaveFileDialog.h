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

// A reusable "Save As" file dialog. Everything it shares with OpenFileDialog (the
// sortable Name/Size/Modified list, folder navigation, search, file-type filter,
// hidden-files toggle, resizable Modal panel) lives in FileDialogBase; see that header
// for the layout and for what is remembered between open() calls.
//
// Below the list sits a "File name" field: clicking an existing file copies its name in
// (for overwrite awareness), and Save or Enter confirms. The selected file type's first
// extension is appended to a name that has none of that filter's extensions. Saving
// over an existing file asks first, in a small "Replace file?" modal on top of this
// one, unless setConfirmOverwrite(false). A name that matches a folder in the current
// folder goes into that folder rather than saving.
//
// `directory` and `extensionFilter` are constructor parameters rather than anything
// pose-file-specific, so any lightVulkanGraphics consumer can reuse this for its own
// "export to a new file" flow.

#include "FileDialogBase.h"

#include <string>

namespace lightGraphics::ui {

class SaveFileDialog : public FileDialogBase {
public:
	// extensionFilter, e.g. ".pose.json" -- matched case-insensitively against the end
	// of each filename for the listing, and appended to whatever the user types if they
	// didn't already type it themselves. "" lists every file and appends nothing.
	SaveFileDialog(GuiContext& context, std::string directory, std::string extensionFilter);

	// Shows the dialog and (re)scans its folder, with an empty, focused filename field
	// -- type and press Enter. Safe to call again while already open, e.g. re-clicking
	// "Save As...".
	void open();
	// Same, with the filename field prefilled (and selected, so typing replaces it) --
	// e.g. the document's current name.
	void open(std::string suggestedFilename);

	// Whether saving over an existing file first asks "Replace file?". Default true.
	void setConfirmOverwrite(bool confirm) { m_confirmOverwrite = confirm; }
	bool confirmOverwrite() const { return m_confirmOverwrite; }

	// setOnConfirm() fires once, with the full chosen path, when the user confirms --
	// after the overwrite question, if one was asked and answered "Replace".

	TextBox* filenameField() const { return m_filenameField; }
	// The "Replace file?" confirmation panel (hidden except while it is asking) and its
	// two buttons -- for tests, and for callers that want to retitle them. The Cancel
	// button is focused while it asks, so Enter keeps the existing file.
	Panel* overwritePanel() const { return m_overwritePanel; }
	Button* replaceButton() const { return m_replaceButton; }
	Button* keepExistingButton() const { return m_keepButton; }

protected:
	void onFileActivated(const FileEntry& entry) override;
	void onConfirmClicked() override;
	void onSelectionChanged(const FileEntry* entry) override;

private:
	void confirm();
	void askToReplace(const std::string& name, std::string fullPath);

	TextBox* m_filenameField = nullptr;
	bool m_confirmOverwrite = true;

	Panel* m_overwritePanel = nullptr;
	Label* m_overwriteMessage = nullptr;
	Button* m_replaceButton = nullptr;
	Button* m_keepButton = nullptr;
	std::string m_pendingPath;   // what Replace will save to
};

} // namespace lightGraphics::ui
