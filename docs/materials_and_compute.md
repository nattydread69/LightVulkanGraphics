# 2D textures, textured and splat-map materials, and compute (3.4)

Defined when `LVG_HAS_MATERIAL_TEXTURES` and `LVG_HAS_COMPUTE` are set (`VolumeRendering.h`). All of it is additive: existing materials and
shaders behave as before. State lives in side tables, so `VkApp`'s data layout did not grow; `MaterialDescription` gained fields, so
applications recompile against the new header.

## 2D textures

    Texture2DDescription d; d.width = w; d.height = h;
    d.format = Texture2DFormat::RGBA8_UNORM;   // or RGBA8_SRGB (hardware linearises on sampling)
    Texture2DHandle t = app.createTexture2D(d, rgba8, w * h * 4);
    app.updateTexture2D(t, newRgba8, w * h * 4);   // same size; mip chain is rebuilt unless d.generateMipmaps = false
    Texture2DHandle fromFile = app.createTexture2DFromImage(bytes, size, Texture2DFormat::RGBA8_SRGB, /*repeat*/ true);  // PNG/JPEG/BMP/TGA via stb
    app.destroyTexture2D(t);   // destroy materials that use it first; the GPU memory is freed once in-flight work finishes

    Texture2DDescription pixelArt = d; pixelArt.filter = Texture2DFilter::Nearest; pixelArt.generateMipmaps = false;   // 3.5: hard texel edges
    Texture2DHandle crisp = app.createTexture2DFromImage(bytes, size, pixelArt);   // decoded image with any option

Mip chains are built on the CPU with a box filter (sRGB colour is averaged in linear light). Weight maps and other data should set
`generateMipmaps = false` and `RGBA8_UNORM`.

## Materials with textures

`MaterialDescription::textures` (10 slots = bindings 0-9 of descriptor set 1) and `parameters` / `parameters2` (two `vec4`s, fragment push
constants) feed the built-in textured shaders. Unused slots sample a white texel.

| `ShaderSelection` | Slots | Constants |
|---|---|---|
| `Textured` | `textures[0]` multiplies the vertex colour (uv scaled by `parameters.xy`), lit like `VertexColorLit` | `parameters.xy` |
| `TexturedUnlit` (3.5) | `textures[0]` | `parameters.xy` |
| `TerrainSplat` | `textures[0]`, `[1]` = weights of layers 0-3 and 4-7 (RGBA8, mesh uv 0..1 across the terrain); `textures[2..9]` = the 8 layer colours, sampled at world xz / tile size | `parameters` = tile metres of layers 0-3, `parameters2` = layers 4-7 |

`TexturedUnlit` is `Textured` without the lighting: the texture times the vertex colour, written as is (alpha included), so artwork keeps its own colours. Pair it with `alphaBlendingEnabled = true` (and usually `depthWriteEnabled = false`, `cullMode = None`) for sprites.

All three need a mesh with `MeshVertex::uv` (and, for splat, the uv spanning the whole terrain). A `Textured`/`TerrainSplat` material without
`textures[0]` is rejected.

## Compute

A deliberately small, synchronous API for tools and simulations (it is not a task graph):

    GpuBufferHandle a = app.createGpuBuffer(bytes);
    app.uploadGpuBuffer(a, data, bytes);
    ComputeKernelHandle k = app.createComputeKernel({"kernel.comp.spv", /*storage buffers*/ 2, /*push bytes*/ 12});
    app.runCompute([&](ComputeRecorder& r)
    {
        r.dispatch(k, {a, b}, &push, sizeof push, groupsX);    // a full memory barrier separates consecutive dispatches
        r.dispatch(k, {b, c}, &push2, sizeof push2, groupsX);
    });                                                         // submits and waits
    app.downloadGpuBuffer(c, out, bytes);

Kernels are GLSL `#version 450` compute shaders with storage buffers at set 0, bindings `0..n-1`, compiled by the application (`glslc`).
Work is recorded into the application's upload batch and submitted on the graphics queue, so it is ordered with pending uploads. Do not
call buffer or kernel creation from inside the `runCompute` callback. Requires `init()` (a window and device exist).

Tests: `material_texture_compute_integration_test` (needs a GPU: `-DLVG_BUILD_VULKAN_INTEGRATION_TESTS=ON`).
