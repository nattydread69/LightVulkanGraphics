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

// docs/gui/05-widgets.md, "TextBox": the hardest widget in the library. The editing
// state machine is exposed as individually testable named methods (insertText,
// deleteBackward, ...) that mutate only m_text/m_caret/m_anchor/m_scrollX (plus firing
// onChange/bind, the same contract every other value widget's internal setValueInternal
// already follows) -- see docs/gui/08-testing.md's random-operation property test, which
// calls these directly with no GuiContext at all. update()/draw() are the only pieces
// that need a GuiContext, and they do nothing but map input events onto these calls.

#include "../Widget.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace lightGraphics::ui {

enum class TextFilter { None, Integer, Decimal, Identifier };

class TextBox : public Widget {
public:
	// Codepoint used to mask password-mode text (docs/gui/05, "TextBox", password mode).
	// U+2022 BULLET is the conventional choice but is outside every range
	// Font::kDefaultGlyphRanges bakes; U+00B7 MIDDLE DOT sits inside the baked Basic
	// Latin/Latin-1 range and reads the same way. Public so GuiContext's post-bake
	// internal-glyph check (see verifyInternalGlyphsBaked in GuiContext.cpp) can verify
	// it is actually baked without a second, driftable copy of the codepoint.
	static constexpr std::uint32_t kPasswordMaskCodepoint = 0x00B7;

	explicit TextBox(std::string label, std::string initial = {});

	std::string_view text() const { return m_text; }
	void setText(std::string text, bool fireCallback = false);
	// The pointer must outlive the widget -- same bind() contract as every other value
	// widget in this library (update() reads *target at the start of every frame).
	void bind(std::string* target) { m_bindTarget = target; }

	void setPlaceholder(std::string placeholder) { m_placeholder = std::move(placeholder); }
	void setMaxLength(std::size_t maxCodepoints) { m_maxLength = maxCodepoints; }   // 0 = unlimited
	void setFilter(TextFilter filter) { m_filter = filter; }
	void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
	void setPasswordMode(bool password) { m_passwordMode = password; }

	void setOnChange(std::function<void(std::string_view)> onChange) { m_onChange = std::move(onChange); }
	void setOnCommit(std::function<void(std::string_view)> onCommit) { m_onCommit = std::move(onCommit); }
	void setValidator(std::function<bool(std::string_view)> validator) { m_validator = std::move(validator); }

	// ---- editing operations (docs/gui/05, "Editing operations") ----
	void insertText(std::string_view utf8);
	void deleteBackward();
	void deleteForward();
	void moveCaret(int dir, bool select);       // dir: -1 = left, +1 = right
	void moveCaretWord(int dir, bool select);
	void moveHome(bool select);
	void moveEnd(bool select);
	void selectAll();
	std::string copy() const;
	std::string cut();
	void paste(std::string_view clipboardText);   // strips control characters and newlines
	void commit();          // fires onCommit, keeps text
	void revert();           // restores the text captured when focus was gained

	std::size_t caretIndex()  const { return m_caret; }
	std::size_t anchorIndex() const { return m_anchor; }
	bool hasSelection() const { return m_caret != m_anchor; }
	std::size_t selectionStart() const { return m_caret < m_anchor ? m_caret : m_anchor; }
	std::size_t selectionEnd()   const { return m_caret < m_anchor ? m_anchor : m_caret; }

	// For an embedding widget (SliderT/DragValueT's Ctrl-click inline entry, docs/gui/05
	// "Inline text entry") that owns a TextBox privately, outside any Panel:
	// GuiContext's focus/capture resolution only ever reaches panel-owned widgets, so an
	// embedding owner reports its own focus state here instead of this box ever
	// appearing in ctx.focusedId().
	void setForcedFocus(bool focused) { m_forcedFocus = focused; }
	// Drives mouse + keyboard editing unconditionally, without consulting
	// ctx.focusedId()/ctx.activeId() (see setForcedFocus). Enter/Escape are deliberately
	// NOT handled here -- the owner needs to intercept them itself (numeric parse-and-
	// commit vs. this class's own plain string commit()/revert()), so it inspects
	// ctx.input().keyQueue for them after calling this.
	void updateEmbedded(GuiContext& ctx);

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus()   const override { return true; }
	bool wantsTextInput() const override { return true; }

private:
	void handleMouse(GuiContext& ctx, bool isActive);
	// Returns true iff Escape was processed (revert() + ctx.clearFocus() already ran) --
	// update() needs to know, because GuiContext::updateFocusNavigation() (docs/gui/01)
	// clears ctx.focusedId() on Escape BEFORE any widget's own update() runs, so by the
	// time update() can compare ctx.focusedId() again, an Escape-driven loss and an
	// external one (click/Tab elsewhere) already look identical from the outside.
	bool handleKeyboard(GuiContext& ctx, bool isEmbedded);
	void eraseSelection();
	void fireChange();
	void markMoved() { m_caretMoved = true; }
	bool passesFilter(std::uint32_t codepoint) const;
	bool isFocusedFor(const GuiContext& ctx) const;
	std::string displayText() const;   // m_text, or the bullet string in password mode
	float pixelOffsetForCaret(const GuiContext& ctx, std::size_t byteIndex) const;
	std::size_t hitIndexAt(const GuiContext& ctx, float mouseX) const;
	void selectWordAt(std::size_t byteIndex);
	void updateScroll(const GuiContext& ctx);
	bool blinkVisible() const;

	std::string m_text;
	std::size_t m_caret  = 0;   // byte index, always on a codepoint boundary
	std::size_t m_anchor = 0;   // selection anchor; caret == anchor means no selection
	float       m_scrollX = 0.0f;
	std::string m_textBeforeEdit;   // for Escape revert / focus-loss commit

	std::string m_placeholder;
	std::size_t m_maxLength = 0;   // codepoints, 0 = unlimited
	TextFilter  m_filter = TextFilter::None;
	bool m_readOnly = false;
	bool m_passwordMode = false;

	std::string* m_bindTarget = nullptr;
	std::function<void(std::string_view)> m_onChange;
	std::function<void(std::string_view)> m_onCommit;
	std::function<bool(std::string_view)> m_validator;

	bool  m_wasFocused = false;
	bool  m_forcedFocus = false;   // see setForcedFocus
	float m_time = 0.0f;                 // accumulated deltaTime, for blink phase
	float m_lastCaretMoveTime = 0.0f;    // blink resets to fully visible on every caret move
	bool  m_caretMoved = false;

	int   m_clickCount = 0;        // 1 = single, 2 = double (select word), 3+ = triple (select all)
	float m_lastClickTime = -1000.0f;
};

} // namespace lightGraphics::ui
