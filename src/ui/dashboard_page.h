#pragma once

#include <QWidget>

class QLabel;

namespace combat::ui {

class DashboardPage : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPage(QWidget* parent = nullptr);

public slots:
    void setCharacterCount(int count);
    void setLoadError();

signals:
    void openPageRequested(int index);

private:
    QLabel* m_count = nullptr;
};

}  // namespace combat::ui
