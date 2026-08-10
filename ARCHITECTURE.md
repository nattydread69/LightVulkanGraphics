# LightVulkanGraphics Architecture

## Version 2.1.0

## Overview

LightVulkanGraphics (LVG) is a high-performance Vulkan-based graphics library designed for real-time 3D visualization. Version 2.1.0 introduces a well-defined public API with clear separation of concerns and component-based organization.

## Architecture Tiers

### Tier 1: Public API (User-Facing)

**Entry Point:** `LightVulkanGraphicsPublic.h`

This is the primary header users should include. It re-exports and organizes all public APIs by component.

**Components:**

#### Core Graphics Component
- **Purpose:** Main graphics rendering and scene management
- **Key Classes:**
  - `lightVulkanGraphics` - Main application class
  - `SceneGraph` - Scene hierarchy management
  - `pObject` - Renderable object representation
  - `Camera` - View management
  - `Light` - Lighting support
- **Headers:**
  - `lightVulkanGraphics.h` (main)
  - `SceneGraph.h`
  - `pObject.h`
  - `GraphicsModel.h`
  - `Camera.h`
  - `Light.h`

#### Asset Loaders Component
- **Purpose:** Loading models and rigged assets
- **Key Classes:**
  - `FBXLoader` - FBX file format support
  - `RiggedObject` - Skinned/animated model support
- **Headers:**
  - `FBXLoader.h`
  - `RiggedObject.h`
- **Dependencies:**
  - Assimp (optional)

#### GUI Component (LVGUI)
- **Purpose:** Immediate-mode GUI framework
- **Located:** `lightVulkanGraphics/ui/`
- **Key Classes:**
  - `GuiContext` - GUI context and management
  - `Panel` - Floating UI window
  - `Widget` - Base widget class
  - Various widgets (Button, Slider, DropDown, etc.)
- **Conditional:** Only available if `LVG_BUILD_UI=ON`

#### Advanced Rendering Component
- **Purpose:** Specialized rendering techniques
- **Key Classes:**
  - `VolumeRendering` - Volume/fog rendering
  - `RotationGlyph` - Rotation visualization
- **Headers:**
  - `VolumeRendering.h`
  - `RotationGlyph.h`

### Tier 2: Internal Implementation

**Location:** `src/` and internal headers

**Characteristics:**
- Not directly included by users
- Implementation details
- May change between minor versions
- Compiled into library

**Examples:**
- Vulkan implementation details
- GPU command buffer management
- Shader compilation
- Memory management internals

## Directory Structure

```
lightVulkanGraphics/
├── include/                          # Public headers
│   ├── LightVulkanGraphicsPublic.h   # Main facade (2.1.0 NEW)
│   ├── lightVulkanGraphics.h         # Main API
│   ├── VkApp.h                       # VkApp impl details
│   ├── Camera.h                      # Camera API
│   ├── Light.h                       # Lighting API
│   ├── SceneGraph.h                  # Scene hierarchy
│   ├── pObject.h                     # Object definition
│   ├── GraphicsModel.h               # Base class
│   ├── FBXLoader.h                   # FBX support
│   ├── RiggedObject.h                # Rigged models
│   ├── VolumeRendering.h             # Volume rendering
│   ├── RotationGlyph.h               # Rotation glyphs
│   ├── LightVulkanGraphicsLogging.h  # Logging utilities
│   ├── pHeaders.h                    # Common declarations
│   └── lightVulkanGraphics/          # Namespaced headers
│       └── ui/                       # GUI framework
│           ├── GuiContext.h
│           ├── Panel.h
│           ├── Widget.h
│           ├── widgets/
│           │   ├── Button.h
│           │   ├── Slider.h
│           │   ├── DropDown.h
│           │   └── ...
│           └── ...
│
├── src/                              # Implementation (internal)
│   ├── VkApp.cpp
│   ├── ui/
│   └── ...
│
├── cmake/                            # Build configuration
│   ├── LightVulkanGraphicsConfig.cmake.in
│   ├── LightVulkanGraphicsVersion.h.in
│   └── ...
│
├── docs/                             # Documentation (generated)
│
├── examples/                         # Examples and tests
│   ├── external_usage_example/       # Usage as external lib (2.1.0 NEW)
│   └── ...
│
├── CMakeLists.txt                    # Build configuration
├── Doxyfile.in                       # Documentation config (2.1.0 NEW)
├── INSTALL.md                        # Installation guide (2.1.0 NEW)
├── ARCHITECTURE.md                   # This file (2.1.0 NEW)
└── REFACTORING_2_1_0.md             # Refactoring notes (2.1.0 NEW)
```

