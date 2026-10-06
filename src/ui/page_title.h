#pragma once

#include "ui/theme.h"

#include <QLabel>
#include <QString>

namespace combat::ui {

inline QLabel* makePageTitle(const QString& text)
{
    return makeTitle(text);
}

}  // namespace combat::ui
