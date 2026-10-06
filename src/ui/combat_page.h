#pragma once

#include "core/character.h"
#include "core/combat_rules.h"
#include "core/encounter.h"
#include "core/sheet.h"

#include <QString>
#include <QStringList>
#include <QWidget>

#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QFrame;
class QGridLayout;
class QHideEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QShowEvent;
class QSpinBox;
class QTabWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace combat {
class CharacterStore;
class EncounterStore;
class MonsterCatalog;
}

namespace combat::ui {

// The fight for one encounter chosen from the dropdown. Every rule (attack
// rolls, saves, damage, death saves, the action economy, durations) goes
// through the Qt-free core; this page shows the result and asks the GM when a
// roll needs a decision.
class CombatPage : public QWidget {
    Q_OBJECT

public:
    // A check the GM resolves: a concentration save after damage, a save to
    // end a condition, or a dying character's death save.
    struct Prompt {
        // Rider: a yes-or-no question before an action's conditions apply
        // ("did the boar move 20+ feet straight toward Aria?").
        // Hide: a monster took the Hide action; its Stealth check (auraName
        // holds the feature that granted it).
        enum class Kind { Concentration, SaveToEnd, DeathSave, Aura, Rider, Hide };
        Kind kind = Kind::Concentration;
        std::string combatantId;
        std::string conditionId;
        // An aura: the monster and its trait. A rider: the attacker.
        std::string sourceId;
        std::string auraName;
        Ability ability = Ability::Constitution;
        int dc = 10;
        // A rider: the action, which of its riders the answer applies, and the
        // damage to give back on a yes (the rug grapples instead).
        std::optional<MonsterAttack> attack;
        std::vector<std::size_t> riders;
        int refund = 0;
    };

    CombatPage(CharacterStore& characters, MonsterCatalog& catalog, EncounterStore& encounters,
               std::vector<Spell> spells, std::vector<Condition> conditions, QWidget* parent = nullptr);
    ~CombatPage() override;

    bool hasLoadError() const { return !m_loadError.isEmpty(); }
    QString loadError() const { return m_loadError; }

    // Options page choices.
    void setGroupInitiative(bool on) { m_groupInitiative = on; }
    void setAutoPass(bool on) { m_autoPass = on; }
    // Remembers the encounter shown here in this file, and opens it next time.
    void setStateFile(const QString& path);
    // Keeps the undo history and the log in this file between runs: written
    // when the Dashboard is left or the app quits, read when it first opens.
    void setHistoryFile(const QString& path);
    void saveHistory();

    // Writes an edit that is still waiting for typing to pause.
    void flushPendingSave();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // Typed edits to one field merge into one undo step until another kind of
    // change happens.
    enum class EditKind { Once, Initiative, HitPoints, TemporaryHp, Exhaustion };


    struct PageUndo {
        std::string encounterId;
        Encounter encounter;
        std::vector<Character> roster;
        QStringList log;
        std::vector<Prompt> prompts;
        std::string selectionId;
    };

    // An action waiting for its target clicks.
    struct ArmedAction {
        std::string attackerId;
        MonsterAttack attack;
        std::vector<TypedDamage> saveDamage;  // rolled once for a save effect
        std::vector<std::string> targets;     // already resolved
        bool spent = false;
        // A character's attack: damage typed in, no roll.
        std::optional<std::vector<TypedDamage>> fixedDamage;
        // A character's healing (Cure Wounds): the amount typed in, no roll.
        std::optional<int> fixedHealing;
        QStringList logBefore;  // the log before arming, so undoing the first target removes the arming lines too
        // A bonus action, reaction, or legendary action aimed like an action.
        std::optional<FeatureKind> featureKind;
        MonsterFeature feature;
    };

    void reloadEncounters();
    void reloadCharacters();
    bool syncSnapshots();
    void showEncounter();
    void rebuildCombatantList(const std::string& selectId);
    void showCombatant();
    void updateTurnLabels();
    void rebuildPrompts();
    void rebuildActions(const Combatant& combatant);
    void addCharacterAttackRow(const Combatant& combatant);
    void onCharacterAttackClicked(const std::string& attackerId);
    void onCharacterHealClicked(const std::string& healerId);
    void rebuildConditionList(const Combatant& combatant);
    void rebuildDetails(const Combatant& combatant);
    void updateDeathSaveRow(const Combatant& combatant);

