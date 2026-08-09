# 04 — Input and events

## InputState

The GUI never calls GLFW. It consumes a plain struct, which is what makes it testable and
what would let somebody swap in SDL later without touching layers 1–3.

```cpp
enum class MouseButton { Left = 0, Right = 1, Middle = 2, Count = 3 };

struct KeyEvent {
    int  key;        // GLFW-compatible key code (see KeyCodes.h — our own mirror)
    int  mods;       // bitmask: Shift=1, Ctrl=2, Alt=4, Super=8
    bool pressed;    // false = released
    bool repeat;
};

struct InputState {
    Vec2  mousePos       {0, 0};   // logical pixels, origin top-left
    Vec2  mousePosPrev   {0, 0};
    Vec2  mouseDelta     {0, 0};
    Vec2  mouseDownPos[3];         // where each button went down — for drag thresholds

    bool  mouseDown    [3] {};     // level: currently held
    bool  mousePressed [3] {};     // edge: went down this frame
    bool  mouseReleased[3] {};     // edge: came up this frame
    float mouseDownDuration[3] {}; // seconds held; -1.0f when up

    float wheelDelta {0};          // vertical, positive = away from user

    std::vector<uint32_t> charQueue;  // Unicode codepoints from text input
    std::vector<KeyEvent> keyQueue;   // ordered key transitions

    int   modsDown {0};            // level: bitmask of Mod:: flags currently held

    float deltaTime {0};
    Vec2  displaySize {0, 0};      // logical size
    float contentScale {1.0f};
};
```

**Deviation added in phase 6:** `modsDown` was not in the original struct above. Phase 6's
Shift-drag (Slider, DragValue) and Ctrl-click hooks need to know whether a modifier is
*currently* held across many frames of an ongoing drag, which `keyQueue`'s edge-only
events cannot answer on frames with no new key transitions. `UiPlatformGlfw::injectKey`
now also writes `mods` into `m_pending.modsDown` (GLFW always reports the *full* current
modifier bitmask on every key event, not just a delta for the key involved, so
last-writer-wins is correct), and `beginFrame` copies it into `current` unconditionally —
unlike `keyQueue`/`charQueue` it is never cleared, since it is level state, not a queue.

Level flags and edge flags are both needed. A button widget fires on release-inside
(level plus edge); a slider drags on level; a checkbox toggles on press-edge. Deriving
edges inside widgets from level state alone requires each widget to keep its own previous
state, which is exactly the duplication the context should absorb.

`mouseDownPos` enables a drag threshold: a panel title bar should not start moving on a
1-pixel jitter during a click. Use 4 logical pixels.

## GLFW plumbing

Five callbacks. All of them **must chain** to whatever the library already installed, or
you will break the existing camera controls.

```cpp
// In UiPlatformGlfw.cpp
void installCallbacks(GLFWwindow* window, GuiContext* ctx) {
    auto* prev = new PreviousCallbacks{
        glfwSetMouseButtonCallback(window, &onMouseButton),
        glfwSetCursorPosCallback  (window, &onCursorPos),
        glfwSetScrollCallback     (window, &onScroll),
        glfwSetKeyCallback        (window, &onKey),
        glfwSetCharCallback       (window, &onChar),
    };
    // store `prev` and `ctx` in the window user pointer struct
}
```

Every handler does its own work **and then unconditionally calls the previous callback**.
Do not conditionally suppress the chain based on `wantsMouse()` — the camera must still
see mouse-up events even if the press was consumed by the GUI, or a right-drag that
starts on a panel and ends on the scene leaves the camera stuck in look mode forever.

Suppression happens one level up, in the camera's own per-frame update, not in the
callback chain. That is decision D7 and it is the single most common way this integration
goes wrong.

### Callback bodies

- **`onCursorPos`** — write into `pending.mousePos`. Convert from GLFW's coordinate
  space, which is already top-left-origin logical pixels on all platforms. If the
  window is on a HiDPI display, `glfwGetCursorPos` returns logical (screen) coordinates
  while `glfwGetFramebufferSize` returns physical. Keep the GUI in logical throughout
  and scale only at the projection matrix. Getting this backwards produces a UI where
  the cursor and the hover highlight are offset by a factor of two on a Retina display.
- **`onMouseButton`** — append to a pending press/release queue; do not write level
  state directly, or a press-and-release inside a single frame is lost entirely. This
  happens with high-polling-rate mice more often than you would expect.
- **`onScroll`** — accumulate `yoffset` into `pending.wheelDelta`; reset at
  `beginFrame`.
- **`onKey`** — append a `KeyEvent`. Translate GLFW mods into our own bitmask so layers
  1–3 never include a GLFW header.
