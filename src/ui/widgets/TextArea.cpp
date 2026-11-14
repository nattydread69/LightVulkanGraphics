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

#include <lightVulkanGraphics/ui/widgets/TextArea.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "Utf8.h"

#include <algorithm>
#include <cmath>

namespace lightGraphics::ui {

namespace {
	constexpr float kMultiClickSeconds = 0.35f;
	constexpr float kBlinkPeriodSeconds = 1.06f;
	constexpr float kBlinkVisibleFraction = 0.53f;
	constexpr float kMinScrollThumb = 16.0f;
	constexpr std::size_t kMaxMeasureBytes = 8192;   // bounds the cost of wrapping an unbroken run

	bool isAsciiWhitespace(std::uint32_t cp) {
		return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\v' || cp == '\f';
	}

	// CR and CRLF become LF, tabs four spaces, other control characters are dropped.
	std::string normalizeInput(std::string_view in) {
		std::string out;
		out.reserve(in.size());
		std::size_t pos = 0;
		while (pos < in.size()) {
			std::uint32_t cp = decodeUtf8(in, pos);
			if (cp == '\r') {
				if (pos < in.size() && in[pos] == '\n') {
					++pos;
				}
				out.push_back('\n');
			} else if (cp == '\n') {
				out.push_back('\n');
			} else if (cp == '\t') {
				out.append("    ");
			} else if (cp < 0x20 || cp == 0x7F) {
				continue;
			} else {
				utf8Encode(cp, out);
			}
		}
		return out;
	}

