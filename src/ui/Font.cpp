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

#include <lightVulkanGraphics/ui/Font.h>
#include "Utf8.h"

// The UI library's only copy of stb_truetype, compiled STATIC: every stbtt_* function is
// private to this file (the only one that uses it), so LightVulkanGraphicsUI exports none
// of them and an application can use or vendor its own stb_truetype without symbol
// clashes. STATIC leaves the parts this file does not call unused, which the compilers
// would warn about once each.
#if defined(__GNUC__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#	pragma warning(push)
#	pragma warning(disable : 4505) // unreferenced function with internal linkage removed
#	pragma warning(disable : 4244) // stb_truetype's own narrowing conversions
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"
#if defined(__GNUC__)
#	pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#	pragma warning(pop)
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace lightGraphics::ui {

const GlyphRange kDefaultGlyphRanges[] = {
	{ 0x0020, 0x00FF },   // Basic Latin + Latin-1 Supplement (deg, +-, sup2, micro, x, /)
	{ 0x0391, 0x03C9 },   // Greek
	{ 0x2010, 0x2027 },   // Dashes, curly quotes, bullet, ellipsis
	{ 0x2030, 0x2030 },   // Per mille
	{ 0x2190, 0x2193 },   // Arrows
	{ 0x20AC, 0x20AC },   // Euro
	{ 0x2212, 0x2212 },   // Minus sign
	{ 0x00B7, 0x00B7 },   // Middle dot
};
const std::size_t kDefaultGlyphRangeCount = sizeof(kDefaultGlyphRanges) / sizeof(kDefaultGlyphRanges[0]);

namespace {
	constexpr int kWhiteBlockRows = 4;

	std::uint64_t pairKey(std::uint32_t a, std::uint32_t b) {
		return (static_cast<std::uint64_t>(a) << 32) | b;
	}
}


// ---- GPOS pair kerning ------------------------------------------------------------
// stb_truetype's own GPOS reader ignores Extension lookups (type 9), which is exactly how
// Inter (and most fonts built with fonttools/makeotf) store their kerning, so it reports
// zero for every pair. This is a small bounds-checked reader for the one thing needed:
// the x-advance adjustment of 'kern' feature PairPos lookups (formats 1 and 2, directly or
// through an Extension). Reading nothing is always safe -- every access that would leave
// the table returns "no kerning".
namespace {
	struct Reader {
		const std::uint8_t* data;
		std::size_t size;
		bool ok(std::size_t off, std::size_t len) const { return off <= size && len <= size - off; }
		std::uint32_t u16(std::size_t off) const { return ok(off, 2) ? (static_cast<std::uint32_t>(data[off]) << 8 | data[off + 1]) : 0u; }
		std::int32_t s16(std::size_t off) const { return static_cast<std::int16_t>(u16(off)); }
		std::uint32_t u32(std::size_t off) const { return ok(off, 4) ? (u16(off) << 16 | u16(off + 2)) : 0u; }
	};

	// Coverage index of `glyph`, or -1.
	int coverageIndex(const Reader& r, std::size_t cov, std::uint32_t glyph) {
		std::uint32_t format = r.u16(cov);
		if (format == 1) {
			std::uint32_t count = r.u16(cov + 2);
			std::uint32_t lo = 0, hi = count;
			while (lo < hi) {
				std::uint32_t mid = (lo + hi) / 2;
				std::uint32_t g = r.u16(cov + 4 + 2 * mid);
				if (g == glyph) return static_cast<int>(mid);
				if (g < glyph) lo = mid + 1; else hi = mid;
			}
		} else if (format == 2) {
			std::uint32_t count = r.u16(cov + 2);
			for (std::uint32_t i = 0; i < count; ++i) {
				std::size_t rec = cov + 4 + 6 * static_cast<std::size_t>(i);
				if (glyph >= r.u16(rec) && glyph <= r.u16(rec + 2)) {
					return static_cast<int>(r.u16(rec + 4) + (glyph - r.u16(rec)));
				}
			}
		}
		return -1;
	}

	std::uint32_t classOf(const Reader& r, std::size_t def, std::uint32_t glyph) {
		std::uint32_t format = r.u16(def);
		if (format == 1) {
			std::uint32_t start = r.u16(def + 2), count = r.u16(def + 4);
			if (glyph >= start && glyph < start + count) return r.u16(def + 6 + 2 * static_cast<std::size_t>(glyph - start));
		} else if (format == 2) {
			std::uint32_t count = r.u16(def + 2);
			for (std::uint32_t i = 0; i < count; ++i) {
				std::size_t rec = def + 4 + 6 * static_cast<std::size_t>(i);
				if (glyph >= r.u16(rec) && glyph <= r.u16(rec + 2)) return r.u16(rec + 4);
			}
		}
		return 0;   // class 0: everything not listed
	}

	int bitCount(std::uint32_t v) { int n = 0; for (; v; v &= v - 1) ++n; return n; }

	// Reads the XAdvance of the first value record (if the format has one).
	int xAdvanceOf(const Reader& r, std::size_t rec, std::uint32_t valueFormat) {
		if (!(valueFormat & 0x0004)) return 0;
		return r.s16(rec + 2 * static_cast<std::size_t>(bitCount(valueFormat & 0x0003)));
	}

	// Result: found = this subtable decided the pair (even if the adjustment is 0).
	bool pairPosSubtable(const Reader& r, std::size_t st, std::uint32_t g1, std::uint32_t g2, int& out) {
		std::uint32_t format = r.u16(st);
		int cov = coverageIndex(r, st + r.u16(st + 2), g1);
		if (cov < 0) return false;
		std::uint32_t vf1 = r.u16(st + 4), vf2 = r.u16(st + 6);
		std::size_t size1 = 2 * static_cast<std::size_t>(bitCount(vf1));
		std::size_t size2 = 2 * static_cast<std::size_t>(bitCount(vf2));
		if (format == 1) {
			std::uint32_t pairSets = r.u16(st + 8);
			if (static_cast<std::uint32_t>(cov) >= pairSets) return false;
			std::size_t set = st + r.u16(st + 10 + 2 * static_cast<std::size_t>(cov));
			std::uint32_t count = r.u16(set);
			std::size_t recSize = 2 + size1 + size2;
			for (std::uint32_t i = 0; i < count; ++i) {
				std::size_t rec = set + 2 + recSize * i;
				if (r.u16(rec) == g2) {
					out = xAdvanceOf(r, rec + 2, vf1);
					return true;
				}
			}
			return false;   // glyph covered but pair absent: let a later subtable try
		}
		if (format == 2) {
			std::size_t classDef1 = st + r.u16(st + 8), classDef2 = st + r.u16(st + 10);
			std::uint32_t c1Count = r.u16(st + 12), c2Count = r.u16(st + 14);
			std::uint32_t c1 = classOf(r, classDef1, g1), c2 = classOf(r, classDef2, g2);
			if (c1 >= c1Count || c2 >= c2Count) return false;
			std::size_t recSize = size1 + size2;
			std::size_t rec = st + 16 + recSize * (static_cast<std::size_t>(c1) * c2Count + c2);
			out = xAdvanceOf(r, rec, vf1);
			return true;
		}
		return false;
	}

	// Lookup indices referenced by the 'kern' feature (any script/language -- UI text is
	// not script-shaped here, and 'kern' lookups are shared across them in practice).
	std::vector<std::uint16_t> gposKernLookups(const Reader& r, std::size_t gpos) {
		std::vector<std::uint16_t> lookups;
		std::size_t featureList = gpos + r.u16(gpos + 6);
		std::uint32_t featureCount = r.u16(featureList);
		for (std::uint32_t i = 0; i < featureCount; ++i) {
			std::size_t rec = featureList + 2 + 6 * static_cast<std::size_t>(i);
			if (!r.ok(rec, 6)) break;
			if (std::memcmp(r.data + rec, "kern", 4) != 0) continue;
			std::size_t feature = featureList + r.u16(rec + 4);
			std::uint32_t n = r.u16(feature + 2);
			for (std::uint32_t k = 0; k < n; ++k) {
				auto idx = static_cast<std::uint16_t>(r.u16(feature + 4 + 2 * static_cast<std::size_t>(k)));
				if (std::find(lookups.begin(), lookups.end(), idx) == lookups.end()) lookups.push_back(idx);
			}
		}
		return lookups;
	}

	// Sum of the 'kern' PairPos x-advance adjustments for (g1, g2), in font units.
	int gposPairAdvance(const Reader& r, std::size_t gpos, const std::vector<std::uint16_t>& kernLookups,
	                    std::uint32_t g1, std::uint32_t g2) {
		std::size_t lookupList = gpos + r.u16(gpos + 8);
		std::uint32_t lookupCount = r.u16(lookupList);
		int total = 0;
		for (std::uint16_t index : kernLookups) {
			if (index >= lookupCount) continue;
			std::size_t lookup = lookupList + r.u16(lookupList + 2 + 2 * static_cast<std::size_t>(index));
			std::uint32_t type = r.u16(lookup);
			std::uint32_t subCount = r.u16(lookup + 4);
			for (std::uint32_t s = 0; s < subCount; ++s) {
				std::size_t st = lookup + r.u16(lookup + 6 + 2 * static_cast<std::size_t>(s));
				std::uint32_t t = type;
				if (type == 9) {   // Extension: format, real type, 32-bit offset to the real subtable
					t = r.u16(st + 2);
					st += r.u32(st + 4);
				}
				int adj = 0;
				if (t == 2 && pairPosSubtable(r, st, g1, g2, adj)) {
					total += adj;
					break;   // first subtable that decides the pair wins within a lookup
				}
			}
		}
		return total;
	}
}

// All stb_truetype state lives here so Font.h stays free of stb types.
struct Font::Impl {
	struct Face {
		std::vector<std::uint8_t> data;
		stbtt_fontinfo info{};
		float scale = 0.0f;   // stbtt_ScaleForPixelHeight at the current bake size
		bool hasGposKern = false;                 // GPOS 'kern' feature present
		std::vector<std::uint16_t> gposKernLookups;
	};

	// faces[0] is the primary face of the current bake; the rest are fallbackData, parsed.
	std::vector<Face> faces;
	std::vector<std::vector<std::uint8_t>> fallbackData;

	stbtt_pack_context pack{};
	bool packActive = false;   // Bitmap mode: skyline packer kept alive for dynamic glyphs

	// Sdf mode: simple shelf packer (stb's SDF API produces standalone bitmaps).
	int shelfX = 0, shelfY = 0, shelfH = 0;

	std::unordered_map<std::uint32_t, int> faceCache;                 // codepoint -> face or -1
	std::unordered_map<std::uint64_t, float> kernCache;               // pair -> px at bake size
	std::unordered_set<std::uint32_t> unbakeable;                     // atlas full / failed
	bool warnedAtlasFull = false;

	~Impl() { endPack(); }
	void endPack() {
		if (packActive) {
			stbtt_PackEnd(&pack);
			packActive = false;
		}
	}
};

Font::Font() : m_impl(std::make_unique<Impl>()) {
	m_flatLookup.fill(kInvalidGlyphSlot);
}

Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

void Font::addFallbackFace(std::vector<std::uint8_t> ttfData) {
	m_impl->fallbackData.push_back(std::move(ttfData));
}
void Font::clearFallbackFaces() { m_impl->fallbackData.clear(); }
std::size_t Font::fallbackFaceCount() const { return m_impl->fallbackData.size(); }

std::vector<std::uint8_t> Font::readFile(const std::string& path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		throw std::runtime_error("Font::readFile: could not open font file: " + path);
	}
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
	if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
		throw std::runtime_error("Font::readFile: could not read font file: " + path);
	}
	return data;
}

