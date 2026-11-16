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

#include <lightVulkanGraphics/ui/MenuBar.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>

namespace lightGraphics::ui {

MenuBar::Menu MenuBar::addMenu(std::string title) {
	MenuData data;
	data.title = std::move(title);
	m_menus.push_back(std::move(data));
	return Menu(*this, m_menus.size() - 1);
}

std::vector<MenuBar::Item>& MenuBar::itemsAt(std::size_t menu, const std::vector<std::size_t>& path) {
	auto* items = &m_menus.at(menu).items;
	for (std::size_t index : path) {
		items = &items->at(index).children;
	}
	return *items;
}

void MenuBar::Menu::addItem(std::string label, std::function<void()> onSelect, std::string shortcutHint) {
	Item item;
	item.label = std::move(label);
	item.onSelect = std::move(onSelect);
	item.shortcutHint = std::move(shortcutHint);
	m_owner->itemsAt(m_index, m_path).push_back(std::move(item));
}

void MenuBar::Menu::addSeparator() {
	Item item;
	item.isSeparator = true;
	m_owner->itemsAt(m_index, m_path).push_back(std::move(item));
}

MenuBar::Menu MenuBar::Menu::addSubmenu(std::string label) {
	auto& items = m_owner->itemsAt(m_index, m_path);
	Item item;
	item.label = std::move(label);
	items.push_back(std::move(item));
	auto path = m_path;
	path.push_back(items.size() - 1);
	return Menu(*m_owner, m_index, std::move(path));
}

const std::vector<MenuBar::Item>* MenuBar::itemsAtDepth(std::size_t depth) const {
	if (!isMenuOpen()) return nullptr;
	auto const* items = &m_menus[static_cast<std::size_t>(m_openMenuIndex)].items;
	for (std::size_t level = 0; level < depth; ++level) {
		if (level >= m_openPath.size() || m_openPath[level] >= items->size()) return nullptr;
		items = &(*items)[m_openPath[level]].children;
		if (items->empty()) return nullptr;
	}
	return items;
}

std::vector<Rect> MenuBar::openRects(const GuiContext& ctx) const {
	std::vector<Rect> result;
	if (!isMenuOpen()) return result;
	result.push_back(openMenuRect(ctx));
	const Theme& th = ctx.theme();
	Vec2 display = ctx.input().displaySize;
	for (std::size_t depth = 0; depth < m_openPath.size(); ++depth) {
		auto const* parent = itemsAtDepth(depth);
		auto const* children = itemsAtDepth(depth + 1);
		if (!parent || !children || m_openPath[depth] >= parent->size()) break;
		float width = 8.0f * th.fontSize;
		float height = 2.0f * th.framePadding;
		for (auto const& item : *children) {
			height += item.isSeparator ? th.itemSpacing : th.rowHeight;
			if (!item.isSeparator) {
				float w = ctx.font().measureText(item.label, th.fontSize).x;
				if (!item.shortcutHint.empty()) w += th.windowPadding + ctx.font().measureText(item.shortcutHint, th.fontSize).x;
				if (!item.children.empty()) w += 2.0f * th.windowPadding;
				width = std::max(width, w + 2.0f * th.framePadding + th.windowPadding);
			}
		}
		Rect const prior = result.back();
		float y = prior.y;
		for (std::size_t i = 0; i < m_openPath[depth]; ++i) y += (*parent)[i].isSeparator ? th.itemSpacing : th.rowHeight;
		float x = prior.right();
		if (x + width > display.x) x = prior.x - width;
		x = std::max(0.0f, x);
		y = std::max(0.0f, std::min(y, display.y - height));
		result.push_back({x, y, width, height});
	}
	return result;
}

bool MenuBar::containsOpenMenu(const GuiContext& ctx, Vec2 point) const {
	for (Rect const& rect : openRects(ctx)) if (rect.contains(point)) return true;
	return false;
}

float MenuBar::height(const GuiContext& ctx) const {
	return m_menus.empty() ? 0.0f : ctx.theme().titleBarHeight;
}

Rect MenuBar::barRect(const GuiContext& ctx) const {
	Vec2 display = ctx.input().displaySize;
	return { 0.0f, 0.0f, display.x, ctx.theme().titleBarHeight };
}

Rect MenuBar::menuTitleRect(const GuiContext& ctx, std::size_t menuIndex) const {
	const Theme& th = ctx.theme();
	float x = 0.0f;
	for (std::size_t i = 0; i < menuIndex; ++i) {
		x += ctx.font().measureText(m_menus[i].title, th.fontSize).x + 2.0f * th.windowPadding;
	}
	float width = ctx.font().measureText(m_menus[menuIndex].title, th.fontSize).x + 2.0f * th.windowPadding;
	return { x, 0.0f, width, th.titleBarHeight };
}

Rect MenuBar::openMenuRect(const GuiContext& ctx) const {
	if (!isMenuOpen()) {
		return {};
	}
	const Theme& th = ctx.theme();
	const MenuData& menu = m_menus[static_cast<std::size_t>(m_openMenuIndex)];

	float width = 0.0f;
	float height = 2.0f * th.framePadding;
	for (const Item& item : menu.items) {
		if (item.isSeparator) {
			height += th.itemSpacing;
		} else {
			height += th.rowHeight;
			float w = ctx.font().measureText(item.label, th.fontSize).x;
			if (!item.children.empty()) w += 2.0f * th.windowPadding;
			if (!item.shortcutHint.empty()) {
				// windowPadding as the gap between label and shortcut hint -- same token
				// ContextMenu-adjacent widgets already use for "comfortable gap between
				// two pieces of text in one row" (see its own width computation).
				w += th.windowPadding + ctx.font().measureText(item.shortcutHint, th.fontSize).x;
			}
			width = std::max(width, w);
		}
	}
	width += 2.0f * th.framePadding + th.windowPadding;
	width = std::max(width, 8.0f * th.fontSize);

	Rect title = menuTitleRect(ctx, static_cast<std::size_t>(m_openMenuIndex));
	Vec2 display = ctx.input().displaySize;
	// Only needs to flip/clamp horizontally, same reasoning DropDown's own popup uses --
	// the owning title is already guaranteed on-screen (it's part of the bar), and the
	// dropdown always opens directly below the bar, never above it.
	float x = std::min(title.x, std::max(0.0f, display.x - width));
	x = std::max(0.0f, x);
	float y = title.bottom();

	return { x, y, width, height };
}

int MenuBar::itemAtY(const GuiContext& ctx, const Rect& menu, float mouseY) const {
	if (!isMenuOpen()) {
		return -1;
	}
	const Theme& th = ctx.theme();
	const MenuData& data = m_menus[static_cast<std::size_t>(m_openMenuIndex)];
	float y = menu.y + th.framePadding;
	for (std::size_t i = 0; i < data.items.size(); ++i) {
		float h = data.items[i].isSeparator ? th.itemSpacing : th.rowHeight;
		if (mouseY >= y && mouseY < y + h) {
			return data.items[i].isSeparator ? -1 : static_cast<int>(i);
		}
		y += h;
	}
	return -1;
}

void MenuBar::update(GuiContext& ctx) {
	m_hovered = false;
	if (m_menus.empty() || ctx.activeModalPanel() != nullptr) return;
	const InputState& in = ctx.input();
	Vec2 mouse = in.mousePos;
	int const button = static_cast<int>(MouseButton::Left);
	bool const pressed = in.mousePressed[button];
	bool const released = in.mouseReleased[button];
	bool const overBar = barRect(ctx).contains(mouse);
	int hoveredTitle = -1;
	if (overBar) for (std::size_t i = 0; i < m_menus.size(); ++i) {
		if (menuTitleRect(ctx, i).contains(mouse)) { hoveredTitle = static_cast<int>(i); break; }
	}
	auto rects = openRects(ctx);
	int depth = -1;
	for (std::size_t i = 0; i < rects.size(); ++i) if (rects[i].contains(mouse)) depth = static_cast<int>(i);
	m_hovered = overBar || depth >= 0;
	if (pressed) {
		if (hoveredTitle >= 0) {
			if (m_openMenuIndex == hoveredTitle) closeMenu();
			else { m_openMenuIndex = hoveredTitle; m_openPath.clear(); }
		} else if (depth >= 0) m_pressStartedInOpenMenu = true;
		else closeMenu();
	}
	if (!pressed && isMenuOpen() && hoveredTitle >= 0 && hoveredTitle != m_openMenuIndex) {
		m_openMenuIndex = hoveredTitle;
		m_openPath.clear();
	}
	if (depth >= 0 && isMenuOpen()) {
		auto const* items = itemsAtDepth(static_cast<std::size_t>(depth));
		int index = -1;
		bool overSeparator = false;
		float y = rects[static_cast<std::size_t>(depth)].y + ctx.theme().framePadding;
		if (items) for (std::size_t i = 0; i < items->size(); ++i) {
			float const h = (*items)[i].isSeparator ? ctx.theme().itemSpacing : ctx.theme().rowHeight;
			if (mouse.y >= y && mouse.y < y + h) { if ((*items)[i].isSeparator) overSeparator = true; else index = static_cast<int>(i); break; }
			y += h;
		}
		if (index >= 0) {
			Item const& item = (*items)[static_cast<std::size_t>(index)];
			if (!item.children.empty()) {
				m_openPath.resize(static_cast<std::size_t>(depth));
				m_openPath.push_back(static_cast<std::size_t>(index));
			} else m_openPath.resize(static_cast<std::size_t>(depth));
			if (released && m_pressStartedInOpenMenu && item.children.empty()) {
				if (item.onSelect) item.onSelect();
				closeMenu();
			}
		}
		// Releasing on a separator isn't selectable but still closes the menu (ContextMenu convention).
		if (index < 0 && overSeparator && released && m_pressStartedInOpenMenu) closeMenu();
	}
	if (released) m_pressStartedInOpenMenu = false;
}

void MenuBar::draw(DrawList& dl, const GuiContext& ctx) const {
	if (m_menus.empty()) {
		return;
	}
	const Theme& th = ctx.theme();
	Rect bar = barRect(ctx);
	dl.addRectFilled(bar, th.titleBg);

	Vec2 mouse = ctx.input().mousePos;
	for (std::size_t i = 0; i < m_menus.size(); ++i) {
		Rect title = menuTitleRect(ctx, i);
		bool isOpenTitle = (m_openMenuIndex == static_cast<int>(i));
		if (isOpenTitle) {
			dl.addRectFilled(title, th.frameBgActive);
		} else if (title.contains(mouse)) {
			dl.addRectFilled(title, th.frameBgHovered);
		}
		dl.addTextClipped(ctx.font(), th.fontSize, title, th.text, m_menus[i].title, Align::Center, Align::Center);
	}
}

void MenuBar::drawOpenMenu(DrawList& dl, const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	auto rects = openRects(ctx);
	Vec2 mouse = ctx.input().mousePos;
	for (std::size_t depth = 0; depth < rects.size(); ++depth) {
		Rect const menu = rects[depth];
		auto const* items = itemsAtDepth(depth);
		if (!items) break;
		dl.addRectFilled(menu, th.frameBg, th.rounding);
		dl.addRect(menu, th.accent, 1.0f, th.rounding);
		float y = menu.y + th.framePadding;
		for (std::size_t i = 0; i < items->size(); ++i) {
			Item const& item = (*items)[i];
			if (item.isSeparator) {
				float lineY = y + th.itemSpacing * 0.5f;
				dl.addLine({menu.x + th.framePadding, lineY}, {menu.right() - th.framePadding, lineY}, th.border, 1.0f);
				y += th.itemSpacing;
				continue;
			}
			Rect row{menu.x + th.framePadding * 0.5f, y, menu.w - th.framePadding, th.rowHeight};
			bool const selected = depth < m_openPath.size() && m_openPath[depth] == i;
			if (row.contains(mouse) || selected) dl.addRectFilled(row, th.frameBgHovered, th.rounding);
			Rect text = row.insetXY(th.framePadding, 0.0f);
			dl.addTextClipped(ctx.font(), th.fontSize, text, th.text, item.label, Align::Start, Align::Center);
			std::string const hint = item.children.empty() ? item.shortcutHint : ">";
			if (!hint.empty()) dl.addTextClipped(ctx.font(), th.fontSize, text, th.textDisabled, hint, Align::End, Align::Center);
			y += th.rowHeight;
		}
	}
}

} // namespace lightGraphics::ui
