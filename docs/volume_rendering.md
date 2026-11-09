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

**Editing a transfer function.** `app.updateTransferFunction(transfer, points)` re-uploads
the lookup texture in place (3.1+): every volume using it shows the new curve on the next
frame, the handle stays valid, and there is no device-idle wait, so it is cheap enough to
call every frame while a user drags a control point. `destroyTransferFunction` still waits
for the device and refuses a function a volume is using.

`ExponentialExtinction` is the default for new descriptions. It computes a physical world-space step length and uses `1 - exp(-extinction * stepLength / referenceStepLength)`. This keeps appearance substantially more stable as `raymarchSteps` changes. `densityScale` scales both the sampled scalar and extinction, while `opacityScale` is the artistic extinction multiplier.

`LinearAlpha` preserves VOL-1 behavior: transfer alpha is multiplied by `opacityScale` once per sample. Its appearance therefore depends strongly on step count. Set `normalizeOpacityByStepLength = false` only when intentionally treating each sample as one reference interval.

By default (`VolumeColorSource::TransferFunction`) the texture's red channel is the scalar, and the transfer function supplies both colour and opacity. Set `colorSource = VolumeColorSource::TextureRgba` to colour a volume by a second quantity: the texture must then be `RGBA8_UNORM`, its RGB is used directly as the colour, and its alpha is the scalar that (times `densityScale`) the transfer function maps to opacity. The transfer function's own colour is ignored in this mode. `createVolume` and `updateVolume` throw `std::invalid_argument` if the texture is not RGBA8.

```cpp
// Colour by speed, opacity by density: rgb = colour map(speed), a = density in 0..255
textureDescription.format = lightGraphics::TextureFormat::RGBA8_UNORM;
description.colorSource = lightGraphics::VolumeColorSource::TextureRgba;
```

`updateVolume` changes bounds, scaling, transfer/volume bindings, ray steps, and clipping. `hideVolume` stops submission without destroying resources.

The clip plane is evaluated in world space and retains samples for which `dot(position, normal) <= offset`. The clip box uses normalized texture coordinates. Both can be enabled independently.

Custom mesh shaders receive the camera UBO at set 0, binding 0, a model-matrix vertex push constant, and attributes at locations 0..3. Volume shaders additionally receive set 1 binding 0 as `sampler3D`, binding 1 as the transfer lookup, and the documented built-in volume push block. Arbitrary descriptor and uniform layouts remain outside the public API.

The example at `examples/VolumeFogExample` compares a 64-step linear volume with a 256-step exponentially normalized volume, registers two transparent reference rings with sort keys, and orbits the camera.

## Render styles (3.2)

`VolumeRenderDescription` controls the data, transfer function, clipping and opacity.
How rays turn into pixels is set separately, so the 3.x ABI is unchanged:

```cpp
lightGraphics::VolumeStyleDescription style;
style.style = lightGraphics::VolumeRenderStyle::MaximumIntensity; // MIP
app.setVolumeStyle(volume, style);

style.style = lightGraphics::VolumeRenderStyle::Isosurface;
style.isoValue = 0.42f;   // normalised scalar, after densityScale
app.setVolumeStyle(volume, style);

style.style = lightGraphics::VolumeRenderStyle::Composite;
style.shading = true;     // headlight gradient lighting
style.shadingStrength = 0.8f;
app.setVolumeStyle(volume, style);
```

| Style | Per ray | Colour and opacity |
|---|---|---|
| `Composite` | front-to-back emission/absorption (default) | transfer function, optional gradient shading |
| `MaximumIntensity` | largest scalar | TF at that scalar; alpha = TF alpha x opacityScale x 4 |
| `MinimumIntensity` | smallest scalar | as above |
| `Average` | mean scalar (a simulated radiograph) | as above |
| `Isosurface` | first sample >= isoValue, refined by 6 bisection steps | TF colour at the iso value, gradient-lit, opaque |

With `VolumeColorSource::TextureRgba` the texture's RGB colours every style and its alpha
is the scalar. Gradients use central differences one texel apart (6 extra fetches per
shaded sample, only where the sample contributes).

## Bricked (sparse) volumes (3.3)

A volume that is larger than one 3D texture, or mostly empty, is split into cubic bricks.
Only the bricks you make resident use GPU memory: they live in an **atlas** (one `Texture3D`)
and a small **brick index** texture tells the ray marcher where each one is. Samples in a
non-resident brick read as zero. Every render style works unchanged, and the picture is
identical to the same data in one texture (the integration test compares all five styles
pixel for pixel).

```cpp
lightGraphics::BrickedTextureDescription bd;
bd.voxels = {4000, 3000, 2500};          // whole volume; not limited by maxImageDimension3D
bd.brickSize = 64;                       // voxels per edge, 8..254
bd.format = lightGraphics::TextureFormat::R16_UNORM;
bd.atlasBricks = {10, 10, 9};            // capacity: 900 bricks = 660 x 660 x 594 texels
auto bricks = app.createBrickedTexture(bd);

// Each brick is (brickSize + 2)^3 samples: a one-voxel border copied from the neighbouring
// bricks (edge voxels replicated at the volume's faces) keeps trilinear filtering seamless.
std::vector<lightGraphics::BrickUpload> uploads = {{{bx, by, bz}, samples, byteSize}, /* ... */};
std::size_t placed = app.uploadBricks(bricks, uploads);   // evicts least recently used if full

lightGraphics::VolumeRenderDescription vd;
vd.volumeTexture = app.brickedAtlas(bricks);             // the atlas is the volume texture
vd.transferFunction = transfer;
vd.volumeMin = {...}; vd.volumeMax = {...};              // world box of the whole volume
auto volume = app.createVolume(vd);
app.setVolumeBricks(volume, bricks);                     // sample through the brick index
app.drawVolume(volume, {lightGraphics::RenderLayer::Volume, 0.0f});
```

`evictBricks`, `evictAllBricks`, `isBrickResident`, `residentBrickCount`, `brickCapacity` and
`brickGrid` manage residency; `setVolumeBricks(volume, {})` detaches. Choosing *which* bricks to
keep is the application's job -- typically the bricks that are not transparent under the
current transfer function, at a resolution level whose non-empty bricks fit the atlas.

`BrickPool` (the slot bookkeeping with LRU eviction) and `updateTexture3DRegions` (batched
partial updates of any `Texture3D`, one staging buffer and one submission) are public too.
The atlas and index are owned by the bricked texture: `destroyBrickedTexture` frees them and
`destroyTexture3D` refuses them. Limits: `atlasBricks` is at most 255 per axis, and
`atlasBricks * (brickSize + 2)` and the brick grid must fit `maxImageDimension3D`.

Implementation notes: the brick index is an RGBA8 texture (slot x, y, z and 255 when
resident) read with `texelFetch`; the shader's `fetchTexel()` replaces direct texture reads.
The volume's voxel counts travel in the unused `w` components of the push block. Non-bricked
volumes take the original path unchanged.