	Rect thumbRect(const Rect& track, float content, float view, float scrollY, float maxScrollY) {
		float thumbH = std::max(kMinScrollThumb, track.h * view / std::max(content, 1.0f));
		thumbH = std::min(thumbH, track.h);
		float travel = track.h - thumbH;
		float t = maxScrollY > 0.0f ? scrollY / maxScrollY : 0.0f;
		return Rect{ track.x, track.y + travel * t, track.w, thumbH };
	}
}

TextArea::TextArea(std::string initial) : m_text(std::move(initial)) {
	m_text = normalizeInput(m_text);
	m_caret = m_anchor = m_text.size();
}

// ---- editing operations --------------------------------------------------------

void TextArea::fireChange() {
	m_layoutDirty = true;
	if (m_bindTarget) {
		*m_bindTarget = m_text;
	}
	if (m_onChange) {
		m_onChange(m_text);
	}
}

void TextArea::eraseSelection() {
	std::size_t s = selectionStart(), e = selectionEnd();
	m_text.erase(s, e - s);
	m_caret = m_anchor = s;
	markMoved();
	fireChange();
}

void TextArea::insertText(std::string_view utf8) {
	if (m_readOnly) {
		return;
	}
	std::string cleaned = normalizeInput(utf8);

	bool hadSelection = hasSelection();
	if (hadSelection) {
		std::size_t s = selectionStart(), e = selectionEnd();
		m_text.erase(s, e - s);
		m_caret = m_anchor = s;
	}
	if (!cleaned.empty()) {
		m_text.insert(m_caret, cleaned);
		m_caret += cleaned.size();
	}
	m_anchor = m_caret;
	if (hadSelection || !cleaned.empty()) {
		markMoved();
		fireChange();
	}
}

void TextArea::deleteBackward() {
	if (m_readOnly) {
		return;
	}
	if (hasSelection()) {
		eraseSelection();
		return;
	}
	if (m_caret == 0) {
		return;
	}
	std::size_t start = utf8PrevBoundary(m_text, m_caret);
	m_text.erase(start, m_caret - start);
	m_caret = m_anchor = start;
	markMoved();
	fireChange();
}

void TextArea::deleteForward() {
	if (m_readOnly) {
		return;
	}
	if (hasSelection()) {
		eraseSelection();
		return;
	}
	if (m_caret >= m_text.size()) {
		return;
	}
	std::size_t end = utf8NextBoundary(m_text, m_caret);
	m_text.erase(m_caret, end - m_caret);
	m_anchor = m_caret;
	markMoved();
	fireChange();
}

void TextArea::moveCaret(int dir, bool select) {
	if (!select && hasSelection()) {
		std::size_t target = (dir > 0) ? selectionEnd() : selectionStart();
		m_caret = m_anchor = target;
		markMoved();
		return;
	}
	std::size_t newCaret = (dir > 0) ? utf8NextBoundary(m_text, m_caret) : utf8PrevBoundary(m_text, m_caret);
	m_caret = newCaret;
	if (!select) {
		m_anchor = newCaret;
	}
	markMoved();
}

void TextArea::moveCaretWord(int dir, bool select) {
	std::size_t pos = m_caret;
	auto peekCp = [this](std::size_t p) {
		std::size_t q = p;
		return decodeUtf8(m_text, q);
	};
	if (dir > 0) {
		while (pos < m_text.size() && isAsciiWhitespace(peekCp(pos))) {
			pos = utf8NextBoundary(m_text, pos);
		}
		while (pos < m_text.size() && !isAsciiWhitespace(peekCp(pos))) {
			pos = utf8NextBoundary(m_text, pos);
		}
	} else {
		while (pos > 0 && isAsciiWhitespace(peekCp(utf8PrevBoundary(m_text, pos)))) {
			pos = utf8PrevBoundary(m_text, pos);
		}
		while (pos > 0 && !isAsciiWhitespace(peekCp(utf8PrevBoundary(m_text, pos)))) {
			pos = utf8PrevBoundary(m_text, pos);
		}
	}
	m_caret = pos;
	if (!select) {
		m_anchor = pos;
	}
	markMoved();
}

void TextArea::moveDocumentStart(bool select) {
	m_caret = 0;
	if (!select) {
		m_anchor = 0;
	}
	markMoved();
}

void TextArea::moveDocumentEnd(bool select) {
	m_caret = m_text.size();
	if (!select) {
		m_anchor = m_text.size();
	}
	markMoved();
}

void TextArea::selectAll() {
	m_anchor = 0;
	m_caret = m_text.size();
	markMoved();
}

std::string TextArea::copy() const {
	return m_text.substr(selectionStart(), selectionEnd() - selectionStart());
}

std::string TextArea::cut() {
	std::string s = copy();
	if (!m_readOnly && hasSelection()) {
		eraseSelection();
	}
	return s;
}

void TextArea::paste(std::string_view clip) {
	insertText(clip);
}

void TextArea::setText(std::string text, bool fireCallback) {
	m_text = normalizeInput(text);
	m_caret = m_anchor = m_text.size();
	m_layoutDirty = true;
	markMoved();
	if (m_bindTarget) {
		*m_bindTarget = m_text;
	}
	if (fireCallback && m_onChange) {
		m_onChange(m_text);
	}
}

void TextArea::setWordWrap(bool wrap) {
	if (m_wrap != wrap) {
		m_wrap = wrap;
		m_layoutDirty = true;
	}
}

// ---- layout ----------------------------------------------------------------------

void TextArea::wrapLogicalLine(const Font& font, float pixelSize, float width,
                               std::size_t start, std::size_t end) {
	const std::string_view all(m_text);
	if (!m_wrap || end == start) {
		m_lines.push_back({ start, end });
		return;
	}
	std::size_t lineStart = start;
	while (true) {
		// Take whole words (plus the spaces after them) while they fit. Trailing spaces
		// stay on the line they follow, so they never start the next line.
		std::size_t good = lineStart, p = lineStart;
		while (p < end) {
			std::size_t wordEnd = p;
			while (wordEnd < end && all[wordEnd] != ' ') {
				wordEnd = utf8NextBoundary(all, wordEnd);
			}
			std::size_t next = wordEnd;
			while (next < end && all[next] == ' ') {
				++next;
			}
			std::size_t span = wordEnd - lineStart;
			if (span > kMaxMeasureBytes ||
			    font.measureText(all.substr(lineStart, span), pixelSize).x > width) {
				break;
			}
			good = next;
			p = next;
		}
		if (good >= end) {
			m_lines.push_back({ lineStart, end });
			return;
		}
		if (good == lineStart) {
			// A single word wider than the line: break it at the last glyph that fits.
			std::string_view rest = all.substr(lineStart, std::min(end - lineStart, kMaxMeasureBytes));
			std::size_t idx = font.indexAtOffset(rest, pixelSize, width);
			idx = std::max(idx, utf8NextBoundary(all, lineStart) - lineStart);
			good = lineStart + idx;
			if (good >= end) {
				m_lines.push_back({ lineStart, end });
				return;
			}
		}
		m_lines.push_back({ lineStart, good });
		lineStart = good;
	}
}

void TextArea::computeLayout(const Font& font, float pixelSize, float width) {
	m_lines.clear();
	m_font = &font;
	m_layoutWidth = width;
	m_layoutPixelSize = pixelSize;
	m_layoutDirty = false;

	std::size_t pos = 0;
	while (true) {
		std::size_t eol = m_text.find('\n', pos);
		bool hard = eol != std::string::npos;
		wrapLogicalLine(font, pixelSize, width, pos, hard ? eol : m_text.size());
		if (!hard) {
			break;
		}
		pos = eol + 1;
	}
}

void TextArea::ensureLayout(const GuiContext& ctx) {
	const Theme& th = ctx.theme();
	Rect inner = innerRect(ctx);
	float width = std::max(8.0f, inner.w - th.scrollbarWidth - th.framePadding);
	if (m_layoutDirty || width != m_layoutWidth || th.fontSize != m_layoutPixelSize) {
		computeLayout(ctx.font(), th.fontSize, width);
	}
}

std::size_t TextArea::lineForIndex(std::size_t idx) const {
	// Last line whose start is <= idx.
	auto it = std::upper_bound(m_lines.begin(), m_lines.end(), idx,
		[](std::size_t value, const VisualLine& l) { return value < l.start; });
	if (it == m_lines.begin()) {
		return 0;
	}
	return static_cast<std::size_t>(it - m_lines.begin()) - 1;
}

std::string_view TextArea::lineText(std::size_t line) const {
	if (line >= m_lines.size()) {
		return {};
	}
	return std::string_view(m_text).substr(m_lines[line].start, m_lines[line].end - m_lines[line].start);
}

bool TextArea::isSoftLineEnd(std::size_t line) const {
	return line + 1 < m_lines.size() && m_lines[line + 1].start == m_lines[line].end;
}

std::size_t TextArea::lineEndCaret(std::size_t line) const {
	const VisualLine& l = m_lines[line];
	if (isSoftLineEnd(line) && l.end > l.start) {
		return utf8PrevBoundary(m_text, l.end);
	}
	return l.end;
}

float TextArea::caretXInLine(const Font& font, float pixelSize, std::size_t line, std::size_t idx) const {
	const VisualLine& l = m_lines[line];
	std::size_t rel = std::min(std::max(idx, l.start), l.end) - l.start;
	return font.offsetAtIndex(lineText(line), pixelSize, rel);
}

std::size_t TextArea::indexInLineAt(const Font& font, float pixelSize, std::size_t line, float x) const {
	std::size_t idx = m_lines[line].start + font.indexAtOffset(lineText(line), pixelSize, x);
	return std::min(idx, lineEndCaret(line));
}

// ---- layout-dependent movement ---------------------------------------------------

void TextArea::moveCaretVertical(int lines, bool select) {
	if (m_lines.empty() || m_font == nullptr) {
		return;
	}
	long target = static_cast<long>(lineForIndex(m_caret)) + lines;
	if (target < 0) {
		moveDocumentStart(select);
		return;
	}
	if (target >= static_cast<long>(m_lines.size())) {
		moveDocumentEnd(select);
		return;
	}
	if (m_preferredX < 0.0f) {
		m_preferredX = caretXInLine(*m_font, m_layoutPixelSize, lineForIndex(m_caret), m_caret);
	}
	std::size_t idx = indexInLineAt(*m_font, m_layoutPixelSize, static_cast<std::size_t>(target), m_preferredX);
	m_caret = idx;
	if (!select) {
		m_anchor = idx;
	}
	markMoved(true);
}

void TextArea::moveLineStart(bool select) {
	if (m_lines.empty()) {
		return;
	}
	m_caret = m_lines[lineForIndex(m_caret)].start;
	if (!select) {
		m_anchor = m_caret;
	}
	markMoved();
}

void TextArea::moveLineEnd(bool select) {
	if (m_lines.empty()) {
		return;
	}
	m_caret = lineEndCaret(lineForIndex(m_caret));
	if (!select) {
		m_anchor = m_caret;
	}
	markMoved();
}

// ---- geometry ---------------------------------------------------------------------

Rect TextArea::innerRect(const GuiContext& ctx) const {
	return m_bounds.inset(ctx.theme().framePadding);
}

Rect TextArea::scrollTrack(const GuiContext& ctx) const {
	Rect inner = innerRect(ctx);
	float w = ctx.theme().scrollbarWidth;
	return Rect{ inner.x + inner.w - w, inner.y, w, inner.h };
}

float TextArea::contentHeight(const GuiContext& ctx) const {
	return static_cast<float>(m_lines.size()) * ctx.font().lineHeight(ctx.theme().fontSize);
}

float TextArea::viewHeight(const GuiContext& ctx) const {
	return innerRect(ctx).h;
}

float TextArea::maxScroll(const GuiContext& ctx) const {
	return std::max(0.0f, contentHeight(ctx) - viewHeight(ctx));
}

void TextArea::ensureCaretVisible(const GuiContext& ctx) {
	if (m_lines.empty()) {
		return;
	}
	float lineH = ctx.font().lineHeight(ctx.theme().fontSize);
	float top = static_cast<float>(lineForIndex(m_caret)) * lineH;
	float view = viewHeight(ctx);
	if (top < m_scrollY) {
		m_scrollY = top;
	} else if (top + lineH > m_scrollY + view) {
		m_scrollY = top + lineH - view;
	}
}

std::size_t TextArea::hitIndexAt(const GuiContext& ctx, Vec2 p) const {
	if (m_lines.empty()) {
		return 0;
	}
	const Theme& th = ctx.theme();
	Rect inner = innerRect(ctx);
	float lineH = ctx.font().lineHeight(th.fontSize);
	float row = std::floor((p.y - inner.y + m_scrollY) / lineH);
	long line = std::clamp(static_cast<long>(row), 0L, static_cast<long>(m_lines.size()) - 1);
	return indexInLineAt(ctx.font(), th.fontSize, static_cast<std::size_t>(line), p.x - inner.x);
}

void TextArea::selectWordAt(std::size_t byteIndex) {
	std::size_t idx = std::min(byteIndex, m_text.size());
	auto peekCp = [this](std::size_t p) {
		std::size_t q = p;
		return decodeUtf8(m_text, q);
	};
	std::size_t start = idx, end = idx;
	while (end < m_text.size() && !isAsciiWhitespace(peekCp(end))) {
		end = utf8NextBoundary(m_text, end);
	}
	while (start > 0 && !isAsciiWhitespace(peekCp(utf8PrevBoundary(m_text, start)))) {
		start = utf8PrevBoundary(m_text, start);
	}
	m_anchor = start;
	m_caret = end;
	markMoved();
}

void TextArea::selectLineAt(std::size_t byteIndex) {
	std::size_t idx = std::min(byteIndex, m_text.size());
	std::size_t nl = idx == 0 ? std::string::npos : m_text.rfind('\n', idx - 1);
	std::size_t start = (nl == std::string::npos) ? 0 : nl + 1;
	std::size_t end = m_text.find('\n', idx);
	if (end == std::string::npos) {
		end = m_text.size();
	}
	m_anchor = start;
	m_caret = end;
	markMoved();
}

bool TextArea::blinkVisible() const {
	float elapsed = std::max(0.0f, m_time - m_lastCaretMoveTime);
	return std::fmod(elapsed, kBlinkPeriodSeconds) < kBlinkPeriodSeconds * kBlinkVisibleFraction;
}

bool TextArea::isFocusedFor(const GuiContext& ctx) const {
	return ctx.focusedId() == id();
}

// ---- input dispatch ---------------------------------------------------------------

void TextArea::handleMouse(GuiContext& ctx, bool isActive) {
	if (!isActive) {
		m_draggingScrollbar = false;
		return;
	}
	const InputState& in = ctx.input();
	const int leftIdx = static_cast<int>(MouseButton::Left);
	bool leftPressed = in.mousePressed[leftIdx];
	bool leftDown = in.mouseDown[leftIdx];

	float maxScrollY = maxScroll(ctx);
	Rect track = scrollTrack(ctx);
	if (leftPressed && maxScrollY > 0.0f && track.contains(in.mousePos)) {
		Rect thumb = thumbRect(track, contentHeight(ctx), viewHeight(ctx), m_scrollY, maxScrollY);
		m_draggingScrollbar = true;
		// Grabbing the thumb keeps the point under the cursor; clicking the track
		// recentres the thumb on the cursor.
		m_scrollGrabOffset = thumb.contains(in.mousePos) ? in.mousePos.y - thumb.y : thumb.h * 0.5f;
	}
	if (m_draggingScrollbar) {
		if (leftDown) {
			Rect thumb = thumbRect(track, contentHeight(ctx), viewHeight(ctx), m_scrollY, maxScrollY);
			float travel = track.h - thumb.h;
			if (travel > 0.0f) {
				float t = (in.mousePos.y - m_scrollGrabOffset - track.y) / travel;
				m_scrollY = std::clamp(t, 0.0f, 1.0f) * maxScrollY;
			}
		} else {
			m_draggingScrollbar = false;
		}
		return;
	}

	if (leftPressed) {
		bool withinWindow = (m_time - m_lastClickTime) < kMultiClickSeconds;
		m_clickCount = withinWindow ? (m_clickCount % 3) + 1 : 1;
		m_lastClickTime = m_time;

		std::size_t idx = hitIndexAt(ctx, in.mousePos);
		if (m_clickCount == 2) {
			selectWordAt(idx);
		} else if (m_clickCount >= 3) {
			selectLineAt(idx);
		} else {
			m_caret = m_anchor = idx;
			markMoved();
		}
	} else if (leftDown) {
		// Dragging above/below the box maps to lines outside the view, and
		// ensureCaretVisible() then scrolls toward them -- the drag auto-scroll.
		std::size_t idx = hitIndexAt(ctx, in.mousePos);
		if (idx != m_caret) {
			m_caret = idx;
			markMoved();
		}
	}
}

void TextArea::handleKeyboard(GuiContext& ctx) {
	const InputState& in = ctx.input();
	if (!m_readOnly) {
		for (std::uint32_t cp : in.charQueue) {
			std::string s;
			utf8Encode(cp, s);
			insertText(s);
		}
	}

	float lineH = ctx.font().lineHeight(ctx.theme().fontSize);
	int pageRows = std::max(1, static_cast<int>(viewHeight(ctx) / lineH) - 1);

	for (const KeyEvent& ev : in.keyQueue) {
		if (!ev.pressed) {
			continue;
		}
		bool shift = (ev.mods & Mod::Shift) != 0;
		bool ctrl  = (ev.mods & Mod::Ctrl) != 0;

		// Anything that depends on wrapped lines needs the layout to reflect edits made
		// earlier in this same frame.
		ensureLayout(ctx);

		if (ev.key == Key::Backspace) {
			deleteBackward();
		} else if (ev.key == Key::Delete) {
			deleteForward();
		} else if (ev.key == Key::Left) {
			ctrl ? moveCaretWord(-1, shift) : moveCaret(-1, shift);
		} else if (ev.key == Key::Right) {
			ctrl ? moveCaretWord(1, shift) : moveCaret(1, shift);
		} else if (ev.key == Key::Up) {
			moveCaretVertical(-1, shift);
		} else if (ev.key == Key::Down) {
			moveCaretVertical(1, shift);
		} else if (ev.key == Key::PageUp) {
			moveCaretVertical(-pageRows, shift);
		} else if (ev.key == Key::PageDown) {
			moveCaretVertical(pageRows, shift);
		} else if (ev.key == Key::Home) {
			ctrl ? moveDocumentStart(shift) : moveLineStart(shift);
		} else if (ev.key == Key::End) {
			ctrl ? moveDocumentEnd(shift) : moveLineEnd(shift);
		} else if (ev.key == Key::A) {
			if (ctrl) {
				selectAll();
			}
		} else if (ev.key == Key::C) {
			if (ctrl && hasSelection()) {
				ctx.setClipboardText(copy());
			}
		} else if (ev.key == Key::X) {
			if (ctrl && !m_readOnly && hasSelection()) {
				ctx.setClipboardText(cut());
			}
		} else if (ev.key == Key::V) {
			if (ctrl && !m_readOnly) {
				paste(ctx.clipboardText());
			}
		} else if (ev.key == Key::Enter) {
			if (ctrl) {
				if (m_onSubmit) {
					m_onSubmit(m_text);
				}
			} else {
				insertText("\n");
			}
		}
	}
}

// ---- Widget overrides ---------------------------------------------------------------

Vec2 TextArea::preferredSize(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	return { 0.0f, static_cast<float>(m_visibleRows) * ctx.font().lineHeight(th.fontSize) + 2.0f * th.framePadding };
}

void TextArea::update(GuiContext& ctx) {
	if (m_bindTarget && *m_bindTarget != m_text) {
		m_text = *m_bindTarget;
		m_caret = std::min(m_caret, m_text.size());
		m_anchor = std::min(m_anchor, m_text.size());
		m_layoutDirty = true;
	}
	ensureLayout(ctx);
	m_time += ctx.input().deltaTime;

	if (m_enabled) {
		handleMouse(ctx, ctx.activeId() == id());
		if (isFocusedFor(ctx)) {
			handleKeyboard(ctx);
		}
		const InputState& in = ctx.input();
		if (ctx.hoveredId() == id()) {
			ctx.requestCursorShape(scrollTrack(ctx).contains(in.mousePos) ? CursorShape::Arrow : CursorShape::TextInput);
			if (in.wheelDelta != 0.0f) {
				// Same "3 x lineHeight" per notch as Panel's scrollbar.
				m_scrollY -= in.wheelDelta * 3.0f * ctx.font().lineHeight(ctx.theme().fontSize);
			}
			// Middle-click pastes the most recent text selection (X11 idiom).
			if (!m_readOnly && in.mousePressed[static_cast<int>(MouseButton::Middle)]) {
				const std::string primary = ctx.primaryText();
				if (!primary.empty()) {
					ctx.setFocus(this);
					m_caret = m_anchor = hitIndexAt(ctx, in.mousePos);
					paste(primary);
				}
			}
		}
		publishOwnSelection(ctx);
	}

	ensureLayout(ctx);   // edits above may have changed the text
	if (m_caretMoved) {
		m_lastCaretMoveTime = m_time;
		m_caretMoved = false;
		ensureCaretVisible(ctx);
	}
	m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll(ctx));
}

