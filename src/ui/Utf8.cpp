#include "Utf8.h"

namespace lightGraphics::ui {

namespace {
	unsigned char byteAt(std::string_view s, std::size_t i) {
		return static_cast<unsigned char>(s[i]);
	}

	bool isContinuation(std::string_view s, std::size_t i) {
		return i < s.size() && (byteAt(s, i) & 0xC0) == 0x80;
	}
}

std::uint32_t decodeUtf8(std::string_view s, std::size_t& pos) {
	if (pos >= s.size()) {
		return 0xFFFD;
	}

	std::size_t start = pos;
	unsigned char b0 = byteAt(s, start);

	if (b0 < 0x80) {
		pos = start + 1;
		return b0;
	}

	std::size_t len;
	std::uint32_t cp;
	std::uint32_t minCp;
	if ((b0 & 0xE0) == 0xC0) {
		len = 2; cp = b0 & 0x1F; minCp = 0x80;
	} else if ((b0 & 0xF0) == 0xE0) {
		len = 3; cp = b0 & 0x0F; minCp = 0x800;
	} else if ((b0 & 0xF8) == 0xF0) {
		len = 4; cp = b0 & 0x07; minCp = 0x10000;
	} else {
		// Stray continuation byte or an obsolete 5/6-byte lead byte: malformed.
		pos = start + 1;
		return 0xFFFD;
	}

	for (std::size_t i = 1; i < len; ++i) {
		if (!isContinuation(s, start + i)) {
			pos = start + 1;
			return 0xFFFD;
		}
		cp = (cp << 6) | (byteAt(s, start + i) & 0x3F);
	}

	// Reject overlong encodings and surrogate halves smuggled through as UTF-8.
	if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
		pos = start + 1;
		return 0xFFFD;
	}

	pos = start + len;
	return cp;
}

std::size_t utf8PrevBoundary(std::string_view s, std::size_t pos) {
	if (pos == 0) {
		return 0;
	}

	std::size_t p = pos - 1;
	// A well-formed sequence has at most 3 continuation bytes; capping the walk-back
	// keeps a malformed run of 0x80-0xBF bytes from dragging the caret far away.
	int steps = 0;
	while (p > 0 && steps < 3 && isContinuation(s, p)) {
		--p;
		++steps;
	}
	return p;
}

std::size_t utf8NextBoundary(std::string_view s, std::size_t pos) {
	if (pos >= s.size()) {
		return s.size();
	}
	std::size_t p = pos;
	decodeUtf8(s, p);
	return p;
}

void utf8Encode(std::uint32_t cp, std::string& out) {
	if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
		cp = 0xFFFD;
	}

	if (cp < 0x80) {
		out.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

} // namespace lightGraphics::ui
