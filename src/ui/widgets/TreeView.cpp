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

#include <lightVulkanGraphics/ui/widgets/TreeView.h>
#include <lightVulkanGraphics/ui/GuiContext.h>
#include "../Utf8.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace lightGraphics::ui {

namespace {
	constexpr float kDoubleClickSeconds = 0.4f;
	constexpr float kTypeAheadResetSeconds = 1.0f;
	// Horizontal step per depth level, as a multiple of the font size; the disclosure
	// arrow sits in the first step of each row.
	constexpr float kIndentRatio = 1.15f;
	// Detail text gives way to the node's own text: it is dropped from a row rather
	// than take more than this share of the room after the indent.
	constexpr float kMaxDetailShare = 0.45f;

	const std::string kEmptyString;
	const std::vector<int> kNoChildren;

	char asciiLower(char c) {
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	}

	bool startsWithIgnoreCase(std::string_view text, std::string_view prefix) {
		if (prefix.size() > text.size()) {
			return false;
		}
		for (std::size_t i = 0; i < prefix.size(); ++i) {
			if (asciiLower(text[i]) != asciiLower(prefix[i])) {
				return false;
			}
		}
		return true;
	}

	// Black or white, whichever reads better on `bg` -- the selected row is filled with
	// the theme's accent, which themes don't promise their text colour works on (see
	// FileListView).
	Color readableOn(Color bg) {
		float const luma = (0.299f * bg.r + 0.587f * bg.g + 0.114f * bg.b) / 255.0f;
		return luma > 0.6f ? Color{ 0x00, 0x00, 0x00, 0xFF } : Color{ 0xFF, 0xFF, 0xFF, 0xFF };
	}

	// Right-pointing when collapsed, down-pointing when expanded -- CollapsingSection's
	// header arrow, a little smaller.
	void drawDisclosureArrow(DrawList& dl, Vec2 centre, float size, bool expanded, Color color) {
		if (expanded) {
			dl.addTriangleFilled({ centre.x - size, centre.y - size * 0.6f }, { centre.x + size, centre.y - size * 0.6f },
			                     { centre.x, centre.y + size * 0.6f }, color);
		} else {
			dl.addTriangleFilled({ centre.x - size * 0.6f, centre.y - size }, { centre.x - size * 0.6f, centre.y + size },
			                     { centre.x + size * 0.6f, centre.y }, color);
		}
	}
}

TreeView::TreeView(std::string label) {
	setLabel(std::move(label));
}

// ---- nodes -------------------------------------------------------------------------

TreeView::Node* TreeView::nodeAt(int node) {
	if (node < 0 || node >= static_cast<int>(m_nodes.size())) {
		return nullptr;
	}
	Node& n = m_nodes[static_cast<std::size_t>(node)];
	return n.alive ? &n : nullptr;
}

const TreeView::Node* TreeView::nodeAt(int node) const {
	return const_cast<TreeView*>(this)->nodeAt(node);
}

bool TreeView::isValid(int node) const {
	return nodeAt(node) != nullptr;
}

int TreeView::addNode(std::string text, int parent) {
	int depth = 0;
	if (parent != -1) {
		const Node* p = nodeAt(parent);
		if (!p) {
			return -1;
		}
		depth = p->depth + 1;
	}
	int const id = static_cast<int>(m_nodes.size());
	Node n;
	n.text = std::move(text);
	n.parent = parent;
	n.depth = depth;
	m_nodes.push_back(std::move(n));
	if (parent == -1) {
		m_roots.push_back(id);
	} else {
		m_nodes[static_cast<std::size_t>(parent)].children.push_back(id);
	}
	++m_liveCount;
	markRowsDirty();
	return id;
}

void TreeView::collectSubtree(int node, std::vector<int>& out) const {
	if (!isValid(node)) {
		return;
	}
	std::size_t const first = out.size();
	out.push_back(node);
	// Breadth-first over `out` itself: no recursion, so a pathologically deep tree can't
	// overflow the stack.
	for (std::size_t i = first; i < out.size(); ++i) {
		// push_back rather than a range insert(): GCC 14 misreads the latter in Release
		// builds as overflowing (-Wstringop-overflow).
		for (int child : m_nodes[static_cast<std::size_t>(out[i])].children) {
			out.push_back(child);
		}
	}
}

