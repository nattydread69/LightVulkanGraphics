# LightVulkanGraphics Installation Guide

## Version 2.1.0

LightVulkanGraphics is a high-performance Vulkan graphics library for real-time visualization.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Local Installation](#local-installation)
3. [System-Wide Installation](#system-wide-installation)
4. [Using in Your Project](#using-in-your-project)
5. [Components](#components)
6. [Troubleshooting](#troubleshooting)

## Prerequisites

### Required
- **CMake** >= 3.20
- **C++20** compatible compiler (GCC 10+, Clang 11+, MSVC 2019+)
- **Vulkan SDK** (1.3+)
- **GLM** (header-only math library)
- **GLFW3** (window management)

### Optional
- **Doxygen** (for building documentation)
- **Assimp** (for FBX loader component)
- **GraphViz/Dot** (for enhanced Doxygen output)

### Install Prerequisites (Ubuntu/Debian)

```bash
sudo apt-get install cmake vulkan-sdk libvulkan-dev
sudo apt-get install libglfw3-dev libglm-dev
# Optional: for documentation
sudo apt-get install doxygen graphviz
# Optional: for FBX loader
sudo apt-get install libassimp-dev
```

## Local Installation

**Recommended for development** when you don't have system permissions or want to keep dependencies isolated.

### Step 1: Configure

```bash
cd lightVulkanGraphics
cmake -B build \
    -DCMAKE_INSTALL_PREFIX=~/.local \
    -DCMAKE_BUILD_TYPE=Release
```

### Step 2: Build

```bash
cmake --build build -j$(nproc)
```

### Step 3: Install

```bash
cmake --install build
```

This installs to:
- Headers: `~/.local/include/LightVulkanGraphics/`
- Libraries: `~/.local/lib/`
- CMake Config: `~/.local/lib/cmake/LightVulkanGraphics/`
- Documentation: `~/.local/share/doc/LightVulkanGraphics/`
- Shaders: `~/.local/share/LightVulkanGraphics/spv/`

### Step 4: Add to CMake Search Path

Add this to your `~/.bashrc` or `~/.zshrc`:

```bash
export CMAKE_PREFIX_PATH="$HOME/.local:$CMAKE_PREFIX_PATH"
export LD_LIBRARY_PATH="$HOME/.local/lib:$LD_LIBRARY_PATH"
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH"
```

Then source your shell configuration:
```bash
source ~/.bashrc  # or source ~/.zshrc
```

## System-Wide Installation

**Requires root/sudo access**

### Step 1: Configure

```bash
cd lightVulkanGraphics
cmake -B build \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_BUILD_TYPE=Release
```

### Step 2: Build

```bash
cmake --build build -j$(nproc)
```

### Step 3: Install

```bash
sudo cmake --install build
```

This installs to:
- Headers: `/usr/local/include/LightVulkanGraphics/`
- Libraries: `/usr/local/lib/`
- CMake Config: `/usr/local/lib/cmake/LightVulkanGraphics/`

## Using in Your Project

### Method 1: Using CMake (Recommended)

In your `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyProject)

find_package(LightVulkanGraphics 2.1 REQUIRED COMPONENTS Core)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE LightVulkanGraphics::LightVulkanGraphics)
```

### Method 2: With Optional Components

```cmake
find_package(LightVulkanGraphics 2.1 REQUIRED
    COMPONENTS
        Core      # Main graphics library
        UI        # GUI framework (optional)
        Loaders   # Asset loading (optional)
)

target_link_libraries(my_app PRIVATE
    LightVulkanGraphics::LightVulkanGraphics
    LightVulkanGraphics::UI
    LightVulkanGraphics::Loaders
)
```

### Method 3: Manual Configuration

If CMake can't find the library:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=~/.local
```

## Components

### Core
Essential graphics rendering and object management.

**Includes:**
- Main application (`lightVulkanGraphics`)
- Scene graph (`SceneGraph`)
- Object management (`pObject`)
- Camera system (`Camera`)
- Lighting (`Light`)

**Targets:**
- `LightVulkanGraphics::LightVulkanGraphics` (main)
- `LightVulkanGraphics::Core` (alias)

### UI
Immediate-mode GUI framework (LVGUI).

**Includes:**
- GUI context (`GuiContext`)
- Panels and widgets
- Complete widget set (Button, Slider, etc.)

**Target:**
- `LightVulkanGraphics::UI`

**Compile Definition:**
- `LVG_BUILD_UI` (automatically set when using this component)

### Loaders
Asset and model loading utilities.

**Includes:**
- FBX loader (`FBXLoader`)
- Rigged object support (`RiggedObject`)

**Target:**
- `LightVulkanGraphics::Loaders`

**Optional Dependencies:**
- Assimp (for FBX support)

## Building Documentation

Generate Doxygen documentation:

```bash
cd lightVulkanGraphics
cmake -B build -DLVG_BUILD_DOXYGEN=ON
cmake --build build --target doxygen
```

Documentation is generated in `build/docs/html/index.html`

## Troubleshooting

### CMake Can't Find LightVulkanGraphics

**Solution:** Ensure `CMAKE_PREFIX_PATH` includes the installation directory:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=~/.local
```

Or set environment variable:
```bash
export CMAKE_PREFIX_PATH="$HOME/.local:$CMAKE_PREFIX_PATH"
cmake -B build
```

### Vulkan Not Found

**Solution:** Set `Vulkan_DIR`:

```bash
cmake -B build -DVulkan_DIR=/path/to/vulkan/sdk/lib/cmake/Vulkan
```

### Link Errors on Linux

**Solution:** Ensure `LD_LIBRARY_PATH` includes library directory:

```bash
export LD_LIBRARY_PATH="$HOME/.local/lib:$LD_LIBRARY_PATH"
```

### Multiple Installation Versions

If you have both local and system installations, CMake will use whichever comes first in `CMAKE_PREFIX_PATH`. To prioritize local:

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/.local:/usr/local"
```

## Version Information

Check installed version:

```bash
grep -r "LIGHT_VULKAN_GRAPHICS_VERSION" ~/.local/include/
```

Or in your C++ code:

```cpp
#include "LightVulkanGraphicsVersion.h"

std::cout << "LightVulkanGraphics " << lightGraphics::kLightVulkanGraphicsVersion << std::endl;
```

## Backward Compatibility

Version 2.1.0 maintains full backward compatibility with 2.0.x applications. Existing code will continue to work without modifications.

## Support

For issues, questions, or contributions, please refer to the project documentation and examples.
