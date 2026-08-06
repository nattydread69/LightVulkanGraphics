# 09 — Claude Code prompts

One prompt per phase, in order. Each is written to be pasted verbatim into
`claude` in the repository root.

## How to run these

**Before the first prompt**, put the design docs where Claude Code will find them:

```bash
cd /path/to/LightVulkanGraphics
mkdir -p docs/gui
cp /path/to/downloaded/docs/gui/*.md docs/gui/
git add docs/gui && git commit -m "docs: LVGUI design specification"
```

**Create a `CLAUDE.md` in the repository root** so every session starts with the right
context without you repeating it:

```markdown
# LightVulkanGraphics — working notes for Claude Code

## Project
Lightweight C++17 Vulkan rendering library. LGPL-3.0-or-later. CMake, `find_package`
consumable. Targets scientific visualisation and simulation tools.

## Current work
Implementing LVGUI, a Vulkan-native retained-mode GUI layer. The complete design
specification lives in `docs/gui/`. **Read the relevant document before writing code.**

- `docs/gui/00-overview.md` — decisions D1–D8, non-goals
- `docs/gui/01-architecture.md` — layers, ownership, frame sequence
- `docs/gui/02-rendering.md` — DrawList, pipeline, shaders
- `docs/gui/03-text-and-fonts.md` — font atlas, measurement
- `docs/gui/04-input-and-events.md` — input model, camera hand-off
- `docs/gui/05-widgets.md` — widget behavioural specs
- `docs/gui/06-layout-and-theme.md` — layout, theme tokens
- `docs/gui/07-public-api.md` — normative signatures
- `docs/gui/08-implementation-plan.md` — phases and acceptance criteria

## Hard rules
- Nothing under `include/lightVulkanGraphics/ui/` or `src/ui/` may include a Vulkan or
  GLFW header, except `UiRenderer.*` and `UiPlatformGlfw.*`.
- C++17. No C++20 features.
- Match the existing code style in `src/` — brace placement, naming, member prefix.
- Every public header must compile standalone.
- Build must stay clean under `-DLVG_ENABLE_WARNINGS=ON -DLVG_WARNINGS_AS_ERRORS=ON`.

## Build and test
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DLVG_BUILD_UI=ON
    cmake --build build -j
    ctest --test-dir build --output-on-failure

## Workflow
Work one phase at a time. Do not start the next phase. At the end of a phase, run the
acceptance criteria from `docs/gui/08-implementation-plan.md` and report which pass.
```

Commit that too. Then work through the prompts below, one session each. Start a fresh
session per phase — a clean context window produces better work than a long one.

---

## Phase 0 — Scaffolding

```
Read docs/gui/00-overview.md and docs/gui/08-implementation-plan.md (phase 0 only).

Set up the build scaffolding for the LVGUI layer. Do not implement any GUI logic yet.

1. Add a CMake option LVG_BUILD_UI, default ON, guarded so that OFF produces exactly
   the library that exists today.
2. Create the target LightVulkanGraphicsUI with alias LightVulkanGraphics::UI. It links
   PUBLIC to LightVulkanGraphics and glm, PRIVATE to Vulkan and glfw. C++17.
3. When LVG_BUILD_UI is ON, define LVG_WITH_UI as a PUBLIC compile definition on the
   core library target.
4. Extend the install and export rules so an external consumer doing
   find_package(LightVulkanGraphics) gets both targets. Follow the pattern already in
   cmake/ — read it first and match it rather than inventing a second mechanism.
5. Create include/lightVulkanGraphics/ui/Ui.h as an umbrella header that for now only
   opens and closes namespace lightGraphics::ui and documents the lvgui alias
   convention.
6. Create tests/ui/CMakeLists.txt wired into the existing CTest setup, with one
   placeholder test that asserts true, so the harness is proven before phase 1.
7. Create THIRD_PARTY_NOTICES.md listing stb (already vendored) and reserving a section
   for stb_truetype and the bundled font.

Then verify: configure and build with LVG_BUILD_UI both ON and OFF, and run ctest.
Report the result of each. Do not proceed to phase 1.
```

