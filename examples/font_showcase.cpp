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

// Font showcase. One window that exercises every font feature:
//   * pair kerning ("AVATAR", "Wa To") and sub-pixel glyph placement
//   * dynamic glyphs: punctuation and symbols outside the pre-baked ranges
//   * a fallback face (DejaVu Sans) supplying symbols/Cyrillic that Inter lacks
//   * FontStyle::Heading (baked as a signed distance field), Bold and Monospace labels
//   * ScreenText drawn from the real font atlas, next to the legacy stroke font
//
// Usage: font_showcase [screenshot.png]
//   With an argument it saves a screenshot after a few frames and exits (handy for CI and
//   for eyeballing a change); without one it runs until the window is closed.

#include "VkApp.h"
#include <lightVulkanGraphics/ui/Ui.h>

#include <GLFW/glfw3.h>

#include <iostream>
#include <string>

namespace lvgui = lightGraphics::ui;

int main(int argc, char** argv)
{
	try
	{
		const std::string screenshotPath = (argc > 1) ? argv[1] : "";

		lightGraphics::VkApp app;

		lvgui::GuiCreateInfo guiInfo;
		guiInfo.fontSize = 15.0f;
		guiInfo.headingFontSize = 30.0f;
		guiInfo.headingFontSdf = true;
		// Optional companions, found wherever the OS keeps fonts. Missing ones are skipped.
		if (std::string bold = lvgui::Font::findSystemFont("DejaVuSans-Bold.ttf"); !bold.empty())
		{
			guiInfo.boldFontPath = bold;
		}
		if (std::string symbols = lvgui::Font::findSystemFont("DejaVuSans.ttf"); !symbols.empty())
		{
			guiInfo.fallbackFontPaths.push_back(symbols);
		}
		app.setGuiCreateInfo(guiInfo);

		app.init(1100, 760, "Font showcase");
		app.setCameraLookAt({ 4.0f, 2.5f, 6.0f }, { 0.0f, 0.0f, 0.0f });
		app.finalizeScene();

		if (!app.hasGui())
		{
			std::cout << "[font-showcase] no GUI available\n";
			return 1;
		}

		auto& gui = app.gui();
		auto* panel = gui.createPanel("Fonts", { 24.0f, 24.0f, 620.0f, 600.0f });

		auto addLabel = [&](const std::string& text, lvgui::FontStyle style = lvgui::FontStyle::Regular) {
			auto* label = panel->add<lvgui::Label>(text);
			label->setFontStyle(style);
			return label;
		};

		addLabel("Font showcase (SDF heading)", lvgui::FontStyle::Heading);
		addLabel("Regular: The quick brown fox jumps over the lazy dog 0123456789");
		addLabel("Bold: The quick brown fox jumps over the lazy dog", lvgui::FontStyle::Bold);
		addLabel("Mono: x = 3.14159;  for (int i = 0; i < n; ++i)", lvgui::FontStyle::Monospace);
		addLabel("Kerning: AVATAR  To  Wa  Ty  LT  P.  V.  \"Yes\"");
		addLabel("Punctuation (dynamic): \xE2\x80\x9C" "quoted\xE2\x80\x9D \xE2\x80\x94 dash \xE2\x80\xA6 \xE2\x80\xA2 bullet \xE2\x82\xAC 5 \xE2\x88\x92 3");
		addLabel("Greek: \xCE\xB1 \xCE\xB2 \xCE\xB3 \xCE\xB4 \xCE\x94t \xCF\x89 \xCE\xA9 \xCE\xA3 \xCF\x80");
		addLabel("Fallback face: \xE2\x9C\x93 \xE2\x89\x88 \xE2\x88\x91 \xE2\x88\x9A \xE2\x88\x9E \xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xE2\x99\xA0");

		// Screen text: atlas-rendered (default) vs. the legacy stroke font.
		lightGraphics::ScreenTextDescription atlasText;
		atlasText.text = "ScreenText from the font atlas: AVATAR \xE2\x80\x94 kerned, anti-aliased \xCE\xA9";
		atlasText.positionPixels = { 24.0f, 650.0f };
		atlasText.backgroundEnabled = true;
		app.createScreenText(atlasText);

		lightGraphics::ScreenTextDescription monoText;
		monoText.text = "mono screen text:\nfps   60.0\nframe 16.6 ms";
		monoText.monospace = true;
		monoText.positionPixels = { 700.0f, 60.0f };
		monoText.backgroundEnabled = true;
		app.createScreenText(monoText);

		lightGraphics::ScreenTextDescription big;
		big.text = "scale 2.0";
		big.scale = 2.0f;
		big.positionPixels = { 700.0f, 200.0f };
		app.createScreenText(big);

		lightGraphics::ScreenTextDescription legacy;
		legacy.text = "legacy stroke font (useFontAtlas=false)";
		legacy.useFontAtlas = false;
		legacy.positionPixels = { 700.0f, 300.0f };
		app.createScreenText(legacy);

		int frames = 0;
		app.setUpdateCallback([&](float) {
			if (screenshotPath.empty() || ++frames != 10)
			{
				return;
			}
			app.requestFrameCapture([&](const lightGraphics::CapturedFrame& frame) {
				std::string error;
				if (!frame.ok || !lightGraphics::VkApp::writePng(screenshotPath, frame, &error))
				{
					std::cerr << "[font-showcase] capture failed: " << (frame.ok ? error : frame.error) << "\n";
				}
				else
				{
					std::cout << "[font-showcase] wrote " << screenshotPath << " (" << frame.width << "x"
					          << frame.height << ")\n";
				}
				glfwSetWindowShouldClose(app.getWindowPointer(), GLFW_TRUE);
			});
		});

		app.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "[font-showcase] " << e.what() << "\n";
		return 1;
	}
	return 0;
}
