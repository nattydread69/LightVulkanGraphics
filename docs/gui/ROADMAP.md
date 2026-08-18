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
contact with the hard parts, and there are 23 widgets covering the common cases. UTF-8
caret handling, capture and focus resolution, DPI rebaking, and the two-pass layout
convergence all work and have tests pinning them down.

Every item from the original Tier 1 (colour picker, `GuiCreateInfo` through `init()`,
layout persistence, texture support) and everything promoted from the old Tier 2 into
its place after that (`ListBox`, context menus, `LogView`, the `GuiContext` font search
order, and finally the modal dialog — the one item in that batch that needed a real
change to the input hand-off, `wantsMouse()`/`wantsKeyboard()`/`wantsScroll()`, rather
than being just a new `Widget` subclass) has shipped and moved into the numbered docs.
What was Tier 3 is promoted below.

## Tier 1 — larger, pick at most one

### Transfer-function editor

Given the volume-rendering work elsewhere in the library, a draggable-control-point
opacity and colour editor is the most *differentiating* thing on this list — it is the
widget that domain lives on, and no general-purpose GUI toolkit ships one. `ColorEdit`
(05-widgets.md) covers picking a single colour; this is a different, larger
control-point editor built on the same HSV/gradient primitives, and its gradient strip
is exactly what `Image`/`registerUiTexture()` (05-widgets.md) now makes straightforward
to draw — nothing about it is blocked anymore, it is just still the largest single item
here.

### Second font face or size

There is one `Font`, baked once, at one size. That means no headings and no monospace
column for numeric readouts — and for scientific data, proportional digits make columns
visibly jitter as values change. Fixing it means either a second atlas or multi-range
packing into the existing one — the multi-texture plumbing `Image` added (`UiRenderer`'s
per-`DrawCmd` descriptor selection, docs/gui/02-rendering.md) means a second atlas, if
that turns out to be the simpler of the two, is no longer a new mechanism to build.

### Anti-aliased edges

Listed as a deliberate v1 non-goal in [00-overview.md](00-overview.md), and rounded
corners currently stair-step. Purely cosmetic — but screenshots are how a library gets
adopted, so it is not worth nothing.

## Explicit non-goals

**Docking, tabs across panels, and multi-viewport.** These stay non-goals. They are where
GUI libraries go to die, they would invalidate the floating-panel model that currently
works, and the target user wants a parameter panel beside a scene, not an IDE. (`TabBar`
(05-widgets.md), grouping content *within* one panel, is a different and much smaller
thing, and already shipped.)

**Accessibility tree / screen reader support.** Still a real gap and still honestly
declared as one in [00-overview.md](00-overview.md). It is out of scope not because it
does not matter but because doing it properly means a platform-accessibility bridge on
every target, which is a larger project than the rest of this document combined.

**CJK and complex-script shaping, IME, bidirectional text.** Same reasoning: correct
implementations mean HarfBuzz-class text shaping, which contradicts the zero-external-
dependency goal that shapes the whole design.