void TextArea::publishOwnSelection(GuiContext& ctx) {
	if (!isFocusedFor(ctx)) {
		return;
	}
	if (hasSelection()) {
		const std::string selected = copy();
		if (ctx.selectionOwner() != id() || ctx.selectedText() != selected) {
			ctx.publishSelection(id(), selected);
		}
	} else {
		ctx.clearSelection(id());
	}
}

void TextArea::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	const Rect& box = m_bounds;
	bool focused = isFocusedFor(ctx);

	Color bg;
	if (!effectivelyEnabled()) {
		bg = th.frameBg.withAlpha(0.4f);
	} else if (focused) {
		bg = th.frameBgActive;
	} else if (ctx.hoveredId() == id()) {
		bg = th.frameBgHovered;
	} else {
		bg = th.frameBg;
	}
	dl.addRectFilled(box, bg, th.rounding);
	dl.addRect(box, focused ? th.accent : th.border, 1.0f, th.rounding);

	Rect inner = innerRect(ctx);
	Rect textClip{ inner.x, inner.y, std::max(0.0f, inner.w - th.scrollbarWidth - th.framePadding * 0.5f), inner.h };
	float lineH = ctx.font().lineHeight(th.fontSize);
	Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;

	dl.pushClipRect(textClip);
	if (m_text.empty()) {
		dl.addText(ctx.font(), th.fontSize, { inner.x, inner.y }, th.textDisabled, m_placeholder);
	} else if (!m_lines.empty()) {
		std::size_t first = static_cast<std::size_t>(std::max(0.0f, std::floor(m_scrollY / lineH)));
		std::size_t last = std::min(m_lines.size() - 1,
			static_cast<std::size_t>(std::max(0.0f, std::floor((m_scrollY + inner.h) / lineH))));
		bool showSelection = focused && hasSelection();
		std::size_t selStart = selectionStart(), selEnd = selectionEnd();

		for (std::size_t i = first; i <= last; ++i) {
			const VisualLine& l = m_lines[i];
			float y = inner.y + static_cast<float>(i) * lineH - m_scrollY;
			std::string_view text = lineText(i);

			if (showSelection) {
				std::size_t lo = std::max(selStart, l.start), hi = std::min(selEnd, l.end);
				bool hardEnd = !isSoftLineEnd(i);
				bool coversNewline = hardEnd && selStart <= l.end && selEnd > l.end && i + 1 < m_lines.size();
				if (lo < hi || coversNewline) {
					float x0 = inner.x + ctx.font().offsetAtIndex(text, th.fontSize, std::min(lo, l.end) - l.start);
					float x1 = inner.x + ctx.font().offsetAtIndex(text, th.fontSize, std::max(hi, std::min(lo, l.end)) - l.start);
					if (coversNewline) {
						x1 += th.fontSize * 0.4f;   // show the selected line break
					}
					dl.addRectFilled(Rect{ x0, y, x1 - x0, lineH }, th.selectionBg);
				}
			}
			dl.addText(ctx.font(), th.fontSize, { inner.x, y }, textColor, text);
		}

		if (focused && effectivelyEnabled() && blinkVisible()) {
			std::size_t li = lineForIndex(m_caret);
			float caretX = inner.x + ctx.font().offsetAtIndex(lineText(li), th.fontSize,
				std::min(m_caret, m_lines[li].end) - m_lines[li].start);
			float caretY = inner.y + static_cast<float>(li) * lineH - m_scrollY;
			dl.addRectFilled(Rect{ caretX, caretY, 1.0f, lineH }, th.text);
		}
	}
	dl.popClipRect();

	float maxScrollY = maxScroll(ctx);
	if (maxScrollY > 0.0f) {
		Rect track = scrollTrack(ctx);
		dl.addRectFilled(track, th.scrollbarBg, th.rounding);
		dl.addRectFilled(thumbRect(track, contentHeight(ctx), viewHeight(ctx), m_scrollY, maxScrollY),
			th.scrollbarGrab, th.rounding);
	}
}

} // namespace lightGraphics::ui
