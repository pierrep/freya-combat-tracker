#pragma once

#include <QWidget>

namespace combat::ui {

class PlaceholderPage : public QWidget {
    Q_OBJECT

public:
    explicit PlaceholderPage(const QString& title, const QString& message = {}, QWidget* parent = nullptr);
};

}  // namespace combat::ui
