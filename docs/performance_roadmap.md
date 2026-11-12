# Performance roadmap

Where LVG's frame time goes and the order in which to take it back. Measured with
`perf_overlay_demo`: N animated flexible-shape instances (`--instances N`, rewritten every
frame) plus N animated `Worker.fbx` characters (`--characters N`), with shadows and frustum
culling on. Machine: AMD Radeon RX 6700 XT, Xeon E5-2650 v4 (2.2 GHz, 48 threads), Release
build, 2026-11-12.

How to measure (prints one `BENCH` line, then exits):

    env -u WAYLAND_DISPLAY LVG_PRESENT_MODE=uncapped \
        ./perf_overlay_demo --instances 10000 --characters 64 --bench 6 --warmup 3
    LIGHT_VULKAN_GRAPHICS_SKINNING_DEVICE=cpu ...    # same, CPU skinning

Run under XWayland (`env -u WAYLAND_DISPLAY`): a native Wayland window has no IMMEDIATE
present mode, and the compositor throttled the same scene from 147 to 76 fps. That hides
everything being measured.

`demoVulkanGraphics --workers 6 --freeze 1.0 --capture out.png` writes a deterministic frame,
for pixel comparisons between builds (`compare -metric AE a.png b.png null:`).

Every configuration measured below is **CPU-bound**: the process's CPU time per frame equals
the frame time. So the stages are ordered by measured main-thread cost, found by sampling
stacks under gdb (`perf` needs `perf_event_paranoid` <= 2).

Rewriting core parts in another language (Rust was considered) is not on this list. The same
algorithm compiles to the same machine code, and the costs below are algorithmic or
synchronisation costs that a rewrite would carry over unchanged.

## Stage 1: GPU skinning and no per-frame upload stall (done)

See `rigged_skinning.md`, "Where the skinning runs".

- `rigged_skin.comp` blends the vertices; the CPU only sends bone matrices.
- Rigged uploads and dispatches are recorded into the frame's own command buffer, which
  removes a `vkQueueSubmit` + `vkQueueWaitIdle` per frame (it also helps the CPU fallback).

Mean frame time in ms (fps), average of two interleaved runs that agreed within 3%:
*before* = the tree before this change; *CPU* = this change with
`LIGHT_VULKAN_GRAPHICS_SKINNING_DEVICE=cpu` (stall removed, CPU skinning); *GPU* = this change.

| Shapes | Characters | Before         | CPU (stall fix) | GPU skinning    | Speed-up |
|-------:|-----------:|---------------:|----------------:|----------------:|---------:|
|  1,000 |          0 |   1.51 (662)   |   1.51 (662)    |   1.50 (667)    |   1.0x   |
|  1,000 |         16 |  13.18 (75.9)  |  10.55 (94.8)   |   2.60 (385)    |   5.1x   |
|  1,000 |         64 |  46.04 (21.7)  |  37.86 (26.4)   |   5.49 (182)    |   8.4x   |
|  1,000 |        128 |  88.47 (11.3)  |  72.81 (13.7)   |   9.24 (108)    |   9.6x   |
| 10,000 |          0 |   4.72 (212)   |   4.70 (213)    |   4.72 (212)    |   1.0x   |
| 10,000 |         16 |  16.35 (61.2)  |  13.76 (72.7)   |   5.83 (172)    |   2.8x   |
| 10,000 |         64 |  49.77 (20.1)  |  40.36 (24.8)   |   8.64 (116)    |   5.8x   |
| 10,000 |        128 |  93.59 (10.7)  |  77.06 (13.0)   |  12.45 (80.3)   |   7.5x   |
| 50,000 |          0 |  19.49 (51.3)  |  19.30 (51.8)   |  19.07 (52.4)   |   1.0x   |
| 50,000 |         16 |  30.60 (32.7)  |  28.58 (35.0)   |  20.45 (48.9)   |   1.5x   |
| 50,000 |         64 |  64.93 (15.4)  |  56.20 (17.8)   |  24.25 (41.2)   |   2.7x   |
| 50,000 |        128 | 106.98 (9.35)  |  90.06 (11.1)   |  27.78 (36.0)   |   3.9x   |

