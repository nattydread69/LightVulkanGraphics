#pragma once

// docs/gui/05-widgets.md, "ListBox": an always-visible, scrollable selectable list --
// what DropDown's popup already draws (itemAtY(), the wheel/keyboard-only scrollbar, no
// drag-to-scroll), just never collapsed behind a control row. Picking a timestep, a
// dataset, a light: anywhere the selection itself is the point, not a setting tucked
// behind a combo box.
//
// Selection is immediate (Up/Down changes it directly, same convention RadioGroup's
// arrow-key handling already uses) rather than DropDown's highlight-then-commit-on-
// Enter model -- that model exists specifically to preview a choice before closing a
// popup; a ListBox never closes, so there is nothing to commit against.

#include "../Widget.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

class ListBox : public Widget {
public:
	// initialIndex < 0 means "nothing selected" -- a legitimate, meaningfully different
	// state from "the first item happens to be selected" (e.g. "no dataset chosen yet"),
	// unlike DropDown, whose control face must always show something and so can never
	// be indexless.
	ListBox(std::string label, std::vector<std::string> items, int initialIndex = -1);

	int selectedIndex() const { return m_selectedIndex; }
	std::string_view selectedText() const;
	void setItems(std::vector<std::string> items);
	// index < 0 clears the selection; otherwise clamped to a valid item index.
	void setSelectedIndex(int index, bool fireCallback = false);
	void setOnChange(std::function<void(int)> onChange) { m_onChange = std::move(onChange); }
	// How many rows tall the list is, regardless of item count -- fewer items leaves
	// blank rows, more items scrolls. Default 6.
	void setVisibleRows(int rows) { m_visibleRows = rows; }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return true; }
	// Consumes the wheel while hovered so the owning Panel doesn't also scroll
	// underneath it (docs/gui/05, "Panel", "Scrolling") -- same convention Slider/
	// DropDown/ColorEdit already follow for exactly this reason.
	bool wantsWheel() const override { return true; }

private:
	// mouseY must already be known to fall within `list` -- same "resolve which row from
	// raw coordinates, don't encode it in the hit-test return value" pattern DropDown's
	// itemAtY() uses for its popup.
	int itemAtY(const GuiContext&, const Rect& list, float mouseY) const;
	void clampScrollRow();
	void scrollToShowSelected();
	void moveSelection(int dir);   // wraps, immediate select + fire

	std::vector<std::string> m_items;
	int m_selectedIndex = -1;
	int m_scrollRow = 0;
	int m_visibleRows = 6;

	std::function<void(int)> m_onChange;
};

} // namespace lightGraphics::ui
