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

# Exercises UiPlatformGlfw's inject*/beginFrame/endFrame bookkeeping headlessly -- no
# GLFWwindow is ever created; only the pure input-state plumbing is under test.
add_executable(lvgui_input_tests
	test_input.cpp
)
target_include_directories(lvgui_input_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/ui)
target_link_libraries(lvgui_input_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_input_tests PRIVATE cxx_std_17)

add_test(NAME lvgui_input_tests COMMAND lvgui_input_tests)

# Phase 5: GuiContext/Panel/Widget layout and interaction, headless -- no Vulkan device,
# no window, no GLFW. Constructs a real Font from the bundled test font.
add_executable(lvgui_layout_tests
	test_layout.cpp
)
target_link_libraries(lvgui_layout_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_layout_tests PRIVATE cxx_std_17)
target_compile_definitions(lvgui_layout_tests PRIVATE
	LVG_UI_TEST_FONT_PATH="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
)

add_test(NAME lvgui_layout_tests COMMAND lvgui_layout_tests)

add_executable(lvgui_hittest_tests
	test_hittest.cpp
)
target_link_libraries(lvgui_hittest_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_hittest_tests PRIVATE cxx_std_17)
target_compile_definitions(lvgui_hittest_tests PRIVATE
	LVG_UI_TEST_FONT_PATH="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
)

add_test(NAME lvgui_hittest_tests COMMAND lvgui_hittest_tests)

# Phase 6: Checkbox, RadioGroup/RadioButton, SliderT<float|int>, DragValueT -- headless,
# same pattern as test_layout.cpp/test_hittest.cpp.
add_executable(lvgui_slider_tests
	test_slider.cpp
)
target_link_libraries(lvgui_slider_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_slider_tests PRIVATE cxx_std_17)
target_compile_definitions(lvgui_slider_tests PRIVATE
	LVG_UI_TEST_FONT_PATH="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
)

add_test(NAME lvgui_slider_tests COMMAND lvgui_slider_tests)

# Phase 7: TextBox's editing state machine, headless -- same pattern as
# test_slider.cpp/test_hittest.cpp. Includes the random-operation caret/anchor
# boundary-validity property test from docs/gui/10-testing.md.
add_executable(lvgui_textedit_tests
	test_textedit.cpp
)
target_link_libraries(lvgui_textedit_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_textedit_tests PRIVATE cxx_std_17)
target_compile_definitions(lvgui_textedit_tests PRIVATE
	LVG_UI_TEST_FONT_PATH="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
)

add_test(NAME lvgui_textedit_tests COMMAND lvgui_textedit_tests)

# Phase 8, session A: CompositeWidget (covered inline in test_layout.cpp, since it's a
# layout-mechanics concern) and DropDown's popup, headless -- same pattern as the tests
# above.
add_executable(lvgui_dropdown_tests
	test_dropdown.cpp
)
target_link_libraries(lvgui_dropdown_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_dropdown_tests PRIVATE cxx_std_17)
target_compile_definitions(lvgui_dropdown_tests PRIVATE
	LVG_UI_TEST_FONT_PATH="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
)

add_test(NAME lvgui_dropdown_tests COMMAND lvgui_dropdown_tests)

# Phase 8, session B: CompositeWidget-derived widgets (Row, Vec3Field, CollapsingSection)
# and simple value widgets (ProgressBar, PlotLine), headless -- same pattern as tests above.
add_executable(lvgui_composites_tests
	test_composites.cpp
)
target_link_libraries(lvgui_composites_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_composites_tests PRIVATE cxx_std_17)
target_compile_definitions(lvgui_composites_tests PRIVATE
	LVG_UI_TEST_FONT_PATH="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
)

add_test(NAME lvgui_composites_tests COMMAND lvgui_composites_tests)

add_executable(lvgui_progressbar_tests
	test_progressbar.cpp
)
target_link_libraries(lvgui_progressbar_tests PRIVATE LightVulkanGraphics::UI)
target_compile_features(lvgui_progressbar_tests PRIVATE cxx_std_17)
target_compile_definitions(lvgui_progressbar_tests PRIVATE
	LVG_UI_TEST_FONT_PATH="${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
)

add_test(NAME lvgui_progressbar_tests COMMAND lvgui_progressbar_tests)

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