- Each animated character used to cost **0.68 ms** of frame time. It now costs
  **0.06 ms**, 11x less. Removing the stall alone accounts for about 18% of the old cost; the
  rest is GPU skinning.
- p95 frame times follow the means. With 128 characters on 1,000 shapes, p95 went from
  94.7 ms to 10.3 ms.
- The rows with no characters are unchanged, as they should be: Stage 1 does not touch the
  shape path. Those rows are Stage 2's target.
- Correctness: the CPU path reproduces the old frame exactly (0 pixels differ). The GPU path
  differs in 5 of 2.08 M pixels on the RX 6700 XT and 7 of 2 M on Lavapipe, all on triangle
  edges (float rounding). Validation (with synchronisation validation on) reports nothing new.
  The 21 messages it does report also appear before this change (Stage 4, item 4; fixed there).

## Stage 2: build each object's instance once per frame (done)

Before: with 50,000 shapes and nothing else, a frame took 19-20 ms, all of it CPU. 25 of 40
main-thread stack samples were in `VkApp::makeInstanceForObject()`.

What changed:

1. **Instances are built once, when an object changes.** Every pass used to rebuild every
   visible object's instance from scratch: the shadow pass once per shadow-casting light,
   then the main, overlay and transparent passes, each into a fresh per-frame vector.
   Instead, each instance is built when its object changes (`updateInstanceDataOptimized()`).
   `writeCachedInstances()` copies it straight into the mapped per-frame buffer, and the
   shadow pass writes once for all lights. Frustum culling reads the cached matrix. The
   whole-cache copy into the buffer each frame is gone: the passes overwrote it before the
   GPU ever read it.
2. **No per-object global lock or string copies.** `detail::appExtras()` (a global mutex plus
   a hash lookup) is fetched once per update rather than per object. The per-object name
   and texture-path copies use the new `pObject::getNameRef()` / `getTexturePathRef()`.
   The per-pass group lists keep their capacity between frames.
3. **Cheaper model matrix.** `getObjectModelMatrix()` composes rotation * size + position
   directly instead of multiplying three full 4x4 matrices. Bit-identical for finite input.
4. **Fixed: wrong shadows under frustum culling.** The shadow pass and the main pass wrote
   their instances to the same range of the frame buffer, both during recording, so shadow
   draws used the main pass's culled, reordered list. With culling on (the default),
   off-screen objects cast no shadow, and transparent and overlay objects cast shadows. Shadow
   casters now have their own range, `[N, 2N)`. In a test scene (pillars behind the camera,
   a low sun, glass and overlay boxes) the old code drew 23% of the frame wrongly with
   culling on; it now matches the culling-off picture exactly.
5. Fixed: an object changed before `finalizeScene()` could leave its cache entry stale, since
   setters do not mark objects dirty before then. The cache is fully rebuilt until the first
   update after it.

Mean frame time in ms (fps), `perf_overlay_demo`, no characters unless stated, average of
two interleaved runs (within 7% of each other). *Static* = `--static`: nothing moves.

| Shapes | Scene                  | Stage 1        | Stage 2       | Speed-up |
|-------:|------------------------|---------------:|--------------:|---------:|
|  1,000 | animated               |  1.48 (675)    |  1.20 (834)   |   1.2x   |
|  1,000 | static                 |  1.48 (677)    |  1.09 (914)   |   1.4x   |
| 10,000 | animated               |  4.81 (208)    |  2.41 (414)   |   2.0x   |
| 10,000 | static                 |  3.24 (309)    |  1.58 (634)   |   2.1x   |
| 10,000 | animated + 64 characters | 8.63 (116)   |  6.42 (156)   |   1.3x   |
| 50,000 | animated               | 20.13 (50)     |  8.00 (125)   |   2.5x   |
| 50,000 | static                 | 11.64 (86)     |  6.26 (160)   |   1.9x   |

- The 50,000-shape static scene is now GPU-bound: 4.0 ms of CPU in a 6.3 ms frame. It is the
  first configuration here that is, so Stage 5's GPU timestamps become useful from now on.
- Correctness: `perf_overlay_demo --static --capture` (culling on and off),
  `demoVulkanGraphics --freeze --capture` and the shadow test scene all match the Stage 1
  pictures pixel for pixel, except the shadow scene, where Stage 2 matches the correct
  (culling-off) picture. Validation shows only the pre-existing messages (Stage 4).

