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

#include <lightVulkanGraphics/ui/widgets/OpenFileDialog.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

namespace {
	constexpr float kDialogHeight = 440.0f;
}

OpenFileDialog::OpenFileDialog(GuiContext& context, std::string directory, std::string extensionFilter)
	: FileDialogBase(context, "Open", std::move(directory), std::move(extensionFilter), kDialogHeight)
{
	addFooter("Open");
}

void OpenFileDialog::open() {
	showDialog();
	m_context.setFocus(fileList());
}

void OpenFileDialog::onFileActivated(const FileEntry& entry) {
	finish(pathInDirectory(entry.name));
}

void OpenFileDialog::onConfirmClicked() {
	const FileEntry* selected = fileList()->selectedEntry();
	if (!selected) {
		setStatus("Select a file first.", true);
		return;
	}
	if (selected->isDirectory) {
		navigateTo(pathInDirectory(selected->name));
		return;
	}
	finish(pathInDirectory(selected->name));
}

} // namespace lightGraphics::ui