- **`onChar`** — append the codepoint to `charQueue`. This is the *only* source of text
  for `TextBox`. Never synthesise characters from key events; that breaks every keyboard
  layout that is not US QWERTY.

### Clipboard

```cpp
struct PlatformHooks {
    std::function<std::string()>            getClipboardText;
    std::function<void(std::string_view)>   setClipboardText;
    std::function<void(CursorShape)>        setCursorShape;
};
```

Injected into `GuiContext` at construction; backed by `glfwGetClipboardString`,
`glfwSetClipboardString`, and `glfwSetCursor` in layer 5. Layers 1–3 see only the
`std::function`s. Tests supply no-ops.

`CursorShape` values needed: `Arrow`, `TextInput` (over a text box), `ResizeEW`,
`ResizeNS`, `ResizeNWSE` (panel resize grip), `Hand`. GLFW provides standard cursors for
all of these.

## Hit testing and dispatch

Run at the top of `GuiContext::update()`:

```
if activeId != 0:
    # A widget has mouse capture. It gets everything, wherever the cursor is.
    hoveredId = activeId
    hoveredPanel = panel owning activeId
else:
    hoveredPanel = nullptr
    for panel in panels front-to-back:
        if not panel.visible: continue
        if panel.rect.contains(mousePos):
            hoveredPanel = panel
            break                       # topmost wins; panels are opaque
    hoveredId = hoveredPanel ? hoveredPanel->hitTest(mousePos) : 0
```

`Panel::hitTest` checks its title bar and resize grip first, then walks visible,
enabled widgets in reverse declaration order, testing each widget's laid-out rect
intersected with the panel's content clip rect. A widget scrolled out of view must not
be hittable even though its stored rect might still contain the cursor — intersect with
the clip rect, always.

An open dropdown popup is tested **before** any panel, since it floats above everything.

### Capture

```
on mousePressed[Left]:
    if hoveredId != 0 and widget accepts capture:
        activeId  = hoveredId
        focusedId = hoveredId if widget acceptsFocus() else 0
    else if hoveredPanel and press was on its title bar:
        beginPanelDrag()
    else:
        focusedId = 0                 # click on empty scene defocuses

on mouseReleased[Left]:
    activeId = 0
    endPanelDrag()
```

Capture is what makes drags work across widget boundaries. Test it explicitly: press on
a slider, move the cursor 500 pixels beyond the panel, and confirm the value still
tracks.

## Keyboard

Keyboard events go to `focusedId` only. Two exceptions handled by the context itself
before dispatch:

- **Tab / Shift-Tab** — move focus to the next/previous focusable widget within the
  focused widget's panel, wrapping. If nothing is focused, Tab focuses the first
  focusable widget in the topmost panel.
- **Escape** — if a popup is open, close it; else clear focus.

### Ordering: the context's own Escape/Tab handling runs BEFORE the widget's

The per-frame sequence in [01-architecture.md](01-architecture.md) is normative here and
worth restating precisely, because it is easy to read "the focused widget should get
first refusal on Escape" into this document and implement the opposite of what step 3
actually says:

```
3.  gui.update()
    d. dispatch keyboard events to focusedId; handle Tab / Shift-Tab focus cycling
    e. for each panel front-to-back: panel->update(ctx) → widget->update(ctx)
```

**3.d runs before 3.e.** By the time a widget's own `update()` executes, `GuiContext`
has *already* applied its Escape default (`focusedId` cleared) and its Tab default
(`focusedId` moved to the next focusable widget) for this frame. A widget cannot get a
"first look" at the raw event before the context's default fires — the context sees
every key event first, unconditionally, because that dispatch is baked into its own
`update()` before panels are touched at all.

This matters for any widget whose Escape/Tab behaviour is more than "lose focus" —
`TextBox` (Escape must **revert** the edit, not merely lose focus with the typed text
still committed) is the first case, and phase 8's `DropDown` (Escape must close the
popup) is the next. Naively comparing `ctx.focusedId() == id()` inside `update()` cannot
distinguish "I still have focus, nothing happened" from "I had focus a moment ago and
Escape just took it away, unlocked by GuiContext before I even ran this frame" — both
read as `ctx.focusedId() != id()` by the time the widget can look.

**The fix: compare against the *entering* focus state, not the current one.** Have the
widget remember, at the end of every `update()` call, whether it was focused as of that
call (`m_wasFocused` in `TextBox`). On the next frame, that remembered value is the
focus state *as it was before this frame's context-level Escape/Tab handling ran* —
because nothing else can have changed `focusedId` between frames. So:

