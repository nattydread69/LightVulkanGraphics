# 07 — Public API

The contract Claude Code implements against. Signatures here are normative; if an
implementation needs to deviate, update this file in the same commit.

## Header layout

```
include/lightVulkanGraphics/ui/
    Types.h          Vec2, Rect, Color, Align, WidgetId, PanelFlags
    KeyCodes.h       Key:: enum, Mod:: bitmask, CursorShape
    InputState.h     InputState, KeyEvent, MouseButton
    Theme.h          Theme
    Font.h           Font, Glyph, GlyphRange
    DrawList.h       UiVertex, DrawCmd, DrawList
    Widget.h         Widget base
    Panel.h          Panel
    GuiContext.h     GuiContext, GuiCreateInfo, PlatformHooks*
    Ui.h             umbrella header — includes everything, defines `namespace lvgui`
    widgets/
        Label.h  Separator.h  Spacer.h  Button.h  Checkbox.h  RadioButton.h
        Slider.h  DragValue.h  Vec3Field.h  TextBox.h  DropDown.h
        ProgressBar.h  PlotLine.h  CollapsingSection.h  Row.h
```

`*` **Current deviation (as of phase 4):** `PlatformHooks` is declared in
`src/ui/UiPlatformGlfw.h`, not `GuiContext.h` — it needed to exist before `GuiContext`
did, so phase 4's `UiPlatformGlfw::makeHooks()` could build and test it without a
window. The shape matches this doc exactly. Phase 5 should move the declaration into
`GuiContext.h` (or have `GuiContext.h` re-export the `UiPlatformGlfw.h` one) so the
project ends up with the single definition this table describes.

Consumers include one thing:

```cpp
#include <lightVulkanGraphics/ui/Ui.h>
namespace lvgui = lightGraphics::ui;
```

## Types.h

```cpp
namespace lightGraphics::ui {

using WidgetId = std::uint64_t;
inline constexpr WidgetId kInvalidWidgetId = 0;

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2) const;  Vec2 operator-(Vec2) const;
    Vec2 operator*(float) const; Vec2& operator+=(Vec2);
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    constexpr float left()   const { return x; }
    constexpr float top()    const { return y; }
    constexpr float right()  const { return x + w; }
    constexpr float bottom() const { return y + h; }
    constexpr Vec2  min()    const { return { x, y }; }
    constexpr Vec2  centre() const { return { x + w * 0.5f, y + h * 0.5f }; }

    bool contains(Vec2 p) const;
    Rect intersect(const Rect&) const;      // may return zero-area
    Rect inset(float amount) const;
    Rect insetXY(float dx, float dy) const;
    bool empty() const { return w <= 0.0f || h <= 0.0f; }

    static Rect fromMinMax(Vec2 lo, Vec2 hi);
};

struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    constexpr Color() = default;
    constexpr Color(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_,
                    std::uint8_t a_ = 255);
    static constexpr Color fromFloats(float r, float g, float b, float a = 1.0f);
    static constexpr Color fromHex(std::uint32_t rgb, float alpha = 1.0f);
    constexpr std::uint32_t packed() const;      // 0xAABBGGRR
    Color withAlpha(float multiplier) const;
    static Color lerp(Color a, Color b, float t);
};

enum class Align { Start, Center, End };

} // namespace lightGraphics::ui
```

`Rect` is deliberately `{x, y, w, h}` rather than min/max. Layout code reads better with
width and height; the `right()`/`bottom()` accessors cover the rest.

## Font.h

```cpp
namespace lightGraphics::ui {

struct GlyphRange { std::uint32_t first, last; };   // inclusive

extern const GlyphRange kDefaultGlyphRanges[];
extern const std::size_t kDefaultGlyphRangeCount;

struct Glyph {
    Vec2  uv0, uv1;
    Vec2  offset;
    Vec2  size;            // display size = xoff2-xoff (NOT the atlas rect)
    float xAdvance = 0.0f;
};

class Font {
public:
    Font();
    ~Font();
    Font(const Font&)            = delete;
    Font& operator=(const Font&) = delete;
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;

    void bake(const std::vector<std::uint8_t>& ttfData, float pixelHeight,
              const GlyphRange* ranges, std::size_t rangeCount,
              int atlasWidth = 512, int atlasHeight = 512, float contentScale = 1.0f);
    void bake(const std::vector<std::uint8_t>& ttfData, float pixelHeight,
              int atlasWidth = 512, int atlasHeight = 512, float contentScale = 1.0f);
    bool isBaked() const;

    Vec2  measureText(std::string_view utf8, float pixelSize) const;
    float lineHeight(float pixelSize) const;
    float ascent(float pixelSize) const;
    std::size_t indexAtOffset(std::string_view utf8, float pixelSize, float x) const;
    float offsetAtIndex(std::string_view utf8, float pixelSize, std::size_t byteIndex) const;

    const Glyph& glyphFor(std::uint32_t codepoint) const;   // never zero-size
    bool hasGlyph(std::uint32_t codepoint) const;

    Vec2 whitePixelUV() const;
    int  atlasWidth() const;
    int  atlasHeight() const;
    const std::vector<std::uint8_t>& atlasPixels() const;

    float bakedPixelSize() const;
    float contentScaleAtBake() const;
};

} // namespace lightGraphics::ui
```

