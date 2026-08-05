# Screen text

LightVulkanGraphics provides persistent ASCII screen text using
`stb_easy_font`. Positions are framebuffer pixels measured from the top-left.
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

The text mesh is regenerated automatically after a framebuffer resize.
Characters outside printable ASCII are replaced with `?`; newline is
supported. Updating beyond `maximumCharacters` throws `std::length_error`.

## Drop shadow

Every label draws a dark copy of its glyphs offset behind the real text
(`shadowEnabled = true` by default) so it stays legible over bright or busy
backgrounds -- there's no texture-atlas font here, just geometry, so a flat
color underneath is the cheapest way to guarantee contrast. Tune or disable
it per label:

```cpp
label.shadowColor = {0.0f, 0.0f, 0.0f, 0.75f};
label.shadowOffsetPixels = {1.5f, 1.5f};   // right + down, in pixels
label.shadowEnabled = false;               // opt out entirely
```

Mesh capacity is always reserved for the shadow pass, even when it's
disabled, so flipping `shadowEnabled` on a live label never requires
recreating its mesh.
