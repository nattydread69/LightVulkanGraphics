# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and the project follows semantic versioning for public releases.

## Unreleased

### Added
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

### Removed
- Maintainer-local editor and agent workflow files from the public repo surface.
