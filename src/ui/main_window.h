#pragma once

#include "core/sheet.h"

#include <QMainWindow>
#include <QString>

#include <vector>

class QToolButton;
class QStackedWidget;

namespace combat {
class CharacterStore;
class CustomMonsterStore;
class EncounterStore;
class MergedMonsterCatalog;
}

namespace combat::ui {

class CharactersPage;
class CombatPage;
class EncounterBuilderPage;
class MonstersPage;
class OptionsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum Page { DashboardIndex = 0, CharactersIndex, MonstersIndex, EncounterBuilderIndex, OptionsIndex };

    MainWindow(CharacterStore& store, MergedMonsterCatalog& catalog, CustomMonsterStore& customStore,
               EncounterStore& encounters, const std::vector<Spell>& spells, const std::vector<Condition>& conditions,
               const std::vector<std::string>& species, const QString& attribution, const QString& catalogError,
               const QString& sheetCatalogError, QWidget* parent = nullptr);

    // Switches page, as choosing it from the page menu does.
    void showPage(Page page);
    Page currentPage() const;

    // Keeps the Options page's choices in this INI file.
    void setOptionsFile(const QString& path);
    // Switches the whole app to the dark colours or back.
    void setDarkTheme(bool on);
    // The Options page's theme: "system" follows the computer's light or dark
    // setting (and its changes), "light" and "dark" stay put.
    void setThemeChoice(const QString& choice);
    // Where the Dashboard keeps its undo history and log between runs.
    void setHistoryFile(const QString& path);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    QToolButton* m_navigator = nullptr;
    QStackedWidget* m_pages = nullptr;
    void flushPendingSaves();
    void placeNavigator();

    CharactersPage* m_characters = nullptr;
    CombatPage* m_combat = nullptr;
    EncounterBuilderPage* m_builder = nullptr;
    MonstersPage* m_monsters = nullptr;
    OptionsPage* m_options = nullptr;
    QString m_themeChoice;
    bool m_reportedLoadError = false;
};

}  // namespace combat::ui
