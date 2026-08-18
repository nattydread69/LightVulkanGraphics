#include <lightVulkanGraphics/ui/DrawList.h>
#include <lightVulkanGraphics/ui/Font.h>
#include "Utf8.h"
#include <cmath>
#include <algorithm>
#include <string>

namespace lightGraphics::ui {

namespace {
	Vec2 snapToPixel(Vec2 v) {
		return { std::round(v.x), std::round(v.y) };
	}

	constexpr int kCircleTableSize = 65;

	class CircleTable {
	public:
		CircleTable() {
			for (int i = 0; i < kCircleTableSize; ++i) {
				float angle = (i / 64.0f) * 6.28318530718f;
				data[i] = { std::cos(angle), std::sin(angle) };
			}
		}
		const Vec2& operator[](int i) const { return data[i]; }
	private:
		Vec2 data[kCircleTableSize];
	};

	static const CircleTable kCircleTable;

	struct RoundedCorner { Vec2 centre; int tableStart; };

	// Traces the boundary of a rounded rect as a single perimeter loop: four
	// quarter-circle corner arcs, each anchored at its own corner's inset centre
	// (not the rect's centre -- anchoring all four at the rect's centre is what
	// draws a circle instead of a rounded rect). Consecutive arcs' endpoints
	// already lie on the flat edge between them, so the loop needs no separate
	// straight-edge points: a closed polyline or a fan drawn over these points
	// alone reproduces the flat runs for free.
	//
	// `rounding` is clamped to half the shorter side so two opposing corners can
	// never cross past each other and fold the geometry inside out.
	float buildRoundedRectPerimeter(const Rect& rect, float rounding, std::vector<Vec2>& out) {
		float r = std::min(rounding, std::min(rect.w, rect.h) * 0.5f);

		const RoundedCorner corners[4] = {
			{ { rect.right() - r, rect.bottom() - r }, 0  },  // bottom-right
			{ { rect.left()  + r, rect.bottom() - r }, 16 },  // bottom-left
			{ { rect.left()  + r, rect.top()    + r }, 32 },  // top-left
			{ { rect.right() - r, rect.top()    + r }, 48 },  // top-right
		};

		out.reserve(out.size() + 4 * 17);
		for (const RoundedCorner& corner : corners) {
			for (int i = 0; i <= 16; ++i) {
				const Vec2& circlePoint = kCircleTable[corner.tableStart + i];
				out.push_back({ corner.centre.x + circlePoint.x * r,
				                 corner.centre.y + circlePoint.y * r });
			}
		}
		return r;
	}
}

DrawList::DrawList() {
	m_vertices.reserve(4096);
	m_indices.reserve(6144);
	m_commands.reserve(256);
	m_clipStack.reserve(16);
}

DrawList::~DrawList() = default;

void DrawList::clear() {
	m_vertices.clear();
	m_indices.clear();
	m_commands.clear();
	m_clipStack.clear();

	Rect fullscreen = { 0, 0, 10000.0f, 10000.0f };
	m_clipStack.push_back(fullscreen);

	DrawCmd cmd{};
	cmd.indexOffset = 0;
	cmd.indexCount = 0;
	cmd.clipRect = fullscreen;
	cmd.textureId = 0;
	m_commands.push_back(cmd);
}

void DrawList::pushClipRect(const Rect& rect, bool intersectWithCurrent) {
	if (m_clipStack.empty()) {
		m_clipStack.push_back(rect);
	} else if (intersectWithCurrent) {
		m_clipStack.push_back(m_clipStack.back().intersect(rect));
	} else {
		m_clipStack.push_back(rect);
	}
	ensureCommand();
}

void DrawList::popClipRect() {
	if (m_clipStack.size() > 1) {
		m_clipStack.pop_back();
	}
	ensureCommand();
}

Rect DrawList::getCurrentClipRect() const {
	if (m_clipStack.empty()) {
		return { 0, 0, 10000.0f, 10000.0f };
	}
	return m_clipStack.back();
}

std::uint32_t DrawList::packColor(Color c) const {
	return c.packed();
}

void DrawList::updateCurrentCommandIndexCount() {
	if (!m_commands.empty()) {
		m_commands.back().indexCount = m_indices.size() - m_commands.back().indexOffset;
	}
}

void DrawList::ensureCommand() {
	if (m_commands.empty()) {
		DrawCmd cmd{};
		cmd.indexOffset = m_indices.size();
		cmd.indexCount = 0;
		cmd.clipRect = getCurrentClipRect();
		cmd.textureId = 0;
		m_commands.push_back(cmd);
		return;
	}

	DrawCmd& cmd = m_commands.back();
	Rect currentClip = getCurrentClipRect();

	bool needsNewCommand = false;
	if (!(cmd.clipRect.x == currentClip.x && cmd.clipRect.y == currentClip.y &&
		  cmd.clipRect.w == currentClip.w && cmd.clipRect.h == currentClip.h)) {
		needsNewCommand = true;
	}
	if (cmd.textureId != 0) {
		needsNewCommand = true;
	}

	if (needsNewCommand) {
		cmd.indexCount = m_indices.size() - cmd.indexOffset;
		if (cmd.indexCount > 0) {
			DrawCmd newCmd{};
			newCmd.indexOffset = m_indices.size();
			newCmd.indexCount = 0;
			newCmd.clipRect = currentClip;
			newCmd.textureId = 0;
			m_commands.push_back(newCmd);
		} else {
			cmd.clipRect = currentClip;
		}
	}
}

void DrawList::addRectFilled(const Rect& rect, Color color, float rounding) {
	ensureCommand();
	glm::vec2 wuv(m_whitePixelUV.x, m_whitePixelUV.y);

	if (rounding <= 0.0f) {
		std::uint32_t col = packColor(color);
		std::uint32_t idx = m_vertices.size();

		Vec2 tl = snapToPixel(rect.min());
		Vec2 br = snapToPixel({ rect.right(), rect.bottom() });

		m_vertices.push_back(UiVertex(glm::vec2(tl.x, tl.y), wuv, col));
		m_vertices.push_back(UiVertex(glm::vec2(br.x, tl.y), wuv, col));
		m_vertices.push_back(UiVertex(glm::vec2(br.x, br.y), wuv, col));
		m_vertices.push_back(UiVertex(glm::vec2(tl.x, br.y), wuv, col));

		m_indices.push_back(idx + 0);
		m_indices.push_back(idx + 1);
		m_indices.push_back(idx + 2);
		m_indices.push_back(idx + 0);
		m_indices.push_back(idx + 2);
		m_indices.push_back(idx + 3);

	} else {
		// A proper rounded rect, not a circle: a triangle fan over the perimeter's
		// own first vertex (docs/gui/02-rendering.md, "build a convex fan"), the same
		// technique addConvexPolyFilled uses. A rounded rect is convex, so fanning
		// from any one of its own boundary points -- rather than from a separate hub
		// planted at the rect's centre -- tiles it correctly with one fewer vertex
		// and no vertex left sitting at the centre.
		std::uint32_t col = packColor(color);

		std::vector<Vec2> perimeter;
		buildRoundedRectPerimeter(rect, rounding, perimeter);

		std::uint32_t baseIdx = m_vertices.size();
		for (const Vec2& p : perimeter) {
			Vec2 pos = snapToPixel(p);
			m_vertices.push_back(UiVertex(glm::vec2(pos.x, pos.y), wuv, col));
		}

		std::uint32_t perimeterCount = static_cast<std::uint32_t>(perimeter.size());
		for (std::uint32_t i = 1; i + 1 < perimeterCount; ++i) {
			m_indices.push_back(baseIdx);
			m_indices.push_back(baseIdx + i);
			m_indices.push_back(baseIdx + i + 1);
		}
	}

	updateCurrentCommandIndexCount();
}

void DrawList::addRect(const Rect& rect, Color color, float thickness, float rounding) {
	if (rounding <= 0.0f) {
		Vec2 pts[4] = {
			rect.min(),
			{ rect.right(), rect.top() },
			{ rect.right(), rect.bottom() },
			{ rect.left(), rect.bottom() }
		};
		addPolyline(pts, 4, color, thickness, true);
		return;
	}

	// Shares the exact perimeter addRectFilled uses so a rounded outline drawn over
	// a rounded fill (slider handles, the text box border) actually follows the same
	// curve instead of a square outline showing corners past the fill's rounding.
	std::vector<Vec2> perimeter;
	buildRoundedRectPerimeter(rect, rounding, perimeter);
	addPolyline(perimeter.data(), static_cast<int>(perimeter.size()), color, thickness, true);
}

void DrawList::addRectFilledMultiColor(const Rect& rect, Color tl, Color tr, Color br, Color bl) {
	ensureCommand();
	glm::vec2 wuv(m_whitePixelUV.x, m_whitePixelUV.y);

	std::uint32_t tl_col = packColor(tl);
	std::uint32_t tr_col = packColor(tr);
	std::uint32_t br_col = packColor(br);
	std::uint32_t bl_col = packColor(bl);

	std::uint32_t idx = m_vertices.size();

	Vec2 min = snapToPixel(rect.min());
	Vec2 max = snapToPixel({ rect.right(), rect.bottom() });

	m_vertices.push_back(UiVertex(glm::vec2(min.x, min.y), wuv, tl_col));
	m_vertices.push_back(UiVertex(glm::vec2(max.x, min.y), wuv, tr_col));
	m_vertices.push_back(UiVertex(glm::vec2(max.x, max.y), wuv, br_col));
	m_vertices.push_back(UiVertex(glm::vec2(min.x, max.y), wuv, bl_col));

	m_indices.push_back(idx + 0);
	m_indices.push_back(idx + 1);
	m_indices.push_back(idx + 2);
	m_indices.push_back(idx + 0);
	m_indices.push_back(idx + 2);
	m_indices.push_back(idx + 3);

	updateCurrentCommandIndexCount();
}

void DrawList::addLine(Vec2 a, Vec2 b, Color color, float thickness) {
	Vec2 pts[2] = { a, b };
	addPolyline(pts, 2, color, thickness, false);
}

void DrawList::addTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color color) {
	ensureCommand();
	glm::vec2 wuv(m_whitePixelUV.x, m_whitePixelUV.y);

	std::uint32_t col = packColor(color);
	std::uint32_t idx = m_vertices.size();

	Vec2 sa = snapToPixel(a);
	Vec2 sb = snapToPixel(b);
	Vec2 sc = snapToPixel(c);
	m_vertices.push_back(UiVertex(glm::vec2(sa.x, sa.y), wuv, col));
	m_vertices.push_back(UiVertex(glm::vec2(sb.x, sb.y), wuv, col));
	m_vertices.push_back(UiVertex(glm::vec2(sc.x, sc.y), wuv, col));

	m_indices.push_back(idx + 0);
	m_indices.push_back(idx + 1);
	m_indices.push_back(idx + 2);

	updateCurrentCommandIndexCount();
}

