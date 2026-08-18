# 00 — Overview

## What we are building

A GUI layer that draws as Vulkan geometry inside the frame that
`LightVulkanGraphics` already renders. Panels float over the 3D scene in the same
window, in the same render pass, in the same swapchain image. No second window, no
second event loop, no second toolkit.

The target user is somebody building a scientific visualisation or simulation tool on
top of your library, who wants a parameter panel beside their scene: sliders for
physical constants, tick boxes for display toggles, text boxes for exact numeric entry,
a live readout of a scalar quantity.

## Goals

1. **Zero external GUI dependency.** `stb_truetype.h` goes into the existing
   `external/stb/` directory. Nothing else is added.
2. **One pipeline, one descriptor set, few draw calls.** A typical frame with four
   panels should cost fewer than a dozen `vkCmdDrawIndexed` calls. Still true for
   everything that isn't an `Image` widget: glyphs and solid fills always share the one
   font-atlas descriptor set. A consumer-registered texture (docs/gui/05-widgets.md,
   "Image") adds one descriptor bind of its own, opt-in per widget that actually uses
   one — the pipeline itself stays singular either way.
3. **Headless-testable above the backend.** Layout, hit-testing, text measurement,
   text-editing state and draw-list geometry must all be testable without a Vulkan
   device — matching the approach already taken for the rotation-glyph mesh API.
4. **Retained mode.** Consumers construct a panel once and attach callbacks. They do
   not re-declare their UI every frame.
5. **Opt-out at build time.** `-DLVG_BUILD_UI=OFF` produces the library exactly as it
   is today.

## Non-goals for v1

Stated explicitly so nobody sinks a week into them by accident:

- **No CJK / complex script shaping, no IME.** Latin-1 plus Greek (scientific notation
  needs `α β θ ω`) is the character set. Document this limit loudly.
- **No bidirectional text.**
- **No accessibility tree / screen reader support.** This is a real gap and should be
  written down as such.
- **No anti-aliased edges in v1.** Rectangles snapped to integer pixels look crisp
  without it. AA is a possible future addition, not currently planned.
- **No docking, tabs, or multi-viewport.** Panels float and can be dragged, collapsed,
  and resized. That's it.
- **No 3D-space-anchored labels in the widget system.** That is a separate, simpler
  feature — see the note at the end of [02-rendering.md](02-rendering.md).

## The eight decisions

Everything in the other documents follows from these. If you disagree with one, change
it here first and let the consequences propagate.

**D1 — Retained mode, not immediate mode.**
Panels and widgets are heap objects with stable identity and callbacks. This matches the
existing API style (`gfx.addObject(...)`, `scene.createNode(...)`) and means a consumer's
render loop stays untouched. The cost is bidirectional state sync, which
[05-widgets.md](05-widgets.md) addresses with an optional bound-pointer mechanism.

**D2 — Immediate-mode *rendering*, retained-mode *widgets*.**
The widget tree is persistent, but it re-emits its geometry into a fresh `DrawList`
every frame. No per-widget vertex buffers, no dirty-rectangle bookkeeping. This is the
combination that keeps the renderer trivial while keeping the API pleasant.

**D3 — Screen-space pixel coordinates throughout, origin top-left.**
Y grows downward. Widgets store and reason in logical pixels; DPI scaling is applied
once, at the projection and font-baking level.

**D4 — One R8 atlas holds both glyph coverage and a white block.**
The fragment shader multiplies vertex colour by the atlas red channel used as alpha.
Solid fills sample the white block and come out as pure vertex colour. One texture, one
descriptor set, one pipeline for everything the library itself draws. Consumer-registered
textures (`Image`, docs/gui/05-widgets.md) are the one deliberate exception — their own
descriptor set, sharing the same pipeline and the same sampler settings.

**D5 — 32-bit indices.**
Dear ImGui's 16-bit default causes an overflow class of bug that you do not need to
inherit. Memory cost is irrelevant at these vertex counts.

**D6 — Scissor rectangles for clipping, never stencil.**
`vkCmdSetScissor` between draws. The draw list splits into commands at every clip-rect
change. Panels clip their content; dropdown popups escape by rendering in a separate
overlay pass at the end of the list.

**D7 — The UI owns input first, and says so.**
`GuiContext::wantsMouse()` and `wantsKeyboard()` are queried by the camera controller
before it consumes anything. This has to be wired into the camera code directly, not
layered on top of it afterward — a free-fly WASD camera will otherwise fly across the
scene every time a user types into a text box.

**D8 — The font ships as an asset through the existing shader-payload mechanism.**
You already have a robust relocatable search path for `spv/` payloads
(`LIGHT_VULKAN_GRAPHICS_SHADER_PATH`, build-tree, library-relative, executable-relative).
The font atlas source TTF uses the identical resolution order under a `fonts/`
sibling directory. No new mechanism, no giant generated byte-array header.

## Naming and namespacing

- Namespace: `lightGraphics::ui`
- Convenience alias for consumers: `namespace lvgui = lightGraphics::ui;`
- Public headers: `include/lightVulkanGraphics/ui/*.h`
- Sources: `src/ui/*.cpp`
- CMake target: `LightVulkanGraphicsUI`, exported as `LightVulkanGraphics::UI`
- Preprocessor guard for the core library's integration points: `LVG_WITH_UI`

## Licensing notes

- `stb_truetype.h` is public domain / MIT dual-licensed. Compatible with your
  LGPL-3.0-or-later. Record it in a `THIRD_PARTY_NOTICES.md`.
- The bundled font must be permissively licensed. **Recommended: Inter or DejaVu Sans
  (SIL OFL 1.1), or Roboto Mono (Apache 2.0).** OFL permits bundling and redistribution
  provided the licence text travels with the font and the font file is not sold on its
  own. Put the licence text in `assets/fonts/License.txt`, exactly as you already do for
  `assets/License.txt` with the Quaternius model.
- Nothing in LVGUI changes the licence position of the parent library.