---

## Phase 1 — Types and DrawList

```
Read docs/gui/02-rendering.md and docs/gui/07-public-api.md (the Types.h section).

Implement phase 1 of docs/gui/08-implementation-plan.md: the primitive types and the
DrawList.

Files:
  include/lightVulkanGraphics/ui/Types.h
  include/lightVulkanGraphics/ui/DrawList.h
  src/ui/DrawList.cpp
  tests/ui/test_drawlist.cpp

Requirements:
- Vec2, Rect, Color, Align exactly as specified in 07-public-api.md. Colour packs to
  0xAABBGGRR to match VK_FORMAT_R8G8B8A8_UNORM byte order.
- UiVertex is 20 bytes: vec2 pos, vec2 uv, uint32 colour. Add a static_assert on
  sizeof.
- 32-bit indices.
- The full primitive API from 02-rendering.md. addText and addTextClipped take a
  const Font& — forward-declare Font and leave those two unimplemented for now with a
  clearly marked TODO; phase 2 fills them in.
- Clip stack: clear() pushes a full-framebuffer base entry. A new DrawCmd opens on any
  clip-rect or texture change. Never underflow on popClipRect.
- All rect coordinates snap to integers via std::round before entering the vertex
  buffer. Put this in one helper so it cannot be forgotten.
- Rounded rects use a static unit-circle table of 16 points per quadrant.
- addPolyline draws a quad per segment with no mitring.
- No Vulkan headers, no GLFW headers, no dynamic allocation per primitive — reserve and
  reuse the vectors across frames.

Write the tests listed under phase 1 acceptance in 08-implementation-plan.md. Use the
test framework already present in tests/ — check what it is first and match it.

Report which acceptance criteria pass. Do not proceed to phase 2.
```

---

## Phase 2 — Font

```
Read docs/gui/03-text-and-fonts.md in full.

Implement phase 2: the font atlas, glyph table, UTF-8 handling, and text measurement.
Also complete DrawList::addText and addTextClipped, which were stubbed in phase 1.

First: download or tell me to supply a font. I want Inter (SIL OFL 1.1) at
assets/fonts/Inter-Regular.ttf with the licence at assets/fonts/License.txt. If you
cannot fetch it, stop and tell me, and I will add the file.

Add external/stb/stb_truetype.h. Define STB_TRUETYPE_IMPLEMENTATION in exactly one
translation unit: src/ui/FontImpl.cpp.

Files:
  include/lightVulkanGraphics/ui/Font.h
  src/ui/Font.cpp
  src/ui/FontImpl.cpp
  src/ui/Utf8.h
  src/ui/Utf8.cpp
  tests/ui/test_font.cpp
  tests/ui/test_utf8.cpp

Requirements from 03-text-and-fonts.md, in particular:
- stbtt_PackBegin/PackSetOversampling(2,2)/PackFontRanges/PackEnd, not BakeFontBitmap.
- The four default glyph ranges including Greek.
- Reserve the bottom 4 rows of the atlas for a solid white block. Bake into the
  reduced-height region, then write 0xFF into those rows. whitePixelUV is the CENTRE of
  a 4x4 block, not a single pixel.
- Two-tier glyph lookup: flat 256-entry array below U+0100, hash map above.
- Missing codepoints return a visible fallback glyph, never a zero-size quad.
- decodeUtf8 emits U+FFFD and advances by at least one byte on malformed input. This
  must be impossible to make loop forever — write a test that feeds it random bytes.
- measureText, lineHeight, ascent, indexAtOffset, offsetAtIndex.
- Font stores both m_bakedPixelSize and m_contentScaleAtBake; metrics are reported in
  logical pixels (divided by content scale). Read the DPI section of
  docs/gui/06-layout-and-theme.md before implementing this — it is the part people get
  backwards.
- No kerning in v1.

Then implement DrawList::addText: one quad per glyph, sampling the atlas, skipping
whitespace quads. addTextClipped adds alignment and ellipsis truncation via binary
search on the byte index.

Write the phase 2 acceptance tests including the indexAtOffset/offsetAtIndex mutual
inverse property test. Report results. Do not proceed to phase 3.
```

