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
#include <lightVulkanGraphics/ui/widgets/TextBox.h>
#include <lightVulkanGraphics/ui/widgets/ListBox.h>
#include <lightVulkanGraphics/ui/widgets/Button.h>
#include <lightVulkanGraphics/ui/widgets/Label.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <filesystem>

namespace lightGraphics::ui {

namespace {
	constexpr float kDialogWidth = 420.0f;
	constexpr float kDialogHeight = 360.0f;

	bool endsWithFilter(const std::string& name, const std::string& filter) {
		return filter.empty() ||
			(name.size() >= filter.size() &&
			 name.compare(name.size() - filter.size(), filter.size(), filter) == 0);
	}
}

SaveFileDialog::SaveFileDialog(GuiContext& context, std::string directory, std::string extensionFilter)
	: m_context(context)
	, m_directory(std::move(directory))
	, m_extensionFilter(std::move(extensionFilter))
{
	// Centered over whatever the display size happens to be at construction time;
	// open() re-centers against the current size every time it's shown, so a window
	// resize between construction and first use doesn't leave it off-centre.
	Vec2 const display = m_context.input().displaySize;
	Rect const bounds{
		std::max(0.0f, (display.x - kDialogWidth) * 0.5f),
		std::max(0.0f, (display.y - kDialogHeight) * 0.5f),
		kDialogWidth, kDialogHeight
	};

	m_panel = m_context.createPanel("Save As", bounds,
		PanelFlags::Modal | PanelFlags::Closable | PanelFlags::Movable);
	m_panel->setVisible(false);
	// Cancel button, the title-bar X, and Escape all route through requestClose(),
	// which fires this -- one path for every "the user backed out" case. The Save
	// button below deliberately does NOT go through requestClose() (it calls
	// setVisible(false) directly instead), so a successful save never also fires
	// onCancel -- see Panel::setOnClose's own doc comment for why setVisible(false)
	// alone is silent.
	m_panel->setOnClose([this] { if (m_onCancel) { m_onCancel(); } });

	m_panel->add<Label>("Existing files (click to overwrite):");
	m_fileList = m_panel->add<ListBox>("Files", std::vector<std::string>{}, -1);
	m_fileList->setVisibleRows(6);
	m_fileList->setOnChange([this](int) {
		if (m_filenameField && m_fileList->selectedIndex() >= 0) {
			m_filenameField->setText(std::string(m_fileList->selectedText()), false);
		}
	});

	m_filenameField = m_panel->add<TextBox>("Filename");

	m_statusLabel = m_panel->add<Label>("");

	auto* saveButton = m_panel->add<Button>("Save");
	saveButton->setOnClick([this] { confirm(); });

	auto* cancelButton = m_panel->add<Button>("Cancel");
	cancelButton->setOnClick([this] { m_panel->requestClose(); });
}

bool SaveFileDialog::isOpen() const {
	return m_panel && m_panel->visible();
}

void SaveFileDialog::refreshFileList() {
	std::vector<std::string> names;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(m_directory, ec)) {
		if (ec) {
			break;
		}
		if (!entry.is_regular_file()) {
			continue;
		}
		std::string const name = entry.path().filename().string();
		if (endsWithFilter(name, m_extensionFilter)) {
			names.push_back(name);
		}
	}
	std::sort(names.begin(), names.end());
	m_fileList->setItems(std::move(names));
}

void SaveFileDialog::open() {
	refreshFileList();
	if (m_filenameField) {
		m_filenameField->setText("", false);
	}
	if (m_statusLabel) {
		m_statusLabel->setText("");
	}

	Vec2 const display = m_context.input().displaySize;
	Rect bounds = m_panel->bounds();
	bounds.x = std::max(0.0f, (display.x - bounds.w) * 0.5f);
	bounds.y = std::max(0.0f, (display.y - bounds.h) * 0.5f);
	m_panel->setBounds(bounds);

	m_panel->setVisible(true);
	m_panel->bringToFront();
}

void SaveFileDialog::confirm() {
	if (!m_filenameField) {
		return;
	}
	std::string name(m_filenameField->text());
	// Trim surrounding whitespace -- a filename that's all whitespace (or empty) isn't
	// a name the user meant to save, just an unconfirmed empty field.
	auto const first = name.find_first_not_of(" \t");
	if (first == std::string::npos) {
		if (m_statusLabel) {
			m_statusLabel->setText("Enter a filename.");
		}
		return;
	}
	auto const last = name.find_last_not_of(" \t");
	name = name.substr(first, last - first + 1);

	if (!endsWithFilter(name, m_extensionFilter)) {
		name += m_extensionFilter;
	}

	std::filesystem::path const fullPath = std::filesystem::path(m_directory) / name;
	if (m_onConfirm) {
		m_onConfirm(fullPath.string());
	}
	// Deliberately setVisible(false) rather than requestClose() -- see the
	// constructor's comment on m_panel->setOnClose for why a successful save must not
	// also fire onCancel.
	m_panel->setVisible(false);
}

} // namespace lightGraphics::ui
