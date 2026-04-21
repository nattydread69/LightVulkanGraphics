cmake_minimum_required(VERSION 3.20)
project(libLightVulkanGraphics VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(LVG_BUILD_EXAMPLES "Build bundled example executables" ON)
option(LVG_ENABLE_WARNINGS "Enable recommended compiler warnings for project targets" ON)
option(LVG_WARNINGS_AS_ERRORS "Treat warnings as errors for project targets" OFF)
option(LVG_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers for supported toolchains" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(FetchContent)
include(CTest)

set(LVG_PACKAGE_NAME "LightVulkanGraphics")
set(LVG_ABI_VERSION 2)
set(LVG_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/${LVG_PACKAGE_NAME}")
set(LVG_SHADER_INSTALL_DIR "${CMAKE_INSTALL_DATAROOTDIR}/${LVG_PACKAGE_NAME}/spv")
set(LVG_SHADER_INSTALL_FULL_DIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/${LVG_PACKAGE_NAME}/spv")
set(LVG_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")

file(MAKE_DIRECTORY "${LVG_GENERATED_INCLUDE_DIR}")
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/LightVulkanGraphicsVersion.h.in
	${LVG_GENERATED_INCLUDE_DIR}/LightVulkanGraphicsVersion.h
	@ONLY
)

if (APPLE)
	set(LVG_BUILD_TREE_ORIGIN_RPATH "@loader_path")
elseif (UNIX)
	set(LVG_BUILD_TREE_ORIGIN_RPATH "\$ORIGIN")
endif()

if (LVG_ENABLE_SANITIZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	message(WARNING "LVG_ENABLE_SANITIZERS is only supported for GCC and Clang toolchains")
endif()

set(LVG_SANITIZER_LINK_FLAG "")
if (LVG_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT WIN32)
	set(LVG_SANITIZER_LINK_FLAG -fsanitize=address,undefined)
endif()

function(lvg_apply_project_options target)
	if (LVG_ENABLE_WARNINGS)
		if (MSVC)
			target_compile_options(${target} PRIVATE /W4 /permissive-)
			if (LVG_WARNINGS_AS_ERRORS)
				target_compile_options(${target} PRIVATE /WX)
			endif()
		else()
			target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
			if (LVG_WARNINGS_AS_ERRORS)
				target_compile_options(${target} PRIVATE -Werror)
			endif()
		endif()
	endif()

	if (LVG_SANITIZER_LINK_FLAG)
		target_compile_options(${target} PRIVATE ${LVG_SANITIZER_LINK_FLAG} -fno-omit-frame-pointer)
		target_link_options(${target} PRIVATE ${LVG_SANITIZER_LINK_FLAG})
	endif()
endfunction()

# ---------------- Vulkan (robust) ----------------
find_package(Vulkan QUIET)
if (NOT TARGET Vulkan::Vulkan)
	# Create an imported target from legacy variables if needed
	if (Vulkan_FOUND OR Vulkan_INCLUDE_DIRS OR Vulkan_LIBRARIES OR Vulkan_LIBRARY)
		set(_VULKAN_INC "${Vulkan_INCLUDE_DIRS}")
		if (NOT _VULKAN_INC)
			find_path(_VULKAN_INC vulkan/vulkan.h
				HINTS "$ENV{VULKAN_SDK}/Include" "/usr/include" "/usr/local/include")
		endif()
		set(_VULKAN_LIB "${Vulkan_LIBRARY}")
		if (NOT _VULKAN_LIB)
			if (Vulkan_LIBRARIES)
				list(GET Vulkan_LIBRARIES 0 _VULKAN_LIB)
			else()
				find_library(_VULKAN_LIB NAMES vulkan Vulkan
					HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib"
					      "/usr/lib" "/usr/lib/x86_64-linux-gnu" "/usr/local/lib")
			endif()
		endif()
		if (NOT _VULKAN_LIB)
			message(FATAL_ERROR "Found Vulkan headers but not the lib. Install libvulkan-dev or the LunarG SDK.")
		endif()
		add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
		set_target_properties(Vulkan::Vulkan PROPERTIES
			IMPORTED_LOCATION "${_VULKAN_LIB}"
			INTERFACE_INCLUDE_DIRECTORIES "${_VULKAN_INC}")
	else()
		message(FATAL_ERROR "Vulkan not found. Install libvulkan-dev / LunarG SDK.")
	endif()
endif()

# ---------------- GLM (export-safe) ----------------
find_package(glm QUIET CONFIG)

if (TARGET glm::glm)
    # Great: use the proper imported target both for build and for install.
    set(LVG_GLM_BUILD_TARGET   glm::glm)
    set(LVG_GLM_INSTALL_TARGET glm::glm)
else()
    include(FetchContent)

    # Populate sources only (no 'glm' target), avoid CMP0169 warnings.
    if (POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()

    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
    )
    FetchContent_Populate(glm)  # gives you ${glm_SOURCE_DIR}

    # Build-only header shim (NOT exported; no '::' in the name)
    add_library(lvg_glm_headers INTERFACE)
    target_include_directories(lvg_glm_headers INTERFACE "${glm_SOURCE_DIR}")

    set(LVG_GLM_BUILD_TARGET   lvg_glm_headers)  # used while building this project
    set(LVG_GLM_INSTALL_TARGET glm::glm)         # consumer must provide via find_dependency(glm)
endif()



# ---------------- GLFW (prefer system; fallback fetch) ----------------
set(GLFW_LIB_TARGET "")
find_package(glfw3 CONFIG QUIET)            # usually exports glfw::glfw
if (TARGET glfw::glfw)
	set(GLFW_LIB_TARGET glfw::glfw)
else()
	find_package(glfw3 QUIET)               # module mode -> target 'glfw'
	if (TARGET glfw)
		set(GLFW_LIB_TARGET glfw)
	else()
		message(STATUS "System glfw3 not found; fetching from source")
		FetchContent_Declare(
			glfw
			GIT_REPOSITORY https://github.com/glfw/glfw.git
			GIT_TAG        3.3.9
		)
		# Choose ONE platform backend you actually have dev packages for:
		set(GLFW_BUILD_X11     ON  CACHE BOOL "" FORCE)
		set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
		set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
		set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
		set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
		set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
		FetchContent_MakeAvailable(glfw)
		set(GLFW_LIB_TARGET glfw)
	endif()
endif()

# ---------------- Assimp (FBX loader) ----------------
# Prefer local/offline install; FetchContent is opt-in.
set(ASSIMP_ROOT "" CACHE PATH "Path to local Assimp installation")
set(ASSIMP_USE_CONFIG OFF CACHE BOOL "Use assimp's CMake package config")
set(ASSIMP_USE_FETCHCONTENT OFF CACHE BOOL "Fetch assimp from source (requires network)")
set(ASSIMP_CONTRIB_INCLUDE_DIR "")

if (ASSIMP_ROOT)
	list(PREPEND CMAKE_PREFIX_PATH "${ASSIMP_ROOT}")
endif()

find_path(ASSIMP_INCLUDE_DIR
	NAMES assimp/scene.h
	HINTS ${ASSIMP_ROOT}
	PATH_SUFFIXES include
)

find_library(ASSIMP_LIBRARY

	NAMES assimp libassimp
	HINTS ${ASSIMP_ROOT}
	PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
)

if (ASSIMP_LIBRARY AND ASSIMP_INCLUDE_DIR)
	add_library(assimp::assimp UNKNOWN IMPORTED)
	set_target_properties(assimp::assimp PROPERTIES
		IMPORTED_LOCATION "${ASSIMP_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${ASSIMP_INCLUDE_DIR}"
	)
	message(STATUS "Found assimp library: ${ASSIMP_LIBRARY}")
	message(STATUS "Found assimp headers: ${ASSIMP_INCLUDE_DIR}")
endif()

if (NOT TARGET assimp::assimp AND ASSIMP_USE_CONFIG)
	find_package(assimp QUIET)
	find_package(Assimp QUIET)

	if (TARGET Assimp::Assimp AND NOT TARGET assimp::assimp)
		add_library(assimp::assimp ALIAS Assimp::Assimp)
	endif()

	if (NOT TARGET assimp::assimp)
		if (ASSIMP_LIBRARIES AND NOT ASSIMP_LIBRARY)
			list(GET ASSIMP_LIBRARIES 0 ASSIMP_LIBRARY)
		endif()
		if (ASSIMP_INCLUDE_DIRS AND NOT ASSIMP_INCLUDE_DIR)
			list(GET ASSIMP_INCLUDE_DIRS 0 ASSIMP_INCLUDE_DIR)
		endif()

		if (ASSIMP_LIBRARY AND ASSIMP_INCLUDE_DIR)
			add_library(assimp::assimp UNKNOWN IMPORTED)
			set_target_properties(assimp::assimp PROPERTIES
				IMPORTED_LOCATION "${ASSIMP_LIBRARY}"
				INTERFACE_INCLUDE_DIRECTORIES "${ASSIMP_INCLUDE_DIR}"
			)
		endif()
	endif()
endif()

if (NOT TARGET assimp::assimp AND ASSIMP_USE_FETCHCONTENT)
	FetchContent_Declare(
		assimp
		GIT_REPOSITORY https://github.com/assimp/assimp.git
		GIT_TAG        v5.3.1
	)

	# Configure Assimp build options
	set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
	set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
	set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
	set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)

	FetchContent_MakeAvailable(assimp)
	if (TARGET assimp AND NOT TARGET assimp::assimp)
		add_library(assimp::assimp ALIAS assimp)
	endif()
	set(ASSIMP_CONTRIB_INCLUDE_DIR "${assimp_SOURCE_DIR}/contrib")
endif()

if (NOT TARGET assimp::assimp)
	message(FATAL_ERROR
		"Assimp library not found!\n"
		"Install assimp locally and reconfigure, or set ASSIMP_ROOT.\n"
		"Example: cmake -DASSIMP_ROOT=/path/to/assimp .."
	)
endif()

# ---------------- Sources ----------------
# Explicitly list all source files
set(SOURCES
	"${PROJECT_SOURCE_DIR}/src/VkApp.cpp"
	"${PROJECT_SOURCE_DIR}/src/LightVulkanGraphicsLogging.cpp"
	"${PROJECT_SOURCE_DIR}/src/lightVulkanGraphics.cpp"
	"${PROJECT_SOURCE_DIR}/src/pObject.cpp"
	"${PROJECT_SOURCE_DIR}/src/FBXLoader.cpp"
	"${PROJECT_SOURCE_DIR}/src/GraphicsModel.cpp"
	"${PROJECT_SOURCE_DIR}/src/RiggedObject.cpp"
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES
	OUTPUT_NAME "LightVulkanGraphics"
	VERSION ${PROJECT_VERSION}
	SOVERSION ${LVG_ABI_VERSION}
	EXPORT_NAME ${LVG_PACKAGE_NAME}
    DEBUG_POSTFIX ""
)
add_library(${LVG_PACKAGE_NAME} ALIAS ${PROJECT_NAME})
add_library(${LVG_PACKAGE_NAME}::${LVG_PACKAGE_NAME} ALIAS ${PROJECT_NAME})

# ---------------- Shaders (SPIR-V) ----------------
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SPV_DIR    ${CMAKE_CURRENT_BINARY_DIR}/spv)
file(MAKE_DIRECTORY ${SPV_DIR})

# glslc detection
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

# Original sphere shaders
set(VS_SRC ${SHADER_DIR}/instanced_sphere.vert)
set(FS_SRC ${SHADER_DIR}/instanced_sphere.frag)
set(VS_SPV ${SPV_DIR}/instanced_sphere.vert.spv)
set(FS_SPV ${SPV_DIR}/instanced_sphere.frag.spv)

# Flexible shape shaders
set(FLEX_VS_SRC ${SHADER_DIR}/flexible_shape.vert)
set(FLEX_FS_SRC ${SHADER_DIR}/flexible_shape.frag)
set(FLEX_VS_SPV ${SPV_DIR}/flexible_shape.vert.spv)
set(FLEX_FS_SPV ${SPV_DIR}/flexible_shape.frag.spv)

# Rigged mesh shaders
set(RIGGED_VS_SRC ${SHADER_DIR}/rigged_mesh.vert)
set(RIGGED_FS_SRC ${SHADER_DIR}/rigged_mesh.frag)
set(RIGGED_VS_SPV ${SPV_DIR}/rigged_mesh.vert.spv)
set(RIGGED_FS_SPV ${SPV_DIR}/rigged_mesh.frag.spv)

# Wireframe shaders
set(WIRE_VS_SRC ${SHADER_DIR}/wireframe.vert)
set(WIRE_FS_SRC ${SHADER_DIR}/wireframe.frag)
set(WIRE_VS_SPV ${SPV_DIR}/wireframe.vert.spv)
set(WIRE_FS_SPV ${SPV_DIR}/wireframe.frag.spv)

# Unlit shaders
set(UNLIT_VS_SRC ${SHADER_DIR}/unlit.vert)
set(UNLIT_FS_SRC ${SHADER_DIR}/unlit.frag)
set(UNLIT_VS_SPV ${SPV_DIR}/unlit.vert.spv)
set(UNLIT_FS_SPV ${SPV_DIR}/unlit.frag.spv)

# Compile original sphere shaders
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

# Compile flexible shape shaders
add_custom_command(
	OUTPUT ${FLEX_VS_SPV}
	COMMAND ${GLSLC} -fshader-stage=vert -o ${FLEX_VS_SPV} ${FLEX_VS_SRC}
	DEPENDS ${FLEX_VS_SRC}
	COMMENT "Compiling ${FLEX_VS_SRC}"
)
add_custom_command(
	OUTPUT ${FLEX_FS_SPV}
	COMMAND ${GLSLC} -fshader-stage=frag -o ${FLEX_FS_SPV} ${FLEX_FS_SRC}
	DEPENDS ${FLEX_FS_SRC}
	COMMENT "Compiling ${FLEX_FS_SRC}"
)

# Compile rigged mesh shaders
add_custom_command(
	OUTPUT ${RIGGED_VS_SPV}
	COMMAND ${GLSLC} -fshader-stage=vert -o ${RIGGED_VS_SPV} ${RIGGED_VS_SRC}
	DEPENDS ${RIGGED_VS_SRC}
	COMMENT "Compiling ${RIGGED_VS_SRC}"
)
add_custom_command(
	OUTPUT ${RIGGED_FS_SPV}
	COMMAND ${GLSLC} -fshader-stage=frag -o ${RIGGED_FS_SPV} ${RIGGED_FS_SRC}
	DEPENDS ${RIGGED_FS_SRC}
	COMMENT "Compiling ${RIGGED_FS_SRC}"
)

# Compile wireframe shaders
add_custom_command(
	OUTPUT ${WIRE_VS_SPV}
	COMMAND ${GLSLC} -fshader-stage=vert -o ${WIRE_VS_SPV} ${WIRE_VS_SRC}
	DEPENDS ${WIRE_VS_SRC}
	COMMENT "Compiling ${WIRE_VS_SRC}"
)
add_custom_command(
	OUTPUT ${WIRE_FS_SPV}
	COMMAND ${GLSLC} -fshader-stage=frag -o ${WIRE_FS_SPV} ${WIRE_FS_SRC}
	DEPENDS ${WIRE_FS_SRC}
	COMMENT "Compiling ${WIRE_FS_SRC}"
)

# Compile unlit shaders
add_custom_command(
	OUTPUT ${UNLIT_VS_SPV}
	COMMAND ${GLSLC} -fshader-stage=vert -o ${UNLIT_VS_SPV} ${UNLIT_VS_SRC}
	DEPENDS ${UNLIT_VS_SRC}
	COMMENT "Compiling ${UNLIT_VS_SRC}"
)
add_custom_command(
	OUTPUT ${UNLIT_FS_SPV}
	COMMAND ${GLSLC} -fshader-stage=frag -o ${UNLIT_FS_SPV} ${UNLIT_FS_SRC}
	DEPENDS ${UNLIT_FS_SRC}
	COMMENT "Compiling ${UNLIT_FS_SRC}"
)

# Create shader target that depends on all compiled shaders
add_custom_target(Shaders ALL DEPENDS
	${VS_SPV} ${FS_SPV}
	${FLEX_VS_SPV} ${FLEX_FS_SPV}
	${RIGGED_VS_SPV} ${RIGGED_FS_SPV}
	${WIRE_VS_SPV} ${WIRE_FS_SPV}
	${UNLIT_VS_SPV} ${UNLIT_FS_SPV}
)

# Ensure shaders build whenever the lib builds (so install will have them)
add_dependencies(${PROJECT_NAME} Shaders)

# ---------------- Public includes & link ----------------
target_include_directories(${PROJECT_NAME}
	PUBLIC
		$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
		$<BUILD_INTERFACE:${LVG_GENERATED_INCLUDE_DIR}>
		$<INSTALL_INTERFACE:${LVG_INSTALL_INCLUDEDIR}>
	PRIVATE
		${PROJECT_SOURCE_DIR}/external
)

if (ASSIMP_CONTRIB_INCLUDE_DIR)
	target_include_directories(${PROJECT_NAME} PRIVATE
		$<BUILD_INTERFACE:${ASSIMP_CONTRIB_INCLUDE_DIR}>
	)
endif()

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    Vulkan::Vulkan
    $<BUILD_INTERFACE:${LVG_GLM_BUILD_TARGET}>
    $<INSTALL_INTERFACE:glm::glm>
  PRIVATE
    ${GLFW_LIB_TARGET}
    ${CMAKE_DL_LIBS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE assimp::assimp)
target_compile_definitions(${PROJECT_NAME} PRIVATE
	LVG_BUILD_SHADER_DIR="${SPV_DIR}"
	LVG_DEFAULT_SHADER_INSTALL_DIR="${LVG_SHADER_INSTALL_FULL_DIR}"
)
lvg_apply_project_options(${PROJECT_NAME})
if (LVG_SANITIZER_LINK_FLAG)
	# A sanitized shared library still needs downstream consumers to link the sanitizer runtimes.
	target_link_options(${PROJECT_NAME} INTERFACE ${LVG_SANITIZER_LINK_FLAG})
endif()

if (WIN32)
	target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
	set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# ---------------- Install rules (enables `sudo make install`) ----------------
# 1) Library + export usage requirements
install(TARGETS ${PROJECT_NAME}
    EXPORT LightVulkanGraphicsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# 2) Public headers (everything under include/)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
	DESTINATION ${LVG_INSTALL_INCLUDEDIR}
)
install(FILES
	${LVG_GENERATED_INCLUDE_DIR}/LightVulkanGraphicsVersion.h
	DESTINATION ${LVG_INSTALL_INCLUDEDIR}
)

# 3) SPIR-V payloads (install whatever .spv were generated)
#    Because the library depends on Shaders, running `make install` will build them first.
install(DIRECTORY ${SPV_DIR}/
	DESTINATION ${LVG_SHADER_INSTALL_DIR}
	FILES_MATCHING REGEX ".*\\.spv$"
)

# 4) Exported targets file
install(EXPORT LightVulkanGraphicsTargets
	FILE LightVulkanGraphicsTargets.cmake
	NAMESPACE ${LVG_PACKAGE_NAME}::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${LVG_PACKAGE_NAME}
)

# 5) Package config files for find_package()
write_basic_package_version_file(
	${CMAKE_CURRENT_BINARY_DIR}/${LVG_PACKAGE_NAME}ConfigVersion.cmake
	VERSION ${PROJECT_VERSION}
	COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/${LVG_PACKAGE_NAME}Config.cmake.in
	${CMAKE_CURRENT_BINARY_DIR}/${LVG_PACKAGE_NAME}Config.cmake
	INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${LVG_PACKAGE_NAME}
)
install(FILES
	${CMAKE_CURRENT_BINARY_DIR}/${LVG_PACKAGE_NAME}Config.cmake
	${CMAKE_CURRENT_BINARY_DIR}/${LVG_PACKAGE_NAME}ConfigVersion.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${LVG_PACKAGE_NAME}
)

# Add examples

function(lvg_configure_build_tree_example target)
	target_link_libraries(${target} PRIVATE ${PROJECT_NAME})
	lvg_apply_project_options(${target})

	if (LVG_BUILD_TREE_ORIGIN_RPATH)
		# Prefer the sibling build-tree library over unrelated system installs.
		set_target_properties(${target} PROPERTIES
			BUILD_RPATH "${LVG_BUILD_TREE_ORIGIN_RPATH}"
		)

		if (UNIX AND NOT APPLE)
			target_link_options(${target} PRIVATE "LINKER:--disable-new-dtags")
		endif()
	endif()
endfunction()

#add_executable(line_example line_example.cpp)
#target_link_libraries(line_example LightVulkanGraphics)

#add_executable(cylinder_connection_example cylinder_connection_example.cpp)
#target_link_libraries(cylinder_connection_example LightVulkanGraphics)

#add_executable(scaling_rotation_example scaling_rotation_example.cpp)
#target_link_libraries(scaling_rotation_example LightVulkanGraphics)

#add_executable(lattice_generation_example lattice_generation_example.cpp)
#target_link_libraries(lattice_generation_example LightVulkanGraphics)

# FBX Loader examples
if (LVG_BUILD_EXAMPLES)
	add_executable(simple_fbx_loader_example examples/simple_fbx_loader_example.cpp)
	lvg_configure_build_tree_example(simple_fbx_loader_example)

	add_executable(fbx_rigged_example examples/fbx_rigged_example.cpp)
	lvg_configure_build_tree_example(fbx_rigged_example)

	add_executable(demoVulkanGraphics examples/demo_vulkan_graphics.cpp)
	lvg_configure_build_tree_example(demoVulkanGraphics)
	target_link_libraries(demoVulkanGraphics PRIVATE ${GLFW_LIB_TARGET})

	add_custom_target(LightVulkanGraphicsExamples
		DEPENDS
			simple_fbx_loader_example
			fbx_rigged_example
			demoVulkanGraphics
	)
endif()

if (BUILD_TESTING)
	if (LVG_BUILD_EXAMPLES)
		add_test(
			NAME light_vulkan_graphics_examples_build
			COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target LightVulkanGraphicsExamples
		)
	endif()

	add_test(
		NAME light_vulkan_graphics_package_smoke
		COMMAND ${CMAKE_COMMAND}
			-DLVG_PROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-DLVG_TEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/package_consumer
			-DLVG_TEST_WORK_DIR=${CMAKE_BINARY_DIR}/tests/package_smoke
			-DLVG_CXX_COMPILER=${CMAKE_CXX_COMPILER}
			-DLVG_GENERATOR=${CMAKE_GENERATOR}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPackageConsumerSmokeTest.cmake
	)

	add_test(
		NAME light_vulkan_graphics_package_relocated_smoke
		COMMAND ${CMAKE_COMMAND}
			-DLVG_PROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
			-DLVG_TEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/package_consumer
			-DLVG_TEST_WORK_DIR=${CMAKE_BINARY_DIR}/tests/package_relocated_smoke
			-DLVG_CXX_COMPILER=${CMAKE_CXX_COMPILER}
			-DLVG_GENERATOR=${CMAKE_GENERATOR}
			-DLVG_RELOCATE_INSTALL=ON
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPackageConsumerSmokeTest.cmake
	)
endif()
