#pragma once

// Layer 3: a movable, resizable window of widgets. Owned exclusively by GuiContext
// (docs/gui/01-architecture.md, "Ownership") -- consumers only ever see a raw Panel*
// returned from GuiContext::createPanel(). See docs/gui/05-widgets.md, "Panel", and
// docs/gui/06-layout-and-theme.md for the layout pass this class runs.

#include "Types.h"
#include "Widget.h"
#include "widgets/CompositeWidget.h"

#include <cstddef>
#include <functional>
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
		// If this widget is a CompositeWidget, propagate m_panel to its children
		// (they may have been added in the constructor before m_panel was set)
		if (auto* composite = dynamic_cast<CompositeWidget*>(ptr)) {
			composite->propagatePanelToChildren();
		}
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
	// Fired by the title bar's close button (PanelFlags::Closable) immediately AFTER the
	// panel has already hidden itself, so a handler that wants to VETO the close can
	// simply call setVisible(true) again. Not fired by setVisible(false): this is a "the
	// user clicked the X" notification, not a visibility observer.
	//
	// Do NOT call GuiContext::destroyPanel(this) from inside this callback: it runs from
	// within Panel::update(), i.e. from inside GuiContext::update()'s own walk over
	// m_panels, and destroyPanel() erases the owning unique_ptr immediately (it is not
	// deferred) -- that is both a use-after-free of `this` and an invalidated iterator.
	// To actually destroy the panel, defer it by one frame:
	//     panel->setOnClose([&gui, panel]{ gui.postToMainThread([&gui, panel]{ gui.destroyPanel(panel); }); });
	// postToMainThread() drains at the top of the next beginFrame(), before anything
	// touches the panel list.
	void setOnClose(std::function<void()> onClose) { m_onClose = std::move(onClose); }
	void bringToFront();

	// Anchoring (docs/gui/05, "Panel", "Anchoring and on-screen clamping").
	// Stores only the corner enum here; the pixel offset from that corner is
	// (re)computed every frame in update() from whatever m_bounds/displaySize currently
	// are, so it always reflects the panel's latest position -- including a position that
	// just changed this same frame via a drag, a resize, or a fresh setAnchor() call --
	// without setAnchor() itself needing a GuiContext (its normative signature,
	// docs/gui/05, takes none) to know the current displaySize.
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
	// What the panel actually occupies on screen right now: m_bounds normally, but only
	// the title-bar strip while collapsed. A collapsed panel must not keep painting (or
	// hit-testing, or occluding the panel behind it with) a full-size empty box just
	// because m_bounds still remembers its expanded height -- m_bounds is deliberately
	// left untouched by collapsing so expanding restores the previous size exactly.
	Rect effectiveBounds(const GuiContext& ctx) const;
	float contentHeight() const { return m_contentHeight; }

	// ---- scrolling (docs/gui/05, "Panel", "Scrolling") ----
	float scrollY() const { return m_scrollY; }
	// True iff content overflows the view AND PanelFlags::Scrollable is set -- recomputed
	// every layout() pass (twice a frame, same as everything else GuiContext::endFrame()
	// lays out) from the PREVIOUS pass's own value. See Panel.cpp's layout() for why that
	// self-referential read is deliberate rather than a bug: it is what resolves "does the
	// scrollbar's presence depend on content width, which depends on the scrollbar's
	// presence" without oscillating.
	bool needsScrollbar() const { return m_needsScrollbar; }
	// The content region widgets are laid out and clipped into: below the title bar,
	// inset by windowPadding on all sides, additionally inset on the right by
	// theme.scrollbarWidth when needsScrollbar() is true. docs/gui/04-input-and-events.md,
	// "Hit testing": every widget hit test must intersect this rect, so a widget (or part
	// of one) scrolled out of view is never hittable even though its stored Rect might
	// still geometrically contain the cursor.
	Rect contentClipRect(const GuiContext&) const;

	// ---- internal: the resize grip and scrollbar grab are not Widgets (Panel itself
	// isn't one -- docs/gui/01-architecture.md, "Ownership"), so they don't participate in
	// GuiContext's normal per-widget hitTestDeep() walk at all; GuiContext::update() asks
	// the panel directly, and BEFORE it hit-tests widgets, so an overlapping corner always
	// resolves to the grip over the scrollbar over a widget (docs/gui/05, "Resize grip":
	// "hit-tested BEFORE widgets and before the scrollbar"). Capture itself is claimed by
	// GuiContext (ctx.setActiveId(resizeGripId()/scrollbarGrabId())) the same way it claims
	// a widget's id, which is what makes wantsMouse() stay true and the drag keep tracking
	// even once the cursor leaves the panel's own bounds -- see allocateWidgetId()'s
	// comment (Widget.h) for why these ids can share GuiContext::m_activeId with real
	// widget ids without ever colliding or being misresolved by findWidget().
	bool hitTestResizeGrip(const GuiContext&, Vec2 p) const;
	bool hitTestScrollbarGrab(const GuiContext&, Vec2 p) const;
	WidgetId resizeGripId() const { return m_resizeGripId; }
	WidgetId scrollbarGrabId() const { return m_scrollbarGrabId; }
	void beginResizeDrag(Vec2 mouse);
	void beginScrollbarDrag(const GuiContext&, Vec2 mouse);

	// The two title-bar buttons (PanelFlags::Collapsible / PanelFlags::Closable). Same
	// arrangement as the grip and scrollbar grab above: not Widgets, hit-tested by
	// GuiContext::update() directly, and claiming capture through the same m_activeId via
	// a synthetic id from allocateWidgetId(). Capture matters here for the same reason it
	// does for a Button -- these fire on release-INSIDE, so pressing the X and dragging
	// off it before letting go must cancel the close rather than complete it.
	//
	// They also have to be hit-tested before the title-bar DRAG starts, or every click on
	// a button would also begin dragging the panel; Panel::update()'s drag block checks
	// ctx.activeId() against these two ids for exactly that reason.
	bool hitTestCollapseButton(const GuiContext&, Vec2 p) const;
	bool hitTestCloseButton(const GuiContext&, Vec2 p) const;
	WidgetId collapseButtonId() const { return m_collapseButtonId; }
	WidgetId closeButtonId() const { return m_closeButtonId; }

