#pragma once

// docs/gui/05-widgets.md, "LogView": a read-only, appendable, word-wrapping, internally
// scrollable text region for solver output -- what `Label`'s word-wrap and `Panel`'s own
// draggable scrollbar already do, combined into one widget that owns its own scroll
// state instead of relying on an owning Panel's.

#include "../Widget.h"

#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

class LogView : public Widget {
public:
	explicit LogView(std::string label);

	// Appends one line (no embedded newlines expected -- each push() is one logical
	// entry, wrapped independently of every other). Never trims immediately -- see
	// setMaxLines()'s comment on why eviction is deferred to whenever this is next
	// updated AND currently following the bottom.
	void push(std::string line);
	void clear();

	void setHeight(float px);            // default 160 (roughly 8 rows)
	// Ring-buffer cap on raw (pre-wrap) line count, 0 = unbounded. Eviction of the
	// oldest lines only ever happens while the view is following the bottom (the
	// default state, and where it returns to whenever scrollRow reaches the bottom
	// again) -- while a user has scrolled UP to read history, push() keeps appending
	// past the cap rather than evicting content out from under their current view.
	// This means memory is not strictly bounded while scrolled away; it is bounded
	// again the moment they return to the bottom (Key::End, or scrolling all the way
	// down). Default 500.
	void setMaxLines(std::size_t n) { m_maxLines = n; }
	void setWordWrap(bool wrap) { m_wordWrap = wrap; }

	// Raw (pre-wrap) line count currently held -- may exceed setMaxLines() while
	// !isFollowingBottom() (see its comment). Useful for a "N lines" status readout, not
	// just introspection.
	std::size_t lineCount() const { return m_rawLines.size(); }
	// True unless the user has scrolled away from the newest content (wheel, drag,
	// PageUp, Home) and hasn't yet returned (Key::End, or scrolling all the way back
	// down). Useful for a consumer to show its own "new output" indicator when false --
	// LogView itself draws no such affordance.
	bool isFollowingBottom() const { return m_followBottom; }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return true; }
	// Consumes the wheel while hovered, same convention every other internally-
	// scrolling widget in this library follows (Panel/DropDown/ListBox/ColorEdit).
	bool wantsWheel() const override { return true; }

private:
	enum class DragTarget { None, ScrollbarThumb };

	// Inset by theme.framePadding on all sides -- NOT yet narrowed for the scrollbar
	// gutter (see computeVisualLines()'s comment on why that narrowing is unconditional,
	// not "only when a scrollbar is actually needed").
	Rect contentRect(const GuiContext&) const;
	int visibleRowCount(const GuiContext&) const;
	Rect scrollbarTrackRect(const GuiContext&, const Rect& box) const;
	Rect scrollbarThumbRect(const GuiContext&, const Rect& box, int totalLines, int visibleRows) const;

	// Wraps every raw line independently and concatenates the results -- recomputed
	// fresh on every call (like Label's own wrapLines(), now shared via TextWrap.h)
	// rather than cached, since content and width can both change between calls.
	std::vector<std::string_view> computeVisualLines(const GuiContext&) const;

	std::deque<std::string> m_rawLines;
	std::size_t m_maxLines = 500;
	float m_heightPx = 160.0f;
	bool m_wordWrap = true;

	// True until the user manually scrolls away from the bottom (wheel, drag, PageUp,
	// Home) -- while true, update() keeps scrollRow pinned to the newest content AND
	// applies the deferred ring-buffer trim (see setMaxLines()'s comment). Returns to
	// true automatically once scrollRow reaches the bottom again by any means.
	bool m_followBottom = true;
	int m_scrollRow = 0;

	// Same single-WidgetId-plus-internal-drag-target shape as ColorEdit's SV-square/
	// hue-strip/alpha-strip dragging (docs/gui/05, "ColorEdit") -- not Panel's synthetic-
	// second-id pattern, because LogView (like ColorEdit) has no CHILD widgets whose own
	// hit-testing the scrollbar's pixel region could ever compete with; its own bounds()
	// already resolves the whole widget, scrollbar included, through the normal per-
	// panel hit-test walk.
	DragTarget m_dragTarget = DragTarget::None;
	float m_scrollbarDragStartMouseY = 0.0f;
	int m_scrollbarDragStartScrollRow = 0;
};

} // namespace lightGraphics::ui