---

## Phase 3 — Vulkan backend

```
Read docs/gui/02-rendering.md, especially the pipeline, shader, buffer, and recording
sections. Then read the existing renderer in src/ to find:
  - how the scene render pass or dynamic rendering is set up
  - the sample count
  - how shaders are compiled and located at runtime (SHADER_PATHS.md)
  - how the existing persistently-mapped instance buffers are managed
Match those patterns. Tell me what you found before writing code.

Implement phase 3: the Vulkan backend that plays a DrawList back into a command buffer.

Files:
  src/ui/UiRenderer.h
  src/ui/UiRenderer.cpp
  shaders/ui.vert
  shaders/ui.frag
  (CMake shader rules extended to compile and install these)

Requirements:
- Shaders exactly as given in 02-rendering.md. Compile with the existing glslc step into
  build/spv/ and install alongside the scene shaders.
- One pipeline: triangle list, no cull, no depth test, no depth write, alpha blend with
  the exact factors given in the doc, dynamic viewport and scissor, 16 bytes of vertex
  push constants, one descriptor set with one combined image sampler.
- The pipeline must be created against the same colour format and sample count as the
  scene pass.
- Atlas: R8_UNORM, optimal tiling, uploaded via staging, transitioned to
  SHADER_READ_ONLY_OPTIMAL, LINEAR filtering, CLAMP_TO_EDGE on both axes, no mips.
- Per-frame vertex and index buffers: HOST_VISIBLE | HOST_COHERENT, persistently mapped,
  one set per frame in flight, grown to max(required * 3/2, 4096 * sizeof(UiVertex)).
  Because the frame fence is waited on before recording, growth may destroy and recreate
  directly with no deferred-deletion queue.
- record() as sketched in the doc. clampToFramebuffer must clamp a negative scissor
  origin to zero AND shrink the extent by the same amount. Write that carefully; it is
  the most common source of validation errors here.
- Early-out on an empty draw list and on zero-index commands.
- A rebuildAtlas() path for content-scale changes.

Hook the UI draw into the existing frame at the correct point: after the scene, inside
the same render pass, before vkCmdEndRenderPass.

For verification, add a temporary hard-coded draw list (one red rect, one clipped rect,
the string "Hello") behind a debug flag so I can see it render. Confirm the validation
layers are clean across a resize, including resizing to 1x1.

Report validation-layer status and which acceptance criteria pass. Do not proceed.
```

---

## Phase 4 — Input

```
Read docs/gui/04-input-and-events.md in full. Then read the existing camera and GLFW
callback code in src/ and tell me exactly where the camera consumes mouse and keyboard
input, before writing anything.

Implement phase 4: the input model and platform plumbing.

Files:
  include/lightVulkanGraphics/ui/InputState.h
  include/lightVulkanGraphics/ui/KeyCodes.h
  src/ui/UiPlatformGlfw.h
  src/ui/UiPlatformGlfw.cpp
  tests/ui/test_input.cpp

Requirements:
- InputState exactly as specified, with both level and edge flags, mouseDownPos, and
  mouseDownDuration.
- Our own Key:: enum and Mod:: bitmask. Translation from GLFW happens only in
  UiPlatformGlfw.cpp. InputState.h and KeyCodes.h must not include GLFW.
- Five GLFW callbacks, each of which does its work AND THEN unconditionally calls the
  previously installed callback. Store the previous pointers. Do not conditionally
  suppress the chain.
- Mouse buttons go into a pending queue, not directly into level state, so a
  press-and-release inside one frame is not lost.
- Text input comes only from the char callback, never synthesised from key events.
- Keep the GUI in GLFW's logical coordinate space throughout; do not multiply by the
  framebuffer/window ratio.
- PlatformHooks with clipboard get/set and cursor shape, backed by glfwGetClipboardString,
  glfwSetClipboardString, and glfwSetCursor with the standard cursors.
- beginFrame/endFrame bookkeeping exactly as listed at the end of the doc, including
  clearing the edge flags in endFrame.

Then wire the camera hand-off as described in the doc's final section, including the
"do not interrupt an in-progress camera drag" rule. Since GuiContext does not exist yet,
add a temporary stub that always returns false from wantsMouse/wantsKeyboard so the
guard is in place and testable now.

Tests use the inject* API directly with no window. Report results. Do not proceed.
```

