# 03 — Text and fonts

Text is the part of a GUI that is easy to get 80% right and hard to get 100% right. This
document specifies the 80% precisely and names the remaining 20% as out of scope.

## Dependency

Add `stb_truetype.h` to the existing `external/stb/` directory. It is a single header,
public domain / MIT dual-licensed, ~5000 lines, no build system changes required beyond
one translation unit defining `STB_TRUETYPE_IMPLEMENTATION`.

Put that definition in exactly one place: `src/ui/FontImpl.cpp`. Defining it in a header
that gets included twice produces duplicate-symbol link errors that are tedious to trace.

## What gets baked

```cpp
struct GlyphRange { uint32_t first; uint32_t last; };   // inclusive codepoints

// Default ranges. Total ~250 glyphs — comfortable in a 512×512 R8 atlas at 16px.
static constexpr GlyphRange kDefaultRanges[] = {
    { 0x0020, 0x00FF },   // Basic Latin + Latin-1 Supplement (°, ±, ², µ, ×, ÷)
    { 0x0391, 0x03C9 },   // Greek — α β γ δ θ λ μ π σ φ ω Ω Δ Σ
    { 0x2190, 0x2193 },   // Arrows ← ↑ → ↓
    { 0x00B7, 0x00B7 },   // Middle dot
};
```

The Greek range is not decoration. A scientific tool labels a slider `ω` or `Δt`, and a
GUI that renders those as boxes looks unfinished in exactly the context this library
targets.

## Baking

Use `stbtt_PackBegin` / `stbtt_PackSetOversampling` / `stbtt_PackFontRanges` /
`stbtt_PackEnd`, not the older `stbtt_BakeFontBitmap`. The packing API handles multiple
ranges, gives better atlas utilisation, and supports oversampling.

```cpp
bool Font::bake(const std::vector<uint8_t>& ttfData,
                float pixelHeight,
                std::span<const GlyphRange> ranges,
                int atlasWidth = 512, int atlasHeight = 512);
```

Settings:

- **Oversampling 2×2.** Costs 4× the atlas area per glyph but produces noticeably
  cleaner small text, because it allows sub-pixel horizontal positioning without the
  blur of pure bilinear filtering. At 250 glyphs you have the room.
- If `stbtt_PackFontRanges` returns failure, the atlas was too small. Retry at
  1024×1024 once, then fail with a clear error naming the pixel height and glyph count.

### The white pixel

After packing, reserve a solid block so that solid-colour geometry can share the atlas:

```cpp
// stb's packer leaves the bottom-right corner free in practice, but do not rely on it.
// Instead, pack a 4×4 "glyph" first by temporarily reserving a rect, OR simply write
// into the last 4×4 corner and verify no glyph overlaps it.
```

Recommended approach that avoids fighting the packer: bake the ranges into a
`(atlasHeight - 4)`-tall region by passing a reduced height to `stbtt_PackBegin`, then
write `0xFF` into the bottom 4 rows yourself. Store:

```cpp
Vec2 whitePixelUV;   // centre of the solid block, e.g. { 2.0f/W, (H-2.0f)/H }
```

Every non-text primitive uses `whitePixelUV` for all its vertices. Use the *centre* of a
4×4 block, not a 1×1 pixel — with linear filtering and oversampled neighbours, a 1×1
white pixel can pick up a fraction of an adjacent glyph's coverage and make your panel
backgrounds subtly translucent.

## Glyph table

```cpp
struct Glyph {
    Vec2  uv0, uv1;        // atlas coords
    Vec2  offset;          // pen-relative top-left, in pixels at bake size
    Vec2  size;            // quad size in pixels at bake size
    float xAdvance;        // pen advance in pixels at bake size
};
```

Storage: a `std::vector<Glyph>` plus a lookup. For codepoints below 0x0100, index a flat
256-entry array directly — that covers the overwhelming majority of lookups with no
hashing. Above that, an `std::unordered_map<uint32_t, uint16_t>` into the vector. This
two-tier scheme is worth the twenty extra lines; text measurement runs over every
character of every label every frame.

Missing codepoints resolve to a fallback glyph. Use `?` in a box, or U+FFFD if the font
has it. **Never silently skip** — invisible missing characters make layout bugs
impossible to diagnose.

## Scaling

Bake at one size. Render at any size by scaling the quad and advance:

```cpp
float scale = requestedPixelSize / m_bakedPixelSize;
```

Text below about 0.8× or above about 1.5× the baked size looks poor. Bake at the size you
actually use — `theme.fontSize * contentScale` — and treat other sizes as an exceptional
case. If you later want a title font and a body font, bake two `Font` objects into two
atlas regions rather than scaling one aggressively.

