# 02 — Rendering

## The draw list

`DrawList` is a CPU-side geometry accumulator. It knows nothing about Vulkan. It has
three parallel outputs:

```cpp
struct UiVertex {          // 20 bytes
    glm::vec2 pos;         // screen-space pixels, origin top-left, Y down
    glm::vec2 uv;          // atlas texture coordinates, [0,1]
    uint32_t  color;       // packed RGBA8, little-endian: 0xAABBGGRR
};

struct DrawCmd {
    uint32_t indexOffset;  // into the index buffer
    uint32_t indexCount;
    Rect     clipRect;     // in framebuffer pixels
    uint32_t textureId;    // 0 = font atlas. Reserved for a future second texture (e.g.
                            // an addImage primitive); always 0 today -- v1 has no such
                            // primitive, and everything routes through the one atlas.
};

class DrawList {
    std::vector<UiVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<DrawCmd>  m_commands;
    std::vector<Rect>     m_clipStack;
};
```

A new `DrawCmd` is opened whenever the clip rect or texture changes; otherwise geometry
accumulates into the current command. A frame with four panels, each clipping its own
content region, produces roughly nine commands — panel chrome, panel content, times four,
plus one overlay.

### Primitive API

```cpp
void clear();

void pushClipRect(const Rect&, bool intersectWithCurrent = true);
void popClipRect();

void addRectFilled  (const Rect&, Color, float rounding = 0.0f);
void addRect        (const Rect&, Color, float thickness = 1.0f, float rounding = 0.0f);
void addRectFilledMultiColor(const Rect&, Color tl, Color tr, Color br, Color bl);
void addLine        (Vec2 a, Vec2 b, Color, float thickness = 1.0f);
void addTriangleFilled(Vec2, Vec2, Vec2, Color);
void addCircleFilled(Vec2 centre, float radius, Color, int segments = 0);
void addConvexPolyFilled(const Vec2* pts, int count, Color);
void addPolyline    (const Vec2* pts, int count, Color, float thickness, bool closed);
void addText        (const Font&, float pixelSize, Vec2 topLeft, Color, std::string_view utf8);
void addTextClipped (const Font&, float pixelSize, const Rect&, Color,
                     std::string_view utf8, Align h, Align v);
```

Every non-text primitive above samples `DrawList::whitePixelUV()` rather than a
hardcoded `{0,0}`, and `DrawList::setWhitePixelUV(Vec2)` lets the owner (`GuiContext`)
point it at the real atlas's white block once a `Font` has baked. Before that call it
defaults to `{0,0}`, which is only correct for tests that never bind a real atlas
texture — solid-colour geometry needs this wired up for correct results once the font
atlas is bound as the single texture every draw call samples.

`addRectFilled` with `rounding == 0` is the hot path and must be exactly four vertices
and six indices with no branching. Rounded rects build a convex fan; precompute a
unit-circle table of 16 points per quadrant at static-init time and scale it.

`addPolyline` is what draws the sparkline widget and the checkbox tick. Implement it as
a quad per segment with mitred joins skipped — at 1–2px thickness nobody can see the
difference, and mitring is where polyline code goes to die.

### Clip stack semantics

`pushClipRect(r, true)` intersects `r` with the current top of stack. This is what panels
use: a panel's content region intersects the panel's own rect, so a widget dragged partly
off-panel clips correctly against both. `pushClipRect(r, false)` replaces — used only by
the overlay list for dropdown popups that must escape their parent panel.

The stack must never be empty. `clear()` pushes a full-framebuffer rect as the base entry.

### Anti-aliasing

Not in v1 (decision D4 in [00-overview.md](00-overview.md)). Instead:

**Snap to integer pixels.** Every rect edge coordinate passes through
`std::round()` before entering the vertex buffer. A 1px border drawn at x=100.0 is crisp;
at x=100.37 it is a grey smear across two columns. This single rule buys most of the
visual quality that AA would, at zero cost.

Circles and the slider handle are the exception — a hard-edged circle at 8px radius looks
bad. Either draw handles as rounded rects (recommended, and it suits a technical
aesthetic) or accept the aliasing.

## The Vulkan pipeline

One graphics pipeline, created once, against the same colour attachment format and sample
count as the scene pass.

