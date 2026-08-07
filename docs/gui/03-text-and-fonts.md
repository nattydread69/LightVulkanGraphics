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

### Call `stbtt_PackFontRanges` once per range — never batched

This document originally said to pass all ranges to a single `stbtt_PackFontRanges`
call. **Do not.** Batching them corrupts glyph metrics.

`stbtt_PackFontRangesRenderIntoRects` keeps a `missing_glyph` variable holding the
per-character index `j` of the first glyph the font does not have. It is **not reset
between ranges**. When a later range contains a codepoint the font lacks, stb copies
`chardata_for_range[missing_glyph]` into it — but `missing_glyph` may be an index from
an earlier, much longer range, so the read runs off the end of the shorter range's
array.

This is reachable with the default ranges, not theoretical. **U+03A2 is an unassigned
hole in the Greek block**, so no font has it; it sets `missing_glyph = 17` while packing
the 57-entry Greek range. Any missing glyph in the 4-entry arrow range or the 1-entry
middle-dot range that follows then reads index 17 of a 4- or 1-element array. AddressSanitizer
reports it as a heap-use-after-free inside `stbtt_PackFontRangesRenderIntoRects`; without
a sanitizer it shows up as glyphs with nonsense UVs — measured atlas occupancy of
11 950% and 497 138% at 15px and 20px, which is what reading a `stbtt_packedchar` out of
bounds produces.

Packing one range per call keeps the index inside the array it came from:

```cpp
int ok = 1;
for (std::size_t i = 0; i < rangeCount; ++i) {
    if (!stbtt_PackFontRanges(&packContext, ttfData.data(), 0, &packRanges[i], 1)) {
        ok = 0;   // a failure in ANY range must trigger the retry
    }
}
```

`ok` is only ever assigned `0`, so a failure in an early range cannot be masked by a
later success. All calls share one `PackBegin`/`PackEnd` pair and one skyline context,
so this is not N independent atlases.

**Do not "optimise" this back into a single batched call.** The measured cost is nil:
packing one range at a time changes atlas occupancy by less than 0.1 percentage point,
and the 512×512-vs-1024×1024 decision point is unchanged (see the utilisation table
below).

### Atlas size in practice

Measured with the bundled Inter build and the four default ranges (286 codepoints),
requesting 512×512 and letting the retry escalate:

| Bake px | Resulting atlas | Retried? | Occupancy |
|---------|-----------------|----------|-----------|
| 12 | 512×512 | no | 17.6% |
| 14 | 512×512 | no | 22.7% |
| 16 | 512×512 | no | 28.9% |
| 20 | 512×512 | no | 44.1% |
| 24 | 512×512 | no | 60.1% |
| 26 | 1024×1024 | **yes** | 17.6% |
| 28 | 1024×1024 | **yes** | 20.0% |
| 32 | 1024×1024 | **yes** | 25.8% |

So a 14px bake — the default `theme.fontSize` at 1× content scale — sits at 22.7% of a
512×512 atlas with the escalation threshold not arriving until about 25px. The same
`theme.fontSize` at 2× content scale bakes at 28px and legitimately needs 1024×1024,
which is why the renderer must read `font.atlasWidth()`/`atlasHeight()` rather than
assuming 512 (see [02-rendering.md](02-rendering.md)).

```cpp
bool Font::bake(const std::vector<uint8_t>& ttfData,
                float pixelHeight,
                std::span<const GlyphRange> ranges,
                int atlasWidth = 512, int atlasHeight = 512);
```

**Implementation note:** the shipped signature is
`void Font::bake(const std::vector<uint8_t>& ttfData, float pixelHeight,
const GlyphRange* ranges, std::size_t rangeCount, int atlasWidth = 512,
int atlasHeight = 512, float contentScale = 1.0f)`, plus a convenience overload that
defaults `ranges`/`rangeCount` to `kDefaultGlyphRanges`. Two deviations from the
signature above: `std::span` is C++20 and this project is pinned to C++17, so it is a
pointer+count pair instead; and the `bool` return became a thrown `std::runtime_error`,
matching the "construction failures throw" policy in
[07-public-api.md](07-public-api.md) -- there is no meaningful `false` case left once
the one-shot 1024x1024 retry also fails. `contentScale` is recorded on `Font` purely for
the caller's rebake-threshold bookkeeping (see the DPI section below); it does not
change how `bake()` itself behaves.

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
    Vec2  size;            // DISPLAY quad size in pixels at bake size -- see below
    float xAdvance;        // pen advance in pixels at bake size
};
```

**`size` must come from `xoff2 - xoff`, not from the atlas rect `x1 - x0`.** With 2×2
oversampling those two differ by roughly a factor of two: `x1 - x0` is measured in atlas
texels (which is what `uv0`/`uv1` need), whereas the on-screen quad is
`stbtt_packedchar::xoff2 - xoff` by `yoff2 - yoff`, which is where stb has divided the
oversampling back out. This is exactly what `stbtt_GetPackedQuad` uses. Taking the size
from the atlas rect draws every glyph at double width, and the result is not subtly
wrong — characters visibly overlap each other. `tests/ui/test_font.cpp` pins this with
`testGlyphInkFitsItsAdvance`, which asserts a glyph's ink is never much wider than its
own advance.

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
