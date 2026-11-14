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

// Newlines in Label text (and in wrapText(), which Label, LogView and the dialogs share): a newline is a
// hard line break. Headless, same pattern as test_textselect.cpp.

#include <lightVulkanGraphics/ui/Ui.h>

#include "TextWrap.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

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

	std::vector<std::string> lines(const lvgui::GuiContext& ctx, std::string_view text, float width) {
		std::vector<std::string> out;
		for (std::string_view l : lvgui::wrapText(ctx.font(), text, ctx.theme().fontSize, width)) {
			out.emplace_back(l);
		}
		return out;
	}

	bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

	void testWrapTextBreaksAtNewlines() {
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		using V = std::vector<std::string>;
		// Without wrapping (width 0) the newlines still break.
		assert((lines(ctx, "alpha\nbeta\ngamma", 0.0f) == V{ "alpha", "beta", "gamma" }));
		// Empty paragraphs are empty lines, and a trailing newline leaves a last empty line.
		assert((lines(ctx, "a\n\nb", 0.0f) == V{ "a", "", "b" }));
		assert((lines(ctx, "a\n", 0.0f) == V{ "a", "" }));
		assert((lines(ctx, "\nx", 0.0f) == V{ "", "x" }));
		assert((lines(ctx, "\n", 0.0f) == V{ "", "" }));
		// A CRLF pair breaks once and leaves no stray carriage return.
		assert((lines(ctx, "one\r\ntwo\r\n", 0.0f) == V{ "one", "two", "" }));
		// Text without a newline is untouched, including the empty string.
		assert((lines(ctx, "plain text", 0.0f) == V{ "plain text" }));
		assert((lines(ctx, "plain text", 5000.0f) == V{ "plain text" }));
		assert((lines(ctx, "", 100.0f) == V{ "" }));

		// Each paragraph wraps on its own width; the hard break survives wrapping.
		const std::string text = "the quick brown fox jumps over the lazy dog again and again\nshort";
		const V narrow = lines(ctx, text, 120.0f);
		assert(narrow.size() > 3);
		assert(narrow.back() == "short");
		for (std::size_t i = 0; i + 1 < narrow.size(); ++i) {
			assert(narrow[i].find('\n') == std::string::npos);
		}
		// Rejoining the first paragraph's segments with spaces gives the paragraph back.
		std::string rejoined;
		for (std::size_t i = 0; i + 1 < narrow.size(); ++i) {
			rejoined += (i ? " " : "") + narrow[i];
		}
		assert(rejoined == "the quick brown fox jumps over the lazy dog again and again");

		// The returned views really are views into the text (Label relies on that).
		const std::string_view source = "left\nright";
		for (std::string_view l : lvgui::wrapText(ctx.font(), source, ctx.theme().fontSize, 0.0f)) {
			assert(l.data() >= source.data() && l.data() + l.size() <= source.data() + source.size());
		}
		std::cout << "✓ testWrapTextBreaksAtNewlines\n";
	}

	void testLabelSizesAndLaysOutLines() {
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 400.0f, 300.0f });
		const float lh = ctx.font().lineHeight(ctx.theme().fontSize);

		auto* one = panel->add<lvgui::Label>("single line");
		auto* three = panel->add<lvgui::Label>("short\na rather longer second line\nx");
		auto* wrapped = panel->add<lvgui::Label>("word wrap on, with a break\nhere");
		wrapped->setWordWrap(true);
		step(ctx);
		step(ctx); // a wrapped Label's height settles on the second layout pass

		// A single line is exactly what it always was.
		const lvgui::Vec2 oneSize = one->preferredSize(ctx);
		assert(near(oneSize.y, ctx.font().measureText("single line", ctx.theme().fontSize).y));

		// Three lines: one line height each, as wide as the widest.
		const lvgui::Vec2 size = three->preferredSize(ctx);
		assert(near(size.y, 3.0f * lh));
		assert(near(size.x, ctx.font().measureText("a rather longer second line", ctx.theme().fontSize).x));
		assert(three->bounds().h >= 3.0f * lh - 0.01f); // the panel gave it the room

		// The wrapped one counts the break too.
		assert(wrapped->preferredSize(ctx).y >= 2.0f * lh - 0.01f);
		std::cout << "✓ testLabelSizesAndLaysOutLines\n";
	}

	void testSelectionCopiesHardBreaksAsNewlines() {
		g_clipboard.clear();
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 400.0f, 300.0f });
		auto* label = panel->add<lvgui::Label>("alpha\nbeta\ngamma");
		step(ctx);
		const lvgui::Rect b = label->bounds();
		const float lh = ctx.font().lineHeight(ctx.theme().fontSize);

		auto press = [&](lvgui::Vec2 p) {
			ctx.injectMousePos(p);
			ctx.injectMouseButton(lvgui::MouseButton::Left, true);
			step(ctx);
		};
		auto dragTo = [&](lvgui::Vec2 p) {
			ctx.injectMousePos(p);
			step(ctx);
			ctx.injectMouseButton(lvgui::MouseButton::Left, false);
			step(ctx);
		};
		// From the start of the first line to the end of the second: "alpha\nbeta", with a newline between.
		press({ b.x + 1.0f, b.y + lh * 0.5f });
		dragTo({ b.x + 2000.0f, b.y + lh * 1.5f });
		assert(ctx.selectionOwner() == label->id());
		assert(ctx.selectedText() == "alpha\nbeta");

		for (int i = 0; i < 60; ++i) {
			step(ctx); // let the double-click interval pass, so the next press is a fresh drag
		}
		// Upwards from the end of the last line to the start of the first: the whole label, exactly as set.
		press({ b.x + b.w - 1.0f, b.y + lh * 2.5f }); // just inside the right edge, on the last line
		dragTo({ b.x + 1.0f, b.y + lh * 0.5f });
		assert(ctx.selectedText() == "alpha\nbeta\ngamma");
		std::cout << "✓ testSelectionCopiesHardBreaksAsNewlines\n";
	}

	void testWrappedSegmentsStillRejoinWithSpaces() {
		g_clipboard.clear();
		lvgui::GuiContext ctx(testCreateInfo(), hooks());
		auto* panel = ctx.createPanel("Panel", { 20.0f, 20.0f, 160.0f, 300.0f });
		auto* label = panel->add<lvgui::Label>("the quick brown fox jumps over the lazy dog\nand a hard break");
		label->setWordWrap(true);
		step(ctx);
		step(ctx);
		const lvgui::Rect b = label->bounds();
		const float lh = ctx.font().lineHeight(ctx.theme().fontSize);
		const int rows = static_cast<int>(std::lround(b.h / lh));
		const std::string text = "the quick brown fox jumps over the lazy dog\nand a hard break";
		assert(rows == static_cast<int>(lines(ctx, text, b.w).size()));
		assert(rows >= 3); // the first paragraph wraps to more than one row, then the second starts a new one

		ctx.injectMousePos({ b.x + 1.0f, b.y + lh * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMousePos({ b.x + 2000.0f, b.y + lh * (static_cast<float>(rows) - 0.5f) });
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		// Everything selected: exactly the text as set.
		assert(ctx.selectedText() == "the quick brown fox jumps over the lazy dog\nand a hard break");

		for (int i = 0; i < 60; ++i) {
			step(ctx); // past the double-click interval again
		}
		// Selecting only the wrapped first paragraph joins its rows with spaces, not newlines.
		ctx.injectMousePos({ b.x + 1.0f, b.y + lh * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMousePos({ b.x + 2000.0f, b.y + lh * (static_cast<float>(rows) - 1.5f) });
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		const std::string partial = ctx.selectedText();
		assert(partial.find('\n') == std::string::npos);
		assert(partial.rfind("the quick", 0) == 0);
		std::cout << "✓ testWrappedSegmentsStillRejoinWithSpaces\n";
	}

} // namespace

int main() {
	testWrapTextBreaksAtNewlines();
	testLabelSizesAndLaysOutLines();
	testSelectionCopiesHardBreaksAsNewlines();
	testWrappedSegmentsStillRejoinWithSpaces();
	std::cout << "\n✅ All multi-line label tests passed!\n";
	return 0;
}