void TreeView::removeNode(int node) {
	Node* n = nodeAt(node);
	if (!n) {
		return;
	}
	std::vector<int>& siblings = (n->parent == -1) ? m_roots : m_nodes[static_cast<std::size_t>(n->parent)].children;
	siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());

	std::vector<int> doomed;
	collectSubtree(node, doomed);
	for (int id : doomed) {
		Node& d = m_nodes[static_cast<std::size_t>(id)];
		d.alive = false;
		// Tombstones keep their slot so later ids don't shift, but nothing else.
		d.children.clear();
		d.children.shrink_to_fit();
		d.text.clear();
		d.text.shrink_to_fit();
		d.detail.clear();
		d.detail.shrink_to_fit();
		if (id == m_selected) {
			m_selected = -1;
		}
		if (id == m_lastClickNode) {
			m_lastClickNode = -1;
		}
	}
	m_liveCount -= static_cast<int>(doomed.size());
	markRowsDirty();
	clampScroll();
}

void TreeView::clear() {
	m_nodes.clear();
	m_roots.clear();
	m_liveCount = 0;
	m_selected = -1;
	m_lastClickNode = -1;
	m_scrollRow = 0;
	markRowsDirty();
}

void TreeView::setNodeText(int node, std::string text) {
	if (Node* n = nodeAt(node)) {
		n->text = std::move(text);
	}
}

const std::string& TreeView::nodeText(int node) const {
	const Node* n = nodeAt(node);
	return n ? n->text : kEmptyString;
}

void TreeView::setNodeDetail(int node, std::string detail) {
	if (Node* n = nodeAt(node)) {
		n->detail = std::move(detail);
	}
}

const std::string& TreeView::nodeDetail(int node) const {
	const Node* n = nodeAt(node);
	return n ? n->detail : kEmptyString;
}

void TreeView::setNodeUserData(int node, std::int64_t value) {
	if (Node* n = nodeAt(node)) {
		n->userData = value;
	}
}

std::int64_t TreeView::nodeUserData(int node) const {
	const Node* n = nodeAt(node);
	return n ? n->userData : 0;
}

int TreeView::parentOf(int node) const {
	const Node* n = nodeAt(node);
	return n ? n->parent : -1;
}

const std::vector<int>& TreeView::childrenOf(int node) const {
	if (node == -1) {
		return m_roots;
	}
	const Node* n = nodeAt(node);
	return n ? n->children : kNoChildren;
}

int TreeView::depthOf(int node) const {
	const Node* n = nodeAt(node);
	return n ? n->depth : 0;
}

// ---- expansion and selection -------------------------------------------------------

void TreeView::setExpanded(int node, bool expanded, bool recursive) {
	if (!isValid(node)) {
		return;
	}
	std::vector<int> targets;
	if (recursive) {
		collectSubtree(node, targets);
	} else {
		targets.push_back(node);
	}
	for (int id : targets) {
		m_nodes[static_cast<std::size_t>(id)].expanded = expanded;
	}
	markRowsDirty();
	clampScroll();
}

bool TreeView::isExpanded(int node) const {
	const Node* n = nodeAt(node);
	return n && n->expanded;
}

void TreeView::expandAll() {
	for (Node& n : m_nodes) {
		n.expanded = true;
	}
	markRowsDirty();
}

void TreeView::collapseAll() {
	for (Node& n : m_nodes) {
		n.expanded = false;
	}
	markRowsDirty();
	clampScroll();
}

void TreeView::reveal(int node) {
	if (!isValid(node)) {
		return;
	}
	for (int p = parentOf(node); p != -1; p = parentOf(p)) {
		m_nodes[static_cast<std::size_t>(p)].expanded = true;
	}
	markRowsDirty();
	scrollToShow(rowOf(node));
}

