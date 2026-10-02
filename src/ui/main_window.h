#pragma once

#include <QMainWindow>

class QListWidget;
class QStackedWidget;

namespace combat {
class CharacterStore;
}

namespace combat::ui {

class CharactersPage;
class DashboardPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum Page { DashboardIndex = 0, CharactersIndex, MonstersIndex, CombatIndex };

    explicit MainWindow(CharacterStore& store, QWidget* parent = nullptr);

    void showPage(int index);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QListWidget* m_sidebar = nullptr;
    QStackedWidget* m_pages = nullptr;
    DashboardPage* m_dashboard = nullptr;
    CharactersPage* m_characters = nullptr;
    bool m_reportedLoadError = false;
};

}  // namespace combat::ui
