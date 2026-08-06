# LVGUI — Vulkan-native GUI for Light Vulkan Graphics

Design documentation for `lightGraphics::ui` (shorthand **LVGUI**), a retained-mode
GUI layer rendered directly through the existing Vulkan pipeline of
`LightVulkanGraphics`.

These documents are written to be read by both a human and by Claude Code. They are
specifications, not tutorials: every file states what must exist, what it must do, and
how to tell whether it works.

## Reading order

| # | Document | What it settles |
|---|----------|-----------------|
| 00 | [Overview](00-overview.md) | Goals, non-goals, the eight decisions everything else follows from |
| 01 | [Architecture](01-architecture.md) | Layers, ownership, the per-frame sequence |
| 02 | [Rendering](02-rendering.md) | Draw list, vertex format, pipeline, shaders, scissor batching |
| 03 | [Text and fonts](03-text-and-fonts.md) | stb_truetype atlas, glyph metrics, measurement, DPI |
| 04 | [Input and events](04-input-and-events.md) | GLFW plumbing, hover/active/focus, capture, camera hand-off |
| 05 | [Widgets](05-widgets.md) | Full behavioural spec for every widget |
| 06 | [Layout and theme](06-layout-and-theme.md) | Row layout, label/control split, theme tokens |
| 07 | [Public API](07-public-api.md) | Header sketches — the contract Claude Code implements against |
| 08 | [Implementation plan](08-implementation-plan.md) | 11 phases, file manifest, acceptance criteria |
| 09 | [Claude Code prompts](09-claude-code-prompts.md) | Copy-paste prompts, one per phase |
| 10 | [Testing](10-testing.md) | Headless test strategy, what to test and what not to |

## Where these files go

Copy the whole `docs/gui/` directory into the repository root of
`LightVulkanGraphics`. It sits alongside the existing `docs/rotation_glyphs.md`.

Then work through [09-claude-code-prompts.md](09-claude-code-prompts.md) one phase at a
time, in order. Each phase ends with something you can build and run.

## The one-paragraph summary

LVGUI builds a flat list of textured, coloured triangles on the CPU each frame
(`DrawList`), then plays that list back through a single Vulkan pipeline with one
descriptor set, one alpha-blended draw per scissor rectangle. Text comes from an
`stb_truetype`-baked R8 atlas that also contains a solid white block, so solid fills and
glyphs share one texture and one pipeline. Widgets are retained objects owned by
`Panel`s, which are owned by a `GuiContext` hanging off `LightVulkanGraphics`. Interaction
uses the hovered/active/focused ID model. Everything above the Vulkan backend is
headless-testable.
