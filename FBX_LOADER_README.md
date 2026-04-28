# FBX Loader for LightVulkanGraphics

This document explains how to use the FBX loader functionality added to the LightVulkanGraphics library for importing rigged FBX models.

## Overview

The FBX loader provides comprehensive support for loading FBX files with rigged models, including:
- Bone hierarchies and transformations
- Animation keyframes and channels
- Vertex data with bone weights
- Material information
- Multiple mesh support

## Dependencies

The FBX loader uses the **Assimp** library (Open Asset Import Library) which is automatically integrated into the CMake build system.

### Assimp Features Used
- FBX file format support
- Bone and animation processing
- Mesh triangulation and optimization
- Material and texture handling

## Classes

### FBXLoader
The main class for loading FBX files.

```cpp
#include "FBXLoader.h"

lightGraphics::FBXLoader loader;
auto model = loader.loadModel("path/to/model.fbx");
```

### RiggedObject
Extends `pObject` to support rigged models with animations.

```cpp
#include "RiggedObject.h"

auto riggedObject = std::make_unique<lightGraphics::RiggedObject>(
    glm::vec3(0.0f, 0.0f, 0.0f),  // Position
    glm::vec3(1.0f, 1.0f, 1.0f),  // Scale
    glm::quat(1.0f, 0.0f, 0.0f, 0.0f), // Rotation
    "MyRiggedModel", // Name
    70.0f, // Mass
    "models/human.fbx" // FBX file path
);
```

## Data Structures

### RiggedModel
Contains the complete loaded model data:
- `meshes`: Vector of `RiggedMesh` objects
- `animations`: Vector of `Animation` objects
- `bones`: Global bone hierarchy
- `boneMapping`: Maps bone names to indices

### RiggedMesh
Individual mesh data:
- `vertices`: Vector of `RiggedVertex` objects with bone weights
- `indices`: Triangle indices
- `bones`: Local bone data for this mesh
- `materialName`: Associated material name

### RiggedVertex
Vertex data with bone information:
- `position`: 3D position
- `normal`: Surface normal
- `texCoords`: Texture coordinates
- `boneWeights`: Weights for up to 4 bones
- `boneIndices`: Indices of the bones

### Animation
Animation data:
- `name`: Animation name
- `duration`: Duration in seconds
- `ticksPerSecond`: Animation frame rate
- `channels`: Vector of `AnimationChannel` objects

## Usage Examples

### Basic Loading

```cpp
#include "FBXLoader.h"

lightGraphics::FBXLoader loader;
auto model = loader.loadModel("character.fbx");

if (model) {
    std::cout << "Loaded " << model->meshes.size() << " meshes" << std::endl;
    std::cout << "Found " << model->animations.size() << " animations" << std::endl;
} else {
    std::cerr << "Error: " << loader.getLastError() << std::endl;
}
```

### Animation Control

```cpp
#include "RiggedObject.h"

// Create rigged object
auto character = std::make_unique<lightGraphics::RiggedObject>(...);

// Play animation by name
character->playAnimation("walk", true); // true = loop

// Play animation by index
character->playAnimation(0, true);

// Control playback
character->pauseAnimation();
character->resumeAnimation();
character->stopAnimation();

// Set animation speed
character->setAnimationSpeed(1.5f); // 1.5x speed

// Update animation (call every frame)
character->updateAnimation(deltaTime);
```

### Accessing Bone Data

```cpp
// Get all bone transforms
auto boneTransforms = character->getBoneTransforms();

// Get specific bone transform
glm::mat4 headTransform = character->getBoneTransform("Head");

// Get animation information
auto animationNames = character->getAnimationNames();
float duration = character->getAnimationDuration();
bool isPlaying = character->isAnimating();
```

## Building

The FBX loader is automatically included when you build the LightVulkanGraphics library. By default, Assimp must already be installed or made available via `ASSIMP_ROOT`. If you explicitly want CMake to fetch Assimp from source, configure with `-DASSIMP_USE_FETCHCONTENT=ON` and allow network access during configuration.

```bash
cmake -S . -B build
cmake --build build
```

## Examples

### Simple FBX Analysis
```bash
./build/simple_fbx_loader_example path/to/model.fbx
```

This will load and analyze an FBX file, printing detailed information about:
- Meshes and vertices
- Bone hierarchy
- Animations and keyframes
- Material data

### Basic Rigged Model Example
```bash
./build/fbx_rigged_example
```

This example demonstrates:
- Loading a rigged FBX model into `RiggedObject`
- Starting the first available animation
- Updating animation state each frame
- Printing periodic bone-transform debug output

It is a basic runtime example, not a full interactive animation viewer with custom playback controls.

## Supported FBX Features

### ✅ Supported
- Rigged models with bone hierarchies
- Animation keyframes (position, rotation, scale)
- Multiple meshes per model
- Bone weights and vertex skinning
- Material names and basic properties
- Multiple animations per file

### ⚠️ Limited Support
- Complex material properties (textures, shaders)
- Advanced animation blending
- Morph targets and shape keys
- Physics constraints

### ❌ Not Supported
- FBX-specific shader materials
- Advanced animation curves
- Custom properties and metadata

## Troubleshooting

### Common Issues

1. **"Failed to load FBX file"**
   - Check file path is correct
   - Ensure file is a valid FBX file
   - Check file permissions

2. **"No animations found"**
   - Verify the FBX file contains animation data
   - Check if animations are properly exported from 3D software

3. **"Bone transforms not updating"**
   - Ensure `updateAnimation()` is called every frame
   - Check that animation is playing (`isAnimating()`)

4. **Build errors with Assimp**
   - Ensure CMake can find or download Assimp
   - Check that all required dependencies are installed

### Debug Information

Enable debug output by checking the console for:
- Model loading progress
- Animation playback status
- Bone transform updates
- Error messages

## Integration with Rendering

`RiggedObject` instances can be rendered by the library with
`lightGraphics::lightVulkanGraphics::addRiggedObject()`. The renderer uploads
the loaded mesh data, updates the skinned vertex buffers as animations advance,
and uses the bundled rigged-mesh shaders. Applications are still responsible
for loading the model, choosing an animation, calling `updateAnimation()` each
frame, and adding the object before `finalizeScene()`.

```cpp
auto character = std::make_shared<lightGraphics::RiggedObject>(
    glm::vec3(0.0f), glm::vec3(1.0f),
    glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
    "Character", 70.0f, "models/character.fbx");

if (!character->getModel()) {
    std::cerr << character->getLastError() << std::endl;
    return;
}

if (character->getAnimationCount() > 0) {
    character->playAnimation(0, true);
}

app.addRiggedObject(character);
app.setUpdateCallback([character](float dt) {
    character->updateAnimation(dt);
});
```

Advanced animation blending, morph targets, and custom material workflows are
not implemented yet.

## Performance Considerations

- **Bone transforms** are recalculated every frame when animating
- **Large models** with many bones may impact performance
- **Animation interpolation** uses linear/slerp for smooth results
- **Memory usage** scales with vertex count and bone complexity

## License

This FBX loader implementation is released under the same LGPL-3.0-or-later license as the LightVulkanGraphics library.

## Contributing

When contributing to the FBX loader:
- Test with various FBX files from different 3D software
- Ensure bone hierarchies are correctly processed
- Verify animation timing and interpolation
- Add support for additional FBX features as needed
