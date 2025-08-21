cmake_minimum_required(VERSION 3.20)
project(SphereCloud LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Vulkan
find_package(Vulkan REQUIRED)  # from LunarG SDK
if (NOT Vulkan_FOUND)
  message(FATAL_ERROR "Vulkan SDK not found. Install LunarG Vulkan SDK.")
endif()

# Try to locate glslc from Vulkan SDK
if (EXISTS "$ENV{VULKAN_SDK}/Bin/glslc")
  set(GLSLC "$ENV{VULKAN_SDK}/Bin/glslc")
elseif (EXISTS "$ENV{VULKAN_SDK}/bin/glslc")
  set(GLSLC "$ENV{VULKAN_SDK}/bin/glslc")
else()
  find_program(GLSLC glslc)
endif()
if (NOT GLSLC)
  message(FATAL_ERROR "glslc not found. Ensure Vulkan SDK is installed and glslc is on PATH.")
endif()

include(FetchContent)

# --- GLFW (for Vulkan window + input)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.3.9
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# --- GLM (math)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# --- Shaders: compile to SPIR-V
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SPV_DIR    ${CMAKE_CURRENT_BINARY_DIR}/spv)
file(MAKE_DIRECTORY ${SPV_DIR})

set(VS_SRC ${SHADER_DIR}/instanced_sphere.vert)
set(FS_SRC ${SHADER_DIR}/instanced_sphere.frag)
set(VS_SPV ${SPV_DIR}/instanced_sphere.vert.spv)
set(FS_SPV ${SPV_DIR}/instanced_sphere.frag.spv)

add_custom_command(
  OUTPUT ${VS_SPV}
  COMMAND ${GLSLC} -fshader-stage=vert -o ${VS_SPV} ${VS_SRC}
  DEPENDS ${VS_SRC}
  COMMENT "Compiling ${VS_SRC}"
)
add_custom_command(
  OUTPUT ${FS_SPV}
  COMMAND ${GLSLC} -fshader-stage=frag -o ${FS_SPV} ${FS_SRC}
  DEPENDS ${FS_SRC}
  COMMENT "Compiling ${FS_SRC}"
)

add_custom_target(Shaders ALL DEPENDS ${VS_SPV} ${FS_SPV})

# --- App
add_executable(SphereCloud
  src/main.cpp
)
add_dependencies(SphereCloud Shaders)

target_include_directories(SphereCloud PRIVATE
  ${SPV_DIR}
)

target_link_libraries(SphereCloud PRIVATE
  Vulkan::Vulkan
  glfw
  glm
)

# On Windows, make sure to export symbols for the Vulkan loader if needed
if (WIN32)
  target_compile_definitions(SphereCloud PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

# macOS note (MoltenVK): you may need VK_USE_PLATFORM_METAL_EXT; left out for brevity.

