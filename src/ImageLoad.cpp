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

#include "ImageLoad.h"

#include <climits>

// The library's only copy of stb_image, compiled STATIC: every stbi_* function is
// private to this file, so LVG exports none of them and an application linking LVG
// can use (or vendor) its own stb_image without symbol clashes. Everything else in LVG
// decodes through loadImageRgba()/decodeImageRgba(). STATIC leaves the parts of stb_image
// this file does not call unused, which GCC/Clang/MSVC would warn about once each.
#if defined(__GNUC__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#	pragma warning(push)
#	pragma warning(disable : 4505) // unreferenced function with internal linkage removed
#	pragma warning(disable : 4244) // stb_image's own narrowing conversions
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#if defined(__GNUC__)
#	pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#	pragma warning(pop)
#endif

namespace lightGraphics
{
	namespace
	{
		bool finishDecode(stbi_uc* pixels, int width, int height, ImageRgba& out, std::string* error)
		{
			if (!pixels)
			{
				if (error)
				{
					const char* reason = stbi_failure_reason();
					*error = reason ? reason : "could not decode image";
				}
				return false;
			}

			out.width = width;
			out.height = height;
			out.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
			stbi_image_free(pixels);
			return true;
		}
	}

	bool loadImageRgba(const std::string& path, ImageRgba& out, std::string* error)
	{
		out = {};
		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
		return finishDecode(pixels, width, height, out, error);
	}

	bool decodeImageRgba(const void* data, std::size_t size, ImageRgba& out, std::string* error)
	{
		out = {};
		if (data == nullptr || size == 0 || size > static_cast<std::size_t>(INT_MAX))
		{
			if (error)
			{
				*error = data == nullptr || size == 0 ? "no image data" : "image data too large";
			}
			return false;
		}
		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(
			static_cast<const stbi_uc*>(data), static_cast<int>(size), &width, &height, &channels, STBI_rgb_alpha);
		return finishDecode(pixels, width, height, out, error);
	}
}
