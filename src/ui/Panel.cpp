#include <lightVulkanGraphics/ui/Panel.h>
#include <lightVulkanGraphics/ui/DrawList.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <cmath>

namespace lightGraphics::ui {

namespace {
	Rect snapToPixels(const Rect& r) {
		return { std::round(r.x), std::round(r.y), std::round(r.w), std::round(r.h) };
	}

	constexpr float kDragThreshold = 4.0f;   // logical pixels, docs/gui/05 "Panel"

	// docs/gui/09 phase 9, "Resize grip": "Enforces the documented minimum size:
	// titleBarHeight + 2*windowPadding by 8*fontSize." titleBarHeight is a vertical
	// metric, so that half is the minimum HEIGHT (just enough to keep the title bar and a
	// sliver of padding); 8*fontSize is the minimum WIDTH (enough for a short label to
	// stay legible).
	float minPanelWidth(const Theme& th) { return 8.0f * th.fontSize; }
	float minPanelHeight(const Theme& th) { return th.titleBarHeight + 2.0f * th.windowPadding; }
}

Panel::Panel(GuiContext& context, std::string title, Rect bounds, PanelFlags flags)
	: m_context(context), m_title(std::move(title)), m_bounds(bounds), m_flags(flags) {}

Panel::~Panel() = default;

void Panel::remove(Widget* widget) {
	m_widgets.erase(
		std::remove_if(m_widgets.begin(), m_widgets.end(),
			[widget](const std::unique_ptr<Widget>& w) { return w.get() == widget; }),
		m_widgets.end());
}

void Panel::clear() {
	m_widgets.clear();
}

void Panel::bringToFront() {
	m_context.bringPanelToFront(this);
}

bool Panel::hitTest(Vec2 p) const {
	if (!m_visible) {
		return false;
	}
	return m_bounds.contains(p);
}

Rect Panel::titleBarRect(const GuiContext& ctx) const {
	if (!hasTitleBar()) {
		return {};
	}
	return { m_bounds.x, m_bounds.y, m_bounds.w, ctx.theme().titleBarHeight };
}

void Panel::update(GuiContext& ctx) {
	updateAnchoring(ctx);

	const InputState& in = ctx.input();
	const Theme& th = ctx.theme();
	const bool leftDown     = in.mouseDown[static_cast<int>(MouseButton::Left)];
	const bool leftPressed  = in.mousePressed[static_cast<int>(MouseButton::Left)];
	const bool resizable    = hasFlag(m_flags, PanelFlags::Resizable);
	const bool scrollable   = hasFlag(m_flags, PanelFlags::Scrollable);

	// ---- resize grip: continue a drag GuiContext::update() already claimed capture for
	// this frame (or a previous one), or just offer the hover cursor otherwise. Capture
	// ITSELF is claimed by GuiContext, not here -- see resizeGripId()'s header comment --
	// so this never needs to inspect leftPressed.
	if (resizable) {
		if (ctx.activeId() == m_resizeGripId) {
			if (leftDown) {
				Vec2 delta = in.mousePos - m_resizeDragStartMouse;
				Rect r = m_resizeDragStartBounds;
				r.w = std::max(minPanelWidth(th), m_resizeDragStartBounds.w + delta.x);
				r.h = std::max(minPanelHeight(th), m_resizeDragStartBounds.h + delta.y);
				m_bounds = r;
			}
			ctx.requestCursorShape(CursorShape::ResizeNWSE);
		} else if (ctx.activeId() == kInvalidWidgetId && hitTestResizeGrip(ctx, in.mousePos)) {
			ctx.requestCursorShape(CursorShape::ResizeNWSE);
		}
	}

	// ---- scrollbar grab: continue a drag GuiContext::update() already claimed capture
	// for -- see the class comment on why this "keeps tracking outside the panel" for
	// free (Panel::update() runs unconditionally for every visible panel every frame,
	// regardless of where the cursor currently is).
	if (scrollable && m_needsScrollbar && ctx.activeId() == m_scrollbarGrabId) {
		if (leftDown) {
			Rect track = scrollbarTrackRect(ctx);
			Rect thumb = scrollbarThumbRect(ctx);
			float travel = std::max(1.0f, track.h - thumb.h);
			float dy = in.mousePos.y - m_scrollbarDragStartMouseY;
			float scrollPerPixel = maxScrollY(ctx) / travel;
			m_scrollY = std::clamp(m_scrollbarDragStartScrollY + dy * scrollPerPixel, 0.0f, maxScrollY(ctx));
		}
	}

	if (hasTitleBar() && hasFlag(m_flags, PanelFlags::Movable)) {
		Rect titleRect = titleBarRect(ctx);

		if (!m_dragging && leftPressed && titleRect.contains(in.mousePos)) {
			m_dragging = true;
			m_dragArmed = false;
			m_dragStartMouse = in.mousePos;
			m_dragStartBounds = m_bounds;
		}

		if (m_dragging) {
			if (leftDown) {
				Vec2 delta = in.mousePos - m_dragStartMouse;
				if (!m_dragArmed) {
					float dist2 = delta.x * delta.x + delta.y * delta.y;
					if (dist2 > kDragThreshold * kDragThreshold) {
						m_dragArmed = true;
					}
				}
				if (m_dragArmed) {
					Rect moved = m_dragStartBounds;
					moved.x += delta.x;
					moved.y += delta.y;
					m_bounds = moved;
				}
			} else {
				m_dragging = false;
				m_dragArmed = false;
			}
		}
	}

	// docs/gui/09 phase 9, "Scrolling": "Wheel scrolls by 3 * lineHeight when the cursor
	// is over the panel." Gated on ctx.hoveredPanel() == this (not just bounds.contains())
	// so a panel occluded by one in front of it never steals a wheel tick meant for the
	// topmost one, and on the hovered widget (if any) NOT wanting the wheel itself
	// (SliderT, DropDown -- Widget::wantsWheel()) so a tick over a slider steps the slider
	// exactly once instead of also scrolling the panel underneath it.
	Widget* hoveredWidget = ctx.findWidget(ctx.hoveredId());
	bool widgetClaimsWheel = hoveredWidget && hoveredWidget->wantsWheel();
	if (scrollable && m_needsScrollbar && ctx.hoveredPanel() == this &&
	    !widgetClaimsWheel && in.wheelDelta != 0.0f) {
		float lineHeight = ctx.font().lineHeight(th.fontSize);
		m_scrollY = std::clamp(m_scrollY - in.wheelDelta * 3.0f * lineHeight, 0.0f, maxScrollY(ctx));
	}

	if (!m_collapsed) {
		for (auto& w : m_widgets) {
			if (w->visible()) {
				w->update(ctx);
			}
		}
	}

	clampToScreen(ctx);
}

void Panel::layout(const GuiContext& ctx) {
	const Theme& th = ctx.theme();
	float titleOffset = hasTitleBar() ? th.titleBarHeight : 0.0f;
	float y = m_bounds.y + titleOffset + th.windowPadding;
	// docs/gui/09 phase 9, "Scrolling", the ordering trap: whether the scrollbar is shown
	// depends on content height, which depends on content width, which depends on whether
	// the scrollbar is shown. Resolved by reading m_needsScrollbar from the PREVIOUS
	// layout() pass rather than trying to decide it fresh here -- exactly the same trick
	// docs/gui/05's Label already relies on for wrapping-height convergence, and it works
	// for the same reason: GuiContext::endFrame() runs layout() twice every frame, so the
	// decision this pass makes from a stale flag is corrected by the SECOND pass before
	// anything is drawn, and a decision that hasn't changed converges immediately. It
	// cannot oscillate frame-to-frame because each frame starts from the flag the previous
	// frame's second pass already settled on, and--for every widget whose preferredSize()
	// doesn't itself depend on the row width (everything except a wrapping Label)--content
	// height doesn't depend on contentW at all, so the two passes never disagree twice.
	bool scrollable = hasFlag(m_flags, PanelFlags::Scrollable);
	float contentW = m_bounds.w - 2 * th.windowPadding - (m_needsScrollbar ? th.scrollbarWidth : 0.0f);
	float x = m_bounds.x + th.windowPadding;

	if (m_collapsed) {
		m_contentHeight = 0.0f;
		m_needsScrollbar = false;
		return;
	}

	for (auto& w : m_widgets) {
		if (!w->visible()) {
			continue;
		}
		if (w->hasBoundsOverride()) {
			Rect ov = w->boundsOverride();
			Rect abs{ m_bounds.x + ov.x, m_bounds.y + ov.y, ov.w, ov.h };
			w->setBounds(snapToPixels(abs));
			// A composite placed via the bounds-override escape hatch still needs its
			// own children laid out (docs/gui/05, "CompositeWidget") -- layout() is a
			// no-op for every other widget, so calling it unconditionally here is free.
			w->layout(ctx);
			continue;
		}
		Vec2 pref = w->preferredSize(ctx);
		// Widget layout offsets by -scrollY; m_contentHeight below is computed in
		// UNSCROLLED space (docs/gui/06's layout pass, restated for phase 9's scrollY).
		Rect r{ x, y - m_scrollY, contentW, pref.y };
		w->setBounds(snapToPixels(r));
		w->layout(ctx);
		y += pref.y + th.itemSpacing;
	}
	m_contentHeight = y - (m_bounds.y + titleOffset) + th.windowPadding;

	m_needsScrollbar = scrollable && m_contentHeight > scrollableSpan(ctx) + 0.5f;
	m_scrollY = std::clamp(m_scrollY, 0.0f, maxScrollY(ctx));
}

void Panel::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();

