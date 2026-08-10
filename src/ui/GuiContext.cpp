#include <lightVulkanGraphics/ui/GuiContext.h>
#include <lightVulkanGraphics/ui/widgets/TextBox.h>
#include "UiPlatformGlfw.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace lightGraphics::ui {

namespace {
	struct RequiredGlyph {
		std::uint32_t codepoint;
		const char* usedFor;
	};

	// Codepoints LVGUI itself draws with, independent of whatever a consumer's own
	// labels happen to contain. These must resolve to a real glyph, not Font's fallback
	// glyph -- the fallback is deliberately never zero-size (docs/gui/03-text-and-fonts.md,
	// "no zero-size quad"), which means an unbaked internal codepoint doesn't fail loudly
	// or disappear, it silently renders as visible mojibake (a password box full of "?"
	// was exactly this bug -- see TextBox::kPasswordMaskCodepoint's comment for why
	// U+00B7 was chosen over U+2022 once it was found). Checked once, right after every
	// bake, rather than trusting each call site to have picked a baked codepoint.
	//
	// addTextClipped's ellipsis is deliberately three ASCII periods, not U+2026 HORIZONTAL
	// ELLIPSIS (see DrawList.cpp, kEllipsis) -- chosen specifically so truncated labels
	// don't need an entry here at all, unlike the password mask, which has no plain-ASCII
	// equivalent that still reads as "obscured".
	constexpr RequiredGlyph kRequiredInternalGlyphs[] = {
		{ TextBox::kPasswordMaskCodepoint, "TextBox password-mode masking" },
	};

	// docs/gui/04-input-and-events.md, "Internal glyph verification".
	void verifyInternalGlyphsBaked(const Font& font) {
		for (const RequiredGlyph& g : kRequiredInternalGlyphs) {
			if (!font.hasGlyph(g.codepoint)) {
				char buf[256];
				std::snprintf(buf, sizeof(buf),
					"GuiContext: the baked font is missing U+%04X, which LVGUI requires "
					"internally for %s. Add it to Font::kDefaultGlyphRanges (Font.cpp) or "
					"pick a different codepoint that IS covered by those ranges.",
					g.codepoint, g.usedFor);
				throw std::runtime_error(buf);
			}
		}
	}
}

GuiContext::GuiContext(const GuiCreateInfo& info, PlatformHooks hooks)
	: m_theme(info.theme)
	, m_fontSizeLogical(info.fontSize)
	, m_atlasWidth(info.atlasWidth)
	, m_atlasHeight(info.atlasHeight)
	, m_hooks(std::move(hooks))
	, m_platform(std::make_unique<UiPlatformGlfw>()) {
	if (info.fontPath.empty()) {
		throw std::runtime_error(
			"GuiContext: GuiCreateInfo::fontPath is required until phase 10 adds the "
			"standard font search order (see docs/gui/07-public-api.md, GuiCreateInfo)");
	}

	std::ifstream file(info.fontPath, std::ios::binary | std::ios::ate);
	if (!file) {
		throw std::runtime_error("GuiContext: could not open font file: " + info.fontPath);
	}
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	m_fontData.resize(static_cast<std::size_t>(size));
	if (!file.read(reinterpret_cast<char*>(m_fontData.data()), size)) {
		throw std::runtime_error("GuiContext: could not read font file: " + info.fontPath);
	}

	m_font.bake(m_fontData, m_fontSizeLogical, m_atlasWidth, m_atlasHeight, 1.0f);
	verifyInternalGlyphsBaked(m_font);

	m_drawList.setWhitePixelUV(m_font.whitePixelUV());
	m_overlayList.setWhitePixelUV(m_font.whitePixelUV());
}

GuiContext::~GuiContext() = default;

Panel* GuiContext::createPanel(std::string title, Rect bounds, PanelFlags flags) {
	auto panel = std::make_unique<Panel>(*this, std::move(title), bounds, flags);
	Panel* ptr = panel.get();
	m_panels.insert(m_panels.begin(), std::move(panel));   // newest panel starts frontmost
	return ptr;
}