Two deliberate departures from the sketch in
[03-text-and-fonts.md](03-text-and-fonts.md):

- **`ranges` is a pointer+count pair, not `std::span`.** `std::span` is C++20 and this
  project builds its public headers as C++17.
- **`bake` returns `void` and throws.** Per the error-handling policy below,
  construction failures throw; once the one-shot 1024×1024 retry has also failed there
  is no recoverable `false` case left to report. It throws `std::runtime_error` if the
  data will not parse as TrueType, or if packing fails at 1024×1024.

**Caller contract on `pixelHeight`:** it must already have `contentScale` folded in —
pass `fontSize_logical * contentScale`. For 14px logical at 2× scale the correct call is
`bake(ttf, 28.0f, ..., /*contentScale=*/2.0f)`. Passing `(14.0f, contentScale=2.0f)` is
a caller error: measurements come back at *half* the intended logical size rather than
merely looking blurry. `Font` cannot verify this — it never sees the logical size — so
it is documented rather than asserted. `contentScale` is stored only so the caller can
compare it against the live content scale and decide when to rebake.

## GuiContext.h

```cpp
namespace lightGraphics::ui {

struct GuiCreateInfo {
    std::string fontPath;                 // empty = use the standard search order
    float       fontSize      = 14.0f;
    Theme       theme         = Theme::dark();
    int         atlasWidth    = 512;
    int         atlasHeight   = 512;
    bool        enableTooltips = true;
};

// As of phase 4 this struct is actually declared in src/ui/UiPlatformGlfw.h, not here
// -- see the header-layout table above. Move it into this file (or re-export it from
// here) when GuiContext lands.
struct PlatformHooks {
    std::function<std::string()>          getClipboardText;
    std::function<void(std::string_view)> setClipboardText;
    std::function<void(CursorShape)>      setCursorShape;
};

class GuiContext {
public:
    GuiContext(const GuiCreateInfo&, PlatformHooks);
    ~GuiContext();
    GuiContext(const GuiContext&)            = delete;
    GuiContext& operator=(const GuiContext&) = delete;

    // ---- panels ----
    Panel* createPanel(std::string title, Rect bounds,
                       PanelFlags flags = PanelFlags::Default);
    void   destroyPanel(Panel*);
    void   destroyAllPanels();
    std::size_t panelCount() const;
    Panel* panelAt(std::size_t index) const;   // index 0 = frontmost

    // ---- frame ----
    void beginFrame(Vec2 displaySize, float contentScale, float deltaTime);
    void update();
    void endFrame();
    const DrawList& drawList() const;

    // ---- input hand-off ----
    bool wantsMouse()    const;
    bool wantsKeyboard() const;

    // ---- input injection (called by the platform layer or by tests) ----
    void injectMousePos(Vec2 logicalPos);
    void injectMouseButton(MouseButton, bool pressed);
    void injectScroll(float delta);
    void injectKey(int key, int mods, bool pressed, bool repeat);
    void injectChar(std::uint32_t codepoint);

    // ---- accessors ----
    Theme&       theme();
    const Theme& theme() const;
    const Font&  font() const;
    const InputState& input() const;

    WidgetId hoveredId() const;
    WidgetId activeId()  const;
    WidgetId focusedId() const;
    void     setFocus(Widget*);
    void     clearFocus();

    // ---- extras ----
    void addWorldLabel(const glm::vec3& worldPos, const glm::mat4& viewProj,
                       std::string_view text, Color, Vec2 pixelOffset = {});
    void postToMainThread(std::function<void()>);
    bool atlasNeedsRebuild() const;     // renderer polls; clears on acknowledge
    void acknowledgeAtlasRebuild();
};

} // namespace lightGraphics::ui
```

