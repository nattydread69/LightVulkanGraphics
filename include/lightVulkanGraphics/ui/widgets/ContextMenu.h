#pragma once

// docs/gui/05-widgets.md, "ContextMenu": a right-click popup with no control row of its
// own -- unlike DropDown/ColorEdit, nothing about it is ever visible until open() is
// called. Reuses GuiContext::openPopup()/drawPopup() exactly as they already work
// (docs/gui/05, "DropDown"'s class comment); the only thing new here is that the
// "owner" widget has zero on-screen presence outside of when its popup is open.
//
// GuiContext has no built-in notion of a right-click gesture -- MouseButton::Right isn't
// read anywhere in its capture/focus resolution, only Left is. This is deliberate, not
// an oversight this class works around: detecting "was there a right-click, and where"
// is left entirely to the caller (ctx.input().mouseReleased[MouseButton::Right]), which
// is already public API. A widget can drive its own context menu from inside its own
// update() (`if (hitTest(...) && ctx.input().mouseReleased[MouseButton::Right])
// menu.open(ctx.input().mousePos);`), or application code can drive one for a right-
// click anywhere, panel or bare scene alike -- both are the exact same call.

#include "../Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace lightGraphics::ui {

class ContextMenu : public Widget {
public:
	// Appends a clickable row; `onSelect` fires once, on release-inside (same convention
	// every clickable surface in this library uses), and the menu closes immediately
	// after -- there is no persisted "selected item" the way DropDown/ListBox have one,
	// since a context menu is a one-shot action picker, not a value widget.
	void addItem(std::string label, std::function<void()> onSelect);
	// A thin divider between groups of items -- not selectable, and does not occupy a
	// full row's height (theme.itemSpacing instead of theme.rowHeight).
	void addSeparator();

	// Opens at `screenPos` -- typically ctx.input().mousePos at the moment a right-click
	// was detected. Re-opening while already open just repositions it. May take one
	// frame to become visible depending on when this is called relative to this frame's
	// GuiContext::update() (docs/gui/05, "ContextMenu") -- unobservable in practice.
	void open(Vec2 screenPos);
	// True the same frame this is the menu GuiContext currently has open.
	bool isOpen(const GuiContext& ctx) const;

	// No control row to lay out or draw -- see this header's class comment.
	Vec2 preferredSize(const GuiContext&) const override { return { 0.0f, 0.0f }; }
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override {}
	void drawPopup(DrawList&, const GuiContext&) const override;

	// Never Tab-focusable -- opened by a mouse gesture the caller detects itself, not by
	// keyboard navigation landing on an (invisible, zero-size) control row.
	bool acceptsFocus() const override { return false; }
	// Deliberately NOT overridden to false: a press inside the open popup must still
	// claim GuiContext::activeId() the same way DropDown's popup rows do (GuiContext's
	// popup-hit branch resolves the hovered widget to the popup's OWNER directly,
	// bypassing this widget's own -- degenerate, zero-size -- hitTest() entirely), or a
	// click on an item could never be told apart from a click that should close the menu.

private:
	struct Item {
		std::string label;
		std::function<void()> onSelect;
		bool isSeparator = false;
	};

	Rect computeMenuRect(const GuiContext&) const;
	// mouseY must already be known to fall within `menu` -- same pattern DropDown's own
	// itemAtY() uses. Returns -1 for a separator row (not selectable) or out of range.
	int itemAtY(const GuiContext&, const Rect& menu, float mouseY) const;

	std::vector<Item> m_items;
	Vec2 m_openPosition;
	bool m_openRequested = false;
	// Recorded at press time and consulted at release time -- same reasoning as
	// DropDown's m_pressStartedInPopup: only a press that itself started inside the
	// popup should let the matching release select an item.
	bool m_pressStartedInPopup = false;
};

} // namespace lightGraphics::ui
