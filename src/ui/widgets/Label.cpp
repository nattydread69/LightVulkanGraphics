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

#include <lightVulkanGraphics/ui/widgets/Label.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "../TextWrap.h"

namespace lightGraphics::ui {

namespace {
	// Resolves which Font/size/texture this Label actually draws with this frame:
	// the heading face (docs/gui/03-text-and-fonts.md, "Headings") only when the widget
	// asked for one AND the GuiContext it's drawn/measured with actually has one baked
	// -- a Label must never crash or silently vanish just because a consumer left
	// GuiCreateInfo::headingFontSize at its disabled 0.0f default.
	struct LabelFont {
		const Font& font;
		float pixelSize;
		TextureId textureId;
	};

	LabelFont resolveLabelFont(const GuiContext& ctx, bool heading) {
		if (heading && ctx.hasHeadingFont()) {
			return { ctx.headingFont(), ctx.headingFontSize(), ctx.headingFontTextureId() };
		}
		return { ctx.font(), ctx.theme().fontSize, kAtlasTextureId };
	}
}

Label::Label(std::string text) : m_text(std::move(text)) {}

Vec2 Label::preferredSize(const GuiContext& ctx) const {
	LabelFont lf = resolveLabelFont(ctx, m_heading);
	TextFlags flags = m_tabular ? TextFlags::Tabular : TextFlags::None;
	if (!m_wordWrap) {
		return lf.font.measureText(m_text, lf.pixelSize, flags);
	}

	// m_bounds.w reflects the row width from the PREVIOUS layout pass (0 the very first
	// time this panel ever laid out). GuiContext::endFrame() runs layout twice per frame
	// for exactly this reason: pass 1 gives every widget a first-cut bounds; pass 2 then
	// sees the correct width here and reports the true wrapped height. See docs/gui/05,
	// "Label".
	float lh = lf.font.lineHeight(lf.pixelSize);
	if (m_bounds.w <= 0.0f) {
		return { 0.0f, lh };
	}
	auto lines = wrapText(lf.font, m_text, lf.pixelSize, m_bounds.w, flags);
	return { 0.0f, static_cast<float>(lines.size()) * lh };
}

void Label::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	Color color = m_colorOverride ? *m_colorOverride : (effectivelyEnabled() ? th.text : th.textDisabled);
	TextFlags flags = m_tabular ? TextFlags::Tabular : TextFlags::None;
	LabelFont lf = resolveLabelFont(ctx, m_heading);

	if (m_wordWrap) {
		auto lines = wrapText(lf.font, m_text, lf.pixelSize, m_bounds.w, flags);
		float lh = lf.font.lineHeight(lf.pixelSize);
		float y = m_bounds.y;
		for (auto& line : lines) {
			Rect lineRect{ m_bounds.x, y, m_bounds.w, lh };
			dl.addTextClipped(lf.font, lf.pixelSize, lineRect, color, line, m_align, Align::Start, flags,
			                   lf.textureId);
			y += lh;
		}
		return;
	}

	dl.addTextClipped(lf.font, lf.pixelSize, m_bounds, color, m_text, m_align, Align::Center, flags, lf.textureId);
}

} // namespace lightGraphics::ui
