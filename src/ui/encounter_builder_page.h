#pragma once

#include "core/character.h"
#include "core/encounter.h"

#include <QString>
#include <QWidget>

#include <random>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QShowEvent;

namespace combat {
class CharacterStore;
class EncounterStore;
class MonsterCatalog;
}

namespace combat::ui {

// Builds an encounter: create, rename, and delete, and add characters and
// monsters that already exist. The fight itself runs on the Dashboard.
class EncounterBuilderPage : public QWidget {
    Q_OBJECT

public:
    EncounterBuilderPage(CharacterStore& characters, MonsterCatalog& catalog, EncounterStore& encounters,
                         QWidget* parent = nullptr);

    bool hasLoadError() const { return !m_loadError.isEmpty(); }
    QString loadError() const { return m_loadError; }

protected:
    void showEvent(QShowEvent* event) override;

private:
    void reloadFromDisk();
    void reloadCharacters();
    void refreshMonsterChoices();
    void addEncounter();
    void deleteEncounter();
    void showEncounter();
    void onEncounterNameEdited(const QString& text);
    void onEncounterNameEditingFinished();
    void rebuildRoster();
    void addCharacter();
    void addSelectedMonster();
    void persist();
    Encounter* selectedEncounter();

    CharacterStore& m_charactersStore;
    MonsterCatalog& m_catalog;
    EncounterStore& m_encountersStore;
    std::vector<Character> m_characters;
    std::vector<Encounter> m_encounters;
    QString m_loadError;
    QString m_characterLoadError;
    std::mt19937_64 m_ids;
    bool m_populating = false;
    bool m_reportedLoadError = false;

    QListWidget* m_encounterList = nullptr;
    QPushButton* m_addEncounterButton = nullptr;
    QPushButton* m_deleteEncounterButton = nullptr;
    QWidget* m_editor = nullptr;
    QLabel* m_emptyHint = nullptr;
    QLineEdit* m_encounterName = nullptr;
    QLabel* m_nameError = nullptr;
    QListWidget* m_roster = nullptr;
    QLabel* m_rosterHint = nullptr;
    QComboBox* m_characterCombo = nullptr;
    QPushButton* m_addCharacterButton = nullptr;
    QLineEdit* m_monsterSearch = nullptr;
    QListWidget* m_monsterChoices = nullptr;
    QPushButton* m_addMonsterButton = nullptr;
};

}  // namespace combat::ui
