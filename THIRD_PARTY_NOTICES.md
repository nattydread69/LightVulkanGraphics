# Third-party notices

LightVulkanGraphics incorporates the following third-party components.

## stb

Files under `external/stb/`: `stb_image.h` v2.30 (image decoding behind
`loadImageRgba()`; see `src/ImageLoad.cpp`), `stb_easy_font.h` (screen text; see
`src/ScreenText.cpp`) and `stb_truetype.h` (used by LVGUI to bake and measure text; see
`src/ui/Font.cpp`). All are compiled with internal linkage, so the libraries export no
`stb*` symbols.
Public domain (Unlicense) or MIT, at the user's option.
Upstream: https://github.com/nothings/stb

## Bundled font

`assets/fonts/Inter-Regular.ttf` -- Inter, by The Inter Project Authors.
Licence: SIL Open Font License, Version 1.1. Full text at `assets/fonts/License.txt`.
Upstream: https://github.com/rsms/inter (static build from the v4.1 release).

`assets/fonts/DejaVuSansMono.ttf` -- DejaVu Sans Mono (Bitstream Vera derivative), the
default monospace face (`FontStyle::Monospace`). Licence: Bitstream Vera / DejaVu free
licence, permitting redistribution; full text at `assets/fonts/DejaVu-License.txt`.
Upstream: https://dejavu-fonts.github.io/

## Assimp, GLFW, GLM, Vulkan SDK

Consumed as external dependencies and not redistributed as source by this project.
See each project's own licence terms.
