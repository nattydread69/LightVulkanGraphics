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

// Umbrella header for LVGUI, the GUI layer of LightVulkanGraphics.
//
// Consumers include only this header:
//
//     #include <lightVulkanGraphics/ui/Ui.h>
//     namespace lvgui = lightGraphics::ui;
//
// The design specification lives in docs/gui/. New widget headers get an include here
// as they land.

#include "Types.h"
#include "Font.h"
#include "DrawList.h"
#include "InputState.h"
#include "KeyCodes.h"
#include "Theme.h"
#include "Widget.h"
#include "Panel.h"
#include "MenuBar.h"
#include "GuiContext.h"

#include "widgets/Label.h"
#include "widgets/Separator.h"
#include "widgets/Spacer.h"
#include "widgets/Button.h"
#include "widgets/Checkbox.h"
#include "widgets/RadioButton.h"
#include "widgets/Slider.h"
#include "widgets/DragValue.h"
#include "widgets/TextBox.h"
#include "widgets/CompositeWidget.h"
#include "widgets/DropDown.h"
#include "widgets/Row.h"
#include "widgets/Vec3Field.h"
#include "widgets/CollapsingSection.h"
#include "widgets/ProgressBar.h"
#include "widgets/PlotLine.h"
#include "widgets/ColorEdit.h"
#include "widgets/Image.h"
#include "widgets/TabBar.h"
#include "widgets/ListBox.h"
#include "widgets/ContextMenu.h"
#include "widgets/LogView.h"
