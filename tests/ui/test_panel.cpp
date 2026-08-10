// docs/gui/09 phase 9, "Panel polish": scrolling, resize grip, anchoring, and tooltips --
// headless, same pattern as test_layout.cpp/test_hittest.cpp/test_dropdown.cpp: no Vulkan
// device, no window.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace lvgui = lightGraphics::ui;

namespace {

	lvgui::GuiCreateInfo testCreateInfo() {
		lvgui::GuiCreateInfo info;
		info.fontPath = LVG_UI_TEST_FONT_PATH;
		return info;
	}

	void step(lvgui::GuiContext& ctx) {
		ctx.beginFrame({ 800.0f, 600.0f }, 1.0f, 0.016f);
		ctx.update();
		ctx.endFrame();
	}

	void stepDt(lvgui::GuiContext& ctx, float dt) {
		ctx.beginFrame({ 800.0f, 600.0f }, 1.0f, dt);
		ctx.update();
		ctx.endFrame();
	}

	void stepDisplaySize(lvgui::GuiContext& ctx, lvgui::Vec2 displaySize) {
		ctx.beginFrame(displaySize, 1.0f, 0.016f);
		ctx.update();
		ctx.endFrame();
	}

	void testOverflowingContentGetsAScrollbarShortContentDoesNot() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});

		// Tall enough content (six 40px spacers + spacing + padding) to overflow a
		// 120-tall panel view.
		auto* tall = ctx.createPanel("Tall", { 0.0f, 0.0f, 220.0f, 120.0f });
		for (int i = 0; i < 6; ++i) {
			tall->add<lvgui::Spacer>(40.0f);
		}

		// One short spacer inside a much taller panel -- nothing to overflow.
		auto* shortPanel = ctx.createPanel("Short", { 300.0f, 0.0f, 220.0f, 400.0f });
		shortPanel->add<lvgui::Spacer>(20.0f);

		step(ctx);

		assert(tall->needsScrollbar());
		assert(!shortPanel->needsScrollbar());

		std::cout << "✓ testOverflowingContentGetsAScrollbarShortContentDoesNot\n";
	}

	void testScrollYClampsAtBothEndsAndAWheelAtTopDoesNotGoNegative() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 220.0f, 120.0f });
		for (int i = 0; i < 6; ++i) {
			panel->add<lvgui::Spacer>(40.0f);
		}

		step(ctx);
		assert(panel->needsScrollbar());
		assert(panel->scrollY() == 0.0f);

		lvgui::Vec2 overPanel{ 60.0f, 60.0f };
		ctx.injectMousePos(overPanel);

		// A positive wheelDelta ("away from user") tries to scroll UP from the very top --
		// scrollY must clamp at 0, not go negative.
		ctx.injectScroll(5.0f);
		step(ctx);
		assert(panel->scrollY() == 0.0f);

		// Scroll far past the bottom -- clamps at the computed maximum, derived the same
		// way Panel does internally (contentHeight minus the span below the title bar).
		float titleBarHeight = ctx.theme().titleBarHeight;
		float expectedMax = panel->contentHeight() - (panel->bounds().h - titleBarHeight);
		ctx.injectScroll(-1000.0f);
		step(ctx);
		assert(std::fabs(panel->scrollY() - expectedMax) < 0.01f);

		// And one more huge downward tick must not push it past that maximum.
		ctx.injectScroll(-1000.0f);
		step(ctx);
		assert(std::fabs(panel->scrollY() - expectedMax) < 0.01f);

		std::cout << "✓ testScrollYClampsAtBothEndsAndAWheelAtTopDoesNotGoNegative\n";
	}

	void testWidgetScrolledOutOfViewIsNotReachableViaHitTestDeep() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 220.0f, 150.0f });
		panel->add<lvgui::Spacer>(40.0f);
		auto* target = panel->add<lvgui::Button>("Target");
		for (int i = 0; i < 6; ++i) {
			panel->add<lvgui::Spacer>(40.0f);
		}

		step(ctx);
		assert(panel->needsScrollbar());

		// Scroll by exactly enough that `target`'s row lands flush with the panel's own
		// TOP edge: still inside m_bounds (a naive, unclipped hitTest(p) would say yes),
		// but above the content clip rect's top (title bar + padding), which must reject
		// it -- this is exactly the "scrolled a widget above the content top" case
		// docs/gui/04's clip-rect intersection rule exists for.
		float targetOffsetFromPanelTop = target->bounds().y - panel->bounds().y;
		float lineHeight = ctx.font().lineHeight(ctx.theme().fontSize);
		float wheelNeeded = -targetOffsetFromPanelTop / (3.0f * lineHeight);

		ctx.injectMousePos({ panel->bounds().x + 50.0f, panel->bounds().y + 60.0f });
		ctx.injectScroll(wheelNeeded);
		step(ctx);
		assert(std::fabs(panel->scrollY() - targetOffsetFromPanelTop) < 1.0f);

		// A point squarely inside `target`'s (unclipped) stored Rect, but within the
		// title-bar/padding band above the content clip top.
		lvgui::Vec2 clipped{ target->bounds().x + 10.0f, panel->bounds().y + 5.0f };
		assert(target->hitTest(clipped));   // the widget's own, un-clipped hitTest agrees
		ctx.injectMousePos(clipped);
		step(ctx);
		assert(ctx.hoveredId() != target->id());

		std::cout << "✓ testWidgetScrolledOutOfViewIsNotReachableViaHitTestDeep\n";
	}

	void testResizeEnforcesMinimumSizeInBothAxes() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 100.0f, 100.0f, 300.0f, 200.0f });

		step(ctx);
		const lvgui::Theme& th = ctx.theme();
		float minW = 8.0f * th.fontSize;
		float minH = th.titleBarHeight + 2.0f * th.windowPadding;

		lvgui::Rect b = panel->bounds();
		lvgui::Vec2 gripPoint{ b.right() - 2.0f, b.bottom() - 2.0f };

		ctx.injectMousePos(gripPoint);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(ctx.activeId() == panel->resizeGripId());

		// Drag far up-left, well past the minimum in both axes.
		ctx.injectMousePos({ b.x - 500.0f, b.y - 500.0f });
		step(ctx);

		assert(std::fabs(panel->bounds().w - minW) < 0.5f);
		assert(std::fabs(panel->bounds().h - minH) < 0.5f);

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testResizeEnforcesMinimumSizeInBothAxes\n";
	}

	void testAnchoredPanelHoldsCornerOffsetAcrossDisplaySizeChange() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		// 800x600 (the fixture displaySize) minus this rect's bottom-right corner is a
		// (50, 50) offset from the bottom-right of the display.
		auto* panel = ctx.createPanel("Panel", { 600.0f, 450.0f, 150.0f, 100.0f });
		panel->setAnchor(lvgui::Panel::Anchor::BottomRight);

		// First frame just RECORDS the offset from the current bounds/displaySize -- it
		// must not move the panel.
		step(ctx);
		assert(panel->bounds().x == 600.0f);
		assert(panel->bounds().y == 450.0f);

		stepDisplaySize(ctx, { 1000.0f, 800.0f });

		lvgui::Rect r = panel->bounds();
		assert(std::fabs(r.w - 150.0f) < 0.01f);
		assert(std::fabs(r.h - 100.0f) < 0.01f);
		// Same (50, 50) offset from the NEW bottom-right corner.
		assert(std::fabs((1000.0f - r.right()) - 50.0f) < 0.5f);
		assert(std::fabs((800.0f - r.bottom()) - 50.0f) < 0.5f);

		std::cout << "✓ testAnchoredPanelHoldsCornerOffsetAcrossDisplaySizeChange\n";
	}

	void testPanelDraggedTowardTopEdgeStopsWithTitleBarOnScreen() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 100.0f, 50.0f, 200.0f, 150.0f });

		ctx.injectMousePos({ 150.0f, 55.0f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);

		// Past the 4px drag threshold, then far off the top of the framebuffer.
		ctx.injectMousePos({ 150.0f, -2000.0f });
		step(ctx);
		ctx.injectMousePos({ 150.0f, -2000.0f });
		step(ctx);

		assert(panel->bounds().y == 0.0f);

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testPanelDraggedTowardTopEdgeStopsWithTitleBarOnScreen\n";
	}

	void testTooltipTimerResetsWhenHoveredIdChanges() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 200.0f });
		auto* a = panel->add<lvgui::Button>("A");
		a->setTooltip("Tooltip A");
		auto* b = panel->add<lvgui::Button>("B");
		b->setTooltip("Tooltip B");

		step(ctx);   // layout only; mouse is nowhere near either button yet
		assert(!ctx.isTooltipVisible());

		lvgui::Rect ab = a->bounds();
		lvgui::Vec2 onA{ ab.x + ab.w * 0.5f, ab.y + ab.h * 0.5f };
		lvgui::Rect bb = b->bounds();
		lvgui::Vec2 onB{ bb.x + bb.w * 0.5f, bb.y + bb.h * 0.5f };

		float delay = ctx.theme().tooltipDelay;

		// hoveredId changes to `a` THIS frame -- the timer resets to 0 (not yet
		// accumulating this frame's own dt), so no tooltip yet.
		ctx.injectMousePos(onA);
		stepDt(ctx, 0.01f);
		assert(ctx.hoveredId() == a->id());
		assert(!ctx.isTooltipVisible());

		// Same widget still hovered across two more frames -- the timer accumulates.
		// One frame short of the delay: not visible yet.
		stepDt(ctx, delay * 0.6f);
		assert(ctx.hoveredId() == a->id());
		assert(!ctx.isTooltipVisible());

		// Past the delay: visible.
		stepDt(ctx, delay * 0.6f);
		assert(ctx.hoveredId() == a->id());
		assert(ctx.isTooltipVisible());

		// Switching to `b` changes hoveredId -- the timer must reset, so the tooltip
		// disappears immediately even though the PREVIOUS accumulated time (1.2x the
		// delay) would otherwise have been more than enough.
		ctx.injectMousePos(onB);
		stepDt(ctx, 0.016f);
		assert(ctx.hoveredId() == b->id());
		assert(!ctx.isTooltipVisible());

		std::cout << "✓ testTooltipTimerResetsWhenHoveredIdChanges\n";
	}

}

int main() {
	testOverflowingContentGetsAScrollbarShortContentDoesNot();
	testScrollYClampsAtBothEndsAndAWheelAtTopDoesNotGoNegative();
	testWidgetScrolledOutOfViewIsNotReachableViaHitTestDeep();
	testResizeEnforcesMinimumSizeInBothAxes();
	testAnchoredPanelHoldsCornerOffsetAcrossDisplaySizeChange();
	testPanelDraggedTowardTopEdgeStopsWithTitleBarOnScreen();
	testTooltipTimerResetsWhenHoveredIdChanges();

	std::cout << "\n✅ All Panel (phase 9) tests passed!\n";
	return 0;
}
