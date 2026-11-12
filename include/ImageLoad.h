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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lightGraphics
{
	// A decoded image: tightly packed 8-bit RGBA, width * height * 4 bytes, row 0 at the
	// top. The same layout SplashScreen::showFromRgba() and CapturedFrame use.
	struct ImageRgba
	{
		int width = 0;
		int height = 0;
		std::vector<std::uint8_t> rgba;
	};

	// Decodes an image file into RGBA. Reads PNG, JPEG, BMP, TGA, GIF (first frame), PSD,
	// HDR (tone-mapped to 8 bits), PIC and PNM; images with fewer channels are expanded
	// (grey -> RGB, opaque alpha). This is the decoder LVG uses for its own textures and
	// splash screens, so an app needs no image library of its own. Returns false, leaves
	// `out` empty and, if `error` is given, sets a short reason on failure.
	bool loadImageRgba(const std::string& path, ImageRgba& out, std::string* error = nullptr);

	// The same, from an encoded image already in memory (e.g. a texture embedded in a
	// model file).
	bool decodeImageRgba(const void* data, std::size_t size, ImageRgba& out, std::string* error = nullptr);
}
