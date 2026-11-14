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

// docs/gui/05-widgets.md, "Text selection" -- headless, same pattern as test_listbox.cpp.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <iostream>
#include <string>

namespace lvgui = lightGraphics::ui;

namespace {

	std::string g_clipboard;

	lvgui::GuiCreateInfo testCreateInfo() {
		lvgui::GuiCreateInfo info;
		info.fontPath = LVG_UI_TEST_FONT_PATH;
		return info;
	}

	lvgui::PlatformHooks hooks() {
		lvgui::PlatformHooks h;
		h.getClipboardText = []() { return g_clipboard; };
		h.setClipboardText = [](std::string_view t) { g_clipboard.assign(t); };
		return h;
	}

	void step(lvgui::GuiContext& ctx) {
		ctx.beginFrame({ 800.0f, 600.0f }, 1.0f, 0.016f);
		ctx.update();
		ctx.endFrame();
	}

	void press(lvgui::GuiContext& ctx, lvgui::MouseButton b, lvgui::Vec2 p) {
		ctx.injectMousePos(p);
		ctx.injectMouseButton(b, true);
		step(ctx);
	}
	void release(lvgui::GuiContext& ctx, lvgui::MouseButton b) {
		ctx.injectMouseButton(b, false);
		step(ctx);
	}

	void ctrlC(lvgui::GuiContext& ctx) {
		ctx.injectKey(lvgui::Key::C, lvgui::Mod::Ctrl, true, false);
		step(ctx);
		ctx.injectKey(lvgui::Key::C, lvgui::Mod::Ctrl, false, false);
		step(ctx);
	}

	// Drag from the left edge of `b` to far right of it, on its vertical centre.
	void dragAcross(lvgui::GuiContext& ctx, lvgui::Rect b) {
		const float y = b.y + b.h * 0.5f;
		press(ctx, lvgui::MouseButton::Left, { b.x + 1.0f, y });
		ctx.injectMousePos({ b.x + b.w - 1.0f, y });
		step(ctx);
		release(ctx, lvgui::MouseButton::Left);
	}

