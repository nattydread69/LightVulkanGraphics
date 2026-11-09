# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and the project follows semantic versioning for public releases.

## Unreleased

### GUI
- `Panel::dragMoving()` and `GuiContext::displaySize()`, so a host can implement drop targets such as docking.

### Bricked volumes
#### Added
- **Sparse, bricked volumes**: `createBrickedTexture`, `uploadBricks`, `evictBricks`,
  `evictAllBricks`, `setVolumeBricks`, `brickedAtlas`, residency queries and
  `BrickedTextureDescription` / `BrickUpload` (see `docs/volume_rendering.md`). A volume
  larger than one 3D texture, or mostly empty, renders from an atlas of resident bricks
  through a brick-index texture; every render style is supported and matches the
  single-texture picture exactly.
- `BrickPool` (slot allocation with LRU eviction), `updateTexture3DRegions` (batched partial
  updates), `brickGridSize`, `validateBrickedTextureDescription`.
- `volume_bricked_integration_test` (GPU: bricked vs plain, all styles, eviction) and
  `BrickPool` unit tests.
- The volume descriptor set gained a third binding (the brick index); shaders using it must
  be rebuilt with the library.

### Fonts

> **ABI note.** `ScreenTextDescription` gained fields and `VkApp` gained members, so this
> release is **not** ABI-compatible with 3.x; bump `LVG_ABI_VERSION` before shipping it.
> (`LightVulkanGraphicsUI`'s `Font`, `GuiCreateInfo` and `GuiContext` changed layout too.)

#### Added
- **Kerning.** `Font::kerning()` / `kerningFor()`, applied by `measureText()`,
  `offsetAtIndex()`, `indexAtOffset()` and `DrawList::addText()`. Reads GPOS `kern`
  lookups including Extension subtables (which stb_truetype skips, so Inter previously had no
  kerning at all). `FontOptions::kerning`, `GuiCreateInfo::kerning`, `TextFlags::NoKerning`.
- **Dynamic glyphs.** Codepoints outside the pre-baked ranges are baked on first use
  (`FontOptions::dynamicGlyphs`, `Font::canRender()`, `Font::atlasRevision()`); VkApp
  re-uploads changed atlases automatically.
- **Wider default glyph set**: dashes, curly quotes, bullet, ellipsis, per mille, euro and the
  minus sign.
- **Fallback faces** in the same atlas: `Font::addFallbackFace()`,
  `GuiCreateInfo::fallbackFontPaths`.
- **Font styles**: `FontStyle`, `GuiContext::styledFont()`, `Label::setFontStyle()`,
  `GuiCreateInfo::boldFontPath / monoFontPath / enableMonoFont`. Bundled
  `assets/fonts/DejaVuSansMono.ttf` (licence in `DejaVu-License.txt`).
- **SDF text**: `FontOptions::mode = FontRenderMode::Sdf`, `GuiCreateInfo::headingFontSdf`,
  `shaders/ui_sdf.frag`, `UiRenderer` `TextureKind::Sdf` pipeline.
- **ScreenText from the font atlas** (default when the GUI is up): anti-aliased, kerned,
  UTF-8. `ScreenTextDescription::useFontAtlas / monospace / bold / fontPixelSize`. The
  stb_easy_font path remains for `useFontAtlas = false` and for apps without a GUI.
- `Font::readFile()`, `bakeFromFile()`, `findSystemFont()`, `setOptions()`
  (`FontOptions`: render mode, oversampling, SDF padding).
- `UiRenderer::updateTexture()` and `syncAtlas()`; `DrawList::setPixelScale()`.
- `examples/font_showcase.cpp` (`font_showcase [shot.png]`), tests in
  `tests/ui/test_font_features.cpp`.

#### Changed
- Glyphs are placed at fractional x positions (oversampled bitmaps and SDFs) and snapped to
  *physical* pixels vertically; quads are no longer stretched by snapping each corner.
- Text coverage is adjusted by text luminance to offset gamma-space blending (not on sRGB
  swapchains).
- `Font::hasGlyph()` now means "resident in the atlas"; codepoints no face has are no longer
  substituted with `.notdef` (they draw as the fallback glyph, as before for out-of-range ones).
- Under `TextFlags::Tabular`, kerning is suppressed next to digits.

#### Fixed
- `UiRenderer::rebuildAtlas()` destroyed the sampler shared with registered textures, leaving
  their descriptor sets referencing a destroyed object.

### 3.3.0 (ABI-compatible with 3.0-3.2: same `libLightVulkanGraphics.so.3`)

No class layouts change; the new accessor is inline.

#### Added
- `VkApp::getSwapchainExtent()`: the size in pixels of the rendered and presented
  images (the space screen text, frame captures and the projection aspect use), which
  can differ from the window size on HiDPI displays.

#### Fixed
- **Depth range.** `Camera::proj()` and the directional/spot shadow projections used
  GLM's OpenGL depth range (-1..1), but Vulkan clips depth to 0..1, so the nearer half
  of every depth range was discarded: in orthographic views everything nearer than
  (zNear + zFar) / 2 disappeared, the effective perspective near plane was about
  2 * zNear, and shadow casters/receivers in the nearer half of a light's shadow volume
  got no shadows. Projections now use `perspectiveRH_ZO` / `orthoRH_ZO`, and
  `ViewFrustum::fromViewProj()` extracts the near plane for the 0..1 range. Shadow-map
  depth is now spread over the whole near..far range, so a given `shadowBias` covers
  about twice the world-space distance it did before.
- **Crash with no instanced objects.** A frame with custom meshes, screen text, rigged
  objects or volumes but no flexible-shape objects issued a zero-vertex `vkCmdDraw`
  that could run without a bound vertex buffer (segfault in the Intel Mesa driver).
  The flexible-shape pass is now skipped when it has nothing to draw.
- **Swapchain size on surfaces without a fixed extent (Wayland).** The swapchain used
  the construction-time window size forever, ignoring HiDPI scaling and later resizes.
  It now follows the window's framebuffer size, clamped to the surface limits.

### 3.2.0 (ABI-compatible with 3.0/3.1: same `libLightVulkanGraphics.so.3`)

No class layouts change. Install the shaders with the library (`volume_raymarch.frag.spv`
changed; the push-constant block keeps its 128 bytes, so old binaries keep working with
the new shader and render as before).

#### Added
- **Volume render styles**: `VkApp::setVolumeStyle(handle, VolumeStyleDescription)` /
  `getVolumeStyle` select composite, maximum- or minimum-intensity projection,
  isosurface (first hit, bisection-refined, gradient-lit) or average (radiograph), plus
  optional headlight gradient shading for composite rendering. Styles live in a side
  table, so `VolumeRenderDescription` and `VkApp` are unchanged.
  `volumeRenderStyleName()` gives stable names.
- `RiggedObject::setAnimationTime(seconds)`: poses the current clip at any time, also
  while paused (timeline scrubbing); wraps looping clips, clamps others.
- **Orthographic cameras**: `setCameraProjection(Camera::Projection::Orthographic)` and
  `setCameraOrthoHeight(h)` give a parallel projection (visible height `h`, width
  `h * aspect`) with Vulkan [0, 1] depth; scroll zoom changes the height. Frustum culling
  works with it. `Camera::projection(aspect, mode, height)` builds the matrix.
- **Straight-up/down views**: `setCameraLookAt(eye, target, up)` now honours `up` when
  the view is vertical (it used to produce NaN there); without a usable `up` the yaw
  direction becomes screen-up. `Camera::up()` / `view(verticalViewUp)` expose this.
- **Per-object visibility**: `setObjectVisible` / `isObjectVisible` hide an object without
  removing it (no draw, no shadow, not in culling stats or the automatic shadow centre),
  keeping indices and handles stable -- for pooled objects.
- **Emissive flexible shapes**: `setObjectEmissive(index, 0..1)` blends an object towards its
  flat colour regardless of lights and shadows (16 levels, packed into the instance
  shape slot, so the instance layout is unchanged; `flexible_shape.*.spv` changed).
- **Mouse camera switch**: `setMouseCameraEnabled(false)` turns off right-drag look/orbit
  and wheel zoom for applications with their own mouse handling.
- **Pinned directional shadow centre**: `setLightShadowCenter(index, center)` /
  `getLightShadowCenter` (nullopt = the previous automatic centre). Follows the light
  through index shifts.
- `LVG_BUILD_UI_SHARED` CMake option: builds LVGUI as `libLightVulkanGraphicsUI.so`
  instead of a static archive, so consumers that use the GUI link no LGPL code
  statically (relevant for proprietary applications). Default unchanged (static).

#### Changed
- Shader search order: shaders next to the loaded library are now found before the
  build-tree directory baked in at compile time, so an installed or relocated library
  no longer loads shaders from the tree it was built in.

#### ABI notes
- The new camera, visibility, emissive, mouse-switch and shadow-centre state lives in a
  per-application side table (`include/internal/VkAppExtras.h`, `src/VkAppExtras.cpp`), like the volume styles: `VkApp`,
  `Camera` and `LightSource` keep their 3.0/3.1 layouts (a unit test pins
  `sizeof(Camera)`). Verified by running a program compiled against the pre-3.2 headers
  against this library.
- As before, `LVG_BUILD_UI=ON` and `=OFF` builds are not layout-compatible with each
  other (`VkApp` has `LVG_WITH_UI` members); consumers must match the library's setting,
  which the exported CMake target does automatically.

### 3.1.0 (ABI-compatible with 3.0: same `libLightVulkanGraphics.so.3`)

No class layouts change; 3.0 applications keep working. Install the shaders with the
library (`ui_image.frag.spv` is new).

#### Added
- **Frame capture**: `VkApp::requestFrameCapture(callback)` reads back the next presented
  frame (scene and GUI) as RGBA; `VkApp::writePng(path, frame)` saves it with no extra
  dependency. Swapchains now request `TRANSFER_SRC` usage where the surface allows it.
- `VkApp::updateTransferFunction(handle, points)`: edits a transfer function in place,
  with no device-idle wait and no handle churn.
- `VkApp::getOrbitAzimuth()` / `getOrbitElevation()`.
- LVGUI `Key` codes for the rest of the alphabet, `Num0`-`Num9`, `F1`-`F12`, `Minus`,
  `Equal` and `Insert` (appended; existing values unchanged).

#### Changed
- The swapchain presents with MAILBOX where the surface offers it, and `run()` paces its
  loop to the primary monitor's refresh rate. FIFO blocked the whole loop while the window
  was hidden on Wayland (no frame callbacks), so background work finished unseen until the
  window came back. `LVG_PRESENT_MODE=fifo` keeps FIFO. Applications that drive frames
  themselves instead of calling `run()` are not paced under MAILBOX.

#### Fixed
- LVGUI `Image` now draws textures from `registerUiTexture()` in colour (new
  `ui_image.frag` pipeline); before, only the red channel was used, as coverage.
- Orbit camera: `setOrbitTarget/Radius/Angles`, `addOrbitAngles`, `panOrbitTarget` and
  `dollyOrbitRadius` move the camera at once (they used to wait for the next mouse event),
  and `setOrbitEnabled(true)` snaps the camera onto the orbit.
- Orbit camera: right-drag set yaw to azimuth - 90 degrees, turning the camera away from
  its target; it now faces the target.
- LVGUI `Row`, `CollapsingSection` and `TabBar` no longer reserve space for hidden
  children (matching `Panel`).
- The package smoke tests install with `--component Unspecified`, so they no longer
  overwrite the build directory's `install_manifest.txt` (the record of the real install
  that `make uninstall` uses) or fail when a `sudo` install left it root-owned.

