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

// docs/gui/05-widgets.md, "ListBox" -- headless, same pattern as test_dropdown.cpp: no
// Vulkan device, no window.

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

	void pressKey(lvgui::GuiContext& ctx, int key, int mods = 0) {
		ctx.injectKey(key, mods, true, false);
		step(ctx);
		ctx.injectKey(key, mods, false, false);
		step(ctx);
	}

	std::vector<std::string> fiveItems() {
		return { "Alpha", "Beta", "Gamma", "Delta", "Epsilon" };
	}

	// Row `row` (0-based, within the visible window) of `lb`'s own bounds -- valid once
	// `lb` has had a layout() pass (step()), since bounds() only holds real geometry
	// after that.
	lvgui::Vec2 rowCentre(const lvgui::GuiContext& ctx, lvgui::ListBox* lb, int row) {
		const lvgui::Theme& th = ctx.theme();
		lvgui::Rect b = lb->bounds();
		float y = b.y + th.framePadding + (static_cast<float>(row) + 0.5f) * th.rowHeight;
		return { b.x + b.w * 0.5f, y };
	}

	void testInitialIndexBelowZeroMeansNoSelection() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* lb = panel->add<lvgui::ListBox>("", fiveItems(), -1);
		step(ctx);

		assert(lb->selectedIndex() < 0);
		assert(lb->selectedText().empty());

		std::cout << "✓ testInitialIndexBelowZeroMeansNoSelection\n";
	}

	void testClickingARowSelectsItOnReleaseInside() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* lb = panel->add<lvgui::ListBox>("", fiveItems());
		step(ctx);

		clickAt(ctx, rowCentre(ctx, lb, 2));
		assert(lb->selectedIndex() == 2);
		assert(lb->selectedText() == "Gamma");

		std::cout << "✓ testClickingARowSelectsItOnReleaseInside\n";
	}

	void testPressThenDragOutsideCancelsSelection() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* lb = panel->add<lvgui::ListBox>("", fiveItems(), 0);
		step(ctx);

		lvgui::Vec2 row3 = rowCentre(ctx, lb, 3);
		ctx.injectMousePos(row3);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		// Drag well outside the list before releasing -- release-inside only, same
		// convention DropDown's popup rows and Button both use.
		ctx.injectMousePos({ -500.0f, -500.0f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		assert(lb->selectedIndex() == 0);   // unchanged from construction

		std::cout << "✓ testPressThenDragOutsideCancelsSelection\n";
	}

	void testArrowKeysMoveSelectionAndWrapBothWays() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* lb = panel->add<lvgui::ListBox>("", fiveItems(), 0);
		step(ctx);
		ctx.setFocus(lb);
		step(ctx);

		pressKey(ctx, lvgui::Key::Down);
		assert(lb->selectedIndex() == 1);

		pressKey(ctx, lvgui::Key::Up);
		assert(lb->selectedIndex() == 0);

		// Wraps at the low end.
		pressKey(ctx, lvgui::Key::Up);
		assert(lb->selectedIndex() == 4);

		// Wraps at the high end.
		pressKey(ctx, lvgui::Key::Down);
		assert(lb->selectedIndex() == 0);

		std::cout << "✓ testArrowKeysMoveSelectionAndWrapBothWays\n";
	}

	void testArrowKeyFromNoSelectionLandsOnFirstItem() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* lb = panel->add<lvgui::ListBox>("", fiveItems(), -1);
		step(ctx);
		ctx.setFocus(lb);
		step(ctx);

		pressKey(ctx, lvgui::Key::Up);   // either direction, from no selection
		assert(lb->selectedIndex() == 0);

		std::cout << "✓ testArrowKeyFromNoSelectionLandsOnFirstItem\n";
	}

	void testArrowKeyNavigationAutoScrollsIntoView() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		std::vector<std::string> items;
		for (int i = 0; i < 20; ++i) {
			items.push_back("Item " + std::to_string(i));
		}
		auto* lb = panel->add<lvgui::ListBox>("", items, 0);
		lb->setVisibleRows(6);
		step(ctx);
		ctx.setFocus(lb);
		step(ctx);

		for (int i = 0; i < 10; ++i) {
			pressKey(ctx, lvgui::Key::Down);
		}
		assert(lb->selectedIndex() == 10);

		// Item 10 is well past the initial 6-row window (items 0-5) -- scrollToShowSelected()
		// must have moved scrollRow to keep it visible, landing the selection on the
		// window's LAST row (docs/gui/05-widgets.md, "ListBox": "scroll the minimum
		// needed to keep the selection in view", the same rule DropDown's popup
		// highlight-scrolling already follows). scrollRow itself isn't public API
		// (neither is DropDown's own m_scrollRow); what's observable through the public
		// surface is WHICH item a click at a given row now resolves to.
		clickAt(ctx, rowCentre(ctx, lb, 5));   // bottom row of the window
		assert(lb->selectedIndex() == 10);
		clickAt(ctx, rowCentre(ctx, lb, 0));   // top row of the window
		assert(lb->selectedIndex() == 5);

		std::cout << "✓ testArrowKeyNavigationAutoScrollsIntoView\n";
	}

	void testSetSelectedIndexDoesNotAutoScroll() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		std::vector<std::string> items;
		for (int i = 0; i < 20; ++i) {
			items.push_back("Item " + std::to_string(i));
		}
		auto* lb = panel->add<lvgui::ListBox>("", items, 0);
		lb->setVisibleRows(6);
		step(ctx);

		// Programmatic selection -- unlike arrow-key navigation, this does NOT scroll
		// the view (matches DropDown::setSelectedIndex(), which is likewise silent about
		// the popup's own scroll position). Row 0 of the still-unscrolled view is still
		// "Item 0", not "Item 15".
		lb->setSelectedIndex(15, true);
		clickAt(ctx, rowCentre(ctx, lb, 0));
		assert(lb->selectedIndex() == 0);

		std::cout << "✓ testSetSelectedIndexDoesNotAutoScroll\n";
	}

	void testWheelScrollsAndClampsAtBothEnds() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		std::vector<std::string> items;
		for (int i = 0; i < 10; ++i) {
			items.push_back("Item " + std::to_string(i));
		}
		auto* lb = panel->add<lvgui::ListBox>("", items, -1);
		lb->setVisibleRows(4);
		step(ctx);

		// A wheel tick "up" at the top must not go negative -- click row 0 both before
		// and after scrolling up; if scrollRow had clamped below 0, row 0 would now be
		// showing something other than "Item 0".
		ctx.injectMousePos(rowCentre(ctx, lb, 0));
		ctx.injectScroll(1.0f);
		step(ctx);
		clickAt(ctx, rowCentre(ctx, lb, 0));
		assert(lb->selectedText() == "Item 0");

		// Scroll down past the end and confirm it clamps rather than running off the
		// list: 10 items, 4 visible -> max scroll is 6 rows, so 20 ticks (60 rows worth)
		// must land on exactly the last window, whose row 3 is "Item 9".
		for (int i = 0; i < 20; ++i) {
			ctx.injectScroll(-1.0f);
			step(ctx);
		}
		clickAt(ctx, rowCentre(ctx, lb, 3));
		assert(lb->selectedText() == "Item 9");

		std::cout << "✓ testWheelScrollsAndClampsAtBothEnds\n";
	}

	void testSetItemsClampsSelectionAndResetsScroll() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* lb = panel->add<lvgui::ListBox>("", fiveItems(), 4);   // last item selected
		step(ctx);
		assert(lb->selectedIndex() == 4);

		lb->setItems({ "Only" });
		assert(lb->selectedIndex() == 0);   // clamped down, not left dangling at 4
		assert(lb->selectedText() == "Only");

		lb->setItems({});
		assert(lb->selectedIndex() < 0);   // no items -> no selection, not an OOB index

		std::cout << "✓ testSetItemsClampsSelectionAndResetsScroll\n";
	}

	void testEmptyListArrowKeysAreNoop() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* lb = panel->add<lvgui::ListBox>("", std::vector<std::string>{}, -1);
		step(ctx);
		ctx.setFocus(lb);
		step(ctx);

		pressKey(ctx, lvgui::Key::Down);
		pressKey(ctx, lvgui::Key::Up);

		assert(lb->selectedIndex() < 0);

		std::cout << "✓ testEmptyListArrowKeysAreNoop\n";
	}

	void testOnChangeFiresOnClickSelectionButNotOnConstruction() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* lb = panel->add<lvgui::ListBox>("", fiveItems(), 1);
		int fireCount = 0;
		int lastValue = -99;
		lb->setOnChange([&](int idx) { ++fireCount; lastValue = idx; });
		step(ctx);

		assert(fireCount == 0);   // constructing with an initial index doesn't fire

		clickAt(ctx, rowCentre(ctx, lb, 3));
		assert(fireCount == 1);
		assert(lastValue == 3);

		std::cout << "✓ testOnChangeFiresOnClickSelectionButNotOnConstruction\n";
	}

}

int main() {
	testInitialIndexBelowZeroMeansNoSelection();
	testClickingARowSelectsItOnReleaseInside();
	testPressThenDragOutsideCancelsSelection();
	testArrowKeysMoveSelectionAndWrapBothWays();
	testArrowKeyFromNoSelectionLandsOnFirstItem();
	testArrowKeyNavigationAutoScrollsIntoView();
	testSetSelectedIndexDoesNotAutoScroll();
	testWheelScrollsAndClampsAtBothEnds();
	testSetItemsClampsSelectionAndResetsScroll();
	testEmptyListArrowKeysAreNoop();
	testOnChangeFiresOnClickSelectionButNotOnConstruction();

	std::cout << "\n✅ All ListBox tests passed!\n";
	return 0;
}
