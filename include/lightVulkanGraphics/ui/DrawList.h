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
	// kAtlasTextureId (0) for everything drawn so far in this library (glyphs, solid
	// fills -- see Types.h's comment); a registered id from addImage() otherwise.
	TextureId textureId;
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

	// Draws a registered texture (VkApp::registerUiTexture(), docs/gui/05-widgets.md,
	// "Image") over `rect`, sampling the sub-rectangle [uv0, uv1] of it (defaults to the
	// whole image) and multiplying by `tint` (defaults to opaque white, i.e. no tint).
	// Unlike every other add*() here, this can start a genuinely NEW draw command even
	// when the clip rect hasn't changed -- see ensureCommand()'s comment -- because each
	// texture needs its own descriptor set bound at record() time (docs/gui/02-
	// rendering.md's "one pipeline, one descriptor set" goal is about the common case;
	// a widget that actually needs a second image is exactly what earns a second bind).
	void addImage(TextureId textureId, const Rect& rect, Vec2 uv0 = { 0.0f, 0.0f },
	              Vec2 uv1 = { 1.0f, 1.0f }, Color tint = Color(0xFF, 0xFF, 0xFF, 0xFF));

	// Appends another DrawList's geometry onto this one: vertices are copied verbatim,
	// indices are offset to stay valid, and non-empty commands are copied with their
	// indexOffset adjusted. Used by GuiContext::endFrame() to merge the overlay list
	// (tooltips, popups, world labels) after the panel draw pass -- see
	// docs/gui/01-architecture.md, per-frame sequence step 5.
	void append(const DrawList& other);

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

	// Closes out the current command if it can't absorb the next primitive: a clip-rect
	// change (as before) OR a textureId change (addImage() only -- every other add*()
	// call passes the default kAtlasTextureId, so their own behaviour is unchanged:
	// splitting on clip rect alone). Consecutive addImage() calls for the SAME texture
	// still batch into one command, same as any other same-clip-rect run of primitives.
	void ensureCommand(TextureId textureId = kAtlasTextureId);
	void updateCurrentCommandIndexCount();
	Rect getCurrentClipRect() const;
	std::uint32_t packColor(Color c) const;

	// Appends a translucent ~1px fringe ring OUTSIDE an already-emitted, already-snapped
	// convex vertex loop -- softens what would otherwise be a hard aliased silhouette
	// against whatever is drawn behind it (docs/gui/02-rendering.md, "Anti-aliasing").
	// `innerBase`/`count` name the loop's own vertices, already sitting at
	// m_vertices[innerBase .. innerBase + count), in order around the shape; NOT closed
	// as input (this function supplies the wraparound edge itself). The loop's existing
	// vertices/indices are only ever READ here, never modified -- fringe geometry is
	// strictly appended after everything the caller already emitted, so no vertex index
	// a caller (or a test) already computed is ever invalidated by calling this.
	// `count < 3` is a no-op (degenerate; nothing to ring).
	void addConvexFillAAFringe(std::uint32_t innerBase, std::size_t count, Color baseColor);
};

} // namespace lightGraphics::ui
