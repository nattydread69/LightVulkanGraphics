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

#include <lightVulkanGraphics/ui/widgets/ProgressBar.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>
#include <cmath>

namespace lightGraphics::ui {

ProgressBar::ProgressBar(std::string label) {
	setLabel(std::move(label));
}

void ProgressBar::setFraction(float frac) {
	m_fraction = std::clamp(frac, 0.0f, 1.0f);
}

void ProgressBar::setIndeterminate(bool indeterminate) {
	m_indeterminate = indeterminate;
}

void ProgressBar::setOverlayText(std::string text) {
	m_overlayText = std::move(text);
}

Vec2 ProgressBar::preferredSize(const GuiContext& ctx) const {
	return { 0.0f, ctx.theme().rowHeight };
}

void ProgressBar::update(GuiContext& ctx) {
	if (m_indeterminate) {
		// Accumulate frame deltaTime
		m_accumulatedTime += ctx.input().deltaTime;
		// Wrap at 1.5 seconds
		if (m_accumulatedTime >= 1.5) {
			m_accumulatedTime -= 1.5;
		}
	}
}

void ProgressBar::draw(DrawList& dl, const GuiContext& ctx) const {
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

	// Draw background
	dl.addRectFilled(box, th.frameBg, th.rounding);
	dl.addRect(box, th.border, 1.0f, th.rounding);

	// Draw progress or indeterminate animation
	Rect fillRect = box.inset(1.0f);  // Inset by border width

	if (m_indeterminate) {
		// Sweep animation: 30% width bar moving left to right over 1.5 seconds
		float barWidth = fillRect.w * 0.3f;
		float position = std::fmod(static_cast<float>(m_accumulatedTime / 1.5), 1.0f);
		float barX = fillRect.x + position * (fillRect.w - barWidth);

		Rect sweepRect{ barX, fillRect.y, barWidth, fillRect.h };
		dl.addRectFilled(sweepRect, th.accent, th.rounding);
	} else {
		// Static fill based on fraction
		float fillWidth = fillRect.w * m_fraction;
		Rect filledRect{ fillRect.x, fillRect.y, fillWidth, fillRect.h };
		dl.addRectFilled(filledRect, th.accent, th.rounding);
	}

	// Draw overlay text if provided
	if (!m_overlayText.empty()) {
		Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, box, textColor, m_overlayText, Align::Center, Align::Center);
	}
}

} // namespace lightGraphics::ui
