# Third-party notices

LightVulkanGraphics incorporates the following third-party components.

## stb

Files under `external/stb/`, including `stb_truetype.h` (used by LVGUI to bake and
measure text; see `src/ui/FontImpl.cpp`).
Public domain (Unlicense) or MIT, at the user's option.
Upstream: https://github.com/nothings/stb

## Bundled font

`assets/fonts/Inter-Regular.ttf` -- Inter, by The Inter Project Authors.
Licence: SIL Open Font License, Version 1.1. Full text at `assets/fonts/License.txt`.
Upstream: https://github.com/rsms/inter (static build from the v4.1 release).

## Assimp, GLFW, GLM, Vulkan SDK

Consumed as external dependencies and not redistributed as source by this project.
See each project's own licence terms.
