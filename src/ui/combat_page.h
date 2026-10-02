#pragma once

#include "core/character.h"
#include "core/encounter.h"
#include "core/sheet.h"

#include <QString>
#include <QWidget>

#include <random>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QShowEvent;
class QVBoxLayout;

namespace combat {
class CharacterStore;
class EncounterStore;
class MonsterCatalog;
}

namespace combat::ui {

// One fight: encounters on the left, turn order on the right. Initiative rolls
// and turn movement go through the Qt-free core. The dice generator lives here.
class CombatPage : public QWidget {
    Q_OBJECT

public:
    CombatPage(CharacterStore& characters, MonsterCatalog& catalog, EncounterStore& encounters,
               std::vector<Spell> spells, std::vector<Condition> conditions, QWidget* parent = nullptr);

    bool hasLoadError() const { return !m_loadError.isEmpty(); }
    QString loadError() const { return m_loadError; }

protected:
    void showEvent(QShowEvent* event) override;

private:
    void reloadCharacters();
    void refreshMonsterChoices();
    void addEncounter();
    void deleteEncounter();
    void showEncounter();
    void onEncounterNameEdited(const QString& text);
    void onEncounterNameEditingFinished();
    void rebuildCombatantList(const std::string& selectId);
    void showCombatant();
    void updateTurnLabels();
    void updateCombatantItemText(int row);
    QString combatantLabel(const Combatant& combatant, bool active) const;
    void onInitiativeChanged(int value);
    void onInitiativeEditingFinished();
    void onHpChanged(int value);
    void onTempHpChanged(int value);
    void applyActiveDamage();
    void applyActiveHealing();
    void addSelectedCondition();
    void removeListedCondition();
    void showConditionText();
    void refreshConcentrationChoices(const QString& text);
    void setSelectedConcentration();
    void clearSelectedConcentration();
    void adjustSelectedDeathSave(bool success, int delta);
    void spendSelectedSlot();
    void restSelectedCharacter();
    void rebuildSlotButtons();
    void updateDerivedModifiers();
    Character* characterFor(const Combatant& combatant);
    void saveCharacters();
    Combatant* activeCombatant();
    void rollAll();
    void rerollSelected();
    void moveSelected(int direction);
    void removeSelected();
    void addCharacter();
    void addSelectedMonster();
    void previousTurn();
    void nextTurn();
    void nextRound();
    void persist();
    int rollD20();
    Encounter* selectedEncounter();
    Combatant* selectedCombatant();

    CharacterStore& m_charactersStore;
    MonsterCatalog& m_catalog;
    EncounterStore& m_encountersStore;
    std::vector<Character> m_characters;
    std::vector<Encounter> m_encounters;
    std::vector<Spell> m_spells;
    std::vector<Condition> m_conditions;
    QString m_loadError;
    QString m_characterLoadError;
    std::mt19937 m_dice;
    std::mt19937_64 m_ids;
    bool m_populating = false;
    bool m_reportedLoadError = false;

    QListWidget* m_encounterList = nullptr;
    QPushButton* m_addEncounterButton = nullptr;
    QPushButton* m_deleteEncounterButton = nullptr;
    QWidget* m_fight = nullptr;
    QLabel* m_emptyHint = nullptr;
    QLineEdit* m_encounterName = nullptr;
    QLabel* m_nameError = nullptr;
    QLabel* m_roundLabel = nullptr;
    QLabel* m_activeLabel = nullptr;
    QPushButton* m_previousTurnButton = nullptr;
    QPushButton* m_nextTurnButton = nullptr;
    QPushButton* m_nextRoundButton = nullptr;
    QPushButton* m_rollAllButton = nullptr;
    QLabel* m_rollNote = nullptr;
    QListWidget* m_combatantList = nullptr;
    QWidget* m_combatantForm = nullptr;
    QLabel* m_noCombatantHint = nullptr;
    QLabel* m_combatantName = nullptr;
    QLabel* m_combatantSource = nullptr;
    QSpinBox* m_initiative = nullptr;
    QLabel* m_bonusLabel = nullptr;
    QPushButton* m_rerollButton = nullptr;
    QLabel* m_acLabel = nullptr;
    QSpinBox* m_hp = nullptr;
    QLabel* m_maxHpLabel = nullptr;
    QSpinBox* m_tempHp = nullptr;
    QSpinBox* m_damageAmount = nullptr;
    QPushButton* m_damageButton = nullptr;
    QSpinBox* m_healAmount = nullptr;
    QPushButton* m_healButton = nullptr;
    QLabel* m_healNote = nullptr;
    QComboBox* m_conditionPicker = nullptr;
    QListWidget* m_conditionList = nullptr;
    QLabel* m_conditionText = nullptr;
    QLabel* m_concentrationLabel = nullptr;
    QLineEdit* m_spellSearch = nullptr;
    QListWidget* m_spellMatches = nullptr;
    QLabel* m_deathSuccessLabel = nullptr;
    QLabel* m_deathFailureLabel = nullptr;
    QWidget* m_slotHost = nullptr;
    QVBoxLayout* m_slotLayout = nullptr;
    QPushButton* m_restButton = nullptr;
    QLabel* m_derivedLabel = nullptr;
    QPushButton* m_moveUpButton = nullptr;
    QPushButton* m_moveDownButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QComboBox* m_characterCombo = nullptr;
    QPushButton* m_addCharacterButton = nullptr;
    QLineEdit* m_monsterSearch = nullptr;
    QListWidget* m_monsterChoices = nullptr;
    QPushButton* m_addMonsterButton = nullptr;
};

}  // namespace combat::ui
