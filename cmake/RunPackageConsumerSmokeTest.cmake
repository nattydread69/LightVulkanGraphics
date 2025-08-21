if (NOT DEFINED LVG_PROJECT_BINARY_DIR)
	message(FATAL_ERROR "LVG_PROJECT_BINARY_DIR is required")
endif()

if (NOT DEFINED LVG_TEST_SOURCE_DIR)
	message(FATAL_ERROR "LVG_TEST_SOURCE_DIR is required")
endif()

if (NOT DEFINED LVG_TEST_WORK_DIR)
	message(FATAL_ERROR "LVG_TEST_WORK_DIR is required")
endif()

set(_install_prefix "${LVG_TEST_WORK_DIR}/install")
set(_consumer_prefix "${_install_prefix}")
set(_consumer_build_dir "${LVG_TEST_WORK_DIR}/consumer-build")

file(REMOVE_RECURSE "${LVG_TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${LVG_TEST_WORK_DIR}")

execute_process(
	COMMAND "${CMAKE_COMMAND}" --install "${LVG_PROJECT_BINARY_DIR}" --prefix "${_install_prefix}"
	RESULT_VARIABLE _install_result
)
if (NOT _install_result EQUAL 0)
	message(FATAL_ERROR "Failed to install LightVulkanGraphics into ${_install_prefix}")
endif()

if (LVG_RELOCATE_INSTALL)
	set(_relocated_prefix "${LVG_TEST_WORK_DIR}/relocated")
	execute_process(
		COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_install_prefix}" "${_relocated_prefix}"
		RESULT_VARIABLE _copy_result
	)
	if (NOT _copy_result EQUAL 0)
		message(FATAL_ERROR "Failed to create relocated install tree")
	endif()
	set(_consumer_prefix "${_relocated_prefix}")
endif()

set(_configure_command
	"${CMAKE_COMMAND}"
	-S "${LVG_TEST_SOURCE_DIR}"
	-B "${_consumer_build_dir}"
	"-DCMAKE_PREFIX_PATH=${_consumer_prefix}"
)

if (DEFINED LVG_GENERATOR AND NOT LVG_GENERATOR STREQUAL "")
	list(APPEND _configure_command -G "${LVG_GENERATOR}")
endif()

if (DEFINED LVG_CXX_COMPILER AND NOT LVG_CXX_COMPILER STREQUAL "")
	list(APPEND _configure_command "-DCMAKE_CXX_COMPILER=${LVG_CXX_COMPILER}")
endif()

execute_process(
	COMMAND ${_configure_command}
	RESULT_VARIABLE _configure_result
)
if (NOT _configure_result EQUAL 0)
	message(FATAL_ERROR "Failed to configure the package consumer smoke test")
endif()

execute_process(
	COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build_dir}"
	RESULT_VARIABLE _build_result
)
if (NOT _build_result EQUAL 0)
	message(FATAL_ERROR "Failed to build the package consumer smoke test")
endif()
