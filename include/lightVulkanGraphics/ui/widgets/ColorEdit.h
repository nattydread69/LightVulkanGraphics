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

// docs/gui/05-widgets.md, "ColorEdit": a swatch button in the control row that opens a
// saturation/value square + hue strip (+ alpha strip, ColorEdit4 only) popup, using the
// same GuiContext::openPopup() machinery DropDown does -- see DropDown.h's class comment
// for the mechanism this reuses (popup re-registered every open frame from the owner's
// CURRENT bounds, so it follows a dragged/scrolled/resized panel for free).
//
// Deliberately not a CompositeWidget: the picker controls only exist inside the popup,
// not as always-visible panel-space children, so there is no inline child tree to manage
// -- same reasoning as DropDown not deriving from CompositeWidget either.

#include "../Widget.h"

#include <functional>
#include <string>

namespace lightGraphics::ui {

struct Theme;

class ColorEdit : public Widget {
public:
	Color value() const { return m_value; }
	// Always resyncs the cached hue/saturation/value from the new RGB -- an explicit
	// external set has no "was this really a change" ambiguity to preserve (contrast
	// pullBoundValue(), which only resyncs when the bound value no longer matches what
	// the cached HSV already explains).
	void setValue(Color value, bool fireCallback = false);
	// The pointer must outlive the widget -- same bind() contract as every other value
	// widget in this library (update() reads *target at the start of every frame).
	void bind(Color* target) { m_bindTarget = target; }
	void setOnChange(std::function<void(Color)> onChange) { m_onChange = std::move(onChange); }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;
	void drawPopup(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return true; }

protected:
	ColorEdit(std::string label, Color initial, bool hasAlpha);

private:
	enum class DragTarget { None, SVSquare, HueStrip, AlphaStrip };

	// Reads *m_bindTarget (if bound) and resyncs the HSV cache from it ONLY when that
	// value differs from what the cache already predicts -- see fromHsv()'s own writes,
	// which keep m_value and the cache in agreement on every internal edit. Skipping the
	// resync in the "already agrees" case is what lets dragging saturation to 0 (grey)
	// and back restore the hue it started at, instead of losing it the instant a bound
	// pointer round-trips the same value back next frame (Color::toHSV canonically
	// returns hue 0 for any grey -- see its header comment).
	void pullBoundValue();
	// Writes m_value from the current m_hue/m_sat/m_val (+ existing alpha), then fires
	// bind()/onChange -- the landing point for every SV-square/hue-strip drag.
	void fromHsv();
	// Writes m_value.a directly (alpha has no place in the HSV cache), then fires
	// bind()/onChange -- the landing point for every alpha-strip drag.
	void fromAlpha(std::uint8_t a);
	void commit(Color v, bool fireCallback);

	Rect computePopupRect(const GuiContext&) const;
	Rect svSquareRect(const Rect& popupRect, const Theme&) const;
	Rect hueStripRect(const Rect& popupRect, const Theme&) const;
	Rect alphaStripRect(const Rect& popupRect, const Theme&) const;

	void dragSVSquare(const Rect&, Vec2 mouse);
	void dragHueStrip(const Rect&, Vec2 mouse);
	void dragAlphaStrip(const Rect&, Vec2 mouse);

	bool m_hasAlpha;
	Color m_value;
	// HSV cache, kept in sync with m_value by fromHsv()/pullBoundValue() -- see their
	// comments for why this exists at all rather than just storing HSV or just RGB.
	float m_hue = 0.0f, m_sat = 0.0f, m_val = 1.0f;

	Color* m_bindTarget = nullptr;
	std::function<void(Color)> m_onChange;

	// Which popup region the currently-held press started in, so a drag that leaves the
	// square/strip mid-gesture keeps tracking (clamped to that region) rather than
	// stopping -- same "capture spans the whole gesture, not just while hovering it"
	// convention as Slider's track drag and Panel's resize/scrollbar-grab drags.
	DragTarget m_dragTarget = DragTarget::None;
};

class ColorEdit3 : public ColorEdit {
public:
	ColorEdit3(std::string label, Color initial) : ColorEdit(std::move(label), initial, false) {}
};

class ColorEdit4 : public ColorEdit {
public:
	ColorEdit4(std::string label, Color initial) : ColorEdit(std::move(label), initial, true) {}
};

} // namespace lightGraphics::ui
