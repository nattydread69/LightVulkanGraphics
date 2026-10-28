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

// docs/gui/05-widgets.md, "TreeView": a scrollable, collapsible hierarchy -- a scene
// graph, a skeleton's bones, a dataset's groups. ListBox's sibling for data that has a
// parent/child shape: each row is one node, indented under its parent, with a
// disclosure arrow on every node that has children.
//
// The tree is built with addNode(text, parent), which hands back an int id. Ids are
// stable until clear() -- removing a node never renumbers the others -- so an
// application can keep using them as keys, or carry its own index on each node with
// setNodeUserData().
//
// Interaction follows FileListView and native tree controls:
//   click             select (on press)
//   click the arrow   expand/collapse; Shift+click does the whole subtree
//   double-click      activate (setOnActivate); on a node with children it also toggles
//   Up/Down, PageUp/PageDown, Home/End   move the selection through the visible rows
//   Right             expand, or step into the first child if already expanded
//   Left              collapse, or step out to the parent if already collapsed
//   Enter             activate
//   typing            jump to the first visible row whose text starts with what was typed
// Collapsing a node that hides the selection moves the selection up to that node.

#include "../Theme.h"
#include "../Widget.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lightGraphics::ui {

class TreeView : public Widget {
public:
	// A non-empty label sits to the left of the tree, as ListBox's does.
	explicit TreeView(std::string label = {});

	// ---- building the tree ----
	// Adds a node as the last child of `parent` (-1: a new top-level node) and returns its
	// id: 0, 1, 2, ... in the order nodes are added. New nodes start collapsed. Returns -1
	// (and adds nothing) if `parent` is neither -1 nor a valid node.
	int addNode(std::string text, int parent = -1);
	// Removes `node` and its whole subtree. Their ids become invalid and are not reused.
	// If the selection was among them it is cleared, without firing the callback.
	void removeNode(int node);
	// Removes every node and restarts ids from 0.
	void clear();
	bool isValid(int node) const;
	int nodeCount() const { return m_liveCount; }

	void setNodeText(int node, std::string text);
	const std::string& nodeText(int node) const;
	// Dimmed secondary text drawn at the right-hand end of the row ("Mesh", "3 bones").
	void setNodeDetail(int node, std::string detail);
	const std::string& nodeDetail(int node) const;
	// An application value carried by the node -- an index into the application's own
	// data, say. Default 0.
	void setNodeUserData(int node, std::int64_t value);
	std::int64_t nodeUserData(int node) const;

	int parentOf(int node) const;   // -1 for a top-level node, or an invalid id
	// The children of `node`, in order; parent -1 gives the top-level nodes.
	const std::vector<int>& childrenOf(int node) const;
	int depthOf(int node) const;    // 0 for a top-level node

	// ---- expansion ----
	// A node without children can be marked expanded; it shows as expanded once it has
	// some. With `recursive`, every node in the subtree gets the same state.
	void setExpanded(int node, bool expanded, bool recursive = false);
	bool isExpanded(int node) const;
	void expandAll();
	void collapseAll();
	// Expands every ancestor of `node` so it is on screen, and scrolls it into view.
	void reveal(int node);

	// ---- selection ----
	int selectedNode() const { return m_selected; }   // -1 = nothing selected
	// node < 0 (or an invalid id) clears the selection; otherwise the node is revealed.
	void setSelectedNode(int node, bool fireCallback = false);

	// ---- callbacks (fired by user interaction, not by the setters above) ----
	// The selection changed; -1 = cleared.
	void setOnSelectionChange(std::function<void(int node)> fn) { m_onSelectionChange = std::move(fn); }
	// Double-click on a row, or Enter while the tree is focused and a node is selected.
	void setOnActivate(std::function<void(int node)> fn) { m_onActivate = std::move(fn); }
	// The user expanded or collapsed a node (once per node for a Shift+click subtree).
	void setOnToggle(std::function<void(int node, bool expanded)> fn) { m_onToggle = std::move(fn); }

	// ---- appearance ----
	// How many rows tall the tree is, regardless of how many are showing. Default 8.
	void setVisibleRows(int count) {
		m_visibleRows = count < 1 ? 1 : count;
		m_viewRows = m_visibleRows;   // until the next layout measures it
	}
	// Shown, dimmed, in the middle of an empty tree. Default: nothing.
	void setEmptyText(std::string text) { m_emptyText = std::move(text); }

