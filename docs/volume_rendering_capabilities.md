# Volume rendering: capability status

What the public API currently exposes for volume/custom rendering work, and what it
doesn't yet.

| Capability | Status | Notes |
|---|---|---|
| Custom mesh submission | Available | Indexed triangle meshes with position, normal, color, and UV. |
| Static mesh buffers | Available | Staged into device-local buffers. |
| Dynamic mesh buffers | Available | Capacity-bounded host-visible update path. |
| Custom vertex attributes | Partial | A stable `MeshVertex` layout is public; arbitrary layouts are not. |
| Custom shader modules | Available | SPIR-V vertex/fragment paths may be selected by a material. |
| Custom material/pipeline creation | Available | Blend, depth, cull, polygon, and shader state are public. |
| 2D texture upload | Internal only | Used by rigged materials and transfer functions; not yet exposed to consumers. |
| 3D texture upload | Available | Five formats are mapped; R32 scalar upload is the baseline. |
| Storage images | Missing | Not needed by the current sampled-volume path. |
| Descriptor set extension points | Partial | Built-in volume and camera layouts plus a documented custom shader ABI; arbitrary descriptor declarations are not exposed. |
| Uniform buffer extension points | Partial | Camera UBO and push-constant ABIs are documented; arbitrary UBO allocation is not exposed. |
| Alpha blending | Available | Standard source-alpha blending is material controlled. |
| Depth write control | Available | Independent depth test and depth write flags. |
| Transparent sorting | Partial | Opaque and transparent custom passes are separated; transparent meshes retain submission order (no per-fragment or per-draw depth sort). |
| Offscreen/framebuffer capture | Missing | See `frame_capture_blockers.md`. |
| Postprocess/fullscreen passes | Missing | No public render-target or subpass abstraction. |

See `custom_mesh_api.md` for the mesh/material API and `volume_rendering.md` for the
volume-specific path.
