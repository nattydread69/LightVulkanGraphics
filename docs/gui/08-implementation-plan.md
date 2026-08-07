# 08 — Implementation plan

Eleven phases. Each ends with something you can build, run, and verify. Do not start a
phase before its predecessor's acceptance criteria pass — the later phases assume the
earlier invariants hold, and debugging a text caret on top of a broken clip stack is
miserable.

Rough effort estimates assume you are directing Claude Code rather than typing it
yourself.

| Phase | Deliverable | Effort |
|-------|-------------|--------|
| 0 | Scaffolding, CMake target, empty namespace | 1 session |
| 1 | Types + DrawList, headless tests | 1–2 sessions |
| 2 | Font atlas + measurement, headless tests | 2 sessions |
| 3 | Vulkan backend: pipeline, atlas upload, buffers, recording | 2–3 sessions |
| 4 | InputState, GLFW plumbing, camera hand-off | 1–2 sessions |
| 5 | GuiContext + Panel + Label/Button/Separator/Spacer | 2 sessions |
| 6 | Checkbox, RadioButton, Slider, DragValue | 2 sessions |
| 7 | TextBox | 2–3 sessions |
| 8 | DropDown, ProgressBar, PlotLine, CollapsingSection, Row, Vec3Field | 2 sessions |
| 9 | Panel polish: scrolling, resize, anchoring, tooltips, themes | 1–2 sessions |
| 10 | Integration, example target, install rules, docs | 1–2 sessions |
| 11 | Optional: AA, world labels, kerning, golden-image tests | open-ended |

## Phase 0 — Scaffolding

**Files created**

```
CMakeLists.txt                              (modified)
cmake/LightVulkanGraphicsUIConfig.cmake.in  (or extend the existing config)
include/lightVulkanGraphics/ui/Ui.h
src/ui/.gitkeep
tests/ui/CMakeLists.txt
docs/gui/                                   (these documents)
THIRD_PARTY_NOTICES.md
```

**CMake shape**

```cmake
option(LVG_BUILD_UI "Build the LVGUI immediate-rendering GUI layer" ON)

if(LVG_BUILD_UI)
    add_library(LightVulkanGraphicsUI ${LVGUI_SOURCES})
    add_library(LightVulkanGraphics::UI ALIAS LightVulkanGraphicsUI)
    set_target_properties(LightVulkanGraphicsUI PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(LightVulkanGraphicsUI
        PUBLIC  glm::glm
        PRIVATE Vulkan::Vulkan)
    # Core links UI, never the reverse -- see below.
    target_link_libraries(LightVulkanGraphics PRIVATE LightVulkanGraphicsUI)
    target_compile_definitions(LightVulkanGraphics PUBLIC LVG_WITH_UI)
    target_compile_features(LightVulkanGraphicsUI PUBLIC cxx_std_17)
endif()
```

**The dependency runs core → UI, not UI → core.** The shape originally sketched here
had `LightVulkanGraphicsUI` link `LightVulkanGraphics` PUBLIC, which cannot work: core
has to call into the UI to record it into the frame (phase 3) and to expose `gfx.gui()`
(phase 10), and CMake rejects a target cycle unless every target in it is `STATIC` —
core is `SHARED`. It is also unnecessary, because layers 1–2 (`Types`, `DrawList`,
`Font`, `Utf8`) are pure glm+std and `UiRenderer` receives its Vulkan handles through
`UiRendererCreateInfo` rather than reaching into `VkApp`. Because the static UI library
is linked into a shared library, it needs `POSITION_INDEPENDENT_CODE`. A useful side
effect of the inversion: the headless UI tests link neither Vulkan, assimp nor glfw.

Consumers therefore link `LightVulkanGraphics::LightVulkanGraphics` to get both, or
`LightVulkanGraphics::UI` alone for just the device-independent toolkit.

**Acceptance**: `cmake -S . -B build && cmake --build build` succeeds with
`LVG_BUILD_UI` both ON and OFF. With OFF, the resulting library is byte-comparable in
its exported symbols to the pre-change build.

## Phase 1 — Primitives and DrawList

**Files**: `Types.h`, `DrawList.h`, `src/ui/DrawList.cpp`, `tests/ui/test_drawlist.cpp`

