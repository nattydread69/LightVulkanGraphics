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

#include <lightVulkanGraphics/ui/widgets/Row.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

Vec2 Row::preferredSize(const GuiContext& ctx) const {
	// Height is the max preferred height of the visible children. A hidden child takes
	// no space at all (same rule as Panel::layout()).
	float maxHeight = ctx.theme().rowHeight;
	for (std::size_t i = 0; i < childCount(); ++i) {
		if (!childAt(i)->visible()) {
			continue;
		}
		Vec2 childPref = childAt(i)->preferredSize(ctx);
		maxHeight = std::max(maxHeight, childPref.y);
	}

	return { 0.0f, maxHeight };
}

void Row::layout(const GuiContext& ctx) {
	const std::size_t count = childCount();
	if (count == 0) {
		return;
	}

	const Theme& th = ctx.theme();
	const bool customWeights = !m_weights.empty() && m_weights.size() == count;

	// Hidden children get no width and no spacing; the visible ones share the row.
	float totalWeight = 0.0f;
	std::size_t visibleCount = 0;
	for (std::size_t i = 0; i < count; ++i) {
		if (!childAt(i)->visible()) {
			continue;
		}
		totalWeight += customWeights ? m_weights[i] : 1.0f;
		++visibleCount;
	}
	if (visibleCount == 0) {
		return;
	}

	// Available width: subtract spacing between visible children
	float spacing = th.itemSpacing * static_cast<float>(visibleCount - 1);
	float availableWidth = m_bounds.w - spacing;

	// Position children left to right
	float x = m_bounds.x;
	for (std::size_t i = 0; i < count; ++i) {
		Widget* child = childAt(i);
		if (!child->visible()) {
			continue;
		}
		float weight = customWeights ? m_weights[i] : 1.0f;
		float childWidth = totalWeight > 0.0f ? availableWidth * (weight / totalWeight) : 0.0f;

		child->setBounds({ x, m_bounds.y, childWidth, m_bounds.h });
		child->layout(ctx);

		x += childWidth + th.itemSpacing;
	}
}

} // namespace lightGraphics::ui
