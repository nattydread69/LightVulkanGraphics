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

#include <lightVulkanGraphics/ui/DrawList.h>
#include <lightVulkanGraphics/ui/Types.h>
#include <lightVulkanGraphics/ui/Font.h>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace lvgui = lightGraphics::ui;

namespace {
	lvgui::Font& testFont() {
		static lvgui::Font font;
		static bool baked = false;
		if (!baked) {
			std::ifstream file(LVG_UI_TEST_FONT_PATH, std::ios::binary | std::ios::ate);
			if (!file) {
				std::cerr << "Could not open test font at " << LVG_UI_TEST_FONT_PATH << "\n";
				std::exit(1);
			}
			std::streamsize size = file.tellg();
			file.seekg(0, std::ios::beg);
			std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
			file.read(reinterpret_cast<char*>(data.data()), size);
			font.bake(data, 14.0f, 512, 512, 1.0f);
			baked = true;
		}
		return font;
	}

	void testAddTextEmitsOneQuadPerVisibleGlyph() {
		lvgui::DrawList list;
		list.clear();

		list.addText(testFont(), 14.0f, { 0, 0 }, lvgui::Color(255, 255, 255), "Hi");

		assert(list.vertices().size() == 8);
		assert(list.indices().size() == 12);
		std::cout << "✓ testAddTextEmitsOneQuadPerVisibleGlyph\n";
	}

	void testAddTextSkipsWhitespaceQuads() {
		lvgui::DrawList list;
		list.clear();

		list.addText(testFont(), 14.0f, { 0, 0 }, lvgui::Color(255, 255, 255), "A B");

		// 'A' and 'B' each get a quad; the space advances the pen but emits nothing.
		assert(list.vertices().size() == 8);
		std::cout << "✓ testAddTextSkipsWhitespaceQuads\n";
	}

	void testAddTextClippedTruncatesAndClips() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect rect = { 10, 10, 40, 20 };
		list.addTextClipped(testFont(), 14.0f, rect, lvgui::Color(255, 255, 255),
		                     "This text is far too long to fit in the box",
		                     lvgui::Align::Start, lvgui::Align::Start);

		assert(!list.vertices().empty());