**Acceptance**
- `addRectFilled` on an empty list produces 4 vertices, 6 indices, 1 command
- Two rects with the same clip rect produce 1 command; with different clip rects, 2
- `pushClipRect(a); pushClipRect(b, true)` yields `a.intersect(b)` as the active clip
- `popClipRect` on a stack of depth 1 leaves the base full-framebuffer rect (does not
  underflow)
- A rect fully outside the clip rect still emits geometry (clipping is the GPU's job via
  scissor) but the command's clip rect is correct
- All rect coordinates in the vertex buffer are integers
- Colours round-trip: `Color::fromHex(0x3D8BFD).packed()` matches the expected
  `0xAABBGGRR` byte order

## Phase 2 — Font

**Files**: `Font.h`, `src/ui/Font.cpp`, `src/ui/FontImpl.cpp` (holds
`STB_TRUETYPE_IMPLEMENTATION`), `src/ui/Utf8.h/.cpp`, `tests/ui/test_font.cpp`,
`tests/ui/test_utf8.cpp`, `assets/fonts/<chosen>.ttf`, `assets/fonts/License.txt`

**Acceptance**
- Baking a 512×512 atlas at 14px succeeds for all default ranges
- `whitePixelUV` samples a value of 255 and no glyph overlaps the reserved block
- `measureText("")` is `{0, lineHeight}`
- `measureText("MM") ≈ 2 × measureText("M").x` within 0.5px
- `offsetAtIndex(s, 0) == 0` and `offsetAtIndex(s, s.size()) == measureText(s).x`
- `indexAtOffset` and `offsetAtIndex` are mutual inverses at every codepoint boundary
- Round-trip: for every codepoint in the default ranges, `utf8Encode` then `decodeUtf8`
  returns the original
- `decodeUtf8` on `"\xFF\xFE"` yields U+FFFD twice and terminates
- A codepoint outside the baked ranges returns the fallback glyph, not a zero-size quad

## Phase 3 — Vulkan backend

**Files**: `src/ui/UiRenderer.h/.cpp`, `shaders/ui.vert`, `shaders/ui.frag`, CMake shader
rules extended

**Acceptance**
- Validation layers clean across 1000 frames including window resize
- A hard-coded `DrawList` containing one red rect and the string `"Hello"` renders
  correctly over the 3D scene
- The scissor path is exercised: two rects with different clip rects, the second one
  visibly clipped
- Buffer growth works: push 50 000 vertices in one frame, confirm no corruption and no
  leak across 100 frames
- Zero-index-count and empty draw lists are no-ops, not crashes
- Resizing the window to 1×1 and back does not produce a validation error (this is where
  the negative-scissor bug surfaces)

## Phase 4 — Input

**Files**: `InputState.h`, `KeyCodes.h`, `src/ui/UiPlatformGlfw.h/.cpp`,
`tests/ui/test_input.cpp`

**Acceptance**
- Existing camera controls behave exactly as before when no panel exists
- Press-and-release within a single frame registers both edge flags
- `mouseDelta` is zero on the first frame after the cursor enters the window (no jump)
- Callback chaining verified: install the UI hooks, confirm the pre-existing GLFW
  callbacks still fire
- On a HiDPI display, `injectMousePos` values match widget rects (no 2× offset)

## Phase 5 — Context, Panel, first widgets

**Files**: `GuiContext.h/.cpp`, `Panel.h/.cpp`, `Widget.h/.cpp`, `Theme.h/.cpp`,
`widgets/Label`, `widgets/Button`, `widgets/Separator`, `widgets/Spacer`,
`tests/ui/test_layout.cpp`, `tests/ui/test_hittest.cpp`

**Acceptance**
- A panel with three labels and a button lays out with correct row positions
- Dragging the title bar moves the panel; a 2px jitter during a click does not
- Clicking a button fires exactly once per press-release cycle, never on press alone
- Pressing on a button then dragging off and releasing does **not** fire
- `wantsMouse()` is true when the cursor is over the panel, false over the scene
- Z-order: clicking a back panel brings it to front
- Layout tests run with no Vulkan device and no window

## Phase 6 — Value widgets

**Files**: `widgets/Checkbox`, `widgets/RadioButton`, `widgets/Slider`,
`widgets/DragValue`, `tests/ui/test_slider.cpp`

