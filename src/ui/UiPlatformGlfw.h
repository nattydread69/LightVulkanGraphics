#pragma once

// Layer 5: GLFW plumbing. This and UiRenderer.* are the only files under src/ui/ or
// include/lightVulkanGraphics/ui/ permitted to include a Vulkan or GLFW header.
//
// UiPlatformGlfw owns the pending/current InputState split and the inject* family
// described in docs/gui/04-input-and-events.md. GLFW's own callbacks feed it through
// install Callbacks(); tests call inject* directly with no window at all. From the widget layer on
// on, GuiContext owns one of these internally and forwards its own inject*/beginFrame/
// endFrame calls to it -- the signatures here already matched the ones normative in
// docs/gui/07-public-api.md, so that swap was mechanical.
//
// PlatformHooks now lives in GuiContext.h (docs/gui/07-public-api.md's header table);
// this header used to carry its own copy (it needed to exist before GuiContext did) and
// now just reuses that definition instead.

#include <lightVulkanGraphics/ui/GuiContext.h>
#include <lightVulkanGraphics/ui/InputState.h>
#include <lightVulkanGraphics/ui/KeyCodes.h>
#include <lightVulkanGraphics/ui/Types.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace lightGraphics::ui {

class UiPlatformGlfw {
public:
	UiPlatformGlfw() = default;
	~UiPlatformGlfw();

	UiPlatformGlfw(const UiPlatformGlfw&) = delete;
	UiPlatformGlfw& operator=(const UiPlatformGlfw&) = delete;

	// Installs the five GLFW callbacks on `window`, chaining unconditionally to
	// whatever was previously installed (docs/gui/04, "GLFW plumbing"). Safe to call
	// once per instance; call uninstallCallbacks() (or destroy this object) before the
	// window is destroyed.
	void installCallbacks(GLFWwindow* window);
	void uninstallCallbacks();

	// Builds the clipboard/cursor hooks for `window`, backed by glfwGetClipboardString,
	// glfwSetClipboardString and glfwSetCursor.
	PlatformHooks makeHooks(GLFWwindow* window) const;
	static std::string getClipboardText(GLFWwindow* window);
	static void setClipboardText(GLFWwindow* window, std::string_view text);
	static void setCursorShape(GLFWwindow* window, CursorShape shape);

	// ---- frame-boundary bookkeeping (docs/gui/04, "Frame-boundary bookkeeping") ----
	void beginFrame(Vec2 displaySize, float contentScale, float deltaTime);
	void endFrame();

	const InputState& current() const { return m_current; }

	// A widget calls this during update() to ask for a cursor shape;
	// endFrame() resets it back to Arrow so the request must be renewed every frame.
	void requestCursorShape(CursorShape shape) { m_requestedCursor = shape; }
	CursorShape requestedCursorShape() const { return m_requestedCursor; }

	// ---- input injection (called by the GLFW trampolines or by tests) ----
	void injectMousePos(Vec2 logicalPos);
	void injectMouseButton(MouseButton button, bool pressed);
	void injectScroll(float delta);
	void injectKey(int key, int mods, bool pressed, bool repeat);
	void injectChar(std::uint32_t codepoint);

private:
	struct PendingButtonEvent {
		MouseButton button;
		bool pressed;
	};

	InputState m_pending;
	InputState m_current;
	std::vector<PendingButtonEvent> m_pendingButtonEvents;
	CursorShape m_requestedCursor = CursorShape::Arrow;
	GLFWwindow* m_window = nullptr;
};

} // namespace lightGraphics::ui