void GuiContext::destroyPanel(Panel* panel) {
	if (m_hoveredPanel == panel) {
		m_hoveredPanel = nullptr;
	}
	m_panels.erase(
		std::remove_if(m_panels.begin(), m_panels.end(),
			[panel](const std::unique_ptr<Panel>& p) { return p.get() == panel; }),
		m_panels.end());
}

void GuiContext::destroyAllPanels() {
	m_panels.clear();
	m_hoveredPanel = nullptr;
}

Panel* GuiContext::panelAt(std::size_t index) const {
	// Programmer error, not a runtime condition (docs/gui/07-public-api.md, "Error
	// handling policy") -- an out-of-range index means the caller mismatched panelCount().
	assert(index < m_panels.size());
	return m_panels[index].get();
}

void GuiContext::bringPanelToFront(Panel* panel) {
	auto it = std::find_if(m_panels.begin(), m_panels.end(),
		[panel](const std::unique_ptr<Panel>& p) { return p.get() == panel; });
	if (it == m_panels.end() || it == m_panels.begin()) {
		return;
	}
	std::unique_ptr<Panel> moved = std::move(*it);
	m_panels.erase(it);
	m_panels.insert(m_panels.begin(), std::move(moved));
}

void GuiContext::rebakeFont(float contentScale) {
	m_font.bake(m_fontData, m_fontSizeLogical * contentScale, m_atlasWidth, m_atlasHeight, contentScale);
	verifyInternalGlyphsBaked(m_font);
	m_drawList.setWhitePixelUV(m_font.whitePixelUV());
	m_overlayList.setWhitePixelUV(m_font.whitePixelUV());
	m_atlasNeedsRebuild = true;
}

void GuiContext::beginFrame(Vec2 displaySize, float contentScale, float deltaTime) {
	// docs/gui/01-architecture.md step 2. Draining the cross-thread queue happens first
	// so any state a background thread just handed over is visible to this frame's
	// widgets and layout.
	{
		std::vector<std::function<void()>> jobs;
		{
			std::lock_guard<std::mutex> lock(m_mainThreadMutex);
			jobs.swap(m_mainThreadQueue);
		}
		for (auto& job : jobs) {
			job();
		}
	}

	m_lastDisplaySize = displaySize;
	m_platform->beginFrame(displaySize, contentScale, deltaTime);

	// Rebake past ~1% content-scale drift (docs/gui/06, "DPI and content scale").
	float baked = m_font.contentScaleAtBake();
	if (baked <= 0.0f || std::abs(contentScale - baked) > 0.01f * std::max(1.0f, baked)) {
		rebakeFont(contentScale);
	}
}

Widget* GuiContext::hitTestWidgets(Panel& panel, Vec2 p) const {
	// docs/gui/04-input-and-events.md, "Hit testing": "intersect with the clip rect,
	// always" -- a widget (or part of one) scrolled out of the content region must not be
	// hittable even though its stored Rect might still geometrically contain p.
	if (!panel.contentClipRect(*this).contains(p)) {
		return nullptr;
	}
	for (std::size_t i = 0; i < panel.widgetCount(); ++i) {
		Widget* w = panel.widgetAt(i);
		// hitTestDeep() recurses into composites (docs/gui/05, "CompositeWidget") to
		// find the specific CHILD a point belongs to; for a plain leaf widget it is
		// exactly the old `w->hitTest(p) ? w : nullptr`.
		if (w->visible()) {
			if (Widget* hit = w->hitTestDeep(p)) {
				return hit;
			}
		}
	}
	return nullptr;
}

Widget* GuiContext::findWidget(WidgetId id) const {
	if (id == kInvalidWidgetId) {
		return nullptr;
	}
	for (auto& panelPtr : m_panels) {
		Panel& panel = *panelPtr;
		for (std::size_t i = 0; i < panel.widgetCount(); ++i) {
			Widget* w = panel.widgetAt(i);
			if (w->id() == id) {
				return w;
			}
			// A composite's activeId/focusedId is always a CHILD's id, never its own
			// (see CompositeWidget::hitTestDeep) -- descend to find it.
			if (Widget* found = w->findDescendant(id)) {
				return found;
			}
		}
	}
	return nullptr;
}