void DrawList::addCircleFilled(Vec2 centre, float radius, Color color, int segments) {
	if (segments <= 0) {
		segments = std::max(12, static_cast<int>(radius / 2));
	}

	ensureCommand();
	glm::vec2 wuv(m_whitePixelUV.x, m_whitePixelUV.y);

	std::uint32_t col = packColor(color);
	std::uint32_t baseIdx = m_vertices.size();

	for (int i = 0; i <= segments; ++i) {
		float angle = (i / static_cast<float>(segments)) * 6.28318530718f;
		Vec2 offset = { std::cos(angle) * radius, std::sin(angle) * radius };
		Vec2 pos = snapToPixel({ centre.x + offset.x, centre.y + offset.y });
		m_vertices.push_back(UiVertex(glm::vec2(pos.x, pos.y), wuv, col));
	}

	Vec2 snappedCentre = snapToPixel(centre);
	std::uint32_t centreIdx = m_vertices.size();
	m_vertices.push_back(UiVertex(glm::vec2(snappedCentre.x, snappedCentre.y), wuv, col));

	// Fan from the centre through each consecutive pair of perimeter points. The
	// perimeter loop above pushes segments+1 points (i=0..segments), where the last one
	// lands back on the first (angle 2*pi == angle 0), so this closes the circle without
	// any wraparound modulo. The previous version of this loop instead fanned from a
	// fixed perimeter point (baseIdx) through unrelated other perimeter points back to
	// the centre, which draws a self-intersecting wedge instead of a circle -- nothing
	// called addCircleFilled until RadioButton made the bug visible.
	for (int i = 0; i < segments; ++i) {
		m_indices.push_back(centreIdx);
		m_indices.push_back(baseIdx + i);
		m_indices.push_back(baseIdx + i + 1);
	}
}

