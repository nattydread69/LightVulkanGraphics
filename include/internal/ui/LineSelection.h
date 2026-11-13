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

#pragma once

// Mouse text selection over a list of display lines, shared by the read-only text widgets
// (Label, LogView, ListBox): drag to select, double-click to select a word, Ctrl+C copies
// (GuiContext publishes the selected text, docs/gui/05-widgets.md, "Text selection").
// Internal only: widgets own one LineSelection each and feed it their own line geometry.

#include <lightVulkanGraphics/ui/Font.h>
#include <lightVulkanGraphics/ui/Types.h>

#include "Utf8.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

struct LinePos {
	int         line = 0;
	std::size_t byte = 0;
	bool operator==(const LinePos& o) const { return line == o.line && byte == o.byte; }
	bool operator<(const LinePos& o) const { return line != o.line ? line < o.line : byte < o.byte; }
};

class LineSelection {
public:
	void clear() { m_anchor = m_caret = {}; m_dragging = false; m_moved = false; }
	bool empty() const { return m_anchor == m_caret; }
	bool dragging() const { return m_dragging; }
	// True once the pointer has moved since the press -- a click that never moves is not a selection.
	bool moved() const { return m_moved; }

	void begin(LinePos p) { m_anchor = m_caret = p; m_dragging = true; m_moved = false; }
	void extend(LinePos p) { if (!(p == m_caret)) { m_caret = p; m_moved = true; } }
	void end() { m_dragging = false; }
	void set(LinePos a, LinePos b) { m_anchor = a; m_caret = b; m_moved = true; }

	LinePos first() const { return m_caret < m_anchor ? m_caret : m_anchor; }
	LinePos last() const { return m_caret < m_anchor ? m_anchor : m_caret; }

	// The selected byte range [s, e) of `line` (length `len`); false when the line is untouched.
	bool lineRange(int line, std::size_t len, std::size_t& s, std::size_t& e) const {
		if (empty()) { return false; }
		const LinePos a = first(), b = last();
		if (line < a.line || line > b.line) { return false; }
		s = (line == a.line) ? std::min(a.byte, len) : 0;
		e = (line == b.line) ? std::min(b.byte, len) : len;
		return e > s || (line > a.line && line < b.line);
	}

	// Selected text; lines are joined with `sep`.
	std::string text(const std::vector<std::string_view>& lines, const char* sep = "\n") const {
		std::string out;
		if (empty()) { return out; }
		const LinePos a = first(), b = last();
		for (int l = a.line; l <= b.line && l < static_cast<int>(lines.size()); ++l) {
			if (l < 0) { continue; }
			std::string_view v = lines[static_cast<std::size_t>(l)];
			std::size_t s = (l == a.line) ? std::min(a.byte, v.size()) : 0;
			std::size_t e = (l == b.line) ? std::min(b.byte, v.size()) : v.size();
			if (l != a.line) { out += sep; }
			if (e > s) { out.append(v.substr(s, e - s)); }
		}
		return out;
	}

	// Select the word (run of non-space characters) around `p`.
	void selectWord(LinePos p, std::string_view lineText) {
		std::size_t i = std::min(p.byte, lineText.size());
		auto isSpace = [&](std::size_t at) {
			return at < lineText.size() && (lineText[at] == ' ' || lineText[at] == '\t');
		};
		std::size_t s = i, e = i;
		while (e < lineText.size() && !isSpace(e)) { e = utf8NextBoundary(lineText, e); }
		while (s > 0 && !isSpace(utf8PrevBoundary(lineText, s))) { s = utf8PrevBoundary(lineText, s); }
		set({ p.line, s }, { p.line, e });
	}

private:
	LinePos m_anchor, m_caret;
	bool    m_dragging = false;
	bool    m_moved = false;
};

// Highlight rectangle of [s, e) in a line drawn from `x` at `y` with height `h`.
inline Rect selectionRect(const Font& font, std::string_view line, float pixelSize, float x, float y, float h,
                          std::size_t s, std::size_t e) {
	const float x0 = x + font.offsetAtIndex(line, pixelSize, s);
	const float x1 = x + font.offsetAtIndex(line, pixelSize, e);
	return { x0, y, std::max(x1 - x0, 2.0f), h };
}

} // namespace lightGraphics::ui
