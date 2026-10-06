#pragma once

#include "core/monster.h"

#include <QDialog>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QVBoxLayout;

namespace combat::ui {

// Edits what a custom monster's entry does in a fight, beyond its text: who
// it can target, the conditions it gives (and how they end), Hit Point
// maximum drains, what it does to its user, and (for a trait) an aura. For a
// bonus action, reaction, or legendary action it also sets whether it is
// aimed at targets like an action, with its roll, save, and damage.
class EffectsDialog : public QDialog {
    Q_OBJECT

public:
    // An action.
    EffectsDialog(const MonsterAttack& attack, QWidget* parent = nullptr);
    // A trait (trait true), bonus action, reaction, or legendary action.
    EffectsDialog(const MonsterFeature& feature, bool trait, QWidget* parent = nullptr);

    MonsterAttack attack() const;
    MonsterFeature feature() const;

private:
    struct RiderRow {
        QWidget* box = nullptr;
        QComboBox* when = nullptr;
        std::vector<QCheckBox*> conditions;
        QSpinBox* escapeDc = nullptr;
        QComboBox* maxSize = nullptr;
        QSpinBox* maxHp = nullptr;
        QLineEdit* exceptTypes = nullptr;
        QLineEdit* ask = nullptr;
        QCheckBox* ifAdvantage = nullptr;
        QCheckBox* refund = nullptr;
        QComboBox* until = nullptr;
        QComboBox* tiedTo = nullptr;
        QCheckBox* saveEnds = nullptr;
        QComboBox* worsensTo = nullptr;
        QCheckBox* worseSaveEnds = nullptr;
        QCheckBox* worseEndsOnDamage = nullptr;
        QCheckBox* endsOnDamage = nullptr;
        QCheckBox* endsWithMonster = nullptr;
        QLineEdit* ongoing = nullptr;
        QComboBox* ongoingAt = nullptr;
        QCheckBox* endsGrapple = nullptr;
        QCheckBox* stabilize = nullptr;
    };

    void build(bool feature, bool trait);
    void addRider(const ConditionRider& rider);
    ConditionRider readRider(const RiderRow& row) const;
    void readInto(MonsterAttack& attack) const;
    void updateEnabled();

    MonsterAttack m_attack;
    MonsterFeature m_feature;
    bool m_isFeature = false;

    // Aimed at targets (features only).
    QCheckBox* m_targeted = nullptr;
    QCheckBox* m_afterBloodied = nullptr;
    QWidget* m_rollBox = nullptr;
    QCheckBox* m_hasAttackRoll = nullptr;
    QSpinBox* m_attackBonus = nullptr;
    QComboBox* m_saveAbility = nullptr;
    QSpinBox* m_saveDc = nullptr;
    QCheckBox* m_half = nullptr;
    QLineEdit* m_damage = nullptr;
    QCheckBox* m_area = nullptr;

    // Who it can target.
    QLineEdit* m_targetCondition = nullptr;
    QComboBox* m_targetMaxSize = nullptr;
    QSpinBox* m_hpThreshold = nullptr;
    QComboBox* m_hpEffect = nullptr;
    QCheckBox* m_advantageIfGrappled = nullptr;
    QCheckBox* m_riderSave = nullptr;
    QComboBox* m_riderSaveAbility = nullptr;
    QSpinBox* m_riderSaveDc = nullptr;

    // Help for the creature it picks.
    QCheckBox* m_benefit = nullptr;
    QLineEdit* m_benefitTempHp = nullptr;
    QLineEdit* m_benefitHealing = nullptr;
    QCheckBox* m_benefitAdvantage = nullptr;
    QSpinBox* m_benefitAc = nullptr;
    QComboBox* m_benefitUntil = nullptr;

    QComboBox* m_drain = nullptr;
    QCheckBox* m_drainHeals = nullptr;

    QVBoxLayout* m_riderLayout = nullptr;
    std::vector<RiderRow> m_riders;

    // What it does to its user.
    QCheckBox* m_self = nullptr;
    QComboBox* m_selfCondition = nullptr;
    QLineEdit* m_selfSource = nullptr;
    QLineEdit* m_selfConcentration = nullptr;
    std::vector<QCheckBox*> m_selfEnds;
    QLineEdit* m_selfEndsActions = nullptr;

    // A trait's aura.
    QCheckBox* m_aura = nullptr;
    QComboBox* m_auraAbility = nullptr;
    QSpinBox* m_auraDc = nullptr;
    QLineEdit* m_auraRange = nullptr;
    QLineEdit* m_auraWho = nullptr;
    QComboBox* m_auraCondition = nullptr;
    QCheckBox* m_auraImmune = nullptr;
    QCheckBox* m_auraWhileActive = nullptr;
    QCheckBox* m_auraEnemies = nullptr;
    QLineEdit* m_auraTypes = nullptr;
    QLineEdit* m_auraSuppressed = nullptr;

    // A trait that changes attack rolls.
    QCheckBox* m_modifier = nullptr;
    QComboBox* m_modifierMode = nullptr;
    QComboBox* m_modifierWhen = nullptr;
    QComboBox* m_modifierType = nullptr;
    QLineEdit* m_modifierAsk = nullptr;
    QCheckBox* m_modifierSticky = nullptr;
    QCheckBox* m_modifierMelee = nullptr;
    QCheckBox* m_modifierAllies = nullptr;
    QCheckBox* m_modifierActive = nullptr;

    QLabel* m_problem = nullptr;
};

}  // namespace combat::ui