void DrawList::addConvexPolyFilled(const Vec2* pts, int count, Color color) {
	if (count < 3) return;

	ensureCommand();
	glm::vec2 wuv(m_whitePixelUV.x, m_whitePixelUV.y);

	std::uint32_t col = packColor(color);
	std::uint32_t baseIdx = m_vertices.size();

	for (int i = 0; i < count; ++i) {
		Vec2 p = snapToPixel(pts[i]);
		m_vertices.push_back(UiVertex(glm::vec2(p.x, p.y), wuv, col));
	}

	for (int i = 1; i < count - 1; ++i) {
		m_indices.push_back(baseIdx);
		m_indices.push_back(baseIdx + i);
		m_indices.push_back(baseIdx + i + 1);
	}

	updateCurrentCommandIndexCount();
}

void DrawList::addPolyline(const Vec2* pts, int count, Color color, float thickness, bool closed) {
	if (count < 2) return;

	ensureCommand();
	glm::vec2 wuv(m_whitePixelUV.x, m_whitePixelUV.y);

	std::uint32_t col = packColor(color);
	float halfThick = thickness * 0.5f;

	for (int i = 0; i < count - 1; ++i) {
		Vec2 a = pts[i];
		Vec2 b = pts[i + 1];

		Vec2 dir = b - a;
		float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
		if (len < 0.001f) continue;

		dir.x /= len;
		dir.y /= len;

		Vec2 perp = { -dir.y * halfThick, dir.x * halfThick };

		std::uint32_t baseIdx = m_vertices.size();

		Vec2 v0 = snapToPixel({ a.x - perp.x, a.y - perp.y });
		Vec2 v1 = snapToPixel({ a.x + perp.x, a.y + perp.y });
		Vec2 v2 = snapToPixel({ b.x + perp.x, b.y + perp.y });
		Vec2 v3 = snapToPixel({ b.x - perp.x, b.y - perp.y });

		m_vertices.push_back(UiVertex(glm::vec2(v0.x, v0.y), wuv, col));
		m_vertices.push_back(UiVertex(glm::vec2(v1.x, v1.y), wuv, col));
		m_vertices.push_back(UiVertex(glm::vec2(v2.x, v2.y), wuv, col));
		m_vertices.push_back(UiVertex(glm::vec2(v3.x, v3.y), wuv, col));

		m_indices.push_back(baseIdx + 0);
		m_indices.push_back(baseIdx + 1);
		m_indices.push_back(baseIdx + 2);
		m_indices.push_back(baseIdx + 0);
		m_indices.push_back(baseIdx + 2);
		m_indices.push_back(baseIdx + 3);
	}

	if (closed && count >= 3) {
		Vec2 a = pts[count - 1];
		Vec2 b = pts[0];

		Vec2 dir = b - a;
		float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
		if (len > 0.001f) {
			dir.x /= len;
			dir.y /= len;

			Vec2 perp = { -dir.y * halfThick, dir.x * halfThick };

			std::uint32_t baseIdx = m_vertices.size();

			Vec2 cv0 = snapToPixel({ a.x - perp.x, a.y - perp.y });
			Vec2 cv1 = snapToPixel({ a.x + perp.x, a.y + perp.y });
			Vec2 cv2 = snapToPixel({ b.x + perp.x, b.y + perp.y });
			Vec2 cv3 = snapToPixel({ b.x - perp.x, b.y - perp.y });

			m_vertices.push_back(UiVertex(glm::vec2(cv0.x, cv0.y), wuv, col));
			m_vertices.push_back(UiVertex(glm::vec2(cv1.x, cv1.y), wuv, col));
			m_vertices.push_back(UiVertex(glm::vec2(cv2.x, cv2.y), wuv, col));
			m_vertices.push_back(UiVertex(glm::vec2(cv3.x, cv3.y), wuv, col));

			m_indices.push_back(baseIdx + 0);
			m_indices.push_back(baseIdx + 1);
			m_indices.push_back(baseIdx + 2);
			m_indices.push_back(baseIdx + 0);
			m_indices.push_back(baseIdx + 2);
			m_indices.push_back(baseIdx + 3);
		}
	}

	updateCurrentCommandIndexCount();
}

