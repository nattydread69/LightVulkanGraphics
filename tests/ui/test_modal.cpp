// docs/gui/05-widgets.md, "Panel", "Modal panels" -- headless, same pattern as
// test_panel.cpp: no Vulkan device, no window.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <iostream>
#include <vector>
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

	void clickAt(lvgui::GuiContext& ctx, lvgui::Vec2 pos) {
		ctx.injectMousePos(pos);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
	}

	void pressKey(lvgui::GuiContext& ctx, int key, int mods = 0) {
		ctx.injectKey(key, mods, true, false);
		step(ctx);
		ctx.injectKey(key, mods, false, false);
		step(ctx);
	}

	lvgui::Vec2 centre(lvgui::Widget* w) {
		lvgui::Rect b = w->bounds();
		return { b.x + b.w * 0.5f, b.y + b.h * 0.5f };
	}

	void testNoModalMeansActiveModalPanelIsNull() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 200.0f });
		step(ctx);

		assert(ctx.activeModalPanel() == nullptr);

		std::cout << "✓ testNoModalMeansActiveModalPanelIsNull\n";
	}

	void testActiveModalPanelIgnoresInvisibleModal() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* modal = ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f }, lvgui::PanelFlags::Modal);
		modal->setVisible(false);
		step(ctx);

		assert(ctx.activeModalPanel() == nullptr);

		std::cout << "✓ testActiveModalPanelIgnoresInvisibleModal\n";
	}

	void testActiveModalPanelReturnsTheOpenModal() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		ctx.createPanel("Background", { 50.0f, 50.0f, 200.0f, 200.0f });
		auto* modal = ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f }, lvgui::PanelFlags::Modal);
		step(ctx);

		assert(ctx.activeModalPanel() == modal);

		std::cout << "✓ testActiveModalPanelReturnsTheOpenModal\n";
	}

	void testBackgroundWidgetDoesNotReceiveClicksWhileModalOpen() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* background = ctx.createPanel("Background", { 50.0f, 50.0f, 200.0f, 200.0f });
		auto* bgCheckbox = background->add<lvgui::Checkbox>("Enabled", false);
		ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f }, lvgui::PanelFlags::Modal);
		step(ctx);

		clickAt(ctx, centre(bgCheckbox));

		assert(!bgCheckbox->value());

		std::cout << "✓ testBackgroundWidgetDoesNotReceiveClicksWhileModalOpen\n";
	}

	void testModalsOwnWidgetStillRespondsWhileOpen() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* modal = ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f }, lvgui::PanelFlags::Modal);
		auto* okCheckbox = modal->add<lvgui::Checkbox>("Confirm", false);
		step(ctx);

		clickAt(ctx, centre(okCheckbox));

		assert(okCheckbox->value());

		std::cout << "✓ testModalsOwnWidgetStillRespondsWhileOpen\n";
	}

	void testWantsMouseIsTrueEvenFarFromTheModal() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f }, lvgui::PanelFlags::Modal);
		step(ctx);

		// Nowhere near the modal, nowhere near any panel at all -- this is exactly the
		// case that must still say yes (docs/gui/05-widgets.md, "Panel", "Modal panels"):
		// a click here must not reach a 3D scene behind the dialog.
		ctx.injectMousePos({ 10.0f, 590.0f });
		step(ctx);

		assert(ctx.wantsMouse());

		std::cout << "✓ testWantsMouseIsTrueEvenFarFromTheModal\n";
	}

	void testWantsKeyboardIsTrueWhileModalOpenEvenWithNoFocus() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f }, lvgui::PanelFlags::Modal);
		step(ctx);

		assert(ctx.focusedId() == lvgui::kInvalidWidgetId);
		assert(ctx.wantsKeyboard());

		std::cout << "✓ testWantsKeyboardIsTrueWhileModalOpenEvenWithNoFocus\n";
	}

	void testWantsScrollIsTrueWhileModalOpenEvenOverNothing() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f }, lvgui::PanelFlags::Modal);
		step(ctx);

		ctx.injectMousePos({ 10.0f, 10.0f });
		step(ctx);

		assert(ctx.wantsScroll());

		std::cout << "✓ testWantsScrollIsTrueWhileModalOpenEvenOverNothing\n";
	}

	void testNoneOfTheWantsAreForcedWithoutAModal() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 200.0f });
		step(ctx);

		ctx.injectMousePos({ 780.0f, 590.0f });   // outside the panel, outside everything
		step(ctx);

		assert(!ctx.wantsMouse());
		assert(!ctx.wantsKeyboard());
		assert(!ctx.wantsScroll());

		std::cout << "✓ testNoneOfTheWantsAreForcedWithoutAModal\n";
	}

	void testTabCyclingStaysWithinModalWidgets() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* background = ctx.createPanel("Background", { 50.0f, 50.0f, 200.0f, 200.0f });
		background->add<lvgui::Checkbox>("BG 1", false);
		background->add<lvgui::Checkbox>("BG 2", false);
		auto* modal = ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f }, lvgui::PanelFlags::Modal);
		auto* m1 = modal->add<lvgui::Checkbox>("Modal 1", false);
		auto* m2 = modal->add<lvgui::Checkbox>("Modal 2", false);
		step(ctx);

		for (int i = 0; i < 6; ++i) {
			pressKey(ctx, lvgui::Key::Tab);
			lvgui::WidgetId focused = ctx.focusedId();
			assert(focused == m1->id() || focused == m2->id());
		}

		std::cout << "✓ testTabCyclingStaysWithinModalWidgets\n";
	}

	void testEscapeClosesClosableModal() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* modal = ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f },
		                               lvgui::PanelFlags::Modal | lvgui::PanelFlags::Closable);
		bool closed = false;
		modal->setOnClose([&] { closed = true; });
		step(ctx);

		pressKey(ctx, lvgui::Key::Escape);

		assert(!modal->visible());
		assert(closed);
		assert(ctx.activeModalPanel() == nullptr);

		std::cout << "✓ testEscapeClosesClosableModal\n";
	}

	void testEscapeDoesNotCloseNonClosableModal() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* modal = ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f }, lvgui::PanelFlags::Modal);
		bool closed = false;
		modal->setOnClose([&] { closed = true; });   // never fires -- Modal alone, no Closable
		step(ctx);

		pressKey(ctx, lvgui::Key::Escape);

		assert(modal->visible());
		assert(!closed);
		assert(ctx.activeModalPanel() == modal);

		std::cout << "✓ testEscapeDoesNotCloseNonClosableModal\n";
	}

	void testClosingModalRestoresBackgroundInteractivity() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* background = ctx.createPanel("Background", { 50.0f, 50.0f, 200.0f, 200.0f });
		auto* bgCheckbox = background->add<lvgui::Checkbox>("Enabled", false);
		auto* modal = ctx.createPanel("Dialog", { 300.0f, 200.0f, 200.0f, 150.0f },
		                               lvgui::PanelFlags::Modal | lvgui::PanelFlags::Closable);
		step(ctx);

		clickAt(ctx, centre(bgCheckbox));
		assert(!bgCheckbox->value());   // still blocked

		modal->requestClose();
		step(ctx);
		assert(ctx.activeModalPanel() == nullptr);

		clickAt(ctx, centre(bgCheckbox));
		assert(bgCheckbox->value());   // interactivity restored

		std::cout << "✓ testClosingModalRestoresBackgroundInteractivity\n";
	}

	void testNestedModalOnlyTheNewerOneIsInteractive() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* first = ctx.createPanel("First dialog", { 100.0f, 100.0f, 200.0f, 150.0f }, lvgui::PanelFlags::Modal);
		auto* firstCheckbox = first->add<lvgui::Checkbox>("First", false);
		step(ctx);
		assert(ctx.activeModalPanel() == first);

		// A confirmation opened from inside the first dialog -- createPanel() inserts it
		// frontmost, so it becomes the new active modal (docs/gui/05-widgets.md, "Panel",
		// "Modal panels": "the newest one blocks the older one too").
		auto* second = ctx.createPanel("Confirm", { 320.0f, 220.0f, 160.0f, 100.0f }, lvgui::PanelFlags::Modal);
		auto* secondCheckbox = second->add<lvgui::Checkbox>("Second", false);
		step(ctx);
		assert(ctx.activeModalPanel() == second);

		clickAt(ctx, centre(firstCheckbox));
		assert(!firstCheckbox->value());   // blocked by the second (newer) modal

		clickAt(ctx, centre(secondCheckbox));
		assert(secondCheckbox->value());   // the active one still works

		std::cout << "✓ testNestedModalOnlyTheNewerOneIsInteractive\n";
	}

	void testPopupFromAWidgetInsideTheModalStillWorks() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* modal = ctx.createPanel("Dialog", { 200.0f, 150.0f, 260.0f, 200.0f }, lvgui::PanelFlags::Modal);
		std::vector<std::string> items = { "Alpha", "Beta", "Gamma" };
		auto* dropdown = modal->add<lvgui::DropDown>("Pick", items, 0);
		step(ctx);

		clickAt(ctx, centre(dropdown));
		assert(ctx.isPopupOpen());
		assert(ctx.popupOwner() == dropdown);

		std::cout << "✓ testPopupFromAWidgetInsideTheModalStillWorks\n";
	}

}

int main() {
	testNoModalMeansActiveModalPanelIsNull();
	testActiveModalPanelIgnoresInvisibleModal();
	testActiveModalPanelReturnsTheOpenModal();
	testBackgroundWidgetDoesNotReceiveClicksWhileModalOpen();
	testModalsOwnWidgetStillRespondsWhileOpen();
	testWantsMouseIsTrueEvenFarFromTheModal();
	testWantsKeyboardIsTrueWhileModalOpenEvenWithNoFocus();
	testWantsScrollIsTrueWhileModalOpenEvenOverNothing();
	testNoneOfTheWantsAreForcedWithoutAModal();
	testTabCyclingStaysWithinModalWidgets();
	testEscapeClosesClosableModal();
	testEscapeDoesNotCloseNonClosableModal();
	testClosingModalRestoresBackgroundInteractivity();
	testNestedModalOnlyTheNewerOneIsInteractive();
	testPopupFromAWidgetInsideTheModalStillWorks();

	std::cout << "\n✅ All modal panel tests passed!\n";
	return 0;
}
