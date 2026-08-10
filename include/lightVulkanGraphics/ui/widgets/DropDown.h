#pragma once

// docs/gui/05-widgets.md, "DropDown". Deliberately does NOT derive from CompositeWidget
// -- its popup is not a child widget tree, just a second thing this one Widget draws
// (via drawPopup(), see Widget.h) and hit-tests (via GuiContext's popup-rect check, see
// GuiContext::update()) outside its own m_bounds. The control row and the popup share a
// single WidgetId; "which item" is answered by DropDown's own coordinate math
// (itemAtY()), not by separate per-item Widgets.
//
// Popup mechanism (docs/gui/05, "DropDown"): GuiContext holds `popupOwner`/`popupRect`;
// DropDown re-registers both via ctx.openPopup(this, rect) every frame its popup is
// open, recomputing `rect` from its own CURRENT bounds each time -- this is what makes
// the popup FOLLOW the control if the owning panel is dragged, scrolled, or resized
// while open (the alternative, closing the popup on any such move, was rejected: it
// would require extra move-detection bookkeeping GuiContext doesn't otherwise need,
// where following falls out for free from Panel::layout() already recomputing every
// widget's bounds unconditionally every frame).
//
// Escape ordering (docs/gui/04-input-and-events.md's phase 8 note): unlike TextBox,
// DropDown needs NO m_wasFocused-style entering-state comparison. GuiContext's own
// Escape default (docs/gui/04, "Keyboard") now closes the popup itself, before
// DropDown's update() ever runs -- DropDown only ever reads the CURRENT
// ctx.popupOwner() == this each frame, which already reflects that by the time it
// looks. There is no second cause of the popup closing that DropDown needs to tell
// apart from Escape (a click outside is also handled entirely inside GuiContext::
// update(), before DropDown's own update() runs), so there is nothing to disambiguate.

#include "../Widget.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

class DropDown : public Widget {
public:
	DropDown(std::string label, std::vector<std::string> items, int initialIndex = 0);

	int selectedIndex() const { return m_selectedIndex; }
	std::string_view selectedText() const;
	void setItems(std::vector<std::string> items);
	void setSelectedIndex(int index, bool fireCallback = false);
	void setOnChange(std::function<void(int)> onChange) { m_onChange = std::move(onChange); }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;
	void drawPopup(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return true; }
	// docs/gui/05: the popup scrolls on the wheel while hovered; the owning Panel must not
	// also scroll underneath it (docs/gui/09 phase 9, "Scrolling").
	bool wantsWheel() const override { return true; }

private:
	// docs/gui/05: "scrolls if more than 12 items".
	static constexpr int kMaxVisibleItems = 12;

	Rect computePopupRect(const GuiContext&) const;
	// Byte... rather, ITEM index at popup-local screen position `mouseY`, or -1 if
	// `mouseY` doesn't land on any row. Only ever called with a point already known to
	// be inside `popupRect` (docs/gui/05: "items beyond the visible region are not
	// hit-testable" -- guaranteed structurally here, since popupRect's own height is
	// exactly kMaxVisibleItems (or fewer) rows tall, so a point outside that range is
	// never inside popupRect in the first place, let alone passed to this function).
	int itemAtY(const GuiContext&, const Rect& popupRect, float mouseY) const;
	void moveHighlight(int dir);          // wraps at both ends -- see .cpp
	void scrollToShowHighlight();
	void clampScrollRow();

	std::vector<std::string> m_items;
	int m_selectedIndex = 0;
	int m_highlightIndex = 0;   // distinct from m_selectedIndex -- see docs/gui/05
	int m_scrollRow = 0;        // index of the first visible item

	// Recorded at press time (rect.contains(pressPos)) and consulted at release time.
	// Capture (activeId == id()) is held for the WHOLE press-to-release span of any
	// click that starts on this widget, including the control click that opens the
	// popup in the first place -- without this flag, that opening click's OWN release
	// would be misread as "released inside the popup" and immediately re-close what it
	// just opened. Only a press that itself started inside the popup should let the
	// matching release select an item / close.
	bool m_pressStartedInPopup = false;

	std::function<void(int)> m_onChange;
};

} // namespace lightGraphics::ui
