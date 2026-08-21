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

#include <lightVulkanGraphics/ui/widgets/ColorEdit.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <cmath>

namespace lightGraphics::ui {

ColorEdit::ColorEdit(std::string label, Color initial, bool hasAlpha)
	: m_hasAlpha(hasAlpha), m_value(initial) {
	setLabel(std::move(label));
	m_value.toHSV(m_hue, m_sat, m_val);
}

void ColorEdit::setValue(Color value, bool fireCallback) {
	m_value = value;
	m_value.toHSV(m_hue, m_sat, m_val);
	if (m_bindTarget) {
		*m_bindTarget = m_value;
	}
	if (fireCallback && m_onChange) {
		m_onChange(m_value);
	}
}

void ColorEdit::pullBoundValue() {
	if (!m_bindTarget) {
		return;
	}
	Color bound = *m_bindTarget;
	Color predicted = Color::fromHSV(m_hue, m_sat, m_val, m_value.a);
	if (bound.r != predicted.r || bound.g != predicted.g || bound.b != predicted.b ||
	    bound.a != m_value.a) {
		m_value = bound;
		m_value.toHSV(m_hue, m_sat, m_val);
	}
}

void ColorEdit::fromHsv() {
	commit(Color::fromHSV(m_hue, m_sat, m_val, m_value.a), true);
}

void ColorEdit::fromAlpha(std::uint8_t a) {
	Color v = m_value;
	v.a = a;
	commit(v, true);
}

void ColorEdit::commit(Color v, bool fireCallback) {
	m_value = v;
	if (m_bindTarget) {
		*m_bindTarget = v;
	}
	if (fireCallback && m_onChange) {
		m_onChange(v);
	}
}

// ---- popup geometry -----------------------------------------------------------------

Rect ColorEdit::computePopupRect(const GuiContext& ctx) const {
	RowSplit split = splitRow(ctx);
	const Rect& control = split.control;
	const Theme& th = ctx.theme();

	float width = 2.0f * th.framePadding + th.colorSquareSize + th.itemSpacing + th.colorStripWidth;
	float height = 2.0f * th.framePadding + th.colorSquareSize +
	               (m_hasAlpha ? th.itemSpacing + th.colorStripWidth : 0.0f);

	float y = control.bottom();
	if (y + height > ctx.input().displaySize.y) {
		// docs/gui/05, "DropDown": same upward flip when the popup would run off the
		// bottom of the framebuffer.
		y = control.top() - height;
	}

	return { control.x, y, width, height };
}

Rect ColorEdit::svSquareRect(const Rect& popupRect, const Theme& th) const {
	return { popupRect.x + th.framePadding, popupRect.y + th.framePadding,
	         th.colorSquareSize, th.colorSquareSize };
}

Rect ColorEdit::hueStripRect(const Rect& popupRect, const Theme& th) const {
	Rect sq = svSquareRect(popupRect, th);
	return { sq.right() + th.itemSpacing, sq.y, th.colorStripWidth, th.colorSquareSize };
}

Rect ColorEdit::alphaStripRect(const Rect& popupRect, const Theme& th) const {
	Rect sq = svSquareRect(popupRect, th);
	return { sq.x, sq.bottom() + th.itemSpacing,
	         th.colorSquareSize + th.itemSpacing + th.colorStripWidth, th.colorStripWidth };
}

// ---- dragging -------------------------------------------------------------------------

void ColorEdit::dragSVSquare(const Rect& r, Vec2 mouse) {
	m_sat = std::clamp((mouse.x - r.x) / r.w, 0.0f, 1.0f);
	m_val = std::clamp(1.0f - (mouse.y - r.y) / r.h, 0.0f, 1.0f);
	fromHsv();
}

void ColorEdit::dragHueStrip(const Rect& r, Vec2 mouse) {
	float t = std::clamp((mouse.y - r.y) / r.h, 0.0f, 1.0f);
	m_hue = t * 360.0f;
	fromHsv();
}

void ColorEdit::dragAlphaStrip(const Rect& r, Vec2 mouse) {
	float t = std::clamp((mouse.x - r.x) / r.w, 0.0f, 1.0f);
	fromAlpha(static_cast<std::uint8_t>(std::lround(t * 255.0f)));
}

// ---- Widget overrides -----------------------------------------------------------------

Vec2 ColorEdit::preferredSize(const GuiContext& ctx) const {
	return { 0.0f, ctx.theme().rowHeight };
}

void ColorEdit::update(GuiContext& ctx) {
	pullBoundValue();

	if (!m_enabled) {
		m_dragTarget = DragTarget::None;
		return;
	}

	const InputState& in = ctx.input();
	const int leftIdx = static_cast<int>(MouseButton::Left);
	bool isOpen = (ctx.popupOwner() == this);

	// Control click: press-based toggle, same convention as DropDown/Checkbox.
	bool controlPressed = ctx.activeId() == id() && in.mousePressed[leftIdx] && hitTest(in.mousePos);

	bool keyboardOpen = false;
	if (!isOpen && ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (ev.pressed && !ev.repeat && (ev.key == Key::Enter || ev.key == Key::Space)) {
				keyboardOpen = true;
				break;
			}
		}
	}