The `inject*` family is the whole platform seam. Layer 5 calls it from GLFW callbacks;
tests call it directly. Nothing else crosses that boundary.

## The core library integration

Additions to `include/lightVulkanGraphics.h`:

```cpp
namespace lightGraphics {

struct LightVulkanGraphicsCreateInfo {
    // ... existing fields ...
#ifdef LVG_WITH_UI
    bool          enableGui = true;
    ui::GuiCreateInfo guiCreateInfo {};
#endif
};

class LightVulkanGraphics {
public:
    // ... existing API ...

#ifdef LVG_WITH_UI
    bool            hasGui() const;
    ui::GuiContext& gui();                 // asserts hasGui()
#endif
};

} // namespace lightGraphics
```

## Usage — what a consumer writes

This is the target. If the final API is more verbose than this, something went wrong.

```cpp
#include "lightVulkanGraphics.h"
#include <lightVulkanGraphics/ui/Ui.h>

namespace lvgui = lightGraphics::ui;

struct SimParams {
    float dt        = 0.016f;
    float gravity   = 9.81f;
    int   substeps  = 4;
    bool  showGrid  = true;
    bool  wireframe = false;
    glm::vec3 windDir { 1.0f, 0.0f, 0.0f };
};

int main()
{
    lightGraphics::LightVulkanGraphics gfx("Simulation");
    SimParams params;

    auto& gui = gfx.gui();
    auto* panel = gui.createPanel("Simulation", { 16, 16, 320, 460 });
    panel->setAnchor(lvgui::Panel::Anchor::TopLeft);

    panel->add<lvgui::Separator>("Integration");

    auto* dt = panel->add<lvgui::Slider>("Timestep", 1e-4f, 0.1f, params.dt);
    dt->setScale(lvgui::SliderScale::Logarithmic);
    dt->setUnitSuffix(" s");
    dt->setFormat("%.4f");
    dt->bind(&params.dt);

    panel->add<lvgui::SliderInt>("Substeps", 1, 32, params.substeps)
         ->bind(&params.substeps);

    auto* g = panel->add<lvgui::Slider>("Gravity", 0.0f, 30.0f, params.gravity);
    g->setUnitSuffix(" m/s²");
    g->setOnCommit([&](float v){ rebuildIntegrator(v); });

    panel->add<lvgui::Separator>("Display");
    panel->add<lvgui::Checkbox>("Show grid", params.showGrid)->bind(&params.showGrid);
    panel->add<lvgui::Checkbox>("Wireframe", params.wireframe)
         ->setOnChange([&](bool on){ gfx.setWireframe(on); });

    panel->add<lvgui::Vec3Field>("Wind", params.windDir)->bind(&params.windDir);

    auto* name = panel->add<lvgui::TextBox>("Run name", "run_001");
    name->setFilter(lvgui::TextFilter::Identifier);
    name->setOnCommit([&](std::string_view s){ setOutputPrefix(s); });

    auto* energy = panel->add<lvgui::PlotLine>("Energy", 512);
    energy->setHeight(48.0f);

    panel->add<lvgui::Row>()
         ->add<lvgui::Button>("Reset")->setOnClick([&]{ resetSim(); });

    gfx.setCameraLookAt({ 0, 2, 6 }, { 0, 0, 0 });
    gfx.finalizeScene();

    while (gfx.beginFrame()) {
        stepSimulation(params);
        energy->push(totalEnergy());
        gfx.endFrame();
    }
    return 0;
}
```

Note what is absent: no per-frame UI declaration, no ImGui-style `if (Button(...))`
polling, no manual draw calls. The panel is built once and the callbacks and bindings do
the rest.

## Error handling policy

- **Construction failures throw.** Missing font, atlas too small, pipeline creation
  failure — these are unrecoverable at init and a `std::runtime_error` with a message
  naming the cause is the right answer.
- **Per-frame operations never throw.** A widget with a broken format string draws
  `"<fmt error>"`; a NaN slider value clamps to the range minimum. A GUI must not be able
  to take down a running simulation.
- **Programmer errors assert.** `Logarithmic` scale with a non-positive minimum,
  `panelAt` out of range, adding a widget to a destroyed panel.
- **Route diagnostics through the existing log callback.** Do not add a second logging
  mechanism; use whatever `setDebugOutput` / the log callback already provides, and
  respect `enableConsoleOutput`.
