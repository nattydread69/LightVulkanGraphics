cmake_minimum_required(VERSION 3.20)
project(libLightVulkanGraphics VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(LVG_BUILD_EXAMPLES "Build bundled example executables" ON)
option(LVG_BUILD_VOLUME_EXAMPLES "Build volume rendering examples" ON)
option(LVG_BUILD_VULKAN_INTEGRATION_TESTS "Build Vulkan tests that require a GPU" OFF)
option(LVG_ENABLE_WARNINGS "Enable recommended compiler warnings for project targets" ON)
option(LVG_WARNINGS_AS_ERRORS "Treat warnings as errors for project targets" OFF)
option(LVG_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers for supported toolchains" OFF)
option(LVG_BUILD_UI "Build the LVGUI GUI layer" ON)

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
	if (WIN32)
		target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
	endif()

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

if (WIN32 AND NOT ASSIMP_ROOT)
	# Common Windows build-from-source locations (build dir alongside source dir)
	set(_assimp_win_candidates
		"C:/dev/assimp/build"
		"C:/assimp/build"
		"C:/libs/assimp/build"
		"$ENV{USERPROFILE}/source/repos/assimp/build"
	)
	# Also check vcpkg
	if (DEFINED ENV{VCPKG_ROOT})
		list(APPEND _assimp_win_candidates "$ENV{VCPKG_ROOT}/installed/x64-windows")
	endif()
	foreach(_candidate IN LISTS _assimp_win_candidates)
		# scene.h is in the source tree; config.h is generated into the build tree
		cmake_path(GET _candidate PARENT_PATH _candidate_src)
		if (EXISTS "${_candidate_src}/include/assimp/scene.h" OR
			EXISTS "${_candidate}/include/assimp/config.h")
			set(ASSIMP_ROOT "${_candidate}" CACHE PATH "Path to local Assimp installation" FORCE)
			message(STATUS "Auto-detected Assimp at: ${_candidate}")
			break()
		endif()
	endforeach()
endif()

# When built from source, headers are split: generated headers in the build
# tree (include/) and original headers in the source tree (../include/).
if (ASSIMP_ROOT)
	cmake_path(GET ASSIMP_ROOT PARENT_PATH _assimp_src_root)
	set(_assimp_include_hints "${ASSIMP_ROOT}" "${_assimp_src_root}")
else()
	set(_assimp_include_hints "")
endif()

find_path(ASSIMP_INCLUDE_DIR
	NAMES assimp/scene.h
	HINTS ${_assimp_include_hints}
	PATH_SUFFIXES include
)

find_library(ASSIMP_LIBRARY
	NAMES assimp libassimp assimp-vc143-mt assimp-vc142-mt assimp-vc141-mt
	HINTS ${ASSIMP_ROOT}
	PATH_SUFFIXES lib lib64 lib/Release lib/Debug lib/x86_64-linux-gnu
)

if (ASSIMP_LIBRARY AND ASSIMP_INCLUDE_DIR)
	# For from-source builds, also include the build tree for generated headers
	# (e.g. assimp/config.h, assimp/revision.h which are not in the source tree)
	set(_assimp_all_includes "${ASSIMP_INCLUDE_DIR}")
	if (ASSIMP_ROOT AND EXISTS "${ASSIMP_ROOT}/include/assimp/config.h")
		list(APPEND _assimp_all_includes "${ASSIMP_ROOT}/include")
	endif()

	add_library(assimp::assimp UNKNOWN IMPORTED)
	set_target_properties(assimp::assimp PROPERTIES
		IMPORTED_LOCATION "${ASSIMP_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${_assimp_all_includes}"
	)
	message(STATUS "Found assimp library: ${ASSIMP_LIBRARY}")
	message(STATUS "Found assimp headers: ${_assimp_all_includes}")

	# On Windows the import lib and DLL live in different trees (lib/ vs bin/).
	# Derive the DLL path so examples can copy it as a post-build step.
	if (WIN32 AND ASSIMP_LIBRARY MATCHES "\\.lib$")
		string(REGEX REPLACE "/lib/" "/bin/" _assimp_dll_path "${ASSIMP_LIBRARY}")
		string(REGEX REPLACE "\\.lib$" ".dll" _assimp_dll_path "${_assimp_dll_path}")
		if (EXISTS "${_assimp_dll_path}")
			set(ASSIMP_DLL "${_assimp_dll_path}" CACHE FILEPATH "Path to the assimp runtime DLL (Windows only)")
			message(STATUS "Found assimp DLL: ${ASSIMP_DLL}")
		endif()
	endif()
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
# src/ui/ is excluded: it belongs exclusively to the LightVulkanGraphicsUI target below.
# Globbing it in here too would compile FontImpl.cpp's STB_TRUETYPE_IMPLEMENTATION (and
# every other src/ui/*.cpp symbol) into both libraries -- exactly the duplicate-symbol
# hazard docs/gui/03-text-and-fonts.md calls out.
#
# Excluded by exact-path removal (list(REMOVE_ITEM ...)) rather than a path-string
# regex: both globs below come from the same file(GLOB_RECURSE) call, so they use
# whatever separator convention CMake normalizes to on the current platform, and
# REMOVE_ITEM matches whole list entries rather than a substring. That keeps this
# correct on Windows without depending on CMake's forward-slash normalization, and
# unlike a regex such as "/src/ui/", it cannot accidentally swallow an unrelated future
# directory (e.g. src/ui_platform/) just because its name shares a prefix.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/*.cxx"
	"${PROJECT_SOURCE_DIR}/src/*.cc"
)
file(GLOB_RECURSE LVGUI_EXCLUDED_SOURCES CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/ui/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/ui/*.cxx"
	"${PROJECT_SOURCE_DIR}/src/ui/*.cc"
)
list(REMOVE_ITEM SOURCES ${LVGUI_EXCLUDED_SOURCES})

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

# lvg_compile_shader(<stage> <shader-basename-without-extension-or-stage-suffix> <out-var>)
# Compiles ${SHADER_DIR}/<name>.<stage> to ${SPV_DIR}/<name>.<stage>.spv and appends the
# output path to ALL_SHADER_SPVS (used to build the Shaders target's DEPENDS list).
function(lvg_compile_shader stage name)
	set(_src ${SHADER_DIR}/${name}.${stage})
	set(_spv ${SPV_DIR}/${name}.${stage}.spv)
	add_custom_command(
		OUTPUT ${_spv}
		COMMAND ${GLSLC} -fshader-stage=${stage} -o ${_spv} ${_src}
		DEPENDS ${_src}
		COMMENT "Compiling ${_src}"
	)
	set(ALL_SHADER_SPVS ${ALL_SHADER_SPVS} ${_spv} PARENT_SCOPE)
endfunction()

lvg_compile_shader(vert instanced_sphere)
lvg_compile_shader(frag instanced_sphere)
lvg_compile_shader(vert flexible_shape)
lvg_compile_shader(frag flexible_shape)
lvg_compile_shader(vert rigged_mesh)
lvg_compile_shader(frag rigged_mesh)
lvg_compile_shader(vert wireframe)
lvg_compile_shader(frag wireframe)
lvg_compile_shader(vert unlit)
lvg_compile_shader(frag unlit)
lvg_compile_shader(vert shadow_depth)
lvg_compile_shader(vert custom_mesh)
lvg_compile_shader(frag custom_mesh)
lvg_compile_shader(frag custom_mesh_unlit)
lvg_compile_shader(vert screen_text)
lvg_compile_shader(frag screen_text)
lvg_compile_shader(vert volume_raymarch)
lvg_compile_shader(frag volume_raymarch)
if(LVG_BUILD_UI)
	lvg_compile_shader(vert ui)
	lvg_compile_shader(frag ui)
endif()

# Create shader target that depends on all compiled shaders
add_custom_target(Shaders ALL DEPENDS ${ALL_SHADER_SPVS})

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

if(LVG_BUILD_UI)
	set(LVGUI_SOURCES
		src/ui/Types.cpp
		src/ui/DrawList.cpp
		src/ui/Font.cpp
		src/ui/FontImpl.cpp
		src/ui/Utf8.cpp
		src/ui/UiRenderer.cpp
		src/ui/UiPlatformGlfw.cpp
		src/ui/Theme.cpp
		src/ui/Widget.cpp
		src/ui/Panel.cpp
		src/ui/GuiContext.cpp
		src/ui/widgets/Label.cpp
		src/ui/widgets/Separator.cpp
		src/ui/widgets/Button.cpp
		src/ui/widgets/Checkbox.cpp
		src/ui/widgets/RadioButton.cpp
		src/ui/widgets/Slider.cpp
		src/ui/widgets/DragValue.cpp
		src/ui/widgets/TextBox.cpp
		src/ui/widgets/CompositeWidget.cpp
		src/ui/widgets/DropDown.cpp
		src/ui/widgets/Row.cpp
		src/ui/widgets/Vec3Field.cpp
		src/ui/widgets/CollapsingSection.cpp
		src/ui/widgets/ProgressBar.cpp
		src/ui/widgets/PlotLine.cpp
	)

	add_library(LightVulkanGraphicsUI ${LVGUI_SOURCES})
	add_library(LightVulkanGraphics::UI ALIAS LightVulkanGraphicsUI)

	target_compile_features(LightVulkanGraphicsUI PUBLIC cxx_std_17)

	# Linked into the core SHARED library below, so its objects must be PIC.
	set_target_properties(LightVulkanGraphicsUI PROPERTIES POSITION_INDEPENDENT_CODE ON)

	target_include_directories(LightVulkanGraphicsUI PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
	)
	target_include_directories(LightVulkanGraphicsUI PRIVATE
		${PROJECT_SOURCE_DIR}/external
	)

	# The dependency runs core -> UI, never the reverse. LVGUI is a self-contained
	# toolkit: layers 1-2 (Types/DrawList/Font/Utf8) are pure glm+std, and UiRenderer
	# takes explicit Vulkan handles rather than reaching into VkApp. Linking
	# LightVulkanGraphics here instead would be a cycle -- core has to call into the UI
	# to record it into the frame (and, from phase 10, to expose gfx.gui()) -- and CMake
	# rejects cycles unless every target in them is STATIC, which core is not.
	# It also keeps the headless UI tests free of Vulkan, assimp and glfw *headers* --
	# UiRenderer.* and (from phase 4) UiPlatformGlfw.* still need Vulkan/GLFW PRIVATE to
	# link, but nothing in include/lightVulkanGraphics/ui/ exposes either.
	target_link_libraries(LightVulkanGraphicsUI
		PUBLIC
			$<BUILD_INTERFACE:${LVG_GLM_BUILD_TARGET}>
			$<INSTALL_INTERFACE:glm::glm>
		PRIVATE Vulkan::Vulkan ${GLFW_LIB_TARGET}
	)
	lvg_apply_project_options(LightVulkanGraphicsUI)
	if (LVG_SANITIZER_LINK_FLAG)
		# Consumers that link ONLY LightVulkanGraphics::UI (the headless tests do) still
		# need the sanitizer runtimes on their link line. lvg_apply_project_options adds
		# them PRIVATE, which a static library does not propagate, and since the UI
		# target deliberately no longer links the core library they cannot arrive via
		# core's INTERFACE options either.
		target_link_options(LightVulkanGraphicsUI INTERFACE ${LVG_SANITIZER_LINK_FLAG})
	endif()

	# Core owns and drives the UI renderer, so it links the UI target (see above).
	target_link_libraries(${PROJECT_NAME} PRIVATE LightVulkanGraphicsUI)

	# The core library advertises GUI availability to its consumers.
	target_compile_definitions(${PROJECT_NAME} PUBLIC LVG_WITH_UI)

	# Phase 3 needs the bundled font to build the atlas before phase 10 formalises font
	# payload installation; mirrors the LVG_BUILD_SHADER_DIR convention above.
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		LVG_BUILD_FONT_DIR="${PROJECT_SOURCE_DIR}/assets/fonts"
	)
endif()

# ---------------- Install rules (enables `sudo make install`) ----------------
# 1) Library + export usage requirements
install(TARGETS ${PROJECT_NAME}
    EXPORT LightVulkanGraphicsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# 1b) UI Library (optional)
if(LVG_BUILD_UI)
	install(TARGETS LightVulkanGraphicsUI
		EXPORT LightVulkanGraphicsTargets
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	)
endif()

# 2) Public headers (everything under include/)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
	DESTINATION ${LVG_INSTALL_INCLUDEDIR}
)
install(FILES
	${LVG_GENERATED_INCLUDE_DIR}/LightVulkanGraphicsVersion.h
	DESTINATION ${LVG_INSTALL_INCLUDEDIR}
)

# 2b) UI public headers (if built)
if(LVG_BUILD_UI)
	install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/lightVulkanGraphics/
		DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lightVulkanGraphics
	)
endif()

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

	add_executable(rotation_glyph_example examples/rotation_glyph_example.cpp)
	lvg_configure_build_tree_example(rotation_glyph_example)

	# THROWAWAY -- phase 5 visual smoke test. Remove this target and
	# examples/phase5_gui_smoke_demo.cpp once the GUI integration has been confirmed by
	# eye; it is not part of the phase 5 deliverable.
	if (LVG_BUILD_UI)
		add_executable(phase5_gui_smoke_demo examples/phase5_gui_smoke_demo.cpp)
		lvg_configure_build_tree_example(phase5_gui_smoke_demo)
		# Consumer code that instantiates Panel::add<W>() for a concrete widget type is
		# the only place those widgets' constructors/vtables get instantiated -- the core
		# library's own object code never references them, so its PRIVATE static link of
		# LightVulkanGraphicsUI does not re-export those symbols. Any real consumer needs
		# both targets; docs/gui/00-overview.md's phase 0 CMake requirements anticipate
		# this ("an external consumer doing find_package() gets both targets").
		target_link_libraries(phase5_gui_smoke_demo PRIVATE LightVulkanGraphics::UI)
	endif()

	# The permanent, growing LVGUI demo (docs/gui/09-claude-code-prompts.md): every widget
	# gets added here as it lands, phase by phase. Same "consumer code is what actually
	# instantiates widget templates/vtables" reasoning as phase5_gui_smoke_demo above for
	# why it needs LightVulkanGraphics::UI linked directly.
	if (LVG_BUILD_UI)
		add_executable(gui_demo examples/gui_demo.cpp)
		lvg_configure_build_tree_example(gui_demo)
		target_link_libraries(gui_demo PRIVATE LightVulkanGraphics::UI)
	endif()

	# Copy the assimp DLL next to each example on Windows so they run in-place.
	if (WIN32 AND ASSIMP_DLL)
		foreach(_lvg_ex IN ITEMS simple_fbx_loader_example fbx_rigged_example demoVulkanGraphics)
			add_custom_command(TARGET ${_lvg_ex} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${ASSIMP_DLL}"
					"$<TARGET_FILE_DIR:${_lvg_ex}>"
				COMMENT "Copying ${ASSIMP_DLL} for ${_lvg_ex}"
			)
		endforeach()
	endif()

	add_custom_target(LightVulkanGraphicsExamples
		DEPENDS
			simple_fbx_loader_example
			fbx_rigged_example
			demoVulkanGraphics
			rotation_glyph_example
	)
endif()

if (LVG_BUILD_VOLUME_EXAMPLES)
	add_executable(VolumeFogExample examples/VolumeFogExample/main.cpp)
	lvg_configure_build_tree_example(VolumeFogExample)
	if (TARGET LightVulkanGraphicsExamples)
		add_dependencies(LightVulkanGraphicsExamples VolumeFogExample)
	endif()
endif()

if (BUILD_TESTING)
	add_executable(light_vulkan_graphics_unit_tests
		tests/unit/test_core_types.cpp
	)
	lvg_configure_build_tree_example(light_vulkan_graphics_unit_tests)
	target_compile_definitions(light_vulkan_graphics_unit_tests PRIVATE
		LVG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
	)

	add_executable(worker_skinning_sanity
		tests/worker_skinning_sanity.cpp
	)
	lvg_configure_build_tree_example(worker_skinning_sanity)
	target_compile_definitions(worker_skinning_sanity PRIVATE
		LVG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
	)

	if (TARGET LightVulkanGraphicsExamples)
		add_dependencies(LightVulkanGraphicsExamples
			light_vulkan_graphics_unit_tests
			worker_skinning_sanity
		)
	endif()
	add_test(
		NAME light_vulkan_graphics_unit_tests
		COMMAND light_vulkan_graphics_unit_tests
	)
	add_test(
		NAME worker_skinning_sanity
		COMMAND worker_skinning_sanity
	)

	if (LVG_BUILD_VULKAN_INTEGRATION_TESTS)
		add_executable(volume_vulkan_integration_test
			tests/volume_vulkan_integration.cpp
		)
		lvg_configure_build_tree_example(volume_vulkan_integration_test)
		add_test(
			NAME volume_vulkan_integration_test
			COMMAND volume_vulkan_integration_test
		)
	endif()

	if (LVG_BUILD_UI)
		add_subdirectory(tests/ui)
	endif()

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

	# Point every test at the sanitizer suppression file when sanitizers are on, so
	# `ctest` needs no manual ASAN_OPTIONS. See sanitizers.supp for what is suppressed
	# and why (an AMD driver over-read, not ours). detect_leaks is disabled because the
	# Vulkan loader and driver retain process-lifetime allocations.
	if (LVG_ENABLE_SANITIZERS)
		get_property(_lvg_all_tests DIRECTORY PROPERTY TESTS)
		if (_lvg_all_tests)
			set_tests_properties(${_lvg_all_tests} PROPERTIES
				ENVIRONMENT
					"ASAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/sanitizers.supp:detect_leaks=0"
			)
		endif()
	endif()
endif()
