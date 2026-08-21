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

#include <lightVulkanGraphics/ui/Theme.h>

namespace lightGraphics::ui {

Theme Theme::dark() {
	return Theme{};
}

Theme Theme::light() {
	Theme t;
	t.windowBg      = Color(0xF2, 0xF3, 0xF5, 0xF0);
	t.titleBg       = Color(0xE1, 0xE3, 0xE6, 0xFF);
	t.titleBgActive = Color(0xD3, 0xD7, 0xDC, 0xFF);
	t.border        = Color(0xC3, 0xC7, 0xCE, 0xFF);

	t.text         = Color(0x1B, 0x1E, 0x23, 0xFF);
	t.textDisabled = Color(0x8B, 0x91, 0x9B, 0xFF);

	t.frameBg        = Color(0xFF, 0xFF, 0xFF, 0xFF);
	t.frameBgHovered = Color(0xEC, 0xEE, 0xF1, 0xFF);
	t.frameBgActive  = Color(0xE0, 0xE3, 0xE7, 0xFF);

	t.checkMark     = Color(0x1B, 0x1E, 0x23, 0xFF);
	t.selectionBg   = Color(0x3D, 0x8B, 0xFD, 0x55);
	t.scrollbarBg   = Color(0xE1, 0xE3, 0xE6, 0xFF);
	t.scrollbarGrab = Color(0xB7, 0xBC, 0xC4, 0xFF);
	return t;
}

Theme Theme::highContrast() {
	Theme t;
	t.windowBg      = Color(0x00, 0x00, 0x00, 0xFF);
	t.titleBg       = Color(0x00, 0x00, 0x00, 0xFF);
	t.titleBgActive = Color(0xFF, 0xFF, 0xFF, 0xFF);
	t.border        = Color(0xFF, 0xFF, 0xFF, 0xFF);

	t.text         = Color(0xFF, 0xFF, 0xFF, 0xFF);
	t.textDisabled = Color(0xA0, 0xA0, 0xA0, 0xFF);

	t.frameBg        = Color(0x00, 0x00, 0x00, 0xFF);
	t.frameBgHovered = Color(0x20, 0x20, 0x20, 0xFF);
	t.frameBgActive  = Color(0x40, 0x40, 0x40, 0xFF);

	t.accent        = Color(0xFF, 0xFF, 0x00, 0xFF);
	t.accentHovered = Color(0xFF, 0xFF, 0x80, 0xFF);
	t.accentActive  = Color(0xC0, 0xC0, 0x00, 0xFF);

	t.checkMark     = Color(0x00, 0x00, 0x00, 0xFF);
	t.selectionBg   = Color(0xFF, 0xFF, 0x00, 0x80);
	t.scrollbarBg   = Color(0x00, 0x00, 0x00, 0xFF);
	t.scrollbarGrab = Color(0xFF, 0xFF, 0xFF, 0xFF);
	t.error         = Color(0xFF, 0x40, 0x40, 0xFF);

	t.borderWidth = 2.0f;
	return t;
}

} // namespace lightGraphics::ui
