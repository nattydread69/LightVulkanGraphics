#include "Utf8.h"
#include <lightVulkanGraphics/ui/Font.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>

namespace lvgui = lightGraphics::ui;

namespace {
	void testAsciiRoundTrip() {
		std::string s = "Hello, World! 123";
		std::size_t pos = 0;
		std::string rebuilt;
		while (pos < s.size()) {
			std::uint32_t cp = lvgui::decodeUtf8(s, pos);
			lvgui::utf8Encode(cp, rebuilt);
		}
		assert(rebuilt == s);
		std::cout << "✓ testAsciiRoundTrip\n";
	}

	void testMultiByteDecode() {
		// Greek small alpha, U+03B1
		std::string alpha = "\xCE\xB1";
		std::size_t pos = 0;
		std::uint32_t cp = lvgui::decodeUtf8(alpha, pos);
		assert(cp == 0x03B1);
		assert(pos == 2);

		// Euro sign, U+20AC
		std::string euro = "\xE2\x82\xAC";
		pos = 0;
		cp = lvgui::decodeUtf8(euro, pos);
		assert(cp == 0x20AC);
		assert(pos == 3);

		// Deseret capital long I, U+10348 (a 4-byte codepoint)
		std::string deseret = "\xF0\x90\x8D\x88";
		pos = 0;
		cp = lvgui::decodeUtf8(deseret, pos);
		assert(cp == 0x10348);
		assert(pos == 4);

		std::cout << "✓ testMultiByteDecode\n";
	}

	void testMalformedYieldsReplacementAndTerminates() {
		std::string bad = "\xFF\xFE";
		std::size_t pos = 0;
		assert(lvgui::decodeUtf8(bad, pos) == 0xFFFD);
		assert(pos == 1);
		assert(lvgui::decodeUtf8(bad, pos) == 0xFFFD);
		assert(pos == 2);
		assert(pos == bad.size());
		std::cout << "✓ testMalformedYieldsReplacementAndTerminates\n";
	}

	void testTruncatedSequence() {
		// A 3-byte lead byte with no continuation bytes following it at all.
		std::string bad = "\xE2";
		std::size_t pos = 0;
		assert(lvgui::decodeUtf8(bad, pos) == 0xFFFD);
		assert(pos == 1);
		std::cout << "✓ testTruncatedSequence\n";
	}

	void testFuzzNeverLoopsForever() {
		std::mt19937 rng(12345);
		std::uniform_int_distribution<int> byteDist(0, 255);
		std::uniform_int_distribution<int> lenDist(0, 32);

		for (int iter = 0; iter < 5000; ++iter) {
			int len = lenDist(rng);
			std::string s;
			s.reserve(static_cast<std::size_t>(len));
			for (int i = 0; i < len; ++i) {
				s.push_back(static_cast<char>(byteDist(rng)));
			}

			std::size_t pos = 0;
			int steps = 0;
			while (pos < s.size()) {
				std::size_t before = pos;
				lvgui::decodeUtf8(s, pos);
				assert(pos > before);
				++steps;
				assert(steps <= static_cast<int>(s.size()) + 1);
			}
		}
		std::cout << "✓ testFuzzNeverLoopsForever\n";
	}

	void testBoundaryHelpers() {
		// 'a' + Greek alpha (2 bytes) + 'b'
		std::string s = "a\xCE\xB1" "b";
		assert(s.size() == 4);

		assert(lvgui::utf8NextBoundary(s, 0) == 1);
		assert(lvgui::utf8NextBoundary(s, 1) == 3);
		assert(lvgui::utf8NextBoundary(s, 3) == 4);

		assert(lvgui::utf8PrevBoundary(s, 4) == 3);
		assert(lvgui::utf8PrevBoundary(s, 3) == 1);
		assert(lvgui::utf8PrevBoundary(s, 1) == 0);
		assert(lvgui::utf8PrevBoundary(s, 0) == 0);

		std::cout << "✓ testBoundaryHelpers\n";
	}

	void testDefaultRangeRoundTrip() {
		for (std::size_t r = 0; r < lvgui::kDefaultGlyphRangeCount; ++r) {
			const lvgui::GlyphRange& range = lvgui::kDefaultGlyphRanges[r];
			for (std::uint32_t cp = range.first; cp <= range.last; ++cp) {
				std::string encoded;
				lvgui::utf8Encode(cp, encoded);
				std::size_t pos = 0;
				std::uint32_t decoded = lvgui::decodeUtf8(encoded, pos);
				assert(decoded == cp);
				assert(pos == encoded.size());
			}
		}
		std::cout << "✓ testDefaultRangeRoundTrip\n";
	}
}

int main() {
	testAsciiRoundTrip();
	testMultiByteDecode();
	testMalformedYieldsReplacementAndTerminates();
	testTruncatedSequence();
	testFuzzNeverLoopsForever();
	testBoundaryHelpers();
	testDefaultRangeRoundTrip();

	std::cout << "\n✅ All UTF-8 tests passed!\n";
	return 0;
}