    void onInitiativeChanged(int value);
    void onInitiativeEditingFinished();
    // The initiative phase's list on the right: every character, with a box
    // for the total each player rolls.
    void refreshInitiativeEntry();
    void onEntryInitiativeChanged(const std::string& combatantId, int value);
    void onEntryEditingFinished();
    void showInitiativeEntry();
    // Characters still at initiative 0 in the initiative phase.
    QStringList charactersWithoutInitiative();
    // In the initiative phase, monsters there are and none has rolled.
    bool monstersWithoutInitiative();
    void rollPlayers();
    void fitTurnOrder();
    void onStandardActionClicked(const std::string& combatantId, FeatureKind kind, const MonsterFeature& feature,
                                 const std::vector<std::string>& actions);
    void restoreHistory();
    void onHpChanged(int value);
    void onTempHpChanged(int value);
    void onExhaustionChanged(int value);
    void onEconomyToggled();
    void applySelectedDamage();
    // The creatures Damage and Heal act on: every selected row, or the one shown.
    std::vector<std::string> hitTargets();
    void openHitPanel(bool healing);
    void refreshHitPanel();
    void applySelectedHealing();
    void addSelectedCondition();
    void removeListedCondition(const QString& conditionId);
    void showConditionText();
    void refreshConcentrationChoices(const QString& text);
    void setSelectedConcentration();
    void clearSelectedConcentration();
    void adjustSelectedDeathSave(bool success, int delta);
    void stabilizeSelected();
    void spendSelectedSlot();
    void rollAll();
    void rerollSelected();
    void removeSelected();
    void nextTurn();
    // Ends the turn as part of the change already captured in before.
    void nextTurn(PageUndo before);
    void undoLastChange();

    void onActionClicked(const std::string& attackerId, const MonsterAttack& attack);
    void onFeatureClicked(const std::string& combatantId, FeatureKind kind, const MonsterFeature& feature);
    void armAction(ArmedAction action);
    void disarmAttack();
    void onTargetClicked(QTreeWidgetItem* item, int column);
    void resolveArmedOn(const std::string& targetId);
    void afterDamage(Combatant& target, const DamageResult& result, const QString& source);
    void maybeAutoPass(const std::string& attackerId);
    // Conditions the action gives the target for this outcome (kRiderOn...).
    // dealt is the damage just taken, for a rider that replaces it.
    void applyRiders(Combatant& attacker, Combatant& target, const MonsterAttack& attack, const std::string& on,
                     RollMode mode, int dealt);
    void applyDrainTo(Combatant& attacker, Combatant& target, const HpDrain& drain,
                      const std::vector<TypedDamage>& damage, const DamageResult& result);
    static std::string choiceKey(const std::string& attackerId, const std::string& action,
                                 const std::string& condition);
    bool rollTicked(const std::string& attackerId, const std::string& questionKey) const;
    bool choiceTicked(const std::string& attackerId, const std::string& action, const std::string& condition) const;
    void clearChoices(const std::string& attackerId, const std::string& action);
    // The damage options for this action: what the app sees for itself
    // (Bloodied, grappling) and what the GM ticked.
    DamageOptions damageOptionsFor(const Combatant& attacker, const Combatant* target, const MonsterAttack& attack) const;
    void addDamageChoices(QVBoxLayout* layout, const Combatant& attacker, const MonsterAttack& attack);
    void giveRider(Combatant& attacker, Combatant& target, const MonsterAttack& attack, const ConditionRider& rider);
    // Ends conditions whose cause is gone, and logs them.
    void releaseEndedConditions();
    // Removes the conditions these events end (actionEvents, spellEvents) and logs it.
    void endByEvents(Combatant& creature, const std::vector<std::string>& events);
    // The condition and concentration an ability gives its user.
    void applyAbilityEffect(Combatant& creature, const std::optional<SelfEffect>& effect);
    // Logs each condition in before that the creature no longer has.
    void logConditionsGone(const Combatant& creature, const std::vector<ActiveCondition>& before,
                           const std::string& except = {});
    // A concentration's display name: the spell's, or the ability name as stored.
    QString concentrationName(const std::string& id) const;
    void handleTurnEvents(const std::vector<TurnEvent>& events);
    // Asks about each aura that may affect the creature whose turn starts.
    void addAuraPrompts(const std::string& combatantId);
    std::optional<AuraCheck> auraCheckFor(const Prompt& prompt);
    void toggleAura(const std::string& monsterId, const std::string& auraName);
    void resolveAura(Combatant& target, const AuraCheck& check, bool saved);
    void resolvePrompt(std::size_t index, int outcome);

