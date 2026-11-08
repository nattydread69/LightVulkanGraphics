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

#include "Types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lightGraphics::ui {

struct GlyphRange {
	std::uint32_t first;
	std::uint32_t last;   // inclusive
};

// The default glyph set: Basic Latin + Latin-1 Supplement, Greek, general punctuation
// (dashes, curly quotes, bullet, ellipsis), arrows, minus sign, euro. ~300 glyphs total
// -- comfortable in a 512x512 R8 atlas at 14px with 2x2 oversampling. Anything outside
// this set is baked on demand the first time it is drawn (FontOptions::dynamicGlyphs).
extern const GlyphRange kDefaultGlyphRanges[];
extern const std::size_t kDefaultGlyphRangeCount;

// How glyph coverage is stored in the atlas.
enum class FontRenderMode {
	// Anti-aliased coverage rasterised at the bake size (2x2 oversampled by default).
	// Sharpest at exactly the bake size; the default for UI text.
	Bitmap,
	// Signed distance field: the red channel is distance to the outline, 0.5 on the edge.
	// Scales cleanly over a wide size range from a single bake, so it suits headings,
	// zoomed or world-space text. It must be drawn with the SDF pipeline
	// (UiRenderer::registerTexture(..., TextureKind::Sdf)); the plain coverage shader
	// would render it as a blurry blob.
	Sdf,
};

// Everything here must be set BEFORE bake(); setOptions() on an already-baked Font only
// takes effect at the next bake().
struct FontOptions {
	FontRenderMode mode = FontRenderMode::Bitmap;
	// Apply the font's pair-kerning (GPOS 'kern' feature or legacy 'kern' table) when
	// measuring and drawing. Per-call opt-out: TextFlags::NoKerning.
	bool kerning = true;
	// Bake glyphs outside the pre-baked ranges the first time they are requested, as long
	// as the primary face or a fallback face covers them. Each new glyph bumps
	// atlasRevision(); the renderer re-uploads the atlas before the next draw.
	bool dynamicGlyphs = true;
	// Bitmap mode only: 1 (crisp, pixel-snapped look) .. 4. 2 is the long-standing default.
	int oversample = 2;
	// Sdf mode only: distance range in pixels at the bake size. 0 = automatic
	// (about 1/8 of the bake height, at least 3).
	int sdfPadding = 0;
};

struct Glyph {
	Vec2 uv0, uv1;          // atlas coordinates, [0,1]
	Vec2 offset;            // pen-relative top-left, in pixels at bake size
	Vec2 size;              // quad size, in pixels at bake size
	float xAdvance = 0.0f;  // pen advance, in pixels at bake size
};

// Bakes a TrueType font into an R8 atlas and answers text-measurement queries. Font
// owns no Vulkan or GLFW state; the renderer uploads atlasPixels() to a texture.
class Font {
public:
	Font();
	~Font();

	Font(const Font&) = delete;
	Font& operator=(const Font&) = delete;
	Font(Font&&) noexcept;
	Font& operator=(Font&&) noexcept;

	// ---- configuration (before bake) ----
	void setOptions(const FontOptions& options) { m_options = options; }
	const FontOptions& options() const { return m_options; }

	// Fallback faces, consulted in order for any codepoint the primary face lacks (e.g. a
	// symbol font behind a text font, or a CJK face). They are baked into the SAME atlas as
	// the primary face, so a fallback glyph costs no extra texture bind or draw call. Each
	// face is scaled to the same pixel height as the primary. The list survives bake()
	// calls (a content-scale rebake keeps its fallbacks); call before bake().
	void addFallbackFace(std::vector<std::uint8_t> ttfData);
	void clearFallbackFaces();
	std::size_t fallbackFaceCount() const;

	// ---- file / system-font helpers ----
	// Reads a whole file. Throws std::runtime_error naming the path on failure.
	static std::vector<std::uint8_t> readFile(const std::string& path);
	// Looks for a font file by file name (e.g. "DejaVuSansMono.ttf") in the usual system
	// font directories (Linux, macOS, Windows) and returns the full path, or an empty
	// string if it isn't installed. Directories are searched recursively.
	static std::string findSystemFont(const std::string& fileName);

