# 3D texture API

The public 3D texture path creates `VK_IMAGE_TYPE_3D` images and `VK_IMAGE_VIEW_TYPE_3D` views, uploads through a staging buffer, and leaves the image shader-readable. Dimensions and byte size are validated exactly.

```cpp
lightGraphics::Texture3DDescription description;
description.width = width;
description.height = height;
description.depth = depth;
description.format = lightGraphics::TextureFormat::R32_SFLOAT;

auto texture = app.createTexture3D(
	description,
	values.data(),
	values.size() * sizeof(float));
app.updateTexture3D(texture, values.data(), values.size() * sizeof(float));
```

Formats are `R8_UNORM`, `R16_UNORM`, `R16_SFLOAT`, `R32_SFLOAT`, and `RGBA8_UNORM`. Device format support is checked by Vulkan during resource creation. Mipmap generation is rejected in LVG-VOL-1 with a clear error. A texture cannot be destroyed while referenced by a volume.
