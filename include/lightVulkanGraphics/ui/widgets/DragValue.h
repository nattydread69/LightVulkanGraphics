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

// docs/gui/05-widgets.md, "DragValue": an unbounded numeric scrubber -- no track, no
// hard range. Press and drag horizontally; the value changes by dx * speed, scaled 0.1x
// with Shift and 10x with Alt/Ctrl. setSoftRange clamps the result but does not change
// how cursor movement maps to value, unlike Slider's track-relative mapping.

#include "../Widget.h"

#include <functional>
#include <memory>
#include <string>

namespace lightGraphics::ui {

class TextBox;

template <typename T>
class DragValueT : public Widget {
public:
	DragValueT(std::string label, T initial, float speed = 0.1f);

	T value() const { return m_value; }
	void setValue(T value, bool fireCallback = false);
	// The pointer must outlive the widget -- same bind() contract as every other value
	// widget in this library.
	void bind(T* target) { m_bindTarget = target; }
	void setSpeed(float unitsPerPixel) { m_speed = unitsPerPixel; }
	void setSoftRange(T lo, T hi) { m_hasSoftRange = true; m_softLo = lo; m_softHi = hi; }
	void setFormat(std::string printfFmt) { m_format = std::move(printfFmt); }

	void setOnChange(std::function<void(T)> onChange) { m_onChange = std::move(onChange); }
	void setOnCommit(std::function<void(T)> onCommit) { m_onCommit = std::move(onCommit); }

	// Same rationale and contract as SliderT::setCtrlClickHook -- see that comment.
	void setCtrlClickHook(std::function<void(DragValueT<T>&, GuiContext&)> hook) { m_onCtrlClick = std::move(hook); }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus()   const override { return true; }
	// True only while the inline TextBox edit session is open -- see beginTextEdit().
	bool wantsTextInput() const override { return m_editBox != nullptr; }

private:
	T clampSoft(T value) const;
	std::string formatValue() const;
	std::string formatValueForEdit() const;
	void setValueInternal(T value, bool fireOnChange, bool fireOnCommit);
	void beginTextEdit(GuiContext&);
	bool updateTextEdit(GuiContext&);   // see SliderT::updateTextEdit

	T m_value;
	T m_softLo{}, m_softHi{};
	bool m_hasSoftRange = false;
	float m_speed;
	std::string m_format;

	T* m_bindTarget = nullptr;
	std::function<void(T)> m_onChange;
	std::function<void(T)> m_onCommit;
	std::function<void(DragValueT<T>&, GuiContext&)> m_onCtrlClick;

	bool m_dragging = false;
	float m_dragLastMouseX = 0.0f;
	// Fractional units-per-pixel accumulator: for integral T, a single frame's dx*speed
	// is often < 1 unit and would truncate to zero every frame if applied immediately,
	// silently eating slow drags. Carried across frames so it still adds up.
	float m_dragAccum = 0.0f;

	// docs/gui/05, "Inline text entry" (via DragValue's "Ctrl-click for text entry, as
	// with Slider") -- same ownership/lifetime contract as SliderT::m_editBox.
	std::unique_ptr<TextBox> m_editBox;
};

extern template class DragValueT<float>;
extern template class DragValueT<int>;

} // namespace lightGraphics::ui