void GuiContext::updateFocusNavigation() {
	for (const KeyEvent& ev : input().keyQueue) {
		if (!ev.pressed || ev.repeat) {
			continue;
		}
		if (ev.key == Key::Escape) {
			// docs/gui/04-input-and-events.md, "Keyboard": "if a popup is open, close it;
			// else clear focus" -- closing takes priority over the generic default and
			// does NOT also clear focusedId, so the DropDown control itself stays
			// focused (every native combo box collapses its list on Escape without also
			// kicking focus off the control). This also means DropDown itself never
			// needs to inspect the key queue for Escape at all: by the time its own
			// update() runs, ctx.popupOwner() == this is already false, so it just sees
			// "not open" like any other frame the popup happens to be closed.
			if (isPopupOpen()) {
				closePopup();
			} else {
				m_focusedId = kInvalidWidgetId;
			}
			continue;
		}
		if (ev.key != Key::Tab) {
			continue;
		}

		std::vector<Widget*> focusable;
		for (auto& panelPtr : m_panels) {
			if (!panelPtr->visible()) {
				continue;
			}
			for (std::size_t i = 0; i < panelPtr->widgetCount(); ++i) {
				// collectFocusable() applies the same visible/enabled/acceptsFocus test
				// this loop used to inline, and additionally recurses into composites
				// (docs/gui/05, "CompositeWidget") so Tab can reach an individual child.
				panelPtr->widgetAt(i)->collectFocusable(focusable);
			}
		}
		if (focusable.empty()) {
			continue;
		}

		bool shift = (ev.mods & Mod::Shift) != 0;
		auto it = std::find_if(focusable.begin(), focusable.end(),
			[this](Widget* w) { return w->id() == m_focusedId; });

		Widget* next = nullptr;
		if (it == focusable.end()) {
			next = shift ? focusable.back() : focusable.front();
		} else {
			std::size_t idx = static_cast<std::size_t>(it - focusable.begin());
			if (shift) {
				idx = (idx == 0) ? focusable.size() - 1 : idx - 1;
			} else {
				idx = (idx + 1) % focusable.size();
			}
			next = focusable[idx];
		}
		m_focusedId = next->id();
	}
}

