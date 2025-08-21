cmake_minimum_required(VERSION 3.20)
project(LightVulkanGraphicsPackageConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LightVulkanGraphics REQUIRED)

add_executable(light_vulkan_graphics_package_consumer main.cpp)
target_link_libraries(light_vulkan_graphics_package_consumer
	PRIVATE
		LightVulkanGraphics::LightVulkanGraphics
)
