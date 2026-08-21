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

#include <lightVulkanGraphics/ui/widgets/CompositeWidget.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

namespace lightGraphics::ui {

void CompositeWidget::update(GuiContext& ctx) {
	if (!m_enabled || !m_visible) {
		// "A disabled composite disables its children without mutating their own
		// flags" (docs/gui/05, "CompositeWidget"): children simply never run update()
		// this frame, so none of them can react to input, drag, or fire a callback --
		// functionally disabled without this class ever touching child->m_enabled.
		return;
	}

	for (auto& child : m_children) {
		if (child->visible()) {
			child->update(ctx);
		}
	}
}

void CompositeWidget::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	for (auto& child : m_children) {
		if (child->visible()) {
			child->draw(dl, ctx);
		}
	}
}

bool CompositeWidget::hitTest(Vec2 p) const {
	return m_visible && m_enabled && m_bounds.contains(p);
}

Widget* CompositeWidget::hitTestDeep(Vec2 p) {
	if (!m_visible || !m_enabled || !m_bounds.contains(p)) {
		return nullptr;
	}
	// Reverse order, matching docs/gui/04's "walk ... widgets in reverse declaration
	// order" note for Panel's own top-level hit-testing: a later-added child is drawn
	// last (on top), so it should win an overlapping hit test.
	for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
		Widget& child = **it;
		if (!child.visible()) {
			continue;
		}
		if (Widget* hit = child.hitTestDeep(p)) {
			return hit;
		}
	}
	// Within the composite's own bounds, but no child claims this exact point (e.g. the
	// itemSpacing gap between Vec3Field's three fields) -- the composite itself is a
	// layout container, not an interactive target, so this is a deliberate miss rather
	// than falling back to `this`.
	return nullptr;
}

Widget* CompositeWidget::findDescendant(WidgetId targetId) {
	for (auto& child : m_children) {
		if (child->id() == targetId) {
			return child.get();
		}
		if (Widget* found = child->findDescendant(targetId)) {
			return found;
		}
	}
	return nullptr;
}

void CompositeWidget::collectFocusable(std::vector<Widget*>& out) {
	if (!m_visible || !m_enabled) {
		return;   // see update(): a disabled/invisible composite removes its children
		          // from Tab order entirely, without touching their own flags.
	}
	if (acceptsFocus()) {
		// A derived composite that is itself a tab stop (e.g. CollapsingSection's
		// header) opts in by overriding acceptsFocus(); Row/Vec3Field leave it at
		// Widget's default `false` and only their children are reachable by Tab.
		out.push_back(this);
	}
	for (auto& child : m_children) {
		child->collectFocusable(out);
	}
}

} // namespace lightGraphics::ui
