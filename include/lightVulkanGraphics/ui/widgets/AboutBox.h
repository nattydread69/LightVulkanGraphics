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

// An "About" box: a small Modal panel showing an optional graphic (logo / icon) above
// the application name, version, a block of descriptive text and a footer line
// (copyright, licence), with an OK button. The About-dialog pattern from docs/gui/05-
// widgets.md, "Panel", "Modal panels", packaged so an application doesn't rebuild it.
//
// Created once and toggled with open()/close(), like the file dialogs. All text is
// UTF-8; `\n` in the description starts a new paragraph line. Every setter may be
// called before or while the box is open; the layout is rebuilt immediately.
//
//     lightGraphics::ui::AboutBox about(app.gui());
//     about.setName("My App");
//     about.setVersion("1.2.0");
//     about.setDescription("Does a thing.\nBuilt on lightVulkanGraphics.");
//     about.setFooter("Copyright (C) 2026 Me. Licensed under the LGPL-3.0.");
//     about.setImage(app.registerUiTexture(rgba, w, h), { 128.0f, 128.0f });
//     menu item "About" -> about.open();
//
// The texture comes from VkApp::registerUiTexture(); the box does not own it.

#include "../Types.h"

#include <functional>
#include <string>

namespace lightGraphics::ui {

class GuiContext;
class Panel;
class Button;

class AboutBox {
public:
	explicit AboutBox(GuiContext& context, std::string title = "About");
	~AboutBox();

	AboutBox(const AboutBox&) = delete;
	AboutBox& operator=(const AboutBox&) = delete;

	// The graphic, drawn centred at `size` logical pixels (not stretched).
	void setImage(TextureId texture, Vec2 size);
	void clearImage();
	// Application name, drawn large (the heading face when the GuiContext has one).
	void setName(std::string name);
	// Shown under the name, dimmed, e.g. "Version 1.2.0".
	void setVersion(std::string version);
	// Word-wrapped body text.
	void setDescription(std::string text);
	// Word-wrapped small print at the bottom: copyright, licence, credits.
	void setFooter(std::string text);
	// Panel title bar text.
	void setTitle(std::string title);
	// Width of the box in logical pixels (default 420). The height follows the content.
	void setWidth(float width);

	// Fires when the box is dismissed by OK, the title-bar X or Escape.
	void setOnClose(std::function<void()> callback) { m_onClose = std::move(callback); }

	// Centres the box over the window, shows it and brings it to the front.
	void open();
	void close();
	bool isOpen() const;

	// The underlying panel, for anything not covered here.
	Panel* panel() const { return m_panel; }
	Button* okButton() const { return m_okButton; }

private:
	void rebuild();

	GuiContext& m_context;
	Panel* m_panel = nullptr;
	Button* m_okButton = nullptr;
	std::function<void()> m_onClose;

	TextureId m_texture = kAtlasTextureId;
	bool m_hasImage = false;
	Vec2 m_imageSize{ 0.0f, 0.0f };
	std::string m_name;
	std::string m_version;
	std::string m_description;
	std::string m_footer;
	float m_width = 420.0f;
};

} // namespace lightGraphics::ui