### 3.0.0

**ABI break (3.0.0, `libLightVulkanGraphics.so.3`)**: `VkApp`, `detail::Texture` and
`RiggedMesh` gained members, so applications must be rebuilt against the new headers.

### Added
- `MeshTranslucency` (`RiggedMesh::translucency`): per-submesh override of how the
  rigged renderer treats texture alpha; `Auto` (the default) decides from the texture.
- Cutout shadows (`shadow_cutout.vert/.frag`) for rigged submeshes with see-through
  textures, so hair cards and eye cornea shells no longer cast solid shadows.
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
- **LVGUI file dialogs**: `OpenFileDialog` and `SaveFileDialog` now share a
  `FileDialogBase` and are built around a new `FileListView` widget: Name / Size /
  Modified columns sorted by clicking a header (natural, case-insensitive name order;
  folders first), folder navigation (double-click/Enter, ↑/Backspace, editable path,
  `setNavigationEnabled()`), a search box, multiple file-type filters
  (`setFileTypeFilters()`), a hidden-files toggle, a resizable dialog whose list fills
  it, and a status line. Save As adds an overwrite confirmation (`setConfirmOverwrite()`),
  filename validation and `open(suggestedFilename)`. Folder, sort and filter are
  remembered between `open()` calls, and confirmed paths are now absolute. New
  `file_dialog_demo` example; `gui_demo`'s File > Open... (and Ctrl+O) now opens an
  `OpenFileDialog` on the repository.
