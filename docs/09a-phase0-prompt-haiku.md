# Phase 0 prompt — written for a small model

This replaces the Phase 0 prompt in `09-claude-code-prompts.md`. It is more explicit and
gives file contents verbatim, so it does not rely on the model inferring intent.

Run with:

```bash
claude --model haiku
```

Paste everything between the rules below.

---

You are adding build scaffolding to an existing C++17 CMake project called
LightVulkanGraphics. This is Phase 0 of a larger piece of work. **In this phase you will
not write any GUI code.** You are only creating empty structure and wiring up CMake.

## Absolute rules

1. **Do not delete or rewrite any existing file.** Every change to an existing file must
   be an addition. If you believe something existing must be changed or removed, stop and
   ask me.
2. **Do not create any file that is not listed in Step 3 below.**
3. **Do not write any GUI, Vulkan, or rendering code.** No widgets, no draw calls, no
   shaders. Later phases do that.
4. **Do not run `git commit`, `git push`, or `git checkout`.** I will handle git.
5. If any step does not match what you find in the repository, **stop and tell me what
   you found instead of improvising.**

## Step 1 — Inspect and report, then stop

Read these files and report what you find. Write nothing yet.

- `CMakeLists.txt` (the one in the repository root)
- everything in `cmake/`
- `tests/` — specifically, find out how tests are currently registered with CTest and
  which test framework, if any, is in use
- pick any two `.cpp` files and any two `.h` files from `src/` and `include/`

Report, as a short bulleted list:

- **A.** The exact name of the main library target as written in `add_library(...)`
- **B.** The exact name of the alias target, if one exists (something like
  `add_library(Foo::Foo ALIAS Foo)`)
- **C.** The filename of the package config template in `cmake/` (something ending in
  `.cmake.in`)
- **D.** The exact `install(...)` and `export(...)` command block used for the main
  library, quoted verbatim
- **E.** How tests are added: the framework (Catch2, GoogleTest, doctest, or plain
  `assert` with a `main`), and the exact `add_test(...)` lines
- **F.** The code style you observed: brace placement (same line or next line), indent
  width, member variable naming convention (for example `m_thing` or `thing_`), and
  whether `#pragma once` or include guards are used

**Then stop and wait for me to confirm before doing anything else.** Do not continue to
Step 2 in the same response.

## Step 2 — Add the CMake option and target

Using the real target names you reported in Step 1, make these additions to the root
`CMakeLists.txt`.

Add near the other `option(...)` declarations:

```cmake
option(LVG_BUILD_UI "Build the LVGUI GUI layer" ON)
```

Add after the main library target is fully defined (after its
`target_link_libraries` call), substituting the real target name from Step 1A wherever
this snippet says `LightVulkanGraphics`:

```cmake
if(LVG_BUILD_UI)
    set(LVGUI_SOURCES
        src/ui/Placeholder.cpp
    )

    add_library(LightVulkanGraphicsUI ${LVGUI_SOURCES})
    add_library(LightVulkanGraphics::UI ALIAS LightVulkanGraphicsUI)

    target_compile_features(LightVulkanGraphicsUI PUBLIC cxx_std_17)

    target_include_directories(LightVulkanGraphicsUI PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    target_link_libraries(LightVulkanGraphicsUI
        PUBLIC  LightVulkanGraphics
        PRIVATE Vulkan::Vulkan glfw
    )

    # The core library advertises GUI availability to its consumers.
    target_compile_definitions(LightVulkanGraphics PUBLIC LVG_WITH_UI)
endif()
```

Two things to check rather than assume:

- The names `Vulkan::Vulkan` and `glfw` must match exactly how the existing main target
  links Vulkan and GLFW. Copy whatever names the existing `target_link_libraries` call
  uses. If it links GLFW as `glfw::glfw` or a variable, use that.
- If the existing `target_include_directories` for the main library uses a different
  generator-expression form, match it.

**Show me the diff before writing it.**

## Step 3 — Create exactly these files

### `src/ui/Placeholder.cpp`

```cpp
// Placeholder translation unit so the LightVulkanGraphicsUI target has a source file
// during Phase 0 scaffolding. Delete this in Phase 1 once DrawList.cpp exists.

namespace lightGraphics::ui {

namespace {
// Prevents an empty-translation-unit warning on some toolchains.
[[maybe_unused]] int scaffoldingPlaceholder = 0;
}

} // namespace lightGraphics::ui
```

### `include/lightVulkanGraphics/ui/Ui.h`

