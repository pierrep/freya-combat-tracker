#pragma once

#include <QFont>
#include <QLabel>
#include <QString>

namespace combat::ui {

inline QLabel* makePageTitle(const QString& text)
{
    auto* label = new QLabel(text);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 1.8);
    font.setBold(true);
    label->setFont(font);
    return label;
}

}  // namespace combat::ui
