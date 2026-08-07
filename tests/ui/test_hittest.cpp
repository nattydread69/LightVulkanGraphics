#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
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

	void testWantsMouseTracksCursorOverPanel() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		ctx.createPanel("Panel", { 100.0f, 100.0f, 200.0f, 150.0f });

		ctx.injectMousePos({ 1000.0f, 1000.0f });   // off in the scene, no panel there
		step(ctx);
		assert(!ctx.wantsMouse());

		ctx.injectMousePos({ 150.0f, 150.0f });     // inside the panel
		step(ctx);
		assert(ctx.wantsMouse());

		ctx.injectMousePos({ 1000.0f, 1000.0f });   // back to the scene
		step(ctx);
		assert(!ctx.wantsMouse());

		std::cout << "✓ testWantsMouseTracksCursorOverPanel\n";
	}

	void testButtonFiresOnceOnReleaseInsideNeverOnPress() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 200.0f });
		auto* btn = panel->add<lvgui::Button>("Click me");

		int fireCount = 0;
		btn->setOnClick([&] { ++fireCount; });

		// One frame so layout places the button before we aim at it.
		step(ctx);
		lvgui::Rect b = btn->bounds();
		lvgui::Vec2 inside{ b.x + b.w * 0.5f, b.y + b.h * 0.5f };

		ctx.injectMousePos(inside);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(fireCount == 0);   // never fires on press alone
		assert(ctx.activeId() == btn->id());

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(fireCount == 1);   // fires exactly once on release-inside
		assert(ctx.activeId() == lvgui::kInvalidWidgetId);

		std::cout << "✓ testButtonFiresOnceOnReleaseInsideNeverOnPress\n";
	}

	void testButtonDoesNotFireWhenReleasedOutside() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 200.0f });
		auto* btn = panel->add<lvgui::Button>("Click me");

		int fireCount = 0;
		btn->setOnClick([&] { ++fireCount; });

		step(ctx);
		lvgui::Rect b = btn->bounds();
		lvgui::Vec2 inside{ b.x + b.w * 0.5f, b.y + b.h * 0.5f };
		lvgui::Vec2 outside{ b.right() + 60.0f, b.y + b.h * 0.5f };

		ctx.injectMousePos(inside);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(ctx.activeId() == btn->id());   // capture holds even once the cursor leaves

		ctx.injectMousePos(outside);
		step(ctx);
		assert(ctx.activeId() == btn->id());

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(fireCount == 0);
		assert(ctx.activeId() == lvgui::kInvalidWidgetId);

		std::cout << "✓ testButtonDoesNotFireWhenReleasedOutside\n";
	}

	void testHoveredAndFocusedIdResolution() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 200.0f });
		auto* label = panel->add<lvgui::Label>("Not clickable");
		auto* btn = panel->add<lvgui::Button>("Click me");

		step(ctx);
		lvgui::Rect bb = btn->bounds();
		lvgui::Vec2 onButton{ bb.x + bb.w * 0.5f, bb.y + bb.h * 0.5f };
		lvgui::Rect lb = label->bounds();
		lvgui::Vec2 onLabel{ lb.x + lb.w * 0.5f, lb.y + lb.h * 0.5f };

		ctx.injectMousePos(onButton);
		step(ctx);
		assert(ctx.hoveredId() == btn->id());

		// Clicking the button both captures it and focuses it.
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(ctx.focusedId() == btn->id());
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		// A click on the (non-capturing) label must not steal or clear that focus.
		ctx.injectMousePos(onLabel);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(ctx.focusedId() == btn->id());
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testHoveredAndFocusedIdResolution\n";
	}

}

int main() {
	testWantsMouseTracksCursorOverPanel();
	testButtonFiresOnceOnReleaseInsideNeverOnPress();
	testButtonDoesNotFireWhenReleasedOutside();
	testHoveredAndFocusedIdResolution();

	std::cout << "\n✅ All hit-test tests passed!\n";
	return 0;
}
