#pragma once

#include "Types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lightGraphics::ui {

struct GlyphRange {
	std::uint32_t first;
	std::uint32_t last;   // inclusive
};

// The default glyph set: Basic Latin + Latin-1 Supplement, Greek, arrows, middle dot.
// ~250 glyphs total -- comfortable in a 512x512 R8 atlas at 14px with 2x2 oversampling.
extern const GlyphRange kDefaultGlyphRanges[];
extern const std::size_t kDefaultGlyphRangeCount;

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

	// Never returns a zero-size quad: an unbaked codepoint resolves to the fallback
	// glyph rather than being silently invisible.
	const Glyph& glyphFor(std::uint32_t codepoint) const;
	bool hasGlyph(std::uint32_t codepoint) const;

	Vec2 whitePixelUV() const { return m_whitePixelUV; }
	int atlasWidth() const { return m_atlasWidth; }
	int atlasHeight() const { return m_atlasHeight; }
	const std::vector<std::uint8_t>& atlasPixels() const { return m_atlasPixels; }

	float bakedPixelSize() const { return m_bakedPixelSize; }
	float contentScaleAtBake() const { return m_contentScaleAtBake; }

private:
	void bakeInternal(const std::vector<std::uint8_t>& ttfData, float pixelHeight,
	                   const GlyphRange* ranges, std::size_t rangeCount,
	                   int atlasWidth, int atlasHeight);

	static constexpr std::size_t kFlatLookupSize = 256;
	static constexpr std::uint16_t kInvalidGlyphSlot = 0xFFFF;

	bool m_baked = false;
	int m_atlasWidth = 0;
	int m_atlasHeight = 0;
	std::vector<std::uint8_t> m_atlasPixels;
	Vec2 m_whitePixelUV;

	float m_bakedPixelSize = 0.0f;
	float m_contentScaleAtBake = 1.0f;
	float m_ascentAtBake = 0.0f;
	float m_lineHeightAtBake = 0.0f;
	// Widest xAdvance among baked '0'-'9', at bake pixel size (unscaled) -- 0 if no
	// digit was baked. See advanceFor()'s comment.
	float m_tabularDigitAdvanceAtBake = 0.0f;

	std::vector<Glyph> m_glyphs;
	std::array<std::uint16_t, kFlatLookupSize> m_flatLookup;
	std::unordered_map<std::uint32_t, std::uint16_t> m_extendedLookup;
	std::uint16_t m_fallbackGlyphSlot = kInvalidGlyphSlot;
};

} // namespace lightGraphics::ui