private:
	bool hasTitleBar() const { return !hasFlag(m_flags, PanelFlags::NoTitleBar); }
	// Full vertical span below the title bar (padding included) -- what m_contentHeight is
	// compared against to decide overflow, and what scrollY clamps against. Kept as one
	// function so layout(), the wheel handler, and the scrollbar-drag handler can never
	// disagree about it.
	float scrollableSpan(const GuiContext&) const;
	float maxScrollY(const GuiContext&) const;
	// Square, titleBarHeight on a side, inset slightly: the collapse arrow sits at the
	// title bar's leading edge and the close button at its trailing edge. Both return an
	// empty Rect when the corresponding flag is unset, so the title TEXT rect can be
	// derived by simply subtracting their widths without branching on the flags again.
	Rect collapseButtonRect(const GuiContext&) const;
	Rect closeButtonRect(const GuiContext&) const;
	// Takes the two rects draw() has already computed for the title-text split rather
	// than recomputing them, so the glyphs can never drift out of the space reserved
	// for them.
	void drawTitleButtons(DrawList&, const GuiContext&, const Rect& collapseRect,
	                       const Rect& closeRect) const;
	Rect resizeGripRect(const GuiContext&) const;
	Rect scrollbarTrackRect(const GuiContext&) const;
	Rect scrollbarThumbRect(const GuiContext&) const;
	// Repositions m_bounds from the stored anchor offset when displaySize has changed
	// since last frame, then refreshes that stored offset from wherever m_bounds ends up
	// by the end of THIS frame (after any drag/resize/scroll below has had its say) --
	// see setAnchor()'s comment for why the offset is a per-frame derived value rather
	// than something computed once inside setAnchor() itself.
	void updateAnchoring(const GuiContext&);
	Vec2 anchorOffsetFor(Anchor, const Rect& bounds, Vec2 displaySize) const;
	// docs/gui/05, "Panel", "Anchoring and on-screen clamping": "a panel dragged fully off
	// the top edge becomes permanently unreachable." Clamps so at least the full title bar
	// height stays vertically on screen, and at least a minimal grabbable width of it
	// stays horizontally on screen.
	void clampToScreen(const GuiContext&);

	GuiContext& m_context;
	std::string m_title;
	Rect m_bounds;
	PanelFlags m_flags;
	bool m_visible = true;
	bool m_collapsed = false;
	Anchor m_anchor = Anchor::TopLeft;
	std::vector<std::unique_ptr<Widget>> m_widgets;
	float m_contentHeight = 0.0f;
	float m_scrollY = 0.0f;

	bool m_needsScrollbar = false;

	WidgetId m_resizeGripId = allocateWidgetId();
	WidgetId m_scrollbarGrabId = allocateWidgetId();
	WidgetId m_collapseButtonId = allocateWidgetId();
	WidgetId m_closeButtonId = allocateWidgetId();

	std::function<void()> m_onClose;

	// Drag-start snapshots; the drag is IN PROGRESS iff ctx.activeId() == m_resizeGripId /
	// m_scrollbarGrabId respectively -- GuiContext::update() is the single source of truth
	// for that (see resizeGripId()'s header comment), so there is no separate bool here to
	// risk disagreeing with it.
	Vec2 m_resizeDragStartMouse;
	Rect m_resizeDragStartBounds;

	float m_scrollbarDragStartMouseY = 0.0f;
	float m_scrollbarDragStartScrollY = 0.0f;

	// Sentinel {-1,-1}: "no frame has run yet, don't reposition off a made-up displaySize"
	// -- see updateAnchoring().
	Vec2 m_lastDisplaySize{ -1.0f, -1.0f };
	Vec2 m_anchorOffset;

	// Title-bar dragging (docs/gui/05, "Panel", 4-logical-pixel drag threshold).
	bool m_dragging = false;
	bool m_dragArmed = false;
	Vec2 m_dragStartMouse;
	Rect m_dragStartBounds;
};

} // namespace lightGraphics::ui