void GuiContext::update() {
	const InputState& in = input();
	Vec2 mouse = in.mousePos;
	const bool leftPressed  = in.mousePressed[static_cast<int>(MouseButton::Left)];
	const bool leftReleased = in.mouseReleased[static_cast<int>(MouseButton::Left)];

	// docs/gui/05-widgets.md, "DropDown": an open popup floats above every panel and is
	// hit-tested BEFORE any of them.
	Widget* popup = popupOwner();
	const bool popupHit = popup && m_popupRect.contains(mouse);

	// "Closes on ... any mouse press outside the popup rect." The owner's own CONTROL is
	// deliberately exempt from this: clicking it is the owner's OWN toggle gesture
	// ("closes on a second click of the control", handled inside DropDown::update()
	// itself). If a control click also closed the popup here, DropDown's own
	// press-driven toggle would then see "currently closed" on the very same press and
	// reopen it -- popup would net stay open on what the user meant as a close click.
	if (popup && leftPressed && !popupHit && !popup->hitTest(mouse)) {
		closePopup();
		popup = nullptr;
	}

	// (a) hit test panels front-to-back; m_panels[0] is frontmost. Skipped entirely when
	// the popup claims the point -- it must win even over a DIFFERENT panel that
	// happens to overlap the popup rect and sits in front of the popup's own owner.
	Panel* hoveredPanel = nullptr;
	if (!popupHit) {
		for (auto& panelPtr : m_panels) {
			Panel* p = panelPtr.get();
			if (p->visible() && p->hitTest(mouse)) {
				hoveredPanel = p;
				break;
			}
		}
	}
	m_hoveredPanel = hoveredPanel;

	// (b)/(c) capture pins the hovered widget regardless of cursor position.
	//
	// docs/gui/05-widgets.md, "Resize grip": the grip and the scrollbar grab are
	// hit-tested BEFORE widgets, so an overlapping corner (docs/gui/09 phase 9 geometry:
	// resizeGripSize can exceed windowPadding, putting the grip's hit box a few pixels
	// into the content clip rect's own bottom-right corner) always resolves to the grip
	// over the scrollbar over whatever widget might also be there. Neither is a Widget
	// (Panel itself isn't one -- docs/gui/01-architecture.md, "Ownership"), so they are
	// asked for directly rather than falling out of hitTestWidgets()'s per-widget walk.
	enum class PanelGrab { None, ResizeGrip, ScrollbarGrab };
	PanelGrab grab = PanelGrab::None;

	Widget* hoveredWidget = nullptr;
	if (popupHit) {
		hoveredWidget = popup;
	} else if (m_activeId != kInvalidWidgetId) {
		hoveredWidget = findWidget(m_activeId);
	} else if (hoveredPanel) {
		if (hoveredPanel->hitTestResizeGrip(*this, mouse)) {
			grab = PanelGrab::ResizeGrip;
		} else if (hoveredPanel->hitTestScrollbarGrab(*this, mouse)) {
			grab = PanelGrab::ScrollbarGrab;
		} else {
			hoveredWidget = hitTestWidgets(*hoveredPanel, mouse);
		}
	}
	m_hoveredId = hoveredWidget ? hoveredWidget->id() : kInvalidWidgetId;

	// docs/gui/06-layout-and-theme.md, "Tooltips": "reset the hover timer whenever
	// hoveredId changes." Grip/scrollbar hover (m_hoveredId stays invalid for both, since
	// neither is a Widget) never starts a tooltip timer -- there is no tooltip text to
	// show for either.
	if (m_hoveredId != m_tooltipTargetId) {
		m_tooltipTargetId = m_hoveredId;
		m_tooltipHoverTime = 0.0f;
	} else if (m_hoveredId != kInvalidWidgetId) {
		m_tooltipHoverTime += in.deltaTime;
	}

	// Press: establish capture/focus/z-order before dispatching to widgets, so a widget
	// observes ctx.activeId() == id() from inside its own update() this same frame.
	if (leftPressed) {
		if (hoveredPanel) {
			bringPanelToFront(hoveredPanel);
		}
		// "Takes mouse capture like any other widget" (docs/gui/09 phase 9, "Scrolling")
		// -- the grip/scrollbar branches claim m_activeId exactly like the widget branch
		// below does, via the SAME field, which is what makes wantsMouse() stay true and
		// the drag keep tracking once the cursor leaves the panel.
		if (m_activeId == kInvalidWidgetId && grab == PanelGrab::ResizeGrip) {
			m_activeId = hoveredPanel->resizeGripId();
			hoveredPanel->beginResizeDrag(mouse);
		} else if (m_activeId == kInvalidWidgetId && grab == PanelGrab::ScrollbarGrab) {
			m_activeId = hoveredPanel->scrollbarGrabId();
			hoveredPanel->beginScrollbarDrag(*this, mouse);
		} else if (m_activeId == kInvalidWidgetId && hoveredWidget && hoveredWidget->enabled() &&
		           hoveredWidget->acceptsCapture()) {
			m_activeId = hoveredWidget->id();
			m_focusedId = hoveredWidget->acceptsFocus() ? hoveredWidget->id() : kInvalidWidgetId;
		} else if (!hoveredWidget && !hoveredPanel) {
			// A click on the scene, outside every panel.
			m_focusedId = kInvalidWidgetId;
		}
	}

	updateFocusNavigation();

	// (e) panels update front-to-back (each panel updates its own widgets).
	for (auto& panelPtr : m_panels) {
		if (panelPtr->visible()) {
			panelPtr->update(*this);
		}
	}

	// activeId is released only after every widget has had a chance to observe it this
	// frame -- e.g. Button's release-inside check reads ctx.activeId() == id() from
	// inside the update() call that just ran above.
	if (leftReleased) {
		m_activeId = kInvalidWidgetId;
	}
}