void TreeView::setSelectedNode(int node, bool fireCallback) {
	m_selected = isValid(node) ? node : -1;
	if (m_selected >= 0) {
		reveal(m_selected);
	}
	if (fireCallback && m_onSelectionChange) {
		m_onSelectionChange(m_selected);
	}
}

// ---- the flattened rows ------------------------------------------------------------

const std::vector<TreeView::Row>& TreeView::rows() const {
	if (m_rowsDirty) {
		m_rows.clear();
		m_rowNodes.clear();
		// Pre-order walk with an explicit stack (children pushed in reverse so they pop
		// in order) -- again no recursion, for arbitrarily deep trees.
		std::vector<int> stack(m_roots.rbegin(), m_roots.rend());
		while (!stack.empty()) {
			int const id = stack.back();
			stack.pop_back();
			const Node& n = m_nodes[static_cast<std::size_t>(id)];
			m_rows.push_back({ id, n.depth });
			m_rowNodes.push_back(id);
			if (n.expanded) {
				for (auto it = n.children.rbegin(); it != n.children.rend(); ++it) {
					stack.push_back(*it);
				}
			}
		}
		m_rowsDirty = false;
	}
	return m_rows;
}

const std::vector<int>& TreeView::visibleNodes() const {
	rows();
	return m_rowNodes;
}

