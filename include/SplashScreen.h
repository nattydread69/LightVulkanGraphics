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

#include <cstdint>
#include <memory>
#include <string>

namespace lightGraphics
{
	// A splash screen: a small borderless window showing one image while the application
	// does its slow start-up work (loading models, building pipelines, ...).
	//
	// It is deliberately independent of VkApp. It owns its own minimal Vulkan instance,
	// device and swapchain and blits the decoded image straight to the swapchain, so it can
	// be put on screen BEFORE VkApp::init() runs and stays up through everything that
	// follows until the application is ready to show its real window:
	//
	//     auto splash = lightGraphics::SplashScreen::showFromFile("assets/splash.png");
	//     VkApp app;
	//     app.init(1280, 720, "My app");
	//     ... slow scene load, calling splash->pump() now and then ...
	//     app.finalizeScene();
	//     splash->close();          // honours Options::minDisplaySeconds
	//     app.run();
	//
	// A splash is a nicety, never a requirement: every failure (no display, no Vulkan, a
	// missing or undecodable image) is reported by returning nullptr, never by throwing,
	// so an application can simply carry on without one.
	//
	// Threading: main thread only (it uses GLFW). It does not call glfwTerminate(); GLFW's
	// lifetime stays with the application / VkApp.
	class SplashScreen
	{
	public:
		struct Options
		{
			// Window title (visible in some task bars even for a borderless window).
			std::string title = "Loading";
			// close() blocks (pumping events) until the splash has been up this long, so a
			// fast start-up doesn't flash it for a few milliseconds. 0 = close immediately.
			double minDisplaySeconds = 0.0;
			// The image is scaled down, preserving aspect ratio, so the window is at most
			// this fraction of the primary monitor's size. Never scaled up.
			float maxMonitorFraction = 0.8f;
			// Colour (RGB, 0-255) that transparent parts of the image are composited over.
			std::uint8_t backgroundRgb[3] = { 0, 0, 0 };
			// Keep the splash above other windows.
			bool alwaysOnTop = true;
		};

		// Decode an image file (anything stb_image reads: PNG, JPEG, BMP, TGA, ...).
		static std::unique_ptr<SplashScreen> showFromFile(const std::string& path, const Options& options);
		static std::unique_ptr<SplashScreen> showFromFile(const std::string& path);

		// Tightly packed 8-bit RGBA, width * height * 4 bytes, rows top to bottom.
		static std::unique_ptr<SplashScreen> showFromRgba(const std::uint8_t* rgba, int width, int height,
		                                                  const Options& options);
		static std::unique_ptr<SplashScreen> showFromRgba(const std::uint8_t* rgba, int width, int height);

		~SplashScreen();
		SplashScreen(const SplashScreen&) = delete;
		SplashScreen& operator=(const SplashScreen&) = delete;

		// Process window events and re-present the image. Call it between slow steps of
		// start-up: a window nobody services for several seconds is flagged "not
		// responding" by some desktops. Cheap; safe to call as often as you like.
		void pump();

		// Seconds since the splash appeared.
		double elapsedSeconds() const;

		// True until close() (or a failure while presenting) has torn the window down.
		bool isOpen() const;

		// Tear the window down, first waiting out Options::minDisplaySeconds. Idempotent;
		// the destructor calls it without the wait.
		void close();

	private:
		struct Impl;
		explicit SplashScreen(std::unique_ptr<Impl> impl);
		static std::unique_ptr<SplashScreen> create(const std::uint8_t* rgba, int width, int height,
		                                            const Options& options);

		std::unique_ptr<Impl> impl_;
	};
}