namespace {
	// std::getenv is a hard MSVC error under /WX (C4996); same _dupenv_s/getenv
	// split as GuiContext.cpp.
	std::string getEnvironmentVariable(const char* name) {
#if defined(_WIN32)
		char* value = nullptr;
		std::size_t length = 0;
		if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
			return {};
		}
		std::string result(value);
		free(value);
		return result;
#else
		const char* value = std::getenv(name);
		return value ? std::string(value) : std::string();
#endif
	}
}

std::string Font::findSystemFont(const std::string& fileName) {
	namespace fs = std::filesystem;
	std::vector<fs::path> roots;
	if (const std::string home = getEnvironmentVariable("HOME"); !home.empty()) {
		roots.push_back(fs::path(home) / ".fonts");
		roots.push_back(fs::path(home) / ".local/share/fonts");
		roots.push_back(fs::path(home) / "Library/Fonts");
	}
	roots.push_back("/usr/share/fonts");
	roots.push_back("/usr/local/share/fonts");
	roots.push_back("/Library/Fonts");
	roots.push_back("/System/Library/Fonts");
	if (const std::string winDir = getEnvironmentVariable("WINDIR"); !winDir.empty()) {
		roots.push_back(fs::path(winDir) / "Fonts");
	}
	for (const fs::path& root : roots) {
		std::error_code ec;
		if (!fs::is_directory(root, ec)) continue;
		fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
		for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
			if (it->path().filename() == fileName) {
				return it->path().string();
			}
		}
	}
	return {};
}

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

