#pragma once

#include "core/sheet.h"

#include <QMainWindow>
#include <QString>

#include <vector>

class QListWidget;
class QStackedWidget;

namespace combat {
class CharacterStore;
class CustomMonsterStore;
class EncounterStore;
class MergedMonsterCatalog;
}

namespace combat::ui {

class CharactersPage;
class DashboardPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum Page { DashboardIndex = 0, CharactersIndex, MonstersIndex, CombatIndex };

    MainWindow(CharacterStore& store, MergedMonsterCatalog& catalog, CustomMonsterStore& customStore,
               EncounterStore& encounters, const std::vector<Spell>& spells, const std::vector<Condition>& conditions,
               const std::vector<std::string>& species, const QString& attribution, const QString& catalogError,
               const QString& sheetCatalogError, QWidget* parent = nullptr);

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
