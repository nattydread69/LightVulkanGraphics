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

// docs/gui/05-widgets.md, "LogView" -- headless, same pattern as test_listbox.cpp/
// test_panel.cpp: no Vulkan device, no window.

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

	void pressKey(lvgui::GuiContext& ctx, int key, int mods = 0) {
		ctx.injectKey(key, mods, true, false);
		step(ctx);
		ctx.injectKey(key, mods, false, false);
		step(ctx);
	}

	void pushN(lvgui::LogView* log, int n, int startAt = 0) {
		for (int i = 0; i < n; ++i) {
			log->push("line " + std::to_string(startAt + i));
		}
	}

	void testStartsFollowingBottomWithNoLines() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		step(ctx);

		assert(log->isFollowingBottom());
		assert(log->lineCount() == 0);

		std::cout << "✓ testStartsFollowingBottomWithNoLines\n";
	}

	void testPushWhileFollowingBottomStaysFollowing() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		step(ctx);

		pushN(log, 40);   // comfortably more than the default view can show at once
		step(ctx);

		assert(log->isFollowingBottom());
		assert(log->lineCount() == 40);

		std::cout << "✓ testPushWhileFollowingBottomStaysFollowing\n";
	}

	void testWheelUpStopsFollowingBottom() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		pushN(log, 40);
		step(ctx);
		assert(log->isFollowingBottom());

		lvgui::Rect b = log->bounds();
		ctx.injectMousePos({ b.x + b.w * 0.5f, b.y + b.h * 0.5f });
		ctx.injectScroll(1.0f);   // "away from user" -- scrolls UP toward older content
		step(ctx);

		assert(!log->isFollowingBottom());

		std::cout << "✓ testWheelUpStopsFollowingBottom\n";
	}

	void testPushWhileScrolledAwayDoesNotResumeFollowing() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		pushN(log, 40);
		step(ctx);

		lvgui::Rect b = log->bounds();
		ctx.injectMousePos({ b.x + b.w * 0.5f, b.y + b.h * 0.5f });
		ctx.injectScroll(1.0f);
		step(ctx);
		assert(!log->isFollowingBottom());

		// docs/gui/05-widgets.md, "LogView": new content must not yank a reader's view
		// back to the bottom out from under them.
		pushN(log, 10, 40);
		step(ctx);

		assert(!log->isFollowingBottom());
		assert(log->lineCount() == 50);

		std::cout << "✓ testPushWhileScrolledAwayDoesNotResumeFollowing\n";
	}

	void testEndKeyReturnsToBottomAndResumesFollowing() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		pushN(log, 40);
		step(ctx);
		ctx.setFocus(log);
		step(ctx);

		pressKey(ctx, lvgui::Key::Home);
		assert(!log->isFollowingBottom());

		pressKey(ctx, lvgui::Key::End);
		assert(log->isFollowingBottom());

		std::cout << "✓ testEndKeyReturnsToBottomAndResumesFollowing\n";
	}

	void testHomeKeyStopsFollowingBottom() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		pushN(log, 40);
		step(ctx);
		ctx.setFocus(log);
		step(ctx);
		assert(log->isFollowingBottom());

		pressKey(ctx, lvgui::Key::Home);
		assert(!log->isFollowingBottom());

		std::cout << "✓ testHomeKeyStopsFollowingBottom\n";
	}

	void testPageUpThenRepeatedPageDownReachesBottomAgain() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		pushN(log, 60);
		step(ctx);
		ctx.setFocus(log);
		step(ctx);

		pressKey(ctx, lvgui::Key::PageUp);
		assert(!log->isFollowingBottom());

		// However many rows one PageUp moved, enough PageDowns must land back at the
		// bottom and flip isFollowingBottom() true again -- clamped, not overshootable.
		for (int i = 0; i < 20; ++i) {
			pressKey(ctx, lvgui::Key::PageDown);
		}
		assert(log->isFollowingBottom());

		std::cout << "✓ testPageUpThenRepeatedPageDownReachesBottomAgain\n";
	}

	void testMaxLinesDefersEvictionWhileScrolledAwayThenTrimsOnReturn() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		log->setMaxLines(20);
		pushN(log, 20);
		step(ctx);
		assert(log->lineCount() == 20);

		ctx.setFocus(log);
		step(ctx);
		pressKey(ctx, lvgui::Key::Home);   // stop following the bottom
		assert(!log->isFollowingBottom());

		// Push well past the cap while scrolled away -- docs/gui/05-widgets.md,
		// "LogView": eviction is deferred while not following the bottom, so this must
		// NOT trim yet.
		pushN(log, 15, 20);
		step(ctx);
		assert(log->lineCount() == 35);

		// Returning to the bottom applies the deferred trim down to the cap.
		pressKey(ctx, lvgui::Key::End);
		assert(log->isFollowingBottom());
		assert(log->lineCount() == 20);

		std::cout << "✓ testMaxLinesDefersEvictionWhileScrolledAwayThenTrimsOnReturn\n";
	}

	void testMaxLinesTrimsImmediatelyWhileFollowingBottom() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		log->setMaxLines(10);
		step(ctx);

		pushN(log, 25);   // never leaves "following the bottom" this whole time
		step(ctx);

		assert(log->isFollowingBottom());
		assert(log->lineCount() == 10);

		std::cout << "✓ testMaxLinesTrimsImmediatelyWhileFollowingBottom\n";
	}

	void testZeroMaxLinesIsUnbounded() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		log->setMaxLines(0);
		pushN(log, 1000);
		step(ctx);

		assert(log->lineCount() == 1000);

		std::cout << "✓ testZeroMaxLinesIsUnbounded\n";
	}

	void testClearResetsLineCountAndFollowBottom() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		pushN(log, 40);
		step(ctx);
		ctx.setFocus(log);
		step(ctx);
		pressKey(ctx, lvgui::Key::Home);
		assert(!log->isFollowingBottom());

		log->clear();
		step(ctx);

		assert(log->lineCount() == 0);
		assert(log->isFollowingBottom());

		std::cout << "✓ testClearResetsLineCountAndFollowBottom\n";
	}

	void testVeryLongLineWithWordWrapDoesNotThrowOrCrash() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 50.0f, 50.0f, 220.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		assert(log->isFollowingBottom());   // setWordWrap(true) is the default

		std::string longLine;
		for (int i = 0; i < 40; ++i) {
			longLine += "a very long word-wrapped diagnostic message segment ";
		}
		log->push(longLine);
		step(ctx);
		step(ctx);   // a second pass, same reason Label needs one for wrapped height

		assert(log->lineCount() == 1);   // one RAW line, however many visual rows it wraps to

		std::cout << "✓ testVeryLongLineWithWordWrapDoesNotThrowOrCrash\n";
	}

}

int main() {
	testStartsFollowingBottomWithNoLines();
	testPushWhileFollowingBottomStaysFollowing();
	testWheelUpStopsFollowingBottom();
	testPushWhileScrolledAwayDoesNotResumeFollowing();
	testEndKeyReturnsToBottomAndResumesFollowing();
	testHomeKeyStopsFollowingBottom();
	testPageUpThenRepeatedPageDownReachesBottomAgain();
	testMaxLinesDefersEvictionWhileScrolledAwayThenTrimsOnReturn();
	testMaxLinesTrimsImmediatelyWhileFollowingBottom();
	testZeroMaxLinesIsUnbounded();
	testClearResetsLineCountAndFollowBottom();
	testVeryLongLineWithWordWrapDoesNotThrowOrCrash();

	std::cout << "\n✅ All LogView tests passed!\n";
	return 0;
}