void GuiContext::endFrame() {
	// Two layout passes: a wrapping Label's true height is only known once its row width
	// is, which itself is only known after the first pass runs (docs/gui/05, "Label").
	for (auto& panelPtr : m_panels) {
		if (panelPtr->visible()) {
			panelPtr->layout(*this);
			panelPtr->layout(*this);
		}
	}

	m_drawList.clear();
	m_drawList.setWhitePixelUV(m_font.whitePixelUV());

	for (auto it = m_panels.rbegin(); it != m_panels.rend(); ++it) {
		if ((*it)->visible()) {
			(*it)->draw(m_drawList, *this);
		}
	}

	// The popup draws into overlayList AFTER every panel above -- docs/gui/05,
	// "DropDown": it "must escape its parent panel's clip rect and draw above every
	// other panel, including panels in front of its owner." pushClipRect(rect, false)
	// REPLACES rather than intersects the current clip, so the popup is not additionally
	// clamped by whatever clip its owning panel happened to leave pushed.
	if (Widget* owner = popupOwner()) {
		m_overlayList.pushClipRect(m_popupRect, /*intersectWithCurrent=*/false);
		owner->drawPopup(m_overlayList, *this);
		m_overlayList.popClipRect();
	}

	// docs/gui/06-layout-and-theme.md, "Tooltips" -- drawn after the popup, into the same
	// overlay list, so it floats above every panel too. A popup being open already
	// suppresses this (see tooltipWidgetIfVisible()), so there is no ordering conflict
	// between the two ever actually drawing at once.
	if (Widget* w = tooltipWidgetIfVisible()) {
		drawTooltip(*w);
	}

	// m_overlayList is deliberately NOT cleared at the top of this function: content
	// (world labels now; tooltips/popups from phases 8-9) may be added any time between
	// the previous endFrame() and this one, including during update() above, so clearing
	// it first would discard that frame's additions before they are ever drawn. It is
	// appended here and cleared afterwards instead, ready for the next frame's additions.
	m_drawList.append(m_overlayList);
	m_overlayList.clear();
	m_overlayList.setWhitePixelUV(m_font.whitePixelUV());

	m_platform->endFrame();
}

bool GuiContext::wantsMouse() const {
	return m_activeId != kInvalidWidgetId || m_hoveredPanel != nullptr || isPopupOpen();
}

bool GuiContext::wantsScroll() const {
	// docs/gui/04-input-and-events.md, "Scroll wheel": the phase-9 decision. Unlike
	// wantsMouse() (deliberately true over any panel, to swallow drags/clicks aimed at the
	// panel's own background), this asks the narrower question "will something actually
	// react to a wheel tick right now" so a non-overflowing panel doesn't blind the camera
	// to zoom/dolly for no reason.
	if (isPopupOpen()) {
		return true;
	}
	if (Widget* w = findWidget(m_hoveredId)) {
		if (w->wantsWheel()) {
			return true;
		}
	}
	return m_hoveredPanel != nullptr && m_hoveredPanel->needsScrollbar();
}

bool GuiContext::wantsKeyboard() const {
	if (m_focusedId == kInvalidWidgetId) {
		return false;
	}
	Widget* w = findWidget(m_focusedId);
	return w != nullptr && w->wantsTextInput();
}

void GuiContext::openPopup(Widget* owner, const Rect& screenRect) {
	m_popupOwnerId = owner ? owner->id() : kInvalidWidgetId;
	m_popupRect = screenRect;
}

void GuiContext::closePopup() {
	m_popupOwnerId = kInvalidWidgetId;
}

Widget* GuiContext::popupOwner() const {
	if (m_popupOwnerId == kInvalidWidgetId) {
		return nullptr;
	}
	Widget* w = findWidget(m_popupOwnerId);
	if (!w) {
		// The owner's panel was destroyed while the popup was open -- self-heal instead
		// of leaving m_popupOwnerId pointing at an id nothing will ever claim again
		// (docs/gui/05, "DropDown": "popupOwner must not dangle").
		m_popupOwnerId = kInvalidWidgetId;
	}
	return w;
}

void GuiContext::injectMousePos(Vec2 logicalPos) { m_platform->injectMousePos(logicalPos); }
void GuiContext::injectMouseButton(MouseButton button, bool pressed) { m_platform->injectMouseButton(button, pressed); }
void GuiContext::injectScroll(float delta) { m_platform->injectScroll(delta); }
void GuiContext::injectKey(int key, int mods, bool pressed, bool repeat) { m_platform->injectKey(key, mods, pressed, repeat); }
void GuiContext::injectChar(std::uint32_t codepoint) { m_platform->injectChar(codepoint); }

