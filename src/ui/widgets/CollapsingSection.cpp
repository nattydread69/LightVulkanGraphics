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

#include <lightVulkanGraphics/ui/widgets/CollapsingSection.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <cmath>

namespace lightGraphics::ui {

CollapsingSection::CollapsingSection(std::string title, bool openInitially)
	: m_title(std::move(title)), m_open(openInitially) {
}

Vec2 CollapsingSection::preferredSize(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	float height = th.rowHeight;  // header height

	if (m_open) {
		// Add up preferred heights of all children
		for (std::size_t i = 0; i < childCount(); ++i) {
			height += childAt(i)->preferredSize(ctx).y;
		}
		// Add spacing between items
		if (childCount() > 0) {
			height += th.itemSpacing * static_cast<float>(childCount());
		}
	}

	return { 0.0f, height };
}

void CollapsingSection::update(GuiContext& ctx) {
	if (!m_enabled) {
		return;
	}

	const InputState& in = ctx.input();
	const int leftIdx = static_cast<int>(MouseButton::Left);

	// Header rect is at the top of m_bounds
	const Theme& th = ctx.theme();
	Rect headerRect{ m_bounds.x, m_bounds.y, m_bounds.w, th.rowHeight };

	// Click on header toggles open/closed
	if (ctx.activeId() == id() && in.mousePressed[leftIdx] && headerRect.contains(in.mousePos)) {
		m_open = !m_open;
	}

	// When closed, don't update children at all
	if (!m_open) {
		return;
	}

	// Forward update to visible children
	for (std::size_t i = 0; i < childCount(); ++i) {
		Widget* child = childAt(i);
		if (child->visible()) {
			child->update(ctx);
		}
	}
}

void CollapsingSection::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}

	const Theme& th = ctx.theme();
	Rect headerRect{ m_bounds.x, m_bounds.y, m_bounds.w, th.rowHeight };

	// Draw header background
	Color bgColor = ctx.hoveredId() == id() ? th.frameBgHovered : th.frameBg;
	dl.addRectFilled(headerRect, bgColor, th.rounding);
	dl.addRect(headerRect, effectivelyEnabled() && ctx.hoveredId() == id() ? th.accent : th.border, 1.0f, th.rounding);

	// Draw triangle glyph
	float triSize = th.fontSize * 0.4f;
	Vec2 triCenter{ m_bounds.x + th.framePadding + triSize, headerRect.centre().y };

	// Triangle points: right-pointing when closed, down-pointing when open
	Vec2 p0, p1, p2;
	if (m_open) {
		// Down-pointing triangle
		p0 = { triCenter.x - triSize, triCenter.y - triSize * 0.6f };
		p1 = { triCenter.x + triSize, triCenter.y - triSize * 0.6f };
		p2 = { triCenter.x, triCenter.y + triSize * 0.6f };
	} else {
		// Right-pointing triangle
		p0 = { triCenter.x - triSize * 0.6f, triCenter.y - triSize };
		p1 = { triCenter.x - triSize * 0.6f, triCenter.y + triSize };
		p2 = { triCenter.x + triSize * 0.6f, triCenter.y };
	}
	Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;
	dl.addTriangleFilled(p0, p1, p2, textColor);

	// Draw title
	Rect titleRect{ triCenter.x + triSize + th.framePadding, headerRect.y, headerRect.w - (triCenter.x + triSize + th.framePadding - m_bounds.x), headerRect.h };
	dl.addTextClipped(ctx.font(), th.fontSize, titleRect, textColor, m_title, Align::Start, Align::Center);

	// Draw children if open
	if (m_open) {
		for (std::size_t i = 0; i < childCount(); ++i) {
			Widget* child = childAt(i);
			if (child->visible()) {
				child->draw(dl, ctx);
			}
		}
	}
}

void CollapsingSection::layout(const GuiContext& ctx) {
	if (!m_open) {
		return;
	}

	const Theme& th = ctx.theme();

	// Children are laid out below the header, starting at y = m_bounds.y + rowHeight
	float y = m_bounds.y + th.rowHeight + th.itemSpacing;

	for (std::size_t i = 0; i < childCount(); ++i) {
		Widget* child = childAt(i);
		Vec2 prefSize = child->preferredSize(ctx);

		child->setBounds({ m_bounds.x, y, m_bounds.w, prefSize.y });
		child->layout(ctx);

		y += prefSize.y + th.itemSpacing;
	}
}

Widget* CollapsingSection::hitTestDeep(Vec2 p) {
	if (!m_visible || !m_enabled || !m_bounds.contains(p)) {
		return nullptr;
	}
	// Header height is typically ~22 (rowHeight) + ~2 for padding, so we use 24 as
	// a safe estimate. This allows header hit-testing without GuiContext access.
	float headerHeight = 24.0f;
	bool hitInHeader = p.y < m_bounds.y + headerHeight;

	if (!m_open) {
		// When closed, only the header is hit-testable; children are blocked
		return hitInHeader ? this : nullptr;
	}

	// When open, check children first (in reverse declaration order)
	for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
		Widget& child = **it;
		if (!child.visible()) {
			continue;
		}
		if (Widget* hit = child.hitTestDeep(p)) {
			return hit;
		}
	}
	// No child claimed the hit; if it's in the header area, return this for toggle clicks
	return hitInHeader ? this : nullptr;
}

} // namespace lightGraphics::ui