void DrawList::addText(const Font& font, float pixelSize, Vec2 topLeft, Color color, std::string_view utf8) {
	if (utf8.empty()) return;

	ensureCommand();

	std::uint32_t col = packColor(color);
	// glyph.offset is baseline-relative (negative for the ink above the baseline, which
	// is most of a typical glyph -- see stb_truetype's pc.yoff), but `topLeft` is
	// documented as the text's TOP-LEFT corner, and addTextClipped's callers size their
	// clip rect to exactly font.lineHeight() with no slack. Advancing the pen to the
	// baseline here (topLeft.y + ascent) is what makes "topLeft" actually mean top-left:
	// without it every glyph renders shifted up by a full ascent, which a tall clip rect
	// (a title bar, a button) has enough headroom to absorb invisibly, but a row sized to
	// exactly lineHeight() clips away everything except a sliver near the baseline.
	Vec2 pen = { topLeft.x, topLeft.y + font.ascent(pixelSize) };
	float scale = (font.bakedPixelSize() > 0.0f) ? pixelSize / font.bakedPixelSize() : 0.0f;

	std::size_t pos = 0;
	while (pos < utf8.size()) {
		std::uint32_t cp = decodeUtf8(utf8, pos);
		const Glyph& glyph = font.glyphFor(cp);
		float advance = glyph.xAdvance * scale;

		// Space and tab advance the pen but never emit a quad -- there is nothing to
		// blend, and it is one less draw-list entry per word gap.
		if (cp != ' ' && cp != '\t' && glyph.size.x > 0.0f && glyph.size.y > 0.0f) {
			Vec2 tl = { pen.x + glyph.offset.x * scale, pen.y + glyph.offset.y * scale };
			Vec2 br = { tl.x + glyph.size.x * scale, tl.y + glyph.size.y * scale };
			tl = snapToPixel(tl);
			br = snapToPixel(br);

			std::uint32_t idx = m_vertices.size();
			m_vertices.push_back(UiVertex({ tl.x, tl.y }, { glyph.uv0.x, glyph.uv0.y }, col));
			m_vertices.push_back(UiVertex({ br.x, tl.y }, { glyph.uv1.x, glyph.uv0.y }, col));
			m_vertices.push_back(UiVertex({ br.x, br.y }, { glyph.uv1.x, glyph.uv1.y }, col));
			m_vertices.push_back(UiVertex({ tl.x, br.y }, { glyph.uv0.x, glyph.uv1.y }, col));

			m_indices.push_back(idx + 0);
			m_indices.push_back(idx + 1);
			m_indices.push_back(idx + 2);
			m_indices.push_back(idx + 0);
			m_indices.push_back(idx + 2);
			m_indices.push_back(idx + 3);
		}

		pen.x += advance;
	}

	updateCurrentCommandIndexCount();
}

