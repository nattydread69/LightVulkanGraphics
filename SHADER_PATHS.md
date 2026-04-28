# Shader Path Resolution

The LightVulkanGraphics library automatically searches for shader files in multiple locations to work both during development and after installation.

## Automatic Path Resolution

The library searches for shader files in the following order:

1. **Custom Path** (if set via `setShaderPath()`)
2. **Environment Variable** (`LIGHT_VULKAN_GRAPHICS_SHADER_PATH`)
3. **Development Paths**:
   - `spv/`
   - `../spv/`
   - `../../spv/`
4. **Installed Paths**:
   - `/usr/local/share/LightVulkanGraphics/spv/`
   - `/usr/share/LightVulkanGraphics/spv/`
5. **Relative Paths** (for packaged applications):
   - `./spv/`
   - `../share/LightVulkanGraphics/spv/`
   - `../../share/LightVulkanGraphics/spv/`

## Usage Examples

### Basic Usage (Automatic Detection)
```cpp
#include "lightVulkanGraphics.h"

int main() {
    lightGraphics::lightVulkanGraphics app("My App");
    return 0;
}
```

### Custom Shader Path
```cpp
#include "lightVulkanGraphics.h"

int main() {
    lightGraphics::LightVulkanGraphicsCreateInfo createInfo;
    createInfo.shaderPath = "/path/to/your/spv";
    
    lightGraphics::lightVulkanGraphics app("My App", createInfo);
    
    // Or set LIGHT_VULKAN_GRAPHICS_SHADER_PATH before constructing the app.
    
    return 0;
}
```

### Environment Variable
```bash
# Set the shader path via environment variable
export LIGHT_VULKAN_GRAPHICS_SHADER_PATH="/usr/local/share/LightVulkanGraphics/spv"

# Run your application
./your_app
```

## Troubleshooting

If you get "Failed to open file" errors:

1. **Check if shaders are installed**:
   ```bash
   ls /usr/local/share/LightVulkanGraphics/spv/
   ```

2. **Set a custom path**:
   ```cpp
   app.setShaderPath("/path/to/your/shaders/");
   ```

3. **Use environment variable**:
   ```bash
   export LIGHT_VULKAN_GRAPHICS_SHADER_PATH="/path/to/your/shaders"
   ```

4. **Check debug output**: The library prints which shader paths it searches and which one it finds.

## Installation

When you install the library with `sudo make install`, the shaders are automatically installed to:
- `/usr/local/share/LightVulkanGraphics/spv/` (default)
- `/usr/share/LightVulkanGraphics/spv/` (system-wide)

The library will automatically find them in these locations.