void Font::bakeFromFile(const std::string& path, float pixelHeight, int atlasWidth,
                         int atlasHeight, float contentScale) {
	bake(readFile(path), pixelHeight, atlasWidth, atlasHeight, contentScale);
}

int Font::faceFor(std::uint32_t codepoint) const {
	auto it = m_impl->faceCache.find(codepoint);
	if (it != m_impl->faceCache.end()) return it->second;
	int found = -1;
	for (std::size_t i = 0; i < m_impl->faces.size(); ++i) {
		if (stbtt_FindGlyphIndex(&m_impl->faces[i].info, static_cast<int>(codepoint)) != 0) {
			found = static_cast<int>(i);
			break;
		}
	}
	m_impl->faceCache.emplace(codepoint, found);
	return found;
}

bool Font::canRender(std::uint32_t codepoint) const {
	return m_baked && faceFor(codepoint) >= 0;
}

void Font::bakeInternal(const std::vector<std::uint8_t>& ttfData, float pixelHeight,
                         const GlyphRange* ranges, std::size_t rangeCount,
                         int atlasWidth, int atlasHeight) {
	// ---- parse faces (primary + fallbacks) into locals; nothing is committed until the
	// atlas packed successfully, so a throw / retry leaves the previous bake intact.
	std::vector<Impl::Face> faces;
	faces.reserve(1 + m_impl->fallbackData.size());
	auto addFace = [&](const std::vector<std::uint8_t>& data, bool primary) {
		Impl::Face face;
		face.data = data;
		int offset = stbtt_GetFontOffsetForIndex(face.data.data(), 0);
		if (offset < 0 || !stbtt_InitFont(&face.info, face.data.data(), offset)) {
			if (primary) {
				throw std::runtime_error("Font::bake: could not parse TrueType font data");
			}
			throw std::runtime_error("Font::bake: could not parse a fallback face");
		}
		face.scale = stbtt_ScaleForPixelHeight(&face.info, pixelHeight);
		if (face.info.gpos) {
			Reader r{ face.data.data(), face.data.size() };
			face.gposKernLookups = gposKernLookups(r, static_cast<std::size_t>(face.info.gpos));
			face.hasGposKern = !face.gposKernLookups.empty();
		}
		faces.push_back(std::move(face));
	};
	addFace(ttfData, true);
	for (const auto& fb : m_impl->fallbackData) addFace(fb, false);
	// NOTE: stbtt_fontinfo::data points into Face::data's heap buffer, which survives the
	// Face being moved into the vector (and later into m_impl->faces).

	const bool sdf = m_options.mode == FontRenderMode::Sdf;
	const int oversample = std::clamp(m_options.oversample, 1, 4);
	int sdfPadding = 0;
	if (sdf) {
		sdfPadding = m_options.sdfPadding > 0
			? m_options.sdfPadding
			: std::max(3, static_cast<int>(std::lround(pixelHeight / 8.0f)));
	}

	// ---- gather the codepoints to pre-bake: only ones some face really has. (stb's range
	// API would silently substitute .notdef for the rest, which made hasGlyph() lie.)
	std::vector<std::vector<std::uint32_t>> perFace(faces.size());
	std::size_t totalGlyphs = 0;
	{
		std::unordered_set<std::uint32_t> seen;
		for (std::size_t r = 0; r < rangeCount; ++r) {
			for (std::uint32_t cp = ranges[r].first; cp <= ranges[r].last; ++cp) {
				if (!seen.insert(cp).second) continue;
				for (std::size_t f = 0; f < faces.size(); ++f) {
					if (stbtt_FindGlyphIndex(&faces[f].info, static_cast<int>(cp)) != 0) {
						perFace[f].push_back(cp);
						++totalGlyphs;
						break;
					}
				}
			}
		}
	}

	std::vector<std::uint8_t> pixels(static_cast<std::size_t>(atlasWidth) * static_cast<std::size_t>(atlasHeight), 0);
	const int packedHeight = atlasHeight - kWhiteBlockRows;

	struct Placed { std::uint32_t cp; Glyph g; };
	std::vector<Placed> placed;
	placed.reserve(totalGlyphs);

	stbtt_pack_context packContext{};
	bool packOpen = false;
	bool ok = true;
	int shelfX = 0, shelfY = 0, shelfH = 0;

	auto uvOf = [&](float x, float y) {
		return Vec2{ x / static_cast<float>(atlasWidth), y / static_cast<float>(atlasHeight) };
	};

	if (!sdf) {
		if (!stbtt_PackBegin(&packContext, pixels.data(), atlasWidth, packedHeight, atlasWidth, 1, nullptr)) {
			throw std::runtime_error("Font::bake: stbtt_PackBegin failed");
		}
		packOpen = true;
		stbtt_PackSetOversampling(&packContext, static_cast<unsigned>(oversample), static_cast<unsigned>(oversample));

		for (std::size_t f = 0; f < faces.size() && ok; ++f) {
			if (perFace[f].empty()) continue;
			std::vector<int> cps(perFace[f].begin(), perFace[f].end());
			std::vector<stbtt_packedchar> chars(cps.size());
			stbtt_pack_range pr{};
			pr.font_size = pixelHeight;
			pr.array_of_unicode_codepoints = cps.data();
			pr.num_chars = static_cast<int>(cps.size());
			pr.chardata_for_range = chars.data();
			// Every codepoint was verified present, so stb's cross-range `missing_glyph`
			// bookkeeping (see docs/gui/03-text-and-fonts.md) can never fire here.
			if (!stbtt_PackFontRanges(&packContext, faces[f].data.data(), 0, &pr, 1)) {
				ok = false;
				break;
			}
			for (std::size_t i = 0; i < cps.size(); ++i) {
				const stbtt_packedchar& pc = chars[i];
				Glyph g;
				g.uv0 = uvOf(pc.x0, pc.y0);
				g.uv1 = uvOf(pc.x1, pc.y1);
				g.offset = { pc.xoff, pc.yoff };
				// Display size is xoff2-xoff, NOT the atlas rect (x1-x0): with oversampling
				// the atlas rect is `oversample` times the display size.
				g.size = { pc.xoff2 - pc.xoff, pc.yoff2 - pc.yoff };
				g.xAdvance = pc.xadvance;
				placed.push_back({ static_cast<std::uint32_t>(cps[i]), g });
			}
		}
	} else {
		for (std::size_t f = 0; f < faces.size() && ok; ++f) {
			for (std::uint32_t cp : perFace[f]) {
				Impl::Face& face = faces[f];
				int gi = stbtt_FindGlyphIndex(&face.info, static_cast<int>(cp));
				int w = 0, h = 0, xo = 0, yo = 0;
				unsigned char* bmp = stbtt_GetGlyphSDF(&face.info, face.scale, gi, sdfPadding, 128,
				                                       128.0f / static_cast<float>(sdfPadding), &w, &h, &xo, &yo);
				int adv = 0, lsb = 0;
				stbtt_GetGlyphHMetrics(&face.info, gi, &adv, &lsb);
				Glyph g;
				g.xAdvance = static_cast<float>(adv) * face.scale;
				if (bmp && w > 0 && h > 0) {
					if (shelfX + w + 1 > atlasWidth) { shelfX = 0; shelfY += shelfH + 1; shelfH = 0; }
					if (shelfY + h > packedHeight) { stbtt_FreeSDF(bmp, nullptr); ok = false; break; }
					for (int row = 0; row < h; ++row) {
						std::copy(bmp + static_cast<std::size_t>(row) * static_cast<std::size_t>(w),
						          bmp + static_cast<std::size_t>(row + 1) * static_cast<std::size_t>(w),
						          pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(shelfY + row) * static_cast<std::size_t>(atlasWidth) + static_cast<std::size_t>(shelfX)));
					}
					g.uv0 = uvOf(static_cast<float>(shelfX), static_cast<float>(shelfY));
					g.uv1 = uvOf(static_cast<float>(shelfX + w), static_cast<float>(shelfY + h));
					g.offset = { static_cast<float>(xo), static_cast<float>(yo) };
					g.size = { static_cast<float>(w), static_cast<float>(h) };
					shelfX += w + 1;
					shelfH = std::max(shelfH, h);
				}
				if (bmp) stbtt_FreeSDF(bmp, nullptr);
				placed.push_back({ cp, g });
			}
		}
	}

	if (!ok) {
		if (packOpen) stbtt_PackEnd(&packContext);
		if (atlasWidth == 1024 && atlasHeight == 1024) {
			throw std::runtime_error(
				"Font::bake: atlas too small at " + std::to_string(pixelHeight) +
				"px for " + std::to_string(totalGlyphs) + " glyphs, even at 1024x1024");
		}
		bakeInternal(ttfData, pixelHeight, ranges, rangeCount, 1024, 1024);
		return;
	}

	// ---- commit ----
	m_impl->endPack();
	m_impl->faces = std::move(faces);
	if (packOpen) {
		m_impl->pack = packContext;
		m_impl->packActive = true;
	}
	m_impl->shelfX = shelfX; m_impl->shelfY = shelfY; m_impl->shelfH = shelfH;
	m_impl->faceCache.clear();
	m_impl->kernCache.clear();
	m_impl->unbakeable.clear();
	m_impl->warnedAtlasFull = false;

	for (int y = atlasHeight - kWhiteBlockRows; y < atlasHeight; ++y) {
		for (int x = 0; x < atlasWidth; ++x) {
			pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(atlasWidth) + static_cast<std::size_t>(x)] = 0xFF;
		}
	}

	m_glyphs.clear();
	m_flatLookup.fill(kInvalidGlyphSlot);
	m_extendedLookup.clear();
	m_atlasPixels = std::move(pixels);   // heap buffer unchanged: the pack context stays valid
	m_atlasWidth = atlasWidth;
	m_atlasHeight = atlasHeight;
	m_bakedPixelSize = pixelHeight;
	m_sdfPadding = sdfPadding;
	m_baked = true;

	for (const Placed& p : placed) {
		auto slot = static_cast<std::uint32_t>(m_glyphs.size());
		m_glyphs.push_back(p.g);
		if (p.cp < kFlatLookupSize) m_flatLookup[p.cp] = slot;
		else m_extendedLookup[p.cp] = slot;
	}

	m_whitePixelUV = { 2.0f / static_cast<float>(atlasWidth), (static_cast<float>(atlasHeight) - 2.0f) / static_cast<float>(atlasHeight) };

	// Prefer U+FFFD as the fallback glyph if some face has it, else '?', else whatever
	// baked first -- but never leave a codepoint silently invisible. Baked even when
	// dynamicGlyphs is off, since it is the one glyph that must always exist.
	m_fallbackGlyphSlot = kInvalidGlyphSlot;
	for (std::uint32_t cp : { 0xFFFDu, static_cast<std::uint32_t>('?') }) {
		std::uint32_t slot = slotFor(cp, true);
		if (slot != kInvalidGlyphSlot) { m_fallbackGlyphSlot = slot; break; }
	}
	if (m_fallbackGlyphSlot == kInvalidGlyphSlot) {
		if (m_glyphs.empty()) m_glyphs.push_back(Glyph{});
		m_fallbackGlyphSlot = 0;
	}

	// Widest ASCII digit advance, for TextFlags::Tabular -- computed once here. 0.0f if no
	// digit is resident, which advanceFor() treats as "tabular padding not available."
	m_tabularDigitAdvanceAtBake = 0.0f;
	for (std::uint32_t digit = '0'; digit <= '9'; ++digit) {
		if (hasGlyph(digit)) {
			m_tabularDigitAdvanceAtBake = std::max(m_tabularDigitAdvanceAtBake, glyphFor(digit).xAdvance);
		}
	}

	int ascentRaw = 0, descentRaw = 0, lineGapRaw = 0;
	stbtt_GetFontVMetrics(&m_impl->faces[0].info, &ascentRaw, &descentRaw, &lineGapRaw);
	float unitsScale = stbtt_ScaleForPixelHeight(&m_impl->faces[0].info, pixelHeight);
	m_ascentAtBake = static_cast<float>(ascentRaw) * unitsScale;
	m_lineHeightAtBake = static_cast<float>(ascentRaw - descentRaw + lineGapRaw) * unitsScale;

	++m_revision;
}