void DrawList::addTextClipped(const Font& font, float pixelSize, const Rect& rect, Color color,
                               std::string_view utf8, Align h, Align v) {
	static constexpr std::string_view kEllipsis = "...";

	std::string_view display = utf8;
	std::string truncated;

	Vec2 textSize = font.measureText(utf8, pixelSize);
	if (textSize.x > rect.w) {
		float ellipsisWidth = font.measureText(kEllipsis, pixelSize).x;
		float available = std::max(0.0f, rect.w - ellipsisWidth);

		// Codepoint boundaries only -- a byte-by-byte search could cut a multi-byte
		// character in half.
		std::vector<std::size_t> boundaries;
		boundaries.push_back(0);
		for (std::size_t p = 0; p < utf8.size();) {
			p = utf8NextBoundary(utf8, p);
			boundaries.push_back(p);
		}

		std::size_t lo = 0, hi = boundaries.size() - 1, best = 0;
		while (lo <= hi) {
			std::size_t mid = lo + (hi - lo) / 2;
			float w = font.measureText(utf8.substr(0, boundaries[mid]), pixelSize).x;
			if (w <= available) {
				best = mid;
				lo = mid + 1;
			} else {
				if (mid == 0) break;
				hi = mid - 1;
			}
		}

		truncated.assign(utf8.substr(0, boundaries[best]));
		truncated += kEllipsis;
		display = truncated;
		textSize = font.measureText(display, pixelSize);
	}

	float x;
	switch (h) {
		case Align::Center: x = rect.x + (rect.w - textSize.x) * 0.5f; break;
		case Align::End:    x = rect.right() - textSize.x; break;
		default:            x = rect.x; break;
	}

	float lh = font.lineHeight(pixelSize);
	float y;
	switch (v) {
		case Align::Center: y = rect.y + (rect.h - lh) * 0.5f; break;
		case Align::End:    y = rect.bottom() - lh; break;
		default:            y = rect.y; break;
	}

	pushClipRect(rect);
	addText(font, pixelSize, { x, y }, color, display);
	popClipRect();
}

void DrawList::append(const DrawList& other) {
	if (other.m_vertices.empty()) {
		return;
	}

	std::uint32_t vertexOffset = static_cast<std::uint32_t>(m_vertices.size());
	std::uint32_t indexOffset = static_cast<std::uint32_t>(m_indices.size());

	m_vertices.insert(m_vertices.end(), other.m_vertices.begin(), other.m_vertices.end());
	m_indices.reserve(m_indices.size() + other.m_indices.size());
	for (std::uint32_t idx : other.m_indices) {
		m_indices.push_back(idx + vertexOffset);
	}

	for (const DrawCmd& cmd : other.m_commands) {
		if (cmd.indexCount == 0) {
			continue;
		}
		DrawCmd copy = cmd;
		copy.indexOffset += indexOffset;
		m_commands.push_back(copy);
	}
}

} // namespace lightGraphics::ui
