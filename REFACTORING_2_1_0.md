# LightVulkanGraphics 2.1.0 Refactoring

## Overview

Version 2.1.0 introduces comprehensive improvements to make LightVulkanGraphics a first-class installable external library with a well-defined public API, proper versioning, and complete documentation.

## Key Changes

### 1. **Version Bump to 2.1.0**
- Updated from 2.0.0 to 2.1.0 (semantic versioning)
- Version information centralized in `CMakeLists.txt`
- Auto-generated version header at build time
- Version macros for runtime checking

**Files Changed:**
- `CMakeLists.txt`: Version updated to 2.1.0
- Added `LVG_VERSION_MAJOR`, `LVG_VERSION_MINOR`, `LVG_VERSION_PATCH` variables

### 2. **Public API Facade**

New file: `include/LightVulkanGraphicsPublic.h`

**Purpose:** Single entry point for all public API, organized by components

**Components Exposed:**
- **Core**: Main graphics functionality (VkApp, Scene graph, Camera, Lighting)
- **Loaders**: Asset loading (FBX, Rigged objects)
- **UI**: GUI framework (LVGUI) - optional, marked with `#ifdef LVG_BUILD_UI`
- **Advanced**: Volume rendering, Rotation glyphs

**Benefits:**
- Clear separation of public vs internal APIs
- Organized by feature areas
- Easy component discovery
- Doxygen-compatible documentation structure

**Usage:**
```cpp
#include "LightVulkanGraphicsPublic.h"
// All public APIs available in namespace lightGraphics
```

### 3. **Enhanced CMake Configuration**

**Updated Files:**
- `cmake/LightVulkanGraphicsConfig.cmake.in`: Component-aware find_package support
- `CMakeLists.txt`: Added Doxygen build target

**New Capabilities:**
- Component-based find_package()
  ```cmake
  find_package(LightVulkanGraphics 2.1 REQUIRED COMPONENTS Core UI)
  ```
- Version checking
- Proper target exports
- Namespace targets: `LightVulkanGraphics::LightVulkanGraphics`, `LightVulkanGraphics::UI`, etc.

### 4. **Doxygen Documentation**

New file: `Doxyfile.in`

**Features:**
- Auto-generated HTML documentation
- Component-based organization
- Call and caller graphs
- Interactive SVG diagrams
- Light/Dark theme support

**Build Documentation:**
```bash
cmake -B build -DLVG_BUILD_DOXYGEN=ON
cmake --build build --target doxygen
# Output: build/docs/html/index.html
```

### 5. **Installation Guide**

New file: `INSTALL.md`

**Covers:**
- Prerequisites (Vulkan SDK, CMake 3.20+, C++20)
- Local installation to `~/.local` (recommended for development)
- System-wide installation to `/usr/local`
- Using in your own projects with `find_package()`
- Troubleshooting common issues

### 6. **External Usage Example**

New files:
- `examples/external_usage_example/CMakeLists.txt`
- `examples/external_usage_example/main.cpp`

**Demonstrates:**
- Using installed LightVulkanGraphics via `find_package()`
- Linking with component-based targets
- Building without needing library source

## Installation Steps

### Local Installation (Recommended)

```bash
cd lightVulkanGraphics
cmake -B build \
    -DCMAKE_INSTALL_PREFIX=~/.local \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build
```

### Update Shell Configuration

Add to `~/.bashrc` or `~/.zshrc`:
```bash
export CMAKE_PREFIX_PATH="$HOME/.local:$CMAKE_PREFIX_PATH"
export LD_LIBRARY_PATH="$HOME/.local/lib:$LD_LIBRARY_PATH"
```

### Use in Your Project

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApp)

find_package(LightVulkanGraphics 2.1 REQUIRED COMPONENTS Core UI)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE LightVulkanGraphics::LightVulkanGraphics)
```

## Backward Compatibility

✅ **Full backward compatibility maintained**

- All existing headers remain unchanged
- All existing APIs continue to work
- No breaking changes to public interfaces
- Existing projects require no modifications
- Component-based find_package is optional (old direct includes still work)

## Migration Path for Existing Projects

### Old Way (Still Works)
```cpp
#include "lightVulkanGraphics.h"
```

### New Way (Recommended)
```cpp
#include "LightVulkanGraphicsPublic.h"
```

Both work identically. Migration is gradual and optional.

## CMake Variable Changes

### New Variables
- `LVG_VERSION_MAJOR` - Major version (2)
- `LVG_VERSION_MINOR` - Minor version (1)
- `LVG_VERSION_PATCH` - Patch version (0)
- `LVG_BUILD_DOXYGEN` - Enable Doxygen documentation generation

### Existing Variables (Unchanged)
- `LVG_BUILD_EXAMPLES`
- `LVG_BUILD_VOLUME_EXAMPLES`
- `LVG_BUILD_VULKAN_INTEGRATION_TESTS`
- `LVG_ENABLE_WARNINGS`
- `LVG_WARNINGS_AS_ERRORS`
- `LVG_ENABLE_SANITIZERS`
- `LVG_BUILD_UI`

## Installation Directories

### Local Installation (~/.local)
```
~/.local/
├── include/LightVulkanGraphics/          # Headers
├── lib/
│   ├── libLightVulkanGraphics.so         # Library
│   └── cmake/LightVulkanGraphics/        # CMake configs
├── share/
│   ├── doc/LightVulkanGraphics/          # Doxygen docs
│   └── LightVulkanGraphics/spv/          # Shaders
```

### System Installation (/usr/local)
```
/usr/local/
├── include/LightVulkanGraphics/
├── lib/
├── share/doc/LightVulkanGraphics/
└── share/LightVulkanGraphics/spv/
```

## Testing the Refactoring

### Build the Library
```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=~/.local
cmake --build build
```

### Test Installation
```bash
cmake --install build
# Verify files exist
ls -la ~/.local/include/LightVulkanGraphics/
```

### Build Example
```bash
cd examples/external_usage_example
cmake -B build -DCMAKE_PREFIX_PATH=~/.local
cmake --build build
./build/my_graphics_app
```

### Generate Documentation
```bash
cmake -B build -DLVG_BUILD_DOXYGEN=ON
cmake --build build --target doxygen
# Open build/docs/html/index.html in browser
```

## File Summary

**Modified:**
- `CMakeLists.txt` - Version, Doxygen target, variables
- `cmake/LightVulkanGraphicsConfig.cmake.in` - Component support

**Created:**
- `include/LightVulkanGraphicsPublic.h` - Public API facade
- `Doxyfile.in` - Documentation configuration
- `INSTALL.md` - Installation guide
- `REFACTORING_2_1_0.md` - This file
- `examples/external_usage_example/CMakeLists.txt` - Usage example
- `examples/external_usage_example/main.cpp` - Example code

## Next Steps for Users

1. **Install the library:**
   ```bash
   cd lightVulkanGraphics
   cmake -B build -DCMAKE_INSTALL_PREFIX=~/.local
   cmake --build build
   cmake --install build
   ```

2. **Update your projects** (optional, gradual):
   - Update CMakeLists.txt to use `find_package()`
   - Update includes to use `LightVulkanGraphicsPublic.h`

3. **Review documentation:**
   - Run `cmake --build build --target doxygen`
   - Open `build/docs/html/index.html`

## Support & Questions

Refer to:
- `INSTALL.md` - Installation and usage
- `Doxyfile.in` + generated HTML - API reference
- `examples/external_usage_example/` - Working example
- `cmake/LightVulkanGraphicsConfig.cmake.in` - CMake integration

---

**Version 2.1.0** — Refactored for external library usage with full backward compatibility.