bool Font::bakeOneGlyph(std::uint32_t codepoint, int faceIndex) const {
	Impl& impl = *m_impl;
	Impl::Face& face = impl.faces[static_cast<std::size_t>(faceIndex)];
	const float atlasW = static_cast<float>(m_atlasWidth);
	const float atlasH = static_cast<float>(m_atlasHeight);
	Glyph g;

	if (m_options.mode == FontRenderMode::Bitmap) {
		if (!impl.packActive) return false;
		int icp = static_cast<int>(codepoint);
		stbtt_packedchar pc{};
		stbtt_pack_range pr{};
		pr.font_size = m_bakedPixelSize;
		pr.array_of_unicode_codepoints = &icp;
		pr.num_chars = 1;
		pr.chardata_for_range = &pc;
		if (!stbtt_PackFontRanges(&impl.pack, face.data.data(), 0, &pr, 1)) return false;
		g.uv0 = { pc.x0 / atlasW, pc.y0 / atlasH };
		g.uv1 = { pc.x1 / atlasW, pc.y1 / atlasH };
		g.offset = { pc.xoff, pc.yoff };
		g.size = { pc.xoff2 - pc.xoff, pc.yoff2 - pc.yoff };
		g.xAdvance = pc.xadvance;
	} else {
		int gi = stbtt_FindGlyphIndex(&face.info, static_cast<int>(codepoint));
		int w = 0, h = 0, xo = 0, yo = 0;
		unsigned char* bmp = stbtt_GetGlyphSDF(&face.info, face.scale, gi, m_sdfPadding, 128,
		                                       128.0f / static_cast<float>(m_sdfPadding), &w, &h, &xo, &yo);
		int adv = 0, lsb = 0;
		stbtt_GetGlyphHMetrics(&face.info, gi, &adv, &lsb);
		g.xAdvance = static_cast<float>(adv) * face.scale;
		if (bmp && w > 0 && h > 0) {
			const int packedHeight = m_atlasHeight - kWhiteBlockRows;
			int sx = impl.shelfX, sy = impl.shelfY, sh = impl.shelfH;
			if (sx + w + 1 > m_atlasWidth) { sx = 0; sy += sh + 1; sh = 0; }
			if (sy + h > packedHeight) { stbtt_FreeSDF(bmp, nullptr); return false; }
			for (int row = 0; row < h; ++row) {
				std::copy(bmp + static_cast<std::size_t>(row) * static_cast<std::size_t>(w),
				          bmp + static_cast<std::size_t>(row + 1) * static_cast<std::size_t>(w),
				          m_atlasPixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(sy + row) * static_cast<std::size_t>(m_atlasWidth) + static_cast<std::size_t>(sx)));
			}
			g.uv0 = { sx / atlasW, sy / atlasH };
			g.uv1 = { (sx + w) / atlasW, (sy + h) / atlasH };
			g.offset = { static_cast<float>(xo), static_cast<float>(yo) };
			g.size = { static_cast<float>(w), static_cast<float>(h) };
			impl.shelfX = sx + w + 1;
			impl.shelfY = sy;
			impl.shelfH = std::max(sh, h);
		}
		if (bmp) stbtt_FreeSDF(bmp, nullptr);
	}

	auto slot = static_cast<std::uint32_t>(m_glyphs.size());
	m_glyphs.push_back(g);
	if (codepoint < kFlatLookupSize) m_flatLookup[codepoint] = slot;
	else m_extendedLookup[codepoint] = slot;
	++m_revision;
	return true;
}

