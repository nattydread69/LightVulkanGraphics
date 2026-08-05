# Rotation-ring glyphs

`include/RotationGlyph.h` builds a procedural mesh and orientation transform
for visualising an angular quantity -- angular velocity, angular momentum, or
torque -- as an oriented annular arrow lying in its own plane of rotation,
rather than as the conventional axial (pseudovector) arrow perpendicular to
that plane. It is a graphics-only module: it builds `MeshData` and
`Transform` values using the existing custom-mesh API (`docs/custom_mesh_api.md`),
owns no Vulkan resources, requires no device or window to use or test, and
does not alter the physical definitions of the quantities it draws.

## Why an oriented-plane glyph

The conventional way to draw an angular quantity is a straight arrow along
the rotation axis (the right-hand-rule pseudovector). That representation is
compact and is what most physics texts use, but it asks the reader to
mentally reconstruct the actual plane of rotation from a single perpendicular
line -- and it draws angular velocity, angular momentum, and torque with
exactly the same visual shape as a translational vector, even though they are
a different kind of quantity.

A rotation-ring glyph instead lies flat in the real plane the rotation acts
in, with its curved arrowhead showing which way it turns. Reading it doesn't
require reconstructing a plane from a normal; the plane is drawn directly.
`docs/rotation_glyphs.md`'s companion example (`rotation_glyph_example.cpp`)
draws both side by side.

This is not a replacement for the axial arrow -- `makeRotationRingTransform`
places a ring so a conventional arrow drawn along the same axis, through the
same centre (via the existing `ShapeType::ARROW` / `computeArrowTransform`
helpers), reads as the same rotation shown two ways. Neither this module nor
the glyph it draws is a new kind of vector; it is a drawing of one, and the
two representations should agree.

### Trajectory vs. angular velocity vs. angular momentum vs. torque

These are frequently conflated in casual visualisation and are worth keeping
distinct when choosing what to draw:

- **Trajectory of a body point** is a path in space -- a sequence of
  positions a point on the rotating body sweeps through. It is not an
  angular quantity at all, and a rotation-ring glyph should not be used to
  draw one; a plain polyline or trail is the right tool.
- **Angular velocity (omega)** is the instantaneous rate and plane of
  rotation of a rigid body, independent of where its mass is concentrated.
- **Angular momentum (L)** depends on both the angular velocity and the mass
  distribution (`L = I * omega` for a rigid body); its plane can differ from
  omega's for a body whose inertia tensor isn't aligned with the rotation
  axis.
- **Torque (tau)** is the rate of change of angular momentum -- a cause
  acting on the system, not a state of it.

A rotation-ring glyph is a valid way to draw any of the latter three (each
has a well-defined plane, sense, and magnitude at an instant), but drawing
one doesn't imply anything about the others; an application choosing to
visualise multiple of these quantities for the same body should give each
its own glyph, as the bundled example does.

## Local construction and direction convention

`buildRotationRingMesh` authors the glyph around the local origin in the
local XY plane, with its front face normal along local `+Z`. Viewed from the
`+Z` side (the viewer on the `+Z` side of the plane, looking toward the
origin):

- `RotationSense::CounterClockwise` travels in increasing local polar angle
  (`atan2(y, x)` increasing);
- `RotationSense::Clockwise` travels in decreasing local polar angle.

The ribbon covers the full circle minus a gap of `gapAngleRadians`, centred
on `gapCentreAngleRadians`. Travelling from the gap in the glyph's sense, the
ribbon widens into an arrowhead over its last `arrowHeadAngleRadians` and
terminates in a point -- so the tip always sits just before the far edge of
the gap, approached from the sense's direction of travel.

`makeRotationRingTransform` aligns local `+Z` with the requested world-space
`planeNormal`, and local `+X` with `inPlaneReference` projected into the
plane -- since local angle zero is where the gap is centred by default, the
in-plane reference controls where the gap (and the general orientation of
the glyph) appears in world space. This convention is deterministic: the
same description and transform inputs always produce the same mesh and
orientation, on any plane.

## `RotationRingDescription` fields

