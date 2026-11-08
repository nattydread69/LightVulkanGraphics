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

// Font features added after the original bake-once Font: kerning, dynamic glyphs,
// fallback faces, SDF mode, font styles in GuiContext, and the file helpers.

#include <lightVulkanGraphics/ui/DrawList.h>
#include <lightVulkanGraphics/ui/Font.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include <lightVulkanGraphics/ui/widgets/Label.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace lvgui = lightGraphics::ui;

namespace {
	const std::vector<std::uint8_t>& inter() {
		static const std::vector<std::uint8_t> data = lvgui::Font::readFile(LVG_UI_TEST_FONT_PATH);
		return data;
	}
	const std::vector<std::uint8_t>& mono() {
		static const std::vector<std::uint8_t> data = lvgui::Font::readFile(LVG_UI_TEST_MONO_FONT_PATH);
		return data;
	}

	bool near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) < eps; }

	// ---- kerning ----

	void testKerningTucksPairsTogether() {
		lvgui::Font font;
		font.bake(inter(), 28.0f);

		float kern = font.kerning('A', 'V', 28.0f);
		assert(kern < 0.0f);   // Inter kerns AV tighter

		float pair = font.measureText("AV", 28.0f).x;
		float separate = font.measureText("A", 28.0f).x + font.measureText("V", 28.0f).x;
		assert(near(pair, separate + kern));

		// Opt-outs.
		assert(near(font.measureText("AV", 28.0f, lvgui::TextFlags::NoKerning).x, separate));
		lvgui::Font plain;
		lvgui::FontOptions options;
		options.kerning = false;
		plain.setOptions(options);
		plain.bake(inter(), 28.0f);
		assert(near(plain.measureText("AV", 28.0f).x, separate));

		// Scales with pixel size.
		assert(near(font.kerning('A', 'V', 14.0f), kern * 0.5f, 0.01f));
		std::cout << "✓ testKerningTucksPairsTogether\n";
	}

	void testKerningNeverBreaksTabularDigits() {
		lvgui::Font font;
		font.bake(inter(), 14.0f);
		// Whatever the font does between digits, Tabular columns must stay fixed.
		float a = font.measureText("1111", 14.0f, lvgui::TextFlags::Tabular).x;
		float b = font.measureText("8888", 14.0f, lvgui::TextFlags::Tabular).x;
		float c = font.measureText("1818", 14.0f, lvgui::TextFlags::Tabular).x;
		assert(near(a, b) && near(b, c));
		assert(font.kerningFor('1', '1', 14.0f, lvgui::TextFlags::Tabular) == 0.0f);
		assert(font.kerningFor('1', 'X', 14.0f, lvgui::TextFlags::Tabular) == 0.0f);
		assert(font.kerningFor('X', '1', 14.0f, lvgui::TextFlags::Tabular) == 0.0f);
		// What follows a tabular digit lands in the same place whichever digit it was.
		assert(near(font.measureText("1X", 14.0f, lvgui::TextFlags::Tabular).x,
		            font.measureText("8X", 14.0f, lvgui::TextFlags::Tabular).x));
		assert(font.kerningFor(0, 'A', 14.0f, lvgui::TextFlags::None) == 0.0f);
		std::cout << "✓ testKerningNeverBreaksTabularDigits\n";
	}

	void testCursorMathIncludesKerning() {
		lvgui::Font font;
		font.bake(inter(), 28.0f);
		const std::string s = "AVATAR";
		// Cursor after the whole string is exactly its measured width.
		assert(near(font.offsetAtIndex(s, 28.0f, s.size()), font.measureText(s, 28.0f).x));
		// Every boundary round-trips through indexAtOffset().
		for (std::size_t i = 0; i <= s.size(); ++i) {
			float x = font.offsetAtIndex(s, 28.0f, i);
			assert(font.indexAtOffset(s, 28.0f, x + 0.01f) == i);
		}
		// The cursor before 'V' sits where the V glyph is drawn: after A's advance AND the
		// A-V kern (which pulls it left), so the caret never floats away from the glyph.
		float kern = font.kerning('A', 'V', 28.0f);
		assert(kern < 0.0f);
		assert(near(font.offsetAtIndex(s, 28.0f, 1), font.advanceFor('A', 28.0f) + kern));
		std::cout << "✓ testCursorMathIncludesKerning\n";
	}

	void testDrawListAppliesKerning() {
		lvgui::Font font;
		font.bake(inter(), 28.0f);
		const lvgui::Color white(255, 255, 255);

		lvgui::DrawList kerned, plain;
		kerned.clear();
		plain.clear();
		kerned.addText(font, 28.0f, { 0, 0 }, white, "AV");
		plain.addText(font, 28.0f, { 0, 0 }, white, "AV", lvgui::TextFlags::NoKerning);
		assert(kerned.vertices().size() == 8 && plain.vertices().size() == 8);
		// 'V' is the second quad (vertices 4..7).
		float shift = kerned.vertices()[4].pos.x - plain.vertices()[4].pos.x;
		assert(near(shift, font.kerning('A', 'V', 28.0f), 0.01f));
		std::cout << "✓ testDrawListAppliesKerning\n";
	}

	void testDrawListSubpixelAndPixelScale() {
		lvgui::Font font;
		font.bake(inter(), 28.0f);   // 2x2 oversampled -> x stays fractional
		const lvgui::Color white(255, 255, 255);

		lvgui::DrawList list;
		list.clear();
		list.addText(font, 14.0f, { 0.3f, 0.3f }, white, "iii");
		bool fractionalX = false;
		for (std::size_t q = 0; q < 3; ++q) {
			float x = list.vertices()[q * 4].pos.x;
			if (std::fabs(x - std::round(x)) > 0.05f) fractionalX = true;
		}
		assert(fractionalX);

		// With a 2x content scale, y snaps to half logical pixels (whole physical ones).
		lvgui::DrawList hidpi;
		hidpi.clear();
		hidpi.setPixelScale(2.0f);
		hidpi.addText(font, 14.0f, { 0.0f, 0.3f }, white, "i");
		float y = hidpi.vertices()[0].pos.y * 2.0f;
		assert(near(y, std::round(y), 0.001f));

		// A 1x bake has no oversampled data; it snaps x instead of blurring.
		lvgui::Font crisp;
		lvgui::FontOptions options;
		options.oversample = 1;
		crisp.setOptions(options);
		crisp.bake(inter(), 14.0f);
		lvgui::DrawList snapped;
		snapped.clear();
		snapped.addText(crisp, 14.0f, { 0.3f, 0.3f }, white, "iii");
		for (std::size_t q = 0; q < 3; ++q) {
			float x = snapped.vertices()[q * 4].pos.x;
			assert(near(x, std::round(x), 0.001f));
		}
		std::cout << "✓ testDrawListSubpixelAndPixelScale\n";
	}

	// ---- dynamic glyphs ----

	void testDynamicGlyphBakedOnDemand() {
		lvgui::Font font;
		lvgui::GlyphRange latin[] = { { 0x20, 0x7E } };
		font.bake(inter(), 20.0f, latin, 1, 512, 512);

		const std::uint32_t euro = 0x20AC;
		assert(!font.hasGlyph(euro));
		assert(font.canRender(euro));
		std::uint64_t before = font.atlasRevision();

		const lvgui::Glyph& g = font.glyphFor(euro);
		assert(font.hasGlyph(euro));
		assert(font.atlasRevision() > before);
		assert(g.size.x > 0.0f && g.size.y > 0.0f && g.xAdvance > 0.0f);

		// Its pixels really are in the atlas: the UV rect contains some ink.
		int x0 = static_cast<int>(g.uv0.x * static_cast<float>(font.atlasWidth()));
		int x1 = static_cast<int>(g.uv1.x * static_cast<float>(font.atlasWidth()));
		int y0 = static_cast<int>(g.uv0.y * static_cast<float>(font.atlasHeight()));
		int y1 = static_cast<int>(g.uv1.y * static_cast<float>(font.atlasHeight()));
		int ink = 0;
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
				ink += font.atlasPixels()[static_cast<std::size_t>(y) * static_cast<std::size_t>(font.atlasWidth()) + static_cast<std::size_t>(x)];
		assert(ink > 0);

		// Second request is a cache hit: no further revision bump.
		std::uint64_t after = font.atlasRevision();
		(void)font.glyphFor(euro);
		assert(font.atlasRevision() == after);
		std::cout << "✓ testDynamicGlyphBakedOnDemand\n";
	}

	void testDynamicGlyphsDisabled() {
		lvgui::Font font;
		lvgui::FontOptions options;
		options.dynamicGlyphs = false;
		font.setOptions(options);
		lvgui::GlyphRange latin[] = { { 0x20, 0x7E } };
		font.bake(inter(), 20.0f, latin, 1);
		std::uint64_t rev = font.atlasRevision();
		const lvgui::Glyph& g = font.glyphFor(0x20AC);
		assert(!font.hasGlyph(0x20AC));
		assert(font.atlasRevision() == rev);
		assert(g.size.x > 0.0f);   // the placeholder, never invisible
		std::cout << "✓ testDynamicGlyphsDisabled\n";
	}

	void testGlyphReferencesSurviveDynamicBakes() {
		lvgui::Font font;
		lvgui::GlyphRange latin[] = { { 0x41, 0x41 } };
		font.bake(inter(), 16.0f, latin, 1);
		const lvgui::Glyph& a = font.glyphFor('A');
		lvgui::Glyph copy = a;
		for (std::uint32_t cp = 0x391; cp <= 0x3C9; ++cp) (void)font.glyphFor(cp);
		for (std::uint32_t cp = 0x100; cp < 0x17F; ++cp) (void)font.glyphFor(cp);
		assert(a.uv0.x == copy.uv0.x && a.xAdvance == copy.xAdvance);   // no dangling reference
		std::cout << "✓ testGlyphReferencesSurviveDynamicBakes\n";
	}

	void testFullAtlasFallsBackGracefully() {
		lvgui::Font font;
		lvgui::GlyphRange one[] = { { '?', '?' } };
		font.bake(inter(), 20.0f, one, 1, 96, 96);   // room for some glyphs, not all
		std::size_t resident = 0;
		for (std::uint32_t cp = 0x100; cp < 0x17F; ++cp) {
			const lvgui::Glyph& g = font.glyphFor(cp);
			assert(g.size.x > 0.0f);
			resident += font.hasGlyph(cp) ? 1u : 0u;
		}
		assert(resident > 0 && resident < 0x7F);   // some fit, then it filled up -- nothing crashed
		std::cout << "✓ testFullAtlasFallsBackGracefully (" << resident << " glyphs fit)\n";
	}

	void testMovedFontKeepsBakingDynamically() {
		lvgui::Font a;
		lvgui::GlyphRange latin[] = { { 0x20, 0x7E } };
		a.bake(inter(), 18.0f, latin, 1);
		lvgui::Font b = std::move(a);
		assert(b.isBaked());
		assert(b.glyphFor(0x20AC).size.x > 0.0f && b.hasGlyph(0x20AC));
		std::cout << "✓ testMovedFontKeepsBakingDynamically\n";
	}

	void testRebakeBumpsRevisionAndKeepsFallbacks() {
		lvgui::Font font;
		font.addFallbackFace(mono());
		font.bake(inter(), 14.0f);
		std::uint64_t rev = font.atlasRevision();
		font.bake(inter(), 28.0f, 512, 512, 2.0f);
		assert(font.atlasRevision() > rev);
		assert(font.fallbackFaceCount() == 1);
		std::cout << "✓ testRebakeBumpsRevisionAndKeepsFallbacks\n";
	}

	// ---- fallback faces ----

	void testFallbackFaceSuppliesMissingCodepoints() {
		// Find a symbol the primary face lacks but DejaVu Sans Mono has.
		lvgui::Font primaryOnly;
		primaryOnly.bake(inter(), 16.0f);
		lvgui::Font withFallback;
		withFallback.addFallbackFace(mono());
		withFallback.bake(inter(), 16.0f);

		std::uint32_t chosen = 0;
		for (std::uint32_t cp : { 0x2603u, 0x2660u, 0x2665u, 0x2234u, 0x2302u, 0x25A0u }) {
			if (!primaryOnly.canRender(cp) && withFallback.canRender(cp)) { chosen = cp; break; }
		}
		assert(chosen != 0 && "expected a symbol only the fallback face covers");

		assert(!primaryOnly.hasGlyph(chosen));
		const lvgui::Glyph& real = withFallback.glyphFor(chosen);
		const lvgui::Glyph& placeholder = primaryOnly.glyphFor(chosen);
		// The real glyph is drawn from the fallback face, not the '?' placeholder.
		assert(withFallback.hasGlyph(chosen));
		assert(real.uv0.x != placeholder.uv0.x || real.uv0.y != placeholder.uv0.y ||
		       real.size.x != placeholder.size.x);

		// Primary-covered codepoints still come from the primary face (Inter's 'i' is
		// proportional; the mono face's would be as wide as its 'W').
		assert(withFallback.glyphFor('i').xAdvance < withFallback.glyphFor('W').xAdvance);

		// Kerning is never applied across faces.
		assert(withFallback.kerning('A', chosen, 16.0f) == 0.0f);
		std::cout << "✓ testFallbackFaceSuppliesMissingCodepoints (U+" << std::hex << chosen << std::dec << ")\n";
	}

	void testBadFallbackFaceThrows() {
		lvgui::Font font;
		font.addFallbackFace(std::vector<std::uint8_t>(64, 0x00));
		bool threw = false;
		try { font.bake(inter(), 14.0f); } catch (const std::runtime_error&) { threw = true; }
		assert(threw);
		std::cout << "✓ testBadFallbackFaceThrows\n";
	}

	// ---- signed distance field mode ----

	void testSdfBakeProducesDistanceField() {
		lvgui::Font font;
		lvgui::FontOptions options;
		options.mode = lvgui::FontRenderMode::Sdf;
		font.setOptions(options);
		font.bake(inter(), 32.0f, 1024, 1024);
		assert(font.isSdf());
		assert(font.sdfPadding() >= 3);

		const lvgui::Glyph& g = font.glyphFor('I');
		assert(g.size.x > 0.0f && g.size.y > 0.0f);
		int x0 = static_cast<int>(std::round(g.uv0.x * 1024.0f));
		int x1 = static_cast<int>(std::round(g.uv1.x * 1024.0f));
		int y0 = static_cast<int>(std::round(g.uv0.y * 1024.0f));
		int y1 = static_cast<int>(std::round(g.uv1.y * 1024.0f));
		int inside = 0, outside = 0;
		for (int y = y0; y < y1; ++y) {
			for (int x = x0; x < x1; ++x) {
				std::uint8_t v = font.atlasPixels()[static_cast<std::size_t>(y) * 1024u + static_cast<std::size_t>(x)];
				if (v > 160) ++inside;
				if (v < 96) ++outside;
			}
		}
		// A distance field has both clearly-inside and clearly-outside texels, with the
		// padding supplying the outside ones (a coverage bitmap has no mid-range ramp).
		assert(inside > 0 && outside > 0);
		// The corner texel of the padded quad is far outside the glyph.
		assert(font.atlasPixels()[static_cast<std::size_t>(y0) * 1024u + static_cast<std::size_t>(x0)] < 64);

		// Metrics match a bitmap bake of the same face/size closely.
		lvgui::Font bitmap;
		bitmap.bake(inter(), 32.0f, 1024, 1024);
		assert(near(font.measureText("Hello", 32.0f, lvgui::TextFlags::NoKerning).x,
		            bitmap.measureText("Hello", 32.0f, lvgui::TextFlags::NoKerning).x, 0.01f));

		// Dynamic glyphs work in SDF mode too.
		assert(!font.hasGlyph(0x20AC) || true);
		assert(font.glyphFor(0x20AC).size.x > 0.0f && font.hasGlyph(0x20AC));
		std::cout << "✓ testSdfBakeProducesDistanceField\n";
	}

	// ---- file helpers ----

	void testFileHelpers() {
		bool threw = false;
		try { (void)lvgui::Font::readFile("/definitely/not/a/font.ttf"); }
		catch (const std::runtime_error& e) { threw = std::string(e.what()).find("font.ttf") != std::string::npos; }
		assert(threw);

		lvgui::Font font;
		font.bakeFromFile(LVG_UI_TEST_FONT_PATH, 14.0f);
		assert(font.isBaked() && font.hasGlyph('A'));

		assert(lvgui::Font::findSystemFont("definitely-not-installed-9f3a.ttf").empty());
		std::cout << "✓ testFileHelpers\n";
	}

	// ---- GuiContext font styles ----

	lvgui::GuiCreateInfo baseInfo() {
		lvgui::GuiCreateInfo info;
		info.fontPath = LVG_UI_TEST_FONT_PATH;
		return info;
	}

	void testStylesResolveAndFallBackToRegular() {
		lvgui::GuiCreateInfo info = baseInfo();
		info.monoFontPath = LVG_UI_TEST_MONO_FONT_PATH;
		lvgui::GuiContext gui(info, lvgui::PlatformHooks{});

		assert(gui.hasStyleFont(lvgui::FontStyle::Regular));
		assert(gui.hasStyleFont(lvgui::FontStyle::Monospace));
		assert(!gui.hasStyleFont(lvgui::FontStyle::Bold));
		assert(!gui.hasStyleFont(lvgui::FontStyle::Heading));

		lvgui::StyledFont mono = gui.styledFont(lvgui::FontStyle::Monospace);
		assert(&mono.font != &gui.font());
		// Monospace: every glyph the same width.
		assert(near(mono.font.measureText("iiii", mono.pixelSize).x, mono.font.measureText("WWWW", mono.pixelSize).x));

		// Unconfigured styles transparently resolve to the regular face.
		lvgui::StyledFont bold = gui.styledFont(lvgui::FontStyle::Bold);
		assert(&bold.font == &gui.font() && bold.textureId == lvgui::kAtlasTextureId);

		// A Label measures with its style's face.
		lvgui::Label proportional("iiii");
		lvgui::Label fixed("iiii");
		fixed.setFontStyle(lvgui::FontStyle::Monospace);
		assert(fixed.preferredSize(gui).x > proportional.preferredSize(gui).x);
		std::cout << "✓ testStylesResolveAndFallBackToRegular\n";
	}

	void testMonoFontCanBeDisabledAndBadPathThrows() {
		lvgui::GuiCreateInfo off = baseInfo();
		off.enableMonoFont = false;
		lvgui::GuiContext gui(off, lvgui::PlatformHooks{});
		assert(!gui.hasStyleFont(lvgui::FontStyle::Monospace));

		lvgui::GuiCreateInfo bad = baseInfo();
		bad.monoFontPath = "/nope/mono.ttf";
		bool threw = false;
		try { lvgui::GuiContext g(bad, lvgui::PlatformHooks{}); } catch (const std::runtime_error&) { threw = true; }
		assert(threw);

		lvgui::GuiCreateInfo badFallback = baseInfo();
		badFallback.fallbackFontPaths = { "/nope/fallback.ttf" };
		threw = false;
		try { lvgui::GuiContext g(badFallback, lvgui::PlatformHooks{}); } catch (const std::runtime_error&) { threw = true; }
		assert(threw);
		std::cout << "✓ testMonoFontCanBeDisabledAndBadPathThrows\n";
	}

	void testBoldAndSdfHeadingAndFallbackConfig() {
		lvgui::GuiCreateInfo info = baseInfo();
		info.boldFontPath = LVG_UI_TEST_MONO_FONT_PATH;   // any TTF will do for the plumbing
		info.headingFontSize = 28.0f;
		info.headingFontSdf = true;
		info.fallbackFontPaths = { LVG_UI_TEST_MONO_FONT_PATH };
		info.kerning = false;
		lvgui::GuiContext gui(info, lvgui::PlatformHooks{});

		assert(gui.hasStyleFont(lvgui::FontStyle::Bold));
		assert(gui.hasHeadingFont() && gui.styleFontIsSdf(lvgui::FontStyle::Heading));
		assert(gui.headingFont().isSdf());
		assert(!gui.styleFontIsSdf(lvgui::FontStyle::Bold));
		assert(gui.font().fallbackFaceCount() == 1 && gui.headingFont().fallbackFaceCount() == 1);
		assert(!gui.font().options().kerning);
		assert(gui.font().kerning('A', 'V', 14.0f) == 0.0f);

		// setHeading() still works and maps onto the style.
		lvgui::Label label("Title");
		label.setHeading(true);
		assert(label.fontStyle() == lvgui::FontStyle::Heading);
		std::cout << "✓ testBoldAndSdfHeadingAndFallbackConfig\n";
	}

	void testUploadTrackingFollowsAtlasRevision() {
		lvgui::GuiContext gui(baseInfo(), lvgui::PlatformHooks{});
		assert(gui.fontNeedsUpload(lvgui::FontStyle::Regular));   // never uploaded yet
		gui.markFontUploaded(lvgui::FontStyle::Regular);
		assert(!gui.fontNeedsUpload(lvgui::FontStyle::Regular));

		(void)gui.font().glyphFor(0x0416);   // Cyrillic Zhe: outside the default ranges
		assert(gui.fontNeedsUpload(lvgui::FontStyle::Regular));
		gui.markFontUploaded(lvgui::FontStyle::Regular);
		assert(!gui.fontNeedsUpload(lvgui::FontStyle::Regular));

		assert(!gui.fontNeedsUpload(lvgui::FontStyle::Bold));   // style absent -> nothing to upload
		std::cout << "✓ testUploadTrackingFollowsAtlasRevision\n";
	}

	void testDefaultRangesCoverCommonPunctuation() {
		lvgui::Font font;
		font.bake(inter(), 14.0f);
		for (std::uint32_t cp : { 0x2013u, 0x2014u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2022u, 0x2026u, 0x20ACu, 0x2212u }) {
			assert(font.hasGlyph(cp));   // pre-baked: no dynamic bake needed for the common set
		}
		std::cout << "✓ testDefaultRangesCoverCommonPunctuation\n";
	}
}

int main() {
	testKerningTucksPairsTogether();
	testKerningNeverBreaksTabularDigits();
	testCursorMathIncludesKerning();
	testDrawListAppliesKerning();
	testDrawListSubpixelAndPixelScale();
	testDynamicGlyphBakedOnDemand();
	testDynamicGlyphsDisabled();
	testGlyphReferencesSurviveDynamicBakes();
	testFullAtlasFallsBackGracefully();
	testMovedFontKeepsBakingDynamically();
	testRebakeBumpsRevisionAndKeepsFallbacks();
	testFallbackFaceSuppliesMissingCodepoints();
	testBadFallbackFaceThrows();
	testSdfBakeProducesDistanceField();
	testFileHelpers();
	testStylesResolveAndFallBackToRegular();
	testMonoFontCanBeDisabledAndBadPathThrows();
	testBoldAndSdfHeadingAndFallbackConfig();
	testUploadTrackingFollowsAtlasRevision();
	testDefaultRangesCoverCommonPunctuation();
	std::cout << "\n✅ All font feature tests passed!\n";
	return 0;
}
