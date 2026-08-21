# 01 — Architecture

## Layers

Five layers, each depending only on those below it. The dependency direction is strict:
**nothing below layer 4 may include a Vulkan header.** That rule is what makes the
headless tests possible.

```
┌─────────────────────────────────────────────────────────────┐
│ 5  Integration                                              │
│    LightVulkanGraphics::gui() ; GLFW callback chaining ;    │
│    camera input suppression ; CMake target & install        │
├─────────────────────────────────────────────────────────────┤
│ 4  Vulkan backend            ← ONLY layer that sees Vulkan  │
│    UiRenderer: pipeline, atlas image, per-frame buffers,    │
│    DrawList playback into a VkCommandBuffer                 │
├─────────────────────────────────────────────────────────────┤
│ 3  Widgets & panels                                         │
│    GuiContext, Panel, Widget and all concrete widgets,      │
│    focus/hover/active resolution, layout                    │
├─────────────────────────────────────────────────────────────┤
│ 2  Text                                                     │
│    Font: atlas baking (stb_truetype), glyph lookup,         │
│    UTF-8 decode, measureText                                │
├─────────────────────────────────────────────────────────────┤
│ 1  Primitives                                               │
│    Vec2, Rect, Color, Theme tokens, DrawList, DrawCmd,      │
│    UiVertex, InputState                                     │
└─────────────────────────────────────────────────────────────┘
```

Layer 2 needs a font file but no GPU. Layer 3 needs layer 2 for measurement but produces
only a `DrawList`. A test can construct a `GuiContext`, feed it synthetic `InputState`,
step it, and assert on the resulting geometry and on widget values — all in a CI job with
no display and no driver.

## Ownership

```
LightVulkanGraphics
 └── std::unique_ptr<ui::GuiContext>        (created iff LVG_WITH_UI)
      ├── ui::Theme                          (value)
      ├── ui::Font                           (value; owns atlas pixels + glyph table)
      ├── ui::DrawList                       (value; cleared and refilled each frame)
      ├── ui::DrawList overlayList           (value; popups, tooltips — drawn last)
      ├── std::vector<std::unique_ptr<Panel>> panels   (front-to-back z-order)
      └── InteractionState  { hoveredId, activeId, focusedId, ... }

Panel
 ├── Rect bounds, title, flags (movable/resizable/collapsible/scrollable)
 ├── std::vector<std::unique_ptr<Widget>> widgets   (declaration order = layout order)
 └── scroll offset, content height

UiRenderer                                   (owned by the Vulkan side, not by GuiContext)
 ├── VkPipeline, VkPipelineLayout, VkDescriptorSetLayout, VkDescriptorPool
 ├── atlas VkImage / VkImageView / VkSampler / VkDeviceMemory
 └── FrameBuffers[N]  { vertex + index, host-visible, persistently mapped, grow-on-demand }
```

`GuiContext` never touches `UiRenderer` and vice versa. The renderer is handed a
`const DrawList&` and a command buffer. That is the entire interface between layers 3 and 4.

## Per-frame sequence

This is the contract. Everything is single-threaded on the main thread.

```
1.  Poll platform events
      glfwPollEvents()
      → GLFW callbacks have already appended into GuiContext's pending InputState
        (mouse position, buttons, wheel, characters, key events)

2.  gui.beginFrame(framebufferSize, contentScale, deltaTime)
      - swap pending input into current InputState, compute deltas & edge flags
      - clear the pending queues
      - rebake the font atlas if contentScale changed  (rare; flags the renderer)

3.  gui.update()
      a. menuBar.update(ctx) FIRST -- MenuBar (docs/gui/05-widgets.md, "MenuBar") isn't
         a Widget or a Panel, so it gets its own small hit-test/open-close state machine,
         resolved before anything below; while its row or an open dropdown is under the
         cursor, the panel hit-test walk in (b) is skipped entirely for this frame
      b. hit test: walk panels front-to-back, find topmost panel containing the cursor
      c. if activeId != 0, that widget keeps the mouse regardless of position
      d. resolve hoveredId
      e. dispatch keyboard events to focusedId; handle Tab / Shift-Tab focus cycling
      f. for each panel front-to-back: panel->update(ctx) → widget->update(ctx)
         (widgets fire their callbacks here, inside this call, synchronously)
      g. reorder z if a panel title bar was clicked

4.  Host application reads widget values / has already had its callbacks fired.
      Camera update MUST be guarded:
          if (!gui.wantsMouse())    camera.handleMouse(...);
          if (!gui.wantsKeyboard()) camera.handleKeys(...);

5.  gui.endFrame()
      - run layout for every visible panel (positions all widgets)
      - clear both DrawLists
      - for each panel BACK-to-front: panel->draw(drawList)
      - draw overlay content (menu bar row + its open dropdown, modal backdrop, open
        popup, tooltip) into overlayList -- the menu bar draws even while non-
        interactive behind an open modal, so it stays visually present throughout
      - append overlayList onto drawList  → final ordered command stream

6.  Scene rendering as today (3D pass)

7.  uiRenderer.record(cmd, gui.drawList(), frameIndex)
      - upload vertices/indices into this frame's persistently-mapped buffers
      - bind pipeline + descriptor set once
      - for each DrawCmd: vkCmdSetScissor, vkCmdDrawIndexed

8.  Submit / present as today
```

