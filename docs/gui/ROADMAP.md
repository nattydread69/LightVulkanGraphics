# Roadmap

Where LVGUI stands today and what is worth building next, in priority order. This is a
planning document, not a spec — anything listed here that gets built moves into the
numbered documents beside it, and the entry here goes away.

The organising question throughout is the one from
[00-overview.md](00-overview.md): the target user is building a scientific
visualisation or simulation tool and wants a parameter panel beside their scene.
Features are ranked by how much they help that person, not by how interesting they are
to implement.

## Where things stand

The foundation is in place and tested. Layering holds (`InputState` → `DrawList`/`Font` →
`Widget`/`Panel`/`GuiContext` → `UiRenderer`), the headless-testability goal survived
contact with the hard parts, and there are 16 widgets covering the common cases. UTF-8
caret handling, capture and focus resolution, DPI rebaking, and the two-pass layout
convergence all work and have tests pinning them down.

What is missing is mostly breadth — widget count and texture support — rather than depth
in what already exists.

## Tier 1 — highest value

### Colour picker (`ColorEdit3` / `ColorEdit4`)

The single most-requested widget in visualisation tooling, and right now a consumer who
wants to tint an isosurface has to hand-build three sliders. Everything needed already
exists: `Vec3Field` is the structural template, `DropDown`'s popup machinery gives the
expanded picker somewhere to live, and `DrawList::addRectFilledMultiColor` already draws
the saturation/value square.

Design questions to settle first: RGB vs. HSV as the stored representation (HSV makes the
picker natural and RGB makes the binding natural — probably store RGB, convert at the
edges), and whether alpha is a separate widget or a flag.

### Thread `GuiCreateInfo` through `VkApp::init()`

Every app currently gets the bundled Inter font at 14px and the dark theme, hardcoded in
`VkApp::initUi()`. A consumer can swap the theme after construction via `gui().theme()`,
but cannot choose a font, a font size, or an atlas size at all. This is the most visible
gap between what `GuiContext` supports and what the integration actually exposes, and it
is a small change.

Worth doing at the same time: give `GuiCreateInfo::fontPath` a real search order so it can
be left empty, mirroring the existing relocatable `spv/` payload resolution
(`LIGHT_VULKAN_GRAPHICS_SHADER_PATH`, build tree, library-relative, executable-relative)
under a `fonts/` sibling. Today an empty `fontPath` throws.

### Texture support in the draw list

`DrawCmd::textureId` exists and is always zero. `UiRenderer` binds one descriptor set once,
outside the per-command loop, so the field is currently a placeholder rather than a
feature. Honouring it — via a descriptor array indexed by `textureId`, or a rebind between
commands — unblocks an `Image` widget, and with it colormap legends, render-target
thumbnails, and 2D slice previews. For a volume-rendering-adjacent library these are not
nice-to-haves.

This is the one Tier 1 item that touches the Vulkan backend, so it is also the one whose
cost is hardest to estimate from the outside.

### Layout persistence

Save and restore panel position, size, collapse state, and scroll offset between runs.
Users who drag panels into place expect them to stay there, and every piece of state
involved is trivially serialisable. The only real design decision is the panel key:
title strings are the obvious choice but are neither stable nor unique, so this probably
wants an explicit consumer-supplied id.

## Tier 2 — natural next widgets

- **`ListBox` / selectable list.** Picking a timestep, a dataset, a light. `DropDown` is
  the wrong shape once the list is long or the selection is the point rather than a
  setting.
- **Context menu (right-click).** `GuiContext::openPopup()` is already general and
  `DropDown` is its only user; a context menu is largely a new `Widget` that drives the
  existing machinery.
- **Read-only log / console view.** A scrollable, wrapping, appendable text region for
  solver output. `Label` wraps but there is no independently scrolling text area.
- **Tab bar within a panel.** `CollapsingSection` is currently the only grouping
  primitive, and a panel with forty parameters needs more than vertical stacking.
- **Modal dialog with a blocking overlay.** Confirmations and "unsaved changes". Needs a
  genuine addition to the input hand-off: a modal has to swallow everything beneath it,
  which `wantsMouse()`/`wantsKeyboard()` do not currently express.

## Tier 3 — larger, pick at most one

### Transfer-function editor

Given the volume-rendering work elsewhere in the library, a draggable-control-point
opacity and colour editor is the most *differentiating* thing on this list — it is the
widget that domain lives on, and no general-purpose GUI toolkit ships one. It depends on
the colour picker and on texture support (for the gradient strip), so it belongs after
Tier 1 rather than instead of it.

### Second font face or size

There is one `Font`, baked once, at one size. That means no headings and no monospace
column for numeric readouts — and for scientific data, proportional digits make columns
visibly jitter as values change. Fixing it means either a second atlas or multi-range
packing into the existing one; either way it overlaps with the texture work above.

### Anti-aliased edges

Listed as a deliberate v1 non-goal in [00-overview.md](00-overview.md), and rounded
corners currently stair-step. Purely cosmetic — but screenshots are how a library gets
adopted, so it is not worth nothing.

## Explicit non-goals

**Docking, tabs across panels, and multi-viewport.** These stay non-goals. They are where
GUI libraries go to die, they would invalidate the floating-panel model that currently
works, and the target user wants a parameter panel beside a scene, not an IDE. (A tab bar
*within* one panel, in Tier 2, is a different and much smaller thing.)

**Accessibility tree / screen reader support.** Still a real gap and still honestly
declared as one in [00-overview.md](00-overview.md). It is out of scope not because it
does not matter but because doing it properly means a platform-accessibility bridge on
every target, which is a larger project than the rest of this document combined.

**CJK and complex-script shaping, IME, bidirectional text.** Same reasoning: correct
implementations mean HarfBuzz-class text shaping, which contradicts the zero-external-
dependency goal that shapes the whole design.
