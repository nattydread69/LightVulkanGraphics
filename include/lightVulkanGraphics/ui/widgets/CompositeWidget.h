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

#pragma once

// docs/gui/05-widgets.md, the note under "Vec3Field": "the composite pattern used here
// should be reusable -- factor out a small CompositeWidget base that forwards update,
// draw, and hit-testing to children." This is that base. Three widgets in the next
// implementation session (Row, Vec3Field, CollapsingSection) derive from it, so this
// class only provides the TRAVERSAL (owning children, forwarding update/draw/hit-test,
// inheriting enabled/visible state); it deliberately has no placement POLICY of its own
// -- Row splits horizontally, Vec3Field splits into thirds, CollapsingSection stacks
// vertically below a header, and there is no default that suits more than one of them.
//
// Child interactivity (docs/gui/01-architecture.md, "Widget identity"/hoveredId-
// activeId-focusedId): each child keeps its OWN WidgetId and is independently
// hover/capture/focus-able, exactly like a top-level Panel widget. That only works
// because Widget grew three small composite-aware hooks (hitTestDeep, findDescendant,
// collectFocusable) that GuiContext now calls instead of hitTest()/an id-equality
// check/acceptsFocus() wherever it walks a panel's top-level widget list -- see
// Widget.h's "composite support" section and GuiContext.cpp's hitTestWidgets/
// findWidget/updateFocusNavigation. This class overrides all three to recurse.

#include "../Widget.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace lightGraphics::ui {

class CompositeWidget : public Widget {
public:
	// Mirrors Panel::add<W>(Args&&...): constructs W in place, takes ownership, returns
	// a raw observing pointer. Sets the child's m_panel to match this composite's m_panel,
	// allowing nested widgets to walk up to the owning Panel. Also sets m_parent to this
	// composite -- see Widget::effectivelyEnabled().
	template <typename W, typename... Args>
	W* add(Args&&... args) {
		static_assert(std::is_base_of<Widget, W>::value,
			"CompositeWidget::add<W> requires W to derive from Widget");
		auto child = std::make_unique<W>(std::forward<Args>(args)...);
		W* ptr = child.get();
		ptr->m_parent = this;
		ptr->m_panel = m_panel;  // Propagate owning Panel to children
		m_children.push_back(std::move(child));
		return ptr;
	}

	std::size_t childCount() const { return m_children.size(); }
	Widget* childAt(std::size_t index) const { return m_children[index].get(); }

	// Propagates this composite's m_panel to all children (and their descendants if they're
	// composites). Called by Panel after adding a CompositeWidget, since children may be
	// added during the composite's constructor before m_panel is set.
	void propagatePanelToChildren() {
		for (auto& child : m_children) {
			child->m_panel = m_panel;
			// If the child is also a composite, propagate recursively
			if (auto* childComposite = dynamic_cast<CompositeWidget*>(child.get())) {
				childComposite->propagatePanelToChildren();
			}
		}
	}

	// ---- Widget overrides: traversal only ----
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;
	bool hitTest(Vec2 p) const override;
	Widget* hitTestDeep(Vec2 p) override;
	Widget* findDescendant(WidgetId) override;
	void collectFocusable(std::vector<Widget*>&) override;

	// preferredSize() stays pure-virtual, inherited unchanged from Widget: a composite's
	// size is entirely a function of its placement policy (a Row's height is its
	// tallest child; CollapsingSection's is header-only when closed), and guessing a
	// default here would only ever be right for one of the three derived widgets.
	//
	// layout(const GuiContext&) is likewise NOT overridden here, on purpose: it is
	// inherited straight from Widget (see Widget.h's "layout" section) as a no-op, and
	// Row/Vec3Field/CollapsingSection each override it directly to position m_children
	// from m_bounds -- Row splits horizontally, Vec3Field into thirds,
	// CollapsingSection stacks vertically below a header, and there is no default that
	// suits more than one of them. It is called by Panel::layout() immediately after
	// EVERY setBounds() call it makes on a widget, including the bounds-override path,
	// so a composite positioned that way is laid out too, in the same pass -- not one
	// cached from an earlier call in the same frame.

protected:
	std::vector<std::unique_ptr<Widget>> m_children;
};

} // namespace lightGraphics::ui
