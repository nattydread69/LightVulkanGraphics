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
	// docs/gui/04-input-and-events.md, "Scroll wheel": the phase-9 resolution of "wheel
	// over a panel with no overflow currently dies silently" (wantsMouse() is true over
	// ANY panel, scrollable or not, so the wheel neither scrolled the panel nor reached
	// the camera). True when SOMETHING under the cursor will actually consume the wheel
	// this frame -- an open popup, a specific hovered widget that claims it
	// (Widget::wantsWheel(), e.g. SliderT/DropDown), or a hovered panel that currently has
	// overflow to scroll (Panel::needsScrollbar()). False otherwise, even while hovering a
	// panel, so the camera keeps getting the wheel when there is nothing here for it to do.
	bool wantsScroll() const;

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
	// The topmost panel whose rect contains the cursor this frame, or nullptr -- what
	// m_hoveredId/m_hoveredPanel were already resolved from in update(). Exposed so a
	// Panel can tell "the cursor is over ME specifically" apart from "over some other
	// panel that happens to overlap mine" (docs/gui/09 phase 9, "Scrolling": the wheel
	// must scroll only the panel actually under the cursor).
	Panel* hoveredPanel() const { return m_hoveredPanel; }

	// ---- tooltips (docs/gui/06-layout-and-theme.md, "Tooltips") ----
	// True the same frame endFrame() would draw a tooltip. The geometry itself only ever
	// lands in the overlay draw list, so this is the test-facing hook
	// (tests/ui/test_panel.cpp) for "the timer reset" without parsing DrawList output.
	bool isTooltipVisible() const;

	// ---- platform pass-through ----
	// docs/gui/07-public-api.md's GuiContext table (written phase 4, before any widget
	// needed the platform directly) does not list these; PlatformHooks lives on this
	// class specifically so a widget deep inside a panel can reach the platform without
	// every intermediate layer re-exposing it. Added for TextBox (phase 7): copy/cut/
	// paste need clipboard access, and hovering a text box requests the I-beam cursor.
	std::string clipboardText() const;
	void        setClipboardText(std::string_view text) const;
	void        requestCursorShape(CursorShape shape) const;

	// ---- internal: used by Panel and widget update() implementations, not part of the
	// consumer-facing surface normative in docs/gui/07-public-api.md, but required for
	// the generic capture/focus/z-order machinery docs/gui/01-architecture.md describes.
	void setActiveId(WidgetId);
	void clearActiveId();
	Widget* findWidget(WidgetId) const;
	void bringPanelToFront(Panel*);

	// ---- popup (docs/gui/05-widgets.md, "DropDown") ----
	// At most one popup is open at a time -- opening a new one implicitly replaces
	// whatever was open, exactly as clicking a second DropDown while the first is still
	// expanded should look. `screenRect` is expected to be re-supplied by the owner
	// every frame it stays open (DropDown::update() calls this unconditionally each
	// frame it is open, recomputed from its own current bounds): that is what makes the
	// popup FOLLOW the owner when the owning panel is dragged, scrolled, or resized,
	// rather than the rect going stale -- see docs/gui/05's "Popup position when the
	// world moves under it".
	void openPopup(Widget* owner, const Rect& screenRect);
	void closePopup();
	bool isPopupOpen() const { return popupOwner() != nullptr; }
	// Resolves the current popup owner through findWidget(), which naturally answers
	// nullptr if the owner (or its whole panel) has been destroyed since the popup was
	// opened, self-healing m_popupOwnerId back to "no popup" in that case -- see
	// docs/gui/05's "handle the panel being destroyed while its popup is open". Every
	// other popup accessor funnels through this one so that guarantee always applies.
	Widget* popupOwner() const;
	Rect popupRect() const { return m_popupRect; }

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
	// Returns the widget a tooltip should be drawn for this frame, or nullptr -- shared by
	// endFrame() (which draws it) and isTooltipVisible() (which just answers the question).
	Widget* tooltipWidgetIfVisible() const;
	void drawTooltip(const Widget&);

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

	// docs/gui/06-layout-and-theme.md, "Tooltips": "reset the hover timer whenever
	// hoveredId changes... track it in GuiContext, not the widget, so only one tooltip can
	// ever be pending." m_tooltipTargetId is the id the timer below is currently counting
	// for; it is deliberately a separate field from m_hoveredId rather than reusing it, so
	// "did hoveredId change since last frame" has something to compare against.
	WidgetId m_tooltipTargetId = kInvalidWidgetId;
	float    m_tooltipHoverTime = 0.0f;

	// mutable: popupOwner() is const (queried from wantsMouse(), also const) but still
	// needs to self-heal this back to kInvalidWidgetId when the owner no longer exists.
	mutable WidgetId m_popupOwnerId = kInvalidWidgetId;
	Rect m_popupRect;

	Vec2 m_lastDisplaySize;
	bool m_atlasNeedsRebuild = false;

	std::mutex m_mainThreadMutex;
	std::vector<std::function<void()>> m_mainThreadQueue;
};

} // namespace lightGraphics::ui