| State | Value |
|-------|-------|
| Topology | `TRIANGLE_LIST` |
| Polygon mode | `FILL` |
| Cull mode | `NONE` — draw-list triangles are not consistently wound |
| Front face | irrelevant given cull none |
| Depth test | **disabled** |
| Depth write | **disabled** |
| Blend | enabled, see below |
| Dynamic state | `VIEWPORT`, `SCISSOR` |
| Descriptor sets | 1 set, 1 binding: `COMBINED_IMAGE_SAMPLER` at binding 0, fragment stage |
| Push constants | 16 bytes, vertex stage: `vec2 scale; vec2 translate;` |

Blend state — note the alpha channel differs from the colour channels, which matters if
anything ever reads the framebuffer's alpha:

```
srcColorBlendFactor = SRC_ALPHA
dstColorBlendFactor = ONE_MINUS_SRC_ALPHA
colorBlendOp        = ADD
srcAlphaBlendFactor = ONE
dstAlphaBlendFactor = ONE_MINUS_SRC_ALPHA
alphaBlendOp        = ADD
```

Vertex input, one binding at stride 20:

| Location | Format | Offset |
|----------|--------|--------|
| 0 | `R32G32_SFLOAT` | 0 |
| 1 | `R32G32_SFLOAT` | 8 |
| 2 | `R8G8B8A8_UNORM` | 16 |

`R8G8B8A8_UNORM` as a vertex attribute is normalised to a `vec4` by the hardware for
free. No unpacking in the shader.

## Shaders

Both compile with the existing `glslc` build step into `build/spv/` and install to
`${DATAROOTDIR}/LightVulkanGraphics/spv` alongside your scene shaders.

`shaders/ui.vert`:

```glsl
#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    vec2 scale;
    vec2 translate;
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main() {
    vUV     = inUV;
    vColor  = inColor;
    gl_Position = vec4(inPos * pc.scale + pc.translate, 0.0, 1.0);
}
```

`shaders/ui.frag`:

```glsl
#version 450

layout(set = 0, binding = 0) uniform sampler2D uAtlas;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main() {
    float coverage = texture(uAtlas, vUV).r;
    outColor = vec4(vColor.rgb, vColor.a * coverage);
}
```

The push constants map pixel space to clip space. For a framebuffer of width `W` and
height `H` with origin at top-left:

```cpp
push.scale     = { 2.0f / W, 2.0f / H };
push.translate = { -1.0f, -1.0f };
```

Vulkan's clip space already has +Y downward, so no flip is needed — this is one of the
few places where Vulkan's convention is more convenient than OpenGL's.

## Per-frame buffers

Mirror the pattern already used for instance data: host-visible, persistently mapped,
one set per frame in flight, grown on demand.

```cpp
struct UiFrameBuffers {
    VkBuffer       vertexBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory  = VK_NULL_HANDLE;
    void*          vertexMapped  = nullptr;
    VkDeviceSize   vertexCapacity = 0;
    // ... same four for indices
};
```

Growth policy: when required size exceeds capacity, destroy and recreate at
`std::max(required * 3 / 2, 4096 * sizeof(UiVertex))`. Because the buffer belongs to a
specific frame index and that frame's fence has been waited on before recording, the
destroy is safe without a deferred-deletion queue. This is a real simplification —
take it.

Memory properties: `HOST_VISIBLE | HOST_COHERENT`. Do not attempt a staging-buffer upload
for UI geometry; the data changes completely every frame and is small (a busy frame is
under 100 KB).

## Recording

```cpp
void UiRenderer::record(VkCommandBuffer cmd, const DrawList& list,
                        uint32_t frameIndex, VkExtent2D framebufferExtent)
{
    if (list.indices().empty()) return;

    auto& buf = m_frames[frameIndex];
    ensureCapacity(buf, list.vertices().size(), list.indices().size());
    std::memcpy(buf.vertexMapped, list.vertices().data(), /* bytes */);
    std::memcpy(buf.indexMapped,  list.indices().data(),  /* bytes */);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, ..., 0, 1, &m_descriptorSet, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buf.vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, buf.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    PushConstants pc = makePushConstants(framebufferExtent);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(pc), &pc);

    VkViewport vp{ 0, 0, (float)framebufferExtent.width,
                         (float)framebufferExtent.height, 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &vp);

    for (const DrawCmd& dc : list.commands()) {
        if (dc.indexCount == 0) continue;
        VkRect2D scissor = clampToFramebuffer(dc.clipRect, framebufferExtent);
        if (scissor.extent.width == 0 || scissor.extent.height == 0) continue;
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.indexOffset, 0, 0);
    }
}
```

