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

#include <lightVulkanGraphics/ui/widgets/RadioButton.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>

namespace lightGraphics::ui {

void RadioGroup::registerMember(RadioButton* button, int value) {
	m_members.emplace_back(button, value);
}

void RadioGroup::unregisterMember(RadioButton* button) {
	m_members.erase(
		std::remove_if(m_members.begin(), m_members.end(),
			[button](const std::pair<RadioButton*, int>& m) { return m.first == button; }),
		m_members.end());
}

void RadioGroup::select(int value) {
	if (value == m_value) {
		return;
	}
	m_value = value;
	if (m_onChange) {
		m_onChange(m_value);
	}
}

RadioButton* RadioGroup::memberAfter(RadioButton* from, int direction) const {
	if (m_members.size() < 2) {
		return nullptr;
	}
	auto it = std::find_if(m_members.begin(), m_members.end(),
		[from](const std::pair<RadioButton*, int>& m) { return m.first == from; });
	if (it == m_members.end()) {
		return nullptr;
	}
	std::size_t idx = static_cast<std::size_t>(it - m_members.begin());
	std::size_t n = m_members.size();
	std::size_t next = (direction >= 0) ? (idx + 1) % n : (idx + n - 1) % n;
	return m_members[next].first;
}

RadioButton::RadioButton(std::string label, RadioGroup* group, int valueWhenSelected)
	: m_group(group), m_valueWhenSelected(valueWhenSelected) {
	setLabel(std::move(label));
	m_group->registerMember(this, m_valueWhenSelected);
}

RadioButton::~RadioButton() {
	// The group commonly outlives individual RadioButtons (e.g. Panel::clear() destroys
	// widgets while the group, owned by the consumer, persists) -- unregister so
	// memberAfter() never returns a dangling pointer.
	m_group->unregisterMember(this);
}

Vec2 RadioButton::preferredSize(const GuiContext& ctx) const {
	return { 0.0f, ctx.theme().rowHeight };
}

void RadioButton::update(GuiContext& ctx) {
	if (!m_enabled) {
		return;
	}

	const InputState& in = ctx.input();
	if (ctx.activeId() == id() && in.mousePressed[static_cast<int>(MouseButton::Left)]) {
		if (m_group->value() != m_valueWhenSelected) {
			m_group->select(m_valueWhenSelected);
		}
	}

	if (ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (!ev.pressed || ev.repeat) {
				continue;
			}
			int direction = 0;
			if (ev.key == Key::Left || ev.key == Key::Up) {
				direction = -1;
			} else if (ev.key == Key::Right || ev.key == Key::Down) {
				direction = 1;
			}
			if (direction == 0) {
				continue;
			}
			// Roving-tabindex behaviour: arrow keys move both the selection AND keyboard
			// focus together, matching how native radio groups behave.
			if (RadioButton* next = m_group->memberAfter(this, direction)) {
				m_group->select(next->m_valueWhenSelected);
				ctx.setFocus(next);
			}
		}
	}
}

void RadioButton::draw(DrawList& dl, const GuiContext& ctx) const {
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
	float diameter = th.fontSize;
	float radius = diameter * 0.5f;
	Vec2 centre{ control.x + radius, control.y + control.h * 0.5f };

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

	// DrawList has no circle-outline primitive, only addCircleFilled -- fake a ring by
	// drawing the border colour first and the interior fill on top, inset by the border
	// width. Same trick a filled-then-inset rect uses for addRect vs addRectFilled.
	Color borderColor = (effectivelyEnabled() && ctx.focusedId() == id()) ? th.accent : th.border;
	dl.addCircleFilled(centre, radius, borderColor);
	dl.addCircleFilled(centre, std::max(0.0f, radius - th.borderWidth), fill);

	if (m_group->value() == m_valueWhenSelected) {
		dl.addCircleFilled(centre, radius * 0.4f, th.checkMark);
	}
}

} // namespace lightGraphics::ui