	if (!hasFlag(m_flags, PanelFlags::NoBackground)) {
		dl.addRectFilled(m_bounds, th.windowBg, th.rounding);
		dl.addRect(m_bounds, th.border, th.borderWidth, th.rounding);
	}

	if (hasTitleBar()) {
		Rect titleRect = titleBarRect(ctx);
		bool frontmost = ctx.panelCount() > 0 && ctx.panelAt(0) == this;
		dl.addRectFilled(titleRect, frontmost ? th.titleBgActive : th.titleBg, th.rounding);

		Rect titleTextRect = titleRect.insetXY(th.windowPadding * 0.5f, 0.0f);
		dl.addTextClipped(ctx.font(), th.fontSize, titleTextRect, th.text, m_title,
		                   Align::Start, Align::Center);
	}

	if (!m_collapsed) {
		// docs/gui/04-input-and-events.md, "Hit testing": widgets scrolled out of view
		// must not be hittable; the drawing-side counterpart is that they must not be
		// VISIBLE either, or a widget half past the content edge would render its other
		// half over the title bar / scrollbar gutter / panel border.
		dl.pushClipRect(contentClipRect(ctx));
		for (auto& w : m_widgets) {
			if (w->visible()) {
				w->draw(dl, ctx);
			}
		}
		dl.popClipRect();

		if (m_needsScrollbar) {
			Rect track = scrollbarTrackRect(ctx);
			dl.addRectFilled(track, th.scrollbarBg, th.rounding);
			Rect thumb = scrollbarThumbRect(ctx);
			bool active = ctx.activeId() == m_scrollbarGrabId;
			Color thumbColor = active ? th.accentActive : th.scrollbarGrab;
			dl.addRectFilled(thumb, thumbColor, th.rounding);
		}
	}

