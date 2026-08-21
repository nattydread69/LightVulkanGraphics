# 06 — Layout and theme

## The layout model

Deliberately simple. A panel is a **vertical stack** of rows. Each row is one widget,
full content width, at its preferred height. That covers 90% of control panels. Two
escape hatches handle the rest: `Row` for horizontal grouping and `setBoundsOverride` for
absolute placement.

Complex constraint solvers, flexbox emulation, and grid systems are all out of scope.
They are where GUI libraries go to become unmaintainable, and a parameter panel does not
need them.

```
┌─ Panel ────────────────────────────────┐
│ ┌─ title bar (theme.titleBarHeight) ──┐│
│ └─────────────────────────────────────┘│
│  ← windowPadding →                     │
│   ┌────────────────────────────────┐   │
│   │ label column │ control column  │   │  ← row 0
│   ├────────────────────────────────┤   │  ← itemSpacing
│   │ label column │ control column  │   │  ← row 1
│   └────────────────────────────────┘   │
│  ← windowPadding →                     │
└────────────────────────────────────────┘
```

### Layout pass

```cpp
void Panel::layout(const GuiContext& ctx) {
    const Theme& th = ctx.theme();
    float y = m_bounds.y + (hasTitleBar() ? th.titleBarHeight : 0) + th.windowPadding;
    float contentW = m_bounds.w - 2 * th.windowPadding
                   - (needsScrollbar() ? th.scrollbarWidth : 0);
    float x = m_bounds.x + th.windowPadding;

    for (auto& w : m_widgets) {
        if (!w->visible()) continue;
        Vec2 pref = w->preferredSize(ctx);
        Rect r { x, y - m_scrollY, contentW, pref.y };
        w->setBounds(snapToPixels(r));
        y += pref.y + th.itemSpacing;
    }
    m_contentHeight = y - (m_bounds.y + titleOffset) + th.windowPadding;
}
```

Note `- m_scrollY` applied to the row position, and `m_contentHeight` computed in
unscrolled space. Mixing those up produces a scrollbar that fights itself.

`snapToPixels` rounds all four components. Do it once, here, and widgets inherit crisp
edges for free.

### The label / control split

Inside its assigned row rect, a widget computes:

```cpp
float labelW = (m_labelWidthOverride >= 0.0f)
             ? m_labelWidthOverride
             : m_bounds.w * ctx.theme().labelWidthRatio;

Rect labelRect  { m_bounds.x, m_bounds.y, labelW - theme.itemSpacing, m_bounds.h };
Rect controlRect{ m_bounds.x + labelW, m_bounds.y,
                  m_bounds.w - labelW, m_bounds.h };
```

If the label is empty, `labelW = 0` and the control takes the full row.

Put this in a shared helper — `Widget::splitRow(const GuiContext&) const` returning a
`{ Rect label, Rect control }` — rather than repeating it in twelve widgets.

### Row (horizontal grouping)

```cpp
class Row : public Widget {
public:
    Row() = default;
    template <typename W, typename... Args> W* add(Args&&...);
    void setWeights(std::vector<float>);   // relative widths; default all equal
};
```

Divides its assigned rect horizontally by weight, minus `itemSpacing` between children.
Height is the max of children's preferred heights. Used for things like a
`[Reset] [Apply]` button pair, or the three fields of a `Vec3Field`.

### Absolute placement escape hatch

```cpp
void Widget::setBoundsOverride(const Rect& panelRelative);
void Widget::clearBoundsOverride();
```

When set, the layout pass skips this widget and uses the override, offset by the panel
origin. Provided so a user with an unusual requirement is never blocked, and documented
as a last resort.

## DPI and content scale

One rule: **everything in layers 1–3 is in logical pixels.** Scaling is applied in
exactly two places:

1. The font atlas is baked at `theme.fontSize * contentScale`, and `Font` reports its
   metrics divided back down to logical units.
2. The projection push-constants use the physical framebuffer size.

Wait — those two conflict if you are not careful. Resolve it as follows:

- Widget rects, theme metrics, mouse positions: **logical pixels**.
- The vertex positions written into `DrawList`: **logical pixels**.
- `push.scale = 2.0f / logicalSize`, and the Vulkan viewport covers the physical
  framebuffer. The hardware scales for you.
- The font atlas is baked at physical resolution so glyphs are sharp, but every glyph
  quad's *size and advance* is divided by `contentScale` before entering the draw list.

The result: on a 2× display you get 2×-resolution glyphs occupying logical-pixel-sized
quads. That is the whole trick, and it is the reason `Font` must store both
`m_bakedPixelSize` and `m_contentScaleAtBake`.