- **LVGUI `PlotView`**: a 2D x/y plot widget -- several named series drawn as lines
  and/or points, axes with 1/2/5 "nice" ticks sized to their labels, grid, legend
  (click to hide/show a series), wheel zoom about the cursor (Shift: X only, Ctrl: Y
  only), drag pan, double-click reset, and a hover readout that snaps to the nearest
  point. Axes auto-fit to the data or are pinned with `setXRange()`/`setYRange()`;
  `appendPoint()` with `setSeriesMaxPoints()` streams into a rolling window;
  `setOnViewChange()` reports user zooms and pans. `function_plotter_demo` now uses it
  (and re-samples the function over the zoomed range), and `gui_demo` has a live
  streaming plot panel.
- **LVGUI `TreeView`**: a scrollable, collapsible hierarchy -- nodes added with
  `addNode(text, parent)` under stable int ids, optional per-node detail text and user
  data, disclosure arrows (Shift+click opens or closes a whole subtree), indent guides,
  double-click/Enter activation, keyboard navigation (Up/Down/PageUp/PageDown/Home/End,
  Right/Left to open/step in and close/step out), type-ahead, `reveal()`, and a
  draggable scrollbar. `fbx_model_inspector` now shows the bone hierarchy as a tree
  instead of a flat list, and `gui_demo` has a scene-graph tree panel.
