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

// AboutBox -- headless, same pattern as test_savefiledialog.cpp: no Vulkan device, no
// window. Checks it starts hidden, opens centred, lays out its contents inside the
// panel, grows with its text, and closes via OK / Escape with onClose firing once.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <cmath>
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

	void fill(lvgui::AboutBox& box, bool withImage) {
		box.setName("Test App");
		box.setVersion("Version 1.0");
		box.setDescription("A fairly long description that has to wrap onto several lines "
		                   "inside a narrow box so the height estimate is exercised.\n\nSecond paragraph.");
		box.setFooter("Copyright (C) 2026 Someone. Licensed under the LGPL-3.0-or-later.");
		if (withImage) {
			box.setImage(7, { 96.0f, 64.0f });
		}
	}

	void testStartsHiddenAndOpensCentred() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		step(ctx);
		lvgui::AboutBox box(ctx);
		fill(box, true);
		assert(!box.isOpen());
		box.open();
		step(ctx);
		assert(box.isOpen());
		lvgui::Rect b = box.panel()->bounds();
		assert(std::abs((b.x + b.w * 0.5f) - 400.0f) < 1.5f);
		assert(std::abs((b.y + b.h * 0.5f) - 300.0f) < 1.5f);
	}

	void testContentFitsInsidePanelAndImageIsCentred() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		step(ctx);
		lvgui::AboutBox box(ctx);
		fill(box, true);
		box.open();
		step(ctx);
		lvgui::Panel* p = box.panel();
		lvgui::Rect pb = p->bounds();
		for (std::size_t i = 0; i < p->widgetCount(); ++i) {
			lvgui::Rect wb = p->widgetAt(i)->bounds();
			assert(wb.y >= pb.y - 0.5f);
			assert(wb.y + wb.h <= pb.y + pb.h + 0.5f);
		}
		bool foundImage = false;
		for (std::size_t i = 0; i < p->widgetCount(); ++i) {
			if (auto* image = dynamic_cast<lvgui::Image*>(p->widgetAt(i))) {
				foundImage = true;
				lvgui::Rect ib = image->bounds();
				assert(std::abs((ib.x + ib.w * 0.5f) - (pb.x + pb.w * 0.5f)) < 1.5f);
				assert(image->textureId() == 7);
			}
		}
		assert(foundImage);
	}

	void testHeightGrowsWithTextAndShrinksWithoutImage() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		step(ctx);
		lvgui::AboutBox box(ctx);
		box.setName("A");
		float const small = box.panel()->bounds().h;
		fill(box, true);
		float const withImage = box.panel()->bounds().h;
		assert(withImage > small + 64.0f);
		box.clearImage();
		assert(box.panel()->bounds().h < withImage - 60.0f);
	}

	void testOkClosesAndFiresOnCloseOnce() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		step(ctx);
		lvgui::AboutBox box(ctx);
		fill(box, false);
		int closed = 0;
		box.setOnClose([&] { ++closed; });
		box.open();
		step(ctx);
		lvgui::Button* ok = box.okButton();
		assert(ok);
		lvgui::Rect r = ok->bounds();
		clickAt(ctx, { r.x + r.w * 0.5f, r.y + r.h * 0.5f });
		assert(!box.isOpen());
		assert(closed == 1);
	}

	void testEscapeClosesAndCloseIsSilent() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		step(ctx);
		lvgui::AboutBox box(ctx);
		fill(box, false);
		int closed = 0;
		box.setOnClose([&] { ++closed; });
		box.open();
		step(ctx);
		ctx.injectKey(lvgui::Key::Escape, 0, true, false);
		step(ctx);
		ctx.injectKey(lvgui::Key::Escape, 0, false, false);
		step(ctx);
		assert(!box.isOpen());
		assert(closed == 1);
		box.open();
		box.close();
		assert(!box.isOpen());
		assert(closed == 1); // programmatic close() does not notify
	}

	void testRebuildWhileOpenKeepsItOpenAndCentred() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		step(ctx);
		lvgui::AboutBox box(ctx);
		fill(box, false);
		box.open();
		step(ctx);
		box.setImage(3, { 128.0f, 128.0f });
		step(ctx);
		assert(box.isOpen());
		lvgui::Rect b = box.panel()->bounds();
		assert(std::abs((b.y + b.h * 0.5f) - 300.0f) < 1.5f);
	}
}

int main() {
	testStartsHiddenAndOpensCentred();
	testContentFitsInsidePanelAndImageIsCentred();
	testHeightGrowsWithTextAndShrinksWithoutImage();
	testOkClosesAndFiresOnCloseOnce();
	testEscapeClosesAndCloseIsSilent();
	testRebuildWhileOpenKeepsItOpenAndCentred();
	std::cout << "\n✅ All AboutBox tests passed!\n";
	return 0;
}