	// Bakes `ranges` at `pixelHeight` -- the PHYSICAL pixel height to render glyphs at.
	// CALLER CONTRACT: `pixelHeight` must already have `contentScale` folded in, i.e.
	// pass `fontSize_logical * contentScale`, not `fontSize_logical` alone. For a 14px
	// logical font at 2x content scale, the correct call is
	// `bake(ttfData, 28.0f, ..., /*contentScale=*/2.0f)` -- calling
	// `bake(ttfData, 14.0f, ..., /*contentScale=*/2.0f)` is a caller error: the atlas
	// would be baked at the same physical resolution as a 1x display, so
	// measureText()/lineHeight()/ascent() (which divide by `pixelHeight`, not
	// `contentScale`, to convert back to logical pixels -- see below) would silently
	// return HALF the intended logical size instead of leaving glyphs blurry, which is
	// easy to miss in a quick visual check. `contentScale` itself is only recorded for
	// the caller's own rebake-threshold bookkeeping (compare against the live content
	// scale each frame and rebake past ~1% drift); Font has no independent way to
	// verify `pixelHeight` was actually derived from it, so this cannot be enforced
	// with an assert -- only documented.
	//
	// Deviation from docs/gui/03-text-and-fonts.md: the spec's signature takes
	// std::span<const GlyphRange>, which is a C++20 type; this project is pinned to
	// C++17, so it is a pointer+count pair here instead. The spec's `bool` return was
	// also replaced with a throw, to match the error-handling policy in
	// docs/gui/07-public-api.md ("construction failures throw"): a font that fails to
	// bake even after the one-shot 1024x1024 retry cannot be silently used, so there is
	// no meaningful `false` case left to return.
	//
	// Throws std::runtime_error if `ttfData` cannot be parsed as a TrueType font, or if
	// packing still fails after one retry at 1024x1024.
	void bake(const std::vector<std::uint8_t>& ttfData, float pixelHeight,
	          const GlyphRange* ranges, std::size_t rangeCount,
	          int atlasWidth = 512, int atlasHeight = 512, float contentScale = 1.0f);

	// Convenience overload baking kDefaultGlyphRanges.
	void bake(const std::vector<std::uint8_t>& ttfData, float pixelHeight,
	          int atlasWidth = 512, int atlasHeight = 512, float contentScale = 1.0f);

	// Convenience: readFile() + bake() with the default ranges.
	void bakeFromFile(const std::string& path, float pixelHeight, int atlasWidth = 512,
	                  int atlasHeight = 512, float contentScale = 1.0f);

	bool isBaked() const { return m_baked; }

	// `pixelSize` is the size to measure/render at, in the same logical-pixel units as
	// theme.fontSize; results come back in those same logical units. This works because
	// the atlas is baked at a physical pixel height (>= pixelSize on a HiDPI display)
	// and every measurement below scales by pixelSize / bakedPixelSize(): dividing by
	// the larger, physical bake height is what converts glyph metrics down to logical
	// pixels. See the DPI section of docs/gui/06-layout-and-theme.md -- getting the
	// direction of that division backwards is the most common bug here.
	Vec2 measureText(std::string_view utf8, float pixelSize, TextFlags flags = TextFlags::None) const;
	float lineHeight(float pixelSize) const;
	float ascent(float pixelSize) const;

	// Pair-kerning adjustment to add to the pen after `left` and before `right`, in the
	// same logical units as `pixelSize` (usually <= 0: pairs like "AV" tuck together).
	// Zero when kerning is disabled, the glyphs come from different faces, or the font has
	// no kerning data for the pair. Already folded into measureText(), offsetAtIndex(),
	// indexAtOffset() and DrawList::addText(); exposed for callers laying out glyphs
	// themselves.
	float kerning(std::uint32_t left, std::uint32_t right, float pixelSize) const;
	// kerning() honouring TextFlags: 0 under NoKerning, and between two ASCII digits under
	// Tabular when either neighbour is an ASCII digit (kerning there would make the text
	// after a live-updating number depend on which digits it shows). `left == 0` (no previous
	// glyph) is also 0. This is what every text layout routine in the library calls.
	float kerningFor(std::uint32_t left, std::uint32_t right, float pixelSize, TextFlags flags) const;

