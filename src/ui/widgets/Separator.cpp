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

#include <lightVulkanGraphics/ui/widgets/Separator.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

Vec2 Separator::preferredSize(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	if (m_caption.empty()) {
		return { 0.0f, th.itemSpacing * 2.0f + 1.0f };
	}
	return { 0.0f, ctx.font().lineHeight(th.fontSize) };
}

void Separator::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	float midY = m_bounds.y + m_bounds.h * 0.5f;

	if (m_caption.empty()) {
		dl.addLine({ m_bounds.x, midY }, { m_bounds.right(), midY }, th.border, 1.0f);
		return;
	}

	const Font& font = ctx.font();
	float textW = font.measureText(m_caption, th.fontSize).x;
	constexpr float kLeadIn = 12.0f;

	float leftLineEnd = m_bounds.x + kLeadIn;
	dl.addLine({ m_bounds.x, midY }, { leftLineEnd, midY }, th.border, 1.0f);

	float textX = leftLineEnd + th.itemSpacing;
	float lh = font.lineHeight(th.fontSize);
	Rect textRect{ textX, m_bounds.y, textW + 2.0f, lh };
	dl.addTextClipped(font, th.fontSize, textRect, th.text, m_caption, Align::Start, Align::Center);

	float rightLineStart = textX + textW + th.itemSpacing;
	if (rightLineStart < m_bounds.right()) {
		dl.addLine({ rightLineStart, midY }, { m_bounds.right(), midY }, th.border, 1.0f);
	}
}

} // namespace lightGraphics::ui
