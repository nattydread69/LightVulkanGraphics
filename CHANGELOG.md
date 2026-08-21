# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and the project follows semantic versioning for public releases.

## Unreleased

### Added
- **LVGUI**: a retained-mode GUI layer (immediate-mode rendering underneath) drawn as
  Vulkan geometry inside the same frame/render pass/swapchain image the library already
  renders, for parameter and debug panels beside a 3D scene. `Panel`, `Widget` and
  around twenty widgets (buttons, checkboxes, sliders, drag values, colour edit,
  dropdowns, list boxes, text boxes, tab bars, context menus, modals, log views, plot
  lines, and more), an `stb_truetype`-backed font/text layer, and a menu bar. Ships as
  its own `LightVulkanGraphicsUI` target (`LightVulkanGraphics::UI`), self-contained and
  headless-testable (no Vulkan/GLFW/assimp in its public headers), linked one-way from
  the core library so `-DLVG_BUILD_UI=OFF` reproduces the library exactly as it was
  before LVGUI existed. See `docs/gui/00-overview.md` for the design and
  `docs/gui_usage.md` for usage; `gui_demo` is the bundled example.
- Configurable CMake options for examples, compiler warnings, and sanitizers.
- Generated public version header with compile-time version macros and constants.
- `lightGraphics::LightVulkanGraphics` compatibility alias for the main application class.
- Contributor guidance for local build and verification workflows.
- Root-level TODO roadmap for the next library-maturity work.
- Focused non-GPU unit test target for core transform, handle, object, and object-index behavior.
- `VkApp::computeArrowTransform()` helper to place the built-in ARROW mesh so it visually spans a given tail/tip pair.
- `include/RotationGlyph.h` / `src/RotationGlyph.cpp`: procedural rotation-ring glyph (`RotationRingDescription`, `buildRotationRingMesh`, `makeRotationRingTransform`) visualising angular velocity/momentum/torque as an oriented annular arrow in its plane of rotation, built on the existing custom-mesh API. New `rotation_glyph_example` target and `docs/rotation_glyphs.md`.

### Changed
- CI expanded beyond a single Ubuntu/GCC job to cover a Linux compiler matrix, sanitizer validation, and a Windows build path.
- Public docs were aligned with the current FBX API, dependency behavior, and bundled asset licensing.
- Reserved-identifier header guards were replaced with project-scoped names.
- Sanitized installs now export the sanitizer link requirements needed by downstream CMake consumers.
- Windows/MSVC builds now avoid `min`/`max` macro collisions, enable the required GLM experimental quaternion extension, and clean up warning-as-error blockers.
- Invalid object indices in remove/update APIs now throw `std::out_of_range` consistently instead of being ignored.
- Object state method implementations were split out of `src/VkApp.cpp` into `src/VkAppObjectState.cpp`.

### Fixed
- The Vulkan validation-layer debug messenger printed to the console unconditionally, ignoring `debugOutput` / `LightVulkanGraphicsCreateInfo::enableDebugOutput` (both default `false`). It now always routes through `logMessage()`, so that flag is the sole switch for validation-layer chatter.
- `FBXLoader::loadModel()` computed a skinned bone's world bind pose as `mesh.globalBindTransform * inverse(offsetMatrix)` for every source format. Assimp's glTF2 importer reports `mOffsetMatrix` already relative to the skin's common space, unlike its FBX importer (mesh-node-local); for a glTF/glb mesh node with a non-identity transform, premultiplying by `mesh.globalBindTransform` doubled that transform and corrupted every bone's bind position (e.g. a hip bind height collapsing from ~1m to ~0.1m with an axis swap). glTF/glb sources now use `inverse(offsetMatrix)` directly; FBX is unaffected (verified via `worker_skinning_sanity`).
- `FBXLoader` never applied a node's accumulated hierarchy transform to its mesh's vertex data. Harmless for skinned meshes (positioned entirely by bone skinning) but for unskinned static-prop meshes -- a large multi-node scene, for example -- every mesh rendered at its own local-node origin, collapsing the whole scene into an overlapping jumble. Each unskinned mesh's node transform is now baked into its vertex positions/normals at load time.
- Embedded-texture cache keys were built from Assimp's per-file texture reference (`"*0"`, `"*1"`, ...), which is only unique within one source file. `VkApp`'s texture cache is a single process-wide map, so loading a second model whose material also referenced `"*0"` silently reused the first model's texture. The source file path is now folded into the cache key.
- The window was mapped to the screen immediately on creation, before any scene loading ran. A slow enough synchronous load between construction and `run()` (nothing pumping the event queue yet) reads to the OS/window manager as a hung application. The window is now created hidden and `run()` reveals it right as its event loop starts.
- `-DLVG_BUILD_UI=OFF` still installed the `lightVulkanGraphics/ui/` public headers (unused, but present) alongside everything else the option correctly excluded from that install. Now excluded too, so an `LVG_BUILD_UI=OFF` install has no trace of GUI code at all.

### Removed
- Maintainer-local editor and agent workflow files from the public repo surface.
