#pragma once

// Layer 3: the retained-mode root. Owns the theme, font, panels, and all interaction
// state (hoveredId/activeId/focusedId). See docs/gui/01-architecture.md for the
// per-frame contract this class implements and docs/gui/07-public-api.md for the
// normative signatures.
//
// PlatformHooks lives here per docs/gui/07-public-api.md's header-layout table. Phase 4's
// UiPlatformGlfw.h carried its own copy of this struct (it needed to exist before
// GuiContext did); it now includes this header and reuses this definition instead of
// duplicating it, per that doc's note on the deviation.

#include "Types.h"
#include "Theme.h"
#include "Font.h"
#include "DrawList.h"
#include "InputState.h"
#include "KeyCodes.h"
#include "Panel.h"

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace lightGraphics::ui {

class UiPlatformGlfw;

struct GuiCreateInfo {
	std::string fontPath;                  // empty = use the standard search order
	float       fontSize      = 14.0f;
	Theme       theme         = Theme::dark();
	int         atlasWidth    = 512;
	int         atlasHeight   = 512;
	bool        enableTooltips = true;
};

struct PlatformHooks {
	std::function<std::string()>          getClipboardText;
	std::function<void(std::string_view)> setClipboardText;
	std::function<void(CursorShape)>      setCursorShape;
};

class GuiContext {
public:
	// Throws std::runtime_error if fontPath cannot be opened or fails to bake (see
	// docs/gui/07-public-api.md, "Error handling policy" -- construction failures throw).
	// fontPath being empty currently also throws: the standard search order it would
	// otherwise use is phase 10 scope (docs/gui/07's GuiCreateInfo::fontPath comment).
	GuiContext(const GuiCreateInfo&, PlatformHooks);
	~GuiContext();

	GuiContext(const GuiContext&)            = delete;
	GuiContext& operator=(const GuiContext&) = delete;

	// ---- panels ----
	Panel* createPanel(std::string title, Rect bounds, PanelFlags flags = PanelFlags::Default);
	void   destroyPanel(Panel*);
	void   destroyAllPanels();
	std::size_t panelCount() const { return m_panels.size(); }
	Panel* panelAt(std::size_t index) const;   // index 0 = frontmost

	// ---- frame ----
	void beginFrame(Vec2 displaySize, float contentScale, float deltaTime);
	void update();
	void endFrame();
	const DrawList& drawList() const { return m_drawList; }

	// ---- input hand-off ----
	bool wantsMouse()    const;
	bool wantsKeyboard() const;

	// ---- input injection (called by the platform layer or by tests) ----
	void injectMousePos(Vec2 logicalPos);
	void injectMouseButton(MouseButton, bool pressed);
	void injectScroll(float delta);
	void injectKey(int key, int mods, bool pressed, bool repeat);
	void injectChar(std::uint32_t codepoint);

	// ---- accessors ----
	Theme&       theme() { return m_theme; }
	const Theme& theme() const { return m_theme; }
	const Font&  font() const { return m_font; }
	const InputState& input() const;

	WidgetId hoveredId() const { return m_hoveredId; }
	WidgetId activeId()  const { return m_activeId; }
	WidgetId focusedId() const { return m_focusedId; }
	void     setFocus(Widget*);
	void     clearFocus();

	// ---- internal: used by Panel and widget update() implementations, not part of the
	// consumer-facing surface normative in docs/gui/07-public-api.md, but required for
	// the generic capture/focus/z-order machinery docs/gui/01-architecture.md describes.
	void setActiveId(WidgetId);
	void clearActiveId();
	Widget* findWidget(WidgetId) const;
	void bringPanelToFront(Panel*);

	// ---- extras ----
	void addWorldLabel(const glm::vec3& worldPos, const glm::mat4& viewProj,
	                    std::string_view text, Color, Vec2 pixelOffset = {});
	void postToMainThread(std::function<void()>);
	bool atlasNeedsRebuild() const { return m_atlasNeedsRebuild; }
	void acknowledgeAtlasRebuild() { m_atlasNeedsRebuild = false; }

private:
	void rebakeFont(float contentScale);
	void updateFocusNavigation();
	Widget* hitTestWidgets(Panel&, Vec2) const;

	Theme    m_theme;
	Font     m_font;
	DrawList m_drawList;
	DrawList m_overlayList;

	std::vector<std::uint8_t> m_fontData;   // kept around so beginFrame() can rebake on DPI change
	float m_fontSizeLogical = 14.0f;
	int   m_atlasWidth = 512;
	int   m_atlasHeight = 512;

	PlatformHooks m_hooks;
	std::unique_ptr<UiPlatformGlfw> m_platform;

	std::vector<std::unique_ptr<Panel>> m_panels;   // front-to-back; index 0 = frontmost

	WidgetId m_hoveredId = kInvalidWidgetId;
	WidgetId m_activeId  = kInvalidWidgetId;
	WidgetId m_focusedId = kInvalidWidgetId;
	Panel*   m_hoveredPanel = nullptr;

	Vec2 m_lastDisplaySize;
	bool m_atlasNeedsRebuild = false;

	std::mutex m_mainThreadMutex;
	std::vector<std::function<void()>> m_mainThreadQueue;
};

} // namespace lightGraphics::ui
