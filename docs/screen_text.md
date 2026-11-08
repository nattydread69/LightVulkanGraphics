# Screen text

LightVulkanGraphics provides persistent screen text. When the GUI is available it is
drawn from the GUI's real TrueType font atlas (anti-aliased, kerned, full Latin / Greek /
punctuation, plus anything the GUI's fallback faces cover -- see
[Font atlas rendering](#font-atlas-rendering)); otherwise, or with
`useFontAtlas = false`, it falls back to the blocky ASCII `stb_easy_font` stroke font.
Positions are framebuffer pixels measured from the top-left.
`VkApp::getSwapchainExtent()` returns the size of that pixel space (on HiDPI
displays it is larger than the window size in screen coordinates), so layouts
such as right-aligned or centred labels can be computed from it.
Text is rendered in the overlay layer with alpha blending enabled and depth
testing disabled.

Create a label once:

```cpp
lightGraphics::ScreenTextDescription label;
label.text = "Kamae reached - press K to continue";
label.positionPixels = {24.0f, 24.0f};
label.scale = 2.0f;
label.color = {1.0f, 0.95f, 0.65f, 1.0f};
label.maximumCharacters = 128;

lightGraphics::ScreenTextHandle labelHandle =
	app.createScreenText(label);
```

Update only its contents:

```cpp
app.updateScreenText(labelHandle, "Rear foot sliding");
```

Update its complete style or position by changing the description and passing
it to the other overload:

```cpp
label.positionPixels = {24.0f, 64.0f};
label.color = {0.7f, 1.0f, 0.8f, 1.0f};
app.updateScreenText(labelHandle, label);
```

Visibility and lifetime are explicit:

```cpp
app.setScreenTextVisible(labelHandle, false);
app.setScreenTextVisible(labelHandle, true);
app.destroyScreenText(labelHandle);
```

The text is regenerated automatically after a framebuffer resize. Newline is
supported. With the stroke font, characters outside printable ASCII are replaced
with `?`; the atlas font takes UTF-8. Updating beyond `maximumCharacters` throws
`std::length_error`.

## Font atlas rendering

`ScreenTextDescription` has a few fields for the atlas path (all ignored by the stroke font):

```cpp
label.useFontAtlas  = true;    // default; false forces the stroke font. Read at creation.
label.monospace     = true;    // use the GUI's monospace face (DejaVu Sans Mono by default)
label.bold          = true;    // use the GUI's bold face, if GuiCreateInfo::boldFontPath was set
label.fontPixelSize = 0.0f;    // glyph height in framebuffer pixels; 0 = the GUI font size
                               // times the content scale, i.e. exactly what the atlas was
                               // baked at (crispest). `scale` multiplies it either way.
```

Notes:

- It needs `LVG_WITH_UI` and an initialised GUI (`VkApp::hasGui()`); there is no extra setup.
  Without one the label silently uses the stroke font.
- It is drawn by the UI renderer in the same render pass, just *under* the GUI panels.
  `sortKey` orders atlas labels among themselves but not against scene overlay meshes.
- `scale` now scales a 14 px-ish proportional font rather than the 8 px stroke grid, so
  `scale = 1` text is a little larger than before. Set `useFontAtlas = false` to keep the
  old look.
- Glyphs outside the pre-baked ranges are baked on first use, so labels can show symbols,
  Cyrillic, etc. as long as `GuiCreateInfo::fallbackFontPaths` (or the primary font) cover them.

## Drop shadow

Every label draws a dark copy of its glyphs offset behind the real text
(`shadowEnabled = true` by default) so it stays legible over bright or busy
backgrounds -- a flat color underneath is the cheapest way to guarantee
contrast. Tune or disable it per label:

```cpp
label.shadowColor = {0.0f, 0.0f, 0.0f, 0.75f};
label.shadowOffsetPixels = {1.5f, 1.5f};   // right + down, in pixels
label.shadowEnabled = false;               // opt out entirely
```

With the stroke font, mesh capacity is always reserved for the shadow pass,
even when it's disabled, so flipping `shadowEnabled` on a live label never
requires recreating its mesh.
