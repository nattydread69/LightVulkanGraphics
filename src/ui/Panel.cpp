#include <lightVulkanGraphics/ui/Panel.h>
#include <lightVulkanGraphics/ui/DrawList.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace lightGraphics::ui {

namespace {
	Rect snapToPixels(const Rect& r) {
		return { std::round(r.x), std::round(r.y), std::round(r.w), std::round(r.h) };
	}

	constexpr float kDragThreshold = 4.0f;   // logical pixels, docs/gui/05 "Panel"

	// docs/gui/05, "Panel", "Resize grip": "Enforces the documented minimum size:
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

void Panel::requestClose() {
	if (!hasFlag(m_flags, PanelFlags::Closable)) {
		return;
	}
	m_visible = false;
	if (m_onClose) {
		// Fired AFTER m_visible is already false so a handler can veto by setting it
		// straight back to true (see setOnClose()'s comment in Panel.h).
		m_onClose();
	}
}

bool Panel::hitTest(Vec2 p) const {
	if (!m_visible) {
		return false;
	}
	// effectiveBounds() needs a GuiContext only for theme().titleBarHeight, and this
	// signature has none (it predates collapsing having any geometric effect); m_context
	// is the same object every caller would pass anyway, so read it directly rather than
	// breaking the signature for every existing caller and test.
	return effectiveBounds(m_context).contains(p);
}

Rect Panel::titleBarRect(const GuiContext& ctx) const {
	if (!hasTitleBar()) {
		return {};
	}
	return { m_bounds.x, m_bounds.y, m_bounds.w, ctx.theme().titleBarHeight };
}

Rect Panel::effectiveBounds(const GuiContext& ctx) const {
	if (m_collapsed && hasTitleBar()) {
		return titleBarRect(ctx);
	}
	return m_bounds;
}

Rect Panel::collapseButtonRect(const GuiContext& ctx) const {
	if (!hasTitleBar() || !hasFlag(m_flags, PanelFlags::Collapsible)) {
		return {};
	}
	float h = ctx.theme().titleBarHeight;
	return { m_bounds.x, m_bounds.y, h, h };
}

Rect Panel::closeButtonRect(const GuiContext& ctx) const {
	if (!hasTitleBar() || !hasFlag(m_flags, PanelFlags::Closable)) {
		return {};
	}
	float h = ctx.theme().titleBarHeight;
	return { m_bounds.right() - h, m_bounds.y, h, h };
}

bool Panel::hitTestCollapseButton(const GuiContext& ctx, Vec2 p) const {
	// Deliberately NOT gated on m_collapsed: this is the only way back OUT of a collapsed
	// panel, so it must stay live in both states (unlike the resize grip and scrollbar,
	// which have nothing to act on while collapsed).
	if (!m_visible) {
		return false;
	}
	Rect r = collapseButtonRect(ctx);
	return !r.empty() && r.contains(p);
}

bool Panel::hitTestCloseButton(const GuiContext& ctx, Vec2 p) const {
	if (!m_visible) {
		return false;
	}
	Rect r = closeButtonRect(ctx);
	return !r.empty() && r.contains(p);
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

	// ---- title-bar buttons: fire on release-INSIDE, mirroring Button::update()
	// (docs/gui/05, "Button"), so press-then-drag-away cancels. Capture was claimed by
	// GuiContext::update() before this ran -- see hitTestCollapseButton()'s header comment.
	const bool leftReleased = in.mouseReleased[static_cast<int>(MouseButton::Left)];

	if (ctx.activeId() == m_collapseButtonId && leftReleased &&
	    hitTestCollapseButton(ctx, in.mousePos)) {
		m_collapsed = !m_collapsed;
	}

	if (ctx.activeId() == m_closeButtonId && leftReleased &&
	    hitTestCloseButton(ctx, in.mousePos)) {
		requestClose();
	}

	// A press that landed on either button must not ALSO start a title-bar drag -- both
	// buttons sit inside titleBarRect(), so without this the panel would follow the mouse
	// away from a button the user is merely about to release on.
	const bool titleButtonCaptured =
		ctx.activeId() == m_collapseButtonId || ctx.activeId() == m_closeButtonId;

	if (hasTitleBar() && hasFlag(m_flags, PanelFlags::Movable) && !titleButtonCaptured) {
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

	// docs/gui/05, "Panel", "Scrolling": "Wheel scrolls by 3 * lineHeight when the cursor
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
	// docs/gui/05, "Panel", "Scrolling", the ordering trap: whether the scrollbar is shown
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
		// UNSCROLLED space (docs/gui/06's layout pass, restated for scrollY).
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
		// effectiveBounds(), not m_bounds: a collapsed panel is just its title bar, and
		// must not paint the expanded-size box it will return to (see effectiveBounds()).
		Rect body = effectiveBounds(ctx);
		dl.addRectFilled(body, th.windowBg, th.rounding);
		dl.addRect(body, th.border, th.borderWidth, th.rounding);
	}

	if (hasTitleBar()) {
		Rect titleRect = titleBarRect(ctx);
		bool frontmost = ctx.panelCount() > 0 && ctx.panelAt(0) == this;
		dl.addRectFilled(titleRect, frontmost ? th.titleBgActive : th.titleBg, th.rounding);

		Rect collapseRect = collapseButtonRect(ctx);
		Rect closeRect    = closeButtonRect(ctx);

		// Title text gets whatever the two buttons leave behind. Both rects are empty when
		// their flag is unset, so this needs no branching (see collapseButtonRect()).
		Rect titleTextRect{
			titleRect.x + collapseRect.w,
			titleRect.y,
			std::max(0.0f, titleRect.w - collapseRect.w - closeRect.w),
			titleRect.h
		};
		titleTextRect = titleTextRect.insetXY(th.windowPadding * 0.5f, 0.0f);
		dl.addTextClipped(ctx.font(), th.fontSize, titleTextRect, th.text, m_title,
		                   Align::Start, Align::Center);

		drawTitleButtons(dl, ctx, collapseRect, closeRect);
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

void Panel::drawTitleButtons(DrawList& dl, const GuiContext& ctx, const Rect& collapseRect,
                              const Rect& closeRect) const {
	const Theme& th = ctx.theme();
	Vec2 mouse = ctx.input().mousePos;

	// Hover feedback has to be resolved here rather than read off ctx.hoveredId(): neither
	// button is a Widget, so hoveredId is kInvalidWidgetId while the cursor is over one
	// (see GuiContext::update()'s tooltip comment). Gate on hoveredPanel() == this so a
	// panel sitting behind another doesn't light up a button through it.
	auto buttonState = [&](const Rect& r, WidgetId buttonId) {
		bool active  = ctx.activeId() == buttonId;
		bool hovered = ctx.hoveredPanel() == this && !r.empty() && r.contains(mouse) &&
		               (ctx.activeId() == kInvalidWidgetId || active);
		return std::pair<bool, bool>{ hovered, active };
	};

	if (!collapseRect.empty()) {
		auto [hovered, active] = buttonState(collapseRect, m_collapseButtonId);
		if (hovered || active) {
			dl.addRectFilled(collapseRect, active ? th.accentActive : th.frameBgHovered, th.rounding);
		}

		// Same disclosure-triangle convention as CollapsingSection: right-pointing when
		// closed, down-pointing when open.
		float triSize = th.fontSize * 0.4f;
		Vec2 c = collapseRect.centre();
		Vec2 p0, p1, p2;
		if (m_collapsed) {
			p0 = { c.x - triSize * 0.6f, c.y - triSize };
			p1 = { c.x - triSize * 0.6f, c.y + triSize };
			p2 = { c.x + triSize * 0.6f, c.y };
		} else {
			p0 = { c.x - triSize, c.y - triSize * 0.6f };
			p1 = { c.x + triSize, c.y - triSize * 0.6f };
			p2 = { c.x, c.y + triSize * 0.6f };
		}
		dl.addTriangleFilled(p0, p1, p2, th.text);
	}

	if (!closeRect.empty()) {
		auto [hovered, active] = buttonState(closeRect, m_closeButtonId);
		if (hovered || active) {
			// Closing is the one destructive thing on a title bar, so its hover state uses
			// the error colour rather than the neutral frameBgHovered the collapse arrow
			// gets -- the affordance should read as "this removes the panel".
			dl.addRectFilled(closeRect, active ? th.error : th.error.withAlpha(0.6f), th.rounding);
		}

		// An X from two strokes rather than a font character: the default glyph ranges
		// bake no multiplication sign or U+2715, and a lowercase 'x' reads as text.
		float arm = th.fontSize * 0.32f;
		Vec2 c = closeRect.centre();
		dl.addLine({ c.x - arm, c.y - arm }, { c.x + arm, c.y + arm }, th.text, th.borderWidth);
		dl.addLine({ c.x - arm, c.y + arm }, { c.x + arm, c.y - arm }, th.text, th.borderWidth);
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
		// docs/gui/05, "Panel", "Anchoring": recompute the absolute position from the
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

	// docs/gui/05, "Panel", "Anchoring and on-screen clamping": "a panel dragged fully off
	// the top edge becomes permanently unreachable." The title bar's full height must stay
	// vertically on screen (it is the only drag handle back into view), and at least a
	// minimal grabbable width of it must stay horizontally on screen.
	m_bounds.y = std::clamp(m_bounds.y, 0.0f, std::max(0.0f, displaySize.y - th.titleBarHeight));

	float minVisibleW = std::min(m_bounds.w, 40.0f);
	m_bounds.x = std::clamp(m_bounds.x, minVisibleW - m_bounds.w, displaySize.x - minVisibleW);
}

} // namespace lightGraphics::ui