---

## Phase 5 — Context, Panel, first widgets

```
Read docs/gui/01-architecture.md, docs/gui/05-widgets.md (base class, Label, Separator,
Spacer, Button, Panel), docs/gui/06-layout-and-theme.md, and docs/gui/07-public-api.md.

Implement phase 5.

Files:
  include/lightVulkanGraphics/ui/Theme.h            src/ui/Theme.cpp
  include/lightVulkanGraphics/ui/Widget.h           src/ui/Widget.cpp
  include/lightVulkanGraphics/ui/Panel.h            src/ui/Panel.cpp
  include/lightVulkanGraphics/ui/GuiContext.h       src/ui/GuiContext.cpp
  include/lightVulkanGraphics/ui/widgets/Label.h    src/ui/widgets/Label.cpp
  ...Separator, Spacer, Button
  tests/ui/test_layout.cpp
  tests/ui/test_hittest.cpp

Requirements:
- GuiContext with the exact public interface in 07-public-api.md, including the inject*
  family. Replace the phase-4 stub with the real wantsMouse/wantsKeyboard.
- The per-frame sequence in 01-architecture.md, in that exact order. Panels update
  front-to-back and draw back-to-front. Update runs before layout.
- hoveredId / activeId / focusedId resolution including mouse capture: while activeId is
  set, that widget receives the mouse regardless of cursor position.
- Widget::splitRow() shared helper for the label/control column split.
- Panel::add<W>(args...) constructs in place, takes ownership via unique_ptr, returns a
  raw pointer.
- Panel title bar dragging with a 4-logical-pixel drag threshold.
- Z-ordering: clicking any part of a panel brings it to front.
- Button fires on release-inside-after-press-inside, never on press. Focusable; Space
  and Enter activate.
- Label supports alignment, ellipsis truncation, and greedy word wrap. When a panel
  contains a wrapping label, run layout twice.

Tests must construct a GuiContext with a real Font but no Vulkan device and no window,
inject synthetic input, and assert on widget bounds, hit results, and callback firing.
This headless capability is a hard requirement, not a nice-to-have — if the design makes
it impossible, stop and tell me rather than working around it.

Report results. Do not proceed.
```

---

## Phase 6 — Value widgets

```
Read docs/gui/05-widgets.md, sections Checkbox, RadioButton, Slider, DragValue.

Implement phase 6: Checkbox, RadioGroup/RadioButton, SliderT<float|int>, DragValueT.

Pay particular attention to:
- Slider capture: dragging far outside the panel must keep tracking.
- Shift-drag fine adjustment at 0.1x.
- Double-click resets to the construction-time initial value.
- Ctrl-click inline text entry. Do NOT duplicate text editing logic — TextBox does not
  exist yet, so for now leave a clearly marked hook (a virtual or a std::function) that
  phase 7 will fill in, and note it in the code.
- Logarithmic scale, asserting minVal > 0 with a message naming the widget label.
- Stepping: clamp LAST, after rounding to the step, so a range of 0-10 with step 3 can
  never produce 12. Write that specific test.
- Two callbacks: onChange during drag, onCommit once on release.
- bind(T*) reads the target at the start of update() so external mutation is visible,
  and writes on change.
- Checkbox: toggles on press, not release. The whole row including the label is
  clickable. The box itself sits in the CONTROL column, not next to the label — this
  differs from Dear ImGui and is deliberate.
- Tri-state checkbox.

Drawing recipes are given explicitly in the doc; follow them, including snapping the
slider fill edge and handle rect to integer pixels.

Write the phase 6 acceptance tests. Report results. Do not proceed.
```