`clampToFramebuffer` must clamp negative origins to zero **and** shrink the extent
accordingly — a scissor rect with a negative offset is undefined behaviour and the
validation layers will tell you about it in the least helpful way possible. Get this
right the first time.

## Atlas image

- Format `VK_FORMAT_R8_UNORM`, tiling `OPTIMAL`, usage `SAMPLED | TRANSFER_DST`
- Uploaded once at init through a staging buffer, transitioned to
  `SHADER_READ_ONLY_OPTIMAL`
- Sampler: `LINEAR` min/mag, `CLAMP_TO_EDGE` on both axes, no mipmaps, no anisotropy
- Rebuilt only when content scale changes (monitor switch on a mixed-DPI setup)

**Read the dimensions from `font.atlasWidth()` / `font.atlasHeight()`; never hardcode
512.** `Font::bake` retries once at 1024×1024 when the requested atlas cannot fit every
glyph, so the actual size depends on the bake pixel height — a 14px bake fits 512×512,
but a 28px bake (14pt at 2× content scale) comes back 1024×1024. Both the `VkImage`
extent and the staging buffer size (`width * height`, one byte per texel at `R8_UNORM`)
must follow the Font. Hardcoding 512 silently truncates the upload on any HiDPI display.

`CLAMP_TO_EDGE` matters. With `REPEAT`, a glyph at the atlas edge bleeds a sliver of the
opposite edge into itself, producing a faint speckle that is maddening to diagnose.

## Render pass lifetime

The pipeline is **not** create-once. `VkApp::recreateSwapChain()` calls
`createRenderPass()` on every resize, and `cleanupSwapChain()` destroys `renderPass_`
along with every pipeline built against it. `UiRenderer::onRenderPassRecreated()` exists
for this: the owner calls it with the new handle after each swapchain recreation, and it
rebuilds the pipeline. Missing this leaves the UI drawing against a destroyed render
pass the first time the window is resized.

## Target layering

The dependency runs **core -> LightVulkanGraphicsUI**, never the reverse. LVGUI is
self-contained: layers 1-2 are pure glm+std, and `UiRenderer` receives its Vulkan
handles through `UiRendererCreateInfo` rather than reaching into `VkApp`. Having the UI
target link the core library instead would be a cycle, because core has to call into the
UI to record it into the frame and to expose `VkApp::gui()` -- and CMake rejects cycles
unless every target involved is `STATIC`, which the core library is not. A useful side
effect: the headless UI tests link neither Vulkan, assimp nor glfw.

`UiRenderer::clampToFramebuffer` is a public static function purely so it can be unit
tested without a device. It is the most likely source of validation errors in this
backend, so it earns direct coverage rather than being inferred from a clean run.

## Integration point in the frame

Note that `VkApp::recordCommandBuffer` has **two** `vkCmdEndRenderPass` call sites: the
normal one at the end, and an early-out taken when the scene has nothing to draw (no
objects, rigged instances, mesh draw requests or visible volumes). The UI must be
recorded before both, or it disappears exactly when the 3D scene is empty -- which is
precisely the case a GUI-only application hits.

The UI draws **after** the scene, **inside the same render pass**, **before**
`vkCmdEndRenderPass`. If you use dynamic rendering, it goes inside the same
`vkCmdBeginRendering`/`vkCmdEndRendering` block. Do not open a second render pass for it —
that costs a full attachment load/store on tiled GPUs for no reason.

If MSAA is enabled on the scene, the UI pipeline must be created with the same
`rasterizationSamples`. UI geometry gets multisampled too, which is harmless (it is
axis-aligned and produces no extra coverage) and much simpler than resolving first.

## World-anchored labels

Not a widget: text pinned to a 3D position — axis annotations, a value floating beside a
rotation glyph — doesn't need the widget system at all, just a straight screen-space
projection each frame:

```cpp
void GuiContext::addWorldLabel(const glm::vec3& worldPos, const glm::mat4& viewProj,
                               std::string_view text, Color color,
                               Vec2 pixelOffset = {0, 0});
```

Projects `worldPos` through `viewProj`, rejects it if `w <= 0` (behind the camera) or the
resulting NDC coordinates fall outside `[-1, 1]` on either axis, then divides to NDC, maps
to screen pixels, and calls `drawList.addText(...)`. It does not attempt overlap culling
between multiple labels — with the small number of labels a simulation typically wants
(axis tags, a handful of value readouts), overdrawn text is not a problem in practice; add
culling if a use case actually needs it.
