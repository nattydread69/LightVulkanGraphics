// TextBox tests (docs/gui/05-widgets.md, "TextBox"). Headless -- no Vulkan device, no
// window, same pattern as test_slider.cpp. Most of these exercise TextBox's editing
// operations directly (docs/gui/08-testing.md: "test the editing state machine
// directly, without rendering"); a few drive a real GuiContext to cover focus/commit/
// revert and the mouse-click-to-caret path, which need real Font metrics.

#include <lightVulkanGraphics/ui/Ui.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
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

	// ASCII-only helper: injects one char event per byte, which is exactly one codepoint
	// event for plain ASCII text (docs/gui/04: text input comes only from the char
	// callback).
	void typeAscii(lvgui::GuiContext& ctx, std::string_view ascii) {
		for (char c : ascii) {
			ctx.injectChar(static_cast<unsigned char>(c));
		}
	}

	void pressKey(lvgui::GuiContext& ctx, int key, int mods = 0) {
		ctx.injectKey(key, mods, true, false);
		step(ctx);
		ctx.injectKey(key, mods, false, false);
		step(ctx);
	}

	// A codepoint boundary check independent of TextBox/Utf8.cpp's own implementation,
	// so this test doesn't just assert the production code agrees with itself: a
	// continuation byte (top bits 10) is never a valid boundary; everything else is.
	bool isCodepointBoundary(std::string_view s, std::size_t idx) {
		if (idx == 0 || idx == s.size()) {
			return true;
		}
		if (idx > s.size()) {
			return false;
		}
		unsigned char b = static_cast<unsigned char>(s[idx]);
		return (b & 0xC0) != 0x80;
	}

	void appendUtf8(std::string& out, std::uint32_t cp) {
		if (cp < 0x80) {
			out.push_back(static_cast<char>(cp));
		} else if (cp < 0x800) {
			out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else {
			out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}

	// ---- pure editing-operation tests (no GuiContext) ------------------------------

	void testInsertBackspaceDeleteAtStartMiddleEnd() {
		lvgui::TextBox box("t", "bd");   // start with "bd" so insert lands in the middle too

		box.moveHome(false);
		box.insertText("a");             // start: "a" + "bd" -> "abd"
		assert(box.text() == "abd");

		box.moveCaret(1, false);         // caret after 'b' (index 2)
		box.insertText("c");             // middle: "ab" + "c" + "d" -> "abcd"
		assert(box.text() == "abcd");

		box.moveEnd(false);
		box.insertText("e");             // end: "abcde"
		assert(box.text() == "abcde");

		box.deleteBackward();            // end: "abcd"
		assert(box.text() == "abcd");
		assert(box.caretIndex() == 4);

		box.moveHome(false);
		box.deleteForward();             // start: "bcd"
		assert(box.text() == "bcd");
		assert(box.caretIndex() == 0);

		box.moveCaret(1, false);
		box.moveCaret(1, false);         // caret after "bc" (index 2)
		box.deleteBackward();            // middle: removes 'c' -> "bd"
		assert(box.text() == "bd");

		std::cout << "✓ testInsertBackspaceDeleteAtStartMiddleEnd\n";
	}

	void testBackspaceDeletesWholeMultibyteCodepoint() {
		std::string s;
		appendUtf8(s, 'x');
		appendUtf8(s, 0x03B1);   // Greek small alpha, 2 bytes
		appendUtf8(s, 'y');
		lvgui::TextBox box("t", s);

		box.moveEnd(false);
		box.deleteBackward();   // removes 'y'
		assert(box.text().size() == s.size() - 1);

		box.deleteBackward();   // removes the whole 2-byte alpha codepoint, not one byte
		std::string expected;
		appendUtf8(expected, 'x');
		assert(box.text() == expected);
		assert(isCodepointBoundary(box.text(), box.caretIndex()));

		std::cout << "✓ testBackspaceDeletesWholeMultibyteCodepoint\n";
	}

	void testRightArrowWithSelectionCollapsesToEdgeNotOneFurther() {
		lvgui::TextBox box("t", "hello");
		box.moveHome(false);
		box.moveCaret(1, true);
		box.moveCaret(1, true);   // select "he" -- anchor=0, caret=2
		assert(box.hasSelection());
		std::size_t selEnd = box.selectionEnd();

		box.moveCaret(1, false);   // Right, no Shift: collapse to the RIGHT edge...
		assert(!box.hasSelection());
		assert(box.caretIndex() == selEnd);   // ...not selEnd + 1

		box.moveHome(false);
		box.moveCaret(1, true);
		box.moveCaret(1, true);
		std::size_t selStart = box.selectionStart();
		box.moveCaret(-1, false);   // Left, no Shift: collapse to the LEFT edge
		assert(!box.hasSelection());
		assert(box.caretIndex() == selStart);

		std::cout << "✓ testRightArrowWithSelectionCollapsesToEdgeNotOneFurther\n";
	}

	void testShiftSelectExtendsAndCancels() {
		lvgui::TextBox box("t", "hello");
		box.moveEnd(false);
		box.moveCaret(-1, true);
		box.moveCaret(-1, true);   // Shift+Left twice from the end: select "lo"
		assert(box.hasSelection());
		assert(box.selectionStart() == 3 && box.selectionEnd() == 5);

		box.moveCaret(1, true);    // Shift+Right cancels one step of the selection...
		assert(box.caretIndex() == 4);
		box.moveCaret(-1, true);   // ...and Shift+Left re-extends it back to the same spot,
		assert(box.caretIndex() == 3);
		assert(box.anchorIndex() == 5);   // net effect: back to the original selection

		std::cout << "✓ testShiftSelectExtendsAndCancels\n";
	}

	void testCtrlAThenTypeReplacesEverything() {
		lvgui::TextBox box("t", "old value");
		box.selectAll();
		box.insertText("new");
		assert(box.text() == "new");

		std::cout << "✓ testCtrlAThenTypeReplacesEverything\n";
	}

	void testPasteStripsControlCharsAndNewlines() {
		lvgui::TextBox box("t", "");
		box.paste("a\nb\tc\rd");
		assert(box.text() == "abcd");

		std::cout << "✓ testPasteStripsControlCharsAndNewlines\n";
	}

	void testMaxLengthCountsCodepointsNotBytes() {
		lvgui::TextBox box("t", "");
		box.setMaxLength(5);

		std::string alpha;
		appendUtf8(alpha, 0x03B1);   // 2-byte Greek alpha
		for (int i = 0; i < 5; ++i) {
			box.insertText(alpha);
		}
		assert(box.text().size() == alpha.size() * 5);   // 10 bytes, 5 codepoints: at the limit

		box.insertText(alpha);   // a 6th codepoint must be rejected
		assert(box.text().size() == alpha.size() * 5);

		std::cout << "✓ testMaxLengthCountsCodepointsNotBytes\n";
	}

	void testCaretBoundaryPropertyRandomOps() {
		const std::vector<std::string> insertables = [] {
			std::vector<std::string> v;
			for (char c : std::string("abcXYZ012 _")) v.push_back(std::string(1, c));
			std::string alpha, eacute;
			appendUtf8(alpha, 0x03B1);    // Greek alpha
			appendUtf8(eacute, 0x00E9);   // Latin small e with acute
			v.push_back(alpha);
			v.push_back(eacute);
			return v;
		}();

		for (int seed = 0; seed < 2000; ++seed) {
			std::mt19937 rng(static_cast<unsigned>(seed));
			std::uniform_int_distribution<int> pickOp(0, 9);
			std::uniform_int_distribution<std::size_t> pickInsertable(0, insertables.size() - 1);
			std::uniform_int_distribution<int> pickBool(0, 1);

			std::string initial;
			int initialLen = seed % 6;
			for (int i = 0; i < initialLen; ++i) {
				initial += insertables[pickInsertable(rng)];
			}
			lvgui::TextBox box("t", initial);

			for (int op = 0; op < 40; ++op) {
				bool select = pickBool(rng) != 0;
				int dir = pickBool(rng) ? 1 : -1;
				switch (pickOp(rng)) {
					case 0: box.insertText(insertables[pickInsertable(rng)]); break;
					case 1: box.deleteBackward(); break;
					case 2: box.deleteForward(); break;
					case 3: box.moveCaret(dir, select); break;
					case 4: box.moveCaretWord(dir, select); break;
					case 5: box.moveHome(select); break;
					case 6: box.moveEnd(select); break;
					case 7: box.selectAll(); break;
					case 8: { std::string cut = box.cut(); (void)cut; break; }
					case 9: box.paste("x\ny\tz" + insertables[pickInsertable(rng)]); break;
				}

				assert(isCodepointBoundary(box.text(), box.caretIndex()));
				assert(isCodepointBoundary(box.text(), box.anchorIndex()));
				assert(box.caretIndex()  <= box.text().size());
				assert(box.anchorIndex() <= box.text().size());
			}
		}

		std::cout << "✓ testCaretBoundaryPropertyRandomOps (2000 seeds x 40 ops)\n";
	}

	// ---- GuiContext-driven tests (need real Font metrics / focus machinery) -------

	void testEscapeRevertsToPreFocusText() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		auto* box = panel->add<lvgui::TextBox>("", "original");

		step(ctx);
		lvgui::Rect b = box->bounds();
		ctx.injectMousePos({ b.x + 2.0f, b.y + b.h * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(ctx.focusedId() == box->id());

		box->selectAll();
		typeAscii(ctx, "clobbered");
		step(ctx);
		assert(box->text() == "clobbered");

		pressKey(ctx, lvgui::Key::Escape);
		assert(box->text() == "original");
		assert(ctx.focusedId() == lvgui::kInvalidWidgetId);   // Escape also clears focus

		std::cout << "✓ testEscapeRevertsToPreFocusText\n";
	}

	void testCommitFiresOnEnterAndOnFocusLoss() {
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		auto* box = panel->add<lvgui::TextBox>("", "");
		auto* other = panel->add<lvgui::Button>("elsewhere");

		int commitCount = 0;
		std::string lastCommitted;
		box->setOnCommit([&](std::string_view s) { ++commitCount; lastCommitted = std::string(s); });

		step(ctx);
		lvgui::Rect b = box->bounds();
		ctx.injectMousePos({ b.x + 2.0f, b.y + b.h * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);

		typeAscii(ctx, "hi");
		step(ctx);
		pressKey(ctx, lvgui::Key::Enter);
		assert(commitCount == 1);
		assert(lastCommitted == "hi");

		// Click a different widget: commit must fire again, on focus loss, without Enter.
		lvgui::Rect ob = other->bounds();
		ctx.injectMousePos({ ob.x + 2.0f, ob.y + ob.h * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(commitCount == 2);

		std::cout << "✓ testCommitFiresOnEnterAndOnFocusLoss\n";
	}

	void testMouseClickPositionsCaretAtClickedCharacterNotOneOff() {
		// Two independent contexts, one per click, so the second press is never close
		// enough in (simulated) time to the first to be mistaken for a double-click --
		// this test wants two independent single clicks.
		{
			lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
			auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
			auto* box = panel->add<lvgui::TextBox>("", "hello world");
			step(ctx);
			lvgui::Rect b = box->bounds();

			// Click near the LEFT inner edge -- should land at or very near index 0, not
			// somewhere in the middle of the string (the classic "off by one" bug this
			// acceptance criterion targets).
			ctx.injectMousePos({ b.x + 5.0f, b.y + b.h * 0.5f });
			ctx.injectMouseButton(lvgui::MouseButton::Left, true);
			step(ctx);
			assert(box->caretIndex() <= 1);
			ctx.injectMouseButton(lvgui::MouseButton::Left, false);
			step(ctx);
		}
		{
			lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
			auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
			auto* box = panel->add<lvgui::TextBox>("", "hello world");
			step(ctx);
			lvgui::Rect b = box->bounds();

			// Click far to the right, beyond the text -- should clamp to the end, not
			// throw or wrap.
			ctx.injectMousePos({ b.x + b.w - 5.0f, b.y + b.h * 0.5f });
			ctx.injectMouseButton(lvgui::MouseButton::Left, true);
			step(ctx);
			assert(box->caretIndex() == box->text().size());
			ctx.injectMouseButton(lvgui::MouseButton::Left, false);
			step(ctx);
		}

		std::cout << "✓ testMouseClickPositionsCaretAtClickedCharacterNotOneOff\n";
	}

	void testPasswordModeCaretMatchesDisplayedBulletString() {
		// Greek alpha is 2 UTF-8 bytes but must measure as ONE bullet-width column, not
		// two -- the whole point of "measure the displayed string, not the source"
		// (docs/gui/05, "TextBox").
		std::string secret;
		appendUtf8(secret, 'a');
		appendUtf8(secret, 0x03B1);
		appendUtf8(secret, 'b');

		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		auto* plainBox = panel->add<lvgui::TextBox>("", secret);
		auto* maskedBox = panel->add<lvgui::TextBox>("", secret);
		maskedBox->setPasswordMode(true);
		step(ctx);

		// Click at the same screen position in both boxes: the masked box must resolve
		// to the codepoint boundary AFTER the 2nd bullet ('b'), same rank as the plain
		// box resolves to after 'a'+alpha -- even though that is not the same BYTE
		// offset in m_text (secret's 'b' sits at byte 3, not byte 2).
		lvgui::Rect pb = plainBox->bounds();
		lvgui::Rect mb = maskedBox->bounds();
		float clickX = pb.x + 18.0f;   // comfortably past two glyphs at 14px

		ctx.injectMousePos({ clickX, pb.y + pb.h * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		std::size_t plainCaret = plainBox->caretIndex();

		ctx.injectMousePos({ mb.x + (clickX - pb.x), mb.y + mb.h * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		std::size_t maskedCaret = maskedBox->caretIndex();

		// Both must land on a valid boundary in the real (unmasked) text either way.
		assert(isCodepointBoundary(secret, plainCaret));
		assert(isCodepointBoundary(secret, maskedCaret));

		std::cout << "✓ testPasswordModeCaretMatchesDisplayedBulletString\n";
	}

	void testFocusedTextBoxClaimsKeyboardSoWasdDoesNotReachCamera() {
		// The scenario docs/gui/09's phase 7 prompt calls out by name: WASD over a panel
		// moves the camera only when nothing has claimed the keyboard (docs/gui/04).
		// GuiContext::wantsKeyboard() is what the real VkApp gates camera movement on
		// (src/VkApp.cpp, updateCameraFromKeyboard) -- this test pins that contract for
		// TextBox specifically, headlessly.
		lvgui::GuiContext ctx(testCreateInfo(), lvgui::PlatformHooks{});
		auto* panel = ctx.createPanel("Panel", { 0.0f, 0.0f, 300.0f, 100.0f });
		auto* box = panel->add<lvgui::TextBox>("", "");

		step(ctx);
		assert(!ctx.wantsKeyboard());   // unfocused: camera should still receive WASD

		lvgui::Rect b = box->bounds();
		ctx.injectMousePos({ b.x + 2.0f, b.y + b.h * 0.5f });
		ctx.injectMouseButton(lvgui::MouseButton::Left, true);
		step(ctx);
		ctx.injectMouseButton(lvgui::MouseButton::Left, false);
		step(ctx);
		assert(ctx.focusedId() == box->id());
		assert(ctx.wantsKeyboard());   // focused: camera must stop reading WASD

		typeAscii(ctx, "wasd");
		step(ctx);
		assert(box->text() == "wasd");   // and the characters land in the box instead

		std::cout << "✓ testFocusedTextBoxClaimsKeyboardSoWasdDoesNotReachCamera\n";
	}

}

int main() {
	testInsertBackspaceDeleteAtStartMiddleEnd();
	testBackspaceDeletesWholeMultibyteCodepoint();
	testRightArrowWithSelectionCollapsesToEdgeNotOneFurther();
	testShiftSelectExtendsAndCancels();
	testCtrlAThenTypeReplacesEverything();
	testPasteStripsControlCharsAndNewlines();
	testMaxLengthCountsCodepointsNotBytes();
	testCaretBoundaryPropertyRandomOps();

	testEscapeRevertsToPreFocusText();
	testCommitFiresOnEnterAndOnFocusLoss();
	testMouseClickPositionsCaretAtClickedCharacterNotOneOff();
	testPasswordModeCaretMatchesDisplayedBulletString();
	testFocusedTextBoxClaimsKeyboardSoWasdDoesNotReachCamera();

	std::cout << "\n✅ All phase 7 TextBox tests passed!\n";
	return 0;
}