const InputState& GuiContext::input() const { return m_platform->current(); }

void GuiContext::setFocus(Widget* widget) {
	m_focusedId = widget ? widget->id() : kInvalidWidgetId;
}

void GuiContext::clearFocus() {
	m_focusedId = kInvalidWidgetId;
}

void GuiContext::setActiveId(WidgetId id) { m_activeId = id; }
void GuiContext::clearActiveId() { m_activeId = kInvalidWidgetId; }

std::string GuiContext::clipboardText() const {
	return m_hooks.getClipboardText ? m_hooks.getClipboardText() : std::string();
}

void GuiContext::setClipboardText(std::string_view text) const {
	if (m_hooks.setClipboardText) {
		m_hooks.setClipboardText(text);
	}
}

void GuiContext::requestCursorShape(CursorShape shape) const {
	if (m_hooks.setCursorShape) {
		m_hooks.setCursorShape(shape);
	}
}

void GuiContext::addWorldLabel(const glm::vec3& worldPos, const glm::mat4& viewProj,
                                std::string_view text, Color color, Vec2 pixelOffset) {
	glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
	if (clip.w <= 0.0f) {
		return;   // behind the camera
	}
	glm::vec3 ndc = glm::vec3(clip) / clip.w;
	if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) {
		return;
	}

	Vec2 screen{
		(ndc.x * 0.5f + 0.5f) * m_lastDisplaySize.x + pixelOffset.x,
		(1.0f - (ndc.y * 0.5f + 0.5f)) * m_lastDisplaySize.y + pixelOffset.y
	};
	m_overlayList.addText(m_font, m_theme.fontSize, screen, color, text);
}

Widget* GuiContext::tooltipWidgetIfVisible() const {
	// Mid-drag (m_activeId set, including a Panel resize/scrollbar drag) or with a popup
	// open, a tooltip would be visual noise on top of something the user is actively
	// doing -- suppress it rather than showing both.
	if (m_activeId != kInvalidWidgetId || isPopupOpen()) {
		return nullptr;
	}
	if (m_hoveredId == kInvalidWidgetId || m_tooltipHoverTime < m_theme.tooltipDelay) {
		return nullptr;
	}
	Widget* w = findWidget(m_hoveredId);
	if (!w || w->tooltip().empty()) {
		return nullptr;
	}
	return w;
}

bool GuiContext::isTooltipVisible() const {
	return tooltipWidgetIfVisible() != nullptr;
}

void GuiContext::drawTooltip(const Widget& w) {
	const Vec2 mouse = input().mousePos;
	const Vec2 pad{ 6.0f, 4.0f };
	const Vec2 cursorOffset{ 16.0f, 16.0f };   // below-right of the cursor

	Vec2 textSize = m_font.measureText(w.tooltip(), m_theme.fontSize);
	Vec2 size = textSize + pad * 2.0f;
	Vec2 pos = mouse + cursorOffset;

	// docs/gui/06-layout-and-theme.md, "Tooltips": "flipped if it would leave the
	// framebuffer." Flips independently per axis, to the OTHER side of the cursor.
	if (pos.x + size.x > m_lastDisplaySize.x) {
		pos.x = mouse.x - cursorOffset.x - size.x;
	}
	if (pos.y + size.y > m_lastDisplaySize.y) {
		pos.y = mouse.y - cursorOffset.y - size.y;
	}

	Rect box{ pos.x, pos.y, size.x, size.y };
	m_overlayList.addRectFilled(box, m_theme.windowBg, m_theme.rounding);
	m_overlayList.addRect(box, m_theme.border, 1.0f, m_theme.rounding);
	m_overlayList.addText(m_font, m_theme.fontSize, { box.x + pad.x, box.y + pad.y }, m_theme.text, w.tooltip());
}

void GuiContext::postToMainThread(std::function<void()> fn) {
	std::lock_guard<std::mutex> lock(m_mainThreadMutex);
	m_mainThreadQueue.push_back(std::move(fn));
}

} // namespace lightGraphics::ui
