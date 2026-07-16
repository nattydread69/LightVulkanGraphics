# Volume rendering capability audit

This audit records the public state before LVG-VOL-1 and the capability delivered by this milestone. Status uses:

```cpp
enum class GraphicsCapabilityStatus
{
	AvailablePublicApi,
	AvailableInternalOnly,
	PartiallyAvailable,
	Missing,
	Unknown
};
```

| Capability | Before LVG-VOL-1 | After LVG-VOL-1 | Notes |
|---|---|---|---|
| Custom mesh submission | AvailableInternalOnly | AvailablePublicApi | Indexed triangle meshes with position, normal, color, and UV. |
| Static mesh buffers | AvailableInternalOnly | AvailablePublicApi | Staged into device-local buffers. |
| Dynamic mesh buffers | PartiallyAvailable | AvailablePublicApi | Capacity-bounded host-visible update path. |
| Custom vertex attributes | Missing | PartiallyAvailable | A stable `MeshVertex` layout is public; arbitrary layouts are not. |
| Custom shader modules | AvailableInternalOnly | AvailablePublicApi | SPIR-V vertex/fragment paths may be selected by a material. |
| Custom material/pipeline creation | AvailableInternalOnly | AvailablePublicApi | Blend, depth, cull, polygon, and shader state are public. |
| 2D texture upload | AvailableInternalOnly | AvailableInternalOnly | Used internally by rigged materials and transfer functions. |
| 3D texture upload | Missing | AvailablePublicApi | Five formats are mapped; R32 scalar upload is the baseline. |
| Storage images | Missing | Missing | Not needed by the initial sampled-volume path. |
| Descriptor set extension points | Missing | PartiallyAvailable | Built-in volume and camera layouts plus custom shader ABI; arbitrary descriptor declarations are not exposed. |
| Uniform buffer extension points | Missing | PartiallyAvailable | Camera UBO and push-constant ABIs are documented; arbitrary UBO allocation is not exposed. |
| Alpha blending | AvailableInternalOnly | AvailablePublicApi | Standard source-alpha blending is material controlled. |
| Depth write control | Missing | AvailablePublicApi | Independent depth test and depth write flags. |
| Transparent sorting | Missing | PartiallyAvailable | Opaque and transparent custom passes are separated; transparent meshes retain submission order. |
| Offscreen/framebuffer capture | Missing | Missing | See `frame_capture_blockers.md`. |
| Postprocess/fullscreen passes | Missing | Missing | No public render-target or subpass abstraction. |

The machine-readable pre-milestone audit is in `runs/light_vulkan_graphics_capability_audit.csv`. The final status is in `runs/lvg_vol_1_feature_status.csv`.
