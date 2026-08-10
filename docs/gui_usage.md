# GUI (LVGUI)

LightVulkanGraphics has a built-in, Vulkan-native GUI layer (`lightGraphics::ui`,
shorthand **LVGUI**) for parameter panels beside your 3D scene: sliders, checkboxes, text
entry, dropdowns, sparklines. It draws through the same pipeline and render pass as the
rest of the frame — no second window, no second toolkit.

This page is a task-focused how-to. For the full behavioural spec of every widget (used
when the library itself was built), see [`gui/`](gui/) — start with
[`gui/00-overview.md`](gui/00-overview.md) if you want the design rationale.

## Enabling it

The GUI builds by default (`-DLVG_BUILD_UI=ON`, the CMake default). Pass
`-DLVG_BUILD_UI=OFF` to drop it entirely. When it's on, `LVG_WITH_UI` is defined for
consumers of the installed package too, so you can guard optional code:

```cpp
#ifdef LVG_WITH_UI
    // ...
#endif
```

No other setup is required. `VkApp::init()` brings up a `GuiContext` automatically (dark
theme, the bundled Inter font at 14px) as soon as the Vulkan device and render pass
exist. Check `app.hasGui()` before touching it — it's `false` if the GUI layer wasn't
built in, or if `init()` hasn't gotten far enough yet:

```cpp
#include "VkApp.h"
#include <lightVulkanGraphics/ui/Ui.h>

namespace lvgui = lightGraphics::ui;

lightGraphics::VkApp app;
app.init(1280, 800, "My Simulation");

if (!app.hasGui()) {
    // built without LVG_BUILD_UI, or the GUI failed to come up -- run without it
    app.run();
    return 0;
}

auto& gui = app.gui();
```

`app.run()` drives its own loop and calls the GUI's per-frame update/draw internally —
you never call `beginFrame`/`endFrame` on the GUI yourself. Your own per-frame code goes
into `setUpdateCallback`, called once before `run()`:

```cpp
app.setUpdateCallback([&](float deltaTime) {
    stepSimulation(params, deltaTime);
    energy->push(totalEnergy());   // widgets are safe to read/update from here
});

app.run();
```

Camera input suppression (don't orbit the camera while dragging a slider, don't zoom
while scrolling a panel) is already wired up inside `run()`; you don't need to check
`wantsMouse()`/`wantsKeyboard()` yourself unless you're driving Vulkan/GLFW directly
instead of through `VkApp`.

## The mental model

- **Retained, not immediate.** You build a `Panel` and its widgets once, wire up
  callbacks or bindings, and don't touch them again unless something needs to change.
  There's no per-frame `if (Button(...))` polling.
- **A panel is a vertical stack of rows.** Each widget you `add<W>()` gets one row, full
  width, at its preferred height, in the order you added it. `Row` (horizontal grouping)
  and `setBoundsOverride()` (absolute placement) are the two escape hatches — you won't
  need either for a typical parameter panel.
- **Every value widget offers both a callback and a bound pointer.** Use whichever suits
  the call site; you don't have to pick one style for the whole panel.

## Quick start

```cpp
struct SimParams {
    float dt      = 0.016f;
    float gravity = 9.81f;
    bool  showGrid = true;
    glm::vec3 windDir{ 1.0f, 0.0f, 0.0f };
};
SimParams params;

auto* panel = gui.createPanel("Simulation", { 16.0f, 16.0f, 320.0f, 420.0f });
panel->setAnchor(lvgui::Panel::Anchor::TopLeft);

panel->add<lvgui::Separator>("Integration");

auto* dt = panel->add<lvgui::Slider>("Timestep", 1e-4f, 0.1f, params.dt);
dt->setScale(lvgui::SliderScale::Logarithmic);
dt->setUnitSuffix(" s");
dt->bind(&params.dt);                 // written back automatically as the user drags

auto* g = panel->add<lvgui::Slider>("Gravity", 0.0f, 30.0f, params.gravity);
g->setUnitSuffix(" m/s^2");
g->setOnCommit([&](float v){ rebuildIntegrator(v); });   // fires once, on release

panel->add<lvgui::Checkbox>("Show grid", params.showGrid)->bind(&params.showGrid);
panel->add<lvgui::Vec3Field>("Wind", params.windDir)->bind(&params.windDir);

auto* energy = panel->add<lvgui::PlotLine>("Energy", 512);
energy->setHeight(48.0f);

app.setUpdateCallback([&](float deltaTime) {
    stepSimulation(params, deltaTime);
    energy->push(totalEnergy());
});

app.run();
```

