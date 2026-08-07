#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace lightGraphics::ui {

using WidgetId = std::uint64_t;
inline constexpr WidgetId kInvalidWidgetId = 0;

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

} // namespace lightGraphics::ui
