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

#include "TextWrap.h"
#include <lightVulkanGraphics/ui/Font.h>

namespace lightGraphics::ui {

namespace {

// Greedy word-wrap of one paragraph (no newlines in it).
void wrapParagraph(const Font& font, std::string_view text, float pixelSize, float width, TextFlags flags,
                   std::vector<std::string_view>& lines) {
	if (width <= 0.0f || text.empty()) {
		lines.push_back(text);
		return;
	}

	std::size_t lineStart = 0;
	std::size_t lastLineEnd = 0;   // byte index of the last space seen on the current line
	std::size_t pos = 0;

	while (pos <= text.size()) {
		bool atEnd = pos == text.size();
		if (atEnd || text[pos] == ' ') {
			float w = font.measureText(text.substr(lineStart, pos - lineStart), pixelSize, flags).x;
			if (w > width && lastLineEnd > lineStart) {
				// Overflowed -- break at the last space seen on this line instead, and
				// re-evaluate the SAME pos against the new, shorter line.
				lines.push_back(text.substr(lineStart, lastLineEnd - lineStart));
				lineStart = lastLineEnd + 1;
				continue;
			}
			lastLineEnd = pos;
			if (atEnd) {
				lines.push_back(text.substr(lineStart, pos - lineStart));
				break;
			}
		}
		++pos;
	}
}

} // namespace

std::vector<std::string_view> wrapText(const Font& font, std::string_view text, float pixelSize, float width,
                                        TextFlags flags) {
	std::vector<std::string_view> lines;
	// A newline is a hard break: each paragraph wraps on its own, and an empty one is an empty line. A "\r\n"
	// pair breaks once. Text without a newline takes exactly the path it always did.
	std::size_t start = 0;
	while (true) {
		const std::size_t nl = text.find('\n', start);
		std::string_view paragraph = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
		if (!paragraph.empty() && paragraph.back() == '\r') {
			paragraph.remove_suffix(1);
		}
		wrapParagraph(font, paragraph, pixelSize, width, flags, lines);
		if (nl == std::string_view::npos) {
			break;
		}
		start = nl + 1;
	}
	return lines;
}

} // namespace lightGraphics::ui
