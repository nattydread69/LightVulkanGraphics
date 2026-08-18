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

void MenuBar::Menu::addItem(std::string label, std::function<void()> onSelect, std::string shortcutHint) {
	Item item;
	item.label = std::move(label);
	item.onSelect = std::move(onSelect);
	item.shortcutHint = std::move(shortcutHint);
	m_owner->m_menus[m_index].items.push_back(std::move(item));
}

void MenuBar::Menu::addSeparator() {
	Item item;
	item.isSeparator = true;
	m_owner->m_menus[m_index].items.push_back(std::move(item));
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
	if (m_menus.empty()) {
		return;
	}
	// docs/gui/05-widgets.md, "MenuBar", "Modal panels": stays visually present (still
	// drawn every frame -- see draw()/drawOpenMenu(), which don't check this) but non-
	// interactive while a modal panel blocks everything else, matching the "modal blocks
	// INTERACTION, not the rest of the app" idiom GuiContext::update() already applies to
	// background panels' own update() calls.
	if (ctx.activeModalPanel() != nullptr) {
		return;
	}

	const InputState& in = ctx.input();
	Vec2 mouse = in.mousePos;
	const int leftIdx = static_cast<int>(MouseButton::Left);
	bool leftPressed = in.mousePressed[leftIdx];
	bool leftReleased = in.mouseReleased[leftIdx];

	Rect bar = barRect(ctx);
	bool overBar = bar.contains(mouse);

	Rect openRect = isMenuOpen() ? openMenuRect(ctx) : Rect{};
	bool overOpenMenu = isMenuOpen() && openRect.contains(mouse);

	m_hovered = overBar || overOpenMenu;

	int hoveredTitle = -1;
	if (overBar) {
		for (std::size_t i = 0; i < m_menus.size(); ++i) {
			if (menuTitleRect(ctx, i).contains(mouse)) {
				hoveredTitle = static_cast<int>(i);
				break;
			}
		}
	}

	if (leftPressed) {
		if (hoveredTitle >= 0) {
			// A second click of the ALREADY-open title closes it (DropDown's own "closes
			// on a second click of the control" convention); a click on any OTHER title
			// always opens/switches to it.
			m_openMenuIndex = (m_openMenuIndex == hoveredTitle) ? -1 : hoveredTitle;
		} else if (overOpenMenu) {
			m_pressStartedInOpenMenu = true;
		} else {
			// Press outside both the bar and the open dropdown -- dismiss, same as a
			// widget popup closing on an outside press (GuiContext::update()).
			m_openMenuIndex = -1;
		}
	}

	// Hover-switch: once a menu is open, moving onto a SIBLING title with no click
	// switches to it -- the standard menu-bar feel the "keep it simple" scope still
	// wants. Unconditional on !leftPressed: a press this same frame already resolved its
	// own open/close/toggle immediately above, so this must not re-decide anything then.
	if (!leftPressed && isMenuOpen() && hoveredTitle >= 0 && hoveredTitle != m_openMenuIndex) {
		m_openMenuIndex = hoveredTitle;
	}

	if (leftReleased) {
		if (m_pressStartedInOpenMenu && overOpenMenu) {
			int idx = itemAtY(ctx, openRect, mouse.y);
			if (idx >= 0) {
				const Item& item = m_menus[static_cast<std::size_t>(m_openMenuIndex)].items[static_cast<std::size_t>(idx)];
				if (item.onSelect) {
					item.onSelect();
				}
			}
			m_openMenuIndex = -1;
		}
		m_pressStartedInOpenMenu = false;
	}
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
	if (!isMenuOpen()) {
		return;
	}
	const Theme& th = ctx.theme();
	Rect menu = openMenuRect(ctx);
	const MenuData& data = m_menus[static_cast<std::size_t>(m_openMenuIndex)];

	dl.addRectFilled(menu, th.frameBg, th.rounding);
	dl.addRect(menu, th.accent, 1.0f, th.rounding);

	Vec2 mouse = ctx.input().mousePos;
	int hoveredItem = menu.contains(mouse) ? itemAtY(ctx, menu, mouse.y) : -1;

	float y = menu.y + th.framePadding;
	for (std::size_t i = 0; i < data.items.size(); ++i) {
		const Item& item = data.items[i];
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
		Rect textRect = rowRect.insetXY(th.framePadding, 0.0f);
		dl.addTextClipped(ctx.font(), th.fontSize, textRect, th.text, item.label, Align::Start, Align::Center);
		if (!item.shortcutHint.empty()) {
			dl.addTextClipped(ctx.font(), th.fontSize, textRect, th.textDisabled, item.shortcutHint, Align::End,
			                   Align::Center);
		}
		y += th.rowHeight;
	}
}

} // namespace lightGraphics::ui