	void testLabelDragSelectsAndCtrlCCopies() {
		g_clipboard.clear();
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 400.0f, 200.0f });
		auto* label = panel->add<lvgui::Label>("Hello selectable world");
		step(ctx);

		const lvgui::Rect b = label->bounds();
		const float y = b.y + b.h * 0.5f;
		press(ctx, lvgui::MouseButton::Left, { b.x + 1.0f, y });
		ctx.injectMousePos({ b.x + 2000.0f, y });   // far past the end: clamps to the last character
		step(ctx);
		release(ctx, lvgui::MouseButton::Left);

		assert(ctx.selectionOwner() == label->id());
		assert(ctx.selectedText() == "Hello selectable world");
		ctrlC(ctx);
		assert(g_clipboard == "Hello selectable world");

		std::cout << "✓ testLabelDragSelectsAndCtrlCCopies\n";
	}

	void testDragRunsAcrossSeveralLabels() {
		g_clipboard.clear();
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 400.0f, 300.0f });
		auto* one = panel->add<lvgui::Label>("First label");
		auto* two = panel->add<lvgui::Label>("Second label");
		auto* three = panel->add<lvgui::Label>("Third label");
		step(ctx);

		const lvgui::Rect a = one->bounds(), c = three->bounds();
		press(ctx, lvgui::MouseButton::Left, { a.x + 1.0f, a.y + a.h * 0.5f });
		ctx.injectMousePos({ c.x + c.w - 1.0f, c.y + c.h * 0.5f });
		step(ctx);
		release(ctx, lvgui::MouseButton::Left);

		assert(ctx.textSpan().widgets.size() == 3);
		assert(ctx.selectedText() == "First label\nSecond label\nThird label");
		ctrlC(ctx);
		assert(g_clipboard == "First label\nSecond label\nThird label");

		// And upwards, from the last label to the middle of the first.
		press(ctx, lvgui::MouseButton::Left, { c.x + c.w - 1.0f, c.y + c.h * 0.5f });
		ctx.injectMousePos({ a.x + 1.0f, a.y + a.h * 0.5f });
		step(ctx);
		release(ctx, lvgui::MouseButton::Left);
		assert(ctx.selectedText() == "First label\nSecond label\nThird label");
		(void)two;

		std::cout << "✓ testDragRunsAcrossSeveralLabels\n";
	}

	void testLabelClickDoesNotClearTextBoxFocus() {
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 400.0f, 200.0f });
		auto* label = panel->add<lvgui::Label>("Caption");
		auto* box = panel->add<lvgui::TextBox>("", "text");
		step(ctx);

		lvgui::Rect bb = box->bounds();
		press(ctx, lvgui::MouseButton::Left, { bb.x + bb.w * 0.5f, bb.y + bb.h * 0.5f });
		release(ctx, lvgui::MouseButton::Left);
		assert(ctx.focusedId() == box->id());

		lvgui::Rect lb = label->bounds();
		press(ctx, lvgui::MouseButton::Left, { lb.x + 2.0f, lb.y + lb.h * 0.5f });
		release(ctx, lvgui::MouseButton::Left);
		assert(ctx.focusedId() == box->id());

		std::cout << "✓ testLabelClickDoesNotClearTextBoxFocus\n";
	}

	void testClickElsewhereClearsSelection() {
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 400.0f, 200.0f });
		auto* label = panel->add<lvgui::Label>("Some text to select");
		auto* other = panel->add<lvgui::Button>("Elsewhere");
		step(ctx);
		dragAcross(ctx, label->bounds());
		assert(ctx.selectionOwner() == label->id());

		lvgui::Rect ob = other->bounds();
		press(ctx, lvgui::MouseButton::Left, { ob.x + 2.0f, ob.y + ob.h * 0.5f });
		release(ctx, lvgui::MouseButton::Left);
		assert(ctx.selectionOwner() == lvgui::kInvalidWidgetId);
		assert(!ctx.primaryText().empty());   // primary survives, for middle-click paste

		std::cout << "✓ testClickElsewhereClearsSelection\n";
	}

	void testMiddleClickPastesPrimaryIntoTextBox() {
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 400.0f, 200.0f });
		auto* label = panel->add<lvgui::Label>("pasteme");
		auto* box = panel->add<lvgui::TextBox>("", "");
		step(ctx);
		dragAcross(ctx, label->bounds());
		assert(ctx.selectedText() == "pasteme");

		lvgui::Rect bb = box->bounds();
		press(ctx, lvgui::MouseButton::Middle, { bb.x + bb.w * 0.5f, bb.y + bb.h * 0.5f });
		release(ctx, lvgui::MouseButton::Middle);
		assert(box->text() == "pasteme");

		std::cout << "✓ testMiddleClickPastesPrimaryIntoTextBox\n";
	}

	void testMiddleClickPastesPrimaryIntoTextArea() {
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 400.0f, 300.0f });
		auto* label = panel->add<lvgui::Label>("pasteme");
		auto* area = panel->add<lvgui::TextArea>();
		step(ctx);
		dragAcross(ctx, label->bounds());
		assert(ctx.selectedText() == "pasteme");

		lvgui::Rect ab = area->bounds();
		press(ctx, lvgui::MouseButton::Middle, { ab.x + ab.w * 0.5f, ab.y + 20.0f });
		release(ctx, lvgui::MouseButton::Middle);
		assert(area->text() == "pasteme");

		std::cout << "\xE2\x9C\x93 testMiddleClickPastesPrimaryIntoTextArea\n";
	}

	// The demo's prompt dialog: a Modal panel, text selected inside the same TextArea.
	void testMiddleClickPastesInsideModalTextArea() {
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* modal = ctx.createPanel("Prompt", { 20.0f, 20.0f, 500.0f, 400.0f },
			lvgui::PanelFlags::Modal | lvgui::PanelFlags::Closable);
		auto* area = modal->add<lvgui::TextArea>();
		area->setText("alpha beta");
		step(ctx);

		lvgui::Rect ab = area->bounds();
		const float y = ab.y + ab.h * 0.5f;
		// Select the word "alpha" by double-click at the start of the first line.
		const float lineY = ab.y + 10.0f;
		press(ctx, lvgui::MouseButton::Left, { ab.x + 6.0f, lineY });
		release(ctx, lvgui::MouseButton::Left);
		press(ctx, lvgui::MouseButton::Left, { ab.x + 6.0f, lineY });
		release(ctx, lvgui::MouseButton::Left);
		assert(ctx.selectedText() == "alpha");

		press(ctx, lvgui::MouseButton::Middle, { ab.x + 40.0f, y });
		release(ctx, lvgui::MouseButton::Middle);
		assert(area->text().find("alpha") != area->text().rfind("alpha"));   // pasted a second copy

		std::cout << "\xE2\x9C\x93 testMiddleClickPastesInsideModalTextArea\n";
	}

	void testListBoxDragAcrossRowsSelectsText() {
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 300.0f, 300.0f });
		auto* lb = panel->add<lvgui::ListBox>("", std::vector<std::string>{ "one two", "three four", "five six" });
		step(ctx);

		const lvgui::Theme& th = ctx.theme();
		const lvgui::Rect b = lb->bounds();
		const float row0 = b.y + th.framePadding + 0.5f * th.rowHeight;
		const float row2 = b.y + th.framePadding + 2.5f * th.rowHeight;
		press(ctx, lvgui::MouseButton::Left, { b.x + 1.0f, row0 });
		ctx.injectMousePos({ b.x + b.w - 20.0f, row2 });
		step(ctx);
		release(ctx, lvgui::MouseButton::Left);

		assert(ctx.selectionOwner() == lb->id());
		assert(ctx.selectedText() == "one two\nthree four\nfive six");
		assert(lb->selectedIndex() < 0);   // a text drag is not a row click

		std::cout << "✓ testListBoxDragAcrossRowsSelectsText\n";
	}

	void testListBoxClickThenCtrlCCopiesRow() {
		g_clipboard.clear();
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 300.0f, 300.0f });
		auto* lb = panel->add<lvgui::ListBox>("", std::vector<std::string>{ "alpha", "beta", "gamma" });
		step(ctx);

		const lvgui::Theme& th = ctx.theme();
		const lvgui::Rect b = lb->bounds();
		press(ctx, lvgui::MouseButton::Left, { b.x + b.w * 0.5f, b.y + th.framePadding + 1.5f * th.rowHeight });
		release(ctx, lvgui::MouseButton::Left);
		step(ctx);
		assert(lb->selectedText() == "beta");
		ctrlC(ctx);
		assert(g_clipboard == "beta");

		std::cout << "✓ testListBoxClickThenCtrlCCopiesRow\n";
	}

	void testLogViewDragSelectsLines() {
		g_clipboard.clear();
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 400.0f, 300.0f });
		auto* log = panel->add<lvgui::LogView>("");
		log->setWordWrap(false);
		log->push("first line");
		log->push("second line");
		step(ctx);

		const lvgui::Theme& th = ctx.theme();
		const lvgui::Rect b = log->bounds();
		const float lh = ctx.font().lineHeight(th.fontSize);
		const float y0 = b.y + th.framePadding + 0.5f * lh;
		press(ctx, lvgui::MouseButton::Left, { b.x + th.framePadding + 1.0f, y0 });
		ctx.injectMousePos({ b.x + b.w - 30.0f, y0 + lh });
		step(ctx);
		release(ctx, lvgui::MouseButton::Left);

		assert(ctx.selectedText() == "first line\nsecond line");
		ctrlC(ctx);
		assert(g_clipboard == "first line\nsecond line");

		std::cout << "✓ testLogViewDragSelectsLines\n";
	}

}

int main() {
	testLabelDragSelectsAndCtrlCCopies();
	testDragRunsAcrossSeveralLabels();
	testLabelClickDoesNotClearTextBoxFocus();
	testClickElsewhereClearsSelection();
	testMiddleClickPastesPrimaryIntoTextBox();
	testMiddleClickPastesPrimaryIntoTextArea();
	testMiddleClickPastesInsideModalTextArea();
	testListBoxDragAcrossRowsSelectsText();
	testListBoxClickThenCtrlCCopiesRow();
	testLogViewDragSelectsLines();

	std::cout << "\n✅ All text-selection tests passed!\n";
	return 0;
}