    // Undo helpers. capture() before a change; commit() after it pushes the
    // snapshot when something changed, saves, and redraws.
    PageUndo capture() const;
    void commit(PageUndo before, EditKind kind = EditKind::Once, const std::string& selectId = {},
                bool saveSoon = false);
    void persistEncounters();
    void persistSoon();
    void saveCharacters();
    void carryToSheet(const Combatant& combatant);
    void addLog(const QString& line);
    void showLog();

    int rollD20();
    int rollDie(int sides);
    RollDie dieRoller();
    Encounter* selectedEncounter();
    Combatant* selectedCombatant();
    Combatant* combatantById(const std::string& id);
    Character* characterFor(const Combatant& combatant);
    std::string currentTurnId();
    bool isTheirTurn(const Combatant& combatant);
    QString nameOf(const std::string& combatantId);
    QString conditionName(const std::string& id) const;
    // "Blinded and Restrained".
    QString conditionList(const std::vector<std::string>& ids) const;

    CharacterStore& m_charactersStore;
    MonsterCatalog& m_catalog;
    EncounterStore& m_encountersStore;
    std::vector<Character> m_characters;
    std::vector<Encounter> m_encounters;
    std::vector<Spell> m_spells;
    std::vector<Condition> m_conditions;
    QString m_loadError;
    std::mt19937 m_dice;

    bool m_populating = false;
    bool m_reportedLoadError = false;
    bool m_swordCursor = false;
    bool m_savePending = false;
    EditKind m_lastEdit = EditKind::Once;
    std::vector<PageUndo> m_undo;
    std::string m_shownEncounterId;
    QString m_historyFile;
    // While an armed action resolves on a target: whose and which, for damage
    // that a condition spares.
    std::string m_damageAttackerId;
    std::string m_damageAction;
    bool m_historyRestored = false;
    std::optional<ArmedAction> m_armed;
    std::vector<Prompt> m_prompts;
    QStringList m_log;
    QTimer* m_saveTimer = nullptr;