	if (controlPressed || keyboardOpen) {
		if (isOpen) {
			ctx.closePopup();
			isOpen = false;
		} else {
			isOpen = true;
			if (keyboardOpen) {
				// Same one-frame-delay rationale as DropDown: don't fall through into
				// this same frame's drag-detection below on the very keypress that
				// opened the popup.
				ctx.openPopup(this, computePopupRect(ctx));
				return;
			}
		}
	}

	if (!isOpen) {
		m_dragTarget = DragTarget::None;
		return;
	}

	// Re-registered every frame the popup stays open, from THIS frame's bounds -- see
	// DropDown.h's class comment on why this makes the popup follow the owner.
	Rect popupRect = computePopupRect(ctx);
	ctx.openPopup(this, popupRect);
	const Theme& th = ctx.theme();

	Rect squareRect = svSquareRect(popupRect, th);
	Rect hueRect = hueStripRect(popupRect, th);
	Rect alphaRect = m_hasAlpha ? alphaStripRect(popupRect, th) : Rect{};

	if (ctx.activeId() == id()) {
		if (in.mousePressed[leftIdx] && !controlPressed) {
			if (squareRect.contains(in.mousePos)) {
				m_dragTarget = DragTarget::SVSquare;
			} else if (hueRect.contains(in.mousePos)) {
				m_dragTarget = DragTarget::HueStrip;
			} else if (m_hasAlpha && alphaRect.contains(in.mousePos)) {
				m_dragTarget = DragTarget::AlphaStrip;
			} else {
				m_dragTarget = DragTarget::None;
			}
		}
		if (in.mouseDown[leftIdx]) {
			switch (m_dragTarget) {
				case DragTarget::SVSquare:   dragSVSquare(squareRect, in.mousePos); break;
				case DragTarget::HueStrip:   dragHueStrip(hueRect, in.mousePos); break;
				case DragTarget::AlphaStrip: dragAlphaStrip(alphaRect, in.mousePos); break;
				case DragTarget::None:       break;
			}
		}
		if (in.mouseReleased[leftIdx]) {
			m_dragTarget = DragTarget::None;
			// The popup stays open past a drag release -- pickers conventionally stay
			// open until the control is clicked again or GuiContext's own outside-click
			// rule closes it (see its class comment on openPopup()), unlike DropDown's
			// one-shot item selection which closes immediately on release.
		}
	}
}

