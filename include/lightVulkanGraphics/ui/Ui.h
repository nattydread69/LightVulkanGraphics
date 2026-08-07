#pragma once

// Umbrella header for LVGUI, the GUI layer of LightVulkanGraphics.
//
// Consumers include only this header:
//
//     #include <lightVulkanGraphics/ui/Ui.h>
//     namespace lvgui = lightGraphics::ui;
//
// The design specification lives in docs/gui/. Subsequent phases will add includes for
// the remaining widget headers as they land.

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