    QComboBox* m_encounterCombo = nullptr;
    QWidget* m_fight = nullptr;
    QLabel* m_emptyHint = nullptr;
    QLabel* m_roundLabel = nullptr;
    QLabel* m_activeLabel = nullptr;
    QLabel* m_hitLabel = nullptr;
    QPushButton* m_nextTurnButton = nullptr;
    QPushButton* m_undoButton = nullptr;
    QPushButton* m_rollAllButton = nullptr;
    QPushButton* m_rollPlayersButton = nullptr;
    // The left column, for sharing its height between the turn order and the log.
    QWidget* m_leftHost = nullptr;
    QWidget* m_orderCard = nullptr;
    QWidget* m_logCard = nullptr;
    QLabel* m_entryMonsterCount = nullptr;
    bool m_groupInitiative = false;
    bool m_autoPass = true;
    int m_characterAttackAmount = 0;
    int m_characterHealAmount = 0;
    QString m_characterAttackType = QStringLiteral("slashing");
    QTabWidget* m_detailTabs = nullptr;
    QLabel* m_selectedName = nullptr;
    QLabel* m_selectedMeta = nullptr;
    QComboBox* m_rollMode = nullptr;
    QLabel* m_rollModeLabel = nullptr;
    // The GM's ticks for an action's "or" and extra damage, by
    // choiceKey(attacker, action, condition). Cleared once the action is used.
    std::map<std::string, bool> m_damageChoices;
    // Ticked trait questions for attack rolls, by "<attacker>|<question key>".
    std::map<std::string, bool> m_rollTicks;
    QLabel* m_rollNote = nullptr;
    QWidget* m_promptHost = nullptr;
    QVBoxLayout* m_promptLayout = nullptr;
    QTreeWidget* m_initiativeList = nullptr;
    QTreeWidget* m_downList = nullptr;
    QListWidget* m_logList = nullptr;
    QWidget* m_combatantForm = nullptr;
    QLabel* m_noCombatantHint = nullptr;
    QWidget* m_actionsSection = nullptr;
    QVBoxLayout* m_actionRows = nullptr;
    QVBoxLayout* m_detailsLayout = nullptr;
    QWidget* m_economyHost = nullptr;
    QCheckBox* m_actionUsed = nullptr;
    QCheckBox* m_bonusUsed = nullptr;
    QCheckBox* m_reactionUsed = nullptr;
    QLabel* m_economyNote = nullptr;
    QSpinBox* m_initiative = nullptr;
    QPushButton* m_rerollButton = nullptr;
    QLabel* m_initiativeBonus = nullptr;
    QLabel* m_acLabel = nullptr;
    QSpinBox* m_hp = nullptr;
    QLabel* m_maxHpLabel = nullptr;
    QLabel* m_bloodiedLabel = nullptr;
    QSpinBox* m_tempHp = nullptr;
    QComboBox* m_exhaustion = nullptr;
    QLabel* m_exhaustionNote = nullptr;
    QSpinBox* m_damageAmount = nullptr;
    QComboBox* m_damageType = nullptr;
    QCheckBox* m_damageCritical = nullptr;
    QPushButton* m_damageButton = nullptr;
    QSpinBox* m_healAmount = nullptr;
    QPushButton* m_healButton = nullptr;
    QPushButton* m_openDamageButton = nullptr;
    QPushButton* m_openHealButton = nullptr;
    QFrame* m_hitPanel = nullptr;
    QLabel* m_hitTargets = nullptr;
    QWidget* m_damageRow = nullptr;
    QWidget* m_healRow = nullptr;
    QWidget* m_savedHost = nullptr;
    QGridLayout* m_savedLayout = nullptr;
    bool m_healing = false;
    QComboBox* m_conditionPicker = nullptr;
    QComboBox* m_invisibleCause = nullptr;
    QComboBox* m_durationKind = nullptr;
    QComboBox* m_durationAnchor = nullptr;
    QSpinBox* m_durationTurns = nullptr;
    QCheckBox* m_saveEnds = nullptr;
    QComboBox* m_saveEndsAbility = nullptr;
    QSpinBox* m_saveEndsDc = nullptr;
    // One rounded tag per condition, coloured by condition; clicking one shows
    // its rules below, its × removes it.
    QWidget* m_conditionChips = nullptr;
    QLabel* m_conditionHeading = nullptr;
    QLabel* m_conditionDetail = nullptr;
    QFrame* m_conditionRule = nullptr;
    QString m_selectedCondition;
    QLabel* m_conditionText = nullptr;
    QLabel* m_concentrationLabel = nullptr;
    QLineEdit* m_spellSearch = nullptr;
    QListWidget* m_spellMatches = nullptr;
    QWidget* m_deathSavesHost = nullptr;
    QLabel* m_deathStatus = nullptr;
    QPushButton* m_stabilizeButton = nullptr;
    QPushButton* m_removeButton = nullptr;

    // One row of the initiative list.
    struct InitiativeEntryRow {
        std::string combatantId;
        QLabel* name = nullptr;
        QLabel* bonus = nullptr;
        QSpinBox* box = nullptr;
        QLabel* note = nullptr;
    };
    QWidget* m_rightHost = nullptr;
    QFrame* m_initiativeEntry = nullptr;
    QGridLayout* m_entryGrid = nullptr;
    QLabel* m_entryCount = nullptr;
    QLabel* m_entryEmpty = nullptr;
    QPushButton* m_entryStartButton = nullptr;
    QPushButton* m_backToEntryButton = nullptr;
    std::vector<InitiativeEntryRow> m_entryRows;
    std::string m_entryEncounterId;
    QString m_stateFile;
};

}  // namespace combat::ui
