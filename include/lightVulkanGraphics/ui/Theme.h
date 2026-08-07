#pragma once

// Layer 1/3 shared: the colour and metric tokens every widget draws from. See
// docs/gui/06-layout-and-theme.md, "Theme".

#include "Types.h"

namespace lightGraphics::ui {

struct Theme {
	// ---- colours ----
	Color windowBg          { 0x1E, 0x21, 0x26, 0xF0 };
	Color titleBg           { 0x15, 0x18, 0x1C, 0xFF };
	Color titleBgActive     { 0x22, 0x27, 0x2E, 0xFF };
	Color border            { 0x3A, 0x41, 0x4B, 0xFF };

	Color text              { 0xE4, 0xE7, 0xEB, 0xFF };
	Color textDisabled      { 0x6B, 0x73, 0x7F, 0xFF };

	Color frameBg           { 0x2A, 0x30, 0x38, 0xFF };
	Color frameBgHovered    { 0x33, 0x3A, 0x44, 0xFF };
	Color frameBgActive     { 0x3B, 0x44, 0x50, 0xFF };

	Color accent            { 0x3D, 0x8B, 0xFD, 0xFF };
	Color accentHovered     { 0x55, 0x9C, 0xFF, 0xFF };
	Color accentActive      { 0x2C, 0x74, 0xDB, 0xFF };

	Color checkMark         { 0xFF, 0xFF, 0xFF, 0xFF };
	Color selectionBg       { 0x3D, 0x8B, 0xFD, 0x66 };
	Color scrollbarBg       { 0x18, 0x1B, 0x20, 0xFF };
	Color scrollbarGrab     { 0x4A, 0x52, 0x5E, 0xFF };
	Color error              { 0xE0, 0x5A, 0x5A, 0xFF };
	Color plotLine           { 0x5A, 0xD0, 0x9A, 0xFF };

	// ---- metrics, all in logical pixels ----
	float fontSize           = 14.0f;
	float rounding            = 3.0f;
	float borderWidth         = 1.0f;
	float windowPadding       = 8.0f;
	float framePadding        = 4.0f;
	float itemSpacing         = 6.0f;
	float titleBarHeight      = 24.0f;
	float rowHeight           = 22.0f;   // default widget height
	float scrollbarWidth      = 10.0f;
	float resizeGripSize      = 14.0f;
	float sliderTrackHeight   = 6.0f;
	float sliderHandleWidth   = 10.0f;
	float labelWidthRatio     = 0.42f;
	float tooltipDelay        = 0.6f;    // seconds

	static Theme dark();
	static Theme light();
	static Theme highContrast();
};

} // namespace lightGraphics::ui