---

## Phase 7 — TextBox

```
Read docs/gui/05-widgets.md, TextBox section, in full. This is the hardest widget in the
library. Prioritise correctness of the editing state machine over drawing.

Implement TextBox.

Structure the editing operations as individually testable named methods:
  insertText, deleteBackward, deleteForward, moveCaret, moveCaretWord, moveHome, moveEnd,
  selectAll, copy, cut, paste, commit, revert
Each takes explicit arguments and mutates only m_text / m_caret / m_anchor / m_scrollX.
The event handling layer maps input to these calls and does nothing else.

Rules that are easy to get wrong and must be tested:
- Caret and anchor are BYTE indices that must always sit on a codepoint boundary. All
  movement goes through the Utf8 boundary helpers.
- Right-arrow with an active selection and no Shift collapses the caret to the RIGHT
  EDGE of the selection and deselects. It does not move one character further.
- setMaxLength counts codepoints, not bytes.
- Paste strips control characters and newlines.
- Escape reverts to the text captured when the widget gained focus.
- Password mode measures the DISPLAYED bullet string for caret positioning.
- Caret blink phase resets to fully visible on every caret move.
- Horizontal scroll adjustment happens in ONE place, called at the end of every editing
  operation.

Then go back to Slider and DragValue from phase 6 and implement the Ctrl-click inline
text entry hook using a TextBox owned by the slider.

Tests: write a property test that applies a few thousand random operation sequences and
asserts after every single step that the caret and anchor are on valid codepoint
boundaries and that caret <= text.size(). Include multi-byte content (Greek, accented
Latin) in the random strings.

Report results. Do not proceed.
```

---

## Phase 8 — Remaining widgets

```
Read docs/gui/05-widgets.md, sections DropDown, ProgressBar, PlotLine,
CollapsingSection, Row, Vec3Field.

Implement all six.

Notes:
- Factor out a CompositeWidget base that owns children and forwards update, draw,
  layout, and hit-testing. Row, Vec3Field, and CollapsingSection all use it.
- DropDown popup: GuiContext gains a popupOwner. endFrame draws the popup into
  overlayList AFTER all panels, using pushClipRect(rect, false) to replace rather than
  intersect the clip. Hit testing checks the popup before any panel. wantsMouse()
  returns true whenever a popup is open, regardless of cursor position.
- Popup flips above the control when it would run off the bottom; scrolls above 12
  items; closes on selection, Escape, or an outside click.
- PlotLine draws with a single addPolyline over a ring buffer. Auto-scale uses running
  min/max with hysteresis so the plot does not jump on every new extreme.
- CollapsingSection closed reports header-only preferredSize and its children must not
  be hit-testable.

Report results against the phase 8 acceptance criteria. Do not proceed.
```

---

## Phase 9 — Panel polish

```
Read docs/gui/05-widgets.md (Panel section) and docs/gui/06-layout-and-theme.md
(tooltips, theme variants).

Implement phase 9: scrolling, resize grip, anchoring, tooltips, and the light and
high-contrast themes.

Specifics:
- Scrolling: scrollbar in a theme.scrollbarWidth gutter on the right when content
  exceeds the view. Content region insets accordingly. Wheel scrolls 3 * lineHeight.
  Clamp scrollY to [0, contentHeight - viewHeight]. Widgets scrolled out of view must
  not be hit-testable — intersect every hit test with the content clip rect.
- Resize grip: theme.resizeGripSize triangle bottom-right, hit-tested BEFORE widgets,
  sets the ResizeNWSE cursor on hover, enforces minimum size.
- Anchoring: store the offset from the anchored corner; recompute absolute position when
  displaySize changes.
- Clamp the panel rect so at least the title bar remains on screen at all times.
- Tooltips: tracked in GuiContext, not in widgets, so only one can be pending. Timer
  resets whenever hoveredId changes. Drawn into overlayList, flipped near edges.
- Theme::light() and Theme::highContrast().

Report results. Do not proceed.
```

