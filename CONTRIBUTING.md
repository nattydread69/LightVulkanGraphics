# Contributing

## Scope

- Keep changes focused. Separate refactors from feature work when practical.
- Avoid unrelated formatting churn in touched files.
- Update public-facing docs when the public API, build flags, examples, or packaging behavior changes.

## Build

Linux/WSL quick start:

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

Useful project options:

- `-DLVG_BUILD_EXAMPLES=ON|OFF`
- `-DLVG_ENABLE_WARNINGS=ON|OFF`
- `-DLVG_WARNINGS_AS_ERRORS=ON|OFF`
- `-DLVG_ENABLE_SANITIZERS=ON|OFF`
- `-DASSIMP_ROOT=/path/to/assimp`
- `-DASSIMP_USE_FETCHCONTENT=ON`

## Verification

Recommended local checks before opening a pull request:

```bash
cmake --build build --target LightVulkanGraphicsExamples
ctest --test-dir build --output-on-failure
```

Sanitizer pass with Clang:

```bash
cmake -S . -B build-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DLVG_ENABLE_SANITIZERS=ON
cmake --build build-sanitized
ctest --test-dir build-sanitized --output-on-failure
```

## Style

- Follow the existing style in the file you are editing.
- Keep comments focused on intent and behavior, not obvious mechanics.
- Prefer compatibility-preserving API changes unless a breaking change is deliberate and documented.

## Packaging

If you change exported headers, install rules, or `find_package()` behavior, make sure the package smoke tests still pass. Those checks validate that an installed tree and a relocated installed tree both remain consumable.
