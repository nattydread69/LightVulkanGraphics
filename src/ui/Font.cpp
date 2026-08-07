#include <lightVulkanGraphics/ui/Font.h>
#include "Utf8.h"

#include "stb/stb_truetype.h"

#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>

namespace lightGraphics::ui {

const GlyphRange kDefaultGlyphRanges[] = {
	{ 0x0020, 0x00FF },   // Basic Latin + Latin-1 Supplement (deg, +-, sup2, micro, x, /)
	{ 0x0391, 0x03C9 },   // Greek
	{ 0x2190, 0x2193 },   // Arrows
	{ 0x00B7, 0x00B7 },   // Middle dot
};
const std::size_t kDefaultGlyphRangeCount = sizeof(kDefaultGlyphRanges) / sizeof(kDefaultGlyphRanges[0]);

Font::Font() {
	m_flatLookup.fill(kInvalidGlyphSlot);
}

Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

void Font::bake(const std::vector<std::uint8_t>& ttfData, float pixelHeight,
                 const GlyphRange* ranges, std::size_t rangeCount,
                 int atlasWidth, int atlasHeight, float contentScale) {
	// This cannot verify pixelHeight actually has contentScale folded in (see the
	// caller-contract note on the declaration) -- only catch obviously-broken values.
	assert(contentScale > 0.0f && "Font::bake: contentScale must be positive");
	bakeInternal(ttfData, pixelHeight, ranges, rangeCount, atlasWidth, atlasHeight);
	m_contentScaleAtBake = contentScale;
}

void Font::bake(const std::vector<std::uint8_t>& ttfData, float pixelHeight,
                 int atlasWidth, int atlasHeight, float contentScale) {
	bake(ttfData, pixelHeight, kDefaultGlyphRanges, kDefaultGlyphRangeCount,
	     atlasWidth, atlasHeight, contentScale);
}

void Font::bakeInternal(const std::vector<std::uint8_t>& ttfData, float pixelHeight,
                         const GlyphRange* ranges, std::size_t rangeCount,
                         int atlasWidth, int atlasHeight) {
	stbtt_fontinfo fontInfo{};
	int fontOffset = stbtt_GetFontOffsetForIndex(ttfData.data(), 0);
	if (fontOffset < 0 || !stbtt_InitFont(&fontInfo, ttfData.data(), fontOffset)) {
		throw std::runtime_error("Font::bake: could not parse TrueType font data");
	}

	constexpr int kWhiteBlockRows = 4;
	std::vector<std::uint8_t> pixels(static_cast<std::size_t>(atlasWidth) * static_cast<std::size_t>(atlasHeight), 0);

	std::vector<std::vector<stbtt_packedchar>> packedPerRange(rangeCount);
	std::vector<stbtt_pack_range> packRanges(rangeCount);
	std::size_t totalGlyphs = 0;
	for (std::size_t i = 0; i < rangeCount; ++i) {
		std::uint32_t count = ranges[i].last - ranges[i].first + 1;
		totalGlyphs += count;
		packedPerRange[i].resize(count);

		stbtt_pack_range pr{};
		pr.font_size = pixelHeight;
		pr.first_unicode_codepoint_in_range = static_cast<int>(ranges[i].first);
		pr.array_of_unicode_codepoints = nullptr;
		pr.num_chars = static_cast<int>(count);
		pr.chardata_for_range = packedPerRange[i].data();
		packRanges[i] = pr;
	}

	stbtt_pack_context packContext{};
	int packedHeight = atlasHeight - kWhiteBlockRows;
	if (!stbtt_PackBegin(&packContext, pixels.data(), atlasWidth, packedHeight, atlasWidth, 1, nullptr)) {
		throw std::runtime_error("Font::bake: stbtt_PackBegin failed");
	}
	stbtt_PackSetOversampling(&packContext, 2, 2);

	// One range per call, deliberately. stb_truetype's `missing_glyph` bookkeeping in
	// stbtt_PackFontRangesRenderIntoRects is a per-character index that is NOT reset
	// between ranges, so a codepoint with no glyph in a later range indexes that
	// range's (possibly much shorter) chardata array with an offset taken from an
	// earlier, longer one -- an out-of-bounds read of one stbtt_packedchar.
	//
	// This is reachable rather than theoretical: U+03A2 is an unassigned hole in the
	// Greek block, so no font has it, and it sets missing_glyph = 17 while packing our
	// 57-entry Greek range. Any missing glyph in the 4-entry arrow range or the
	// 1-entry middle-dot range that follows then reads index 17 of a 4- or 1-element
	// array. Packing one range per call keeps the index inside the array it came from.
	int ok = 1;
	for (std::size_t i = 0; i < rangeCount; ++i) {
		if (!stbtt_PackFontRanges(&packContext, ttfData.data(), 0, &packRanges[i], 1)) {
			ok = 0;
		}
	}

	stbtt_PackEnd(&packContext);

	if (!ok) {
		if (atlasWidth == 1024 && atlasHeight == 1024) {
			throw std::runtime_error(
				"Font::bake: atlas too small at " + std::to_string(pixelHeight) +
				"px for " + std::to_string(totalGlyphs) + " glyphs, even at 1024x1024");
		}
		bakeInternal(ttfData, pixelHeight, ranges, rangeCount, 1024, 1024);
		return;
	}

	for (int y = atlasHeight - kWhiteBlockRows; y < atlasHeight; ++y) {
		for (int x = 0; x < atlasWidth; ++x) {
			pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(atlasWidth) + static_cast<std::size_t>(x)] = 0xFF;
		}
	}