	// The pen advance to use for `codepoint` at `pixelSize`. Identical to
	// `glyphFor(codepoint).xAdvance`, scaled, EXCEPT when `flags` has TextFlags::Tabular
	// and `codepoint` is an ASCII digit ('0'-'9'): then it returns the widest baked
	// digit's own (scaled) advance instead, so every digit in a live-updating numeric
	// readout occupies the same pen-advance width regardless of which digit it actually
	// is (docs/gui/03-text-and-fonts.md, "Tabular figures"). Falls back to the glyph's
	// own advance if no digits were baked at all (tabularDigitAdvance would be 0).
	float advanceFor(std::uint32_t codepoint, float pixelSize, TextFlags flags = TextFlags::None) const;

	// Byte index of the character boundary nearest to pixel offset x along the string.
	std::size_t indexAtOffset(std::string_view utf8, float pixelSize, float x) const;
	// Pixel offset of the glyph starting at byteIndex (accumulated advance up to it).
	float offsetAtIndex(std::string_view utf8, float pixelSize, std::size_t byteIndex) const;

	// Never returns a zero-size quad: a codepoint no face covers resolves to the fallback
	// glyph rather than being silently invisible. With FontOptions::dynamicGlyphs a
	// covered-but-not-yet-baked codepoint is baked right here (hence the atlas members
	// below being mutable); the returned reference stays valid for the Font's lifetime
	// of this bake. NOT thread-safe: like the rest of the GUI, call from one thread.
	const Glyph& glyphFor(std::uint32_t codepoint) const;
	// True iff the glyph is already resident in the atlas (it does not trigger a bake).
	bool hasGlyph(std::uint32_t codepoint) const;
	// True iff the primary or a fallback face has a real glyph for `codepoint`, whether or
	// not it has been baked yet.
	bool canRender(std::uint32_t codepoint) const;

	// Incremented whenever the atlas pixels change after bake() -- i.e. every time a
	// dynamic glyph is added (bake() itself also bumps it). A renderer holding a GPU copy
	// re-uploads when this differs from the value it last uploaded.
	std::uint64_t atlasRevision() const { return m_revision; }
	bool isSdf() const { return m_options.mode == FontRenderMode::Sdf; }
	// Sdf mode: the distance range in pixels at the bake size actually used.
	int sdfPadding() const { return m_sdfPadding; }

	Vec2 whitePixelUV() const { return m_whitePixelUV; }
	int atlasWidth() const { return m_atlasWidth; }
	int atlasHeight() const { return m_atlasHeight; }
	const std::vector<std::uint8_t>& atlasPixels() const { return m_atlasPixels; }

	float bakedPixelSize() const { return m_bakedPixelSize; }
	float contentScaleAtBake() const { return m_contentScaleAtBake; }

private:
	struct Impl;

	void bakeInternal(const std::vector<std::uint8_t>& ttfData, float pixelHeight,
	                   const GlyphRange* ranges, std::size_t rangeCount,
	                   int atlasWidth, int atlasHeight);
	// Looks up or lazily bakes; returns kInvalidGlyphSlot if no face covers it / atlas full.
	std::uint32_t slotFor(std::uint32_t codepoint, bool allowBake) const;
	bool bakeOneGlyph(std::uint32_t codepoint, int face) const;
	int faceFor(std::uint32_t codepoint) const;

	static constexpr std::size_t kFlatLookupSize = 256;
	static constexpr std::uint32_t kInvalidGlyphSlot = 0xFFFFFFFFu;

	FontOptions m_options;
	bool m_baked = false;
	int m_atlasWidth = 0;
	int m_atlasHeight = 0;
	int m_sdfPadding = 0;
	mutable std::vector<std::uint8_t> m_atlasPixels;
	mutable std::uint64_t m_revision = 0;
	Vec2 m_whitePixelUV;

	float m_bakedPixelSize = 0.0f;
	float m_contentScaleAtBake = 1.0f;
	float m_ascentAtBake = 0.0f;
	float m_lineHeightAtBake = 0.0f;
	// Widest xAdvance among baked '0'-'9', at bake pixel size (unscaled) -- 0 if no
	// digit was baked. See advanceFor()'s comment.
	float m_tabularDigitAdvanceAtBake = 0.0f;

	// A deque so references returned by glyphFor() survive later dynamic bakes.
	mutable std::deque<Glyph> m_glyphs;
	mutable std::array<std::uint32_t, kFlatLookupSize> m_flatLookup;
	mutable std::unordered_map<std::uint32_t, std::uint32_t> m_extendedLookup;
	std::uint32_t m_fallbackGlyphSlot = kInvalidGlyphSlot;

	std::unique_ptr<Impl> m_impl;
};

} // namespace lightGraphics::ui