## Component Dependencies

```
User Code
    ↓
LightVulkanGraphicsPublic.h (Facade)
    ├── Core Graphics
    │   ├── Vulkan SDK
    │   ├── GLM
    │   └── GLFW3
    ├── LVGUI (optional)
    │   └── Freetype (for fonts)
    ├── Asset Loaders
    │   └── Assimp (optional)
    └── Advanced
        ├── GLM
        └── (specialized algorithms)
```

## Public vs Internal Markers

### Public API Indicators
- Located in `include/` directory
- Documented with Doxygen comments (`///`)
- Re-exported in `LightVulkanGraphicsPublic.h`
- Part of semantic versioning guarantee

### Internal Indicators
- Marked with `// INTERNAL` comments
- Implementation files in `src/`
- Vulkan implementation details
- Memory management specifics
- GPU synchronization primitives

## Versioning Strategy

### Version Format: MAJOR.MINOR.PATCH

**2.1.0**
- **MAJOR (2)**: ABI compatibility breaking changes (rare)
- **MINOR (1)**: New features, enhancements (backward compatible)
- **PATCH (0)**: Bug fixes only

### Compatibility Guarantees

**Within 2.x:**
- All public APIs remain stable
- New APIs may be added
- Deprecated APIs marked with `[[deprecated]]`
- Binary compatibility maintained

**Major Version Changes (2→3):**
- May break binary compatibility
- Requires recompilation
- May deprecate APIs

## Build Targets

### Main Targets
- `libLightVulkanGraphics` - Core library
- `LightVulkanGraphicsUI` - GUI framework (if enabled)
- Examples - Demo applications

### CMake Export Targets
- `LightVulkanGraphics::LightVulkanGraphics` - Main library
- `LightVulkanGraphics::Core` - Alias for main
- `LightVulkanGraphics::UI` - GUI framework
- `LightVulkanGraphics::Loaders` - Asset loaders

## Integration Patterns

### Pattern 1: Direct Inclusion (Original)
```cpp
#include "lightVulkanGraphics.h"
// Use: lightGraphics::lightVulkanGraphics
```

**Pros:** Direct, minimal overhead
**Cons:** Doesn't clearly show dependencies

### Pattern 2: Facade Inclusion (Recommended)
```cpp
#include "LightVulkanGraphicsPublic.h"
// Use: lightGraphics::lightVulkanGraphics
```

**Pros:** Clear API organization, good for Doxygen
**Cons:** Larger include transitively

### Pattern 3: Component-Based (Projects)
```cpp
// In CMakeLists.txt:
find_package(LightVulkanGraphics REQUIRED COMPONENTS Core UI)
target_link_libraries(myapp PRIVATE LightVulkanGraphics::UI)
```

**Pros:** Optimal linking, clear dependencies
**Cons:** Requires proper CMake setup

## Porting Guidelines

### For New Projects
Use Pattern 3 with `LightVulkanGraphicsPublic.h` includes.

### For Existing Projects
- Continue with current includes (fully supported)
- Gradually migrate to `LightVulkanGraphicsPublic.h`
- Update CMakeLists.txt to use `find_package()` when convenient
- No immediate action required

## Future Enhancements

**Planned for 2.2+:**
- Additional widget types
- Performance profiling API
- Compute shader support
- Advanced material system

**Planned for 3.0:**
- Optional dependency reduction
- Module-based architecture
- Vulkan 1.4 features
- Improved multithreading

## Key Design Decisions

### Decision 1: Facade Header
**Rationale:** Users need a clear entry point with organized imports
**Alternative Considered:** Multiple small headers (rejected - too fragmented)

### Decision 2: Component-Based CMake
**Rationale:** Projects vary in needs; optional components reduce dependencies
**Alternative Considered:** Monolithic target (rejected - bloats small projects)

### Decision 3: Backward Compatibility
**Rationale:** Multiple projects depend on this; breakage is costly
**Alternative Considered:** Clean break in major version (rejected - unnecessary)

## Performance Considerations

### Zero-Cost Abstractions
- Facade includes use `#include` (not inline functions)
- No virtual function calls in hot paths
- Thin wrapper around Vulkan

### Component Separation
- Unused components not linked
- Header-only where possible
- Lazy initialization for expensive resources

## Security Considerations

- No remote network access in core library
- File loading uses safe abstractions (Assimp)
- Vulkan validation available in debug builds
- Memory safety through RAII principles

---

**LightVulkanGraphics 2.1.0** — Well-architected, stable, and extensible.
