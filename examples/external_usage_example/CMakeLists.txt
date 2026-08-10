## Example: Using LightVulkanGraphics as an Installed External Library
##
## This demonstrates how to use LightVulkanGraphics in your own project
## after it has been installed locally or system-wide.
##
## For local installation to ~/.local, run before configuring this project:
##   cd lightVulkanGraphics
##   cmake -B build -DCMAKE_INSTALL_PREFIX=~/.local
##   cmake --build build
##   cmake --install build

cmake_minimum_required(VERSION 3.20)
project(ExternalUsageExample CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# METHOD 1: Using find_package with components
# This will search CMAKE_PREFIX_PATH for installed LightVulkanGraphics
find_package(LightVulkanGraphics 2.1 REQUIRED
    COMPONENTS
        Core
        UI
)

# Create example executable
add_executable(my_graphics_app main.cpp)

# Link against LightVulkanGraphics
# The "PUBLIC" keyword makes the dependencies available to users of my_graphics_app
target_link_libraries(my_graphics_app PRIVATE
    LightVulkanGraphics::LightVulkanGraphics
    LightVulkanGraphics::UI
)

# Optional: Print what was found
message(STATUS "LightVulkanGraphics found!")
message(STATUS "Core library: LightVulkanGraphics::LightVulkanGraphics")
message(STATUS "UI component: LightVulkanGraphics::UI")

# Optional: Configure with local installation
# If CMAKE_PREFIX_PATH is not set, uncomment and use:
# cmake -B build -DCMAKE_PREFIX_PATH=~/.local