	m_glyphs.clear();
	m_glyphs.reserve(totalGlyphs);
	m_flatLookup.fill(kInvalidGlyphSlot);
	m_extendedLookup.clear();

	for (std::size_t r = 0; r < rangeCount; ++r) {
		for (std::size_t i = 0; i < packedPerRange[r].size(); ++i) {
			const stbtt_packedchar& pc = packedPerRange[r][i];
			std::uint32_t codepoint = ranges[r].first + static_cast<std::uint32_t>(i);

			Glyph g;
			g.uv0 = { pc.x0 / static_cast<float>(atlasWidth), pc.y0 / static_cast<float>(atlasHeight) };
			g.uv1 = { pc.x1 / static_cast<float>(atlasWidth), pc.y1 / static_cast<float>(atlasHeight) };
			g.offset = { pc.xoff, pc.yoff };
			// The quad's DISPLAY size is xoff2-xoff, not the atlas rect (x1-x0). With
			// 2x2 oversampling the atlas rect is about twice the display size, so using
			// it here draws every glyph at double width and the characters overlap.
			// stb divides the oversampling back out into xoff2/yoff2 (this is what
			// stbtt_GetPackedQuad uses); the atlas rect stays in atlas texels and is
			// only correct for the UVs above.
			g.size = { pc.xoff2 - pc.xoff, pc.yoff2 - pc.yoff };
			g.xAdvance = pc.xadvance;

			auto slot = static_cast<std::uint16_t>(m_glyphs.size());
			m_glyphs.push_back(g);

			if (codepoint < kFlatLookupSize) {
				m_flatLookup[codepoint] = slot;
			} else {
				m_extendedLookup[codepoint] = slot;
			}
		}
	}

	// Prefer U+FFFD as the fallback glyph if the font has it, else '?', else whatever
	// baked first -- but never leave a codepoint silently invisible.
	if (hasGlyph(0xFFFD)) {
		m_fallbackGlyphSlot = m_extendedLookup[0xFFFD];
	} else if (hasGlyph('?')) {
		m_fallbackGlyphSlot = m_flatLookup['?'];
	} else {
		m_fallbackGlyphSlot = 0;
	}