## Measurement

```cpp
Vec2  Font::measureText(std::string_view utf8, float pixelSize) const;
float Font::lineHeight(float pixelSize) const;
float Font::ascent(float pixelSize) const;

// Index of the character boundary nearest to a pixel offset — needed for
// click-to-position-caret in TextBox.
size_t Font::indexAtOffset(std::string_view utf8, float pixelSize, float x) const;

// Pixel offset of a byte index — needed for caret and selection rendering.
float  Font::offsetAtIndex(std::string_view utf8, float pixelSize, size_t byteIndex) const;
```

These four functions carry more weight than their size suggests. Layout, alignment,
ellipsis truncation, caret placement, selection highlighting and click-to-position all
call them. They are also pure functions over a font and a string, which makes them the
easiest thing in the whole library to test properly. Do that — see
[10-testing.md](10-testing.md).

`measureText` must not include the trailing advance's right side bearing beyond the last
glyph's ink if you want tight visual alignment; but for layout purposes the accumulated
`xAdvance` is correct and simpler. Use accumulated advance and document it.

## Kerning

`stbtt_GetCodepointKernAdvance` exists. Applying it costs one lookup per character pair.

**Recommendation: skip kerning in v1.** UI text is short, small, and mostly labels and
numbers. Unkerned text at 14px is indistinguishable from kerned text to anyone not
looking for it, and kerning complicates `offsetAtIndex`/`indexAtOffset` (the caret
position becomes context-dependent). Note it as a possible phase-11 addition.

## UTF-8 decoding

A minimal decoder, ~40 lines:

```cpp
// Returns the codepoint and advances `pos`. On malformed input, emits U+FFFD and
// advances by exactly one byte — never zero, or you get an infinite loop.
uint32_t decodeUtf8(std::string_view s, size_t& pos);

size_t utf8PrevBoundary(std::string_view s, size_t pos);  // for caret left / backspace
size_t utf8NextBoundary(std::string_view s, size_t pos);  // for caret right / delete
void   utf8Encode(uint32_t cp, std::string& out);         // for character input
```

The "never advance by zero" rule is not a nicety. A malformed byte in a file a user
loaded will otherwise hang your render thread, and it will happen.

`TextBox` stores `std::string` (UTF-8) and byte indices for caret and selection. All
caret movement goes through the boundary helpers, so a caret can never land mid-sequence.

## Font asset resolution

Reuse the existing shader-payload mechanism exactly. Search order, matching what
`SHADER_PATHS.md` already describes for `spv/`:

1. Explicit `fontPath` in `GuiCreateInfo`
2. `LIGHT_VULKAN_GRAPHICS_FONT_PATH` environment variable
3. Build-tree `assets/fonts/`
4. Paths relative to the loaded shared library
5. Paths relative to the executable, then the current working directory

Install to `${DATAROOTDIR}/LightVulkanGraphics/fonts/`.

If no font is found, the GUI must **fail loudly at init with a message naming every path
it tried**, not fall back to invisible text. A GUI that silently renders nothing is
worse than one that refuses to start.

### Font choice

Pick one and bundle it:

| Font | Licence | Notes |
|------|---------|-------|
| **Inter** | SIL OFL 1.1 | Excellent at small sizes, tabular figures available — recommended |
| DejaVu Sans | Bitstream Vera derivative, permissive | Widest glyph coverage including Greek and maths |
| Roboto Mono | Apache 2.0 | Monospace; good for numeric readouts, poor for labels |

Inter is the recommendation, with DejaVu Sans as the fallback if the Greek coverage in
your chosen Inter build is incomplete. Ship the licence text at
`assets/fonts/License.txt` following the pattern already established by
`assets/License.txt`.

### Tabular figures

If the chosen font supports the `tnum` OpenType feature, `stb_truetype` will not apply
it — stb does not do OpenType feature substitution. If you want digits of equal width in
numeric readouts (and you do, because otherwise a live-updating value jitters
horizontally as digits change), the practical workaround is to measure the widest digit
once at bake time and advance every digit by that amount when a `TextFlags::Tabular` flag
is set. Ten lines, and it removes a visual annoyance that would otherwise irritate every
user of a simulation readout.

## Explicitly out of scope for v1

Write these into the public header docs so expectations are set:

- No CJK, Arabic, Hebrew, Devanagari — no complex shaping, no bidi
- No IME / composition events
- No line breaking or word wrap in `TextBox` (single-line only); `Label` gets simple
  greedy word wrap at spaces, which is enough
- No rich text, no per-run styling within a single string