Not done: building the dirty instances in parallel. After these changes the animated
50,000-shape frame is 8 ms. The per-object rebuild of moved objects (one matrix each) and
the grouping and copying during recording are what remain. Split the dirty range across
threads if scenes that size need more.

## Stage 3: per-character CPU cost (done)

After Stage 1 a character cost 0.06 ms. With 128 GPU-skinned characters the samples split
roughly half in `updateRiggedInstances()` (building bone matrices) and a quarter in
`RiggedObject::calculateBoneTransforms()` (evaluating the animation).

Done:

1. **Each distinct bone matrix built once, not once per mesh bone.** Worker.fbx has 12 meshes
   whose bone lists add up to 744 entries, but only **62 distinct** final matrices (same
   skeleton bone, offset and mesh bind). `addRiggedObject()` now finds them
   (`RiggedInstanceRenderData::uniqueBones`), and `updateRiggedInstances()` builds each once
   per pose change. On the GPU path, each mesh's uploaded vertices have their bone indices
   remapped into that table. So a character's frame upload is 62 matrices (4 KB) rather than
   744 (48 KB), and the shader is unchanged. The CPU path gathers each mesh's table from it.
   Two entries are merged only when every input `buildRiggedFinalBoneMatrix()` reads is
   bit-identical, so every result is exactly what it was.
2. **Animation evaluation.**
   - `Animation::channelIndexByBone` (built at load) replaces a `std::map<std::string, int>`
     lookup per bone per frame.
   - Keyframes are binary-searched when every channel's keys are in ascending time
     (`keysSortedByTime`, checked at load). `upper_bound` gives exactly the linear scan's
     answer; otherwise the scan remains.
   - Each bone's local matrix is composed directly rather than from three 4x4 products (as
     `getObjectModelMatrix()` in Stage 2). Bit-identical for finite input.

Mean frame time in ms (fps), `perf_overlay_demo --instances 1000 --characters N`, GPU skinning
unless stated, average of two interleaved runs:

| Characters       | Stage 2       | Stage 3       | Speed-up |
|-----------------:|--------------:|--------------:|---------:|
|               16 |  2.35 (426)   |  1.60 (624)   |   1.5x   |
|               64 |  5.33 (188)   |  2.44 (410)   |   2.2x   |
|              128 |  9.11 (110)   |  3.54 (282)   |   2.6x   |
| 64, CPU skinning | 38.07 (26)    | 36.74 (27)    |   1.04x  |

- A character now costs about **0.018 ms** of frame time, down from 0.062 ms (3.4x less), and
  0.68 ms before Stage 1. The CPU skinning path barely moves: its per-vertex blend dominates.
- Correctness: the frozen `demoVulkanGraphics` frame (6 Workers at 6 different animation
  times) is pixel-identical to before, on both the GPU and the CPU path. Validation shows only
  the pre-existing messages.

Remaining, after re-sampling 128 characters: `RiggedObject::calculateBoneTransforms()` is
now the largest cost (about 38% of samples). `updateRiggedInstances()` is about 15%.

3. **Parallel animation (done, opt-in).** `RiggedObject::updateAnimation()` is called by the
   application, so LVG cannot thread it unasked. New: `lightGraphics::updateAnimations(objects,
   dt)` (`RiggedAnimation.h`) updates a set of objects on a small persistent worker pool
   (at most 8 threads; the caller joins in; fewer than 8 objects run serially). Each call
   writes only its own object and reads the shared model, so the result is exactly the
   serial one. The `rigged_animation_parallel` test checks this: 64 Workers, 30 uneven
   steps, every bone matrix bit-identical. `perf_overlay_demo` uses it
   (`--serial-animation` for comparison):

   | Characters (1,000 shapes) | Serial  | `updateAnimations()` | Speed-up |
   |--------------------------:|--------:|---------------------:|---------:|
   |                        64 | 2.43 ms | 2.09 ms              | 1.16x    |
   |                       128 | 3.50 ms | 2.81 ms              | 1.25x    |

   Total CPU time rises a little (waking workers), but frame time and p99 drop. Not done:
   the bone-matrix build in `updateRiggedInstances()` could run on the same pool.