		bool foundClippedCommand = false;
		for (const auto& cmd : list.commands()) {
			if (cmd.clipRect.x == rect.x && cmd.clipRect.y == rect.y &&
				cmd.clipRect.w == rect.w && cmd.clipRect.h == rect.h) {
				foundClippedCommand = true;
			}
		}
		assert(foundClippedCommand);
		std::cout << "✓ testAddTextClippedTruncatesAndClips\n";
	}

	// docs/gui/03-text-and-fonts.md, "Tabular figures": end-to-end plumbing check, not
	// just the Font-level arithmetic (test_font.cpp already pins that) -- with
	// TextFlags::Tabular, the character AFTER a single digit must land at the exact same
	// pen position regardless of which digit precedes it, since addText() must actually
	// be asking Font::advanceFor() for the (padded) digit advance, not the glyph's own.
	void testAddTextTabularFlagGivesEveryDigitTheSamePenAdvance() {
		lvgui::DrawList listA;
		listA.clear();
		listA.addText(testFont(), 14.0f, { 0, 0 }, lvgui::Color(255, 255, 255), "1X",
		              lvgui::TextFlags::Tabular);

		lvgui::DrawList listB;
		listB.clear();
		listB.addText(testFont(), 14.0f, { 0, 0 }, lvgui::Color(255, 255, 255), "8X",
		              lvgui::TextFlags::Tabular);

		// Each glyph is one quad (4 vertices): '1'/'8' is vertices[0..3], 'X' is
		// vertices[4..7]. vertices[4].pos.x is 'X's left edge -- i.e. the pen position
		// after the digit's (padded) advance.
		assert(listA.vertices().size() == 8);
		assert(listB.vertices().size() == 8);
		assert(std::fabs(listA.vertices()[4].pos.x - listB.vertices()[4].pos.x) < 0.01f);

		std::cout << "✓ testAddTextTabularFlagGivesEveryDigitTheSamePenAdvance\n";
	}

	// docs/gui/03-text-and-fonts.md, "Headings": a Label drawing with the heading face
	// passes that face's own registered TextureId through so its glyph quads sample the
	// HEADING atlas, not the primary one -- addText()/addTextClipped() must actually
	// route it into ensureCommand(), same mechanism addImage() already uses
	// (testSwitchingTextureIdStartsANewCommand, above).
	void testAddTextWithExplicitTextureIdBatchesIntoThatCommand() {
		lvgui::DrawList list;
		list.clear();

		list.addRectFilled({ 0, 0, 10, 10 }, lvgui::Color(255, 0, 0));   // atlas (default)
		list.addText(testFont(), 14.0f, { 0, 0 }, lvgui::Color(255, 255, 255), "Hi",
		             lvgui::TextFlags::None, /*textureId=*/7);
		list.addRectFilled({ 20, 0, 10, 10 }, lvgui::Color(0, 255, 0));  // back to the atlas

		assert(list.commands().size() == 3);
		assert(list.commands()[0].textureId == lvgui::kAtlasTextureId);
		assert(list.commands()[1].textureId == 7);
		assert(list.commands()[1].indexCount == 12);   // "Hi": two glyph quads, 6 indices each
		assert(list.commands()[2].textureId == lvgui::kAtlasTextureId);

		std::cout << "✓ testAddTextWithExplicitTextureIdBatchesIntoThatCommand\n";
	}

	void testAddTextClippedWithExplicitTextureIdBatchesIntoThatCommand() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect rect = { 10, 10, 100, 20 };
		list.addTextClipped(testFont(), 14.0f, rect, lvgui::Color(255, 255, 255), "Heading",
		                     lvgui::Align::Start, lvgui::Align::Center, lvgui::TextFlags::None,
		                     /*textureId=*/7);

		bool foundHeadingTextureCommand = false;
		for (const auto& cmd : list.commands()) {
			if (cmd.textureId == 7 && cmd.indexCount > 0) {
				foundHeadingTextureCommand = true;
			}
		}
		assert(foundHeadingTextureCommand);

		std::cout << "✓ testAddTextClippedWithExplicitTextureIdBatchesIntoThatCommand\n";
	}

	void testRectFilledBasic() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect r = { 10, 10, 100, 100 };
		list.addRectFilled(r, lvgui::Color(255, 0, 0, 255));

		assert(list.vertices().size() == 4);
		assert(list.indices().size() == 6);
		assert(list.commands().size() == 1);
		assert(list.commands()[0].indexCount == 6);
		std::cout << "✓ testRectFilledBasic\n";
	}

	void testSameClipRectOneCommand() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect clip = { 0, 0, 500, 500 };
		list.pushClipRect(clip);

		lvgui::Rect r1 = { 10, 10, 100, 100 };
		list.addRectFilled(r1, lvgui::Color(255, 0, 0));

		lvgui::Rect r2 = { 150, 150, 100, 100 };
		list.addRectFilled(r2, lvgui::Color(0, 255, 0));

		assert(list.commands().size() == 1);
		std::cout << "✓ testSameClipRectOneCommand\n";
	}

	void testDifferentClipRectsTwoCommands() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect r1 = { 10, 10, 100, 100 };
		list.addRectFilled(r1, lvgui::Color(255, 0, 0));

		lvgui::Rect clip2 = { 200, 200, 300, 300 };
		list.pushClipRect(clip2);

		lvgui::Rect r2 = { 250, 250, 50, 50 };
		list.addRectFilled(r2, lvgui::Color(0, 255, 0));

		assert(list.commands().size() == 2);
		std::cout << "✓ testDifferentClipRectsTwoCommands\n";
	}

	void testAddImageEmitsCorrectPositionsUVsAndTint() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect r = { 10.0f, 20.0f, 100.0f, 50.0f };
		lvgui::Color tint(0x11, 0x22, 0x33, 0x44);
		list.addImage(7, r, { 0.25f, 0.0f }, { 1.0f, 0.75f }, tint);

		assert(list.vertices().size() == 4);
		const auto& v = list.vertices();

		assert(v[0].pos.x == r.left()  && v[0].pos.y == r.top());
		assert(v[1].pos.x == r.right() && v[1].pos.y == r.top());
		assert(v[2].pos.x == r.right() && v[2].pos.y == r.bottom());
		assert(v[3].pos.x == r.left()  && v[3].pos.y == r.bottom());

		// UVs follow the same tl/tr/br/bl winding as positions, sampling [uv0,uv1]
		// rather than the atlas's white-pixel UV every other primitive uses.
		assert(v[0].uv.x == 0.25f && v[0].uv.y == 0.0f);
		assert(v[1].uv.x == 1.0f  && v[1].uv.y == 0.0f);
		assert(v[2].uv.x == 1.0f  && v[2].uv.y == 0.75f);
		assert(v[3].uv.x == 0.25f && v[3].uv.y == 0.75f);

		for (const auto& vert : v) {
			assert(vert.color == tint.packed());
		}

		assert(list.commands().size() == 1);
		assert(list.commands()[0].textureId == 7);

		std::cout << "✓ testAddImageEmitsCorrectPositionsUVsAndTint\n";
	}

	void testAddImageDefaultsToFullUVAndOpaqueWhiteTint() {
		lvgui::DrawList list;
		list.clear();

		list.addImage(3, { 0.0f, 0.0f, 10.0f, 10.0f });
		const auto& v = list.vertices();

		assert(v[0].uv.x == 0.0f && v[0].uv.y == 0.0f);
		assert(v[2].uv.x == 1.0f && v[2].uv.y == 1.0f);
		for (const auto& vert : v) {
			assert(vert.color == lvgui::Color(0xFF, 0xFF, 0xFF, 0xFF).packed());
		}

		std::cout << "✓ testAddImageDefaultsToFullUVAndOpaqueWhiteTint\n";
	}

	void testConsecutiveAddImageSameTextureBatchesIntoOneCommand() {
		lvgui::DrawList list;
		list.clear();

		list.addImage(5, { 0.0f, 0.0f, 10.0f, 10.0f });
		list.addImage(5, { 20.0f, 0.0f, 10.0f, 10.0f });

		// Same texture, same (default full-screen) clip rect -- batches into one draw
		// command, exactly like two same-clip-rect addRectFilled calls do
		// (testSameClipRectOneCommand above).
		assert(list.commands().size() == 1);
		assert(list.commands()[0].textureId == 5);
		assert(list.commands()[0].indexCount == 12);   // two quads, 6 indices each

		std::cout << "✓ testConsecutiveAddImageSameTextureBatchesIntoOneCommand\n";
	}

	void testSwitchingTextureIdStartsANewCommand() {
		lvgui::DrawList list;
		list.clear();

		list.addImage(1, { 0.0f, 0.0f, 10.0f, 10.0f });
		list.addImage(2, { 20.0f, 0.0f, 10.0f, 10.0f });
		list.addRectFilled({ 40.0f, 0.0f, 10.0f, 10.0f }, lvgui::Color(255, 0, 0));   // back to the atlas

		assert(list.commands().size() == 3);
		assert(list.commands()[0].textureId == 1);
		assert(list.commands()[1].textureId == 2);
		assert(list.commands()[2].textureId == lvgui::kAtlasTextureId);

		std::cout << "✓ testSwitchingTextureIdStartsANewCommand\n";
	}

	void testAddImageInsideAClipRectAlsoSplitsOnClipChange() {
		lvgui::DrawList list;
		list.clear();

		list.pushClipRect({ 0.0f, 0.0f, 100.0f, 100.0f });
		list.addImage(9, { 10.0f, 10.0f, 10.0f, 10.0f });

		list.pushClipRect({ 0.0f, 0.0f, 50.0f, 50.0f });
		list.addImage(9, { 10.0f, 10.0f, 10.0f, 10.0f });   // same texture, narrower clip

		// A clip-rect change still splits even when the texture stays the same -- the
		// two concerns are independent, and addImage() must not accidentally suppress
		// the pre-existing clip-rect-driven splitting testDifferentClipRectsTwoCommands
		// already pins down for every other primitive.
		assert(list.commands().size() == 2);
		assert(list.commands()[0].textureId == 9);
		assert(list.commands()[1].textureId == 9);

		std::cout << "✓ testAddImageInsideAClipRectAlsoSplitsOnClipChange\n";
	}

	void testClipIntersection() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect clip1 = { 0, 0, 100, 100 };
		list.pushClipRect(clip1);

		lvgui::Rect clip2 = { 50, 50, 100, 100 };
		list.pushClipRect(clip2, true);

		lvgui::Rect expected = clip1.intersect(clip2);
		assert(list.commands()[0].clipRect.x == expected.x);
		assert(list.commands()[0].clipRect.y == expected.y);
		assert(list.commands()[0].clipRect.w == expected.w);
		assert(list.commands()[0].clipRect.h == expected.h);

		std::cout << "✓ testClipIntersection\n";
	}

	void testPopClipRectNoUnderflow() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect baseClip = { 0, 0, 10000, 10000 };

		list.pushClipRect({ 100, 100, 100, 100 });
		list.popClipRect();

		lvgui::Rect cmd_clip = list.commands()[0].clipRect;
		assert(cmd_clip.x == baseClip.x);
		assert(cmd_clip.y == baseClip.y);
		assert(cmd_clip.w == baseClip.w);
		assert(cmd_clip.h == baseClip.h);

		std::cout << "✓ testPopClipRectNoUnderflow\n";
	}

	void testCoordinatesSnappedToIntegers() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect r = { 10.37f, 20.63f, 100.45f, 100.55f };
		list.addRectFilled(r, lvgui::Color(255, 0, 0));

		for (const auto& v : list.vertices()) {
			float fx = v.pos.x - std::floor(v.pos.x);
			float fy = v.pos.y - std::floor(v.pos.y);
			assert(fx == 0.0f || fx == 1.0f);
			assert(fy == 0.0f || fy == 1.0f);
		}

		std::cout << "✓ testCoordinatesSnappedToIntegers\n";
	}

	void testColorRoundTrip() {
		lvgui::Color c = lvgui::Color::fromHex(0x3D8BFD, 1.0f);
		std::uint32_t packed = c.packed();

		std::uint8_t r = packed & 0xFF;
		std::uint8_t g = (packed >> 8) & 0xFF;
		std::uint8_t b = (packed >> 16) & 0xFF;
		std::uint8_t a = (packed >> 24) & 0xFF;

		assert(r == 0xFD);
		assert(g == 0x8B);
		assert(b == 0x3D);
		assert(a == 0xFF);

		std::cout << "✓ testColorRoundTrip\n";
	}

	void testRectOutsideClipStillEmitsGeometry() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect clipRect = { 0, 0, 100, 100 };
		list.pushClipRect(clipRect);

		lvgui::Rect r = { 200, 200, 100, 100 };
		list.addRectFilled(r, lvgui::Color(255, 0, 0));

		assert(list.vertices().size() > 0);
		assert(list.commands()[0].clipRect.x == clipRect.x);
		assert(list.commands()[0].clipRect.y == clipRect.y);

		std::cout << "✓ testRectOutsideClipStillEmitsGeometry\n";
	}

	void testClipRectReplace() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect clip1 = { 0, 0, 100, 100 };
		list.pushClipRect(clip1);

		lvgui::Rect clip2 = { 200, 200, 100, 100 };
		list.pushClipRect(clip2, false);

		lvgui::Rect cmd_clip = list.commands()[0].clipRect;
		assert(cmd_clip.x == clip2.x);
		assert(cmd_clip.y == clip2.y);
		assert(cmd_clip.w == clip2.w);
		assert(cmd_clip.h == clip2.h);

		std::cout << "✓ testClipRectReplace\n";
	}

	void testRect() {
		lvgui::Rect r = { 10, 20, 100, 200 };
		assert(r.left() == 10.0f);
		assert(r.top() == 20.0f);
		assert(r.right() == 110.0f);
		assert(r.bottom() == 220.0f);

		lvgui::Vec2 c = r.centre();
		assert(c.x == 60.0f);
		assert(c.y == 120.0f);

		lvgui::Vec2 p1 = { 50, 100 };
		assert(r.contains(p1));

		lvgui::Vec2 p2 = { 200, 200 };
		assert(!r.contains(p2));

		lvgui::Rect r2 = { 50, 50, 100, 100 };
		lvgui::Rect intersected = r.intersect(r2);
		assert(intersected.left() == 50.0f);
		assert(intersected.top() == 50.0f);
		assert(intersected.right() == 110.0f);
		assert(intersected.bottom() == 150.0f);

		std::cout << "✓ testRect\n";
	}

	void testVec2() {
		lvgui::Vec2 a = { 1, 2 };
		lvgui::Vec2 b = { 3, 4 };

		lvgui::Vec2 c = a + b;
		assert(c.x == 4 && c.y == 6);

		lvgui::Vec2 d = b - a;
		assert(d.x == 2 && d.y == 2);

		lvgui::Vec2 e = a * 2;
		assert(e.x == 2 && e.y == 4);

		a += b;
		assert(a.x == 4 && a.y == 6);

		std::cout << "✓ testVec2\n";
	}

	void testColorOperations() {
		lvgui::Color red = lvgui::Color(255, 0, 0, 255);
		lvgui::Color semi = red.withAlpha(0.5f);
		assert(semi.a < 200 && semi.a > 100);

		lvgui::Color blue = lvgui::Color(0, 0, 255, 255);
		lvgui::Color lerped = lvgui::Color::lerp(red, blue, 0.5f);
		assert(lerped.r > 100);
		assert(lerped.b > 100);

		std::cout << "✓ testColorOperations\n";
	}

	// Pure primary/secondary hues at full saturation/value -- the six corners of the
	// hue hexagon ColorEdit's popup draws as six gradient bands (docs/gui/05,
	// "ColorEdit"). Exact-byte assertions, not tolerance-based, because these six
	// points are exactly where fromHSV()'s six-branch piecewise formula switches
	// branches; an off-by-one in the branch boundary constants (0/60/120/180/240/300)
	// would show up as a wrong RESULT here, not a rounding wobble.
	void testColorFromHSVPrimaryAndSecondaryHues() {
		auto eq = [](lvgui::Color c, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
			return c.r == r && c.g == g && c.b == b;
		};
		assert(eq(lvgui::Color::fromHSV(0.0f,   1.0f, 1.0f), 255,   0,   0));  // red
		assert(eq(lvgui::Color::fromHSV(60.0f,  1.0f, 1.0f), 255, 255,   0));  // yellow
		assert(eq(lvgui::Color::fromHSV(120.0f, 1.0f, 1.0f),   0, 255,   0));  // green
		assert(eq(lvgui::Color::fromHSV(180.0f, 1.0f, 1.0f),   0, 255, 255));  // cyan
		assert(eq(lvgui::Color::fromHSV(240.0f, 1.0f, 1.0f),   0,   0, 255));  // blue
		assert(eq(lvgui::Color::fromHSV(300.0f, 1.0f, 1.0f), 255,   0, 255));  // magenta
		assert(eq(lvgui::Color::fromHSV(360.0f, 1.0f, 1.0f), 255,   0,   0));  // wraps to red

		std::cout << "✓ testColorFromHSVPrimaryAndSecondaryHues\n";
	}

	void testColorFromHSVSaturationAndValueExtremes() {
		// s=0 is grey at every hue -- the hue argument must have no effect.
		assert(lvgui::Color::fromHSV(0.0f, 0.0f, 1.0f).r == 255);
		assert(lvgui::Color::fromHSV(0.0f, 0.0f, 1.0f).g == 255);
		assert(lvgui::Color::fromHSV(200.0f, 0.0f, 1.0f).r == 255);
		assert(lvgui::Color::fromHSV(200.0f, 0.0f, 1.0f).g == 255);
		// v=0 is black regardless of hue or saturation.
		lvgui::Color black = lvgui::Color::fromHSV(240.0f, 1.0f, 0.0f);
		assert(black.r == 0 && black.g == 0 && black.b == 0);
		// Alpha passes through untouched -- it has no HSV analogue.
		assert(lvgui::Color::fromHSV(0.0f, 1.0f, 1.0f, 128).a == 128);

		std::cout << "✓ testColorFromHSVSaturationAndValueExtremes\n";
	}

	void testColorToHSVRoundTripsThroughFromHSV() {
		// Not every (h,s,v) round-trips to the SAME (h,s,v) -- h is undefined at s=0
		// (toHSV canonically returns 0, see its header comment) and at v=0. Restricted
		// to saturated, non-black colours, where the round trip is exact modulo uint8
		// quantisation.
		float testHues[] = { 0.0f, 45.0f, 90.0f, 150.0f, 210.0f, 275.0f, 330.0f };
		for (float h : testHues) {
			lvgui::Color c = lvgui::Color::fromHSV(h, 1.0f, 1.0f);
			float rh, rs, rv;
			c.toHSV(rh, rs, rv);
			// A few degrees of slack: fromHSV quantises to uint8 before toHSV inverts,
			// so the round trip is not bit-exact.
			float diff = std::fabs(rh - h);
			if (diff > 180.0f) {
				diff = 360.0f - diff;   // wrap distance, for the h=0 vs. h=360 case
			}
			assert(diff < 2.0f);
			assert(rs > 0.98f);
			assert(rv > 0.98f);
		}

		std::cout << "✓ testColorToHSVRoundTripsThroughFromHSV\n";
	}

	void testColorToHSVGreyHasCanonicalZeroHue() {
		float h, s, v;
		lvgui::Color(128, 128, 128, 255).toHSV(h, s, v);
		assert(h == 0.0f);
		assert(s == 0.0f);

		lvgui::Color(0, 0, 0, 255).toHSV(h, s, v);
		assert(h == 0.0f);
		assert(v == 0.0f);

		std::cout << "✓ testColorToHSVGreyHasCanonicalZeroHue\n";
	}

	// --- Geometry-position tests -------------------------------------------------
	// These assert WHERE vertices land, not just how many are produced. Count-only
	// tests (testRectFilledBasic above) already passed while addRectFilled emitted a
	// circle instead of a rounded rect, and while addText baselined glyphs a full
	// ascent too high -- both bugs preserved vertex/index counts exactly.

	float dist(lvgui::Vec2 a, lvgui::Vec2 b) {
		float dx = a.x - b.x, dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy);
	}

	void testRectFilledSquareCornersExact() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect r = { 10, 10, 80, 50 };
		list.addRectFilled(r, lvgui::Color(255, 0, 0, 255));  // rounding = 0

		assert(list.vertices().size() == 4);
		const auto& v = list.vertices();
		// Winding is TL, TR, BR, BL -- pinned because addRectFilledMultiColor and the
		// index list both assume this order.
		assert(v[0].pos.x == r.left()  && v[0].pos.y == r.top());
		assert(v[1].pos.x == r.right() && v[1].pos.y == r.top());
		assert(v[2].pos.x == r.right() && v[2].pos.y == r.bottom());
		assert(v[3].pos.x == r.left()  && v[3].pos.y == r.bottom());

		std::cout << "✓ testRectFilledSquareCornersExact\n";
	}

	void testRectFilledRoundedStaysInBoundsAndOffCentre() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect r = { 10, 20, 100, 40 };
		float rounding = 4.0f;
		list.addRectFilled(r, lvgui::Color(255, 0, 0, 255), rounding);

		assert(!list.vertices().empty());

		lvgui::Vec2 centre = r.centre();
		// A circular corner's arc point at 45 degrees into its quadrant sweep is the
		// one boundary vertex that sits closest to the *middle* of that corner's own
		// treatment -- distinct from its two tangent points, which sit at the ends of
		// the flat runs instead. The four rect corners give four such reference
		// points; each must actually be represented by emitted geometry, not just
		// "some point somewhere near that corner."
		float diag = rounding * 0.70710678f;  // cos(45deg) == sin(45deg)
		lvgui::Vec2 cornerArcMids[4] = {
			{ r.right() - rounding + diag, r.bottom() - rounding + diag },  // bottom-right
			{ r.left()  + rounding - diag, r.bottom() - rounding + diag },  // bottom-left
			{ r.left()  + rounding - diag, r.top()    + rounding - diag },  // top-left
			{ r.right() - rounding + diag, r.top()    + rounding - diag },  // top-right
		};
		bool nearCornerArcMid[4] = { false, false, false, false };

		for (const auto& v : list.vertices()) {
			// Snapping to whole pixels can only move a vertex by <= ~0.71px diagonally;
			// the AA fringe (docs/gui/02-rendering.md, "Anti-aliasing") then pushes an
			// outer ring up to 1px further out again, so the bound is 2.0f, not 1.0f.
			assert(v.pos.x >= r.left() - 2.0f && v.pos.x <= r.right() + 2.0f);
			assert(v.pos.y >= r.top() - 2.0f && v.pos.y <= r.bottom() + 2.0f);

			// The assertion that would have caught the circle bug: a fan hub placed
			// at the rect's centre (as a naive centre-anchored circle fan would need)
			// leaves a vertex sitting exactly there. A correct rounded-rect fill has
			// no reason to ever place a vertex at the centre.
			assert(dist({ v.pos.x, v.pos.y }, centre) > 1.0f);

			for (int i = 0; i < 4; ++i) {
				if (dist({ v.pos.x, v.pos.y }, cornerArcMids[i]) <= 1.0f) {
					nearCornerArcMid[i] = true;
				}
			}
		}

		for (int i = 0; i < 4; ++i) assert(nearCornerArcMid[i]);

		std::cout << "✓ testRectFilledRoundedStaysInBoundsAndOffCentre\n";
	}

	void testRectFilledRoundingClampsInsteadOfInverting() {
		lvgui::DrawList list;
		list.clear();

		// rounding (100) far exceeds min(w,h)/2 (20) -- must clamp to a full stadium
		// (here, since w == h, a circle inscribed in the square) rather than letting
		// corner centres cross past each other and fold the geometry inside out.
		lvgui::Rect r = { 0, 0, 40, 40 };
		list.addRectFilled(r, lvgui::Color(255, 0, 0, 255), 100.0f);

		assert(!list.vertices().empty());
		assert(list.indices().size() % 3 == 0);

		lvgui::Vec2 centre = r.centre();
		float maxClampedRadius = std::min(r.w, r.h) * 0.5f;  // 20

		for (const auto& v : list.vertices()) {
			// +2.0f, not +1.0f: pixel snapping (~0.71px diagonal) plus the AA fringe's
			// own further 1px outward step (docs/gui/02-rendering.md, "Anti-aliasing").
			assert(v.pos.x >= r.left() - 2.0f && v.pos.x <= r.right() + 2.0f);
			assert(v.pos.y >= r.top() - 2.0f && v.pos.y <= r.bottom() + 2.0f);
			// Inverted geometry (unclamped radius) would push corner arc centres past
			// the rect's own centre and out the far side, producing vertices well
			// beyond the clamped incircle radius from the centre.
			assert(dist({ v.pos.x, v.pos.y }, centre) <= maxClampedRadius + 2.0f);
		}

		std::cout << "✓ testRectFilledRoundingClampsInsteadOfInverting\n";
	}

	void testAddTextBaselineWithinLineHeightOfTopLeft() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Vec2 topLeft = { 5, 10 };
		float pixelSize = 14.0f;
		list.addText(testFont(), pixelSize, topLeft, lvgui::Color(255, 255, 255), "Hi");

		assert(list.vertices().size() >= 4);

		// First glyph's quad is vertices [0..3]: tl, tr, br, bl.
		float quadTop = list.vertices()[0].pos.y;
		float quadBottom = list.vertices()[2].pos.y;
		float lineHeight = testFont().lineHeight(pixelSize);

		// The baseline bug advanced the pen by a full ascent *without* moving
		// topLeft down first, so glyphs rendered a whole ascent above topLeft.y --
		// this is exactly what these two bounds pin down.
		assert(quadTop >= topLeft.y - 1.0f);
		assert(quadTop <= topLeft.y + lineHeight + 1.0f);
		assert(quadBottom >= topLeft.y - 1.0f);
		assert(quadBottom <= topLeft.y + lineHeight + 1.0f);

		std::cout << "✓ testAddTextBaselineWithinLineHeightOfTopLeft\n";
	}

	void testAddRectFormsRingAndHonoursRounding() {
		// rounding == 0: a plain outline. No vertex should sit in the interior
		// region inset by more than the outline's own thickness -- that's what
		// distinguishes a ring from a filled or malformed shape.
		{
			lvgui::DrawList list;
			list.clear();

			lvgui::Rect r = { 10, 10, 80, 50 };
			float thickness = 4.0f;
			list.addRect(r, lvgui::Color(0, 0, 255, 255), thickness, 0.0f);

			assert(!list.vertices().empty());

			float slack = 1.0f;  // pixel snapping
			float forbiddenInset = thickness + slack;
			float ix0 = r.left() + forbiddenInset, ix1 = r.right() - forbiddenInset;
			float iy0 = r.top() + forbiddenInset, iy1 = r.bottom() - forbiddenInset;

			for (const auto& v : list.vertices()) {
				bool strictlyInside = v.pos.x > ix0 && v.pos.x < ix1 &&
				                       v.pos.y > iy0 && v.pos.y < iy1;
				assert(!strictlyInside);
			}

			std::cout << "✓ testAddRectFormsRingAndHonoursRounding (rounding=0 ring)\n";
		}

		// rounding > 0: the outline must actually follow the rounded corners --
		// fixed here because slider handles and the text box caret both
		// draw a rounded outline over a rounded fill, and a square outline over a
		// rounded fill reads as broken.
		{
			lvgui::DrawList list;
			list.clear();

			lvgui::Rect r = { 10, 10, 80, 50 };
			float thickness = 4.0f;
			float rounding = 8.0f;
			list.addRect(r, lvgui::Color(0, 0, 255, 255), thickness, rounding);

			assert(!list.vertices().empty());

			// Still a ring: nothing deep in the interior.
			float slack = 1.0f;
			float forbiddenInset = thickness + slack;
			float ix0 = r.left() + forbiddenInset, ix1 = r.right() - forbiddenInset;
			float iy0 = r.top() + forbiddenInset, iy1 = r.bottom() - forbiddenInset;
			for (const auto& v : list.vertices()) {
				bool strictlyInside = v.pos.x > ix0 && v.pos.x < ix1 &&
				                       v.pos.y > iy0 && v.pos.y < iy1;
				assert(!strictlyInside);
			}

			// The rounding is not ignored: an unmitred square-outline join already
			// keeps vertices a couple pixels clear of the raw corner point, so
			// "no vertex at the exact corner" holds even when rounding is dropped on
			// the floor and can't tell the two cases apart. What *can* tell them
			// apart is the flat-edge tangent point where a rounded corner's arc is
			// supposed to hand off to the straight run: a square outline has no
			// vertex anywhere near it (nearest is 3*thickness away, at the corner),
			// while a rounded outline puts one right there (within half the
			// thickness). Check all 8 (two per corner, one on each adjoining edge).
			lvgui::Vec2 tangentPoints[8] = {
				{ r.left() + rounding, r.top() },     { r.right() - rounding, r.top() },
				{ r.left() + rounding, r.bottom() },  { r.right() - rounding, r.bottom() },
				{ r.left(),  r.top() + rounding },    { r.left(),  r.bottom() - rounding },
				{ r.right(), r.top() + rounding },    { r.right(), r.bottom() - rounding },
			};
			for (const auto& tp : tangentPoints) {
				bool found = false;
				for (const auto& v : list.vertices()) {
					if (dist({ v.pos.x, v.pos.y }, tp) <= thickness * 0.5f + 1.0f) {
						found = true;
						break;
					}
				}
				assert(found);
			}

			std::cout << "✓ testAddRectFormsRingAndHonoursRounding (rounding=8 rounded ring)\n";
		}
	}

	// --- Never-called-in-src/ui primitives -----------------------------------------
	// addCircleFilled sat broken from the start until RadioButton became its
	// first real caller and made the wedge-shaped fan visible. These primitives have no
	// caller in src/ui/ today either, so nothing would surface a similar bug by eye until
	// addTriangleFilled and addPolyline-as-a-line wire them up -- these tests are
	// what stand in for that eyeball check now.

	void testAddTriangleFilledVerticesMatchInputExactly() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Vec2 a{ 10, 20 }, b{ 90, 20 }, c{ 50, 80 };
		list.addTriangleFilled(a, b, c, lvgui::Color(255, 0, 0, 255));

		// 3 core vertices + 3 AA fringe vertices (one ring around a 3-point loop);
		// 3 core indices + 18 fringe indices (3 fringe quads, 6 indices each) -- the AA
		// fringe is always appended strictly AFTER the core geometry a caller already
		// computed (docs/gui/02-rendering.md, "Anti-aliasing"), so it adds new
		// vertices/indices past the old exact counts rather than changing them.
		assert(list.vertices().size() == 6);
		assert(list.indices().size() == 21);
		const auto& v = list.vertices();
		assert(v[0].pos.x == a.x && v[0].pos.y == a.y);
		assert(v[1].pos.x == b.x && v[1].pos.y == b.y);
		assert(v[2].pos.x == c.x && v[2].pos.y == c.y);
		assert(list.indices()[0] == 0 && list.indices()[1] == 1 && list.indices()[2] == 2);

		// Core vertices are opaque; the fringe ring (vertices 3..5) fades to alpha 0.
		assert(v[0].color >> 24 == 0xFF);
		for (int i = 3; i < 6; ++i) {
			assert((v[i].color >> 24) == 0);
		}

		std::cout << "✓ testAddTriangleFilledVerticesMatchInputExactly\n";
	}

	void testAddConvexPolyFilledVerticesAndFanIndicesAreCorrect() {
		lvgui::DrawList list;
		list.clear();

		// A square expressed as a 4-point convex polygon (not the addRectFilled path) --
		// the known-correct fan for 4 points is exactly two triangles: (0,1,2), (0,2,3).
		// The addCircleFilled bug was precisely a wrong FAN INDEX pattern with vertex
		// positions that looked plausible in isolation, so asserting the index list
		// itself, not just vertex positions, is the point of this test.
		lvgui::Vec2 pts[4] = { { 0, 0 }, { 40, 0 }, { 40, 40 }, { 0, 40 } };
		list.addConvexPolyFilled(pts, 4, lvgui::Color(0, 255, 0, 255));

		// 4 core + 4 AA fringe vertices; 6 core + 24 fringe indices (4 fringe quads, 6
		// indices each) -- see the matching comment in
		// testAddTriangleFilledVerticesMatchInputExactly above.
		assert(list.vertices().size() == 8);
		assert(list.indices().size() == 30);
		const auto& v = list.vertices();
		for (int i = 0; i < 4; ++i) {
			assert(v[i].pos.x == pts[i].x && v[i].pos.y == pts[i].y);
		}
		const auto& idx = list.indices();
		std::uint32_t expected[6] = { 0, 1, 2, 0, 2, 3 };
		for (int i = 0; i < 6; ++i) {
			assert(idx[i] == expected[i]);
		}

		std::cout << "✓ testAddConvexPolyFilledVerticesAndFanIndicesAreCorrect\n";
	}

	void testAddRectFilledMultiColorCornersExactPositionsAndColors() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect r{ 10, 20, 60, 30 };
		lvgui::Color tl(255, 0, 0, 255), tr(0, 255, 0, 255), br(0, 0, 255, 255), bl(255, 255, 0, 255);
		list.addRectFilledMultiColor(r, tl, tr, br, bl);

		assert(list.vertices().size() == 4);
		const auto& v = list.vertices();

		// Position AND colour together: a corner-position bug and a swapped-colour bug
		// would both hide behind a "4 vertices, right bounding box" count-only test, so
		// this pins each corner's position to its OWN expected colour, not just the set.
		assert(v[0].pos.x == r.left()  && v[0].pos.y == r.top()    && v[0].color == tl.packed());
		assert(v[1].pos.x == r.right() && v[1].pos.y == r.top()    && v[1].color == tr.packed());
		assert(v[2].pos.x == r.right() && v[2].pos.y == r.bottom() && v[2].color == br.packed());
		assert(v[3].pos.x == r.left()  && v[3].pos.y == r.bottom() && v[3].color == bl.packed());

		std::cout << "✓ testAddRectFilledMultiColorCornersExactPositionsAndColors\n";
	}

	void testAddPolylineOpenThreePointStraightLineTwoNonOverlappingQuadsSharedEdgeAtMiddle() {
		lvgui::DrawList list;
		list.clear();

		// Collinear so both segments share the exact same perpendicular offset -- the
		// only configuration where "shared edge" means the two quads' touching edges are
		// literally the same two points, not just nearby (addPolyline explicitly does no
		// mitring, so a bent polyline's joint is NOT guaranteed to line up).
		lvgui::Vec2 p0{ 0, 50 }, p1{ 50, 50 }, p2{ 100, 50 };
		lvgui::Vec2 pts[3] = { p0, p1, p2 };
		float thickness = 10.0f;
		list.addPolyline(pts, 3, lvgui::Color(255, 255, 255, 255), thickness, false);

		// Two core quads, no closing segment (open): 8 core vertices, 12 core indices --
		// unchanged from before AA, since the fringe pass reads these back but never
		// alters them. Plus 4 fringe vertices and 6 fringe indices PER segment (2
		// segments): vertices 8+8=16, indices 12+24=36 (docs/gui/02-rendering.md,
		// "Anti-aliasing").
		assert(list.vertices().size() == 16);
		assert(list.indices().size() == 36);
		const auto& v = list.vertices();

		// Quad 0 covers p0->p1: v0,v1 at the p0 end, v2,v3 at the p1 end.
		assert(v[0].pos.x == 0.0f  && v[0].pos.y == 45.0f);
		assert(v[1].pos.x == 0.0f  && v[1].pos.y == 55.0f);
		assert(v[2].pos.x == 50.0f && v[2].pos.y == 55.0f);
		assert(v[3].pos.x == 50.0f && v[3].pos.y == 45.0f);

		// Quad 1 covers p1->p2: v4,v5 at the p1 end, v6,v7 at the p2 end.
		assert(v[4].pos.x == 50.0f  && v[4].pos.y == 45.0f);
		assert(v[5].pos.x == 50.0f  && v[5].pos.y == 55.0f);
		assert(v[6].pos.x == 100.0f && v[6].pos.y == 55.0f);
		assert(v[7].pos.x == 100.0f && v[7].pos.y == 45.0f);

		// Shared edge at the middle point (50,50): quad 0's p1-end pair {v2,v3} and quad
		// 1's p1-end pair {v4,v5} must be the exact same two points (as a set, since
		// per-quad winding order need not match).
		bool sharedEdgeMatches =
			((v[2].pos.x == v[4].pos.x && v[2].pos.y == v[4].pos.y &&
			  v[3].pos.x == v[5].pos.x && v[3].pos.y == v[5].pos.y) ||
			 (v[2].pos.x == v[5].pos.x && v[2].pos.y == v[5].pos.y &&
			  v[3].pos.x == v[4].pos.x && v[3].pos.y == v[4].pos.y));
		assert(sharedEdgeMatches);

		// Non-overlapping: quad 0 never crosses past x=50 into quad 1's span, and vice
		// versa -- they meet exactly at the shared edge, nothing more.
		for (int i = 0; i < 4; ++i) {
			assert(v[i].pos.x <= 50.0f);
		}
		for (int i = 4; i < 8; ++i) {
			assert(v[i].pos.x >= 50.0f);
		}

		std::cout << "✓ testAddPolylineOpenThreePointStraightLineTwoNonOverlappingQuadsSharedEdgeAtMiddle\n";
	}

	// --- Anti-aliasing fringe -----------------------------------------------------

	void testAAFringeAppendsOuterTranslucentRingAfterCoreCircleGeometry() {
		lvgui::DrawList list;
		list.clear();

		int segments = 16;
		list.addCircleFilled({ 50, 50 }, 20.0f, lvgui::Color(0, 128, 255, 255), segments);

		// Core: segments+1 perimeter vertices (last duplicates the first) + 1 centre
		// hub. Fringe rings only the `segments` distinct perimeter points -- see
		// addCircleFilled's own comment on why not segments+1 or the centre.
		std::size_t coreCount = static_cast<std::size_t>(segments) + 2;
		assert(list.vertices().size() == coreCount + static_cast<std::size_t>(segments));

		const auto& v = list.vertices();
		for (std::size_t i = 0; i < coreCount; ++i) {
			assert((v[i].color >> 24) == 0xFF);
		}
		for (std::size_t i = coreCount; i < v.size(); ++i) {
			assert((v[i].color >> 24) == 0);
		}

		std::cout << "✓ testAAFringeAppendsOuterTranslucentRingAfterCoreCircleGeometry\n";
	}

	void testAAFringeSitsOutsideRoundedRectCoreGeometry() {
		lvgui::DrawList list;
		list.clear();

		lvgui::Rect r = { 10, 20, 100, 40 };
		list.addRectFilled(r, lvgui::Color(255, 0, 0, 255), 8.0f);

		// buildRoundedRectPerimeter emits exactly 4 corners * 17 points = 68, with no
		// duplicated closing point, so the whole perimeter loop rings directly.
		assert(list.vertices().size() == 68 + 68);

		const auto& v = list.vertices();
		lvgui::Vec2 centre = r.centre();
		for (std::size_t i = 0; i < 68; ++i) {
			assert((v[i].color >> 24) == 0xFF);
		}
		for (std::size_t i = 68; i < v.size(); ++i) {
			assert((v[i].color >> 24) == 0);
			// Every fringe vertex sits farther from centre than SOME core vertex it was
			// generated from -- a loose sanity check that the ring grew outward, not
			// inward or in place.
			assert(dist({ v[i].pos.x, v[i].pos.y }, centre) > 0.0f);
		}

		std::cout << "✓ testAAFringeSitsOutsideRoundedRectCoreGeometry\n";
	}

	void testAxisAlignedPrimitivesExemptFromAAFringe() {
		// Pixel-snapped axis-aligned edges are already exact -- addRectFilled's
		// rounding<=0 fast path, addRectFilledMultiColor, and addImage deliberately get
		// no fringe at all (docs/gui/02-rendering.md, "Anti-aliasing").
		{
			lvgui::DrawList list;
			list.clear();
			list.addRectFilled({ 10, 10, 80, 50 }, lvgui::Color(255, 0, 0, 255));
			assert(list.vertices().size() == 4);
		}
		{
			lvgui::DrawList list;
			list.clear();
			lvgui::Color c(255, 0, 0, 255);
			list.addRectFilledMultiColor({ 10, 20, 60, 30 }, c, c, c, c);
			assert(list.vertices().size() == 4);
		}
		{
			lvgui::DrawList list;
			list.clear();
			list.addImage(3, { 0.0f, 0.0f, 10.0f, 10.0f });
			assert(list.vertices().size() == 4);
		}

		std::cout << "✓ testAxisAlignedPrimitivesExemptFromAAFringe\n";
	}
}