- `VolumeColorSource::TextureRgba`: volumes can take their colour directly from an
  RGBA8 3D texture, with the texture's alpha mapped to opacity through the transfer
  function, so colour and opacity can show different quantities. The default,
  `VolumeColorSource::TransferFunction`, keeps the existing behaviour.
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
- Bundled `stb_image.h` updated from v2.28 to v2.30: faster PNG decoding, stricter
  handling of truncated compressed data, and the fix for a false
  `-Wstringop-overflow` warning GCC 14 reports in optimised builds (an error under
  `LVG_WARNINGS_AS_ERRORS=ON`).

### Fixed
- Rigged meshes with see-through textures (MakeHuman hair cards, eyelashes, eyes) rendered
  wrongly: texels blended but still wrote depth, so a transparent cornea shell hid the
  iris, and hair listed before the body in the file showed the background through it.
  Fully transparent texels are now discarded; see-through submeshes draw after solid
  ones as an opaque alpha >= 0.5 core plus a blended, non-depth-writing fringe, without
  specular highlights (flat hair-card normals caught the light as pale streaks).
- The Vulkan validation-layer debug messenger printed to the console unconditionally, ignoring `debugOutput` / `LightVulkanGraphicsCreateInfo::enableDebugOutput` (both default `false`). It now always routes through `logMessage()`, so that flag is the sole switch for validation-layer chatter.
- `FBXLoader::loadModel()` computed a skinned bone's world bind pose as `mesh.globalBindTransform * inverse(offsetMatrix)` for every source format. Assimp's glTF2 importer reports `mOffsetMatrix` already relative to the skin's common space, unlike its FBX importer (mesh-node-local); for a glTF/glb mesh node with a non-identity transform, premultiplying by `mesh.globalBindTransform` doubled that transform and corrupted every bone's bind position (e.g. a hip bind height collapsing from ~1m to ~0.1m with an axis swap). glTF/glb sources now use `inverse(offsetMatrix)` directly; FBX is unaffected (verified via `worker_skinning_sanity`).
- `FBXLoader` never applied a node's accumulated hierarchy transform to its mesh's vertex data. Harmless for skinned meshes (positioned entirely by bone skinning) but for unskinned static-prop meshes -- a large multi-node scene, for example -- every mesh rendered at its own local-node origin, collapsing the whole scene into an overlapping jumble. Each unskinned mesh's node transform is now baked into its vertex positions/normals at load time.
- Embedded-texture cache keys were built from Assimp's per-file texture reference (`"*0"`, `"*1"`, ...), which is only unique within one source file. `VkApp`'s texture cache is a single process-wide map, so loading a second model whose material also referenced `"*0"` silently reused the first model's texture. The source file path is now folded into the cache key.
- The window was mapped to the screen immediately on creation, before any scene loading ran. A slow enough synchronous load between construction and `run()` (nothing pumping the event queue yet) reads to the OS/window manager as a hung application. The window is now created hidden and `run()` reveals it right as its event loop starts.
- `-DLVG_BUILD_UI=OFF` still installed the `lightVulkanGraphics/ui/` public headers (unused, but present) alongside everything else the option correctly excluded from that install. Now excluded too, so an `LVG_BUILD_UI=OFF` install has no trace of GUI code at all.
- **LVGUI**: a quick click on a `MenuBar` item -- press and release delivered in the
  same frame, as a fast click or touchpad tap on a slow frame does -- counted as a click
  on the bare scene and cleared keyboard focus right after the item's callback ran, so a
  dialog opened from a menu (e.g. `SaveFileDialog`, which focuses its filename field)
  came up with nothing focused. Presses on the menu bar no longer clear focus.
- **LVGUI**: once the mouse had passed over any text box the cursor stayed an I-beam
  for the rest of the session -- `GuiContext::requestCursorShape()` forwarded the I-beam
  straight to the platform and nothing ever asked for the arrow again. Cursor requests
  now last one frame, defaulting back to the arrow, and the platform is only told when
  the shape actually changes.
- **LVGUI**: on X11, GLFW reports a modifier key's own press with the modifier state from
  *before* the press, and its release likewise. Pressing Shift therefore told the GUI
  Shift was up, and releasing it told the GUI Shift was down -- until the next key event.
  Every modifier+mouse gesture was inverted on Linux: Ctrl-click text entry and Shift
  fine-adjustment drags on sliders and drag values, and `PlotView`'s Shift/Ctrl wheel
  zoom. The GLFW layer now applies a modifier key's own transition to the state it
  reports.
- `-DLVG_BUILD_UI=OFF` builds failed to compile: `VkApp::pollKey()` was declared only
  inside the `LVG_WITH_UI` block of `VkApp.h` but defined (and used by the rigged
  animation shortcuts) unconditionally. It is now declared for both configurations.

### Removed
- Maintainer-local editor and agent workflow files from the public repo surface.
