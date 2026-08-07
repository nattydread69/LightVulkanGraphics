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
}

int main() {
	testRectFilledBasic();
	testSameClipRectOneCommand();
	testDifferentClipRectsTwoCommands();
	testClipIntersection();
	testPopClipRectNoUnderflow();
	testCoordinatesSnappedToIntegers();
	testColorRoundTrip();
	testRectOutsideClipStillEmitsGeometry();
	testClipRectReplace();
	testRect();
	testVec2();
	testColorOperations();
	testAddTextEmitsOneQuadPerVisibleGlyph();
	testAddTextSkipsWhitespaceQuads();
	testAddTextClippedTruncatesAndClips();

	std::cout << "\n✅ All DrawList tests passed!\n";
	return 0;
}
