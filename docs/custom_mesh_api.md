# Custom mesh API

`MeshVertex` has a fixed public layout: position, normal, RGBA color, and UV. Meshes use an indexed triangle list; validation rejects empty data, incomplete triangles, non-finite attributes, and invalid indices.

```cpp
lightGraphics::MeshData data = makeMesh();
auto mesh = app.createStaticMesh(data);

lightGraphics::MaterialDescription materialDescription;
materialDescription.shaderProgram.selection =
	lightGraphics::ShaderSelection::VertexColorLit;
auto material = app.createMaterial(materialDescription);

app.drawMesh(mesh, lightGraphics::Transform{}, material);
```

The overload accepting `DrawOptions` places the mesh into `Opaque`, `Volume`, `Transparent`, `Overlay`, `User0`, or `User1`. Within a layer, `sortKey` and then stable submission index determine order. The simple overload remains source-compatible and derives the default layer from the material's blending state.

Dynamic meshes declare maximum counts once and update without reallocating their Vulkan buffers:

```cpp
auto mesh = app.createDynamicMesh(4096, 12288);
app.updateDynamicMesh(mesh, newData);
```

Draw registrations persist until `clearMeshDraws()`, or until their mesh/material is destroyed. Handles contain an index and generation; stale and invalid handles throw with a diagnostic. Custom SPIR-V must implement the descriptor and vertex ABI described in `volume_rendering.md`.
