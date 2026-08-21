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

#include <lightVulkanGraphics/ui/Font.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include <lightVulkanGraphics/ui/widgets/TextBox.h>
#include "Utf8.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace lvgui = lightGraphics::ui;

namespace {
	std::vector<std::uint8_t> loadFontFile() {
		std::ifstream file(LVG_UI_TEST_FONT_PATH, std::ios::binary | std::ios::ate);
		if (!file) {
			std::cerr << "Could not open test font at " << LVG_UI_TEST_FONT_PATH << "\n";
			std::exit(1);
		}
		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);
		std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
		file.read(reinterpret_cast<char*>(data.data()), size);
		return data;
	}

	void testBakeSucceeds(const lvgui::Font& font) {
		assert(font.isBaked());
		assert(font.atlasWidth() == 512);
		assert(font.atlasHeight() == 512);
		std::cout << "✓ testBakeSucceeds\n";
	}

	void testWhitePixelAndNoGlyphOverlap(const lvgui::Font& font) {
		lvgui::Vec2 uv = font.whitePixelUV();
		int x = static_cast<int>(uv.x * static_cast<float>(font.atlasWidth()));
		int y = static_cast<int>(uv.y * static_cast<float>(font.atlasHeight()));
		std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(font.atlasWidth()) + static_cast<std::size_t>(x);
		assert(font.atlasPixels()[idx] == 255);

		float reservedTop = static_cast<float>(font.atlasHeight() - 4);
		for (std::size_t r = 0; r < lvgui::kDefaultGlyphRangeCount; ++r) {
			const lvgui::GlyphRange& range = lvgui::kDefaultGlyphRanges[r];
			for (std::uint32_t cp = range.first; cp <= range.last; ++cp) {
				const lvgui::Glyph& g = font.glyphFor(cp);
				float bottomPixel = g.uv1.y * static_cast<float>(font.atlasHeight());
				assert(bottomPixel <= reservedTop + 0.01f);
			}
		}
		std::cout << "✓ testWhitePixelAndNoGlyphOverlap\n";
	}

	void testEmptyMeasure(const lvgui::Font& font) {
		lvgui::Vec2 size = font.measureText("", 14.0f);
		assert(size.x == 0.0f);
		assert(std::fabs(size.y - font.lineHeight(14.0f)) < 0.001f);
		std::cout << "✓ testEmptyMeasure\n";
	}

	void testDoubledWidth(const lvgui::Font& font) {
		float mWidth = font.measureText("M", 14.0f).x;
		float mmWidth = font.measureText("MM", 14.0f).x;
		assert(std::fabs(mmWidth - 2.0f * mWidth) < 0.5f);
		std::cout << "✓ testDoubledWidth\n";
	}

	void testOffsetAtIndexEndpoints(const lvgui::Font& font) {
		std::string s = "Slider value";
		assert(font.offsetAtIndex(s, 14.0f, 0) == 0.0f);
		float full = font.measureText(s, 14.0f).x;
		float atEnd = font.offsetAtIndex(s, 14.0f, s.size());
		assert(std::fabs(full - atEnd) < 0.001f);
		std::cout << "✓ testOffsetAtIndexEndpoints\n";
	}

	void testIndexOffsetMutualInverse(const lvgui::Font& font) {
		std::string s = "Omega \xCE\xA9 test 123";

		std::vector<std::size_t> boundaries;
		boundaries.push_back(0);
		for (std::size_t p = 0; p < s.size();) {
			p = lvgui::utf8NextBoundary(s, p);
			boundaries.push_back(p);
		}

		for (std::size_t b : boundaries) {
			float x = font.offsetAtIndex(s, 14.0f, b);
			std::size_t idx = font.indexAtOffset(s, 14.0f, x);
			assert(idx == b);
		}
		std::cout << "✓ testIndexOffsetMutualInverse\n";
	}

	void testContentScaleIndependence() {
		std::vector<std::uint8_t> ttf = loadFontFile();

		// theme.fontSize == 14 logical px, rendered at 1x and 2x content scale --
		// i.e. baked at 14 physical px and 28 physical px respectively, per the
		// documented "bake at theme.fontSize * contentScale" convention.
		lvgui::Font fontLoDpi;
		fontLoDpi.bake(ttf, 14.0f, 512, 512, 1.0f);

		lvgui::Font fontHiDpi;
		fontHiDpi.bake(ttf, 28.0f, 512, 512, 2.0f);

		// Sanity check that the two bakes actually differ physically -- otherwise the
		// width comparison below would pass vacuously.
		assert(fontHiDpi.glyphFor('H').size.y > fontLoDpi.glyphFor('H').size.y * 1.5f);

		float widthLoDpi = fontLoDpi.measureText("Hello", 14.0f).x;
		float widthHiDpi = fontHiDpi.measureText("Hello", 14.0f).x;
		assert(std::fabs(widthLoDpi - widthHiDpi) < 0.5f);

		float lineHeightLoDpi = fontLoDpi.lineHeight(14.0f);
		float lineHeightHiDpi = fontHiDpi.lineHeight(14.0f);
		assert(std::fabs(lineHeightLoDpi - lineHeightHiDpi) < 0.5f);

		std::cout << "✓ testContentScaleIndependence\n";
	}

	// Also pins the atlas-too-small retry path. A 28px bake does not fit the requested
	// 512x512, so bake() must retry at 1024x1024 -- and because ranges are packed one
	// call at a time, this additionally proves the retry restarts EVERY range rather
	// than resuming from whichever call returned zero: range 0 packed fine on the first
	// attempt, so if the retry resumed mid-way its glyphs would be missing here.
	void testAllDefaultGlyphsPresentAtHiDpiBake() {
		std::vector<std::uint8_t> ttf = loadFontFile();

		lvgui::Font font;
		font.bake(ttf, 28.0f, 512, 512, 2.0f);

		// The retry fired: the atlas is larger than the size that was asked for.
		assert(font.atlasWidth() == 1024);
		assert(font.atlasHeight() == 1024);
		assert(font.atlasPixels().size() ==
		       static_cast<std::size_t>(font.atlasWidth()) * static_cast<std::size_t>(font.atlasHeight()));

		// Range 0 is the one that succeeded pre-retry; check it explicitly.
		assert(font.hasGlyph('A'));
		assert(font.glyphFor('A').size.x > 0.0f);

		std::size_t checked = 0;
		for (std::size_t r = 0; r < lvgui::kDefaultGlyphRangeCount; ++r) {
			const lvgui::GlyphRange& range = lvgui::kDefaultGlyphRanges[r];
			for (std::uint32_t cp = range.first; cp <= range.last; ++cp) {
				if (!font.hasGlyph(cp)) {
					std::cerr << "Missing glyph for codepoint 0x" << std::hex << cp
					          << std::dec << " (range " << r << ")\n";
				}
				assert(font.hasGlyph(cp));
				++checked;
			}
		}

		std::cout << "✓ testAllDefaultGlyphsPresentAtHiDpiBake (" << checked
		          << " codepoints checked, atlas ended up " << font.atlasWidth()
		          << "x" << font.atlasHeight() << ")\n";
	}

	// Regression test for the oversampling bug: taking the quad size from the atlas
	// rect (x1-x0) rather than xoff2-xoff makes every glyph ~2x too wide at 2x2
	// oversampling, so rendered text visibly overlaps. Ink is never much wider than the
	// advance for Latin letters, which is what pins the size to the right units.
	void testGlyphInkFitsItsAdvance(const lvgui::Font& font) {
		const char* samples = "HMWimnox0189";
		for (const char* c = samples; *c != '\0'; ++c) {
			const lvgui::Glyph& g = font.glyphFor(static_cast<std::uint32_t>(*c));
			assert(g.size.x > 0.0f);
			assert(g.xAdvance > 0.0f);
			// A little slack for glyphs that legitimately overhang their advance.
			assert(g.size.x <= g.xAdvance * 1.35f);
		}

		// Same invariant stated end-to-end: consecutive glyphs in a word must not
		// overlap, i.e. each glyph's ink must end before the next pen position.
		const lvgui::Glyph& e = font.glyphFor('e');
		assert(e.offset.x + e.size.x <= e.xAdvance * 1.35f);

		std::cout << "✓ testGlyphInkFitsItsAdvance\n";
	}

	void testFallbackGlyphIsNeverZeroSize(const lvgui::Font& font) {
		std::uint32_t outOfRange = 0x4E2D; // CJK "middle", well outside every default range
		assert(!font.hasGlyph(outOfRange));
		const lvgui::Glyph& g = font.glyphFor(outOfRange);
		assert(g.size.x > 0.0f);
		assert(g.size.y > 0.0f);
		std::cout << "✓ testFallbackGlyphIsNeverZeroSize\n";
	}

	// Regression pin for the bug GuiContext::verifyInternalGlyphsBaked (GuiContext.cpp)
	// exists to catch at runtime: a codepoint LVGUI draws with internally (independent of
	// any consumer's own label text) silently falling back to a visible "?" glyph instead
	// of the intended one -- U+2022 BULLET, the original password-mask choice, was
	// outside every default range and rendered exactly that way before it was switched to
	// U+00B7. If TextBox::kPasswordMaskCodepoint is ever changed, this must keep pointing
	// at a codepoint the DEFAULT ranges actually bake, or every GuiContext construction
	// throws (docs/gui/04-input-and-events.md, "Internal glyph verification").
	void testTextBoxPasswordMaskCodepointIsBakedByDefault(const lvgui::Font& font) {
		assert(font.hasGlyph(lvgui::TextBox::kPasswordMaskCodepoint));
		std::cout << "✓ testTextBoxPasswordMaskCodepointIsBakedByDefault\n";
	}

	// docs/gui/05-widgets.md / GuiContext.h, GuiCreateInfo::fontPath: a bare GuiContext
	// (no VkApp) with fontPath left empty must fall back to findBundledFontPath()'s
	// search (GuiContext.cpp) rather than throwing. LVG_BUILD_FONT_DIR is baked into the
	// already-compiled LightVulkanGraphicsUI library as this project's real
	// assets/fonts directory (CMakeLists.txt), so this needs no test-specific compile
	// definition of its own -- unlike LVG_UI_TEST_FONT_PATH above, which every other
	// test in this file depends on.
	void testEmptyFontPathFallsBackToBundledFont() {
		lvgui::GuiCreateInfo info;   // fontPath deliberately left default-constructed (empty)
		lvgui::GuiContext ctx(info, lvgui::PlatformHooks{});
		assert(ctx.font().isBaked());
		std::cout << "✓ testEmptyFontPathFallsBackToBundledFont\n";
	}

	// docs/gui/03-text-and-fonts.md, "Headings": GuiCreateInfo::headingFontSize left at
	// its 0.0f default must leave the heading face entirely disabled -- a consumer who
	// never opts in pays nothing extra (no second bake, no wasted atlas memory).
	void testHeadingFontDisabledByDefault() {
		lvgui::GuiCreateInfo info;
		info.fontPath = LVG_UI_TEST_FONT_PATH;
		lvgui::GuiContext ctx(info, lvgui::PlatformHooks{});
		assert(!ctx.hasHeadingFont());
		assert(ctx.headingFontSize() == 0.0f);
		// kAtlasTextureId (0) until VkApp registers a real one -- see headingFontTextureId()'s
		// comment. A bare, headless GuiContext (this test) never gets that far.
		assert(ctx.headingFontTextureId() == lvgui::kAtlasTextureId);
		std::cout << "✓ testHeadingFontDisabledByDefault\n";
	}

	// Setting GuiCreateInfo::headingFontSize bakes a genuinely second, independent Font
	// -- distinct atlas, larger glyphs than the primary at the normal 14px test size --
	// not just a re-scaled view of the same one.
	void testHeadingFontOnRequestBakesALargerIndependentFont() {
		lvgui::GuiCreateInfo info;
		info.fontPath = LVG_UI_TEST_FONT_PATH;
		info.fontSize = 14.0f;
		info.headingFontSize = 28.0f;
		lvgui::GuiContext ctx(info, lvgui::PlatformHooks{});

		assert(ctx.hasHeadingFont());
		assert(ctx.headingFont().isBaked());
		assert(ctx.headingFontSize() == 28.0f);

		// Measured each at ITS OWN size (28px for heading, 14px for primary) -- a
		// correctly-independent second bake renders roughly twice as wide/tall, not the
		// same metrics reused.
		float primaryWidth = ctx.font().measureText("Heading", 14.0f).x;
		float headingWidth = ctx.headingFont().measureText("Heading", 28.0f).x;
		assert(headingWidth > primaryWidth * 1.5f);

		float primaryLineHeight = ctx.font().lineHeight(14.0f);
		float headingLineHeight = ctx.headingFont().lineHeight(28.0f);
		assert(headingLineHeight > primaryLineHeight * 1.5f);

		std::cout << "✓ testHeadingFontOnRequestBakesALargerIndependentFont\n";
	}

	// docs/gui/03-text-and-fonts.md, "Tabular figures": every digit's own pen advance is
	// padded to the widest digit's, so a live-updating numeric readout's columns don't
	// jitter as the value changes. Every digit measured alone, under TextFlags::Tabular,
	// must come out to exactly the same width -- the widest digit's own (unpadded) width.
	void testTabularDigitsAllMeasureTheSameWidth(const lvgui::Font& font) {
		float widest = 0.0f;
		for (char d = '0'; d <= '9'; ++d) {
			widest = std::max(widest, font.measureText(std::string(1, d), 14.0f).x);
		}
		assert(widest > 0.0f);

		for (char d = '0'; d <= '9'; ++d) {
			float w = font.measureText(std::string(1, d), 14.0f, lvgui::TextFlags::Tabular).x;
			assert(std::fabs(w - widest) < 0.01f);
		}
		std::cout << "✓ testTabularDigitsAllMeasureTheSameWidth\n";
	}

	// Tabular only touches ASCII digits -- everything else (letters, punctuation, a
	// digit measured WITHOUT the flag) must come out identical to the flag-less
	// measurement, byte for byte.
	void testTabularOnlyAffectsDigitsAndOnlyWhenRequested(const lvgui::Font& font) {
		const char* nonDigits = "Hello, world! Omega";
		float plain = font.measureText(nonDigits, 14.0f).x;
		float tabular = font.measureText(nonDigits, 14.0f, lvgui::TextFlags::Tabular).x;
		assert(std::fabs(plain - tabular) < 0.001f);

		float digitPlain = font.measureText("5", 14.0f).x;
		float digitNoneFlag = font.measureText("5", 14.0f, lvgui::TextFlags::None).x;
		assert(std::fabs(digitPlain - digitNoneFlag) < 0.001f);

		std::cout << "✓ testTabularOnlyAffectsDigitsAndOnlyWhenRequested\n";
	}

	// A font with no digits baked at all (a restricted glyph range) must not crash or
	// misbehave under TextFlags::Tabular -- advanceFor() falls back to the (fallback)
	// glyph's own advance, exactly as it would with the flag unset, since
	// m_tabularDigitAdvanceAtBake stays 0.0f when no '0'-'9' glyph was ever baked.
	void testTabularIsNoOpWhenNoDigitsAreBaked() {
		std::vector<std::uint8_t> ttf = loadFontFile();
		lvgui::GlyphRange greekOnly[] = { { 0x0391, 0x03C9 } };

		lvgui::Font font;
		font.bake(ttf, 14.0f, greekOnly, 1, 512, 512, 1.0f);
		assert(!font.hasGlyph('5'));

		float withTabular = font.advanceFor('5', 14.0f, lvgui::TextFlags::Tabular);
		float withoutTabular = font.advanceFor('5', 14.0f, lvgui::TextFlags::None);
		assert(std::fabs(withTabular - withoutTabular) < 0.001f);

		std::cout << "✓ testTabularIsNoOpWhenNoDigitsAreBaked\n";
	}
}

int main() {
	std::vector<std::uint8_t> ttf = loadFontFile();

	lvgui::Font font;
	font.bake(ttf, 14.0f, 512, 512, 1.0f);

	testBakeSucceeds(font);
	testWhitePixelAndNoGlyphOverlap(font);
	testEmptyMeasure(font);
	testDoubledWidth(font);
	testOffsetAtIndexEndpoints(font);
	testIndexOffsetMutualInverse(font);
	testContentScaleIndependence();
	testAllDefaultGlyphsPresentAtHiDpiBake();
	testGlyphInkFitsItsAdvance(font);
	testFallbackGlyphIsNeverZeroSize(font);
	testTextBoxPasswordMaskCodepointIsBakedByDefault(font);
	testEmptyFontPathFallsBackToBundledFont();

	testTabularDigitsAllMeasureTheSameWidth(font);
	testTabularOnlyAffectsDigitsAndOnlyWhenRequested(font);
	testTabularIsNoOpWhenNoDigitsAreBaked();

	testHeadingFontDisabledByDefault();
	testHeadingFontOnRequestBakesALargerIndependentFont();

	std::cout << "\n✅ All Font tests passed!\n";
	return 0;
}
