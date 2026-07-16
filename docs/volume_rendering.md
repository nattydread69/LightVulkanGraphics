# Volume rendering

LVG supplies a general proxy-cube raymarcher for scalar fields normalized to 0..1. It samples a 3D texture and 1D transfer lookup, composites front-to-back, optionally jitters the first sample, and terminates when opacity is saturated.

```cpp
auto texture = app.createTexture3D(textureDescription, data, byteSize);
auto transfer = app.createTransferFunction(
	lightGraphics::TransferFunctionPreset::SoftNeutralFog);

lightGraphics::VolumeRenderDescription description;
description.volumeTexture = texture;
description.transferFunction = transfer;
description.volumeMin = {-1.0f, -1.0f, -1.0f};
description.volumeMax = {1.0f, 1.0f, 1.0f};
description.raymarchSteps = 192;
description.opacityScale = 0.25f;
description.opacityModel =
	lightGraphics::VolumeOpacityModel::ExponentialExtinction;
description.referenceStepLength = 1.0f;
description.normalizeOpacityByStepLength = true;

auto volume = app.createVolume(description);
app.drawVolume(volume, {lightGraphics::RenderLayer::Volume, 0.0f});
```

`ExponentialExtinction` is the default for new descriptions. It computes a physical world-space step length and uses `1 - exp(-extinction * stepLength / referenceStepLength)`. This keeps appearance substantially more stable as `raymarchSteps` changes. `densityScale` scales both the sampled scalar and extinction, while `opacityScale` is the artistic extinction multiplier.

`LinearAlpha` preserves VOL-1 behavior: transfer alpha is multiplied by `opacityScale` once per sample. Its appearance therefore depends strongly on step count. Set `normalizeOpacityByStepLength = false` only when intentionally treating each sample as one reference interval.

`updateVolume` changes bounds, scaling, transfer/volume bindings, ray steps, and clipping. `hideVolume` stops submission without destroying resources.

The clip plane is evaluated in world space and retains samples for which `dot(position, normal) <= offset`. The clip box uses normalized texture coordinates. Both can be enabled independently.

Custom mesh shaders receive the camera UBO at set 0, binding 0, a model-matrix vertex push constant, and attributes at locations 0..3. Volume shaders additionally receive set 1 binding 0 as `sampler3D`, binding 1 as the transfer lookup, and the documented built-in volume push block. Arbitrary descriptor and uniform layouts remain outside the public API.

The example at `examples/VolumeFogExample` compares a 64-step linear volume with a 256-step exponentially normalized volume, registers two transparent reference rings with sort keys, and orbits the camera.
