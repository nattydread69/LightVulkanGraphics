#pragma once

// Layer 3: the base every concrete widget derives from. See docs/gui/05-widgets.md, "The
// base class", and docs/gui/06-layout-and-theme.md for splitRow() and the absolute-
// placement escape hatch.

#include "Types.h"

#include <optional>
#include <string>
#include <string_view>

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

	// ---- drawing ----
	virtual void draw(DrawList&, const GuiContext&) const = 0;

	// ---- common state ----
	void setEnabled(bool enabled) { m_enabled = enabled; }
	bool enabled() const noexcept { return m_enabled; }
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
	Rect        m_bounds;
	std::string m_label, m_tooltip;
	bool        m_enabled = true, m_visible = true;
	float       m_labelWidthOverride = -1.0f;

private:
	friend class Panel;   // sets m_panel when the widget is added to it

	std::optional<Rect> m_boundsOverride;
};

} // namespace lightGraphics::ui