---

## Phase 10 — Integration and release

```
Read docs/gui/07-public-api.md (the usage example) and docs/gui/08-implementation-plan.md
phase 10.

Finish the integration.

1. Add hasGui() and gui() to LightVulkanGraphics, guarded by LVG_WITH_UI. Add
   enableGui and guiCreateInfo to LightVulkanGraphicsCreateInfo.
2. Install rules for assets/fonts/ into ${DATAROOTDIR}/LightVulkanGraphics/fonts/, with
   the same relocatable search order already used for spv/ payloads. Extend
   SHADER_PATHS.md or add FONT_PATHS.md documenting it.
3. Create examples/gui_control_panel_example.cpp implementing EXACTLY the usage example
   in docs/gui/07-public-api.md, adapted to whatever the real simulation stub should be.
   Add it to the LightVulkanGraphicsExamples target.
4. Extend the existing CTest smoke tests to cover the UI target: install to a temp
   prefix, build an external consumer that links LightVulkanGraphics::UI, and repeat
   against a relocated install tree. Verify the font resolves in the relocated case.
5. Update README.md with an LVGUI section, and write docs/gui_usage.md as a
   user-facing guide (not a design doc) covering: creating a panel, each widget, binding
   vs callbacks, theming, and the documented limitations from
   docs/gui/00-overview.md's non-goals list.
6. Update CHANGELOG.md.
7. Update THIRD_PARTY_NOTICES.md with stb_truetype and the bundled font licence.
8. Check the CI workflow covers the UI build on Linux GCC, Linux Clang, and Windows.

Then run the global invariant checks from the end of
docs/gui/08-implementation-plan.md and report on each of the five.
```

---

## Debugging prompts

Keep these for when something goes wrong mid-phase.

**Validation errors**

```
The Vulkan validation layers are reporting: <paste>

Read docs/gui/02-rendering.md. Check in this order: the scissor clamping in
UiRenderer::record (negative origin must clamp to zero AND shrink the extent), the
pipeline's sample count against the scene pass, and the descriptor set binding. Show me
the offending code before changing it.
```

**Text looks wrong**

```
Text is rendering <blurry / offset / with speckles / at the wrong size>.

Read docs/gui/03-text-and-fonts.md and the DPI section of
docs/gui/06-layout-and-theme.md. Check: sampler addressing mode (must be CLAMP_TO_EDGE),
whether the white pixel is the centre of a 4x4 block, the ratio of bake size to render
size, and whether glyph quad sizes are being divided by contentScale before entering the
draw list. Report what you find before changing anything.
```

**Interaction feels wrong**

```
<Describe the behaviour.>

Read docs/gui/04-input-and-events.md. Check in this order: whether the edge flags are
cleared in endFrame, whether activeId capture is being respected during the hit test,
and whether the camera guard is inverted. Write a headless test that reproduces this
before you fix it.
```

**Adding a widget later**

```
Add a <name> widget to LVGUI.

Follow the conventions in docs/gui/05-widgets.md: derive from Widget (or
CompositeWidget if it owns children), implement preferredSize / update / draw, use
splitRow() for the label/control split, support bind() and both onChange and onCommit
where a value is involved, snap drawing coordinates to integers, and pull every colour
and metric from the Theme rather than hard-coding.

Add it to the umbrella header, the usage docs, and write headless tests covering its
interaction rules.
```

## A note on session hygiene

- **One phase per session.** Context degrades; a fresh session reading `CLAUDE.md` and
  the relevant design doc outperforms a session that has been running for four hours.
- **Commit at every phase boundary**, with the acceptance-criteria results in the commit
  message. It gives you a clean point to revert to.
- **When Claude Code proposes deviating from the spec**, that is often correct — it can
  see the actual code and these documents cannot. Update the design doc in the same
  commit so the two never drift apart.
- **Ask it to read before it writes.** Several prompts above start with "read X and tell
  me what you found before writing code". That step is not ceremony; it is where
  mismatches with your existing renderer surface cheaply.
