# LVGUI — Vulkan-native GUI for Light Vulkan Graphics

Architecture and design documentation for `lightGraphics::ui` (shorthand **LVGUI**), a
retained-mode GUI layer rendered directly through LightVulkanGraphics's existing Vulkan
pipeline.

These are specifications, not a tutorial: each file states what a part of the system
does, why it's built that way, and how to tell whether it's working. They're aimed at
anyone extending or maintaining LVGUI itself.

**Just want to use the finished library in your own app?** See
[`../gui_usage.md`](../gui_usage.md) instead — a task-focused how-to, not a spec.

## Reading order

| # | Document | What it covers |
|---|----------|-----------------|
| 00 | [Overview](00-overview.md) | Goals, non-goals, the eight decisions everything else follows from |
| 01 | [Architecture](01-architecture.md) | Layers, ownership, the per-frame sequence |
| 02 | [Rendering](02-rendering.md) | Draw list, vertex format, pipeline, shaders, scissor batching |
| 03 | [Text and fonts](03-text-and-fonts.md) | stb_truetype atlas, glyph metrics, measurement, DPI |
| 04 | [Input and events](04-input-and-events.md) | GLFW plumbing, hover/active/focus, capture, camera hand-off |
| 05 | [Widgets](05-widgets.md) | Full behavioural spec for every widget |
| 06 | [Layout and theme](06-layout-and-theme.md) | Row layout, label/control split, theme tokens |
| 07 | [Public API](07-public-api.md) | Header layout and the public surface |
| 08 | [Testing](08-testing.md) | Headless test strategy, what to test and what not to |

## The one-paragraph summary

LVGUI builds a flat list of textured, coloured triangles on the CPU each frame
(`DrawList`), then plays that list back through a single Vulkan pipeline with one
descriptor set, one alpha-blended draw per scissor rectangle. Text comes from an
`stb_truetype`-baked R8 atlas that also contains a solid white block, so solid fills and
glyphs share one texture and one pipeline. Widgets are retained objects owned by
`Panel`s, which are owned by a `GuiContext` that the core library constructs and holds.
Interaction uses the hovered/active/focused ID model. Everything above the Vulkan
backend is headless-testable.
