#include "ui/flow_layout.h"

#include <QWidget>

#include <algorithm>

namespace combat::ui {

FlowLayout::FlowLayout(QWidget* parent, int spacing) : QLayout(parent), m_gap(spacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0)) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return static_cast<int>(m_items.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return index >= 0 && index < m_items.size() ? m_items.at(index) : nullptr;
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return layOut(QRect(0, 0, width, 0), false);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items) {
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    layOut(rect, true);
}

int FlowLayout::layOut(const QRect& rect, bool apply) const
{
    const QMargins margins = contentsMargins();
    const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
    int x = area.x();
    int y = area.y();
    int lineHeight = 0;
    for (QLayoutItem* item : m_items) {
        if (item->widget() != nullptr && item->widget()->isHidden()) {
            continue;
        }
        const QSize size = item->sizeHint();
        if (x > area.x() && x + size.width() > area.right() + 1) {
            x = area.x();
            y += lineHeight + m_gap;
            lineHeight = 0;
        }
        if (apply) {
            item->setGeometry(QRect(QPoint(x, y), QSize(std::min(size.width(), area.width()), size.height())));
        }
        x += size.width() + m_gap;
        lineHeight = std::max(lineHeight, size.height());
    }
    return y + lineHeight - rect.y() + margins.bottom();
}

}  // namespace combat::ui