```cpp
#pragma once

// Umbrella header for LVGUI, the GUI layer of LightVulkanGraphics.
//
// Consumers include only this header:
//
//     #include <lightVulkanGraphics/ui/Ui.h>
//     namespace lvgui = lightGraphics::ui;
//
// The design specification lives in docs/gui/. This header is currently empty;
// subsequent phases will add includes for Types.h, DrawList.h, Font.h, GuiContext.h,
// Panel.h, and the widget headers.

namespace lightGraphics::ui {

// Intentionally empty during Phase 0.

} // namespace lightGraphics::ui
```

If Step 1F reported include guards rather than `#pragma once`, use include guards
instead, matching the existing naming convention.

### `tests/ui/CMakeLists.txt`

Write this to match the test framework you reported in Step 1E. If the project uses plain
`assert` and a `main`, this is the shape:

```cmake
add_executable(lvgui_tests
    test_placeholder.cpp
)

target_link_libraries(lvgui_tests PRIVATE LightVulkanGraphics::UI)

add_test(NAME lvgui_tests COMMAND lvgui_tests)
```

If the project uses Catch2, GoogleTest, or doctest, link the same test dependencies the
existing test target links, and register the test the same way the existing tests are
registered.

### `tests/ui/test_placeholder.cpp`

```cpp
// Phase 0: proves the test target builds and links against LightVulkanGraphics::UI.
// Phase 1 replaces this with real DrawList tests.

#include <lightVulkanGraphics/ui/Ui.h>

int main()
{
    return 0;
}
```

Adapt to the project's test framework if it is not plain `main`.

### `THIRD_PARTY_NOTICES.md`

```markdown
# Third-party notices

LightVulkanGraphics incorporates the following third-party components.

## stb

Files under `external/stb/`.
Public domain (Unlicense) or MIT, at the user's option.
Upstream: https://github.com/nothings/stb

## Bundled font

Reserved. To be completed in Phase 2, when a font is added under `assets/fonts/`.

## Assimp, GLFW, GLM, Vulkan SDK

Consumed as external dependencies and not redistributed as source by this project.
See each project's own licence terms.
```

## Step 4 — Register the test subdirectory

Find where `tests/` is added to the build in the root `CMakeLists.txt` — most likely an
`add_subdirectory(tests)` inside an `if(BUILD_TESTING)` block. Read `tests/CMakeLists.txt`
and add, guarded so it does not break a `LVG_BUILD_UI=OFF` build:

```cmake
if(LVG_BUILD_UI)
    add_subdirectory(ui)
endif()
```

Place it alongside however the existing test subdirectories or targets are declared.

## Step 5 — Install and export rules

Extend the existing install/export block you quoted in Step 1D so that
`LightVulkanGraphicsUI` is installed and exported alongside the main library, in the same
export set, using the same destination variables and the same component names.

Also add an install rule for the new public header directory
`include/lightVulkanGraphics/ui/`, matching however the existing public headers are
installed.

**Do not invent a second export set or a second package config file.** If the existing
mechanism cannot accommodate a second target without restructuring, stop and tell me.

Then check the package config template from Step 1C. If it uses
`check_required_components` or explicitly lists components, add the UI target
consistently.

## Step 6 — Verify

Run all four of these from the repository root and report the exact outcome of each.

```bash
rm -rf build-on build-off

cmake -S . -B build-on  -DCMAKE_BUILD_TYPE=Debug -DLVG_BUILD_UI=ON
cmake --build build-on -j

cmake -S . -B build-off -DCMAKE_BUILD_TYPE=Debug -DLVG_BUILD_UI=OFF
cmake --build build-off -j

ctest --test-dir build-on --output-on-failure
```

Expected:

- Both configure steps succeed
- Both builds succeed with no new warnings
- `libLightVulkanGraphicsUI.a` or `.so` exists in `build-on` and does **not** exist in
  `build-off`
- `ctest` reports `lvgui_tests` passing in `build-on`
- `ctest --test-dir build-off` runs the pre-existing tests and does not reference
  `lvgui_tests`

If a build fails, report the first error verbatim and stop. Do not attempt more than two
fixes before reporting back to me.

## Step 7 — Report

Reply with exactly this, filled in:

```
PHASE 0 REPORT

Files created:
  <list>

Files modified:
  <list, with a one-line description of each addition>

Build LVG_BUILD_UI=ON:   PASS / FAIL
Build LVG_BUILD_UI=OFF:  PASS / FAIL
ctest on build-on:       PASS / FAIL
UI library present in build-on only: YES / NO

Deviations from the instructions and why:
  <list, or "none">

Anything I was unsure about:
  <list, or "none">
```

Then stop. Do not begin Phase 1.
