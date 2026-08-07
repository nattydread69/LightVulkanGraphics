#include <lightVulkanGraphics/ui/Types.h>
#include <cmath>
#include <algorithm>

namespace lightGraphics::ui {

bool Rect::contains(Vec2 p) const {
	return p.x >= x && p.x <= right() && p.y >= y && p.y <= bottom();
}

Rect Rect::intersect(const Rect& other) const {
	float left = std::max(x, other.x);
	float top = std::max(y, other.y);
	float right = std::min(this->right(), other.right());
	float bottom = std::min(this->bottom(), other.bottom());

	float w = std::max(0.0f, right - left);
	float h = std::max(0.0f, bottom - top);
	return { left, top, w, h };
}

Rect Rect::inset(float amount) const {
	return { x + amount, y + amount, std::max(0.0f, w - 2 * amount), std::max(0.0f, h - 2 * amount) };
}

Rect Rect::insetXY(float dx, float dy) const {
	return { x + dx, y + dy, std::max(0.0f, w - 2 * dx), std::max(0.0f, h - 2 * dy) };
}

Rect Rect::fromMinMax(Vec2 lo, Vec2 hi) {
	return { lo.x, lo.y, std::max(0.0f, hi.x - lo.x), std::max(0.0f, hi.y - lo.y) };
}

Color Color::withAlpha(float multiplier) const {
	return { r, g, b, static_cast<std::uint8_t>(a * multiplier) };
}

Color Color::lerp(Color a, Color b, float t) {
	t = std::max(0.0f, std::min(1.0f, t));
	return {
		static_cast<std::uint8_t>(a.r + (b.r - a.r) * t),
		static_cast<std::uint8_t>(a.g + (b.g - a.g) * t),
		static_cast<std::uint8_t>(a.b + (b.b - a.b) * t),
		static_cast<std::uint8_t>(a.a + (b.a - a.a) * t)
	};
}

} // namespace lightGraphics::ui
