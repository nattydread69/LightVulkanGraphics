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

#include <lightVulkanGraphics/ui/widgets/SaveFileDialog.h>
#include <lightVulkanGraphics/ui/widgets/Button.h>
#include <lightVulkanGraphics/ui/widgets/Label.h>
#include <lightVulkanGraphics/ui/widgets/Row.h>
#include <lightVulkanGraphics/ui/widgets/Spacer.h>
#include <lightVulkanGraphics/ui/widgets/TextBox.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "../TextWrap.h"

#include <algorithm>

namespace lightGraphics::ui {

namespace {
	constexpr float kDialogHeight = 480.0f;
	constexpr float kReplaceWidth = 400.0f;
	constexpr float kReplaceGap = 4.0f;   // extra space between the question and its buttons
}

SaveFileDialog::SaveFileDialog(GuiContext& context, std::string directory, std::string extensionFilter)
	: FileDialogBase(context, "Save As", std::move(directory), std::move(extensionFilter), kDialogHeight)
{
	const Theme& th = m_context.theme();

	m_filenameField = m_panel->add<TextBox>("File name");
	m_filenameField->setLabelWidthOverride(
		m_context.font().measureText("File name", th.fontSize).x + 2.0f * th.itemSpacing);
	// Enter in the filename field saves, the way every native Save As does. onSubmit
	// rather than onCommit: onCommit also fires on focus loss, so the file would be
	// written the moment the user clicked across to the existing-files list.
	m_filenameField->setOnSubmit([this](std::string_view) { confirm(); });

	addFooter("Save");

	// The "Replace file?" question: a second Modal panel. Created after the Save panel,
	// and raised with bringToFront() whenever it is shown, so it is the frontmost modal
	// -- which is what makes it, not the Save dialog under it, take the input and the
	// Escape key (GuiContext::activeModalPanel()).
	// Its height is set in askToReplace(), to fit the message for that particular name.
	m_overwritePanel = m_context.createPanel("Replace file?", { 0.0f, 0.0f, kReplaceWidth, 120.0f },
		PanelFlags::Modal | PanelFlags::Closable | PanelFlags::Movable);
	m_overwritePanel->setVisible(false);
	// Backing out (its Cancel button, the X, Escape) returns to the Save dialog with the
	// name still there to edit.
	m_overwritePanel->setOnClose([this] {
		m_pendingPath.clear();
		m_context.setFocus(m_filenameField);
	});
	m_overwriteMessage = m_overwritePanel->add<Label>("");
	m_overwriteMessage->setWordWrap(true);
	m_overwritePanel->add<Spacer>(kReplaceGap);
	auto* replaceRow = m_overwritePanel->add<Row>();
	replaceRow->setWeights({ 1.2f, 1.0f, 1.0f });
	replaceRow->add<Spacer>(0.0f);
	m_replaceButton = replaceRow->add<Button>("Replace");
	m_replaceButton->setOnClick([this] {
		std::string const path = m_pendingPath;
		m_pendingPath.clear();
		m_overwritePanel->setVisible(false);
		finish(path);
	});
	m_keepButton = replaceRow->add<Button>("Cancel");
	m_keepButton->setOnClick([this] { m_overwritePanel->requestClose(); });
}

void SaveFileDialog::open() {
	m_overwritePanel->setVisible(false);
	m_pendingPath.clear();
	showDialog();
	m_filenameField->setText("", false);
	m_context.setFocus(m_filenameField);
}

void SaveFileDialog::open(std::string suggestedFilename) {
	open();
	m_filenameField->setText(std::move(suggestedFilename), false);
	m_filenameField->selectAll();
}

void SaveFileDialog::onSelectionChanged(const FileEntry* entry) {
	if (entry && !entry->isDirectory) {
		m_filenameField->setText(entry->name, false);
	}
}

void SaveFileDialog::onFileActivated(const FileEntry& entry) {
	m_filenameField->setText(entry.name, false);
	confirm();
}

void SaveFileDialog::onConfirmClicked() {
	confirm();
}

void SaveFileDialog::confirm() {
	std::string name(m_filenameField->text());
	// Trim surrounding whitespace -- a filename that's all whitespace (or empty) isn't
	// a name the user meant to save, just an unconfirmed empty field.
	auto const first = name.find_first_not_of(" \t");
	if (first == std::string::npos) {
		setStatus("Enter a filename.", true);
		return;
	}
	auto const last = name.find_last_not_of(" \t");
	name = name.substr(first, last - first + 1);

	if (name.find_first_of("/\\") != std::string::npos) {
		setStatus("A filename can't contain / or \\.", true);
		return;
	}
	if (name == "." || name == "..") {
		setStatus("\"" + name + "\" isn't a valid filename.", true);
		return;
	}
	// Typing a folder's name and pressing Enter goes into it, as native dialogs do,
	// rather than appending an extension and saving a file next to it.
	if (navigationEnabled() && isFolderInDirectory(name)) {
		navigateTo(pathInDirectory(name));
		m_filenameField->setText("", false);
		return;
	}

	if (const FileTypeFilter* filter = activeFilter(); filter && !matchesTypeFilter(name)) {
		name += filter->extensions.front();
	}

	std::string fullPath = pathInDirectory(name);
	if (m_confirmOverwrite && existsInDirectory(name)) {
		askToReplace(name, std::move(fullPath));
		return;
	}
	finish(fullPath);
}

void SaveFileDialog::askToReplace(const std::string& name, std::string fullPath) {
	m_pendingPath = std::move(fullPath);
	std::string message = "\"" + name + "\" already exists in this folder. Do you want to replace it?";

	// Tall enough for the message wrapped at the panel's width -- a long name can take
	// it to two or three lines -- plus the button row, using the same vertical stack
	// arithmetic as Panel::layout().
	const Theme& th = m_context.theme();
	float const textW = kReplaceWidth - 2.0f * th.windowPadding;
	std::size_t const lines = std::max<std::size_t>(1,
		wrapText(m_context.font(), message, th.fontSize, textW).size());
	float const height = th.titleBarHeight + 2.0f * th.windowPadding +
		static_cast<float>(lines) * m_context.font().lineHeight(th.fontSize) +
		kReplaceGap + th.rowHeight + 3.0f * th.itemSpacing;
	m_overwriteMessage->setText(std::move(message));

	// Centred over the Save dialog rather than the window, so it reads as that
	// dialog's own question.
	Rect const parent = m_panel->bounds();
	Rect const bounds{
		parent.x + (parent.w - kReplaceWidth) * 0.5f,
		parent.y + (parent.h - height) * 0.5f,
		kReplaceWidth, height
	};
	m_overwritePanel->setBounds(bounds);
	m_overwritePanel->setVisible(true);
	m_overwritePanel->bringToFront();
	// Cancel, not Replace, takes Enter: overwriting a file should never be what a
	// stray keypress does.
	m_context.setFocus(m_keepButton);
}

} // namespace lightGraphics::ui
