# CPack configuration. Included from the top-level CMakeLists.txt after all install rules.
set(CPACK_PACKAGE_NAME "LightVulkanGraphics")
set(CPACK_PACKAGE_VENDOR "Nathanael Inkson")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight Vulkan rendering library")
set(CPACK_PACKAGE_CONTACT "Nathanael Inkson <NathanaelInkson@gmail.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME
	"LightVulkanGraphics-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if (WIN32)
	set(CPACK_GENERATOR ZIP)
elseif (APPLE)
	set(CPACK_GENERATOR TGZ)
else()
	set(CPACK_GENERATOR DEB TGZ)
endif()

# ---- Debian ----
# Every dependency below is something dpkg can map. Bundled Assimp is static and adds
# none; system Assimp adds libassimp-dev (static core consumers re-find it via the
# package config) -- never a bare /usr/local library.
set(CPACK_DEBIAN_PACKAGE_NAME "liblightvulkangraphics-dev")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)   # runtime deps of any shared libs we ship
set(_lvg_deb_deps "libvulkan-dev, libglm-dev, libglfw3-dev, cmake")
if (NOT LVG_ASSIMP_BUNDLED)
	string(APPEND _lvg_deb_deps ", libassimp-dev")
endif()
set(CPACK_DEBIAN_PACKAGE_DEPENDS "${_lvg_deb_deps}")
# A .deb built against an Assimp in /usr/local would declare libassimp-dev but be built
# against a different, dpkg-invisible version. Fail loudly instead of shipping that.
# Only when this library is the top-level project: as a subproject its own packages are not made
# (the parent defines its packaging), so the warning would be noise there.
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR AND NOT LVG_ASSIMP_BUNDLED AND ASSIMP_LIBRARY MATCHES "^/usr/local/" AND "DEB" IN_LIST CPACK_GENERATOR)
	message(WARNING
		"Assimp was found in /usr/local (${ASSIMP_LIBRARY}), which dpkg does not own. "
		"Do not publish a .deb from this build: use a clean container with libassimp-dev, "
		"or configure with -DLVG_USE_SYSTEM_ASSIMP=OFF.")
endif()

if (LVG_ASSIMP_BUNDLED)
	set(CPACK_PACKAGE_DESCRIPTION "Includes a statically linked Assimp ${LVG_ASSIMP_GIT_TAG} (BSD-3-Clause).")
endif()

include(CPack)
