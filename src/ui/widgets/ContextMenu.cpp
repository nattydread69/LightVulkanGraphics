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

#include <lightVulkanGraphics/ui/widgets/ContextMenu.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>

namespace lightGraphics::ui {

void ContextMenu::addItem(std::string label, std::function<void()> onSelect) {
	Item item;
	item.label = std::move(label);
	item.onSelect = std::move(onSelect);
	m_items.push_back(std::move(item));
}

void ContextMenu::addSeparator() {
	Item item;
	item.isSeparator = true;
	m_items.push_back(std::move(item));
}

void ContextMenu::open(Vec2 screenPos) {
	m_openPosition = screenPos;
	m_openRequested = true;
}

bool ContextMenu::isOpen(const GuiContext& ctx) const {
	return ctx.popupOwner() == this;
}

Rect ContextMenu::computeMenuRect(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();

	float width = 0.0f;
	float height = 2.0f * th.framePadding;
	for (const Item& item : m_items) {
		if (item.isSeparator) {
			height += th.itemSpacing;
		} else {
			height += th.rowHeight;
			width = std::max(width, ctx.font().measureText(item.label, th.fontSize).x);
		}
	}
	// Padding on both sides of the widest label, plus a floor wide enough that a
	// one-word menu doesn't look like a sliver -- same spirit as Panel's own
	// minPanelWidth (8 * fontSize).
	width += 2.0f * th.framePadding + th.windowPadding;
	width = std::max(width, 8.0f * th.fontSize);

	// Clamped to stay fully on screen in BOTH axes -- unlike DropDown, whose popup only
	// ever needs to flip vertically, because its horizontal position is anchored to a
	// control that is already guaranteed on-screen. A context menu opens at an arbitrary
	// click point that could be anywhere, including a screen corner.
	Vec2 display = ctx.input().displaySize;
	float x = std::min(m_openPosition.x, std::max(0.0f, display.x - width));
	float y = std::min(m_openPosition.y, std::max(0.0f, display.y - height));
	x = std::max(0.0f, x);
	y = std::max(0.0f, y);

	return { x, y, width, height };
}

int ContextMenu::itemAtY(const GuiContext& ctx, const Rect& menu, float mouseY) const {
	const Theme& th = ctx.theme();
	float y = menu.y + th.framePadding;
	for (std::size_t i = 0; i < m_items.size(); ++i) {
		float h = m_items[i].isSeparator ? th.itemSpacing : th.rowHeight;
		if (mouseY >= y && mouseY < y + h) {
			return m_items[i].isSeparator ? -1 : static_cast<int>(i);
		}
		y += h;
	}
	return -1;
}

void ContextMenu::update(GuiContext& ctx) {
	bool isOpenNow = (ctx.popupOwner() == this);
	if (m_openRequested) {
		m_openRequested = false;
		isOpenNow = true;
	}
	if (!isOpenNow) {
		return;
	}

	// Re-registered every frame the menu stays open, from THIS frame's position -- same
	// mechanism DropDown's popup uses (docs/gui/05, "DropDown"), though a context menu's
	// position never actually moves once opened (there is no owning control it could
	// follow) -- the re-registration is only here because openPopup() itself expects it
	// every open frame, not because this rect can change.
	Rect menu = computeMenuRect(ctx);
	ctx.openPopup(this, menu);

	const InputState& in = ctx.input();
	const int leftIdx = static_cast<int>(MouseButton::Left);

	if (ctx.activeId() == id()) {
		if (in.mousePressed[leftIdx]) {
			m_pressStartedInPopup = menu.contains(in.mousePos);
		}
		if (in.mouseReleased[leftIdx] && m_pressStartedInPopup) {
			if (menu.contains(in.mousePos)) {
				int idx = itemAtY(ctx, menu, in.mousePos.y);
				if (idx >= 0 && m_items[static_cast<std::size_t>(idx)].onSelect) {
					m_items[static_cast<std::size_t>(idx)].onSelect();
				}
			}
			ctx.closePopup();
		}
	}
}

void ContextMenu::drawPopup(DrawList& dl, const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	Rect menu = ctx.popupRect();

	dl.addRectFilled(menu, th.frameBg, th.rounding);
	dl.addRect(menu, th.accent, 1.0f, th.rounding);

	bool hoveredHere = ctx.hoveredId() == id() && menu.contains(ctx.input().mousePos);
	int hoveredItem = hoveredHere ? itemAtY(ctx, menu, ctx.input().mousePos.y) : -1;

	float y = menu.y + th.framePadding;
	for (std::size_t i = 0; i < m_items.size(); ++i) {
		const Item& item = m_items[i];
		if (item.isSeparator) {
			float lineY = y + th.itemSpacing * 0.5f;
			dl.addLine({ menu.x + th.framePadding, lineY }, { menu.right() - th.framePadding, lineY },
			           th.border, 1.0f);
			y += th.itemSpacing;
			continue;
		}

		Rect rowRect{ menu.x + th.framePadding * 0.5f, y, menu.w - th.framePadding, th.rowHeight };
		if (static_cast<int>(i) == hoveredItem) {
			dl.addRectFilled(rowRect, th.frameBgHovered, th.rounding);
		}
		dl.addTextClipped(ctx.font(), th.fontSize, rowRect.insetXY(th.framePadding, 0.0f), th.text,
		                   item.label, Align::Start, Align::Center);
		y += th.rowHeight;
	}
}

} // namespace lightGraphics::ui
