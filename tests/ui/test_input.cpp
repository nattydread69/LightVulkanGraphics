#include "UiPlatformGlfw.h"

#include <cassert>
#include <iostream>

namespace lvgui = lightGraphics::ui;

namespace {

	void testMousePosAndDelta() {
		lvgui::UiPlatformGlfw platform;

		platform.injectMousePos({ 10.0f, 20.0f });
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(platform.current().mousePos.x == 10.0f);
		assert(platform.current().mousePos.y == 20.0f);
		// No previous frame yet -- delta is measured from the default-constructed
		// mousePosPrev {0,0}.
		assert(platform.current().mouseDelta.x == 10.0f);
		assert(platform.current().mouseDelta.y == 20.0f);
		platform.endFrame();

		platform.injectMousePos({ 15.0f, 20.0f });
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(platform.current().mouseDelta.x == 5.0f);
		assert(platform.current().mouseDelta.y == 0.0f);
		platform.endFrame();

		std::cout << "✓ testMousePosAndDelta\n";
	}

	void testPressReleaseEdges() {
		lvgui::UiPlatformGlfw platform;

		platform.injectMousePos({ 50.0f, 60.0f });
		platform.injectMouseButton(lvgui::MouseButton::Left, true);
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(platform.current().mouseDown[0]);
		assert(platform.current().mousePressed[0]);
		assert(!platform.current().mouseReleased[0]);
		assert(platform.current().mouseDownPos[0].x == 50.0f);
		assert(platform.current().mouseDownPos[0].y == 60.0f);
		platform.endFrame();

		// Edge flags must not leak into the next frame with nothing injected.
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(platform.current().mouseDown[0]);       // still held
		assert(!platform.current().mousePressed[0]);    // edge already consumed
		assert(!platform.current().mouseReleased[0]);
		platform.endFrame();

		platform.injectMouseButton(lvgui::MouseButton::Left, false);
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(!platform.current().mouseDown[0]);
		assert(!platform.current().mousePressed[0]);
		assert(platform.current().mouseReleased[0]);
		platform.endFrame();

		std::cout << "✓ testPressReleaseEdges\n";
	}

	void testFastClickNotLost() {
		// A press and release queued within the same frame (a fast click, or a
		// high-polling-rate mouse) must set both edges even though the level state
		// ends the frame back at "up". docs/gui/04, "onMouseButton".
		lvgui::UiPlatformGlfw platform;

		platform.injectMouseButton(lvgui::MouseButton::Left, true);
		platform.injectMouseButton(lvgui::MouseButton::Left, false);
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(platform.current().mousePressed[0]);
		assert(platform.current().mouseReleased[0]);
		assert(!platform.current().mouseDown[0]);
		platform.endFrame();

		std::cout << "✓ testFastClickNotLost\n";
	}

	void testReleaseWithoutPriorPressIsNotAnEdge() {
		lvgui::UiPlatformGlfw platform;

		platform.injectMouseButton(lvgui::MouseButton::Right, false);
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(!platform.current().mouseDown[1]);
		assert(!platform.current().mouseReleased[1]);
		platform.endFrame();

		std::cout << "✓ testReleaseWithoutPriorPressIsNotAnEdge\n";
	}

	void testMouseDownDuration() {
		lvgui::UiPlatformGlfw platform;

		assert(platform.current().mouseDownDuration[0] == -1.0f); // never touched yet

		platform.injectMouseButton(lvgui::MouseButton::Left, true);
		platform.beginFrame({ 800, 600 }, 1.0f, 0.5f);
		assert(platform.current().mouseDownDuration[0] == 0.0f);
		platform.endFrame();

		platform.beginFrame({ 800, 600 }, 1.0f, 0.5f);
		assert(platform.current().mouseDownDuration[0] == 0.5f);
		platform.endFrame();

		platform.beginFrame({ 800, 600 }, 1.0f, 0.25f);
		assert(platform.current().mouseDownDuration[0] == 0.75f);
		platform.endFrame();

		platform.injectMouseButton(lvgui::MouseButton::Left, false);
		platform.beginFrame({ 800, 600 }, 1.0f, 0.25f);
		assert(platform.current().mouseDownDuration[0] == -1.0f);
		platform.endFrame();

		std::cout << "✓ testMouseDownDuration\n";
	}

