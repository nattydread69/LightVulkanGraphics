add_executable(lvgui_tests
	test_drawlist.cpp
)

target_link_libraries(lvgui_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_tests PRIVATE cxx_std_17)
target_compile_definitions(lvgui_tests PRIVATE
	LVG_UI_TEST_FONT_PATH="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
)

add_test(NAME lvgui_tests COMMAND lvgui_tests)

# test_utf8.cpp calls the internal Utf8.h helpers directly; it links the compiled
# Utf8.cpp from LightVulkanGraphicsUI rather than recompiling it, so it only needs the
# header on its include path.
add_executable(lvgui_utf8_tests
	test_utf8.cpp
)
target_include_directories(lvgui_utf8_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/ui)
target_link_libraries(lvgui_utf8_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_utf8_tests PRIVATE cxx_std_17)

add_test(NAME lvgui_utf8_tests COMMAND lvgui_utf8_tests)

add_executable(lvgui_font_tests
	test_font.cpp
)
target_include_directories(lvgui_font_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/ui)
target_link_libraries(lvgui_font_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_font_tests PRIVATE cxx_std_17)
target_compile_definitions(lvgui_font_tests PRIVATE
	LVG_UI_TEST_FONT_PATH="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
)

add_test(NAME lvgui_font_tests COMMAND lvgui_font_tests)

# Exercises UiRenderer's scissor clamping headlessly -- no device, no window. It needs
# the Vulkan headers for VkRect2D/VkExtent2D, but creates no Vulkan objects.
add_executable(lvgui_uirenderer_tests
	test_uirenderer.cpp
)
target_include_directories(lvgui_uirenderer_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/ui)
target_link_libraries(lvgui_uirenderer_tests PRIVATE LightVulkanGraphics::UI Vulkan::Vulkan)
target_compile_features(lvgui_uirenderer_tests PRIVATE cxx_std_17)

add_test(NAME lvgui_uirenderer_tests COMMAND lvgui_uirenderer_tests)

# These tests live in their own directory scope, so the top-level CMakeLists' pass over
# DIRECTORY PROPERTY TESTS does not reach them; apply the same suppression wiring here.
if (LVG_ENABLE_SANITIZERS)
	get_property(_lvgui_tests DIRECTORY PROPERTY TESTS)
	if (_lvgui_tests)
		set_tests_properties(${_lvgui_tests} PROPERTIES
			ENVIRONMENT
				"ASAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/sanitizers.supp:detect_leaks=0"
		)
	endif()
endif()
