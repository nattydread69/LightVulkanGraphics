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

#include <lightVulkanGraphics/ui/widgets/AboutBox.h>
#include <lightVulkanGraphics/ui/widgets/Button.h>
#include <lightVulkanGraphics/ui/widgets/Image.h>
#include <lightVulkanGraphics/ui/widgets/Label.h>
#include <lightVulkanGraphics/ui/widgets/Row.h>
#include <lightVulkanGraphics/ui/widgets/Spacer.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "TextWrap.h"

#include <algorithm>

namespace lightGraphics::ui {

namespace {
	// Never open bigger than the window it is in.
	constexpr float kScreenMargin = 20.0f;
	// Slack so a rounding difference between this estimate and Label's own wrapping
	// never clips the last line.
	constexpr float kHeightSlack = 6.0f;
}

AboutBox::AboutBox(GuiContext& context, std::string title)
	: m_context(context)
{
	Rect const bounds{ 0.0f, 0.0f, m_width, 200.0f };
	m_panel = m_context.createPanel(std::move(title), bounds, PanelFlags::Modal | PanelFlags::Closable | PanelFlags::Movable);
	m_panel->setVisible(false);
	// OK, the title-bar X and Escape all reach here through Panel::requestClose().
	m_panel->setOnClose([this] { if (m_onClose) { m_onClose(); } });
	rebuild();
}

AboutBox::~AboutBox() = default;

void AboutBox::setImage(TextureId texture, Vec2 size) {
	m_texture = texture;
	m_imageSize = size;
	m_hasImage = size.x > 0.0f && size.y > 0.0f;
	rebuild();
}

void AboutBox::clearImage() {
	m_hasImage = false;
	rebuild();
}

void AboutBox::setName(std::string name) { m_name = std::move(name); rebuild(); }
void AboutBox::setVersion(std::string version) { m_version = std::move(version); rebuild(); }
void AboutBox::setDescription(std::string text) { m_description = std::move(text); rebuild(); }
void AboutBox::setFooter(std::string text) { m_footer = std::move(text); rebuild(); }
void AboutBox::setTitle(std::string title) { m_panel->setTitle(std::move(title)); }
void AboutBox::setWidth(float width) { m_width = std::max(200.0f, width); rebuild(); }

bool AboutBox::isOpen() const { return m_panel && m_panel->visible(); }

void AboutBox::close() {
	m_panel->setVisible(false);
}

void AboutBox::open() {
	Vec2 const display = m_context.input().displaySize;
	Rect bounds = m_panel->bounds();
	if (display.x > 0.0f && display.y > 0.0f) {
		bounds.x = std::max(0.0f, (display.x - bounds.w) * 0.5f);
		bounds.y = std::max(0.0f, (display.y - bounds.h) * 0.5f);
	}
	m_panel->setBounds(bounds);
	m_panel->setVisible(true);
	m_panel->bringToFront();
}

void AboutBox::rebuild() {
	const Theme& th = m_context.theme();
	float const contentW = std::max(1.0f, m_width - 2.0f * th.windowPadding);

	m_panel->clear();
	m_okButton = nullptr;

	// Height is accumulated alongside the widgets: this panel is not Scrollable or
	// Resizable, so nothing else would size it to its content.
	float height = th.titleBarHeight + th.windowPadding;
	auto addRow = [&](float rowHeight) { height += rowHeight + th.itemSpacing; };

	if (m_hasImage) {
		// Reserve the vertical space in the flow, then place the image itself centred
		// with a bounds override (an Image draws left-aligned in its row otherwise).
		m_panel->add<Spacer>(m_imageSize.y);
		auto* image = m_panel->add<Image>("", m_texture, m_imageSize);
		image->setBoundsOverride(Rect{ (m_width - m_imageSize.x) * 0.5f, height, m_imageSize.x, m_imageSize.y });
		addRow(m_imageSize.y);
	}

	auto addText = [&](const std::string& text, FontStyle style, bool wrap, bool dim) {
		if (text.empty()) {
			return;
		}
		auto* label = m_panel->add<Label>(text);
		label->setAlign(Align::Center);
		label->setFontStyle(style);
		label->setWordWrap(wrap);
		if (dim) {
			label->setColor(th.textDisabled);
		}
		StyledFont const face = m_context.styledFont(style);
		float const lineHeight = face.font.lineHeight(face.pixelSize);
		std::size_t lines = 1;
		if (wrap) {
			lines = std::max<std::size_t>(1, wrapText(face.font, text, face.pixelSize, contentW).size());
		}
		addRow(static_cast<float>(lines) * lineHeight);
	};

	addText(m_name, FontStyle::Heading, false, false);
	addText(m_version, FontStyle::Regular, false, true);
	if (!m_description.empty()) {
		m_panel->add<Spacer>(th.itemSpacing);
		addRow(th.itemSpacing);
		// One Label per `\n`-separated line, so blank lines make paragraph gaps and
		// each paragraph wraps on its own.
		std::size_t start = 0;
		while (start <= m_description.size()) {
			std::size_t end = m_description.find('\n', start);
			if (end == std::string::npos) {
				end = m_description.size();
			}
			std::string const line = m_description.substr(start, end - start);
			if (line.empty()) {
				m_panel->add<Spacer>(th.itemSpacing);
				addRow(th.itemSpacing);
			} else {
				addText(line, FontStyle::Regular, true, false);
			}
			start = end + 1;
		}
	}
	if (!m_footer.empty()) {
		m_panel->add<Spacer>(th.itemSpacing);
		addRow(th.itemSpacing);
		addText(m_footer, FontStyle::Regular, true, true);
	}

	m_panel->add<Spacer>(th.itemSpacing);
	addRow(th.itemSpacing);
	auto* buttonRow = m_panel->add<Row>();
	buttonRow->setWeights({ 2.0f, 1.0f, 2.0f });
	buttonRow->add<Spacer>(0.0f);
	m_okButton = buttonRow->add<Button>("OK");
	m_okButton->setOnClick([this] { m_panel->requestClose(); });
	buttonRow->add<Spacer>(0.0f);
	addRow(th.rowHeight);

	height += th.windowPadding - th.itemSpacing + kHeightSlack;

	Rect bounds = m_panel->bounds();
	Vec2 const display = m_context.input().displaySize;
	bounds.w = m_width;
	bounds.h = height;
	if (display.x > 0.0f && display.y > 0.0f) {
		bounds.w = std::min(bounds.w, std::max(200.0f, display.x - 2.0f * kScreenMargin));
		bounds.h = std::min(bounds.h, std::max(120.0f, display.y - 2.0f * kScreenMargin));
	}
	if (m_panel->visible()) {
		// Stay centred on where it already is when content changes under an open box.
		Rect const old = m_panel->bounds();
		bounds.x = old.x + (old.w - bounds.w) * 0.5f;
		bounds.y = old.y + (old.h - bounds.h) * 0.5f;
	}
	m_panel->setBounds(bounds);
}

} // namespace lightGraphics::ui