`bind(&x)` reads `x` at the start of every frame (so external changes show up) and writes
it back the moment the user changes the value. It's the answer to "the simulation changed
`params.gravity`, why does the slider still show the old number" — you never have to
manually push a value back into a bound widget. The pointer must outlive the widget.

## Panels

```cpp
Panel* p = gui.createPanel("Title", Rect{ x, y, w, h }, PanelFlags flags = Default);
```

`PanelFlags::Default` is `Movable | Resizable | Collapsible | Scrollable`. Turn any of
those off with `setFlags()`, or add `NoTitleBar` / `NoBackground` for a borderless
overlay panel. A few things a panel does for you without any further code:

- **Scrolls.** If the widgets you've added don't fit the panel's height, a scrollbar
  appears in the right-hand gutter automatically — wheel or drag the grab to reach the
  rest. Nothing to opt into beyond leaving `PanelFlags::Scrollable` set (the default).
- **Resizes.** Drag the bottom-right corner. It won't shrink below a size that would hide
  the title bar or make labels unreadable.
- **Stays on screen.** Drag the title bar fully off the top edge and the panel stops with
  the title bar still grabbable — it can't be dragged somewhere permanently unreachable.
- **Anchors to a corner across window resizes:**

  ```cpp
  panel->setAnchor(lvgui::Panel::Anchor::TopRight);
  ```

  Without this, a panel placed near the right edge ends up floating in the middle of the
  window after a resize. `Anchor::{TopLeft, TopRight, BottomLeft, BottomRight}` — pick
  whichever corner the panel should hug.

Other panel-level calls: `setTitle`, `setVisible`, `setCollapsed` (collapses to just the
title bar), `bringToFront`, `remove(Widget*)`, `clear()`.

## Widget tour

Every widget's constructor takes a label as its first argument (pass `""` for a control
that should take the full row width, e.g. a `Row`'s children). Every widget accepts
`setTooltip("...")` (shown after hovering it for `theme.tooltipDelay` seconds — works on
`Label`/`Separator` too, not just controls; `Spacer` is the one exception, since it's
never hit-testable) and `setEnabled(false)` (greys it out and blocks input).

**Non-interactive:**

| Widget | Use it for |
|---|---|
| `Label(text)` | Static text. `setAlign`, `setWordWrap`, `setColor`. |
| `Separator()` / `Separator(caption)` | A visual break between groups of controls. |
| `Spacer(height)` | Fixed vertical gap. |
| `ProgressBar(label)` | `setFraction(0..1)` or `setIndeterminate(true)` for a busy sweep. |
| `PlotLine(label, historySize)` | Sparkline. `push(sample)` each frame; auto-scales. |

**Values:**

| Widget | Use it for |
|---|---|
| `Checkbox(label, initial)` | Bool toggle. `setTriState(true)` adds Indeterminate. |
| `RadioButton(label, RadioGroup*, value)` | Exclusive selection; group is a separate object you own (see below). |
| `Slider` / `SliderInt` (`SliderT<float>`/`SliderT<int>`) | A value with a physically meaningful range. `setStep`, `setScale(Logarithmic)` for values spanning orders of magnitude, `setUnitSuffix` (put units here, not in the label). |
| `DragValueT<float>` / `DragValueT<int>` | An unbounded scrubber for a quantity where a min/max would be a lie (mass, position). `setSoftRange` clamps without changing the drag mapping. |
| `Vec3Field(label, glm::vec3)` | Three `DragValue`s in a row for a position/direction/color. |
| `TextBox(label, initial)` | Free text. `setFilter(Integer/Decimal/Identifier)`, `setPlaceholder`, `setMaxLength`, `setPasswordMode`, `setValidator` (red border when it returns false). |
| `DropDown(label, items, initialIndex)` | A combo box; scrolls its own popup past 12 items. |