	void testWheelDeltaAccumulatesAndResets() {
		lvgui::UiPlatformGlfw platform;

		platform.injectScroll(1.0f);
		platform.injectScroll(2.5f);
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(platform.current().wheelDelta == 3.5f);
		platform.endFrame();

		// Nothing injected this time -- must not carry the previous value forward.
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(platform.current().wheelDelta == 0.0f);
		platform.endFrame();

		std::cout << "✓ testWheelDeltaAccumulatesAndResets\n";
	}

	void testCharAndKeyQueues() {
		lvgui::UiPlatformGlfw platform;

		platform.injectChar(0x41);  // 'A'
		platform.injectChar(0x3B1); // Greek alpha
		platform.injectKey(lvgui::Key::Enter, lvgui::Mod::Shift, true, false);
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(platform.current().charQueue.size() == 2);
		assert(platform.current().charQueue[0] == 0x41);
		assert(platform.current().charQueue[1] == 0x3B1);
		assert(platform.current().keyQueue.size() == 1);
		assert(platform.current().keyQueue[0].key == lvgui::Key::Enter);
		assert(platform.current().keyQueue[0].mods == lvgui::Mod::Shift);
		assert(platform.current().keyQueue[0].pressed);
		assert(!platform.current().keyQueue[0].repeat);
		platform.endFrame();

		// Queues must not leak into a frame with nothing new injected.
		platform.beginFrame({ 800, 600 }, 1.0f, 0.016f);
		assert(platform.current().charQueue.empty());
		assert(platform.current().keyQueue.empty());
		platform.endFrame();

		std::cout << "✓ testCharAndKeyQueues\n";
	}

	void testDisplaySizeAndContentScalePassThrough() {
		lvgui::UiPlatformGlfw platform;

		platform.beginFrame({ 1920.0f, 1080.0f }, 2.0f, 0.033f);
		assert(platform.current().displaySize.x == 1920.0f);
		assert(platform.current().displaySize.y == 1080.0f);
		assert(platform.current().contentScale == 2.0f);
		assert(platform.current().deltaTime == 0.033f);
		platform.endFrame();

		std::cout << "✓ testDisplaySizeAndContentScalePassThrough\n";
	}

	void testCursorShapeRequestResetsEachFrame() {
		lvgui::UiPlatformGlfw platform;
		assert(platform.requestedCursorShape() == lvgui::CursorShape::Arrow);

		platform.requestCursorShape(lvgui::CursorShape::TextInput);
		assert(platform.requestedCursorShape() == lvgui::CursorShape::TextInput);

		platform.endFrame();
		assert(platform.requestedCursorShape() == lvgui::CursorShape::Arrow);

		std::cout << "✓ testCursorShapeRequestResetsEachFrame\n";
	}

	void testKeyAndModCodesAreDistinct() {
		const int keys[] = {
			lvgui::Key::Backspace, lvgui::Key::Delete, lvgui::Key::Tab, lvgui::Key::Enter,
			lvgui::Key::Escape, lvgui::Key::Left, lvgui::Key::Right, lvgui::Key::Up,
			lvgui::Key::Down, lvgui::Key::Home, lvgui::Key::End, lvgui::Key::PageUp,
			lvgui::Key::PageDown, lvgui::Key::A, lvgui::Key::C, lvgui::Key::V,
			lvgui::Key::X, lvgui::Key::Z, lvgui::Key::Y,
		};
		for (std::size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
			assert(keys[i] >= 0 && keys[i] < lvgui::Key::Count);
			for (std::size_t j = i + 1; j < sizeof(keys) / sizeof(keys[0]); ++j) {
				assert(keys[i] != keys[j]);
			}
		}
		assert(lvgui::Key::Unknown == -1);

		assert(lvgui::Mod::Shift == 1);
		assert(lvgui::Mod::Ctrl == 2);
		assert(lvgui::Mod::Alt == 4);
		assert(lvgui::Mod::Super == 8);

		std::cout << "✓ testKeyAndModCodesAreDistinct\n";
	}

}

int main() {
	testMousePosAndDelta();
	testPressReleaseEdges();
	testFastClickNotLost();
	testReleaseWithoutPriorPressIsNotAnEdge();
	testMouseDownDuration();
	testWheelDeltaAccumulatesAndResets();
	testCharAndKeyQueues();
	testDisplaySizeAndContentScalePassThrough();
	testCursorShapeRequestResetsEachFrame();
	testKeyAndModCodesAreDistinct();

	std::cout << "\n✅ All input tests passed!\n";
	return 0;
}