	// ---- geometry and state, for overlays and tests (valid after a layout pass) ----
	// The nodes currently showing, top to bottom -- every node whose ancestors are all
	// expanded -- whether or not they are scrolled into view.
	const std::vector<int>& visibleNodes() const;
	int firstVisibleRow() const { return m_scrollRow; }
	// The node's row, or an empty rect when it is hidden or scrolled out of view.
	Rect rowRect(const GuiContext& ctx, int node) const;
	// The disclosure arrow's click area within that row; empty for a node without children.
	Rect toggleRect(const GuiContext& ctx, int node) const;

	Vec2 preferredSize(const GuiContext&) const override;
	void layout(const GuiContext&) override;
	void update(GuiContext&) override;
	void draw(DrawList&, const GuiContext&) const override;

	bool acceptsFocus() const override { return true; }
	// Consumes the wheel while hovered so the owning Panel doesn't also scroll underneath
	// it -- same convention as ListBox.
	bool wantsWheel() const override { return true; }

private:
	struct Node {
		std::string text;
		std::string detail;
		std::int64_t userData = 0;
		int parent = -1;
		int depth = 0;
		std::vector<int> children;
		bool expanded = false;
		bool alive = true;
	};
	struct Row {
		int node;
		int depth;
	};
	struct Layout {
		Rect list, scrollTrack;
		float scrollW = 0.0f;   // 0 when everything fits and there is no scrollbar
		float indent = 0.0f;    // horizontal step per depth level
		int visibleRows = 0;
	};
	enum class PressTarget { None, Row, Toggle, ScrollThumb, ScrollTrack };

	Node* nodeAt(int node);
	const Node* nodeAt(int node) const;
	const std::vector<Row>& rows() const;
	int rowOf(int node) const;   // index into rows(), or -1 when hidden
	void markRowsDirty() { m_rowsDirty = true; }

	Layout computeLayout(const GuiContext&) const;
	Rect rowRectIn(const Layout&, const Theme&, int row) const;
	Rect toggleRectIn(const Layout&, const Theme&, int row) const;
	Rect scrollThumbRect(const Layout&) const;
	int rowAt(const GuiContext&, const Layout&, Vec2 p) const;
	int maxScroll(int visibleRows) const;
	void clampScroll();
	void scrollToShow(int row);

	// User-driven changes: these fire the callbacks.
	void selectByUser(int node);
	void toggleByUser(int node, bool expanded, bool recursive);
	void moveSelection(int deltaRows);   // clamps at both ends -- no wrap-around
	void activateSelected();
	void handleTypeAhead(const std::vector<std::uint32_t>& chars);
	void collectSubtree(int node, std::vector<int>& out) const;

	std::vector<Node> m_nodes;
	std::vector<int> m_roots;
	int m_liveCount = 0;
	int m_selected = -1;

	// The flattened list of showing rows, rebuilt lazily after any change to the
	// structure or to what is expanded.
	mutable std::vector<Row> m_rows;
	mutable std::vector<int> m_rowNodes;   // m_rows' node ids, for visibleNodes()
	mutable bool m_rowsDirty = true;

	int m_scrollRow = 0;
	int m_visibleRows = 8;
	// Rows that actually fit at the last layout -- what scrolling clamps against, kept
	// because setters such as reveal() run without a ctx to recompute it from. Measured
	// in layout(), since a widget's first update() runs before it has any bounds.
	int m_viewRows = 8;
	float m_wheelAccum = 0.0f;
	std::string m_emptyText;

	PressTarget m_pressTarget = PressTarget::None;
	float m_dragStartMouseY = 0.0f;
	int m_dragStartScroll = 0;

	// Double-click and type-ahead timing, accumulated from InputState::deltaTime, as
	// FileListView does.
	float m_time = 0.0f;
	float m_lastClickTime = -1000.0f;
	int m_lastClickNode = -1;
	std::string m_typeAhead;
	float m_lastTypeTime = -1000.0f;

	std::function<void(int)> m_onSelectionChange;
	std::function<void(int)> m_onActivate;
	std::function<void(int, bool)> m_onToggle;
};

} // namespace lightGraphics::ui
