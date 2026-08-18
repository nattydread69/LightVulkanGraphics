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