	m_atlasPixels = std::move(pixels);
	m_atlasWidth = atlasWidth;
	m_atlasHeight = atlasHeight;
	m_whitePixelUV = { 2.0f / static_cast<float>(atlasWidth), (static_cast<float>(atlasHeight) - 2.0f) / static_cast<float>(atlasHeight) };
	m_bakedPixelSize = pixelHeight;

	int ascentRaw = 0, descentRaw = 0, lineGapRaw = 0;
	stbtt_GetFontVMetrics(&fontInfo, &ascentRaw, &descentRaw, &lineGapRaw);
	float unitsScale = stbtt_ScaleForPixelHeight(&fontInfo, pixelHeight);
	m_ascentAtBake = static_cast<float>(ascentRaw) * unitsScale;
	m_lineHeightAtBake = static_cast<float>(ascentRaw - descentRaw + lineGapRaw) * unitsScale;

	m_baked = true;
}

bool Font::hasGlyph(std::uint32_t codepoint) const {
	if (codepoint < kFlatLookupSize) {
		return m_flatLookup[codepoint] != kInvalidGlyphSlot;
	}
	return m_extendedLookup.find(codepoint) != m_extendedLookup.end();
}

const Glyph& Font::glyphFor(std::uint32_t codepoint) const {
	assert(!m_glyphs.empty() && "Font::glyphFor called before a successful bake()");

	std::uint16_t slot = kInvalidGlyphSlot;
	if (codepoint < kFlatLookupSize) {
		slot = m_flatLookup[codepoint];
	} else {
		auto it = m_extendedLookup.find(codepoint);
		if (it != m_extendedLookup.end()) {
			slot = it->second;
		}
	}
	if (slot == kInvalidGlyphSlot) {
		slot = m_fallbackGlyphSlot;
	}
	return m_glyphs[slot];
}

Vec2 Font::measureText(std::string_view utf8, float pixelSize) const {
	float scale = (m_bakedPixelSize > 0.0f) ? pixelSize / m_bakedPixelSize : 0.0f;
	float width = 0.0f;
	std::size_t pos = 0;
	while (pos < utf8.size()) {
		std::uint32_t cp = decodeUtf8(utf8, pos);
		width += glyphFor(cp).xAdvance * scale;
	}
	return { width, lineHeight(pixelSize) };
}

float Font::lineHeight(float pixelSize) const {
	float scale = (m_bakedPixelSize > 0.0f) ? pixelSize / m_bakedPixelSize : 0.0f;
	return m_lineHeightAtBake * scale;
}

float Font::ascent(float pixelSize) const {
	float scale = (m_bakedPixelSize > 0.0f) ? pixelSize / m_bakedPixelSize : 0.0f;
	return m_ascentAtBake * scale;
}

float Font::offsetAtIndex(std::string_view utf8, float pixelSize, std::size_t byteIndex) const {
	float scale = (m_bakedPixelSize > 0.0f) ? pixelSize / m_bakedPixelSize : 0.0f;
	float x = 0.0f;
	std::size_t pos = 0;
	while (pos < byteIndex && pos < utf8.size()) {
		std::uint32_t cp = decodeUtf8(utf8, pos);
		x += glyphFor(cp).xAdvance * scale;
	}
	return x;
}

std::size_t Font::indexAtOffset(std::string_view utf8, float pixelSize, float x) const {
	float scale = (m_bakedPixelSize > 0.0f) ? pixelSize / m_bakedPixelSize : 0.0f;
	float pen = 0.0f;
	std::size_t pos = 0;
	while (pos < utf8.size()) {
		std::size_t next = pos;
		std::uint32_t cp = decodeUtf8(utf8, next);
		float advance = glyphFor(cp).xAdvance * scale;
		if (x < pen + advance * 0.5f) {
			return pos;
		}
		pen += advance;
		pos = next;
	}
	return utf8.size();
}

} // namespace lightGraphics::ui
