#pragma once

#include "core/character.h"
#include "core/encounter.h"
#include "core/sheet.h"

#include <QString>
#include <QWidget>

#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QSpinBox;
class QShowEvent;
class QVBoxLayout;

namespace combat {
class CharacterStore;
class EncounterStore;
class MonsterCatalog;
}

namespace combat::ui {

// The fight for one encounter chosen from the dropdown. Initiative, turn
// order, damage, and the rest of the bookkeeping go through the Qt-free core.
class CombatPage : public QWidget {
    Q_OBJECT

public:
    CombatPage(CharacterStore& characters, MonsterCatalog& catalog, EncounterStore& encounters,
               std::vector<Spell> spells, std::vector<Condition> conditions, QWidget* parent = nullptr);

    bool hasLoadError() const { return !m_loadError.isEmpty(); }
    QString loadError() const { return m_loadError; }
    ~CombatPage() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    enum class FightEdit { None, Once, Initiative, HitPoints, TemporaryHp };

    void reloadEncounters();
    void reloadCharacters();
    void showEncounter();
    void rebuildCombatantList(const std::string& selectId);
    void showCombatant();
    void updateTurnLabels();
    void refreshListedHitPoints(const Combatant& combatant);
    void onInitiativeChanged(int value);
    void onInitiativeEditingFinished();
    void onHpChanged(int value);
    void onTempHpChanged(int value);
    void applySelectedDamage();
    void applySelectedHealing();
    void applyArmedDamage(QTreeWidgetItem* item, int column);
    void useSelectedAbility();
    void armAttack(const std::string& effect, int total);
    void disarmAttack();
    void rememberArmedSelection(QTreeWidgetItem* previous, QTreeWidget* otherList);
    void showAttacks();
    void clearAttackRows();
    void setCharacterSheetControlsVisible(bool visible);
    void clampAndCarryHitPoints(Combatant& combatant);
    void addSelectedCondition();
    void removeListedCondition();
    void showConditionText();
    void refreshConcentrationChoices(const QString& text);
    void setSelectedConcentration();
    void clearSelectedConcentration();
    void adjustSelectedDeathSave(bool success, int delta);
    void spendSelectedSlot();
    void rebuildSlotButtons();
    void updateDerivedModifiers();
    Character* characterFor(const Combatant& combatant);
    void saveCharacters();
    void rollAll();
    void rerollSelected();
    void removeSelected();
    void previousTurn();
    void nextTurn();
    void undoLastChange();
    void syncAttackCount();
    void setHitText(const QString& text);
    int attacksUsedFor(const std::string& id) const;
    int allotmentFor(const Combatant& combatant) const;
    std::string currentTurnId();
    bool beginUndo(FightEdit edit, const Combatant* sheetCombatant, FightUndo& snapshot);
    void keepUndo(FightEdit edit, FightUndo snapshot);
    bool fightChanged(const FightUndo& snapshot, const Combatant* sheetCombatant);
    void closeFightEdit(FightEdit edit);
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
    std::mt19937 m_dice;
    struct PageUndo {
        FightUndo fight;
        std::map<std::string, int> attacksUsedById;
        std::string countedCombatantId;
        int countedRound = 0;
        QString hitText;
        // Set for an attack or Use ability. Undo selects this combatant again.
        std::optional<std::string> selectedCombatantId;
    };

    bool m_populating = false;
    bool m_undoing = false;
    bool m_suppressUndo = false;
    bool m_reportedLoadError = false;
    bool m_swordCursor = false;
    FightEdit m_openEdit = FightEdit::None;
    std::map<std::string, int> m_attacksUsedById;
    std::map<std::string, int> m_capturedAttacksUsedById;
    int m_countedRound = 0;
    int m_capturedCountedRound = 0;
    std::optional<std::string> m_capturedSelectionId;
    std::string m_selectionBeforeAttack;
    bool m_hasSelectionBeforeAttack = false;
    std::optional<PageUndo> m_undo;
    std::optional<int> m_armedDamage;
    std::string m_armedEffect;
    std::string m_armedAttackerId;
    std::string m_undoEncounterId;
    std::string m_countedCombatantId;
    std::string m_capturedCountedCombatantId;
    QString m_hitText;
    QString m_capturedHitText;

    QComboBox* m_encounterCombo = nullptr;
    QWidget* m_fight = nullptr;
    QLabel* m_emptyHint = nullptr;
    QLabel* m_roundLabel = nullptr;
    QLabel* m_activeLabel = nullptr;
    QLabel* m_hitLabel = nullptr;
    QPushButton* m_previousTurnButton = nullptr;
    QPushButton* m_nextTurnButton = nullptr;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_rollAllButton = nullptr;
    QLabel* m_rollNote = nullptr;
    QTreeWidget* m_initiativeList = nullptr;
    QTreeWidget* m_zeroHpList = nullptr;
    QWidget* m_combatantForm = nullptr;
    QFormLayout* m_combatantFormLayout = nullptr;
    QWidget* m_deathSavesHost = nullptr;
    QLabel* m_noCombatantHint = nullptr;
    QWidget* m_attacksSection = nullptr;
    QVBoxLayout* m_beforeAttackRows = nullptr;
    QVBoxLayout* m_attackRows = nullptr;
    QVBoxLayout* m_afterAttackRows = nullptr;
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
    QLabel* m_derivedLabel = nullptr;
    QPushButton* m_removeButton = nullptr;
};

}  // namespace combat::ui
