#include "TextWrap.h"
#include <lightVulkanGraphics/ui/Font.h>

namespace lightGraphics::ui {

std::vector<std::string_view> wrapText(const Font& font, std::string_view text, float pixelSize, float width,
                                        TextFlags flags) {
	std::vector<std::string_view> lines;
	if (width <= 0.0f || text.empty()) {
		lines.push_back(text);
		return lines;
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
	return lines;
}

} // namespace lightGraphics::ui
