# 08 — Testing

## The strategy in one line

Everything above the Vulkan backend is a pure function of `(state, input) → (state,
geometry)`, so test it headlessly and exhaustively; test the backend by eye and with the
validation layers.

This is not a compromise. GUI bugs cluster in exactly the places that are testable
without a GPU — text editing, layout arithmetic, hit testing, value clamping — and
almost never in "did the triangle appear". Your existing CI already runs Linux GCC,
Linux Clang, a sanitizer pass, and a Windows core build with no display; all the tests
below run in that environment.

## What runs where

| Test | Needs font file | Needs GPU | Needs window | CI |
|------|-----------------|-----------|--------------|-----|
| Types, Rect arithmetic | no | no | no | yes |
| DrawList geometry | no | no | no | yes |
| UTF-8 | no | no | no | yes |
| Font metrics | **yes** | no | no | yes |
| Layout | yes | no | no | yes |
| Hit testing and capture | yes | no | no | yes |
| Widget interaction | yes | no | no | yes |
| Text editing | yes | no | no | yes |
| Header self-containment | no | no | no | yes |
| Pipeline creation | no | **yes** | yes | no — manual |
| Visual correctness | no | yes | yes | no — manual |

The font file is a build-tree asset, so the font-dependent tests still run in CI without
installing anything.

## Test target

`tests/ui/` builds one small executable per feature area (`lvgui_layout_tests`,
`lvgui_hittest_tests`, `lvgui_slider_tests`, `lvgui_dropdown_tests`, `lvgui_panel_tests`,
and so on — see `tests/ui/CMakeLists.txt` for the current, authoritative list), each
registered with `add_test`. Font-dependent ones get the bundled font's path via a compile
definition:

```cmake
add_executable(lvgui_widget_area_tests test_widget_area.cpp)
target_link_libraries(lvgui_widget_area_tests PRIVATE LightVulkanGraphics::UI)
target_compile_definitions(lvgui_widget_area_tests PRIVATE
    LVG_UI_TEST_FONT_PATH="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf")
add_test(NAME lvgui_widget_area_tests COMMAND lvgui_widget_area_tests)
```

Plain `assert` plus a `main`, matching the rest of the project — these tests do not need
fixtures or mocking.

## The headless harness

```cpp
struct UiTestFixture {
    lightGraphics::ui::GuiContext ctx;

    UiTestFixture()
      : ctx(makeCreateInfo(), makeNoOpHooks())
    {
        ctx.beginFrame({ 1280, 720 }, 1.0f, 1.0f / 60.0f);
        ctx.update();
        ctx.endFrame();
    }

    // Advance exactly one frame with the current injected input.
    void step(float dt = 1.0f / 60.0f) {
        ctx.beginFrame({ 1280, 720 }, 1.0f, dt);
        ctx.update();
        ctx.endFrame();
    }

    void click(Vec2 p) {
        ctx.injectMousePos(p);
        ctx.injectMouseButton(MouseButton::Left, true);
        step();
        ctx.injectMouseButton(MouseButton::Left, false);
        step();
    }

    void dragFrom(Vec2 a, Vec2 b, int steps = 8) {
        ctx.injectMousePos(a);
        ctx.injectMouseButton(MouseButton::Left, true);
        step();
        for (int i = 1; i <= steps; ++i) {
            ctx.injectMousePos(lerp(a, b, float(i) / steps));
            step();
        }
        ctx.injectMouseButton(MouseButton::Left, false);
        step();
    }

    void type(std::string_view utf8);   // injects one char event per codepoint
    void press(int key, int mods = 0);  // key down + up, one step each
};
```

Getting this fixture right early pays for itself many times over. Every widget test is
then three lines.

## High-value tests

Ordered by bugs-caught-per-line-written.

### 1. Text editing property test

The single most valuable test in the suite.

```cpp
void testEditingInvariants() {
    for (int seed = 0; seed < 2000; ++seed) {
        std::mt19937 rng(seed);
        TextBox box("t", randomUtf8String(rng));   // must include Greek and accented Latin
        for (int op = 0; op < 40; ++op) {
            applyRandomOperation(box, rng);
            assert(isCodepointBoundary(box.text(), box.caretIndex()));
            assert(isCodepointBoundary(box.text(), box.anchorIndex()));
            assert(box.caretIndex()  <= box.text().size());
            assert(box.anchorIndex() <= box.text().size());
            assert(isValidUtf8(box.text()));
        }
    }
}
```

A caret that lands mid-codepoint produces a crash or mojibake much later, in a context
that gives no hint where it came from. This test finds it immediately.

### 2. Font measurement round-trip

