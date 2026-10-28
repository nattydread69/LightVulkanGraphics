#!/bin/bash
# Installs the Linux build dependencies (matching .github/workflows/ci.yml)
# so LightVulkanGraphics can be built and tested in Claude Code cloud sessions.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

packages=(
  cmake
  ninja-build
  g++
  clang
  libassimp-dev
  libglfw3-dev
  libglm-dev
  libvulkan-dev
  glslc
  mesa-vulkan-drivers
  vulkan-tools
  xvfb
)

# Skip apt entirely when every package is already installed
if ! dpkg -s "${packages[@]}" > /dev/null 2>&1; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq "${packages[@]}" > /dev/null
fi
