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

#include <lightVulkanGraphics/ui/widgets/TextBox.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "../Utf8.h"

#include <cmath>

namespace lightGraphics::ui {

namespace {
	constexpr float kMultiClickSeconds = 0.35f;
	constexpr float kBlinkPeriodSeconds = 1.06f;
	constexpr float kBlinkVisibleFraction = 0.53f;

	bool isAsciiWhitespace(std::uint32_t cp) {
		return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\v' || cp == '\f';
	}

	std::size_t codepointCount(std::string_view s) {
		std::size_t n = 0, pos = 0;
		while (pos < s.size()) {
			pos = utf8NextBoundary(s, pos);
			++n;
		}
		return n;
	}

	std::size_t nthCodepointBoundary(std::string_view s, std::size_t n) {
		std::size_t pos = 0;
		for (std::size_t i = 0; i < n && pos < s.size(); ++i) {
			pos = utf8NextBoundary(s, pos);
		}
		return pos;
	}
}

TextBox::TextBox(std::string label, std::string initial) : m_text(std::move(initial)) {
	setLabel(std::move(label));
	m_caret = m_anchor = m_text.size();
}

// ---- editing operations --------------------------------------------------------

bool TextBox::passesFilter(std::uint32_t cp) const {
	switch (m_filter) {
		case TextFilter::None:
			return true;
		case TextFilter::Integer:
			return (cp >= '0' && cp <= '9') || cp == '-';
		case TextFilter::Decimal:
			return (cp >= '0' && cp <= '9') || cp == '-' || cp == '+' || cp == '.' || cp == 'e' || cp == 'E';
		case TextFilter::Identifier:
			return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_';
	}
	return true;
}

void TextBox::fireChange() {
	if (m_bindTarget) {
		*m_bindTarget = m_text;
	}
	if (m_onChange) {
		m_onChange(m_text);
	}
}

void TextBox::eraseSelection() {
	std::size_t s = selectionStart(), e = selectionEnd();
	m_text.erase(s, e - s);
	m_caret = m_anchor = s;
	markMoved();
	fireChange();
}

void TextBox::insertText(std::string_view utf8In) {
	if (m_readOnly) {
		return;
	}

	std::string filtered;
	std::size_t pos = 0;
	while (pos < utf8In.size()) {
		std::uint32_t cp = decodeUtf8(utf8In, pos);
		if (passesFilter(cp)) {
			utf8Encode(cp, filtered);
		}
	}

	bool hadSelection = hasSelection();
	if (hadSelection) {
		std::size_t s = selectionStart(), e = selectionEnd();
		m_text.erase(s, e - s);
		m_caret = m_anchor = s;
	}

	if (m_maxLength > 0) {
		std::size_t existing = codepointCount(m_text);
		std::size_t room = (existing < m_maxLength) ? (m_maxLength - existing) : 0;
		if (codepointCount(filtered) > room) {
			filtered = filtered.substr(0, nthCodepointBoundary(filtered, room));
		}
	}

	if (!filtered.empty()) {
		m_text.insert(m_caret, filtered);
		m_caret += filtered.size();
	}
	m_anchor = m_caret;

	if (hadSelection || !filtered.empty()) {
		markMoved();
		fireChange();
	}
}

void TextBox::deleteBackward() {
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

void TextBox::deleteForward() {
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

void TextBox::moveCaret(int dir, bool select) {
	// docs/gui/05: Right (or Left) with an active selection and no Shift collapses the
	// caret to the corresponding edge of the selection and deselects -- it does not
	// additionally move one character further.
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

void TextBox::moveCaretWord(int dir, bool select) {
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

void TextBox::moveHome(bool select) {
	m_caret = 0;
	if (!select) {
		m_anchor = 0;
	}
	markMoved();
}

void TextBox::moveEnd(bool select) {
	m_caret = m_text.size();
	if (!select) {
		m_anchor = m_text.size();
	}
	markMoved();
}

void TextBox::selectAll() {
	m_anchor = 0;
	m_caret = m_text.size();
	markMoved();
}

std::string TextBox::copy() const {
	return m_text.substr(selectionStart(), selectionEnd() - selectionStart());
}

std::string TextBox::cut() {
	std::string s = copy();
	if (!m_readOnly && hasSelection()) {
		eraseSelection();
	}
	return s;
}

void TextBox::paste(std::string_view clip) {
	if (m_readOnly) {
		return;
	}
	std::string cleaned;
	std::size_t pos = 0;
	while (pos < clip.size()) {
		std::uint32_t cp = decodeUtf8(clip, pos);
		if (cp == 0x0A || cp == 0x0D || cp < 0x20 || cp == 0x7F) {
			continue;   // strip control characters and newlines
		}
		utf8Encode(cp, cleaned);
	}
	insertText(cleaned);
}

void TextBox::commit() {
	if (m_onCommit) {
		m_onCommit(m_text);
	}
}

void TextBox::revert() {
	m_text = m_textBeforeEdit;
	m_caret = m_anchor = m_text.size();
	if (m_bindTarget) {
		*m_bindTarget = m_text;
	}
	markMoved();
}

void TextBox::setText(std::string text, bool fireCallback) {
	m_text = std::move(text);
	m_caret = m_anchor = m_text.size();
	markMoved();
	if (m_bindTarget) {
		*m_bindTarget = m_text;
	}
	if (fireCallback && m_onChange) {
		m_onChange(m_text);
	}
}

// ---- display / measurement helpers ----------------------------------------------

std::string TextBox::displayText() const {
	if (!m_passwordMode) {
		return m_text;
	}
	std::string out;
	std::size_t pos = 0;
	while (pos < m_text.size()) {
		decodeUtf8(m_text, pos);
		utf8Encode(kPasswordMaskCodepoint, out);
	}
	return out;
}

float TextBox::pixelOffsetForCaret(const GuiContext& ctx, std::size_t byteIndex) const {
	const Font& font = ctx.font();
	float pixelSize = ctx.theme().fontSize;
	if (!m_passwordMode) {
		return font.offsetAtIndex(m_text, pixelSize, byteIndex);
	}
	// Password mode measures the DISPLAYED bullet string, not the source (docs/gui/05):
	// translate the source byte index into a codepoint count, then into the equivalent
	// byte offset in the (fixed-width-per-codepoint) bullet string.
	std::size_t cpIndex = codepointCount(m_text.substr(0, byteIndex));
	std::string bulletOne;
	utf8Encode(kPasswordMaskCodepoint, bulletOne);
	std::string disp = displayText();
	return font.offsetAtIndex(disp, pixelSize, cpIndex * bulletOne.size());
}

std::size_t TextBox::hitIndexAt(const GuiContext& ctx, float mouseX) const {
	RowSplit split = splitRow(ctx);
	const Rect& box = split.control;
	const Theme& th = ctx.theme();
	float localX = mouseX - (box.x + th.framePadding) + m_scrollX;
	const Font& font = ctx.font();
	if (!m_passwordMode) {
		return font.indexAtOffset(m_text, th.fontSize, localX);
	}
	std::string disp = displayText();
	std::size_t bulletByteIdx = font.indexAtOffset(disp, th.fontSize, localX);
	std::string bulletOne;
	utf8Encode(kPasswordMaskCodepoint, bulletOne);
	std::size_t bulletLen = bulletOne.empty() ? 1 : bulletOne.size();
	std::size_t cpIndex = bulletByteIdx / bulletLen;
	return nthCodepointBoundary(m_text, cpIndex);
}

void TextBox::selectWordAt(std::size_t byteIndex) {
	std::size_t idx = byteIndex <= m_text.size() ? byteIndex : m_text.size();
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

void TextBox::updateScroll(const GuiContext& ctx) {
	RowSplit split = splitRow(ctx);
	const Rect& box = split.control;
	const Theme& th = ctx.theme();
	float boxWidth = box.w - 2.0f * th.framePadding;
	if (boxWidth <= 0.0f) {
		m_scrollX = 0.0f;
		return;
	}
	float caretPixel = pixelOffsetForCaret(ctx, m_caret);
	float rel = caretPixel - m_scrollX;
	if (rel < 0.0f) {
		m_scrollX += rel;
	} else if (rel > boxWidth) {
		m_scrollX += (rel - boxWidth);
	}
	if (m_scrollX < 0.0f) {
		m_scrollX = 0.0f;
	}
}

bool TextBox::blinkVisible() const {
	float elapsed = m_time - m_lastCaretMoveTime;
	if (elapsed < 0.0f) {
		elapsed = 0.0f;
	}
	float phase = std::fmod(elapsed, kBlinkPeriodSeconds);
	return phase < kBlinkPeriodSeconds * kBlinkVisibleFraction;
}

bool TextBox::isFocusedFor(const GuiContext& ctx) const {
	return m_forcedFocus || ctx.focusedId() == id();
}

// ---- input dispatch ---------------------------------------------------------------

void TextBox::handleMouse(GuiContext& ctx, bool isActive) {
	if (!isActive) {
		return;
	}
	const InputState& in = ctx.input();
	const int leftIdx = static_cast<int>(MouseButton::Left);
	bool leftPressed = in.mousePressed[leftIdx];
	bool leftDown = in.mouseDown[leftIdx];

	if (leftPressed) {
		bool withinWindow = (m_time - m_lastClickTime) < kMultiClickSeconds;
		m_clickCount = withinWindow ? (m_clickCount % 3) + 1 : 1;
		m_lastClickTime = m_time;

		std::size_t idx = hitIndexAt(ctx, in.mousePos.x);
		if (m_clickCount == 2) {
			selectWordAt(idx);
		} else if (m_clickCount >= 3) {
			selectAll();
		} else {
			m_caret = idx;
			m_anchor = idx;
			markMoved();
		}
	} else if (leftDown) {
		// Extend the selection to the cursor; when the cursor is outside the box,
		// Font::indexAtOffset already clamps to the nearest end of the string, and
		// updateScroll() (called once per frame from update()/updateEmbedded()) then
		// pulls scrollX to keep the caret in view -- that combination is the auto-scroll
		// docs/gui/05 asks for, with no separate edge-detection code needed.
		std::size_t idx = hitIndexAt(ctx, in.mousePos.x);
		if (idx != m_caret) {
			m_caret = idx;
			markMoved();
		}
	}
}

bool TextBox::handleKeyboard(GuiContext& ctx, bool isEmbedded) {
	const InputState& in = ctx.input();
	if (!m_readOnly) {
		for (std::uint32_t cp : in.charQueue) {
			std::string s;
			utf8Encode(cp, s);
			insertText(s);
		}
	}

	bool reverted = false;
	for (const KeyEvent& ev : in.keyQueue) {
		if (!ev.pressed) {
			continue;
		}
		bool shift = (ev.mods & Mod::Shift) != 0;
		bool ctrl  = (ev.mods & Mod::Ctrl) != 0;

		if (ev.key == Key::Backspace) {
			if (!m_readOnly) {
				deleteBackward();
			}
		} else if (ev.key == Key::Delete) {
			if (!m_readOnly) {
				deleteForward();
			}
		} else if (ev.key == Key::Left) {
			ctrl ? moveCaretWord(-1, shift) : moveCaret(-1, shift);
		} else if (ev.key == Key::Right) {
			ctrl ? moveCaretWord(1, shift) : moveCaret(1, shift);
		} else if (ev.key == Key::Home) {
			moveHome(shift);
		} else if (ev.key == Key::End) {
			moveEnd(shift);
		} else if (ev.key == Key::A) {
			if (ctrl) {
				selectAll();
			}
		} else if (ev.key == Key::C) {
			if (ctrl) {
				ctx.setClipboardText(copy());
			}
		} else if (ev.key == Key::X) {
			if (ctrl && !m_readOnly) {
				ctx.setClipboardText(cut());
			}
		} else if (ev.key == Key::V) {
			if (ctrl && !m_readOnly) {
				paste(ctx.clipboardText());
			}
		} else if (ev.key == Key::Enter) {
			if (!isEmbedded) {
				commit();
			}
		} else if (ev.key == Key::Escape) {
			if (!isEmbedded) {
				revert();
				ctx.clearFocus();
				reverted = true;
			}
		}
	}
	return reverted;
}

// ---- Widget overrides ---------------------------------------------------------------

Vec2 TextBox::preferredSize(const GuiContext& ctx) const {
	return { 0.0f, ctx.theme().rowHeight };
}

void TextBox::update(GuiContext& ctx) {
	// No m_visible check: Panel::update() already skips update() entirely for hidden
	// widgets (docs/gui/05's own base class docs) -- no other widget in this library
	// re-checks it here either.
	if (m_bindTarget) {
		m_text = *m_bindTarget;
		if (m_caret > m_text.size())  m_caret = m_text.size();
		if (m_anchor > m_text.size()) m_anchor = m_text.size();
	}

	// "Entering" focus state: whether ctx considered this widget focused as of the START
	// of this frame, before this frame's own key events are processed. This deliberately
	// is NOT the same as isFocusedFor(ctx) evaluated right now: GuiContext::
	// updateFocusNavigation() (docs/gui/01) already ran, inside ctx.update(), before any
	// widget's update() -- and it unconditionally clears ctx.focusedId() on Escape. By
	// the time this function runs on an Escape frame, ctx.focusedId() has ALREADY moved
	// on, so comparing against it here would misread "I was just focused and Escape
	// fired" as an indistinguishable external focus loss and fire commit() instead of
	// letting handleKeyboard's own Escape branch revert() silently. m_wasFocused (set at
	// the end of every previous call to this function) is what still remembers the
	// pre-navigation truth.
	bool enteringFocused = m_wasFocused;
	if (isFocusedFor(ctx) && !enteringFocused) {
		m_textBeforeEdit = m_text;
		markMoved();
	}

	m_time += ctx.input().deltaTime;

	bool revertedThisFrame = false;
	if (m_enabled) {
		handleMouse(ctx, ctx.activeId() == id());
		if (enteringFocused) {
			revertedThisFrame = handleKeyboard(ctx, false);
		}
		if (ctx.hoveredId() == id()) {
			ctx.requestCursorShape(CursorShape::TextInput);
		}
	}

	bool hasFocusAfter = isFocusedFor(ctx);
	if (enteringFocused && !hasFocusAfter && !revertedThisFrame) {
		commit();   // docs/gui/05: commit triggers are "Enter, focus loss" -- Escape is
		            // neither; it already reverted above instead.
	}

	m_wasFocused = hasFocusAfter;
	if (m_caretMoved) {
		m_lastCaretMoveTime = m_time;
		m_caretMoved = false;
	}
	updateScroll(ctx);
}

void TextBox::updateEmbedded(GuiContext& ctx) {
	if (!m_wasFocused) {
		m_textBeforeEdit = m_text;
		m_wasFocused = true;
		markMoved();
	}
	m_time += ctx.input().deltaTime;
	if (m_enabled) {
		handleMouse(ctx, true);
		handleKeyboard(ctx, true);
	}
	if (m_caretMoved) {
		m_lastCaretMoveTime = m_time;
		m_caretMoved = false;
	}
	updateScroll(ctx);
}

void TextBox::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	RowSplit split = splitRow(ctx);

	if (!m_label.empty()) {
		Color labelColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, split.label, labelColor, m_label, Align::Start, Align::Center);
	}

	const Rect& box = split.control;
	bool focused = isFocusedFor(ctx);
	bool validationFailed = m_validator && !m_validator(m_text);

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

	Color borderColor = validationFailed ? th.error : (focused ? th.accent : th.border);
	dl.addRect(box, borderColor, 1.0f, th.rounding);

	Rect clip = box.inset(th.framePadding);
	dl.pushClipRect(clip);

	float lineH = ctx.font().lineHeight(th.fontSize);
	Vec2 textOrigin{ clip.x, box.y + (box.h - lineH) * 0.5f };

	bool showingPlaceholder = m_text.empty();
	if (focused && !showingPlaceholder && hasSelection()) {
		float x0 = textOrigin.x - m_scrollX + pixelOffsetForCaret(ctx, selectionStart());
		float x1 = textOrigin.x - m_scrollX + pixelOffsetForCaret(ctx, selectionEnd());
		dl.addRectFilled(Rect{ x0, box.y + 2.0f, x1 - x0, box.h - 4.0f }, th.selectionBg);
	}

	std::string shown = showingPlaceholder ? m_placeholder : displayText();
	Color textColor = showingPlaceholder ? th.textDisabled : (effectivelyEnabled() ? th.text : th.textDisabled);
	dl.addText(ctx.font(), th.fontSize, textOrigin - Vec2{ m_scrollX, 0.0f }, textColor, shown);

	if (focused && effectivelyEnabled() && blinkVisible()) {
		float caretX = textOrigin.x - m_scrollX + pixelOffsetForCaret(ctx, m_caret);
		dl.addRectFilled(Rect{ caretX, box.y + 2.0f, 1.0f, box.h - 4.0f }, th.text);
	}

	dl.popClipRect();
}

} // namespace lightGraphics::ui
