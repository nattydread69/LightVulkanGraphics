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

// docs/gui/05-widgets.md, "Vec3Field": Three DragValues in a row, labelled X/Y/Z with
// the conventional red/green/blue tint on each sub-field's border.

#include "CompositeWidget.h"

#include <glm/glm.hpp>

#include <functional>

namespace lightGraphics::ui {

class DragValue;  // forward decl

class Vec3Field : public CompositeWidget {
public:
	explicit Vec3Field(std::string label, glm::vec3 initial);

	glm::vec3 value() const;
	void setValue(glm::vec3, bool fireCallback = false);
	void bind(glm::vec3* target) { m_bindTarget = target; }
	void setOnChange(std::function<void(glm::vec3)> onChange) { m_onChange = std::move(onChange); }

	Vec2 preferredSize(const GuiContext& ctx) const override;
	void layout(const GuiContext& ctx) override;

private:
	glm::vec3* m_bindTarget = nullptr;
	std::function<void(glm::vec3)> m_onChange;
};

} // namespace lightGraphics::ui