	if (hasFlag(m_flags, PanelFlags::Resizable)) {
		// Triangle glyph in the bottom-right corner (docs/gui/05, "Resize grip"), built
		// from addTriangleFilled rather than a font character -- same reasoning as
		// CollapsingSection's header disclosure triangle.
		Rect grip = resizeGripRect(ctx);
		bool hot = ctx.activeId() == m_resizeGripId;
		Color gripColor = hot ? th.accentActive : th.border;
		Vec2 p0{ grip.right(), grip.top() };
		Vec2 p1{ grip.right(), grip.bottom() };
		Vec2 p2{ grip.left(), grip.bottom() };
		dl.addTriangleFilled(p0, p1, p2, gripColor);
	}
}

float Panel::scrollableSpan(const GuiContext& ctx) const {
	float titleOffset = hasTitleBar() ? ctx.theme().titleBarHeight : 0.0f;
	return m_bounds.h - titleOffset;
}

float Panel::maxScrollY(const GuiContext& ctx) const {
	return std::max(0.0f, m_contentHeight - scrollableSpan(ctx));
}

Rect Panel::contentClipRect(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	float titleOffset = hasTitleBar() ? th.titleBarHeight : 0.0f;
	float gutter = m_needsScrollbar ? th.scrollbarWidth : 0.0f;
	return {
		m_bounds.x + th.windowPadding,
		m_bounds.y + titleOffset + th.windowPadding,
		std::max(0.0f, m_bounds.w - 2.0f * th.windowPadding - gutter),
		std::max(0.0f, m_bounds.h - titleOffset - 2.0f * th.windowPadding)
	};
}

Rect Panel::resizeGripRect(const GuiContext& ctx) const {
	float s = ctx.theme().resizeGripSize;
	return { m_bounds.right() - s, m_bounds.bottom() - s, s, s };
}

Rect Panel::scrollbarTrackRect(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	Rect content = contentClipRect(ctx);
	return { m_bounds.right() - th.windowPadding - th.scrollbarWidth, content.y, th.scrollbarWidth, content.h };
}

Rect Panel::scrollbarThumbRect(const GuiContext& ctx) const {
	Rect track = scrollbarTrackRect(ctx);
	float maxScroll = maxScrollY(ctx);
	// Same shape as DropDown's popup scrollbar thumb (docs/gui/05, "DropDown"): proportional
	// to how much of the content is visible, floored so a very tall document still leaves a
	// grabbable thumb.
	float fraction = (m_contentHeight > 0.0f) ? (scrollableSpan(ctx) / m_contentHeight) : 1.0f;
	float thumbH = std::min(track.h, std::max(track.h * fraction, 20.0f));
	float t = maxScroll > 0.0f ? m_scrollY / maxScroll : 0.0f;
	return { track.x, track.y + t * (track.h - thumbH), track.w, thumbH };
}

