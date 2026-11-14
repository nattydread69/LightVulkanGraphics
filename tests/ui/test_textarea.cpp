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

// TextArea tests: the editing operations headless, then wrap/scroll/keyboard behaviour
// through a real GuiContext (needs real Font metrics).

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

	void click(lvgui::GuiContext& ctx, lvgui::Vec2 p) {
		ctx.injectMousePos(p);
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
	}

	std::string g_clipboard;

	lvgui::PlatformHooks clipboardHooks() {
		lvgui::PlatformHooks h;
		h.getClipboardText = []() { return g_clipboard; };
		h.setClipboardText = [](std::string_view t) { g_clipboard.assign(t); };
		return h;
	}

	void testCtrlVPastesAndCtrlCCopies() {
		g_clipboard = "first line\r\nsecond line\n" + std::string(5000, 'x');
		lvgui::GuiContext ctx(testCreateInfo(), clipboardHooks());
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 400.0f, 300.0f });
		auto* area = panel->add<lvgui::TextArea>();
		step(ctx);
		lvgui::Rect b = area->bounds();
		click(ctx, { b.x + 10.0f, b.y + 10.0f });
		assert(ctx.focusedId() == area->id());

		pressKey(ctx, lvgui::Key::V, lvgui::Mod::Ctrl);
		assert(area->text() == "first line\nsecond line\n" + std::string(5000, 'x'));
		assert(area->lineCount() > 3);

		// Ctrl+A then Ctrl+C puts the (normalised) text back on the clipboard.
		g_clipboard.clear();
		pressKey(ctx, lvgui::Key::A, lvgui::Mod::Ctrl);
		pressKey(ctx, lvgui::Key::C, lvgui::Mod::Ctrl);
		assert(g_clipboard == area->text());

		// Ctrl+X cuts the selection.
		pressKey(ctx, lvgui::Key::X, lvgui::Mod::Ctrl);
		assert(area->text().empty());
		assert(!g_clipboard.empty());
		std::cout << "ok testCtrlVPastesAndCtrlCCopies\n";
	}

	void testNormalizesPastedText() {
		lvgui::TextArea area;
		area.paste("a\r\nb\rc\td\x01" "e");
		assert(area.text() == "a\nb\nc    de");
		assert(area.caretIndex() == area.text().size());
		std::cout << "ok testNormalizesPastedText\n";
	}

	void testEditing() {
		lvgui::TextArea area("one\ntwo");
		area.insertText("\n");
		area.insertText("three");
		assert(area.text() == "one\ntwo\nthree");

		area.selectAll();
		assert(area.copy() == "one\ntwo\nthree");
		area.deleteBackward();
		assert(area.text().empty());

		area.insertText("h\xC3\xA9llo");   // multi-byte codepoint
		area.moveCaret(-1, false);
		area.moveCaret(-1, false);
		area.moveCaret(-1, false);
		area.moveCaret(-1, false);
		assert(area.caretIndex() == 1);
		area.deleteForward();
		assert(area.text() == "hllo");

		lvgui::TextArea ro("fixed");
		ro.setReadOnly(true);
		ro.insertText("x");
		ro.selectAll();
		ro.deleteBackward();
		assert(ro.text() == "fixed");
		std::cout << "ok testEditing\n";
	}

	void testLargePasteLayoutAndNavigation() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 400.0f, 300.0f });
		auto* area = panel->add<lvgui::TextArea>();
		area->setVisibleRows(6);
		step(ctx);

		std::string big;
		for (int i = 0; i < 400; ++i) {
			big += "line " + std::to_string(i) + " of the pasted text\n";
		}
		area->setText(big);
		step(ctx);
		assert(area->lineCount() >= 401);   // 400 lines plus the empty one after the last '\n'

		// Focus it by clicking, then Ctrl+Home / Ctrl+End jump to the ends and scroll.
		lvgui::Rect b = area->bounds();
		click(ctx, { b.x + 10.0f, b.y + 10.0f });
		assert(ctx.focusedId() == area->id());

		pressKey(ctx, lvgui::Key::End, lvgui::Mod::Ctrl);
		assert(area->caretIndex() == area->text().size());
		assert(area->scrollY() > 0.0f);

		pressKey(ctx, lvgui::Key::Home, lvgui::Mod::Ctrl);
		assert(area->caretIndex() == 0);
		assert(area->scrollY() == 0.0f);

		pressKey(ctx, lvgui::Key::Down);
		pressKey(ctx, lvgui::Key::Down);
		assert(area->lineForIndex(area->caretIndex()) == 2);
		pressKey(ctx, lvgui::Key::Up);
		assert(area->lineForIndex(area->caretIndex()) == 1);

		// Enter inserts a newline; Ctrl+Enter does not.
		std::size_t before = area->text().size();
		pressKey(ctx, lvgui::Key::Enter);
		assert(area->text().size() == before + 1);
		pressKey(ctx, lvgui::Key::Enter, lvgui::Mod::Ctrl);
		assert(area->text().size() == before + 1);
		std::cout << "ok testLargePasteLayoutAndNavigation\n";
	}

	void testWordWrap() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 200.0f, 300.0f });
		auto* area = panel->add<lvgui::TextArea>();
		step(ctx);

		std::string para;
		for (int i = 0; i < 40; ++i) {
			para += "word ";
		}
		area->setText(para);
		step(ctx);
		std::size_t wrapped = area->lineCount();
		assert(wrapped > 3);

		// Every visual line concatenates back to the original text, with no gaps or overlaps.
		std::string rebuilt;
		for (std::size_t i = 0; i < area->lineCount(); ++i) {
			rebuilt += std::string(area->lineText(i));
		}
		assert(rebuilt == area->text());

		area->setWordWrap(false);
		step(ctx);
		assert(area->lineCount() == 1);

		// One unbroken run wider than the box still breaks, and still loses nothing.
		area->setWordWrap(true);
		area->setText(std::string(300, 'W'));
		step(ctx);
		assert(area->lineCount() > 1);
		rebuilt.clear();
		for (std::size_t i = 0; i < area->lineCount(); ++i) {
			rebuilt += std::string(area->lineText(i));
		}
		assert(rebuilt == area->text());
		std::cout << "ok testWordWrap\n";
	}

} // namespace

int main() {
	testNormalizesPastedText();
	testCtrlVPastesAndCtrlCCopies();
	testEditing();
	testLargePasteLayoutAndNavigation();
	testWordWrap();
	std::cout << "All TextArea tests passed\n";
	return 0;
}
