#pragma once

// Umbrella header for LVGUI, the GUI layer of LightVulkanGraphics.
//
// Consumers include only this header:
//
//     #include <lightVulkanGraphics/ui/Ui.h>
//     namespace lvgui = lightGraphics::ui;
//
// The design specification lives in docs/gui/. Subsequent phases will add includes for
// GuiContext.h, Panel.h, and the widget headers.

#include "Types.h"
#include "Font.h"
#include "DrawList.h"
#include "InputState.h"
#include "KeyCodes.h"
