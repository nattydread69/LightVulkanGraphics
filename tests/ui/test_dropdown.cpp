// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2026 Dr. Nathanael John Inkson
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// docs/gui/05-widgets.md, "DropDown" -- headless, same pattern as test_hittest.cpp/
// test_slider.cpp/test_textedit.cpp: no Vulkan device, no window.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

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

	// Matches test_textedit.cpp's pressKey helper: a real press-then-release pair
	// across two frames, not a single synthetic edge.
	void pressKey(lvgui::GuiContext& ctx, int key, int mods = 0) {
		ctx.injectKey(key, mods, true, false);
		step(ctx);
		ctx.injectKey(key, mods, false, false);
		step(ctx);
	}

	std::vector<std::string> threeItems() {
		return { "Alpha", "Beta", "Gamma" };
	}

	// Opens dd's popup via a control click and returns the control's centre (so callers
	// don't need to recompute it for a second click).
	lvgui::Vec2 openViaControlClick(lvgui::GuiContext& ctx, lvgui::DropDown* dd) {
		lvgui::Rect b = dd->bounds();
		lvgui::Vec2 mid{ b.x + b.w * 0.5f, b.y + b.h * 0.5f };
		clickAt(ctx, mid);
		return mid;
	}

	void testPopupOpensOnClickAndClosesOnSecondClickOfControl() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 300.0f });
		auto* dd = panel->add<lvgui::DropDown>("", threeItems());

		step(ctx);
		assert(!ctx.isPopupOpen());

		lvgui::Vec2 mid = openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());
		assert(ctx.popupOwner() == dd);

		clickAt(ctx, mid);
		assert(!ctx.isPopupOpen());

		std::cout << "✓ testPopupOpensOnClickAndClosesOnSecondClickOfControl\n";
	}

	void testClickOutsideClosesWithoutChangingSelection() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 300.0f });
		auto* dd = panel->add<lvgui::DropDown>("", threeItems(), 0);

		step(ctx);
		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());

		clickAt(ctx, { 900.0f, 900.0f });   // well outside both the control and the popup
		assert(!ctx.isPopupOpen());
		assert(dd->selectedIndex() == 0);

		std::cout << "✓ testClickOutsideClosesWithoutChangingSelection\n";
	}

	void testEscapeClosesWithoutChangingSelectionButEnterOnMovedHighlightDoes() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 300.0f });
		auto* dd = panel->add<lvgui::DropDown>("", threeItems(), 0);

		step(ctx);
		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());
		assert(ctx.focusedId() == dd->id());

		// Moving the highlight alone must not touch the selection.
		pressKey(ctx, lvgui::Key::Down);
		assert(dd->selectedIndex() == 0);

		pressKey(ctx, lvgui::Key::Escape);
		assert(!ctx.isPopupOpen());
		assert(dd->selectedIndex() == 0);
		// docs/gui/04-input-and-events.md, "Key events": Escape closes the popup but does not also kick
		// focus off the control (every native combo box behaves this way).
		assert(ctx.focusedId() == dd->id());

		// Reopen: the highlight must reset to the CURRENT selection, not stay wherever
		// the aborted Down-arrow left it.
		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());

		pressKey(ctx, lvgui::Key::Down);
		pressKey(ctx, lvgui::Key::Enter);
		assert(!ctx.isPopupOpen());
		assert(dd->selectedIndex() == 1);

		std::cout << "✓ testEscapeClosesWithoutChangingSelectionButEnterOnMovedHighlightDoes\n";
	}

	void testArrowKeysWrapAtBothEnds() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 300.0f });
		auto* dd = panel->add<lvgui::DropDown>("", threeItems(), 0);

		step(ctx);
		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());

		// Up from the first item wraps to the last.
		pressKey(ctx, lvgui::Key::Up);
		pressKey(ctx, lvgui::Key::Enter);
		assert(dd->selectedIndex() == 2);

		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());
		// Down from the last item (highlight reset to selection == 2) wraps to the first.
		pressKey(ctx, lvgui::Key::Down);
		pressKey(ctx, lvgui::Key::Enter);
		assert(dd->selectedIndex() == 0);

		std::cout << "✓ testArrowKeysWrapAtBothEnds\n";
	}

	void testEnterOrSpaceOpensAFocusedClosedControlForKeyboardOnlyUse() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 300.0f });
		auto* dd = panel->add<lvgui::DropDown>("", threeItems(), 0);

		step(ctx);
		// Tab to focus the control without ever touching the mouse.
		pressKey(ctx, lvgui::Key::Tab);
		assert(ctx.focusedId() == dd->id());
		assert(!ctx.isPopupOpen());

		pressKey(ctx, lvgui::Key::Enter);
		assert(ctx.isPopupOpen());

		// The SAME Enter keydown that opened it must not also be reprocessed as
		// "select the highlight and close" within the same frame.
		assert(dd->selectedIndex() == 0);

		pressKey(ctx, lvgui::Key::Down);
		pressKey(ctx, lvgui::Key::Enter);
		assert(!ctx.isPopupOpen());
		assert(dd->selectedIndex() == 1);

		// Space opens it too (closed-and-focused only -- Enter's role while ALREADY
		// open is "select the highlight", so Space is not overloaded as a second
		// close-toggle here; Escape/outside-click/control-click already cover closing).
		pressKey(ctx, lvgui::Key::Space);
		assert(ctx.isPopupOpen());
		pressKey(ctx, lvgui::Key::Escape);
		assert(!ctx.isPopupOpen());
		assert(dd->selectedIndex() == 1);

		std::cout << "✓ testEnterOrSpaceOpensAFocusedClosedControlForKeyboardOnlyUse\n";
	}

	void testWantsMouseTrueWhilePopupOpenEvenFarOutside() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 300.0f });
		auto* dd = panel->add<lvgui::DropDown>("", threeItems(), 0);

		step(ctx);
		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());

		// A bare hover move (no press) far outside the window must neither close the
		// popup nor stop wantsMouse() from claiming the mouse.
		ctx.injectMousePos({ -500.0f, -500.0f });
		step(ctx);
		assert(ctx.isPopupOpen());
		assert(ctx.wantsMouse());

		std::cout << "✓ testWantsMouseTrueWhilePopupOpenEvenFarOutside\n";
	}

	void testHitTestingReachesPopupOverAnOverlappingFrontPanel() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* owner = ctx.createPanel("Owner", { 50.0f, 50.0f, 200.0f, 100.0f });
		auto* dd = owner->add<lvgui::DropDown>("", threeItems(), 0);

		step(ctx);
		lvgui::Rect b = dd->bounds();

		// Created AFTER the owner, so it starts frontmost (docs/gui/01: "newest panel
		// starts frontmost") -- sized to fully cover where the popup will open below
		// the control.
		auto* front = ctx.createPanel("Front", { b.x - 10.0f, b.bottom() + 5.0f, b.w + 40.0f, 200.0f });
		step(ctx);
		assert(ctx.panelAt(0) == front);

		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());

		lvgui::Rect popupRect = ctx.popupRect();
		lvgui::Vec2 insidePopup{ popupRect.x + popupRect.w * 0.5f, popupRect.y + popupRect.h * 0.5f };
		// Sanity check: this point genuinely falls inside the front panel too, or this
		// test would not be exercising the ordering it claims to.
		assert(front->bounds().contains(insidePopup));

		ctx.injectMousePos(insidePopup);
		step(ctx);
		// This is the assertion that matters most (docs/gui/05): the popup wins over a
		// DIFFERENT, frontmost panel that happens to overlap it.
		assert(ctx.hoveredId() == dd->id());

		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		assert(ctx.activeId() == dd->id());
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		std::cout << "✓ testHitTestingReachesPopupOverAnOverlappingFrontPanel\n";
	}

	void testTwentyItemsScrollAndOffscreenItemsAreNotHitTestable() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 300.0f, 100.0f });
		std::vector<std::string> items;
		for (int i = 0; i < 20; ++i) {
			items.push_back("Item " + std::to_string(i));
		}
		auto* dd = panel->add<lvgui::DropDown>("", items, 0);

		step(ctx);
		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());

		const lvgui::Theme& th = ctx.theme();
		lvgui::Rect popupRect = ctx.popupRect();
		// 20 items > 12 visible -- the popup must be capped to 12 rows tall, not 20.
		float maxExpectedHeight = 12.0f * th.rowHeight + 2.0f * th.framePadding + 0.5f;
		assert(popupRect.h <= maxExpectedHeight);

		// With no scroll yet, clicking near the bottom of the (12-row) popup must land
		// on one of the first 12 items -- never one of the 8 that are off-screen.
		lvgui::Vec2 nearBottom{ popupRect.x + popupRect.w * 0.5f, popupRect.bottom() - 4.0f };
		clickAt(ctx, nearBottom);
		assert(dd->selectedIndex() >= 0 && dd->selectedIndex() < 12);

		// Reopen and push the highlight past the visible window to force a scroll.
		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());
		for (int i = 0; i < 15; ++i) {
			pressKey(ctx, lvgui::Key::Down);
		}

		popupRect = ctx.popupRect();   // the RECT is fixed; only its CONTENT scrolls
		lvgui::Vec2 nearTop{ popupRect.x + popupRect.w * 0.5f, popupRect.y + th.framePadding + 2.0f };
		clickAt(ctx, nearTop);
		// Item 0 has scrolled out of view -- the top row now resolves to whatever
		// scrolled into it, never back to item 0.
		assert(dd->selectedIndex() != 0);

		std::cout << "✓ testTwentyItemsScrollAndOffscreenItemsAreNotHitTestable\n";
	}

	void testClickingAnItemSelectsItAndClosesPopup() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 300.0f });
		auto* dd = panel->add<lvgui::DropDown>("", threeItems(), 0);
		int changedTo = -1;
		dd->setOnChange([&](int idx) { changedTo = idx; });

		step(ctx);
		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());

		const lvgui::Theme& th = ctx.theme();
		lvgui::Rect popupRect = ctx.popupRect();
		// Row index 2 == "Gamma".
		lvgui::Vec2 onGamma{ popupRect.x + popupRect.w * 0.5f,
		                      popupRect.y + th.framePadding + th.rowHeight * 2.5f };
		clickAt(ctx, onGamma);

		assert(!ctx.isPopupOpen());
		assert(dd->selectedIndex() == 2);
		assert(changedTo == 2);
		assert(dd->selectedText() == "Gamma");

		std::cout << "✓ testClickingAnItemSelectsItAndClosesPopup\n";
	}

	void testPopupFlipsAboveNearBottomEdge() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		// step() drives an 800x600 framebuffer; place the control hard against the
		// bottom edge so the default below-control placement cannot possibly fit.
		auto* panel = ctx.createPanel("Panel", { 50.0f, 560.0f, 200.0f, 32.0f }, lvgui::PanelFlags::NoTitleBar);
		auto* dd = panel->add<lvgui::DropDown>("", threeItems(), 0);

		step(ctx);
		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());

		lvgui::Rect b = dd->bounds();
		lvgui::Rect popupRect = ctx.popupRect();
		assert(popupRect.bottom() <= b.top() + 0.5f);

		std::cout << "✓ testPopupFlipsAboveNearBottomEdge\n";
	}

	void testDestroyingOwningPanelWhilePopupOpenDoesNotCrashAndClearsOwner() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 200.0f, 300.0f });
		auto* dd = panel->add<lvgui::DropDown>("", threeItems(), 0);

		step(ctx);
		openViaControlClick(ctx, dd);
		assert(ctx.isPopupOpen());

		ctx.destroyPanel(panel);   // destroys dd along with it

		// Self-heals on the very next query -- no dangling read required to trigger it.
		assert(ctx.popupOwner() == nullptr);
		assert(!ctx.isPopupOpen());

		// Must not crash: a full frame, including endFrame()'s popup-draw path, which
		// would previously have called drawPopup() on a dangling pointer.
		ctx.injectMousePos({ 900.0f, 900.0f });
		step(ctx);

		std::cout << "✓ testDestroyingOwningPanelWhilePopupOpenDoesNotCrashAndClearsOwner\n";
	}

}

int main() {
	testPopupOpensOnClickAndClosesOnSecondClickOfControl();
	testClickOutsideClosesWithoutChangingSelection();
	testEscapeClosesWithoutChangingSelectionButEnterOnMovedHighlightDoes();
	testArrowKeysWrapAtBothEnds();
	testEnterOrSpaceOpensAFocusedClosedControlForKeyboardOnlyUse();
	testWantsMouseTrueWhilePopupOpenEvenFarOutside();
	testHitTestingReachesPopupOverAnOverlappingFrontPanel();
	testTwentyItemsScrollAndOffscreenItemsAreNotHitTestable();
	testClickingAnItemSelectsItAndClosesPopup();
	testPopupFlipsAboveNearBottomEdge();
	testDestroyingOwningPanelWhilePopupOpenDoesNotCrashAndClearsOwner();

	std::cout << "\n✅ All DropDown tests passed!\n";
	return 0;
}
