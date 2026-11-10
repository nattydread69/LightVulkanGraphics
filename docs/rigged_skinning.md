# Rigged models: bind pose, skinning modes and distorted meshes

A skinned (rigged) model that renders as a clean T-pose but turns into stretched, spiky limbs
as soon as an animation plays is almost always a **bind-pose problem**, not a rendering bug.
This note records what the bind pose is, how LVG picks a skinning mode, how to diagnose a
distorted model, and a known pitfall with FBX files written by assimp (for example by
SoundRigs' "Fix and export").

## Two bind poses

A skinned vertex is moved by `boneGlobal * inverseBind`, so every bone needs two things:

1. its **current global transform** (from the node hierarchy plus the animation), and
2. its **inverse bind matrix**, the transform that takes the mesh's bind-pose vertices into
   that bone's space. In assimp this is `aiBone::mOffsetMatrix`.

The file can describe the bind pose in two ways: the skin cluster / offset matrices (what the
skin was really bound against), and the node hierarchy's own rest transforms. A well-formed
rig has both agree. A rig does **not** have to: many exporters leave the nodes at the first
animation frame or any other rest pose, and only the offset matrices say how the skin was
bound. Animation keys are absolute local transforms, so animated bones do not care about the
node rest pose, but the bind pose must still be right.

LVG uses the skin cluster's offset matrix whenever a bone has one
(`Bone::hasSkinBindTransform`, `buildRiggedFinalBoneMatrix` in
`include/internal/RiggedSkinning.h`) and falls back to the node-hierarchy bind pose only for
bones nothing is weighted to.

`FBXLoader::loadModel()` also compares the two sources per bone. If at least four bones
disagree by more than 0.02 model units (and one by more than 0.05) it prints

    [FBXLoader] N bone(s) have a node-hierarchy bind pose that disagrees with their skin-cluster
    bind pose by up to X model units. Using the skin-cluster bind pose for those bones ...

and sets `RiggedModel::usesSkinningBindCorrection`. That line means "the file's rest pose is
not its bind pose"; LVG copes, but any tool that rebuilds the bind pose from the nodes will not.

## Skinning correction modes

`RiggedSkinningBindCorrectionMode` (`include/internal/RiggedSkinning.h`) says how the mesh's
own bind transform is folded in:

| Mode | Final matrix |
|---|---|
| `OffsetOnly` | `globalInverse * boneGlobal * offset` |
| `MeshBind` | the above `* mesh.globalBindTransform` |
| `MeshBindWithoutScale` | the above with `mesh.globalBindTransform`'s scale removed |

Which one is right depends on how the importer reports the offset matrices (assimp's FBX
importer gives them mesh-node-local, its glTF importer relative to the skin's common space).
`src/VkAppRigged.cpp` chooses when a rigged object is registered:

1. An override wins: `LIGHT_VULKAN_GRAPHICS_RIGGED_SKINNING_MODE` = `offset-only` (also
   `offset`, `gpu`), `mesh-bind` (`meshbind`, `cpu`) or `mesh-bind-without-scale`.
2. Otherwise each mode is scored on the **registration pose**: vertices gone non-finite, the
   pose's largest extent, and the worst triangle-edge stretch (1 = undistorted; large = long
   spikes). A mode is *plausible* if it is finite, not collapsed below 5% of the source pose's
   extent, and stretches no edge beyond 8x.
3. Among plausible modes the device default (`OffsetOnly` on a real GPU, `MeshBind` on a CPU
   rasteriser such as lavapipe) is kept if its extent is within 25% (or 0.25 units) of the
   tightest; otherwise the tightest wins. If none is plausible, the least-distorted usable mode
   wins, and only if none is even usable does the device default apply (`selectRiggedSkinningModeIndex`).

The log line shows the outcome:

    [RiggedMesh] Skinning correction mode: offset-only (auto, default mesh-bind, extent=1.86, edgeRatio=1.00004, nonFinite=0)

Limits worth knowing:

- The score is taken **once, at registration**. Damage that only appears in an animated pose
  (see the pitfall below) is invisible to it.
- Headless software rendering can differ from a real GPU because the default mode differs.
  Force the mode with the environment variable when comparing, for example
  `LIGHT_VULKAN_GRAPHICS_RIGGED_SKINNING_MODE=offset-only`.

## Diagnosing a distorted model

1. Does it look right in the bind / T-pose and wrong only when animated? Then suspect the bind
   matrices or the animation data, not the shader.
2. Read the `[RiggedMesh] Skinning correction mode` line: an `edgeRatio` near 1 and a sane
   `extent` (about the character's height) mean the registration pose is fine. Try all three
   modes with the environment variable; if only one is clean, the importer / mode pairing is
   the problem.
3. Look for the `[FBXLoader] ... bind pose that disagrees` line.
4. Compare against the source file. Read both with assimp, compare each bone's `mOffsetMatrix`
   and each animation channel's keys, then skin a few vertices on the CPU at a few clip times
   (`boneGlobal * offset * vertex`). The comparison finds the broken stage quickly; SoundRigs'
   Fix & export was debugged this way.

## Pitfall: FBX written by assimp (2026-11)

assimp's FBX **exporter** is lossy for rigged, animated models. Found while building
SoundRigs' Fix & export; all of it is assimp behaviour, not LVG's:

- **Bind matrices are rebuilt from the node hierarchy**, not taken from `mOffsetMatrix`. If
  the nodes rest away from the bind pose (the bundled `Worker.fbx` is 3.7 units off at the
  body), every bone's bind changes (rotations 20-60 degrees on that rig). A static pose still
  looks fine; once a clip plays the skin is flung into long spindly limbs (up to 96 units of
  error on a 186-unit character).
  *Workaround*: before exporting, set each bone node to its bind pose,
  `world = meshNodeGlobal * inverse(offset)`, so the writer's reconstruction equals the
  original. Animation keys are absolute, so clips are unaffected. A bone shared by several
  meshes whose source bind poses differ slightly can only keep one of them (about 0.5% of
  height on `Worker.fbx`).
- **Key times are read as seconds**, ignoring ticks-per-second: a 0.75 s clip at 24 ticks per
  second came back 18 s long.
  *Workaround*: divide key times by ticks-per-second before export.
- **Only whole seconds of a clip are kept**; later keys are dropped (`Death`: 1,623 keys became
  237).
  *Workaround*: round each clip's duration up to the next whole second (the last pose is held).
- glTF / GLB export through assimp showed none of these problems in testing (clip times and key
  counts exact, rig loads correctly); prefer `.glb` for rigged, animated assets, or use an FBX
  exporter that writes the real per-mesh cluster transforms.

SoundRigs applies the three workarounds in `fixAndExport` (`src/Fixer.cpp`:
`setBoneNodesToBindPose`, `convertAnimationTimesToSeconds`) and has regression tests for both
the bind pose and the timing.