std::uint32_t Font::slotFor(std::uint32_t codepoint, bool allowBake) const {
	std::uint32_t slot = kInvalidGlyphSlot;
	if (codepoint < kFlatLookupSize) {
		slot = m_flatLookup[codepoint];
	} else {
		auto it = m_extendedLookup.find(codepoint);
		if (it != m_extendedLookup.end()) slot = it->second;
	}
	if (slot != kInvalidGlyphSlot || !allowBake || !m_baked) return slot;
	if (m_impl->unbakeable.count(codepoint)) return kInvalidGlyphSlot;

	int face = faceFor(codepoint);
	if (face < 0 || !bakeOneGlyph(codepoint, face)) {
		if (face >= 0 && !m_impl->warnedAtlasFull) {
			m_impl->warnedAtlasFull = true;
			std::cerr << "Font: glyph atlas (" << m_atlasWidth << "x" << m_atlasHeight
			          << ") is full; further new glyphs fall back to the placeholder\n";
		}
		m_impl->unbakeable.insert(codepoint);
		return kInvalidGlyphSlot;
	}
	return codepoint < kFlatLookupSize ? m_flatLookup[codepoint] : m_extendedLookup[codepoint];
}

bool Font::hasGlyph(std::uint32_t codepoint) const {
	return slotFor(codepoint, false) != kInvalidGlyphSlot;
}

