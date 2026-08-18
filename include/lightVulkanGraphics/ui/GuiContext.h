#pragma once

// Layer 3: the retained-mode root. Owns the theme, font, panels, and all interaction
// state (hoveredId/activeId/focusedId). See docs/gui/01-architecture.md for the
// per-frame contract this class implements and docs/gui/07-public-api.md for the
// normative signatures.
//
// PlatformHooks lives here per docs/gui/07-public-api.md's header-layout table.
// UiPlatformGlfw.h originally carried its own copy of the struct (it had to exist before
// GuiContext did); it now includes this header and reuses this definition rather than
// duplicating it.

#include "Types.h"
#include "Theme.h"
#include "Font.h"
#include "DrawList.h"
#include "InputState.h"
#include "KeyCodes.h"
#include "MenuBar.h"
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
	// The path to a TrueType file to bake. Left empty, the constructor falls back to a
	// small built-in search for the bundled Inter font (env var
	// LIGHT_VULKAN_GRAPHICS_FONT_PATH, build tree, install prefix -- see
	// findBundledFontPath(), GuiContext.cpp) and throws only if that search comes up
	// empty too. A VkApp-based consumer never has to think about any of this:
	// VkApp::initUi() already resolves a concrete path via its own findFontPath() before
	// constructing GuiContext. This fallback exists for a caller constructing a bare
	// GuiContext directly, without VkApp.
	std::string fontPath;
	// Drives the font bake (via GuiContext's constructor). Authoritative over
	// theme.fontSize below -- the constructor overwrites theme.fontSize from this value,
	// so the two can never independently disagree about what size text actually renders
	// at. Set this, not theme.fontSize, to change the GUI's font size.
	float       fontSize      = 14.0f;
	Theme       theme         = Theme::dark();
	int         atlasWidth    = 512;
	int         atlasHeight   = 512;
	bool        enableTooltips = true;
	// 0.0f (default) disables the heading face entirely -- hasHeadingFont() stays false
	// and headingFont() must not be called. A positive value bakes a SECOND Font from
	// the same font bytes as `fontPath`, at this logical size, alongside the primary one
	// (docs/gui/03-text-and-fonts.md, "Headings"): a second, independent baked atlas for
	// headings / larger UI text, distinct from the tabular-figure digit padding (which
	// stays inside the single primary Font). Like fontSize, this is a LOGICAL size --
	// GuiContext folds in contentScale itself before baking, same as the primary font.
	float       headingFontSize = 0.0f;
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
	// An empty fontPath also throws, but only once the built-in search described on
	// GuiCreateInfo::fontPath (above) has already failed to find anything either.
	GuiContext(const GuiCreateInfo&, PlatformHooks);
	~GuiContext();

	GuiContext(const GuiContext&)            = delete;
	GuiContext& operator=(const GuiContext&) = delete;

	// ---- menu bar (docs/gui/05-widgets.md, "MenuBar") ----
	// A single, global, always-on-top row of dropdown menus fixed to the window's top
	// edge -- NOT the per-panel TabBar. Starts empty (menuCount() == 0, drawn as
	// nothing, zero interaction cost) until addMenu() is called on it.
	MenuBar&       menuBar()       { return m_menuBar; }
	const MenuBar& menuBar() const { return m_menuBar; }

	// ---- panels ----
	Panel* createPanel(std::string title, Rect bounds, PanelFlags flags = PanelFlags::Default);
	void   destroyPanel(Panel*);
	void   destroyAllPanels();
	std::size_t panelCount() const { return m_panels.size(); }
	Panel* panelAt(std::size_t index) const;   // index 0 = frontmost
	// The frontmost currently-visible PanelFlags::Modal panel, or nullptr if none is open
	// (docs/gui/05-widgets.md, "Panel", "Modal panels"). m_panels is already stored
	// front-to-back, so this is the first Modal match found walking it -- which is also
	// exactly what governs input while it's non-null: hover/hit-testing and Tab-focus
	// collection are restricted to this one panel, and wantsMouse()/wantsKeyboard()/
	// wantsScroll() all report true unconditionally. If more than one Modal panel is
	// visible at once (a confirmation opened from inside another modal), the newest one
	// -- inserted frontmost by createPanel() -- blocks the older one too, the same way a
	// nested native dialog blocks its parent, without this needing an explicit stack.
	Panel* activeModalPanel() const;

	// ---- frame ----
	void beginFrame(Vec2 displaySize, float contentScale, float deltaTime);
	void update();
	void endFrame();
	const DrawList& drawList() const { return m_drawList; }

	// ---- input hand-off ----
	// All three unconditionally report true whenever activeModalPanel() is non-null,
	// regardless of cursor position or focus state -- docs/gui/05-widgets.md, "Panel",
	// "Modal panels": a modal has to swallow the camera hand-off outright, not just over
	// its own rect, or a click past its edge would reach the 3D scene underneath it.
	bool wantsMouse()    const;
	bool wantsKeyboard() const;
	// docs/gui/04-input-and-events.md, "Scroll wheel": the resolution of "wheel
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

	// ---- heading face (docs/gui/03-text-and-fonts.md, "Headings") ----
	// True iff GuiCreateInfo::headingFontSize was positive at construction. headingFont()
	// and headingFontSize() below are only valid to call when this is true -- a widget
	// that wants to use the heading face (Label::setHeading()) must check this first and
	// fall back to the primary font otherwise, since not every consumer configures one.
	bool hasHeadingFont() const { return m_hasHeadingFont; }
	const Font& headingFont() const { return m_headingFont; }
	// The LOGICAL size the heading face was baked at (GuiCreateInfo::headingFontSize),
	// i.e. what to pass as `pixelSize` to measureText()/addText() on headingFont() --
	// mirrors how theme.fontSize is the render size for the primary font.
	float headingFontSize() const { return m_headingFontSizeLogical; }
	// The TextureId the heading atlas is registered under (VkApp::registerUiTexture(),
	// docs/gui/05-widgets.md, "Image"), or kAtlasTextureId if it hasn't been registered
	// yet -- GuiContext cannot register it itself (it stays Vulkan-agnostic; see
	// docs/gui/02-rendering.md, "Target layering"), so the Vulkan-aware owner (VkApp)
	// registers the atlas after construction and reports the id back here. A DrawList
	// draw call made with this still-default id before that handoff happens (or in a
	// bare, headless GuiContext with no VkApp at all) simply batches into the PRIMARY
	// atlas's command instead -- harmless in a headless test, since no real texture is
	// ever sampled there anyway.
	TextureId headingFontTextureId() const { return m_headingFontTextureId; }
	void setHeadingFontTextureId(TextureId id) { m_headingFontTextureId = id; }

	WidgetId hoveredId() const { return m_hoveredId; }
	WidgetId activeId()  const { return m_activeId; }
	WidgetId focusedId() const { return m_focusedId; }
	void     setFocus(Widget*);
	void     clearFocus();
	// The topmost panel whose rect contains the cursor this frame, or nullptr -- what
	// m_hoveredId/m_hoveredPanel were already resolved from in update(). Exposed so a
	// Panel can tell "the cursor is over ME specifically" apart from "over some other
	// panel that happens to overlap mine" (docs/gui/05, "Panel", "Scrolling": the wheel
	// must scroll only the panel actually under the cursor).
	Panel* hoveredPanel() const { return m_hoveredPanel; }

	// ---- tooltips (docs/gui/06-layout-and-theme.md, "Tooltips") ----
	// True the same frame endFrame() would draw a tooltip. The geometry itself only ever
	// lands in the overlay draw list, so this is the test-facing hook
	// (tests/ui/test_panel.cpp) for "the timer reset" without parsing DrawList output.
	bool isTooltipVisible() const;

	// ---- platform pass-through ----
	// docs/gui/07-public-api.md's GuiContext table (written before any widget needed the
	// platform directly) does not list these; PlatformHooks lives on this class
	// specifically so a widget deep inside a panel can reach the platform without every
	// intermediate layer re-exposing it. TextBox is what forced the issue: copy/cut/paste
	// need clipboard access, and hovering a text box requests the I-beam cursor.
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

	// ---- layout persistence (docs/gui/05-widgets.md, "Panel", "Layout persistence") ----
	// Serializes bounds/collapsed/scrollY for every CURRENT panel that has a non-empty
	// Panel::persistenceId() set -- panels without one are silently skipped (see
	// Panel::setPersistenceId's comment on why this is opt-in rather than title-keyed).
	// The format is a small dependency-free `[id]` + `key=value` text blob, not
	// JSON/INI-via-a-library, matching 00-overview.md's "zero external GUI dependency"
	// goal -- there is intentionally no schema version or nesting, because there is
	// nothing here that needs either.
	std::string saveLayout() const;
	// Applies saved state to whichever CURRENTLY EXISTING panels have a matching
	// persistenceId() -- a panel created after this call, or whose id isn't present in
	// `data`, is left exactly as it was constructed. Unrecognised sections/keys and
	// malformed numbers are skipped line-by-line rather than thrown: this is a
	// best-effort restore of a previous session's layout, not a construction-time
	// contract (docs/gui/07-public-api.md, "Error handling policy" -- this falls under
	// "per-frame operations never throw", not "construction failures throw", since it can
	// run at any point after panels exist, not only at startup).
	void loadLayout(std::string_view data);

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
	MenuBar  m_menuBar;

	std::vector<std::uint8_t> m_fontData;   // kept around so beginFrame() can rebake on DPI change
	float m_fontSizeLogical = 14.0f;
	int   m_atlasWidth = 512;
	int   m_atlasHeight = 512;

	// Heading face -- see hasHeadingFont()/headingFont() above. m_headingFont is baked
	// from the SAME m_fontData as the primary font (headings are a second SIZE of the
	// same face, not a second font file -- see GuiCreateInfo::headingFontSize), so no
	// second copy of the font bytes is kept here.
	Font  m_headingFont;
	bool  m_hasHeadingFont = false;
	float m_headingFontSizeLogical = 0.0f;
	TextureId m_headingFontTextureId = kAtlasTextureId;

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