**Grouping / layout:**

| Widget | Use it for |
|---|---|
| `Row()` | Horizontal grouping, e.g. `[Reset] [Apply]`. `add<W>()` children, `setWeights({...})` for relative widths (default equal). |
| `CollapsingSection(title, openInitially)` | Collapsible group — `add<W>()` children. This is how a 40-parameter panel stays usable. |

`RadioGroup` is not a widget — it's shared selection state you own alongside (or ahead
of) the buttons that reference it, and it must outlive them:

```cpp
lvgui::RadioGroup quality;
panel->add<lvgui::RadioButton>("Low", &quality, 0);
panel->add<lvgui::RadioButton>("Medium", &quality, 1);
panel->add<lvgui::RadioButton>("High", &quality, 2);
quality.setValue(1);
quality.setOnChange([](int v){ /* ... */ });
```

Two `Slider`/`DragValue` interactions worth knowing about, since neither is visually
obvious: **Shift+drag** slows the drag to 0.1x for fine adjustment, and **Ctrl+click**
drops into an inline text box for typing an exact value (Enter commits, Escape reverts).

### Two callbacks, and when to use which

Value widgets (`Slider`, `DragValue`, `Checkbox`, `TextBox`, `DropDown`, ...) offer
`setOnChange` (fires on every intermediate change, e.g. every frame of a drag) and
`setOnCommit` (fires once, on release/Enter/focus-loss). Use `onChange` for something
cheap like updating a uniform; use `onCommit` for anything expensive, like rebuilding a
mesh. Wire both if you need a live number on screen but only want to act once the user
settles on a value.

## Themes

```cpp
gui.theme() = lvgui::Theme::light();          // or Theme::highContrast(), or Theme::dark()
```

`theme()` returns a mutable reference — assign a whole preset, or tweak individual
fields (`gui.theme().accent = lvgui::Color::fromHex(0xff8800);`) for a one-off brand
color. Changes apply on the next frame; no rebuild step. `highContrast()` is the closest
thing to an accessibility affordance currently available (pure black/white, thicker
borders) — there's no screen-reader support.

## Threading

The GUI is main-thread only. If your simulation runs on a worker thread and needs to
push a value into a widget, marshal it:

```cpp
gui.postToMainThread([&, value]{ readout->setText(std::to_string(value)); });
```

Queued calls run at the start of the next GUI frame, on the main thread, before that
frame's widgets are updated.

## What's not there yet

- No CJK / complex-script shaping or IME; the baked font covers Latin-1 + Greek (enough
  for scientific notation like `α`, `θ`, `ω`) plus arrows and a middle dot.
- No accessibility tree / screen reader support.
- No docking, tabs, or multi-viewport — panels float, drag, collapse, and resize; that's
  the whole layout model.
- `GuiCreateInfo` (custom font path/size, initial theme) isn't yet exposed through
  `VkApp::init()` — every app currently gets the bundled Inter font at 14px, dark theme
  by default. Switch themes at runtime with `gui.theme() = ...` in the meantime.

## Reference

`examples/gui_demo.cpp` exercises every widget in one running program — the fastest way
to see what something looks like before wiring it into your own panel. The full
behavioural spec (interaction rules, drawing recipes, the layout algorithm, theme token
table) is in [`gui/05-widgets.md`](gui/05-widgets.md) and
[`gui/06-layout-and-theme.md`](gui/06-layout-and-theme.md).
