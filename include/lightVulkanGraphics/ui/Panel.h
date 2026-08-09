#pragma once

// Layer 3: a movable, resizable window of widgets. Owned exclusively by GuiContext
// (docs/gui/01-architecture.md, "Ownership") -- consumers only ever see a raw Panel*
// returned from GuiContext::createPanel(). See docs/gui/05-widgets.md, "Panel", and
// docs/gui/06-layout-and-theme.md for the layout pass this class runs.

#include "Types.h"
#include "Widget.h"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace lightGraphics::ui {

class DrawList;
class GuiContext;

class Panel {
public:
	enum class Anchor { TopLeft, TopRight, BottomLeft, BottomRight };

	Panel(GuiContext& context, std::string title, Rect bounds, PanelFlags flags);
	~Panel();

	Panel(const Panel&) = delete;
	Panel& operator=(const Panel&) = delete;

	template <typename W, typename... Args>
	W* add(Args&&... args) {
		static_assert(std::is_base_of<Widget, W>::value, "Panel::add<W> requires W to derive from Widget");
		auto widget = std::make_unique<W>(std::forward<Args>(args)...);
		W* ptr = widget.get();
		ptr->m_panel = this;
		// A widget added directly to a Panel has no CompositeWidget ancestor -- explicit
		// here (rather than relying on Widget's default member initializer) so the
		// invariant "whoever owns a widget sets m_parent" holds for both owners
		// (docs/gui/05, "CompositeWidget", effectivelyEnabled()), not just one of them.
		ptr->m_parent = nullptr;
		m_widgets.push_back(std::move(widget));
		return ptr;
	}

	void remove(Widget* widget);
	void clear();

	void setTitle(std::string title) { m_title = std::move(title); }
	const std::string& title() const { return m_title; }
	void setBounds(const Rect& bounds) { m_bounds = bounds; }
	Rect bounds() const { return m_bounds; }
	void setFlags(PanelFlags flags) { m_flags = flags; }
	PanelFlags flags() const { return m_flags; }
	void setVisible(bool visible) { m_visible = visible; }
	bool visible() const { return m_visible; }
	void setCollapsed(bool collapsed) { m_collapsed = collapsed; }
	bool collapsed() const { return m_collapsed; }
	void bringToFront();

	// Anchoring is stored from phase 5 on but only recorded, not yet acted on: recomputing
	// the absolute position when displaySize changes is docs/gui/09's phase 9 scope
	// ("Panel polish"). setAnchor() is part of the normative Panel API (docs/gui/05,
	// "Panel") and the phase 10 usage example calls it, so the surface exists now even
	// though only the storage half is implemented yet.
	void setAnchor(Anchor anchor) { m_anchor = anchor; }
	Anchor anchor() const { return m_anchor; }

	std::size_t widgetCount() const { return m_widgets.size(); }
	Widget* widgetAt(std::size_t index) const { return m_widgets[index].get(); }

	// ---- internal: driven by GuiContext's per-frame sequence (docs/gui/01) ----
	void update(GuiContext& ctx);
	void layout(const GuiContext& ctx);
	void draw(DrawList& drawList, const GuiContext& ctx) const;
	bool hitTest(Vec2 p) const;
	Rect titleBarRect(const GuiContext& ctx) const;
	float contentHeight() const { return m_contentHeight; }

private:
	bool hasTitleBar() const { return !hasFlag(m_flags, PanelFlags::NoTitleBar); }

	GuiContext& m_context;
	std::string m_title;
	Rect m_bounds;
	PanelFlags m_flags;
	bool m_visible = true;
	bool m_collapsed = false;
	Anchor m_anchor = Anchor::TopLeft;
	std::vector<std::unique_ptr<Widget>> m_widgets;
	float m_contentHeight = 0.0f;
	float m_scrollY = 0.0f;   // wheel/scrollbar interaction arrives in phase 9

	// Title-bar dragging (docs/gui/05, "Panel", 4-logical-pixel drag threshold).
	bool m_dragging = false;
	bool m_dragArmed = false;
	Vec2 m_dragStartMouse;
	Rect m_dragStartBounds;
};

} // namespace lightGraphics::ui
