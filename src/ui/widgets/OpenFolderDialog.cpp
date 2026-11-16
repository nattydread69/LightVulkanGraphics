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

#include <lightVulkanGraphics/ui/widgets/OpenFolderDialog.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

namespace {
	constexpr float kDialogHeight = 440.0f;
}

OpenFolderDialog::OpenFolderDialog(GuiContext& context, std::string directory)
	: FileDialogBase(context, "Select folder", std::move(directory), "", kDialogHeight)
{
	setFoldersOnly(true);
	addFooter("Select");
}

void OpenFolderDialog::open() {
	showDialog();
	m_context.setFocus(fileList());
}

void OpenFolderDialog::onFileActivated(const FileEntry&) {
	// Files are never listed, and folders are entered by the base class.
}

void OpenFolderDialog::onConfirmClicked() {
	const FileEntry* selected = fileList()->selectedEntry();
	if (selected && selected->isDirectory) {
		finish(pathInDirectory(selected->name));
		return;
	}
	finish(directory());
}

} // namespace lightGraphics::ui
