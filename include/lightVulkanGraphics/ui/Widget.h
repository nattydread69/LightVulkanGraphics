#pragma once

// Layer 3: the base every concrete widget derives from. See docs/gui/05-widgets.md, "The
// base class", and docs/gui/06-layout-and-theme.md for splitRow() and the absolute-
// placement escape hatch.

#include "Types.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

class DrawList;
class GuiContext;
class Panel;

class Widget {
public:
	Widget();
	virtual ~Widget() = default;

	Widget(const Widget&) = delete;
	Widget& operator=(const Widget&) = delete;

	WidgetId id() const noexcept { return m_id; }
	Panel* panel() const noexcept { return m_panel; }

	// ---- layout ----
	virtual Vec2 preferredSize(const GuiContext&) const = 0;
	virtual void setBounds(const Rect& r) { m_bounds = r; }
	// Called by the framework's layout pass immediately after setBounds() -- for every
	// visible top-level widget in a panel, Panel::layout() calls this right after EACH
	// setBounds() call it makes, including the bounds-override path (docs/gui/06,
	// "Absolute placement escape hatch"), so it always sees THIS frame's bounds, never
	// one cached from an earlier call. A leaf widget's position is fully decided by
	// setBounds() alone, so the default here is a no-op; CompositeWidget overrides it to
	// position m_children from m_bounds using the ctx passed here (theme metrics, font
	// measurement -- neither available to setBounds() itself). See docs/gui/05-widgets.md,
	// "CompositeWidget".
	virtual void layout(const GuiContext&) {}
	Rect bounds() const noexcept { return m_bounds; }

	// Absolute placement escape hatch (docs/gui/06, "Absolute placement escape hatch").
	// When set, Panel::layout() skips this widget's row in the vertical stack and places
	// it at `panelOrigin + panelRelative` instead.
	void setBoundsOverride(const Rect& panelRelative);
	void clearBoundsOverride();
	bool hasBoundsOverride() const noexcept { return m_boundsOverride.has_value(); }
	Rect boundsOverride() const noexcept { return m_boundsOverride.value_or(Rect{}); }

	// ---- interaction ----
	virtual void update(GuiContext&) {}
	virtual bool acceptsFocus()   const { return false; }
	virtual bool acceptsCapture() const { return true;  }
	virtual bool wantsTextInput() const { return false; }
	virtual bool hitTest(Vec2 p) const { return m_bounds.contains(p); }

	// ---- composite support (docs/gui/05-widgets.md, "CompositeWidget") ----
	// Every widget is a leaf by default: hitTestDeep() reports itself when hitTest()
	// matches, and there is nothing for findDescendant()/collectFocusable() to recurse
	// into. CompositeWidget overrides all three so that GuiContext's hit-testing,
	// activeId/focusedId lookup (findWidget), and Tab-cycling (updateFocusNavigation)
	// can resolve all the way down to whichever CHILD a point, id, or tab stop actually
	// belongs to -- without GuiContext needing to know composites exist as a concept.
	// GuiContext calls these instead of hitTest()/a manual id-equality check/
	// acceptsFocus() directly wherever it walks a panel's top-level widget list.
	virtual Widget* hitTestDeep(Vec2 p) { return hitTest(p) ? this : nullptr; }
	virtual Widget* findDescendant(WidgetId) { return nullptr; }
	virtual void collectFocusable(std::vector<Widget*>& out) {
		if (visible() && enabled() && acceptsFocus()) {
			out.push_back(this);
		}
	}

	// ---- drawing ----
	virtual void draw(DrawList&, const GuiContext&) const = 0;

	// Called by GuiContext::endFrame() to draw the CURRENT popup owner's popup content
	// (docs/gui/05-widgets.md, "DropDown") into the overlay draw list, after every panel
	// has already drawn and inside a clip rect GuiContext has already pushed via
	// pushClipRect(popupRect(), false) -- see GuiContext::openPopup(). Default is a
	// no-op: a widget only needs this if it ever calls ctx.openPopup(this, ...).
	// DropDown is currently the only override.
	virtual void drawPopup(DrawList&, const GuiContext&) const {}

	// ---- common state ----
	void setEnabled(bool enabled) { m_enabled = enabled; }
	bool enabled() const noexcept { return m_enabled; }
	// True iff this widget AND every composite ancestor (docs/gui/05, "CompositeWidget")
	// is enabled. draw() implementations should read THIS, not enabled(), to choose
	// their disabled-vs-normal colour: a disabled CompositeWidget stops calling its
	// children's update()/hitTestDeep() itself (so they simply never run this frame,
	// see CompositeWidget::update()), but nothing about that tree recursion touches a
	// child's OWN m_enabled or makes its draw() -- which only ever looks at itself --
	// aware that an ancestor is disabled. effectivelyEnabled() walks m_parent to answer
	// that without CompositeWidget ever mutating a child's flags.
	bool effectivelyEnabled() const {
		return m_enabled && (m_parent == nullptr || m_parent->effectivelyEnabled());
	}
	void setVisible(bool visible) { m_visible = visible; }
	bool visible() const noexcept { return m_visible; }
	void setLabel(std::string label) { m_label = std::move(label); }
	std::string_view label() const { return m_label; }
	void setTooltip(std::string tooltip) { m_tooltip = std::move(tooltip); }
	std::string_view tooltip() const { return m_tooltip; }
	void setLabelWidthOverride(float px) { m_labelWidthOverride = px; }   // -1 = theme ratio

protected:
	// The [label][control] row split shared by almost every widget (docs/gui/06, "The
	// label/control split"). An empty label gives the control the full row width.
	struct RowSplit { Rect label, control; };
	RowSplit splitRow(const GuiContext&) const;

	WidgetId    m_id;
	Panel*      m_panel = nullptr;
	// Immediate CompositeWidget owner, or null for a widget added directly to a Panel
	// (Panels are not Widgets, so there is no non-null value to store for a top-level
	// widget -- see effectivelyEnabled()). Set by CompositeWidget::add() when a widget
	// is added to a composite, and explicitly left null by Panel::add() for symmetry.
	Widget*     m_parent = nullptr;
	Rect        m_bounds;
	std::string m_label, m_tooltip;
	bool        m_enabled = true, m_visible = true;
	float       m_labelWidthOverride = -1.0f;

private:
	friend class Panel;            // sets m_panel when the widget is added to it
	friend class CompositeWidget;  // sets m_parent when the widget is added to it

	std::optional<Rect> m_boundsOverride;
};

} // namespace lightGraphics::ui
