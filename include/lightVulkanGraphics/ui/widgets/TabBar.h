#pragma once

// docs/gui/05-widgets.md, "TabBar": groups children under named tabs, only ONE tab's
// children are updated/drawn/laid-out/hit-tested at a time -- the same "occupies no
// space and isn't interactive when not shown" contract CollapsingSection already
// established for its closed state (CollapsingSection.h), generalized from all-or-
// nothing to per-child, filtered by which tab a child was added into.

#include "CompositeWidget.h"

#include <string>
#include <utility>
#include <vector>

namespace lightGraphics::ui {

class TabBar : public CompositeWidget {
public:
	// A scoped add-target for one tab's content, obtained from addTab() -- mirrors
	// Panel::add<W>()/CompositeWidget::add<W>()'s own shape, so adding widgets into a
	// specific tab reads the same way as adding them anywhere else in this library:
	//     auto general = tabBar->addTab("General");
	//     general.add<Checkbox>("Enable X", true);
	// Cheap to copy (two members, owns nothing itself -- the TabBar owns every child,
	// exactly as CompositeWidget::add<W>() already does for any other composite).
	class Tab {
	public:
		template <typename W, typename... Args>
		W* add(Args&&... args) {
			return m_owner->addToTab<W>(m_index, std::forward<Args>(args)...);
		}

	private:
		friend class TabBar;
		Tab(TabBar* owner, int index) : m_owner(owner), m_index(index) {}
		TabBar* m_owner;
		int m_index;
	};

	// Appends a new, initially-empty tab and returns a handle to add content into it.
	// Tabs cannot be removed -- nothing in this library removes a single child from a
	// composite either (Panel::remove() is the closest analogue, and it operates on
	// whole top-level widgets); add every tab you need up front.
	Tab addTab(std::string title);

	void setActiveTab(int index);   // clamped to [0, tabCount() - 1]
	int activeTab() const { return m_activeTab; }
	int tabCount() const { return static_cast<int>(m_tabTitles.size()); }

	Vec2 preferredSize(const GuiContext&) const override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;
	void layout(const GuiContext&) override;
	Widget* hitTestDeep(Vec2 p) override;

	// Left/Right switch tabs while focused, wrapping at both ends -- same convention
	// DropDown's highlight and RadioGroup's selection already use elsewhere in this
	// library (docs/gui/05-widgets.md).
	bool acceptsFocus() const override { return true; }

private:
	friend class Tab;

	template <typename W, typename... Args>
	W* addToTab(int tabIndex, Args&&... args) {
		W* w = add<W>(std::forward<Args>(args)...);   // CompositeWidget::add
		m_childTabIndex.push_back(tabIndex);
		return w;
	}

	Rect headerRect(const GuiContext&) const;
	Rect tabButtonRect(const GuiContext&, int tabIndex) const;
	// Caller must already know mouseX falls within headerRect()'s x-span -- this only
	// resolves WHICH segment, via the same "compute a header row's own coordinate math
	// directly, rather than trying to make hit-testing return it" pattern DropDown's
	// itemAtY() uses for its popup rows.
	int tabAtX(const GuiContext&, float mouseX) const;
	void moveActiveTab(int dir);

	std::vector<std::string> m_tabTitles;
	// Parallel to CompositeWidget::m_children -- m_childTabIndex[i] is the tab m_children[i]
	// belongs to. A parallel array rather than a nested per-tab container so every
	// existing CompositeWidget traversal (hitTestDeep/findDescendant/collectFocusable)
	// keeps working over the flat m_children list unchanged; only THIS class's own
	// update()/draw()/layout()/hitTestDeep() overrides need to filter by it.
	std::vector<int> m_childTabIndex;
	int m_activeTab = 0;
};

} // namespace lightGraphics::ui