**Acceptance**
- Slider drag beyond the panel bounds keeps tracking (capture works)
- Shift-drag moves at 0.1× rate
- Double-click resets to the construction-time initial value
- Stepping: range 0–10 step 3 can never produce a value above 10
- Logarithmic slider with range 1e-4 to 1e-1 places 1e-2 near the midpoint
- `bind()` reflects external mutation of the target on the next frame
- `onChange` fires during drag, `onCommit` fires exactly once on release
- Checkbox toggles on the whole row, label included

## Phase 7 — TextBox

**Files**: `widgets/TextBox.h/.cpp`, `tests/ui/test_textedit.cpp`

**Acceptance** — test the editing state machine directly, without rendering:
- Insert, backspace, delete at the string start, middle, and end
- Backspace across a multi-byte codepoint deletes the whole codepoint
- Right-arrow with an active selection collapses to the selection end
- Shift+Left from the end selects backwards; Shift+Right then Shift+Left cancels
- Ctrl+A then typing replaces everything
- Paste of a string containing `\n` and `\t` strips them
- `setMaxLength(5)` counts codepoints, not bytes — `"ααααα"` is at the limit
- Escape reverts to the pre-focus text
- Caret never lands mid-codepoint after any sequence of operations (property test:
  random operation sequences, assert boundary validity every step)
- Password mode caret positions match the displayed bullet string

## Phase 8 — Remaining widgets

**Acceptance**
- DropDown popup renders above all panels and above a panel that overlaps it
- Popup flips upward when near the bottom of the framebuffer
- Clicking outside the popup closes it without selecting
- PlotLine with 512 samples costs one `addPolyline` call
- CollapsingSection closed reports header-only height and its children are not hit-testable
- Vec3Field's three sub-fields each drag independently

## Phase 9 — Panel polish

**Acceptance**
- Content taller than the panel produces a working scrollbar; wheel scrolls; the grab
  can be dragged
- Widgets scrolled out of view are not hit-testable
- Resize grip resizes; minimum size is enforced
- Anchored panels hold their corner offset across a window resize
- A panel dragged toward the top edge stops with its title bar on screen
- Tooltip appears after the delay, follows the cursor, flips near edges
- `Theme::light()` and `Theme::highContrast()` render legibly

## Phase 10 — Integration

**Files**: `examples/gui_control_panel_example.cpp`, install rules for
`assets/fonts/`, `docs/gui_usage.md`, README section, CHANGELOG entry

**Acceptance**
- `find_package(LightVulkanGraphics)` from a clean external consumer links
  `LightVulkanGraphics::UI` and builds
- The relocated-install CTest case passes with the font payload resolving correctly
- `gui_control_panel_example` builds and runs, showing the panel from
  [07-public-api.md](07-public-api.md)
- CI green on Linux GCC, Linux Clang, and the Windows core build
- Sanitizer pass clean (`LVG_ENABLE_SANITIZERS=ON`)

## Phase 11 — Optional

In rough order of value:

1. **World-anchored labels** — highest value for your stated use case, lowest cost
2. **Anti-aliasing** — ImGui's fringe technique; a full extra vertex ring per shape
3. **Kerning** — cheap, but complicates caret positioning
4. **Golden-image tests** — rasterise the draw list on the CPU with a tiny scanline
   filler, compare against reference PNGs via `stb_image_write`. Catches visual
   regressions that unit tests cannot.
5. **Panel docking to window edges**
6. **A `ColorEdit` widget** — swatch plus RGBA sliders, natural given your `vec4` colours

## Global invariants

Have Claude Code check these at the end of every phase:

1. No file under `include/lightVulkanGraphics/ui/` or `src/ui/` includes a Vulkan or
   GLFW header, except `UiRenderer.*` and `UiPlatformGlfw.*`
2. All public headers compile standalone (add a `test_header_selfcontained.cpp` that
   includes each in isolation)
3. No `new`/`delete` outside of `std::unique_ptr` construction
4. Every widget's `draw()` is `const` and mutates nothing
5. `-DLVG_ENABLE_WARNINGS=ON -DLVG_WARNINGS_AS_ERRORS=ON` still builds