const Glyph& Font::glyphFor(std::uint32_t codepoint) const {
	assert(!m_glyphs.empty() && "Font::glyphFor called before a successful bake()");
	std::uint32_t slot = slotFor(codepoint, m_options.dynamicGlyphs);
	if (slot == kInvalidGlyphSlot) slot = m_fallbackGlyphSlot;
	return m_glyphs[slot];
}

float Font::kerning(std::uint32_t left, std::uint32_t right, float pixelSize) const {
	if (!m_options.kerning || !m_baked || m_bakedPixelSize <= 0.0f) return 0.0f;
	std::uint64_t key = pairKey(left, right);
	auto it = m_impl->kernCache.find(key);
	float atBake;
	if (it != m_impl->kernCache.end()) {
		atBake = it->second;
	} else {
		atBake = 0.0f;
		int fl = faceFor(left), fr = faceFor(right);
		if (fl >= 0 && fl == fr) {
			Impl::Face& face = m_impl->faces[static_cast<std::size_t>(fl)];
			int a = stbtt_FindGlyphIndex(&face.info, static_cast<int>(left));
			int b = stbtt_FindGlyphIndex(&face.info, static_cast<int>(right));
			int units;
			if (face.hasGposKern) {
				Reader r{ face.data.data(), face.data.size() };
				units = gposPairAdvance(r, static_cast<std::size_t>(face.info.gpos), face.gposKernLookups,
				                        static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b));
			} else {
				units = stbtt_GetGlyphKernAdvance(&face.info, a, b);   // legacy 'kern' table
			}
			atBake = static_cast<float>(units) * face.scale;
		}
		m_impl->kernCache.emplace(key, atBake);
	}
	return atBake * (pixelSize / m_bakedPixelSize);
}

