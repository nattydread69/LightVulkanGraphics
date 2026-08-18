// Panel behaviour: scrolling, resize grip, anchoring, tooltips, the two title-bar
// buttons (collapse/close), and layout persistence -- headless, same pattern as
// test_layout.cpp/test_hittest.cpp/test_dropdown.cpp: no Vulkan device, no window.
// See docs/gui/05-widgets.md, "Panel", for the behaviour these pin down.

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

	// Presses and releases at the same point, one frame each -- the release-inside gesture
	// both title-bar buttons act on.
	void clickAt(lvgui::GuiContext& ctx, lvgui::Vec2 p) {
		ctx.injectMousePos(p);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
	}

	void testCollapseButtonTogglesAndShrinksThePanelToItsTitleBar() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 100.0f, 100.0f, 220.0f, 300.0f },
		                               lvgui::PanelFlags::Collapsible | lvgui::PanelFlags::Movable);
		panel->add<lvgui::Spacer>(40.0f);
		step(ctx);

		assert(!panel->collapsed());

		// The arrow is a titleBarHeight square at the title bar's leading edge.
		const float titleH = ctx.theme().titleBarHeight;
		const lvgui::Vec2 arrow{ panel->bounds().x + titleH * 0.5f, panel->bounds().y + titleH * 0.5f };

		clickAt(ctx, arrow);
		assert(panel->collapsed());

		// Collapsed, the panel occupies ONLY its title bar: a point that was comfortably
		// inside the expanded body must no longer hit it...
		lvgui::Vec2 belowTitleBar{ panel->bounds().x + 50.0f, panel->bounds().y + titleH + 40.0f };
		assert(!panel->hitTest(belowTitleBar));
		// ...while the title bar itself still does, or the panel would be unreachable.
		assert(panel->hitTest(arrow));

		// m_bounds is deliberately preserved across a collapse, so expanding restores the
		// original size rather than some remembered-height approximation of it.
		assert(panel->bounds().h == 300.0f);

		clickAt(ctx, arrow);
		assert(!panel->collapsed());
		assert(panel->hitTest(belowTitleBar));

		std::cout << "✓ testCollapseButtonTogglesAndShrinksThePanelToItsTitleBar\n";
	}

	void testClickingCollapseArrowDoesNotAlsoDragThePanel() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 100.0f, 100.0f, 220.0f, 300.0f },
		                               lvgui::PanelFlags::Collapsible | lvgui::PanelFlags::Movable);
		step(ctx);

		const float titleH = ctx.theme().titleBarHeight;
		const lvgui::Vec2 arrow{ panel->bounds().x + titleH * 0.5f, panel->bounds().y + titleH * 0.5f };
		const lvgui::Rect before = panel->bounds();

		// Press on the arrow, then move well past the 4px drag threshold before releasing.
		// The button owns the capture, so the panel must not follow the cursor.
		ctx.injectMousePos(arrow);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMousePos({ arrow.x + 120.0f, arrow.y + 90.0f });
		step(ctx);

		assert(panel->bounds().x == before.x);
		assert(panel->bounds().y == before.y);

		// Released off the arrow, so the toggle is cancelled too (release-INSIDE only).
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(!panel->collapsed());

		std::cout << "✓ testClickingCollapseArrowDoesNotAlsoDragThePanel\n";
	}

	void testCloseButtonHidesThePanelAndFiresOnCloseOnce() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 100.0f, 100.0f, 220.0f, 300.0f },
		                               lvgui::PanelFlags::Closable | lvgui::PanelFlags::Movable);
		int closeCount = 0;
		bool visibleInsideCallback = true;
		panel->setOnClose([&] {
			++closeCount;
			// The callback documents that it fires AFTER the panel has hidden itself, so a
			// handler can veto by setting it back -- verify that ordering holds.
			visibleInsideCallback = panel->visible();
		});
		step(ctx);

		const float titleH = ctx.theme().titleBarHeight;
		const lvgui::Vec2 closeBtn{ panel->bounds().right() - titleH * 0.5f, panel->bounds().y + titleH * 0.5f };

		clickAt(ctx, closeBtn);

		assert(closeCount == 1);
		assert(!visibleInsideCallback);
		assert(!panel->visible());
		assert(!panel->hitTest(closeBtn));

		// A hidden panel gets no update()/draw(), so no further callbacks can fire.
		step(ctx);
		assert(closeCount == 1);

		std::cout << "✓ testCloseButtonHidesThePanelAndFiresOnCloseOnce\n";
	}

	void testTitleBarButtonsAreAbsentWithoutTheirFlags() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		// PanelFlags::Default carries Collapsible but NOT Closable.
		auto* panel = ctx.createPanel("Panel", { 100.0f, 100.0f, 220.0f, 300.0f },
		                               lvgui::PanelFlags::Movable);
		step(ctx);

		const float titleH = ctx.theme().titleBarHeight;
		const lvgui::Vec2 arrow{ panel->bounds().x + titleH * 0.5f, panel->bounds().y + titleH * 0.5f };
		const lvgui::Vec2 closeBtn{ panel->bounds().right() - titleH * 0.5f, panel->bounds().y + titleH * 0.5f };

		assert(!panel->hitTestCollapseButton(ctx, arrow));
		assert(!panel->hitTestCloseButton(ctx, closeBtn));

		// With neither flag, a title-bar click is a plain drag -- unchanged behaviour.
		ctx.injectMousePos(arrow);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMousePos({ arrow.x + 60.0f, arrow.y + 30.0f });
		step(ctx);
		assert(panel->bounds().x > 100.0f);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(!panel->collapsed());
		assert(panel->visible());

		std::cout << "✓ testTitleBarButtonsAreAbsentWithoutTheirFlags\n";
	}

	void testSaveLayoutSkipsPanelsWithoutAPersistenceId() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* kept = ctx.createPanel("Kept", { 10.0f, 10.0f, 100.0f, 100.0f });
		kept->setPersistenceId("kept");
		ctx.createPanel("Skipped", { 200.0f, 200.0f, 100.0f, 100.0f });   // no id set
		step(ctx);

		std::string saved = ctx.saveLayout();
		assert(saved.find("[kept]") != std::string::npos);
		assert(saved.find("[Skipped]") == std::string::npos);

		std::cout << "✓ testSaveLayoutSkipsPanelsWithoutAPersistenceId\n";
	}

	void testSaveThenLoadRestoresBoundsAndCollapsed() {
		std::string saved;
		{
			lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
			auto* panel = ctx.createPanel("Settings", { 10.0f, 10.0f, 100.0f, 100.0f },
			                               lvgui::PanelFlags::Default);
			panel->setPersistenceId("settings");
			step(ctx);

			panel->setBounds({ 340.0f, 220.0f, 260.0f, 180.0f });
			panel->setCollapsed(true);
			step(ctx);

			saved = ctx.saveLayout();
			assert(!saved.empty());
		}

		// A FRESH GuiContext and panel -- loadLayout() must work across sessions, not
		// just onto the same Panel* it happened to be saved from (that's the whole
		// point: this is meant to run again after the process restarts).
		lvgui::GuiContext ctx2(testCreateInfo(), lvgui::PlatformHooks{});
		auto* restored = ctx2.createPanel("Settings", { 10.0f, 10.0f, 100.0f, 100.0f },
		                                   lvgui::PanelFlags::Default);
		restored->setPersistenceId("settings");
		step(ctx2);

		ctx2.loadLayout(saved);

		assert(restored->bounds().x == 340.0f);
		assert(restored->bounds().y == 220.0f);
		assert(restored->bounds().w == 260.0f);
		assert(restored->bounds().h == 180.0f);
		assert(restored->collapsed());

		std::cout << "✓ testSaveThenLoadRestoresBoundsAndCollapsed\n";
	}

	void testLoadLayoutIgnoresIdsWithNoMatchingCurrentPanel() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 10.0f, 10.0f, 100.0f, 100.0f });
		panel->setPersistenceId("panel");
		step(ctx);
		lvgui::Rect before = panel->bounds();

		// A section for an id nothing currently claims -- must be silently skipped, not
		// crash and not disturb the panel that DOES exist.
		ctx.loadLayout("[nonexistent-panel]\nx=999.0\ny=999.0\n");

		assert(panel->bounds().x == before.x);
		assert(panel->bounds().y == before.y);

		std::cout << "✓ testLoadLayoutIgnoresIdsWithNoMatchingCurrentPanel\n";
	}

	void testLoadLayoutBeforeFirstLayoutPassStillClampsScrollYNextPass() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 220.0f, 120.0f });
		panel->setPersistenceId("panel");
		for (int i = 0; i < 6; ++i) {
			panel->add<lvgui::Spacer>(40.0f);
		}
		// Deliberately no step() here -- loadLayout() runs before layout() has EVER
		// computed a real m_contentHeight, so maxScrollY() would currently answer 0.
		// setScrollY() must not clamp against that not-yet-valid bound (see its own
		// comment, Panel.h) -- restoring an absurdly large value must not stick past the
		// next real layout pass.
		ctx.loadLayout("[panel]\nscrollY=99999.0\n");

		step(ctx);   // runs layout() for the first time

		assert(panel->needsScrollbar());
		assert(panel->scrollY() >= 0.0f);
		assert(panel->scrollY() <= panel->contentHeight());   // sanity: not still 99999

		std::cout << "✓ testLoadLayoutBeforeFirstLayoutPassStillClampsScrollYNextPass\n";
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
	testCollapseButtonTogglesAndShrinksThePanelToItsTitleBar();
	testClickingCollapseArrowDoesNotAlsoDragThePanel();
	testCloseButtonHidesThePanelAndFiresOnCloseOnce();
	testTitleBarButtonsAreAbsentWithoutTheirFlags();
	testSaveLayoutSkipsPanelsWithoutAPersistenceId();
	testSaveThenLoadRestoresBoundsAndCollapsed();
	testLoadLayoutIgnoresIdsWithNoMatchingCurrentPanel();
	testLoadLayoutBeforeFirstLayoutPassStillClampsScrollYNextPass();

	std::cout << "\n✅ All Panel tests passed!\n";
	return 0;
}