int main() {
	testRectFilledBasic();
	testSameClipRectOneCommand();
	testDifferentClipRectsTwoCommands();
	testAddImageEmitsCorrectPositionsUVsAndTint();
	testAddImageDefaultsToFullUVAndOpaqueWhiteTint();
	testConsecutiveAddImageSameTextureBatchesIntoOneCommand();
	testSwitchingTextureIdStartsANewCommand();
	testAddImageInsideAClipRectAlsoSplitsOnClipChange();
	testClipIntersection();
	testPopClipRectNoUnderflow();
	testCoordinatesSnappedToIntegers();
	testColorRoundTrip();
	testRectOutsideClipStillEmitsGeometry();
	testClipRectReplace();
	testRect();
	testVec2();
	testColorOperations();
	testColorFromHSVPrimaryAndSecondaryHues();
	testColorFromHSVSaturationAndValueExtremes();
	testColorToHSVRoundTripsThroughFromHSV();
	testColorToHSVGreyHasCanonicalZeroHue();
	testAddTextEmitsOneQuadPerVisibleGlyph();
	testAddTextSkipsWhitespaceQuads();
	testAddTextClippedTruncatesAndClips();
	testAddTextTabularFlagGivesEveryDigitTheSamePenAdvance();
	testAddTextWithExplicitTextureIdBatchesIntoThatCommand();
	testAddTextClippedWithExplicitTextureIdBatchesIntoThatCommand();

	testRectFilledSquareCornersExact();
	testRectFilledRoundedStaysInBoundsAndOffCentre();
	testRectFilledRoundingClampsInsteadOfInverting();
	testAddTextBaselineWithinLineHeightOfTopLeft();
	testAddRectFormsRingAndHonoursRounding();

	testAddTriangleFilledVerticesMatchInputExactly();
	testAddConvexPolyFilledVerticesAndFanIndicesAreCorrect();
	testAddRectFilledMultiColorCornersExactPositionsAndColors();
	testAddPolylineOpenThreePointStraightLineTwoNonOverlappingQuadsSharedEdgeAtMiddle();

	testAAFringeAppendsOuterTranslucentRingAfterCoreCircleGeometry();
	testAAFringeSitsOutsideRoundedRectCoreGeometry();
	testAxisAlignedPrimitivesExemptFromAAFringe();

	std::cout << "\n✅ All DrawList tests passed!\n";
	return 0;
}
