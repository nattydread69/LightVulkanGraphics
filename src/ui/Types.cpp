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

Color Color::fromHSV(float h, float s, float v, std::uint8_t a) {
	h = std::fmod(h, 360.0f);
	if (h < 0.0f) {
		h += 360.0f;
	}
	s = std::clamp(s, 0.0f, 1.0f);
	v = std::clamp(v, 0.0f, 1.0f);

	float c = v * s;
	float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
	float m = v - c;
	float r = 0.0f, g = 0.0f, b = 0.0f;
	if      (h < 60.0f)  { r = c; g = x; b = 0.0f; }
	else if (h < 120.0f) { r = x; g = c; b = 0.0f; }
	else if (h < 180.0f) { r = 0.0f; g = c; b = x; }
	else if (h < 240.0f) { r = 0.0f; g = x; b = c; }
	else if (h < 300.0f) { r = x; g = 0.0f; b = c; }
	else                 { r = c; g = 0.0f; b = x; }

	Color result = Color::fromFloats(r + m, g + m, b + m);
	result.a = a;
	return result;
}

void Color::toHSV(float& h, float& s, float& v) const {
	float rf = static_cast<float>(r) / 255.0f;
	float gf = static_cast<float>(g) / 255.0f;
	float bf = static_cast<float>(b) / 255.0f;
	float maxc = std::max({ rf, gf, bf });
	float minc = std::min({ rf, gf, bf });
	float delta = maxc - minc;

	v = maxc;
	s = maxc <= 0.0f ? 0.0f : delta / maxc;

	if (delta <= 1.0e-6f) {
		h = 0.0f;   // grey: hue undefined, canonical 0 -- see the header comment
		return;
	}
	if (maxc == rf) {
		h = 60.0f * std::fmod((gf - bf) / delta, 6.0f);
	} else if (maxc == gf) {
		h = 60.0f * (((bf - rf) / delta) + 2.0f);
	} else {
		h = 60.0f * (((rf - gf) / delta) + 4.0f);
	}
	if (h < 0.0f) {
		h += 360.0f;
	}
}

} // namespace lightGraphics::ui
