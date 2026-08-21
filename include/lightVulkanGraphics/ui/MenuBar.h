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

// docs/gui/05-widgets.md, "MenuBar": a single, global, always-on-top row of dropdown
// menus fixed to the top of the window (File / Edit / View / ... in the traditional
// desktop-app sense), NOT the per-panel TabBar (grouping content within one panel).
//
// Deliberately NOT a Widget. Every Widget belongs to exactly one Panel (Widget.h,
// Panel::add()/CompositeWidget::add() are the only things that ever set m_panel/
// m_parent), and GuiContext resolves ids, hit-testing, and the popup-owner mechanism
// entirely through that per-panel tree (findWidget() walks m_panels; the DropDown-style
// popup is keyed off a Widget* owner). A menu bar is not scoped to any one panel -- it
// must sit above every panel, independent of which ones exist or are visible -- so it is
// a second top-level kind of thing GuiContext owns directly, the same way Panel itself is
// not a Widget and gets its own special-cased handling throughout GuiContext.cpp. Its
// hit-testing/interaction is hard-coded into GuiContext::update()/wantsMouse() (ahead of
// the panel hit-test walk, exactly like the DropDown-style popup already is) rather than
// going through WidgetId/activeId capture at all.

#include "Types.h"

#include <functional>
#include <string>
#include <vector>

namespace lightGraphics::ui {

class DrawList;
class GuiContext;

class MenuBar {
public:
	// Handle returned by addMenu() -- mirrors TabBar::Tab: a thin, copyable reference
	// into MenuBar's own storage, not an owning object itself.
	class Menu {
	public:
		// Appends a clickable row; `onSelect` fires once, on release-inside (same
		// convention every clickable surface in this library uses), and the menu closes
		// immediately after. `shortcutHint`, if non-empty, draws right-aligned in the row
		// (e.g. "Ctrl+S") -- DISPLAY ONLY, this does not read or dispatch the actual key
		// combination; wiring that up is the consumer's own job via GuiContext::input()
		// or injectKey(), same as everywhere else in this library.
		void addItem(std::string label, std::function<void()> onSelect, std::string shortcutHint = "");
		// A thin divider between groups of items -- not selectable.
		void addSeparator();

	private:
		friend class MenuBar;
		Menu(MenuBar& owner, std::size_t index) : m_owner(&owner), m_index(index) {}
		MenuBar* m_owner;
		std::size_t m_index;
	};

	// Appends a new top-level menu (e.g. "File") to the end of the bar and returns a
	// handle for adding its items. Order of addMenu() calls is left-to-right draw order.
	Menu addMenu(std::string title);
	std::size_t menuCount() const { return m_menus.size(); }

	// 0 if menuCount() == 0 -- a consumer who never touches the menu bar pays nothing and
	// sees no change to their panel layout; one who does can offset their own panels'
	// starting y by this to avoid sitting underneath the bar (the bar does not do this
	// for them -- see docs/gui/05-widgets.md, "MenuBar", "Layout is the consumer's job").
	float height(const GuiContext& ctx) const;

	// ---- geometry queries (public: useful for a consumer's own hit-testing/rendering
	// decisions, and what tests drive interaction through -- mirrors ctx.popupRect()) ----
	// The full-width row itself, y in [0, height(ctx)).
	Rect barRect(const GuiContext& ctx) const;
	// One top-level title's own clickable rect within the bar, e.g. "File"'s.
	Rect menuTitleRect(const GuiContext& ctx, std::size_t menuIndex) const;
	// The currently open dropdown's rect, or an empty Rect if openIndex() < 0.
	Rect openMenuRect(const GuiContext& ctx) const;
	// Index into [0, menuCount()) of the currently open menu, or -1 if none is open.
	int openIndex() const { return m_openMenuIndex; }

private:
	// ---- internal: called by GuiContext, not part of the consumer-facing surface ----
	friend class GuiContext;

	int itemAtY(const GuiContext& ctx, const Rect& menu, float mouseY) const;

	bool isMenuOpen() const { return m_openMenuIndex >= 0; }
	void closeMenu() { m_openMenuIndex = -1; }
	// True iff the bar row itself (any title) OR the currently open dropdown was under
	// the cursor as of the last update() call -- cached, not recomputed, so wantsMouse()
	// (const) can read it without re-deriving hit state itself. Mirrors how
	// GuiContext::m_hoveredPanel is a per-frame-cached value read later the same way.
	bool isHovered() const { return m_hovered; }

	// Resolves hover/press/release against the bar row and any open dropdown, opens/
	// switches/closes menus, and fires the selected item's callback on release-inside.
	// A no-op if menuCount() == 0 or a modal panel is currently active (docs/gui/05-
	// widgets.md, "MenuBar", "Modal panels") -- deliberately not gated behind capture/
	// activeId at all, see this header's top comment.
	void update(GuiContext& ctx);
	// The bar row -- titles, hover/open highlight. Always drawn when menuCount() > 0,
	// regardless of whether a menu is currently open.
	void draw(DrawList& dl, const GuiContext& ctx) const;
	// The currently open dropdown's items, if any -- drawn separately (into the overlay
	// list, after every panel and even a modal's backdrop) so it always floats on top,
	// the same reason DropDown's own popup escapes its owner's z-order.
	void drawOpenMenu(DrawList& dl, const GuiContext& ctx) const;

	struct Item {
		std::string label;
		std::function<void()> onSelect;
		std::string shortcutHint;
		bool isSeparator = false;
	};
	struct MenuData {
		std::string title;
		std::vector<Item> items;
	};

	std::vector<MenuData> m_menus;
	int m_openMenuIndex = -1;   // -1 = nothing open
	bool m_hovered = false;
	// Recorded at press time, consulted at release -- same reasoning as ContextMenu's
	// m_pressStartedInPopup: only a press that itself started inside the open dropdown
	// should let the matching release select an item.
	bool m_pressStartedInOpenMenu = false;
};

} // namespace lightGraphics::ui
