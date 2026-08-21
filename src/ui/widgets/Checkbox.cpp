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

#include <lightVulkanGraphics/ui/widgets/Checkbox.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

Checkbox::Checkbox(std::string label, bool initial) : m_state(initial ? State::On : State::Off) {
	setLabel(std::move(label));
}

void Checkbox::setState(State state, bool fireCallback) {
	m_state = state;
	if (m_bindTarget) {
		*m_bindTarget = value();
	}
	if (fireCallback && m_onChange) {
		m_onChange(value());
	}
}

void Checkbox::toggle() {
	// Off/On always alternate. With tri-state on, On advances to Indeterminate instead of
	// straight back to Off, and any click while Indeterminate resets to Off -- there is no
	// public setter for Indeterminate itself (docs/gui/05 lists none), so this click cycle
	// is the only way to reach it.
	State next;
	if (m_triState) {
		switch (m_state) {
			case State::Off: next = State::On; break;
			case State::On: next = State::Indeterminate; break;
			default: next = State::Off; break;
		}
	} else {
		next = (m_state == State::On) ? State::Off : State::On;
	}
	setState(next, true);
}

void Checkbox::setValue(bool value, bool fireCallback) {
	setState(value ? State::On : State::Off, fireCallback);
}

Vec2 Checkbox::preferredSize(const GuiContext& ctx) const {
	return { 0.0f, ctx.theme().rowHeight };
}

void Checkbox::update(GuiContext& ctx) {
	if (m_bindTarget) {
		m_state = *m_bindTarget ? State::On : State::Off;
	}
	if (!m_enabled) {
		return;
	}

	const InputState& in = ctx.input();
	if (ctx.activeId() == id() && in.mousePressed[static_cast<int>(MouseButton::Left)]) {
		toggle();
	}

	if (ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (ev.pressed && !ev.repeat && ev.key == Key::Space) {
				toggle();
			}
		}
	}
}

void Checkbox::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	RowSplit split = splitRow(ctx);

	if (!m_label.empty()) {
		Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, split.label, textColor, m_label, Align::Start, Align::Center);
	}

	const Rect& control = split.control;
	float side = th.fontSize;
	Rect box{ control.x, control.y + (control.h - side) * 0.5f, side, side };

	Color fill;
	if (!effectivelyEnabled()) {
		fill = th.frameBg.withAlpha(0.4f);
	} else if (ctx.activeId() == id() && ctx.hoveredId() == id()) {
		fill = th.frameBgActive;
	} else if (ctx.hoveredId() == id()) {
		fill = th.frameBgHovered;
	} else {
		fill = th.frameBg;
	}
	dl.addRectFilled(box, fill, th.rounding);

	Color borderColor = (effectivelyEnabled() && ctx.focusedId() == id()) ? th.accent : th.border;
	dl.addRect(box, borderColor, 1.0f, th.rounding);

	if (m_state == State::On) {
		Vec2 p0{ box.x + box.w * 0.24f, box.y + box.h * 0.52f };
		Vec2 p1{ box.x + box.w * 0.42f, box.y + box.h * 0.72f };
		Vec2 p2{ box.x + box.w * 0.76f, box.y + box.h * 0.28f };
		Vec2 pts[3] = { p0, p1, p2 };
		dl.addPolyline(pts, 3, th.checkMark, 2.0f, false);
	} else if (m_state == State::Indeterminate) {
		dl.addRectFilled(box.inset(box.w * 0.28f), th.checkMark);
	}
}

} // namespace lightGraphics::ui
