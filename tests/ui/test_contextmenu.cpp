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

// docs/gui/05-widgets.md, "ContextMenu" -- headless, same pattern as test_dropdown.cpp:
// no Vulkan device, no window.

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

	void clickAt(lvgui::GuiContext& ctx, lvgui::Vec2 pos) {
		ctx.injectMousePos(pos);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
	}

	// Row `index` (0-based, no separators involved) of the CURRENTLY open menu, read
	// back from ctx.popupRect() -- the same public accessor DropDown's own popup
	// geometry would be inspected through, so this needs no access to ContextMenu's
	// private item list.
	lvgui::Vec2 rowCentre(const lvgui::GuiContext& ctx, int index) {
		const lvgui::Theme& th = ctx.theme();
		lvgui::Rect menu = ctx.popupRect();
		float y = menu.y + th.framePadding + (static_cast<float>(index) + 0.5f) * th.rowHeight;
		return { menu.x + menu.w * 0.5f, y };
	}

	void testMenuIsClosedInitially() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* menu = panel->add<lvgui::ContextMenu>();
		step(ctx);

		assert(!menu->isOpen(ctx));
		assert(!ctx.isPopupOpen());

		std::cout << "✓ testMenuIsClosedInitially\n";
	}

	void testOpenMakesItTheCurrentPopup() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* menu = panel->add<lvgui::ContextMenu>();
		menu->addItem("Reset", [] {});
		step(ctx);

		menu->open({ 100.0f, 100.0f });
		step(ctx);

		assert(menu->isOpen(ctx));
		assert(ctx.popupOwner() == menu);

		std::cout << "✓ testOpenMakesItTheCurrentPopup\n";
	}

	void testMenuOpensAtRequestedPositionWellWithinScreen() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* menu = panel->add<lvgui::ContextMenu>();
		menu->addItem("A", [] {});
		menu->addItem("B", [] {});
		step(ctx);

		menu->open({ 120.0f, 90.0f });
		step(ctx);

		lvgui::Rect r = ctx.popupRect();
		assert(r.x == 120.0f);
		assert(r.y == 90.0f);
		// Bottom-right stays comfortably inside the 800x600 test display -- nothing to
		// clamp for a point this far from any edge.
		assert(r.right() < 800.0f);
		assert(r.bottom() < 600.0f);

		std::cout << "✓ testMenuOpensAtRequestedPositionWellWithinScreen\n";
	}

	void testMenuClampsToStayOnScreenNearBottomRightCorner() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* menu = panel->add<lvgui::ContextMenu>();
		menu->addItem("A reasonably long menu item label", [] {});
		menu->addItem("B", [] {});
		menu->addItem("C", [] {});
		step(ctx);

		// Right at the display's bottom-right corner -- both DropDown-style vertical
		// flipping AND horizontal clamping are needed here, unlike DropDown's popup,
		// which only ever flips vertically (docs/gui/05, "ContextMenu").
		menu->open({ 790.0f, 590.0f });
		step(ctx);

		lvgui::Rect r = ctx.popupRect();
		assert(r.right() <= 800.0f + 0.01f);
		assert(r.bottom() <= 600.0f + 0.01f);
		assert(r.x >= 0.0f);
		assert(r.y >= 0.0f);

		std::cout << "✓ testMenuClampsToStayOnScreenNearBottomRightCorner\n";
	}

	void testClickingAnItemFiresOnSelectAndCloses() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* menu = panel->add<lvgui::ContextMenu>();
		int fired = -1;
		menu->addItem("Alpha", [&] { fired = 0; });
		menu->addItem("Beta", [&] { fired = 1; });
		step(ctx);

		menu->open({ 100.0f, 100.0f });
		step(ctx);

		clickAt(ctx, rowCentre(ctx, 1));

		assert(fired == 1);
		assert(!menu->isOpen(ctx));

		std::cout << "✓ testClickingAnItemFiresOnSelectAndCloses\n";
	}

	void testSeparatorRowIsNotSelectableButStillClosesTheMenu() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* menu = panel->add<lvgui::ContextMenu>();
		bool fired = false;
		menu->addItem("Above", [&] { fired = true; });
		menu->addSeparator();
		menu->addItem("Below", [&] { fired = true; });
		step(ctx);

		menu->open({ 100.0f, 100.0f });
		step(ctx);

		const lvgui::Theme& th = ctx.theme();
		lvgui::Rect r = ctx.popupRect();
		// The separator sits between the two rowHeight-tall items, itemSpacing tall.
		lvgui::Vec2 separatorPoint{ r.x + r.w * 0.5f, r.y + th.framePadding + th.rowHeight + th.itemSpacing * 0.5f };

		clickAt(ctx, separatorPoint);

		// docs/gui/05-widgets.md, "ContextMenu": clicking anywhere inside the menu closes
		// it, even a non-actionable row -- common context-menu behaviour, not a bug.
		assert(!fired);
		assert(!menu->isOpen(ctx));

		std::cout << "✓ testSeparatorRowIsNotSelectableButStillClosesTheMenu\n";
	}

	void testClickOutsideClosesWithoutSelecting() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* menu = panel->add<lvgui::ContextMenu>();
		bool fired = false;
		menu->addItem("Only item", [&] { fired = true; });
		step(ctx);

		menu->open({ 100.0f, 100.0f });
		step(ctx);
		assert(menu->isOpen(ctx));

		clickAt(ctx, { 700.0f, 550.0f });   // far outside both the panel and the menu

		assert(!fired);
		assert(!menu->isOpen(ctx));

		std::cout << "✓ testClickOutsideClosesWithoutSelecting\n";
	}

	void testEscapeClosesTheMenu() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* menu = panel->add<lvgui::ContextMenu>();
		menu->addItem("Item", [] {});
		step(ctx);

		menu->open({ 100.0f, 100.0f });
		step(ctx);
		assert(menu->isOpen(ctx));

		ctx.injectKey(lvgui::Key::Escape, 0, true, false);
		step(ctx);
		ctx.injectKey(lvgui::Key::Escape, 0, false, false);
		step(ctx);

		assert(!menu->isOpen(ctx));

		std::cout << "✓ testEscapeClosesTheMenu\n";
	}

	void testReopeningWhileOpenRepositions() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* menu = panel->add<lvgui::ContextMenu>();
		menu->addItem("Item", [] {});
		step(ctx);

		menu->open({ 100.0f, 100.0f });
		step(ctx);
		assert(ctx.popupRect().x == 100.0f);

		menu->open({ 250.0f, 180.0f });
		step(ctx);
		assert(menu->isOpen(ctx));
		assert(ctx.popupRect().x == 250.0f);
		assert(ctx.popupRect().y == 180.0f);

		std::cout << "✓ testReopeningWhileOpenRepositions\n";
	}

}

int main() {
	testMenuIsClosedInitially();
	testOpenMakesItTheCurrentPopup();
	testMenuOpensAtRequestedPositionWellWithinScreen();
	testMenuClampsToStayOnScreenNearBottomRightCorner();
	testClickingAnItemFiresOnSelectAndCloses();
	testSeparatorRowIsNotSelectableButStillClosesTheMenu();
	testClickOutsideClosesWithoutSelecting();
	testEscapeClosesTheMenu();
	testReopeningWhileOpenRepositions();

	std::cout << "\n✅ All ContextMenu tests passed!\n";
	return 0;
}
