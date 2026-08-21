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

// docs/gui/05-widgets.md note under "CompositeWidget": Row divides its rect horizontally
// by weight, minus theme.itemSpacing between children. Height is the max of children's
// preferred heights. Default weights are all equal.

#include "CompositeWidget.h"

#include <vector>

namespace lightGraphics::ui {

class Row : public CompositeWidget {
public:
	Row() = default;

	// Set custom weights for children (must match childCount() when layout is called).
	// Default: all equal.
	void setWeights(std::vector<float> weights) { m_weights = std::move(weights); }

	Vec2 preferredSize(const GuiContext& ctx) const override;
	void layout(const GuiContext& ctx) override;

private:
	std::vector<float> m_weights;
};

} // namespace lightGraphics::ui