```cpp
for (std::string_view s : { "", "M", "Hello, world", "ωΔt = 0.001", "ÀÉîõü" }) {
    for (size_t i = 0; i <= s.size(); i = utf8NextBoundary(s, i)) {
        float x = font.offsetAtIndex(s, 14.0f, i);
        assert(font.indexAtOffset(s, 14.0f, x) == i);
    }
    assert(std::abs(font.offsetAtIndex(s, 14.0f, s.size())
                  - font.measureText(s, 14.0f).x) < 0.01f);
}
```

Caret placement, selection rectangles, ellipsis truncation and click-to-position all
depend on these two functions agreeing. When they disagree the symptom is a caret that
sits one character off, which is easy to notice and hard to localise.

### 3. Slider stepping and clamping

```cpp
SliderInt s("n", 0, 10, 0);
s.setStep(3);
for (float t = -0.5f; t <= 1.5f; t += 0.01f) {
    s.setValueFromNormalised(t);
    assert(s.value() >= 0 && s.value() <= 10);
}
```

The order of round-then-clamp versus clamp-then-round is a one-line difference that
produces out-of-range values only at the extremes, which manual testing usually misses.

### 4. Capture across boundaries

```cpp
auto* slider = panel->add<Slider>("v", 0.0f, 1.0f, 0.5f);
fx.step();
fx.dragFrom(sliderTrackCentre(slider), { 5000.0f, -5000.0f });
assert(slider->value() == 1.0f);       // tracked all the way out and clamped
```

Without capture this silently does nothing, and the failure mode in the real application
is "the slider sometimes stops responding", which is very hard to reproduce by hand.

### 5. Clip-rect hit-test intersection

```cpp
// A widget scrolled out of view must not be hittable.
panel->setBounds({ 0, 0, 300, 100 });
for (int i = 0; i < 20; ++i) panel->add<Button>("b" + std::to_string(i));
fx.step();
auto* offscreen = /* the 19th button */;
assert(!fx.ctx.hitTestReaches(offscreen));
```

### 6. Header self-containment

```cpp
// test_header_selfcontained.cpp — one TU per header, generated by CMake if you prefer
#include <lightVulkanGraphics/ui/Slider.h>
int main() { return 0; }
```

Cheap, and it catches the missing-include-that-happens-to-work-in-one-order class of
problem before a consumer does.

## What not to test

- **Exact vertex counts for complex primitives.** Testing that a rounded rect produces
  exactly 67 vertices locks in an implementation detail and breaks on every tweak. Test
  the invariants instead: the geometry lies within the rect's bounds, the vertex count is
  non-zero, the index count is a multiple of three, every index is in range.
- **Colours of individual pixels.** That is what golden-image testing is for, and it's a
  maintenance burden not currently worth taking on.
- **Panel drag arithmetic beyond the threshold rule.** It is three lines of vector maths
  and testing it adds nothing.

## Manual verification checklist

Run this before every release. Ten minutes, and it catches everything the headless suite
structurally cannot.

**Rendering**
- [ ] Text is sharp, not blurred or doubled
- [ ] Panel edges are crisp 1px lines, not 2px greys
- [ ] Panel background translucency lets the scene through without hurting legibility
- [ ] No speckles or bleed at glyph edges (atlas addressing mode)
- [ ] Widget clipping is exact at the panel content edge

**Interaction**
- [ ] Slider drag continues when the cursor leaves the window entirely
- [ ] Typing `W`, `A`, `S`, `D` into a text box does not move the camera
- [ ] A right-drag camera look that starts on the scene continues across a panel
- [ ] Scroll wheel over a panel scrolls it; over the scene it zooms
- [ ] Tab cycles focus in declaration order and wraps
- [ ] The focus ring is visible on every focusable widget

**Robustness**
- [ ] Window resize down to 1×1 and back: no crash, no validation error
- [ ] Minimise and restore
- [ ] Drag a window between a HiDPI and a standard monitor: text rebakes and stays sharp
- [ ] 200 widgets in one scrolling panel stays at frame rate
- [ ] Destroy and recreate every panel mid-frame: no leak, no dangling pointer
- [ ] Run under `LVG_ENABLE_SANITIZERS=ON` for five minutes of interaction

**Diagnostics**
- [ ] Validation layers silent for 60 seconds of heavy interaction
- [ ] `enableConsoleOutput = false` genuinely silences the GUI layer too

## Performance expectations

Ballpark figures to notice a regression against, on a mid-range desktop:

| Scenario | Draw calls | CPU per frame |
|----------|-----------|---------------|
| 1 panel, 12 widgets | 3–4 | < 0.1 ms |
| 4 panels, 60 widgets | 9–11 | < 0.3 ms |
| 1 panel, 200 widgets scrolling | 3–4 | < 0.8 ms |

If the 200-widget case exceeds a millisecond, the likely cause is `measureText` being
called repeatedly on unchanged labels. Cache the measured width on the widget, keyed by
the string and the font size, and invalidate on `setLabel` or a theme change. Do not do
this pre-emptively — measure first.
