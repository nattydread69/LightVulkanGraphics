add_executable(lvgui_tests
	test_placeholder.cpp
)

target_link_libraries(lvgui_tests PRIVATE LightVulkanGraphics::UI)

add_test(NAME lvgui_tests COMMAND lvgui_tests)
