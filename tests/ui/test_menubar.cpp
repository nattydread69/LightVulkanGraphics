// docs/gui/05-widgets.md, "MenuBar" -- headless, same pattern as test_contextmenu.cpp:
// no Vulkan device, no window. MenuBar is deliberately NOT a Widget (see MenuBar.h's
// class comment), so these tests drive it purely through ctx.menuBar() + the ordinary
// beginFrame()/update()/endFrame() + inject*() cycle, never through a Panel or a
// WidgetId.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <iostream>
#include <string>

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

	void moveTo(lvgui::GuiContext& ctx, lvgui::Vec2 pos) {
		ctx.injectMousePos(pos);
		step(ctx);
	}

	void clickAt(lvgui::GuiContext& ctx, lvgui::Vec2 pos) {
		ctx.injectMousePos(pos);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
	}

	lvgui::Vec2 centre(const lvgui::Rect& r) {
		return { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
	}

	void testMenuBarStartsEmptyByDefault() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		assert(ctx.menuBar().menuCount() == 0);
		assert(ctx.menuBar().height(ctx) == 0.0f);
		assert(ctx.menuBar().openIndex() == -1);

		std::cout << "✓ testMenuBarStartsEmptyByDefault\n";
	}

	void testAddMenuIncreasesCountAndHeight() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		ctx.menuBar().addMenu("File");
		ctx.menuBar().addMenu("Edit");

		assert(ctx.menuBar().menuCount() == 2);
		assert(ctx.menuBar().height(ctx) == ctx.theme().titleBarHeight);

		std::cout << "✓ testAddMenuIncreasesCountAndHeight\n";
	}

	void testClickingATitleOpensItsMenu() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto file = ctx.menuBar().addMenu("File");
		file.addItem("New", [] {});
		step(ctx);

		lvgui::Rect title = ctx.menuBar().menuTitleRect(ctx, 0);
		clickAt(ctx, centre(title));

		assert(ctx.menuBar().openIndex() == 0);

		std::cout << "✓ testClickingATitleOpensItsMenu\n";
	}

	void testClickingTheOpenTitleAgainClosesIt() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto file = ctx.menuBar().addMenu("File");
		file.addItem("New", [] {});
		step(ctx);

		lvgui::Rect title = ctx.menuBar().menuTitleRect(ctx, 0);
		clickAt(ctx, centre(title));
		assert(ctx.menuBar().openIndex() == 0);

		clickAt(ctx, centre(title));
		assert(ctx.menuBar().openIndex() == -1);

		std::cout << "✓ testClickingTheOpenTitleAgainClosesIt\n";
	}

	void testHoveringASiblingTitleWhileOpenSwitchesWithoutAClick() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto file = ctx.menuBar().addMenu("File");
		file.addItem("New", [] {});
		auto edit = ctx.menuBar().addMenu("Edit");
		edit.addItem("Undo", [] {});
		step(ctx);

		clickAt(ctx, centre(ctx.menuBar().menuTitleRect(ctx, 0)));
		assert(ctx.menuBar().openIndex() == 0);

		// No click here -- just moving the cursor onto "Edit" while "File" is open.
		moveTo(ctx, centre(ctx.menuBar().menuTitleRect(ctx, 1)));
		assert(ctx.menuBar().openIndex() == 1);

		std::cout << "✓ testHoveringASiblingTitleWhileOpenSwitchesWithoutAClick\n";
	}

	void testClickingAnItemFiresOnSelectAndCloses() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto file = ctx.menuBar().addMenu("File");
		int fired = -1;
		file.addItem("New", [&] { fired = 0; });
		file.addItem("Open", [&] { fired = 1; });
		step(ctx);

		clickAt(ctx, centre(ctx.menuBar().menuTitleRect(ctx, 0)));
		assert(ctx.menuBar().openIndex() == 0);

		const lvgui::Theme& th = ctx.theme();
		lvgui::Rect menu = ctx.menuBar().openMenuRect(ctx);
		lvgui::Vec2 secondRow{ menu.x + menu.w * 0.5f, menu.y + th.framePadding + 1.5f * th.rowHeight };
		clickAt(ctx, secondRow);

		assert(fired == 1);
		assert(ctx.menuBar().openIndex() == -1);

		std::cout << "✓ testClickingAnItemFiresOnSelectAndCloses\n";
	}

	void testClickOutsideClosesWithoutSelecting() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto file = ctx.menuBar().addMenu("File");
		bool fired = false;
		file.addItem("Only item", [&] { fired = true; });
		step(ctx);

		clickAt(ctx, centre(ctx.menuBar().menuTitleRect(ctx, 0)));
		assert(ctx.menuBar().openIndex() == 0);

		clickAt(ctx, { 700.0f, 550.0f });   // far outside both the bar and the dropdown

		assert(!fired);
		assert(ctx.menuBar().openIndex() == -1);

		std::cout << "✓ testClickOutsideClosesWithoutSelecting\n";
	}

	void testEscapeClosesTheOpenMenu() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto file = ctx.menuBar().addMenu("File");
		file.addItem("Item", [] {});
		step(ctx);

		clickAt(ctx, centre(ctx.menuBar().menuTitleRect(ctx, 0)));
		assert(ctx.menuBar().openIndex() == 0);

		ctx.injectKey(lvgui::Key::Escape, 0, true, false);
		step(ctx);
		ctx.injectKey(lvgui::Key::Escape, 0, false, false);
		step(ctx);

		assert(ctx.menuBar().openIndex() == -1);

		std::cout << "✓ testEscapeClosesTheOpenMenu\n";
	}

	// A panel positioned so its own bounds overlap the bar's row -- clicking the bar's
	// title must resolve to the menu, never to whatever panel happens to sit underneath
	// it (docs/gui/05-widgets.md, "MenuBar": the panel hit-test walk is skipped entirely
	// while the cursor is over the bar or its open dropdown).
	void testMenuBarTakesPriorityOverAnOverlappingPanel() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 400.0f, 300.0f });
		auto file = ctx.menuBar().addMenu("File");
		file.addItem("New", [] {});
		step(ctx);

		lvgui::Rect title = ctx.menuBar().menuTitleRect(ctx, 0);
		assert(panel->bounds().contains(centre(title)));   // sanity: they really do overlap

		clickAt(ctx, centre(title));

		assert(ctx.menuBar().openIndex() == 0);
		// The click must not also have raised/focused the panel underneath.
		assert(ctx.hoveredId() == lvgui::kInvalidWidgetId);

		std::cout << "✓ testMenuBarTakesPriorityOverAnOverlappingPanel\n";
	}

	void testWantsMouseTrueWhileHoveringOrMenuOpen() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto file = ctx.menuBar().addMenu("File");
		file.addItem("New", [] {});
		step(ctx);

		moveTo(ctx, { 400.0f, 300.0f });   // bare scene, well below the bar
		assert(!ctx.wantsMouse());

		moveTo(ctx, centre(ctx.menuBar().menuTitleRect(ctx, 0)));
		assert(ctx.wantsMouse());

		clickAt(ctx, centre(ctx.menuBar().menuTitleRect(ctx, 0)));
		assert(ctx.menuBar().openIndex() == 0);
		moveTo(ctx, { 400.0f, 300.0f });   // moved off the bar, but the menu is still open
		assert(ctx.wantsMouse());

		std::cout << "✓ testWantsMouseTrueWhileHoveringOrMenuOpen\n";
	}

	void testSeparatorIsSkippedInItemLayout() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto file = ctx.menuBar().addMenu("File");
		bool fired = false;
		file.addItem("Above", [&] { fired = true; });
		file.addSeparator();
		file.addItem("Below", [&] { fired = true; });
		step(ctx);

		clickAt(ctx, centre(ctx.menuBar().menuTitleRect(ctx, 0)));

		const lvgui::Theme& th = ctx.theme();
		lvgui::Rect menu = ctx.menuBar().openMenuRect(ctx);
		lvgui::Vec2 separatorPoint{ menu.x + menu.w * 0.5f,
		                            menu.y + th.framePadding + th.rowHeight + th.itemSpacing * 0.5f };
		clickAt(ctx, separatorPoint);

		// Clicking a separator is not selectable, but still closes the menu (same
		// convention as ContextMenu).
		assert(!fired);
		assert(ctx.menuBar().openIndex() == -1);

		std::cout << "✓ testSeparatorIsSkippedInItemLayout\n";
	}

}

int main() {
	testMenuBarStartsEmptyByDefault();
	testAddMenuIncreasesCountAndHeight();
	testClickingATitleOpensItsMenu();
	testClickingTheOpenTitleAgainClosesIt();
	testHoveringASiblingTitleWhileOpenSwitchesWithoutAClick();
	testClickingAnItemFiresOnSelectAndCloses();
	testClickOutsideClosesWithoutSelecting();
	testEscapeClosesTheOpenMenu();
	testMenuBarTakesPriorityOverAnOverlappingPanel();
	testWantsMouseTrueWhileHoveringOrMenuOpen();
	testSeparatorIsSkippedInItemLayout();

	std::cout << "\n✅ All MenuBar tests passed!\n";
	return 0;
}
