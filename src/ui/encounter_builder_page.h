#pragma once

#include "core/campaign.h"
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
class QSpinBox;

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
    // Remembers the encounter picked here in this file, and opens it next time.
    void setStateFile(const QString& path);
    // The parties and adventures (kept by the caller): adventures are made and
    // chosen here, and an encounter can hold a party.
    void setCampaign(CampaignStore* campaign);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void reloadFromDisk();
    void reloadCharacters();
    void refreshMonsterChoices();
    void addEncounter();
    void deleteEncounter();
    void resetEncounter();
    void showEncounter();
    void onEncounterNameEdited(const QString& text);
    void onEncounterNameEditingFinished();
    void rebuildRoster();
    void addCharacter();
    void addParty();
    void removeParty();
    // The adventures: the choice above the list, its name and default party.
    void fillAdventureChoices();
    void fillEncounterList(const QString& selectedId);
    void onAdventureFilterChanged();
    void addAdventure();
    void deleteAdventure();
    void onAdventureNameEdited(const QString& text);
    void onAdventurePartyChosen();
    void onEncounterAdventureChosen();
    void fillPartyChoices();
    void saveCampaign();
    Adventure* chosenAdventure();
    std::string newId();
    void addSelectedMonster();
    void removeSelectedCombatant();
    void updateDifficulty();
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
    QPushButton* m_resetEncounterButton = nullptr;
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
    QSpinBox* m_monsterQuantity = nullptr;
    QPushButton* m_removeCombatantButton = nullptr;
    QLabel* m_difficulty = nullptr;
    QLabel* m_difficultyPill = nullptr;
    QString m_stateFile;

    CampaignStore* m_campaignStore = nullptr;
    Campaign m_campaign;
    QComboBox* m_adventureFilter = nullptr;
    QLineEdit* m_adventureName = nullptr;
    QComboBox* m_adventureParty = nullptr;
    QWidget* m_adventureDetails = nullptr;
    QPushButton* m_newAdventureButton = nullptr;
    QPushButton* m_deleteAdventureButton = nullptr;
    QComboBox* m_encounterAdventure = nullptr;
    QComboBox* m_partyCombo = nullptr;
    QPushButton* m_addPartyButton = nullptr;
    QPushButton* m_removePartyButton = nullptr;
    QLabel* m_partyNote = nullptr;
};

}  // namespace combat::ui
