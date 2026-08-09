#include <lightVulkanGraphics/ui/widgets/Vec3Field.h>
#include <lightVulkanGraphics/ui/widgets/DragValue.h>
#include <lightVulkanGraphics/ui/widgets/TextBox.h>
#include <lightVulkanGraphics/ui/GuiContext.h>

#include <glm/glm.hpp>

namespace lightGraphics::ui {

Vec3Field::Vec3Field(std::string label, glm::vec3 initial) {
	setLabel(std::move(label));

	// Create three DragValue children for X, Y, Z
	auto* x = add<DragValueT<float>>("X", initial.x);
	auto* y = add<DragValueT<float>>("Y", initial.y);
	auto* z = add<DragValueT<float>>("Z", initial.z);

	// Set callbacks on each child to fire onChangewhen any component changes
	x->setOnChange([this](float) { if (m_onChange) m_onChange(value()); });
	y->setOnChange([this](float) { if (m_onChange) m_onChange(value()); });
	z->setOnChange([this](float) { if (m_onChange) m_onChange(value()); });
}

glm::vec3 Vec3Field::value() const {
	if (childCount() != 3) {
		return glm::vec3(0.0f);
	}
	auto* x = dynamic_cast<DragValueT<float>*>(childAt(0));
	auto* y = dynamic_cast<DragValueT<float>*>(childAt(1));
	auto* z = dynamic_cast<DragValueT<float>*>(childAt(2));
	return { x ? x->value() : 0.0f, y ? y->value() : 0.0f, z ? z->value() : 0.0f };
}

void Vec3Field::setValue(glm::vec3 v, bool fireCallback) {
	if (childCount() != 3) {
		return;
	}
	auto* x = dynamic_cast<DragValueT<float>*>(childAt(0));
	auto* y = dynamic_cast<DragValueT<float>*>(childAt(1));
	auto* z = dynamic_cast<DragValueT<float>*>(childAt(2));

	if (x) x->setValue(v.x, fireCallback);
	if (y) y->setValue(v.y, fireCallback);
	if (z) z->setValue(v.z, fireCallback);
}

Vec2 Vec3Field::preferredSize(const GuiContext& ctx) const {
	// Same as other widgets with a label
	return { 0.0f, ctx.theme().rowHeight };
}

void Vec3Field::layout(const GuiContext& ctx) {
	if (childCount() != 3) {
		return;
	}

	const Theme& th = ctx.theme();

	// Get the control rect for this widget
	RowSplit split = splitRow(ctx);
	const Rect& controlRect = split.control;

	// Split control into thirds with itemSpacing between
	float spacing = th.itemSpacing * 2.0f;  // 2 gaps for 3 fields
	float fieldWidth = (controlRect.w - spacing) / 3.0f;

	// Layout each child as a label + control within its field
	// Each child will call splitRow() internally to split its bounds
	float x = controlRect.x;
	for (std::size_t i = 0; i < 3; ++i) {
		Widget* child = childAt(i);
		child->setBounds({ x, controlRect.y, fieldWidth, controlRect.h });
		child->layout(ctx);
		x += fieldWidth + th.itemSpacing;
	}
}

} // namespace lightGraphics::ui