float Font::kerningFor(std::uint32_t left, std::uint32_t right, float pixelSize, TextFlags flags) const {
	if (left == 0 || hasFlag(flags, TextFlags::NoKerning)) return 0.0f;
	// Tabular exists so a live-updating number never moves what follows it; kerning against a
	// neighbouring digit (on either side) would make that depend on which digit it is.
	if (hasFlag(flags, TextFlags::Tabular) &&
	    ((left >= '0' && left <= '9') || (right >= '0' && right <= '9'))) return 0.0f;
	return kerning(left, right, pixelSize);
}

Vec2 Font::measureText(std::string_view utf8, float pixelSize, TextFlags flags) const {
	float width = 0.0f;
	std::uint32_t prev = 0;
	std::size_t pos = 0;
	while (pos < utf8.size()) {
		std::uint32_t cp = decodeUtf8(utf8, pos);
		width += kerningFor(prev, cp, pixelSize, flags);
		width += advanceFor(cp, pixelSize, flags);
		prev = cp;
	}
	return { width, lineHeight(pixelSize) };
}

float Font::advanceFor(std::uint32_t codepoint, float pixelSize, TextFlags flags) const {
	float scale = (m_bakedPixelSize > 0.0f) ? pixelSize / m_bakedPixelSize : 0.0f;
	if (hasFlag(flags, TextFlags::Tabular) && codepoint >= '0' && codepoint <= '9' &&
	    m_tabularDigitAdvanceAtBake > 0.0f) {
		return m_tabularDigitAdvanceAtBake * scale;
	}
	return glyphFor(codepoint).xAdvance * scale;
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
	float x = 0.0f;
	std::uint32_t prev = 0;
	std::size_t pos = 0;
	// The pen position of the glyph starting at byteIndex includes the kern with the glyph
	// before it, so that pair is added after the loop (the glyph itself is not advanced).
	while (pos < byteIndex && pos < utf8.size()) {
		std::uint32_t cp = decodeUtf8(utf8, pos);
		if (prev != 0) x += kerning(prev, cp, pixelSize);
		x += advanceFor(cp, pixelSize);
		prev = cp;
	}
	if (prev != 0 && pos < utf8.size()) {
		std::size_t next = pos;
		x += kerning(prev, decodeUtf8(utf8, next), pixelSize);
	}
	return x;
}

std::size_t Font::indexAtOffset(std::string_view utf8, float pixelSize, float x) const {
	float pen = 0.0f;
	std::uint32_t prev = 0;
	std::size_t pos = 0;
	while (pos < utf8.size()) {
		std::size_t next = pos;
		std::uint32_t cp = decodeUtf8(utf8, next);
		if (prev != 0) pen += kerning(prev, cp, pixelSize);
		float advance = advanceFor(cp, pixelSize);
		if (x < pen + advance * 0.5f) {
			return pos;
		}
		pen += advance;
		prev = cp;
		pos = next;
	}
	return utf8.size();
}

} // namespace lightGraphics::ui