Two ordering details that will cause bugs if reversed:

- **Update before layout.** A widget's `update()` may change its own preferred size
  (a collapsing section toggling open). Laying out first would show it one frame late.
- **Panels draw back-to-front, but update front-to-back.** Drawing back-to-front puts
  the focused panel on top visually; updating front-to-back means the topmost panel
  gets first refusal on the cursor.

## Widget identity

Every widget gets a `WidgetId` — a `uint64_t` from a monotonic counter in `GuiContext`,
assigned at construction. IDs are never reused within a session. `0` is the reserved
null ID.

Interaction is three IDs held by the context:

| ID | Meaning | Set when | Cleared when |
|----|---------|----------|--------------|
| `hoveredId` | cursor is over this widget and no other widget has capture | each frame during hit test | each frame |
| `activeId` | widget has captured the mouse (mid-drag) | mouse pressed on a widget | mouse released |
| `focusedId` | widget receives keyboard events | click, or Tab navigation | click elsewhere, Escape, or Tab away |

`activeId` capture is the mechanism that makes sliders usable. Once you press on a slider
handle, you must be able to drag the cursor far outside the panel — even outside the
window — and keep controlling it. A hit-test-per-frame model without capture produces a
slider that drops the moment your hand wobbles.

## The `wantsMouse` / `wantsKeyboard` contract

```cpp
bool GuiContext::wantsMouse() const {
    return activeId != 0                  // mid-drag, even if cursor left the panel
        || hoveredPanel != nullptr        // cursor over any panel's rect
        || popupOpen;                     // a dropdown is expanded
}

bool GuiContext::wantsKeyboard() const {
    return focusedId != 0 && focusedWidgetAcceptsTextInput;
}
```

Note the asymmetry: hovering a panel takes the mouse, but merely focusing a button does
not take the keyboard. Only widgets that consume character input (`TextBox`, and a
`Slider` in ctrl-click text-entry mode) claim it. Otherwise a user who clicks a checkbox
would find WASD dead until they clicked the background.

## Threading

The GUI is main-thread only. If a consumer runs their simulation on a worker thread and
wants to push values into a widget, they must marshal. Provide a minimal helper:

```cpp
gui.postToMainThread([&]{ readout->setText(formatted); });
```

backed by a mutex-guarded `std::vector<std::function<void()>>` drained at the top of
`beginFrame()`. Twenty lines, and it prevents a whole class of report you would
otherwise spend months triaging.

## What the core library must expose for this to work

Three changes to `LightVulkanGraphics`, all worth making regardless of the GUI:

1. **`ui::GuiContext& gui()`** — accessor, plus `bool hasGui() const`.
2. **A frame hook or an exposed loop.** The retained model means no per-frame user
   callback is strictly required, but the renderer must call `uiRenderer.record()` at
   the right point inside whatever `run()` does. If you also expose
   `beginFrame()`/`endFrame()` publicly, hosts can drive their own loop — recommended
   independently of the GUI work.
3. **Render pass / dynamic rendering info accessible to the UI renderer.** The UI
   pipeline must be created against the same attachment format and sample count as the
   scene pass. Expose whichever your renderer uses through an internal accessor.
