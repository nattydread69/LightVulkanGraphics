# Transparency and depth

`MaterialDescription` independently controls alpha blending, depth testing, and depth writes. Transparent surfaces normally use:

```cpp
lightGraphics::MaterialDescription material;
material.alphaBlendingEnabled = true;
material.depthTestEnabled = true;
material.depthWriteEnabled = false;
```

Custom meshes and volumes can share an application-controlled stable order:

```cpp
app.drawVolume(volume,
	{lightGraphics::RenderLayer::Volume, 0.0f});
app.drawMesh(overlay, transform, transparentMaterial,
	{lightGraphics::RenderLayer::Transparent, 10.0f});
```

The recommended order is:

1. Opaque scene.
2. Volumes or fog.
3. Transparent diagnostic geometry.
4. Overlays.
5. UI and text.

`RenderLayer` sorts first, `sortKey` second, and submission index third. Ordering caches are rebuilt only when registrations or volume visibility change, rather than allocating during every frame. Smaller sort keys draw first; clients should assign keys in the desired approximate back-to-front order.

The simple `drawMesh` overload selects `Opaque` or `Transparent` from material state. For VOL-1 compatibility, the simple `drawVolume` overload retains its old after-transparent behavior by using `Overlay`. Use the `DrawOptions` overload for the recommended `Volume` layer.

Multiple volumes are deterministic by layer, key, and submission order. Intersecting volumes and transparent surfaces remain approximate: order-independent transparency and automatic per-frame camera-distance sorting are not implemented.
