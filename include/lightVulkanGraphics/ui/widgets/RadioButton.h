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

// docs/gui/05-widgets.md, "RadioButton and RadioGroup": same interaction as Checkbox
// except an already-selected button cannot be toggled off by clicking it again, and
// arrow keys move the selection within the group when any member is focused.

#include "../Widget.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace lightGraphics::ui {

class RadioButton;

// Shared selection state for a set of RadioButtons. Not a Widget itself -- owned by the
// consumer alongside (or ahead of) the RadioButtons that reference it, similar in spirit
// to a bound bool* target: it must outlive every RadioButton constructed against it.
class RadioGroup {
public:
	int value() const { return m_value; }
	// Sets the group's value directly. Deliberately does not fire onChange -- like every
	// other widget's setValue(v, fireCallback=false) default, a plain setValue() is for
	// programmatic setup; selection made through the UI goes through select() below,
	// which always fires.
	void setValue(int value) { m_value = value; }
	void setOnChange(std::function<void(int)> onChange) { m_onChange = std::move(onChange); }

	// ---- internal: used by RadioButton, not part of the normative public surface in
	// docs/gui/07-public-api.md ----
	void registerMember(RadioButton* button, int value);
	void unregisterMember(RadioButton* button);
	// Sets the value and fires onChange if it actually changed -- the path a click or an
	// arrow-key selection takes.
	void select(int value);
	// Wrap-around neighbour lookup for arrow-key navigation: direction +1/-1 in
	// registration order. Returns nullptr if `from` is the only registered member.
	RadioButton* memberAfter(RadioButton* from, int direction) const;

private:
	int m_value = 0;
	std::function<void(int)> m_onChange;
	std::vector<std::pair<RadioButton*, int>> m_members;
};

class RadioButton : public Widget {
public:
	RadioButton(std::string label, RadioGroup* group, int valueWhenSelected);
	~RadioButton() override;

	RadioGroup* group() const { return m_group; }
	int valueWhenSelected() const { return m_valueWhenSelected; }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return true; }

private:
	RadioGroup* m_group;
	int m_valueWhenSelected;
};

} // namespace lightGraphics::ui