int TreeView::rowOf(int node) const {
	if (node < 0) {
		return -1;
	}
	const std::vector<Row>& rs = rows();
	for (std::size_t i = 0; i < rs.size(); ++i) {
		if (rs[i].node == node) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// ---- geometry ----------------------------------------------------------------------

TreeView::Layout TreeView::computeLayout(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	Layout l;
	l.list = splitRow(ctx).control;
	l.visibleRows = std::max(1, static_cast<int>((l.list.h - 2.0f * th.framePadding) / th.rowHeight));
	l.indent = std::round(th.fontSize * kIndentRatio);
	bool const scrollable = static_cast<int>(rows().size()) > l.visibleRows;
	l.scrollW = scrollable ? th.scrollbarWidth : 0.0f;
	if (scrollable) {
		l.scrollTrack = { l.list.right() - th.scrollbarWidth - 1.0f, l.list.y + th.framePadding,
		                  th.scrollbarWidth, l.list.h - 2.0f * th.framePadding };
	}
	return l;
}

Rect TreeView::rowRectIn(const Layout& l, const Theme& th, int row) const {
	int const onScreen = row - m_scrollRow;
	if (row < 0 || row >= static_cast<int>(rows().size()) || onScreen < 0 || onScreen >= l.visibleRows) {
		return {};
	}
	return { l.list.x + th.framePadding * 0.5f,
	         l.list.y + th.framePadding + static_cast<float>(onScreen) * th.rowHeight,
	         l.list.w - l.scrollW - th.framePadding,
	         th.rowHeight };
}

Rect TreeView::toggleRectIn(const Layout& l, const Theme& th, int row) const {
	Rect const r = rowRectIn(l, th, row);
	if (r.empty()) {
		return {};
	}
	const Row& rw = rows()[static_cast<std::size_t>(row)];
	if (m_nodes[static_cast<std::size_t>(rw.node)].children.empty()) {
		return {};
	}
	float const x = r.x + th.framePadding * 0.5f + static_cast<float>(rw.depth) * l.indent;
	if (x + l.indent > r.right()) {
		return {};   // indented clean out of the row: nothing to click
	}
	return { x, r.y, l.indent, r.h };
}

Rect TreeView::rowRect(const GuiContext& ctx, int node) const {
	return rowRectIn(computeLayout(ctx), ctx.theme(), rowOf(node));
}

Rect TreeView::toggleRect(const GuiContext& ctx, int node) const {
	return toggleRectIn(computeLayout(ctx), ctx.theme(), rowOf(node));
}

Rect TreeView::scrollThumbRect(const Layout& l) const {
	if (l.scrollTrack.empty()) {
		return {};
	}
	float const total = static_cast<float>(rows().size());
	float const thumbH = std::min(l.scrollTrack.h,
		std::max(l.scrollTrack.h * (static_cast<float>(l.visibleRows) / total), 16.0f));
	int const maxRow = maxScroll(l.visibleRows);
	float const t = maxRow > 0 ? static_cast<float>(m_scrollRow) / static_cast<float>(maxRow) : 0.0f;
	return { l.scrollTrack.x, l.scrollTrack.y + t * (l.scrollTrack.h - thumbH), l.scrollTrack.w, thumbH };
}

int TreeView::rowAt(const GuiContext& ctx, const Layout& l, Vec2 p) const {
	const Theme& th = ctx.theme();
	if (!l.list.contains(p)) {
		return -1;
	}
	float const localY = p.y - l.list.y - th.framePadding;
	if (localY < 0.0f) {
		return -1;
	}
	int const onScreen = static_cast<int>(localY / th.rowHeight);
	if (onScreen >= l.visibleRows) {
		return -1;
	}
	int const row = m_scrollRow + onScreen;
	return row < static_cast<int>(rows().size()) ? row : -1;
}

int TreeView::maxScroll(int visibleRows) const {
	return std::max(0, static_cast<int>(rows().size()) - visibleRows);
}

void TreeView::clampScroll() {
	m_scrollRow = std::clamp(m_scrollRow, 0, maxScroll(m_viewRows));
}

void TreeView::scrollToShow(int row) {
	if (row < 0) {
		return;
	}
	if (row < m_scrollRow) {
		m_scrollRow = row;
	} else if (row >= m_scrollRow + m_viewRows) {
		m_scrollRow = row - m_viewRows + 1;
	}
	clampScroll();
}

Vec2 TreeView::preferredSize(const GuiContext& ctx) const {
	const Theme& th = ctx.theme();
	return { 0.0f, static_cast<float>(m_visibleRows) * th.rowHeight + 2.0f * th.framePadding };
}

void TreeView::layout(const GuiContext& ctx) {
	m_viewRows = computeLayout(ctx).visibleRows;
	clampScroll();
}

// ---- interaction -------------------------------------------------------------------

void TreeView::selectByUser(int node) {
	int const next = isValid(node) ? node : -1;
	if (next == m_selected) {
		return;
	}
	m_selected = next;
	scrollToShow(rowOf(m_selected));
	if (m_onSelectionChange) {
		m_onSelectionChange(m_selected);
	}
}

void TreeView::toggleByUser(int node, bool expanded, bool recursive) {
	if (!isValid(node)) {
		return;
	}
	std::vector<int> targets;
	if (recursive) {
		collectSubtree(node, targets);
	} else {
		targets.push_back(node);
	}
	std::vector<int> changed;
	for (int id : targets) {
		Node& n = m_nodes[static_cast<std::size_t>(id)];
		if (!n.children.empty() && n.expanded != expanded) {
			n.expanded = expanded;
			changed.push_back(id);
		}
	}
	if (changed.empty()) {
		return;
	}
	markRowsDirty();
	clampScroll();

	int const row = rowOf(node);
	if (expanded && row >= 0) {
		// Bring the newly shown rows into view, as native trees do -- but never at the
		// cost of scrolling the expanded node itself off the top.
		int last = row;
		int const depth = m_nodes[static_cast<std::size_t>(node)].depth;
		const std::vector<Row>& rs = rows();
		while (last + 1 < static_cast<int>(rs.size()) && rs[static_cast<std::size_t>(last + 1)].depth > depth) {
			++last;
		}
		scrollToShow(last);
		scrollToShow(row);
	}

	// The selection was somewhere inside what just collapsed: it can't stay on a row
	// that isn't showing, so it moves up to the node that hid it.
	bool selectionHidden = false;
	if (!expanded && m_selected != node) {
		for (int p = parentOf(m_selected); p != -1 && !selectionHidden; p = parentOf(p)) {
			selectionHidden = p == node;
		}
	}

	if (m_onToggle) {
		for (int id : changed) {
			m_onToggle(id, expanded);
		}
	}
	if (selectionHidden) {
		selectByUser(node);
	}
}

void TreeView::moveSelection(int deltaRows) {
	const std::vector<Row>& rs = rows();
	if (rs.empty()) {
		return;
	}
	int const last = static_cast<int>(rs.size()) - 1;
	// A selection hidden by a programmatic collapse moves relative to the nearest
	// ancestor that is still showing.
	int current = -1;
	for (int n = m_selected; n != -1 && current < 0; n = parentOf(n)) {
		current = rowOf(n);
	}
	int const next = current < 0 ? (deltaRows > 0 ? 0 : last) : std::clamp(current + deltaRows, 0, last);
	selectByUser(rs[static_cast<std::size_t>(next)].node);
}

void TreeView::activateSelected() {
	if (isValid(m_selected) && m_onActivate) {
		m_onActivate(m_selected);
	}
}

void TreeView::handleTypeAhead(const std::vector<std::uint32_t>& chars) {
	bool typed = false;
	for (std::uint32_t cp : chars) {
		if (cp < 0x20 || cp == 0x7F) {
			continue;
		}
		if (m_time - m_lastTypeTime > kTypeAheadResetSeconds) {
			m_typeAhead.clear();
		}
		m_lastTypeTime = m_time;
		utf8Encode(cp, m_typeAhead);
		typed = true;
	}
	if (!typed || m_typeAhead.empty()) {
		return;
	}
	for (const Row& r : rows()) {
		if (startsWithIgnoreCase(m_nodes[static_cast<std::size_t>(r.node)].text, m_typeAhead)) {
			selectByUser(r.node);
			return;
		}
	}
}

void TreeView::update(GuiContext& ctx) {
	const InputState& in = ctx.input();
	m_time += in.deltaTime;
	if (!m_enabled) {
		return;
	}
	const Theme& th = ctx.theme();

	Layout const l = computeLayout(ctx);
	clampScroll();

	const int leftIdx = static_cast<int>(MouseButton::Left);
	Vec2 const mouse = in.mousePos;
	bool const captured = ctx.activeId() == id();

	if (captured && in.mousePressed[leftIdx]) {
		m_pressTarget = PressTarget::None;
		Rect const thumb = scrollThumbRect(l);
		if (!l.scrollTrack.empty() && l.scrollTrack.contains(mouse)) {
			if (thumb.contains(mouse)) {
				m_pressTarget = PressTarget::ScrollThumb;
				m_dragStartMouseY = mouse.y;
				m_dragStartScroll = m_scrollRow;
			} else {
				m_pressTarget = PressTarget::ScrollTrack;
				m_scrollRow += (mouse.y < thumb.y ? -1 : 1) * l.visibleRows;
				clampScroll();
			}
		} else if (l.list.contains(mouse)) {
			int const row = rowAt(ctx, l, mouse);
			if (row < 0) {
				// Empty space below the last row: deselect, like FileListView.
				m_pressTarget = PressTarget::Row;
				selectByUser(-1);
				m_lastClickNode = -1;
			} else {
				int const node = rows()[static_cast<std::size_t>(row)].node;
				Rect const toggle = toggleRectIn(l, th, row);
				if (!toggle.empty() && toggle.contains(mouse)) {
					// The arrow toggles without selecting, as in native trees; Shift
					// does the whole subtree.
					m_pressTarget = PressTarget::Toggle;
					bool const subtree = (in.modsDown & Mod::Shift) != 0;
					toggleByUser(node, !isExpanded(node), subtree);
					m_lastClickNode = -1;
				} else {
					m_pressTarget = PressTarget::Row;
					bool const isDoubleClick = node == m_lastClickNode &&
						(m_time - m_lastClickTime) < kDoubleClickSeconds;
					selectByUser(node);
					if (isDoubleClick) {
						// Reset so a third quick click starts a new pair.
						m_lastClickNode = -1;
						m_lastClickTime = -1000.0f;
						if (isValid(node) && !childrenOf(node).empty()) {
							toggleByUser(node, !isExpanded(node), false);
						}
						activateSelected();
						return;   // the callbacks may have rebuilt the tree
					}
					m_lastClickNode = node;
					m_lastClickTime = m_time;
				}
			}
		}
	}

	if (captured && m_pressTarget == PressTarget::ScrollThumb && in.mouseDown[leftIdx]) {
		Rect const thumb = scrollThumbRect(l);
		float const travel = l.scrollTrack.h - thumb.h;
		if (travel > 0.0f) {
			float const rowsPerPixel = static_cast<float>(maxScroll(l.visibleRows)) / travel;
			m_scrollRow = m_dragStartScroll +
				static_cast<int>(std::lround((mouse.y - m_dragStartMouseY) * rowsPerPixel));
			clampScroll();
		}
	}

	if (captured && in.mouseReleased[leftIdx]) {
		m_pressTarget = PressTarget::None;
	}

	if (ctx.focusedId() == id()) {
		for (const KeyEvent& ev : in.keyQueue) {
			if (!ev.pressed) {
				continue;
			}
			switch (ev.key) {
			case Key::Up:       moveSelection(-1); break;
			case Key::Down:     moveSelection(1); break;
			case Key::PageUp:   moveSelection(-l.visibleRows); break;
			case Key::PageDown: moveSelection(l.visibleRows); break;
			case Key::Home:
				if (!rows().empty()) {
					selectByUser(rows().front().node);
				}
				break;
			case Key::End:
				if (!rows().empty()) {
					selectByUser(rows().back().node);
				}
				break;
			case Key::Left:
				if (const Node* n = nodeAt(m_selected)) {
					if (n->expanded && !n->children.empty()) {
						toggleByUser(m_selected, false, false);
					} else if (n->parent != -1) {
						selectByUser(n->parent);
					}
				}
				break;
			case Key::Right:
				if (const Node* n = nodeAt(m_selected)) {
					if (!n->children.empty()) {
						if (!n->expanded) {
							toggleByUser(m_selected, true, false);
						} else {
							selectByUser(n->children.front());
						}
					}
				}
				break;
			case Key::Enter:
				if (!ev.repeat) {
					activateSelected();
					return;   // the callback may have rebuilt the tree
				}
				break;
			default:
				break;
			}
		}
		handleTypeAhead(in.charQueue);
	}

	if (ctx.hoveredId() == id() && in.wheelDelta != 0.0f) {
		// Accumulated, so a trackpad's many small deltas still add up to a scroll.
		m_wheelAccum += in.wheelDelta * 3.0f;
		int const steps = static_cast<int>(m_wheelAccum);
		m_wheelAccum -= static_cast<float>(steps);
		m_scrollRow -= steps;
		clampScroll();
	}
}

// ---- drawing -----------------------------------------------------------------------

void TreeView::draw(DrawList& dl, const GuiContext& ctx) const {
	if (!m_visible) {
		return;
	}
	const Theme& th = ctx.theme();
	const Font& font = ctx.font();
	RowSplit const split = splitRow(ctx);
	Layout const l = computeLayout(ctx);
	bool const isEnabled = effectivelyEnabled();
	bool const focused = ctx.focusedId() == id();
	Vec2 const mouse = ctx.input().mousePos;
	bool const hoveredHere = ctx.hoveredId() == id() && ctx.activeId() == kInvalidWidgetId;

	if (!m_label.empty()) {
		Color const labelColor = isEnabled ? th.text : th.textDisabled;
		dl.addTextClipped(font, th.fontSize, split.label, labelColor, m_label, Align::Start, Align::Center);
	}

	dl.addRectFilled(l.list, th.frameBg, th.rounding);

	const std::vector<Row>& rs = rows();
	int const total = static_cast<int>(rs.size());
	int const hoveredRow = hoveredHere ? rowAt(ctx, l, mouse) : -1;
	Color const textColor = isEnabled ? th.text : th.textDisabled;
	Color const dimText = isEnabled ? Color::lerp(th.text, th.textDisabled, 0.45f) : th.textDisabled;
	Color const guideColor = Color::lerp(th.frameBg, th.textDisabled, 0.4f);
	Color const selectedBg = isEnabled ? th.accentActive : th.frameBgActive;
	float const arrowSize = std::round(th.fontSize * 0.3f);
	float const gap = th.framePadding * 2.0f;

	// Deep rows can indent past the right edge; keep everything inside the list.
	Rect const content{ l.list.x, l.list.y, l.list.w - l.scrollW, l.list.h };
	dl.pushClipRect(content, true);
	for (int onScreen = 0; onScreen < l.visibleRows; ++onScreen) {
		int const row = m_scrollRow + onScreen;
		if (row >= total) {
			break;
		}
		const Row& rw = rs[static_cast<std::size_t>(row)];
		const Node& n = m_nodes[static_cast<std::size_t>(rw.node)];
		Rect const r = rowRectIn(l, th, row);

		bool const selected = rw.node == m_selected;
		if (selected) {
			dl.addRectFilled(r, selectedBg, th.rounding);
		} else if (row == hoveredRow) {
			dl.addRectFilled(r, th.frameBgHovered, th.rounding);
		}

		float const baseX = r.x + th.framePadding * 0.5f;
		// Indent guides: one line per ancestor level, through the middle of that
		// ancestor's arrow column. Rows are contiguous, so each subtree gets one
		// unbroken line down its left side.
		if (!selected) {
			for (int level = 0; level < rw.depth; ++level) {
				float const x = std::round(baseX + (static_cast<float>(level) + 0.5f) * l.indent) + 0.5f;
				dl.addLine({ x, r.y }, { x, r.bottom() }, guideColor);
			}
		}

		Color const rowText = (selected && isEnabled) ? readableOn(selectedBg) : textColor;
		float const arrowX = baseX + static_cast<float>(rw.depth) * l.indent;
		if (!n.children.empty()) {
			Vec2 const centre{ std::round(arrowX + l.indent * 0.5f), std::round(r.y + r.h * 0.5f) };
			drawDisclosureArrow(dl, centre, arrowSize, n.expanded, rowText);
		}

		float const textX = arrowX + l.indent + th.framePadding * 0.25f;
		float const room = r.right() - th.framePadding - textX;
		if (room <= 0.0f) {
			continue;
		}
		float detailW = 0.0f;
		if (!n.detail.empty()) {
			detailW = font.measureText(n.detail, th.fontSize).x;
			if (detailW + gap > room * kMaxDetailShare) {
				detailW = 0.0f;
			}
		}
		float const textW = detailW > 0.0f ? room - detailW - gap : room;
		dl.addTextClipped(font, th.fontSize, { textX, r.y, textW, r.h }, rowText, n.text, Align::Start, Align::Center);
		if (detailW > 0.0f) {
			Color const detailColor = selected ? rowText : dimText;
			dl.addTextClipped(font, th.fontSize, { r.right() - th.framePadding - detailW, r.y, detailW, r.h },
			                  detailColor, n.detail, Align::End, Align::Center);
		}
	}
	dl.popClipRect();

	if (total == 0 && !m_emptyText.empty()) {
		dl.addTextClipped(font, th.fontSize, l.list, th.textDisabled, m_emptyText, Align::Center, Align::Center);
	}

	if (!l.scrollTrack.empty()) {
		dl.addRectFilled(l.scrollTrack, th.scrollbarBg, th.rounding);
		bool const dragging = ctx.activeId() == id() && m_pressTarget == PressTarget::ScrollThumb;
		Rect const thumb = scrollThumbRect(l);
		bool const thumbHovered = hoveredHere && thumb.contains(mouse);
		Color const thumbColor = dragging ? th.accentActive : (thumbHovered ? th.frameBgActive : th.scrollbarGrab);
		dl.addRectFilled(thumb, thumbColor, th.rounding);
	}

	Color const borderColor = (isEnabled && focused) ? th.accent : th.border;
	dl.addRect(l.list, borderColor, 1.0f, th.rounding);
}

} // namespace lightGraphics::ui
