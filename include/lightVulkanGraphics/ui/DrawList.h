#pragma once

#include "Types.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <string_view>

namespace lightGraphics::ui {

class Font;

struct UiVertex {
	glm::vec2 pos;
	glm::vec2 uv;
	std::uint32_t color;

	UiVertex() = default;
	UiVertex(glm::vec2 pos_, glm::vec2 uv_, std::uint32_t color_)
		: pos(pos_), uv(uv_), color(color_) {}
};

static_assert(sizeof(UiVertex) == 20, "UiVertex must be exactly 20 bytes");

struct DrawCmd {
	std::uint32_t indexOffset;
	std::uint32_t indexCount;
	Rect clipRect;
	std::uint32_t textureId;
};

class DrawList {
public:
	DrawList();
	~DrawList();

	void clear();

	void pushClipRect(const Rect& rect, bool intersectWithCurrent = true);
	void popClipRect();

	void addRectFilled(const Rect& rect, Color color, float rounding = 0.0f);
	void addRect(const Rect& rect, Color color, float thickness = 1.0f, float rounding = 0.0f);
	void addRectFilledMultiColor(const Rect& rect, Color tl, Color tr, Color br, Color bl);
	void addLine(Vec2 a, Vec2 b, Color color, float thickness = 1.0f);
	void addTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color color);
	void addCircleFilled(Vec2 centre, float radius, Color color, int segments = 0);
	void addConvexPolyFilled(const Vec2* pts, int count, Color color);
	void addPolyline(const Vec2* pts, int count, Color color, float thickness, bool closed);

	void addText(const Font& font, float pixelSize, Vec2 topLeft, Color color, std::string_view utf8);
	void addTextClipped(const Font& font, float pixelSize, const Rect& rect, Color color,
	                     std::string_view utf8, Align h, Align v);

	// Every non-text primitive samples this UV so it shares the font atlas texture
	// instead of needing a texture switch. Defaults to {0,0} (only correct once a Font
	// has baked and called this) -- see docs/gui/02-rendering.md.
	void setWhitePixelUV(Vec2 uv) { m_whitePixelUV = uv; }
	Vec2 whitePixelUV() const { return m_whitePixelUV; }

	const std::vector<UiVertex>& vertices() const { return m_vertices; }
	const std::vector<std::uint32_t>& indices() const { return m_indices; }
	const std::vector<DrawCmd>& commands() const { return m_commands; }

private:
	std::vector<UiVertex> m_vertices;
	std::vector<std::uint32_t> m_indices;
	std::vector<DrawCmd> m_commands;
	std::vector<Rect> m_clipStack;
	Vec2 m_whitePixelUV;

	void ensureCommand();
	void updateCurrentCommandIndexCount();
	Rect getCurrentClipRect() const;
	std::uint32_t packColor(Color c) const;
};

} // namespace lightGraphics::ui