bool Panel::hitTestResizeGrip(const GuiContext& ctx, Vec2 p) const {
	if (!m_visible || m_collapsed || !hasFlag(m_flags, PanelFlags::Resizable)) {
		return false;
	}
	return resizeGripRect(ctx).contains(p);
}

bool Panel::hitTestScrollbarGrab(const GuiContext& ctx, Vec2 p) const {
	if (!m_visible || m_collapsed || !m_needsScrollbar) {
		return false;
	}
	return scrollbarThumbRect(ctx).contains(p);
}

void Panel::beginResizeDrag(Vec2 mouse) {
	m_resizeDragStartMouse = mouse;
	m_resizeDragStartBounds = m_bounds;
}

void Panel::beginScrollbarDrag(const GuiContext&, Vec2 mouse) {
	m_scrollbarDragStartMouseY = mouse.y;
	m_scrollbarDragStartScrollY = m_scrollY;
}

Vec2 Panel::anchorOffsetFor(Anchor anchor, const Rect& bounds, Vec2 displaySize) const {
	switch (anchor) {
		case Anchor::TopLeft:     return { bounds.x, bounds.y };
		case Anchor::TopRight:    return { displaySize.x - bounds.right(), bounds.y };
		case Anchor::BottomLeft:  return { bounds.x, displaySize.y - bounds.bottom() };
		case Anchor::BottomRight: return { displaySize.x - bounds.right(), displaySize.y - bounds.bottom() };
	}
	return { bounds.x, bounds.y };
}

void Panel::updateAnchoring(const GuiContext& ctx) {
	Vec2 displaySize = ctx.input().displaySize;
	bool firstFrame = m_lastDisplaySize.x < 0.0f;

	if (!firstFrame && (displaySize.x != m_lastDisplaySize.x || displaySize.y != m_lastDisplaySize.y)) {
		// docs/gui/09 phase 9, "Anchoring": recompute the absolute position from the
		// offset stored as of the last frame, holding the panel's SIZE and its anchored
		// corner's OFFSET fixed -- this is what survives a window resize sensibly instead
		// of leaving the panel floating wherever it happened to be.
		switch (m_anchor) {
			case Anchor::TopLeft:
				m_bounds.x = m_anchorOffset.x;
				m_bounds.y = m_anchorOffset.y;
				break;
			case Anchor::TopRight:
				m_bounds.x = displaySize.x - m_anchorOffset.x - m_bounds.w;
				m_bounds.y = m_anchorOffset.y;
				break;
			case Anchor::BottomLeft:
				m_bounds.x = m_anchorOffset.x;
				m_bounds.y = displaySize.y - m_anchorOffset.y - m_bounds.h;
				break;
			case Anchor::BottomRight:
				m_bounds.x = displaySize.x - m_anchorOffset.x - m_bounds.w;
				m_bounds.y = displaySize.y - m_anchorOffset.y - m_bounds.h;
				break;
		}
	}
	m_lastDisplaySize = displaySize;

	// Refresh the stored offset from wherever m_bounds is RIGHT NOW (before this frame's
	// own drag/resize logic runs) -- picking up a setAnchor() call, or a drag/resize that
	// completed on a previous frame, so a future displaySize change repositions from the
	// panel's latest position rather than a stale one. (A drag/resize that happens THIS
	// frame is picked up next frame, one frame after the gesture that caused it -- the
	// same one-pass lag every other frame-driven bookkeeping in this class already has,
	// and unobservable for the same reason: nothing reads the offset until displaySize
	// actually changes.)
	if (displaySize.x > 0.0f && displaySize.y > 0.0f) {
		m_anchorOffset = anchorOffsetFor(m_anchor, m_bounds, displaySize);
	}
}

void Panel::clampToScreen(const GuiContext& ctx) {
	if (!hasTitleBar()) {
		return;   // no title bar to grab, so there's nothing to keep reachable via one
	}
	Vec2 displaySize = ctx.input().displaySize;
	if (displaySize.x <= 0.0f || displaySize.y <= 0.0f) {
		return;   // no frame has run yet
	}
	const Theme& th = ctx.theme();

	// docs/gui/09 phase 9, "Anchoring and on-screen clamping": "a panel dragged fully off
	// the top edge becomes permanently unreachable." The title bar's full height must stay
	// vertically on screen (it is the only drag handle back into view), and at least a
	// minimal grabbable width of it must stay horizontally on screen.
	m_bounds.y = std::clamp(m_bounds.y, 0.0f, std::max(0.0f, displaySize.y - th.titleBarHeight));

	float minVisibleW = std::min(m_bounds.w, 40.0f);
	m_bounds.x = std::clamp(m_bounds.x, minVisibleW - m_bounds.w, displaySize.x - minVisibleW);
}

} // namespace lightGraphics::ui
