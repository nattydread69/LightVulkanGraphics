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

// Splash screen demo: shows lightGraphics::SplashScreen up before VkApp::init(), keeps it
// serviced through a simulated slow load, then hands over to the real window.
//
//   splash_demo [image.png]     (without an argument a generated gradient is used)

#include "lightVulkanGraphics.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
	lightGraphics::SplashScreen::Options options;
	options.title = "Splash demo";
	options.minDisplaySeconds = 2.0;

	std::unique_ptr<lightGraphics::SplashScreen> splash;
	if (argc > 1)
	{
		splash = lightGraphics::SplashScreen::showFromFile(argv[1], options);
	}
	else
	{
		const int w = 640, h = 360;
		std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * w + x) * 4];
				p[0] = static_cast<std::uint8_t>(255 * x / w);
				p[1] = static_cast<std::uint8_t>(255 * y / h);
				p[2] = 160;
				p[3] = 255;
			}
		}
		splash = lightGraphics::SplashScreen::showFromRgba(rgba.data(), w, h, options);
	}
	if (!splash)
	{
		std::cout << "[splash-demo] no splash available; continuing without one" << std::endl;
	}

	try
	{
		lightGraphics::VkApp app;
		app.init(1280, 720, "Splash demo");

		// Stand-in for a slow scene load.
		for (int i = 0; i < 10; ++i)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (splash)
			{
				splash->pump();
			}
		}

		app.finalizeScene();
		if (splash)
		{
			splash->close(); // waits out minDisplaySeconds
		}
		app.run();
		app.cleanup();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}
	return 0;
}
