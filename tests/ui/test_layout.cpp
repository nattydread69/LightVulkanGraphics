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

	void testThreeLabelsAndAButtonLayOutInOrder() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 100.0f, 100.0f, 300.0f, 400.0f });

		auto* l1 = panel->add<lvgui::Label>("One");
		auto* l2 = panel->add<lvgui::Label>("Two");
		auto* l3 = panel->add<lvgui::Label>("Three");
		auto* btn = panel->add<lvgui::Button>("Go");

		step(ctx);

		const lvgui::Theme& th = ctx.theme();
		float expectedX = panel->bounds().x + th.windowPadding;
		float expectedY = panel->bounds().y + th.titleBarHeight + th.windowPadding;

		assert(std::round(l1->bounds().x) == std::round(expectedX));
		assert(std::round(l1->bounds().y) == std::round(expectedY));
		assert(l1->bounds().w > 0.0f);

		// Declaration order = layout order: each row sits strictly below the previous one.
		assert(l2->bounds().y > l1->bounds().y);
		assert(l3->bounds().y > l2->bounds().y);
		assert(btn->bounds().y > l3->bounds().y);

		// All rows share the same x and width -- a single vertical stack.
		assert(l2->bounds().x == l1->bounds().x);
		assert(btn->bounds().w == l1->bounds().w);

		std::cout << "✓ testThreeLabelsAndAButtonLayOutInOrder\n";
	}

	void testTitleBarDragRespectsThresholdThenMoves() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 150.0f });

		// Press on the title bar.
		ctx.injectMousePos({ 100.0f, 55.0f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);

		lvgui::Rect afterPress = panel->bounds();
		assert(afterPress.x == 50.0f);

		// A 2px jitter while still held must NOT move the panel (4px drag threshold).
		ctx.injectMousePos({ 102.0f, 55.0f });
		step(ctx);
		assert(panel->bounds().x == afterPress.x);
		assert(panel->bounds().y == afterPress.y);

		// Moving well past the threshold must move the panel by the same delta.
		ctx.injectMousePos({ 130.0f, 55.0f });
		step(ctx);
		assert(panel->bounds().x == afterPress.x + 30.0f);
		assert(panel->bounds().y == afterPress.y);

		// Releasing stops the drag; further movement without a new press must not move it.
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		lvgui::Rect afterRelease = panel->bounds();

		ctx.injectMousePos({ 400.0f, 400.0f });
		step(ctx);
		assert(panel->bounds().x == afterRelease.x);
		assert(panel->bounds().y == afterRelease.y);

		std::cout << "✓ testTitleBarDragRespectsThresholdThenMoves\n";
	}

	void testZOrderClickBringsBackPanelToFront() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* back = ctx.createPanel("Back", { 50.0f, 50.0f, 200.0f, 150.0f });
		auto* front = ctx.createPanel("Front", { 300.0f, 50.0f, 200.0f, 150.0f });

		// The most recently created panel starts frontmost.
		assert(ctx.panelAt(0) == front);
		assert(ctx.panelAt(1) == back);

		// Click inside 'back', which does not overlap 'front'.
		ctx.injectMousePos({ 60.0f, 100.0f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);

		assert(ctx.panelAt(0) == back);
		assert(ctx.panelAt(1) == front);

		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testZOrderClickBringsBackPanelToFront\n";
	}

	void testWrappingLabelConvergesInOneFrame() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 160.0f, 400.0f });

		auto* label = panel->add<lvgui::Label>(
			"This is a fairly long sentence that should wrap across several lines "
			"once word wrap is enabled on a narrow panel.");
		label->setWordWrap(true);

		auto* after = panel->add<lvgui::Label>("After");

		step(ctx);

		float lh = ctx.font().lineHeight(ctx.theme().fontSize);
		// The wrapping label must occupy more than one line's height...
		assert(label->bounds().h > lh + 0.5f);
		// ...and the next row must already sit below it in THIS frame (not one frame
		// late), which is exactly why endFrame() lays out every panel twice.
		assert(after->bounds().y >= label->bounds().y + label->bounds().h);

		std::cout << "✓ testWrappingLabelConvergesInOneFrame\n";
	}

	void testSpacerAndSeparatorContributeFixedHeightRows() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 200.0f, 400.0f });

		auto* before = panel->add<lvgui::Label>("Before");
		auto* spacer = panel->add<lvgui::Spacer>(40.0f);
		auto* sep = panel->add<lvgui::Separator>();
		auto* after = panel->add<lvgui::Label>("After");

		step(ctx);

		assert(spacer->bounds().h == 40.0f);
		assert(spacer->bounds().y == before->bounds().y + before->bounds().h + ctx.theme().itemSpacing);
		assert(sep->bounds().y == spacer->bounds().y + spacer->bounds().h + ctx.theme().itemSpacing);
		assert(after->bounds().y == sep->bounds().y + sep->bounds().h + ctx.theme().itemSpacing);

		// Neither is a click target: a Spacer is never hit-testable, and a Separator
		// passes clicks through to whatever is behind it (docs/gui/05, base class).
		assert(!spacer->hitTest(spacer->bounds().centre()));
		assert(!sep->acceptsCapture());

		std::cout << "✓ testSpacerAndSeparatorContributeFixedHeightRows\n";
	}

}

int main() {
	testThreeLabelsAndAButtonLayOutInOrder();
	testTitleBarDragRespectsThresholdThenMoves();
	testZOrderClickBringsBackPanelToFront();
	testWrappingLabelConvergesInOneFrame();
	testSpacerAndSeparatorContributeFixedHeightRows();

	std::cout << "\n✅ All layout tests passed!\n";
	return 0;
}