void ColorEdit::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	RowSplit split = splitRow(ctx);

	if (!m_label.empty()) {
		Color labelColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, split.label, labelColor, m_label, Align::Start, Align::Center);
	}

	const Rect& box = split.control;
	bool open = (ctx.popupOwner() == this);
	bool focused = ctx.focusedId() == id();

	// No checkerboard backdrop for the alpha preview (docs/gui/05, "ColorEdit",
	// "Implementation notes") -- a translucent swatch blends against whatever the panel
	// already painted beneath it, which is enough feedback for "less opaque" without
	// tile-drawing machinery a true checkerboard would need.
	Color swatch = effectivelyEnabled() ? m_value : m_value.withAlpha(0.4f);
	dl.addRectFilled(box, swatch, th.rounding);

	Color borderColor = (effectivelyEnabled() && (focused || open)) ? th.accent : th.border;
	dl.addRect(box, borderColor, 1.0f, th.rounding);
}

void ColorEdit::drawPopup(DrawList& dl, const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	Rect popupRect = ctx.popupRect();

	dl.addRectFilled(popupRect, th.frameBg, th.rounding);
	dl.addRect(popupRect, th.accent, 1.0f, th.rounding);

	constexpr Color kWhite{ 0xFF, 0xFF, 0xFF, 0xFF };
	constexpr Color kBlack{ 0x00, 0x00, 0x00, 0xFF };

	// ---- saturation/value square: horizontal axis is saturation (white -> full hue),
	// vertical is value (full brightness at the top, black at the bottom regardless of
	// saturation -- hence both bottom corners being flat black).
	Rect sq = svSquareRect(popupRect, th);
	Color hueColor = Color::fromHSV(m_hue, 1.0f, 1.0f);
	dl.addRectFilledMultiColor(sq, kWhite, hueColor, kBlack, kBlack);
	dl.addRect(sq, th.border, 1.0f, 0.0f);

	Vec2 svCursor{ sq.x + m_sat * sq.w, sq.y + (1.0f - m_val) * sq.h };
	dl.addCircleFilled(svCursor, 4.0f, kWhite);
	dl.addCircleFilled(svCursor, 2.5f, kBlack);

	// ---- hue strip: six 60-degree bands stacked top-to-bottom, each a pure vertical
	// gradient between its two endpoint hues (addRectFilledMultiColor only interpolates
	// linearly between its four corners, so a full 0-360 sweep needs one band per side of
	// the hue hexagon rather than a single rect).
	Rect hue = hueStripRect(popupRect, th);
	constexpr int kHueBands = 6;
	float bandH = hue.h / static_cast<float>(kHueBands);
	for (int i = 0; i < kHueBands; ++i) {
		Color top = Color::fromHSV(static_cast<float>(i) * 60.0f, 1.0f, 1.0f);
		Color bottom = Color::fromHSV(static_cast<float>(i + 1) * 60.0f, 1.0f, 1.0f);
		Rect band{ hue.x, hue.y + bandH * static_cast<float>(i), hue.w, bandH };
		dl.addRectFilledMultiColor(band, top, top, bottom, bottom);
	}
	dl.addRect(hue, th.border, 1.0f, 0.0f);

	float hueY = hue.y + (m_hue / 360.0f) * hue.h;
	dl.addLine({ hue.x - 2.0f, hueY }, { hue.right() + 2.0f, hueY }, kWhite, 2.0f);

	if (m_hasAlpha) {
		Rect a = alphaStripRect(popupRect, th);
		Color opaque{ m_value.r, m_value.g, m_value.b, 0xFF };
		Color transparent{ m_value.r, m_value.g, m_value.b, 0x00 };
		dl.addRectFilledMultiColor(a, transparent, opaque, opaque, transparent);
		dl.addRect(a, th.border, 1.0f, 0.0f);

		float alphaX = a.x + (static_cast<float>(m_value.a) / 255.0f) * a.w;
		dl.addLine({ alphaX, a.y - 2.0f }, { alphaX, a.bottom() + 2.0f }, kWhite, 2.0f);
	}
}

} // namespace lightGraphics::ui
