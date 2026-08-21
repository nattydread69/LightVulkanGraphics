// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Light Vulkan Graphics
// Copyright (C) 2026 Dr. Nathanael John Inkson
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <lightVulkanGraphics/ui/widgets/TabBar.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <algorithm>

namespace lightGraphics::ui {

TabBar::Tab TabBar::addTab(std::string title) {
	m_tabTitles.push_back(std::move(title));
	return Tab(this, static_cast<int>(m_tabTitles.size()) - 1);
}

void TabBar::setActiveTab(int index) {
	if (tabCount() == 0) {
		return;
	}
	m_activeTab = std::clamp(index, 0, tabCount() - 1);
}

void TabBar::moveActiveTab(int dir) {
	int n = tabCount();
	if (n == 0) {
		return;
	}
	m_activeTab = ((m_activeTab + dir) % n + n) % n;
}

Rect TabBar::headerRect(const GuiContext& ctx) const {
	return { m_bounds.x, m_bounds.y, m_bounds.w, ctx.theme().rowHeight };
}

Rect TabBar::tabButtonRect(const GuiContext& ctx, int tabIndex) const {
	Rect header = headerRect(ctx);
	int n = tabCount();
	if (n == 0) {
		return {};
	}
	float w = header.w / static_cast<float>(n);
	return { header.x + w * static_cast<float>(tabIndex), header.y, w, header.h };
}

int TabBar::tabAtX(const GuiContext& ctx, float mouseX) const {
	int n = tabCount();
	if (n == 0) {
		return -1;
	}
	float w = headerRect(ctx).w / static_cast<float>(n);
	int idx = static_cast<int>((mouseX - m_bounds.x) / w);
	return std::clamp(idx, 0, n - 1);
}

Vec2 TabBar::preferredSize(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	float height = th.rowHeight;   // tab button row

	int activeChildCount = 0;
	for (std::size_t i = 0; i < m_children.size(); ++i) {
		if (m_childTabIndex[i] != m_activeTab) {
			continue;
		}
		height += childAt(i)->preferredSize(ctx).y;
		++activeChildCount;
	}
	// Same trailing-gap-per-child convention CollapsingSection's own preferredSize()
	// uses, for visual consistency between the two composites' stacked-children look.
	if (activeChildCount > 0) {
		height += th.itemSpacing * static_cast<float>(activeChildCount);
	}
	return { 0.0f, height };
}

void TabBar::update(GuiContext& ctx) {
	if (!m_enabled) {
		return;
	}
	const InputState& in = ctx.input();
	const int leftIdx = static_cast<int>(MouseButton::Left);

	Rect header = headerRect(ctx);
	if (ctx.activeId() == id() && in.mousePressed[leftIdx] && header.contains(in.mousePos)) {
		int idx = tabAtX(ctx, in.mousePos.x);
		if (idx >= 0) {
			m_activeTab = idx;
		}
	}

	if (ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (!ev.pressed || ev.repeat) {
				continue;
			}
			if (ev.key == Key::Left) {
				moveActiveTab(-1);
			} else if (ev.key == Key::Right) {
				moveActiveTab(1);
			}
		}
	}

	// Only the active tab's children run update() -- an inactive tab's Slider/Checkbox/
	// etc. must not react to this frame's input at all, the same "closed -> don't touch
	// children" rule CollapsingSection's update() already applies.
	for (std::size_t i = 0; i < m_children.size(); ++i) {
		if (m_childTabIndex[i] != m_activeTab) {
			continue;
		}
		Widget* child = childAt(i);
		if (child->visible()) {
			child->update(ctx);
		}
	}
}

void TabBar::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	Rect header = headerRect(ctx);

	dl.addRectFilled(header, th.frameBg, th.rounding);

	bool headerHovered = ctx.hoveredId() == id() && header.contains(ctx.input().mousePos);
	int hoveredTab = headerHovered ? tabAtX(ctx, ctx.input().mousePos.x) : -1;

	for (int i = 0; i < tabCount(); ++i) {
		Rect btn = tabButtonRect(ctx, i);
		bool active = (i == m_activeTab);
		Color bg = active ? th.frameBgActive : (i == hoveredTab ? th.frameBgHovered : th.frameBg);
		dl.addRectFilled(btn, bg, th.rounding);

		Color textColor = effectivelyEnabled() ? th.text : th.textDisabled;
		dl.addTextClipped(ctx.font(), th.fontSize, btn.inset(th.framePadding * 0.5f), textColor,
		                   m_tabTitles[static_cast<std::size_t>(i)], Align::Center, Align::Center);

		if (active) {
			// A thin accent underline, so the active tab still reads clearly under
			// high-contrast themes where frameBg/frameBgActive are close in value.
			Rect underline{ btn.x, btn.bottom() - 2.0f, btn.w, 2.0f };
			dl.addRectFilled(underline, th.accent);
		}
	}
	dl.addRect(header, th.border, 1.0f, th.rounding);

	for (std::size_t i = 0; i < m_children.size(); ++i) {
		if (m_childTabIndex[i] != m_activeTab) {
			continue;
		}
		Widget* child = childAt(i);
		if (child->visible()) {
			child->draw(dl, ctx);
		}
	}
}

void TabBar::layout(const GuiContext& ctx) {
	const Theme& th = ctx.theme();
	float y = m_bounds.y + th.rowHeight + th.itemSpacing;

	for (std::size_t i = 0; i < m_children.size(); ++i) {
		if (m_childTabIndex[i] != m_activeTab) {
			continue;
		}
		Widget* child = childAt(i);
		Vec2 pref = child->preferredSize(ctx);
		child->setBounds({ m_bounds.x, y, m_bounds.w, pref.y });
		child->layout(ctx);
		y += pref.y + th.itemSpacing;
	}
}

Widget* TabBar::hitTestDeep(Vec2 p) {
	if (!m_visible || !m_enabled || !m_bounds.contains(p)) {
		return nullptr;
	}

	// Same estimate CollapsingSection's own hitTestDeep() uses for its header
	// (CollapsingSection.cpp) -- rowHeight is typically ~22 logical pixels, and this
	// method has no GuiContext to read the real theme value from.
	float headerHeight = 24.0f;
	if (tabCount() > 0 && p.y < m_bounds.y + headerHeight) {
		return this;   // update() resolves which segment from the raw mouse x
	}

	// Active tab's children only, in reverse declaration order -- matching
	// CollapsingSection's own top-to-front hit-test walk, filtered to the tab actually
	// on screen. Note this does NOT stop an inactive tab's child from resolving through
	// findWidget()/collectFocusable() (unchanged, inherited from CompositeWidget) --
	// same limitation CollapsingSection already has for its closed children, not a new
	// gap introduced here.
	for (std::size_t i = m_children.size(); i-- > 0;) {
		if (m_childTabIndex[i] != m_activeTab) {
			continue;
		}
		Widget& child = *childAt(i);
		if (!child.visible()) {
			continue;
		}
		if (Widget* hit = child.hitTestDeep(p)) {
			return hit;
		}
	}
	return nullptr;
}

} // namespace lightGraphics::ui