Rebake the atlas when `contentScale` changes by more than 1%. Signal the renderer to
re-upload the atlas image on the next frame.

## Theme

```cpp
struct Theme {
    // ---- colours ----
    Color windowBg          { 0x1E, 0x21, 0x26, 0xF0 };
    Color titleBg           { 0x15, 0x18, 0x1C, 0xFF };
    Color titleBgActive     { 0x22, 0x27, 0x2E, 0xFF };
    Color border            { 0x3A, 0x41, 0x4B, 0xFF };

    Color text              { 0xE4, 0xE7, 0xEB, 0xFF };
    Color textDisabled      { 0x6B, 0x73, 0x7F, 0xFF };

    Color frameBg           { 0x2A, 0x30, 0x38, 0xFF };
    Color frameBgHovered    { 0x33, 0x3A, 0x44, 0xFF };
    Color frameBgActive     { 0x3B, 0x44, 0x50, 0xFF };

    Color accent            { 0x3D, 0x8B, 0xFD, 0xFF };
    Color accentHovered     { 0x55, 0x9C, 0xFF, 0xFF };
    Color accentActive      { 0x2C, 0x74, 0xDB, 0xFF };

    Color checkMark         { 0xFF, 0xFF, 0xFF, 0xFF };
    Color selectionBg       { 0x3D, 0x8B, 0xFD, 0x66 };
    Color scrollbarBg       { 0x18, 0x1B, 0x20, 0xFF };
    Color scrollbarGrab     { 0x4A, 0x52, 0x5E, 0xFF };
    Color error             { 0xE0, 0x5A, 0x5A, 0xFF };
    Color plotLine          { 0x5A, 0xD0, 0x9A, 0xFF };
    Color modalBackdrop     { 0x00, 0x00, 0x00, 0x99 };  // fullscreen dim behind a Modal panel

    // ---- metrics, all in logical pixels ----
    float fontSize           = 14.0f;
    float rounding           = 3.0f;
    float borderWidth        = 1.0f;
    float windowPadding      = 8.0f;
    float framePadding       = 4.0f;
    float itemSpacing        = 6.0f;
    float titleBarHeight     = 24.0f;
    float rowHeight          = 22.0f;   // default widget height
    float scrollbarWidth     = 10.0f;
    float resizeGripSize     = 14.0f;
    float sliderTrackHeight  = 6.0f;
    float sliderHandleWidth  = 10.0f;
    float colorSquareSize    = 140.0f;  // ColorEdit popup: SV square side length
    float colorStripWidth    = 16.0f;   // ColorEdit popup: hue strip width, alpha strip height
    float labelWidthRatio    = 0.42f;
    float tooltipDelay       = 0.6f;    // seconds

    static Theme dark();
    static Theme light();
    static Theme highContrast();
};
```

`Color` is a small struct over a packed `uint32_t` with `withAlpha(float)` and a
`lerp` helper. Store as RGBA8; the draw list wants it packed anyway.

The defaults above are the `dark()` theme. `light()` inverts the neutrals and darkens
the text; `highContrast()` uses pure black/white with a 2px border everywhere, which is
the closest thing to an accessibility affordance available without a proper
accessibility tree.

### Why these particular values

- `rowHeight` 22 with `fontSize` 14 gives 4px of breathing room above and below the
  cap height — dense enough for a forty-parameter panel, loose enough to read.
- `labelWidthRatio` 0.42 rather than 0.5: labels are usually shorter than the controls
  need to be, and a slider under 100px wide is unusable.
- `windowBg` alpha `0xF0` rather than opaque: a slight translucency lets the user see
  the scene continue behind the panel, which matters when the panel overlaps the thing
  being adjusted. Any lower and the text becomes hard to read against a bright scene.

## Tooltips

Any widget with a non-empty tooltip shows it after `theme.tooltipDelay` seconds of
uninterrupted hover. Drawn into `overlayList`: a `windowBg` rounded rect with a border,
positioned below-right of the cursor, flipped if it would leave the framebuffer.

Reset the hover timer whenever `hoveredId` changes. Track it in `GuiContext`, not in the
widget, so only one tooltip can ever be pending.

## Style guidance for consumers

Worth writing into the public docs, because a library that makes it easy to build an ugly
panel will mostly produce ugly panels:

- Group related parameters with `CollapsingSection` or `Separator` captions
- Put units in `setUnitSuffix`, not in the label — `"Timestep"` with suffix `" s"` reads
  better than `"Timestep (s)"` and keeps the label column narrow
- Prefer `Slider` when a range is physically meaningful, `DragValue` when it is not
- Use `setOnCommit` for anything that triggers expensive work
- Anchor panels to a corner so they survive window resizing
