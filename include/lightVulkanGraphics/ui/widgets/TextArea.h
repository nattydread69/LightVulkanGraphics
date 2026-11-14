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

// A multi-line text editor: TextBox's sibling for large or multi-line text (pasting a
// log, editing notes). Same editing model as TextBox -- UTF-8 buffer, byte-index caret
// and anchor on codepoint boundaries, selection, clipboard, primary-selection paste --
// plus newlines, optional word wrap, vertical caret movement, a wheel/drag scrollbar and
// a fixed height set in rows.
//
// The editing operations (insertText, deleteBackward, ...) mutate only the text, caret
// and anchor and need no GuiContext, so they can be tested headless. Operations that
// depend on the wrapped layout (moveCaretVertical, moveLineStart, ...) use the layout
// computed by computeLayout()/update(); callers outside update() must call computeLayout() first.
//
// The widget has no label column: it takes the full row width. Put a Label above it.
// Enter inserts a newline; Ctrl+Enter fires onSubmit. Pasted CR/CRLF become LF, tabs
// become four spaces and other control characters are dropped.

#include "../Widget.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

class Font;

class TextArea : public Widget {
public:
	explicit TextArea(std::string initial = {});

	std::string_view text() const { return m_text; }
	void setText(std::string text, bool fireCallback = false);
	// The pointer must outlive the widget -- same bind() contract as TextBox.
	void bind(std::string* target) { m_bindTarget = target; }

	void setPlaceholder(std::string placeholder) { m_placeholder = std::move(placeholder); }
	void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
	// With wrapping off, long lines are clipped at the right edge (no horizontal scroll).
	void setWordWrap(bool wrap);
	bool wordWrap() const { return m_wrap; }
	// Height of the editor in text rows, regardless of content. Default 8.
	void setVisibleRows(int rows) { m_visibleRows = rows < 1 ? 1 : rows; }

	void setOnChange(std::function<void(std::string_view)> onChange) { m_onChange = std::move(onChange); }
	// Ctrl+Enter (plain Enter inserts a newline).
	void setOnSubmit(std::function<void(std::string_view)> onSubmit) { m_onSubmit = std::move(onSubmit); }

	// ---- editing operations ----
	void insertText(std::string_view utf8);
	void deleteBackward();
	void deleteForward();
	void moveCaret(int dir, bool select);       // dir: -1 = left, +1 = right
	void moveCaretWord(int dir, bool select);
	void moveDocumentStart(bool select);
	void moveDocumentEnd(bool select);
	void selectAll();
	std::string copy() const;
	std::string cut();
	void paste(std::string_view clipboardText);

	// ---- layout-dependent operations (need a current computeLayout()) ----
	// lines > 0 moves down. Keeps the horizontal position across consecutive moves. On
	// the first/last line, moves to the start/end of the text, as most editors do.
	void moveCaretVertical(int lines, bool select);
	void moveLineStart(bool select);
	void moveLineEnd(bool select);

	// Recomputes the visual lines (layout) for `width` pixels of text. update() does this itself
	// whenever the text, width or font size changed.
	void computeLayout(const Font& font, float pixelSize, float width);
	std::size_t lineCount() const { return m_lines.size(); }
	// Visual line holding byte index `idx` (a wrapped line's end belongs to the next line).
	std::size_t lineForIndex(std::size_t idx) const;
	std::string_view lineText(std::size_t line) const;

	std::size_t caretIndex()  const { return m_caret; }
	std::size_t anchorIndex() const { return m_anchor; }
	bool hasSelection() const { return m_caret != m_anchor; }
	std::size_t selectionStart() const { return m_caret < m_anchor ? m_caret : m_anchor; }
	std::size_t selectionEnd()   const { return m_caret < m_anchor ? m_anchor : m_caret; }
	float scrollY() const { return m_scrollY; }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus()   const override { return true; }
	bool wantsTextInput() const override { return true; }
	bool wantsWheel()     const override { return true; }

private:
	struct VisualLine {
		std::size_t start;
		std::size_t end;   // exclusive; excludes the '\n' that ends a hard line
	};

	void wrapLogicalLine(const Font& font, float pixelSize, float width,
	                     std::size_t start, std::size_t end);
	void ensureLayout(const GuiContext& ctx);
	Rect innerRect(const GuiContext& ctx) const;
	Rect scrollTrack(const GuiContext& ctx) const;
	float contentHeight(const GuiContext& ctx) const;
	float viewHeight(const GuiContext& ctx) const;
	float maxScroll(const GuiContext& ctx) const;
	void ensureCaretVisible(const GuiContext& ctx);
	bool isSoftLineEnd(std::size_t line) const;   // wrapped (not newline-terminated) and not last
	std::size_t lineEndCaret(std::size_t line) const;
	float caretXInLine(const Font& font, float pixelSize, std::size_t line, std::size_t idx) const;
	std::size_t indexInLineAt(const Font& font, float pixelSize, std::size_t line, float x) const;
	std::size_t hitIndexAt(const GuiContext& ctx, Vec2 p) const;
	void selectWordAt(std::size_t byteIndex);
	void selectLineAt(std::size_t byteIndex);

	void handleMouse(GuiContext& ctx, bool isActive);
	void handleKeyboard(GuiContext& ctx);
	void eraseSelection();
	void fireChange();
	void markMoved(bool vertical = false) { m_caretMoved = true; if (!vertical) m_preferredX = -1.0f; }
	bool isFocusedFor(const GuiContext& ctx) const;
	bool blinkVisible() const;
	void publishOwnSelection(GuiContext& ctx);

	std::string m_text;
	std::size_t m_caret  = 0;   // byte index, always on a codepoint boundary
	std::size_t m_anchor = 0;
	float       m_scrollY = 0.0f;
	float       m_preferredX = -1.0f;   // < 0: take it from the caret on the next vertical move

	std::vector<VisualLine> m_lines;
	bool  m_layoutDirty = true;
	float m_layoutWidth = -1.0f;
	float m_layoutPixelSize = -1.0f;
	const Font* m_font = nullptr;   // the font layout() last measured with

	std::string m_placeholder;
	std::string* m_bindTarget = nullptr;
	std::function<void(std::string_view)> m_onChange;
	std::function<void(std::string_view)> m_onSubmit;
	bool m_readOnly = false;
	bool m_wrap = true;
	int  m_visibleRows = 8;

	bool  m_draggingScrollbar = false;
	float m_scrollGrabOffset = 0.0f;
	float m_time = 0.0f;
	float m_lastCaretMoveTime = 0.0f;
	bool  m_caretMoved = false;
	int   m_clickCount = 0;
	float m_lastClickTime = -1000.0f;
};

} // namespace lightGraphics::ui