```
enteringFocused = m_wasFocused          // focus state BEFORE this frame's Escape/Tab default
hasFocusNow     = ctx.focusedId() == id()   // focus state AFTER it

if enteringFocused:
    process this frame's key queue as normal (Escape included) — the widget still
    sees every key event regardless of what the context already did to focusedId,
    because events are read from ctx.input().keyQueue, not gated on hasFocusNow
if enteringFocused and !hasFocusNow and the widget's own Escape branch did NOT run:
    an external cause (click elsewhere, Tab cycling past this widget) took focus —
    this is the "commit on focus loss" case, not the "revert on Escape" case
```

`enteringFocused` is what lets the widget tell "Escape fired" (revert, no commit) apart
from "focus moved elsewhere for any other reason" (commit, no revert) — both of which
look identical if you only ever ask `ctx.focusedId()` in the present tense. See
`TextBox::update()` (`src/ui/widgets/TextBox.cpp`) for the worked implementation;
`SliderT`/`DragValueT`'s inline Ctrl-click text entry sidesteps the problem entirely by
never routing its embedded `TextBox` through `ctx.focusedId()` at all (`setForcedFocus`,
`updateEmbedded`) and inspecting the key queue directly instead.

**Phase 8 resolution:** it turned out `DropDown` needs neither the entering-state pattern
nor a plain `ctx.focusedId() == id()` check. Instead, `GuiContext`'s own Escape default
(below) was changed to close the popup itself, before `DropDown::update()` ever runs:
`if (isPopupOpen()) closePopup(); else m_focusedId = kInvalidWidgetId;` — i.e. "if a
popup is open, close it; else clear focus" is now literally what this branch does, rather
than something left to the widget. Since GuiContext is the one source of truth for "is a
popup open" (`ctx.popupOwner() == this`), and `DropDown::update()` just reads that fresh
every frame, there is no second cause of the popup closing that it ever needs to
distinguish Escape from — a click outside the popup is also handled entirely inside
`GuiContext::update()`, before `DropDown::update()` runs. See docs/gui/05-widgets.md,
"DropDown", for the full mechanism.

### Key codes

Do not put GLFW's `GLFW_KEY_*` constants into layer-3 headers. Mirror the handful you
need in `include/lightVulkanGraphics/ui/KeyCodes.h`:

```cpp
namespace lightGraphics::ui::Key {
    enum : int {
        Unknown = -1,
        Backspace, Delete, Tab, Enter, Escape,
        Left, Right, Up, Down, Home, End, PageUp, PageDown,
        A, C, V, X, Z, Y,          // for the standard editing shortcuts
        Count
    };
}
```

Translation from GLFW happens in layer 5. This keeps layers 1–3 free of GLFW and makes
the test harness able to synthesise keystrokes without linking a windowing library.

## The camera hand-off

This belongs in the core library's per-frame update, wherever the camera is currently
driven:

```cpp
#ifdef LVG_WITH_UI
    const bool uiHasMouse    = m_gui && m_gui->wantsMouse();
    const bool uiHasKeyboard = m_gui && m_gui->wantsKeyboard();
#else
    const bool uiHasMouse = false, uiHasKeyboard = false;
#endif

if (!uiHasMouse) {
    updateOrbitAndLook(deltaTime);   // right-drag look, scroll zoom/dolly
}
if (!uiHasKeyboard) {
    updateFreeFly(deltaTime);        // WASD, Q/E, Shift
}
```

Two behaviours to get right beyond the basic guard:

1. **Do not interrupt an in-progress camera drag.** If the user began a right-drag on
   the scene and then swept the cursor across a panel, the look should continue. Track
   `cameraDragActive` and skip the `uiHasMouse` check while it is true. Symmetrically,
   `wantsMouse()` returning true for an active GUI drag already protects the reverse
   case.
2. **Scroll wheel.** If the cursor is over a scrollable panel, the wheel scrolls the
   panel; otherwise it zooms the camera. `wantsMouse()` covers this correctly since it
   is true whenever the cursor is over a panel.

## Frame-boundary bookkeeping

`beginFrame` must, in order:

1. Drain the cross-thread callback queue
2. Move `pending` input into `current`
3. Compute `mouseDelta = mousePos - mousePosPrev`
4. Compute edge flags from the queued press/release events
5. Update `mouseDownDuration`
6. Clear `pending.charQueue`, `pending.keyQueue`, `pending.wheelDelta`

`endFrame` must:

1. Copy `mousePos` into `mousePosPrev`
2. Clear `mousePressed` / `mouseReleased` arrays
3. Reset the requested cursor shape to `Arrow` for the next frame

Missing step 2 of `endFrame` gives you a button that fires every frame while held — a
bug that looks like a debounce problem and is actually a lifecycle problem.
