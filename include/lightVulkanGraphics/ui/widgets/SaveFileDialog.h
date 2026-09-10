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

// A reusable "Save As" file dialog. Not a Widget itself -- like the About-dialog
// pattern in docs/gui/05-widgets.md ("Panel", "Modal panels"), it owns a single
// Modal|Closable|Movable Panel, created once and toggled visible/hidden rather than
// rebuilt each time it's opened. Lists the existing files in `directory` matching
// `extensionFilter` (click one to prefill the filename, for overwrite awareness),
// plus a filename TextBox and Save/Cancel buttons.
//
// `directory` and `extensionFilter` are constructor parameters rather than anything
// pose-file-specific, so any lightVulkanGraphics consumer can reuse this for its own
// "export to a new file" flow.

#include <functional>
#include <string>

namespace lightGraphics::ui {

class GuiContext;
class Panel;
class TextBox;
class ListBox;
class Button;
class Label;

class SaveFileDialog {
public:
	// extensionFilter, e.g. ".pose.json" -- matched against each directory entry's
	// filename suffix for the existing-files list, and appended to whatever the user
	// types if they didn't already type it themselves.
	SaveFileDialog(GuiContext& context, std::string directory, std::string extensionFilter);

	// Shows the dialog and (re)scans `directory` on disk for its file listing. Safe to
	// call again while already open, e.g. re-clicking "Save As...".
	void open();

	// Fired once, with the full chosen path, when the user confirms via the Save
	// button. Not fired by Cancel/the title-bar close button/Escape -- see
	// setOnCancel() for those.
	void setOnConfirm(std::function<void(const std::string& fullPath)> onConfirm) { m_onConfirm = std::move(onConfirm); }
	// Fired when the user backs out without saving: Cancel button, the title-bar X, or
	// Escape (all three route through Panel::requestClose(), see the .cpp).
	void setOnCancel(std::function<void()> onCancel) { m_onCancel = std::move(onCancel); }

	bool isOpen() const;

	// The underlying Panel, for callers that want to dock/anchor it, give it a
	// persistence id, or (in tests) reach its child widgets directly -- the same
	// pattern GuiContext::createPanel() already hands raw Panel* back to any
	// consumer. Never null after construction.
	Panel* panel() const { return m_panel; }

private:
	void refreshFileList();
	void confirm();

	GuiContext& m_context;
	std::string m_directory;
	std::string m_extensionFilter;

	Panel* m_panel = nullptr;
	TextBox* m_filenameField = nullptr;
	ListBox* m_fileList = nullptr;
	Label* m_statusLabel = nullptr;

	std::function<void(const std::string&)> m_onConfirm;
	std::function<void()> m_onCancel;
};

} // namespace lightGraphics::ui
