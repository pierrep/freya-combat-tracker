#pragma once

#include <QLayout>
#include <QList>

namespace combat::ui {

// Lays its items out left to right and wraps them onto new lines, like words
// in a paragraph. Used for condition tags.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int spacing = 6);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    void setGeometry(const QRect& rect) override;

private:
    int layOut(const QRect& rect, bool apply) const;

    QList<QLayoutItem*> m_items;
    int m_gap;
};

}  // namespace combat::ui