4. **Pose-change test: not worth doing.** Comparing and copying `lastBoneTransforms` costs
   about 85 matrices per character, around 0.05 ms per frame at 128 characters.
5. **Skinning-mode choice once per model (done).** `addRiggedObject()` ran
   `chooseRiggedSkinningMode()` (three full skins of the model plus every triangle edge),
   and one more full skin for its log line, *per instance*. The first instance of a
   `RiggedModel` now chooses, and later instances reuse that choice
   (`skinningModeByModel_`). This is a deliberate behaviour change: the choice judges the
   model's bind pose, so all of a model's instances now skin the same way, rather than each
   being judged on its own pose at registration. The environment override still applies to
   every instance, and a reloaded model is a new model with a new choice. Loading 128
   Workers: 1.38 to 1.16 s. The frozen demo frame (6 Workers of one model) is unchanged.

## Stage 4: remaining stalls and synchronisation debt (done)

Hitches at load time and on scene edits, plus correctness; not steady-state fps. Measured
with `perf_overlay_demo`. `SETUP` is the time to build the scene. `--churn 10` removes one
character and adds one back every 10 frames, so add/remove hitches show in the frame-time
percentiles.

1. **Upload batching.** `beginSingleTimeCommands()` / `endSingleTimeCommands()` used to submit
   and `vkQueueWaitIdle` per call: 24 times for one Worker, once per texture, and for every
   volume upload. They now record into one open upload batch. `drawFrame()` submits it, with
   its own fence, just before the frame on the same queue. The barriers already recorded
   order the uploads before the frame's reads. A batch over 256 commands is submitted early,
   e.g. while a crowd loads before the first frame.
2. **Deferred destruction (`retire()`).** Resources the GPU may still use are destroyed once
   a later submission has completed: its fence covers every earlier submission. Each tracked
   submission (upload batch or frame) gets a serial. `drawFrame()` records completion as it
   waits for a frame's fence, and polls the upload fences.
   - This replaces the `vkDeviceWaitIdle()` in `removeRiggedObject()`, `destroyMesh()`,
     `destroyMaterial()`, `destroyTexture3D()`, `destroyTransferFunction()` and
     `destroyVolume()`. Their CPU bookkeeping still happens immediately.
   - While uploads are pending, `destroyBuffer()` and `destroyTexture()` defer by
     themselves. Every caller destroys its staging buffer right after the copy, so they all
     stay correct without changes.
   - `recreateSwapChain()` submits pending uploads before the device goes idle. `cleanup()`
     destroys everything retired (`drainRetired()`) before any pool or the device goes.
   - Found on the way: `addRiggedObject()` rebuilt every frame command buffer
     (`createCommandBuffers()`) in a live scene, which only worked because each upload had
     idled the queue first. It no longer does: `drawFrame()` re-records the frame anyway.
     `createCommandBuffers()` now waits for the frames in flight before freeing their
     command buffers.
3. **Fewer allocations per character** (not in the original plan; profiling showed it was the
   largest load cost, 47% of setup samples once the upload waits were gone). Each buffer
   had its own `vkAllocateMemory`, about 78 per Worker. That is slow, and many drivers limit
   live allocations to 4096, so 128 Workers would have failed there.
   - `createBuffersInOneAllocation()` binds a list of buffers into one allocation, falling
     back to one each if no single memory type suits them all.
   - `createRiggedInstanceBuffers()` uses it for one device-local and one host-visible block
     per character. All initial index and skin-source data goes through one staging buffer.
     That is 3 allocations per character instead of about 78.
   - Lesson: the first version remapped bone indices in place in the mapped staging memory.
     That memory is write-combined, so the reads back were uncached, and loading got
     *slower* (3.2 s) until the remap moved to ordinary memory.

Mean of two runs, 1,000 shapes; characters as stated:

| Measure                              | Before Stage 4 | After Stage 4 |
|--------------------------------------|---------------:|--------------:|
| Load 128 characters (`SETUP`)        | 3.18 s         | 1.45 s        |
| 64 characters, churn: mean frame     | 4.46 ms        | 2.72 ms       |
| 64 characters, churn: p95 frame      | 20.5 ms        | 5.3 ms        |
| 64 characters, churn: p99 frame      | 23.7 ms        | 5.7 ms        |
| 64 characters, churn: worst frame    | 32.4 ms        | 8.9 ms        |
| 64 characters, steady: mean frame    | 2.46 ms        | 2.43 ms       |

Correctness:
- Pixel-identical frames on the GPU and CPU skinning paths.
- No validation messages (synchronisation validation on) in the demos or in churn runs on
  either skinning path.
- Both volume integration tests (`LVG_BUILD_VULKAN_INTEGRATION_TESTS=ON`) pass. Their only
  messages were the pre-existing frame-capture one, since fixed (item 4, below).

With the skinning-mode choice made once per model (Stage 3, item 5), loading 128 Workers takes
1.16 s, most of it the one-off FBX import.

4. **Pre-existing validation errors (done).** Checked with
   `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true`.
   Every run used to report 15 messages; now normal runs report none, including FIFO,
   MAILBOX and IMMEDIATE present modes and live window resizes.
   - `SYNC-HAZARD-WRITE-AFTER-READ`: the main render pass had no subpass dependencies, so
     its swapchain-image layout transition was not ordered after the image-acquired
     semaphore. Nor was the clear of the depth image, which every frame in flight shares,
     ordered after the previous frame's depth tests. Fixed with a `VK_SUBPASS_EXTERNAL`
     dependency (`createRenderPass()`).
   - `VUID-vkQueueSubmit-pSignalSemaphores-00067`: render-finished semaphores were per
     frame in flight, but presentation holds one until its image is presented. They are now
     one per swapchain image, indexed by `imageIndex`, and rebuilt with the swapchain
     (`createRenderFinishedSemaphores()`).
   - `VUID-vkDestroyDevice-device-05137`: `flexibleShapeTransparentPipeline_` was never
     destroyed at shutdown (nor in `cleanupSwapChain()`). Fixed.
   - Frame capture, `SYNC-HAZARD-WRITE-AFTER-PRESENT` (fixed later): the read-back was a
     separate submission chained on the render-finished semaphore and re-signalling it.
     Changing its wait stage, or consuming the semaphore, did not satisfy validation.
     The read-back is now recorded into the frame's own command buffer after the main
     render pass (`prepareFrameCapture()` / `recordFrameCapture()` / `finishFrameCapture()`).
     The pass got an explicit `0 -> EXTERNAL` dependency to the transfer stage for it to
     chain on, and the pixels are read after the frame's fence. Captures, including the
     bricked volume test's many, are now validation-clean and pixel-identical.
5. **Map/unmap fallbacks in command recording.** Partly done in Stage 2: the two per-pass
   copies are gone. `writeCachedInstances()` keeps one fallback, used only when no
   per-frame buffer exists (the legacy single `instanceBuf`, never persistently mapped), and
   a bounds check that refuses rather than writes past the buffer. The legacy buffer stays:
   scene setup and the older `updateInstanceData()` still create it as the bound fallback
   before the per-frame buffers exist, so removing it means reworking that startup path for
   no measured gain.

## Stage 5: GPU cost for large scenes

None of the measured configurations is GPU-bound yet. Start this stage only after Stages 2
and 3, and steer it with GPU timestamps:

1. **GPU timestamp queries** around the skinning, shadow, opaque, translucent, volume and UI
   passes, shown in `perf_overlay_demo`.
2. **Instanced rigged draws.** Each mesh of each character is one draw per rigged pass plus
   one per shadow-casting light. Characters sharing a model can be drawn with one indexed draw
   per mesh per pass.
3. **Cull rigged objects.** Frustum culling covers flexible shapes only; characters, and
   shadow casters outside each light's frustum, are always skinned and drawn.
4. **Static shadow caching.** Static casters are redrawn every frame for every light.

## Not planned

- **Language rewrites (Rust/other)**, for the reason above. Rust remains a good fit for
  application-side compute such as SoundSpecimen's and SoundAttitude's cores, which have narrow
  interfaces.
- **Async compute queue for skinning.** A second queue adds cross-queue synchronisation.
  Revisit only if Stage 5's timestamps show the skinning dispatch on the critical path.
