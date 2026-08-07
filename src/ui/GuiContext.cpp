#include <lightVulkanGraphics/ui/GuiContext.h>
#include "UiPlatformGlfw.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace lightGraphics::ui {

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
	for (std::size_t i = 0; i < panel.widgetCount(); ++i) {
		Widget* w = panel.widgetAt(i);
		if (w->visible() && w->hitTest(p)) {
			return w;
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
			m_focusedId = kInvalidWidgetId;
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
				Widget* w = panelPtr->widgetAt(i);
				if (w->visible() && w->enabled() && w->acceptsFocus()) {
					focusable.push_back(w);
				}
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

	// (a) hit test panels front-to-back; m_panels[0] is frontmost.
	Panel* hoveredPanel = nullptr;
	for (auto& panelPtr : m_panels) {
		Panel* p = panelPtr.get();
		if (p->visible() && p->hitTest(mouse)) {
			hoveredPanel = p;
			break;
		}
	}
	m_hoveredPanel = hoveredPanel;

	// (b)/(c) capture pins the hovered widget regardless of cursor position.
	Widget* hoveredWidget = nullptr;
	if (m_activeId != kInvalidWidgetId) {
		hoveredWidget = findWidget(m_activeId);
	} else if (hoveredPanel) {
		hoveredWidget = hitTestWidgets(*hoveredPanel, mouse);
	}
	m_hoveredId = hoveredWidget ? hoveredWidget->id() : kInvalidWidgetId;

	// Press: establish capture/focus/z-order before dispatching to widgets, so a widget
	// observes ctx.activeId() == id() from inside its own update() this same frame.
	if (leftPressed) {
		if (hoveredPanel) {
			bringPanelToFront(hoveredPanel);
		}
		if (m_activeId == kInvalidWidgetId && hoveredWidget && hoveredWidget->enabled() &&
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
	return m_activeId != kInvalidWidgetId || m_hoveredPanel != nullptr;
	// popupOpen (docs/gui/01) arrives in phase 8.
}

bool GuiContext::wantsKeyboard() const {
	if (m_focusedId == kInvalidWidgetId) {
		return false;
	}
	Widget* w = findWidget(m_focusedId);
	return w != nullptr && w->wantsTextInput();
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

void GuiContext::postToMainThread(std::function<void()> fn) {
	std::lock_guard<std::mutex> lock(m_mainThreadMutex);
	m_mainThreadQueue.push_back(std::move(fn));
}

} // namespace lightGraphics::ui