| Field | Meaning |
| --- | --- |
| `radius` | Radius of the ribbon's centreline. Must be positive. |
| `bandWidth` | Radial width of the body of the ribbon -- the visual magnitude channel (see below). Must be positive, and small enough that `radius - bandWidth/2` stays positive. |
| `thickness` | Extruded depth of the ribbon along local Z. Must be positive. |
| `gapAngleRadians` | Angular width of the open gap in the ring, centred on `gapCentreAngleRadians`. Must be in `(0, 2*pi)`. |
| `gapCentreAngleRadians` | Local angle the gap (and therefore the ribbon's start/end) is centred on. |
| `arrowHeadAngleRadians` | Angular length of the arrowhead at the ribbon's leading end. Must be positive and small enough to fit inside the visible arc (`2*pi - gapAngleRadians`). |
| `arrowHeadWidthScale` | Arrowhead width as a multiple of `bandWidth`. Must be greater than 1. |
| `arcSegments` | Tessellation of the curved body and arrowhead. Must be at least 8; the default (96) is smooth, lower values are lighter-weight. Vertex/index counts depend only on this and the two angle fields above -- never on `radius`, `bandWidth`, `color`, or `arrowHeadWidthScale` -- so a glyph's topology is stable while its band width (magnitude) animates. |
| `sense` | `CounterClockwise` or `Clockwise`; see the direction convention above. |
| `color` | Vertex color baked into the mesh (RGBA). |

`validateRotationRingDescription()` rejects non-finite values and the
constraints above with `std::invalid_argument`, and is called automatically
by `buildRotationRingMesh()`. It never clamps -- an invalid description is a
caller bug to fix, not silently repaired.

## Static-mesh usage

```cpp
#include "RotationGlyph.h"

lightGraphics::RotationRingDescription description;
description.radius = 1.0f;
description.bandWidth = 0.16f;
description.color = glm::vec4(0.15f, 0.65f, 1.0f, 1.0f);
description.sense = lightGraphics::RotationSense::CounterClockwise;

const lightGraphics::MeshHandle mesh =
	app.createStaticMesh(lightGraphics::buildRotationRingMesh(description));

lightGraphics::MaterialDescription materialDescription;
materialDescription.shaderProgram.selection = lightGraphics::ShaderSelection::VertexColorLit;
const lightGraphics::MaterialHandle material = app.createMaterial(materialDescription);

const glm::vec3 centre(0.0f, 0.0f, 0.0f);
const glm::vec3 planeNormal(0.0f, 1.0f, 0.0f); // horizontal plane
app.drawMesh(mesh, lightGraphics::makeRotationRingTransform(centre, planeNormal), material);
```

## Dynamic-mesh usage: updating a glyph's magnitude

Because topology depends only on `arcSegments` (and the angle fields), a
glyph's `bandWidth` can be re-driven every frame into the same dynamic mesh
allocation, with no reallocation and no need to re-register the draw call:

```cpp
const lightGraphics::MeshData initialMesh = lightGraphics::buildRotationRingMesh(description);

const lightGraphics::MeshHandle mesh =
	app.createDynamicMesh(initialMesh.vertices.size(), initialMesh.indices.size());
app.updateDynamicMesh(mesh, initialMesh);
app.drawMesh(mesh, lightGraphics::makeRotationRingTransform(centre, planeNormal), material);

// Later, once per frame:
description.bandWidth = nextBandWidth;
app.updateDynamicMesh(mesh, lightGraphics::buildRotationRingMesh(description));
```

`drawMesh` only needs to be called once per mesh/material/transform
combination; `updateDynamicMesh` replaces the mesh's vertex/index content in
place for that existing registration.

## Mapping a physical magnitude to band width

The module intentionally has no notion of physical units or an
application-specific scale -- only `bandWidth`, a plain radial width. Map a
normalised magnitude onto a chosen visual range yourself, e.g.:

```cpp
const float normalizedMagnitude =
	std::clamp(std::abs(angularMomentum) / referenceAngularMomentum, 0.0f, 1.0f);

description.bandWidth =
	minimumBandWidth +
	normalizedMagnitude * (maximumBandWidth - minimumBandWidth);
```

Choose `referenceAngularMomentum` (or an equivalent reference value for
angular velocity/torque) however suits the application -- a fixed physical
scale, the maximum observed value in a session, or a per-body normalisation.

## Transform / orientation usage

`makeRotationRingTransform(centre, planeNormal, inPlaneReference)`:

- normalises `planeNormal` and aligns it with local `+Z`;
- projects `inPlaneReference` into the plane and aligns it with local `+X`
  (this is what controls where the gap appears in world space -- pass a
  reference direction from the visualisation's own frame, e.g. the body's
  "forward" axis, for a consistent look across many glyphs);
- if `inPlaneReference` is zero-length or (near-)parallel to `planeNormal`
  (including the default, zero-vector argument), a deterministic fallback
  axis is used instead: whichever world axis (X, Y, or Z) is least parallel
  to `planeNormal`;
- throws `std::invalid_argument` if `planeNormal` is zero-length or
  non-finite.

The returned `Transform` has `scale = (1, 1, 1)`; apply any additional
scaling by scaling `radius`/`bandWidth` in the description instead, so the
mesh's own vertex data stays the source of truth for its size.

## Comparing against the conventional axial arrow

Draw the two representations of the same rotation side by side using the
existing built-in arrow support and the same centre/axis the ring uses:

```cpp
glm::vec3 arrowPosition;
glm::quat arrowRotation;
glm::vec3 arrowScale;
app.computeArrowTransform(centre - planeNormal * 0.8f, centre + planeNormal * 0.8f,
                          0.035f, arrowPosition, arrowRotation, arrowScale);
app.addObject(lightGraphics::ShapeType::ARROW, arrowPosition, arrowScale,
             glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), arrowRotation,
             "Axial Arrow (conventional)", 0.0f);
```

## Example

`examples/rotation_glyph_example.cpp` (CMake target `rotation_glyph_example`)
draws three glyphs -- angular velocity in a horizontal plane, angular
momentum in an oblique plane, and torque in a third plane -- with different
band widths, both rotational senses, a conventional axial arrow next to the
angular-velocity glyph for direct comparison, small centre markers, and
on-screen text explaining the convention. The angular-velocity glyph is
built as a dynamic mesh whose band width animates over time via
`updateDynamicMesh`, without reallocating or re-registering its draw call;
the other two are built as static meshes.
