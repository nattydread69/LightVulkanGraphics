# 07 — Public API

The public header layout, and the shape of the surface each header exposes. The headers
under `include/lightVulkanGraphics/ui/` are authoritative for exact signatures; what
follows is a sketch precise enough to work from, trimmed of internal-only members.

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
    MenuBar.h        MenuBar (docs/gui/05-widgets.md, "MenuBar" -- not a Widget)
    GuiContext.h     GuiContext, GuiCreateInfo, PlatformHooks*
    Ui.h             umbrella header — includes everything, defines `namespace lvgui`
    widgets/
        Label.h  Separator.h  Spacer.h  Button.h  Checkbox.h  RadioButton.h
        Slider.h  DragValue.h  Vec3Field.h  TextBox.h  DropDown.h
        ProgressBar.h  PlotLine.h  CollapsingSection.h  Row.h  ColorEdit.h  Image.h
        TabBar.h  ListBox.h  ContextMenu.h  LogView.h
```

`*` `PlatformHooks` is declared in `GuiContext.h` itself; `src/ui/UiPlatformGlfw.h`
includes it from there rather than declaring its own copy, so there is a single
definition.

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

    Vec2  measureText(std::string_view utf8, float pixelSize, TextFlags flags = TextFlags::None) const;
    float lineHeight(float pixelSize) const;
    float ascent(float pixelSize) const;
    // Padded per TextFlags::Tabular for ASCII digits -- docs/gui/03-text-and-fonts.md,
    // "Tabular figures".
    float advanceFor(std::uint32_t codepoint, float pixelSize, TextFlags flags = TextFlags::None) const;
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

Two choices worth calling out:

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
    // Left empty, GuiContext's constructor falls back to its own bundled-font search
    // (findBundledFontPath(), GuiContext.cpp -- env var, build tree, install prefix) and
    // throws only if THAT comes up empty too. VkApp-based consumers never exercise this
    // path at all: VkApp resolves a concrete path via its own font search order (see
    // 03-text-and-fonts.md, "Font asset resolution") before ever constructing a
    // GuiCreateInfo. The fallback exists for a caller constructing a bare GuiContext
    // directly, without VkApp -- LightVulkanGraphicsUI cannot call into VkApp's search
    // itself (the dependency runs core -> UI, never the reverse), so it duplicates the
    // same search order rather than sharing it.
    std::string fontPath;
    float       fontSize      = 14.0f;
    Theme       theme         = Theme::dark();
    int         atlasWidth    = 512;
    int         atlasHeight   = 512;
    bool        enableTooltips = true;
    // 0.0f (default) = no heading face baked at all. A positive logical size bakes a
    // SECOND, independent Font from the same font bytes, for Label::setHeading()
    // (docs/gui/03-text-and-fonts.md, "Headings").
    float       headingFontSize = 0.0f;
};

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
    // Frontmost visible PanelFlags::Modal panel, or nullptr (docs/gui/05-widgets.md,
    // "Panel", "Modal panels").
    Panel* activeModalPanel() const;

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

    // ---- heading face (docs/gui/03-text-and-fonts.md, "Headings") ----
    bool hasHeadingFont() const;
    const Font& headingFont() const;      // only valid when hasHeadingFont()
    float headingFontSize() const;
    TextureId headingFontTextureId() const;
    void setHeadingFontTextureId(TextureId);   // VkApp calls this once it's registered

    // ---- menu bar (docs/gui/05-widgets.md, "MenuBar") ----
    MenuBar&       menuBar();
    const MenuBar& menuBar() const;

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

    // ---- layout persistence (docs/gui/05-widgets.md, "Panel", "Layout persistence") ----
    std::string saveLayout() const;
    void        loadLayout(std::string_view);
};

} // namespace lightGraphics::ui
```

The `inject*` family is the whole platform seam. Layer 5 calls it from GLFW callbacks;
tests call it directly. Nothing else crosses that boundary.

This sketch covers the primary consumer-facing surface; `GuiContext.h` additionally
exposes a handful of narrower queries used mainly by the camera hand-off and by widgets
themselves — `wantsScroll()`, `isTooltipVisible()`, the popup API (`openPopup` /
`closePopup` / `isPopupOpen` / `popupOwner` / `popupRect`, see
[05-widgets.md](05-widgets.md)'s "DropDown") — see the header for the complete list.

## The core library integration

`VkApp` (`include/VkApp.h`) owns the GUI's lifetime. As soon as `LVG_WITH_UI` is
compiled in and `VkApp::init()` has brought up a Vulkan device and render pass, it
constructs a `GuiContext` with theme defaults and the bundled font (see
[03-text-and-fonts.md](03-text-and-fonts.md), "Font asset resolution"):

```cpp
namespace lightGraphics {

class VkApp {
public:
    // ... existing API ...

#ifdef LVG_WITH_UI
    void            setGuiCreateInfo(const ui::GuiCreateInfo&);  // before init(); no effect after
    bool            hasGui() const;
    ui::GuiContext& gui();                 // asserts hasGui()

    // For the Image widget (docs/gui/05-widgets.md, "Image") -- and, internally,
    // for registering the heading face's atlas as a texture too
    // (docs/gui/03-text-and-fonts.md, "Headings").
    ui::TextureId   registerUiTexture(const uint8_t* rgbaPixels, uint32_t width, uint32_t height);
    void            unregisterUiTexture(ui::TextureId);
#endif
};

} // namespace lightGraphics
```

`setGuiCreateInfo()` follows the same "must be called before `init()`" convention as
`setMaxTextureCount()`: it stores the struct, and `init()`'s call to `initUi()` is what
actually constructs `GuiContext` from it. Fields left at `GuiCreateInfo`'s own defaults
behave exactly as before this existed — an empty `fontPath` still resolves through the
bundled-font search (`VkApp::findFontPath()`) rather than being passed through to throw.
`fontSize` is authoritative over `theme.fontSize` (see `GuiCreateInfo::fontSize`'s
comment) — set it once here rather than reconciling both fields by hand. Themes can
still be swapped at runtime with `gui.theme() = ...` (see
[06-layout-and-theme.md](06-layout-and-theme.md), "Theme"); doing so resets `fontSize`
to that theme's own default, so re-apply a non-default size afterward if one was set.

## Usage — what a consumer writes

No per-frame UI declaration, no ImGui-style `if (Button(...))` polling, no manual draw
calls: the panel is built once, and callbacks/bindings do the rest. See
[`../gui_usage.md`](../gui_usage.md) for the full how-to and a worked example against the
real `VkApp` API.

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
