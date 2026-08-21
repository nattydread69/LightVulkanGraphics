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
#include <glm/glm.hpp>

namespace lightGraphics::ui {

using WidgetId = std::uint64_t;
inline constexpr WidgetId kInvalidWidgetId = 0;

// Identifies a texture registered with the UI backend (VkApp::registerUiTexture(),
// docs/gui/05-widgets.md, "Image"). 0 is reserved for the UI's own font atlas -- every
// DrawCmd already carries a textureId, and 0 is what it has always meant, from before any
// consumer could register a texture at all (DrawList.cpp hardcoded it on every command).
// A real texture is never assigned 0 (UiRenderer's id counter starts at 1), so `textureId
// == kAtlasTextureId` unambiguously means "sample the atlas / white pixel", not "no
// texture set yet".
using TextureId = std::uint32_t;
inline constexpr TextureId kAtlasTextureId = 0;

struct Vec2 {
	float x = 0.0f, y = 0.0f;

	constexpr Vec2() = default;
	constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

	Vec2 operator+(Vec2 other) const { return { x + other.x, y + other.y }; }
	Vec2 operator-(Vec2 other) const { return { x - other.x, y - other.y }; }
	Vec2 operator*(float scalar) const { return { x * scalar, y * scalar }; }
	Vec2& operator+=(Vec2 other) { x += other.x; y += other.y; return *this; }
};

struct Rect {
	float x = 0, y = 0, w = 0, h = 0;

	constexpr float left() const { return x; }
	constexpr float top() const { return y; }
	constexpr float right() const { return x + w; }
	constexpr float bottom() const { return y + h; }
	constexpr Vec2 min() const { return { x, y }; }
	constexpr Vec2 centre() const { return { x + w * 0.5f, y + h * 0.5f }; }

	bool contains(Vec2 p) const;
	Rect intersect(const Rect& other) const;
	Rect inset(float amount) const;
	Rect insetXY(float dx, float dy) const;
	constexpr bool empty() const { return w <= 0.0f || h <= 0.0f; }

	static Rect fromMinMax(Vec2 lo, Vec2 hi);
};

struct Color {
	std::uint8_t r = 0, g = 0, b = 0, a = 255;

	constexpr Color() = default;
	constexpr Color(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_,
					std::uint8_t a_ = 255)
		: r(r_), g(g_), b(b_), a(a_) {}

	static constexpr Color fromFloats(float r, float g, float b, float a = 1.0f);
	static constexpr Color fromHex(std::uint32_t rgb, float alpha = 1.0f);
	constexpr std::uint32_t packed() const;
	Color withAlpha(float multiplier) const;
	static Color lerp(Color a, Color b, float t);

	// `h` in degrees, wrapped to [0,360) internally (so -30 and 750 both mean the same
	// as 330); `s`/`v` clamped to [0,1]. `a` is passed straight through, byte-for-byte --
	// alpha has no HSV analogue to round-trip through.
	static Color fromHSV(float h, float s, float v, std::uint8_t a = 255);
	// Inverse of fromHSV, used by ColorEdit (docs/gui/05-widgets.md, "ColorEdit") to seed
	// its hue/saturation/value drag state from an externally-set RGB value. Degenerate
	// case: when r==g==b (grey, s==0), hue is mathematically undefined; this returns 0
	// rather than leaving it uninitialised, matching the canonical convention. Callers
	// that care about not clobbering a previously-chosen hue on desaturation (dragging a
	// colour to grey and back should restore the hue it started at, not silently reset to
	// red) must guard the call themselves -- see ColorEdit's pullBoundValue().
	void toHSV(float& h, float& s, float& v) const;
};

inline constexpr Color Color::fromFloats(float r, float g, float b, float a) {
	auto clamp = [](float v) {
		return static_cast<std::uint8_t>(
			v < 0.0f ? 0 : (v > 1.0f ? 255 : static_cast<int>(v * 255.0f))
		);
	};
	return { clamp(r), clamp(g), clamp(b), clamp(a) };
}

inline constexpr Color Color::fromHex(std::uint32_t rgb, float alpha) {
	return {
		static_cast<std::uint8_t>(rgb & 0xFF),
		static_cast<std::uint8_t>((rgb >> 8) & 0xFF),
		static_cast<std::uint8_t>((rgb >> 16) & 0xFF),
		static_cast<std::uint8_t>(
			alpha < 0.0f ? 0 : (alpha > 1.0f ? 255 : static_cast<int>(alpha * 255.0f))
		)
	};
}

inline constexpr std::uint32_t Color::packed() const {
	return (static_cast<std::uint32_t>(a) << 24) |
	       (static_cast<std::uint32_t>(b) << 16) |
	       (static_cast<std::uint32_t>(g) << 8) |
	       static_cast<std::uint32_t>(r);
}

enum class Align { Start, Center, End };

enum class PanelFlags : std::uint32_t {
	None         = 0,
	Movable      = 1 << 0,
	Resizable    = 1 << 1,
	Collapsible  = 1 << 2,
	Closable     = 1 << 3,
	Scrollable   = 1 << 4,
	NoTitleBar   = 1 << 5,
	NoBackground = 1 << 6,
	// docs/gui/05-widgets.md, "Panel", "Modal panels": while any Modal panel is visible,
	// GuiContext restricts hover/hit-testing and Tab-focus collection to the frontmost
	// one, and wantsMouse()/wantsKeyboard()/wantsScroll() all report true unconditionally
	// -- swallowing the camera hand-off outright, not just over the panel's own rect.
	// Deliberately not in Default: opting a panel into blocking every other panel and the
	// 3D scene behind it is exactly the kind of thing that should never happen by
	// accident.
	Modal        = 1 << 7,
	// operator| below isn't declared yet at this point in the enum body, so Default is
	// built from the raw bit values rather than the sibling enumerators directly.
	Default      = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 4),  // Movable | Resizable | Collapsible | Scrollable
};

constexpr PanelFlags operator|(PanelFlags a, PanelFlags b) {
	return static_cast<PanelFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr PanelFlags operator&(PanelFlags a, PanelFlags b) {
	return static_cast<PanelFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
constexpr bool hasFlag(PanelFlags flags, PanelFlags bit) {
	return static_cast<std::uint32_t>(flags & bit) != 0;
}

enum class TextFlags : std::uint32_t {
	None    = 0,
	// Pads every ASCII digit's ('0'-'9') pen advance out to the widest digit's own
	// advance in the font, so a live-updating numeric readout's digit columns never
	// shift the characters after them horizontally as values change -- see
	// docs/gui/03-text-and-fonts.md, "Tabular figures". A no-op if the font has no
	// digits baked (Font::advanceFor() falls back to the glyph's own advance).
	Tabular = 1 << 0,
};

constexpr TextFlags operator|(TextFlags a, TextFlags b) {
	return static_cast<TextFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr TextFlags operator&(TextFlags a, TextFlags b) {
	return static_cast<TextFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
constexpr bool hasFlag(TextFlags flags, TextFlags bit) {
	return static_cast<std::uint32_t>(flags & bit) != 0;
}

} // namespace lightGraphics::ui
