# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and the project follows semantic versioning for public releases.

## Unreleased

### Added
- Configurable CMake options for examples, compiler warnings, and sanitizers.
- Generated public version header with compile-time version macros and constants.
- `lightGraphics::LightVulkanGraphics` compatibility alias for the main application class.
- Contributor guidance for local build and verification workflows.

### Changed
- CI expanded beyond a single Ubuntu/GCC job to cover a Linux compiler matrix, sanitizer validation, and a Windows build path.
- Public docs were aligned with the current FBX API, dependency behavior, and bundled asset licensing.
- Reserved-identifier header guards were replaced with project-scoped names.
- Sanitized installs now export the sanitizer link requirements needed by downstream CMake consumers.
- Windows/MSVC builds now avoid `min`/`max` macro collisions, enable the required GLM experimental quaternion extension, and clean up warning-as-error blockers.

### Removed
- Maintainer-local editor and agent workflow files from the public repo surface.
