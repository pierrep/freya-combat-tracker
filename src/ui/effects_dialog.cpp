#include "ui/effects_dialog.h"

#include "core/combat_rules.h"
#include "core/text.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>

namespace combat::ui {

namespace {

const std::array<const char*, 14> kConditions{{"blinded", "charmed", "deafened", "frightened", "grappled",
                                               "incapacitated", "invisible", "paralyzed", "petrified", "poisoned",
                                               "prone", "restrained", "stunned", "unconscious"}};
const std::array<const char*, 6> kSizes{{"Tiny", "Small", "Medium", "Large", "Huge", "Gargantuan"}};
const std::array<const char*, 13> kDamageTypes{{"acid", "bludgeoning", "cold", "fire", "force", "lightning",
                                                "necrotic", "piercing", "poison", "psychic", "radiant", "slashing",
                                                "thunder"}};

QString label(const std::string& id)
{
    QString text = QString::fromStdString(id);
    if (!text.isEmpty()) {
        text[0] = text[0].toUpper();
    }
    return text;
}

// A combo of conditions with a first "none" row whose data is empty.
QComboBox* conditionCombo(const QString& none, const std::string& current)
{
    auto* combo = new QComboBox;
    combo->addItem(none, QString());
    for (const char* id : kConditions) {
        combo->addItem(label(id), QString::fromLatin1(id));
    }
    const int row = combo->findData(QString::fromStdString(current));
    combo->setCurrentIndex(row < 0 ? 0 : row);
    return combo;
}

QComboBox* abilityCombo(std::optional<Ability> current, const QString& none = {})
{
    auto* combo = new QComboBox;
    if (!none.isEmpty()) {
        combo->addItem(none, -1);
    }
    for (const Ability ability : kAbilityOrder) {
        combo->addItem(QString::fromLatin1(abilityLabel(ability)), static_cast<int>(ability));
    }
    if (current.has_value()) {
        combo->setCurrentIndex(combo->findData(static_cast<int>(*current)));
    }
    return combo;
}

std::optional<Ability> abilityOf(const QComboBox* combo)
{
    const int value = combo->currentData().toInt();
    if (value < 0) {
        return std::nullopt;
    }
    return kAbilityOrder[static_cast<std::size_t>(value)];
}

QSpinBox* numberBox(int low, int high, int value, const QString& special = {})
{
    auto* box = new QSpinBox;
    box->setRange(low, high);
    box->setValue(value);
    if (!special.isEmpty()) {
        box->setSpecialValueText(special);
    }
    return box;
}

std::vector<std::string> splitList(const QString& text)
{
    std::vector<std::string> out;
    for (const QString& piece : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = piece.trimmed();
        if (!trimmed.isEmpty()) {
            out.push_back(trimmed.toStdString());
        }
    }
    return out;
}

QString joinList(const std::vector<std::string>& list)
{
    QStringList out;
    for (const std::string& item : list) {
        out << QString::fromStdString(item);
    }
    return out.join(QStringLiteral(", "));
}

bool has(const std::vector<std::string>& list, const char* value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

QFrame* section(QVBoxLayout* into, const QString& heading, const QString& note = {})
{
    auto* card = makeCard();
    auto* layout = static_cast<QVBoxLayout*>(card->layout());
    layout->setSpacing(8);
    layout->addWidget(makeHeading(heading));
    if (!note.isEmpty()) {
        auto* muted = makeMuted(note);
        muted->setWordWrap(true);
        layout->addWidget(muted);
    }
    into->addWidget(card);
    return card;
}

}  // namespace

EffectsDialog::EffectsDialog(const MonsterAttack& attack, QWidget* parent) : QDialog(parent), m_attack(attack)
{
    build(false, false);
}

EffectsDialog::EffectsDialog(const MonsterFeature& feature, bool trait, QWidget* parent)
    : QDialog(parent), m_feature(feature), m_isFeature(true)
{
    if (feature.targeted.has_value()) {
        m_attack = *feature.targeted;
    }
    m_attack.name = feature.name;
    m_attack.effect = feature.effect;
    m_attack.selfEffect = feature.selfEffect;
    build(true, trait);
}

void EffectsDialog::build(bool feature, bool trait)
{
    setObjectName(QStringLiteral("effectsDialog"));
    setWindowTitle(tr("Effects: %1").arg(QString::fromStdString(abilityDisplayName(m_attack.name))));
    resize(720, 760);
    auto* outer = new QVBoxLayout(this);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page = new QWidget;
    auto* column = new QVBoxLayout(page);
    column->setSpacing(12);
    scroll->setWidget(page);
    outer->addWidget(scroll, 1);

    // --- Aimed at targets (features) ------------------------------------------
    if (feature && !trait) {
        QFrame* card = section(column, tr("Aimed at targets"),
                               tr("On: in a fight, its button picks targets like an action and rolls for them."));
        auto* layout = static_cast<QVBoxLayout*>(card->layout());
        m_targeted = new QCheckBox(tr("Aim it at targets"));
        m_targeted->setObjectName(QStringLiteral("effectsTargeted"));
        m_targeted->setChecked(m_feature.targeted.has_value());
        layout->addWidget(m_targeted);
        m_afterBloodied = new QCheckBox(tr("Only right after it damages a creature that was already Bloodied (Rampage)"));
        m_afterBloodied->setObjectName(QStringLiteral("effectsAfterBloodied"));
        m_afterBloodied->setChecked(m_feature.afterDamagingBloodied);
        layout->addWidget(m_afterBloodied);
        m_rollBox = new QWidget;
        auto* grid = new QGridLayout(m_rollBox);
        grid->setContentsMargins(0, 0, 0, 0);
        m_hasAttackRoll = new QCheckBox(tr("Attack roll"));
        m_hasAttackRoll->setChecked(m_attack.attackBonus.has_value());
        m_attackBonus = numberBox(-10, 30, m_attack.attackBonus.value_or(0));
        m_attackBonus->setPrefix(QStringLiteral("+"));
        m_saveAbility = abilityCombo(m_attack.save.has_value() ? std::optional(m_attack.save->ability) : std::nullopt,
                                     tr("No save"));
        m_saveAbility->setObjectName(QStringLiteral("effectsSaveAbility"));
        m_saveDc = numberBox(1, 40, m_attack.save.has_value() ? m_attack.save->dc : 10);
        m_saveDc->setPrefix(tr("DC "));
        m_saveDc->setObjectName(QStringLiteral("effectsSaveDc"));
        m_half = new QCheckBox(tr("Half on success"));
        m_half->setChecked(m_attack.save.has_value() && m_attack.save->halfOnSuccess);
        m_damage = new QLineEdit(QString::fromStdString(formatDamageParts(m_attack.damage)));
        m_damage->setPlaceholderText(tr("Damage: 4d8 psychic"));
        m_area = new QCheckBox(tr("Area (several targets)"));
        m_area->setChecked(m_attack.area);
        grid->addWidget(m_hasAttackRoll, 0, 0);
        grid->addWidget(m_attackBonus, 0, 1);
        grid->addWidget(m_saveAbility, 0, 2);
        grid->addWidget(m_saveDc, 0, 3);
        grid->addWidget(m_half, 0, 4);
        grid->addWidget(m_damage, 1, 0, 1, 4);
        grid->addWidget(m_area, 1, 4);
        layout->addWidget(m_rollBox);
        connect(m_targeted, &QCheckBox::toggled, this, [this] { updateEnabled(); });
        connect(m_hasAttackRoll, &QCheckBox::toggled, this, [this] { updateEnabled(); });
        connect(m_saveAbility, &QComboBox::currentIndexChanged, this, [this] { updateEnabled(); });
    }

    if (!trait) {
        // --- Who it can target ---------------------------------------------------
        QFrame* card = section(column, tr("Who it can target"));
        auto* form = new QFormLayout;
        static_cast<QVBoxLayout*>(card->layout())->addLayout(form);
        m_targetCondition = new QLineEdit(QString::fromStdString(m_attack.targetCondition));
        m_targetCondition->setObjectName(QStringLiteral("effectsTargetCondition"));
        m_targetCondition->setPlaceholderText(tr("Any creature (or: frightened, or charmed, grappled)"));
        m_targetCondition->setToolTip(tr("Condition ids the target must have; any one of a comma list will do. "
                                         "Grappled means grappled by this monster."));
        form->addRow(tr("Must be"), m_targetCondition);
        m_targetMaxSize = new QComboBox;
        m_targetMaxSize->addItem(tr("Any size"), QString());
        for (const char* size : kSizes) {
            m_targetMaxSize->addItem(tr("%1 or smaller").arg(QString::fromLatin1(size)), QString::fromLatin1(size));
        }
        m_targetMaxSize->setCurrentIndex(
            std::max(0, m_targetMaxSize->findData(QString::fromStdString(m_attack.targetMaxSize))));
        form->addRow(tr("Size"), m_targetMaxSize);
        auto* threshold = new QHBoxLayout;
        m_hpThreshold = numberBox(0, 999, m_attack.failureHpThreshold.value_or(0), tr("No Hit Point limit"));
        m_hpThreshold->setSuffix(tr(" HP or fewer"));
        m_hpEffect = new QComboBox;
        m_hpEffect->addItem(tr("drops to 0"), QStringLiteral("dropsToZero"));
        m_hpEffect->addItem(tr("dies"), QStringLiteral("dies"));
        m_hpEffect->setCurrentIndex(m_attack.failureHpEffect == "dies" ? 1 : 0);
        threshold->addWidget(m_hpThreshold);
        threshold->addWidget(m_hpEffect);
        threshold->addStretch(1);
        form->addRow(tr("On a failed save, a target with"), threshold);
        m_advantageIfGrappled = new QCheckBox(tr("Advantage against a creature it is grappling"));
        m_advantageIfGrappled->setChecked(m_attack.advantageIfGrappled);
        form->addRow(QString(), m_advantageIfGrappled);
        auto* riderSave = new QHBoxLayout;
        m_riderSave = new QCheckBox(tr("A hit makes the target save"));
        m_riderSave->setChecked(m_attack.riderSave.has_value());
        m_riderSaveAbility = abilityCombo(
            m_attack.riderSave.has_value() ? std::optional(m_attack.riderSave->ability) : Ability::Constitution);
        m_riderSaveDc = numberBox(1, 40, m_attack.riderSave.has_value() ? m_attack.riderSave->dc : 10);
        m_riderSaveDc->setPrefix(tr("DC "));
        riderSave->addWidget(m_riderSaveAbility, 1);
        riderSave->addWidget(m_riderSaveDc);
        form->addRow(QString(), m_riderSave);
        form->addRow(QString(), riderSave);
        connect(m_riderSave, &QCheckBox::toggled, this, [this] { updateEnabled(); });

        // --- Conditions it gives ---------------------------------------------------
        QFrame* riders = section(column, tr("Conditions it gives"),
                                 tr("Applied in a fight when it hits or the target fails its save. A question is "
                                    "asked only when the app cannot know the answer."));
        auto* ridersLayout = static_cast<QVBoxLayout*>(riders->layout());
        m_riderLayout = new QVBoxLayout;
        m_riderLayout->setSpacing(10);
        ridersLayout->addLayout(m_riderLayout);
        auto* add = new QPushButton(tr("Add a condition"));
        add->setObjectName(QStringLiteral("addRider"));
        ridersLayout->addWidget(add, 0, Qt::AlignLeft);
        connect(add, &QPushButton::clicked, this, [this] {
            ConditionRider rider;
            rider.conditions = {"prone"};
            rider.on = m_attack.attackBonus.has_value() || !m_attack.save.has_value() ? kRiderOnHit : kRiderOnFailure;
            addRider(rider);
        });
        for (const ConditionRider& rider : m_attack.riders) {
            addRider(rider);
        }

        // --- Help for the creature it picks ---------------------------------------
        {
            const std::optional<Benefit>& benefit = m_attack.benefit;
            QFrame* help = section(column, tr("Helps the creature it picks"),
                                   tr("A War Cry or a Shimmering Shield: in a fight its button shows the green "
                                      "sparkle, and you click who gets it (the monster itself too)."));
            auto* layout = static_cast<QVBoxLayout*>(help->layout());
            m_benefit = new QCheckBox(tr("It helps instead of hurting"));
            m_benefit->setObjectName(QStringLiteral("effectsBenefit"));
            m_benefit->setChecked(benefit.has_value());
            layout->addWidget(m_benefit);
            auto* helpForm = new QFormLayout;
            layout->addLayout(helpForm);
            m_benefitTempHp = new QLineEdit(benefit.has_value() ? QString::fromStdString(benefit->tempHp) : QString());
            m_benefitTempHp->setObjectName(QStringLiteral("effectsBenefitTempHp"));
            m_benefitTempHp->setPlaceholderText(tr("None (or dice: 2d10+5)"));
            helpForm->addRow(tr("Temporary Hit Points"), m_benefitTempHp);
            m_benefitHealing = new QLineEdit(benefit.has_value() ? QString::fromStdString(benefit->healing) : QString());
            m_benefitHealing->setPlaceholderText(tr("None (or dice: 4d8+2)"));
            helpForm->addRow(tr("Healing"), m_benefitHealing);
            m_benefitAdvantage = new QCheckBox(tr("Advantage on its attack rolls"));
            m_benefitAdvantage->setChecked(benefit.has_value() && benefit->advantageOnAttacks);
            helpForm->addRow(QString(), m_benefitAdvantage);
            m_benefitAc = numberBox(0, 10, benefit.has_value() ? benefit->acBonus : 0, tr("No AC change"));
            m_benefitAc->setPrefix(tr("+"));
            m_benefitAc->setSuffix(tr(" AC"));
            helpForm->addRow(tr("AC"), m_benefitAc);
            m_benefitUntil = new QComboBox;
            m_benefitUntil->addItem(tr("Until the start of the monster's next turn"), QString::fromLatin1(kUntilSourceStart));
            m_benefitUntil->addItem(tr("Until the end of the monster's next turn"), QString::fromLatin1(kUntilSourceEnd));
            m_benefitUntil->setCurrentIndex(benefit.has_value() && benefit->until == kUntilSourceEnd ? 1 : 0);
            helpForm->addRow(tr("Advantage and AC last"), m_benefitUntil);
            connect(m_benefit, &QCheckBox::toggled, this, [this] { updateEnabled(); });
        }

        // --- Drain ----------------------------------------------------------------
        QFrame* drain = section(column, tr("Hit Point maximum"));
        auto* drainRow = new QHBoxLayout;
        static_cast<QVBoxLayout*>(drain->layout())->addLayout(drainRow);
        m_drain = new QComboBox;
        m_drain->setObjectName(QStringLiteral("effectsDrain"));
        m_drain->addItem(tr("Not lowered"), QStringLiteral("-"));
        m_drain->addItem(tr("Lowered by all the damage taken"), QString());
        for (const char* type : kDamageTypes) {
            m_drain->addItem(tr("Lowered by the %1 damage taken").arg(QString::fromLatin1(type)),
                             QString::fromLatin1(type));
        }
        m_drain->setCurrentIndex(
            m_attack.drain.has_value() ? std::max(0, m_drain->findData(QString::fromStdString(m_attack.drain->type)))
                                       : 0);
        m_drainHeals = new QCheckBox(tr("The monster regains that many Hit Points"));
        m_drainHeals->setChecked(m_attack.drain.has_value() && m_attack.drain->heals);
        drainRow->addWidget(m_drain, 1);
        static_cast<QVBoxLayout*>(drain->layout())->addWidget(m_drainHeals);
        connect(m_drain, &QComboBox::currentIndexChanged, this, [this] { updateEnabled(); });
    }

    // --- What it does to its user ------------------------------------------------
    {
        QFrame* card = section(column, trait ? tr("A condition it always has") : tr("What it does to the monster"),
                               trait ? tr("Given at the start of the fight (an Invisible Stalker's Invisibility).")
                                     : tr("Given to the monster when it uses this (a wisp's Vanish)."));
        auto* layout = static_cast<QVBoxLayout*>(card->layout());
        const std::optional<SelfEffect>& self = m_attack.selfEffect;
        m_self = new QCheckBox(tr("Gives the monster a condition"));
        m_self->setObjectName(QStringLiteral("effectsSelf"));
        m_self->setChecked(self.has_value());
        layout->addWidget(m_self);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        m_selfCondition = conditionCombo(tr("Choose a condition"), self.has_value() ? self->condition : "invisible");
        form->addRow(tr("Condition"), m_selfCondition);
        m_selfSource = new QLineEdit(self.has_value() ? QString::fromStdString(self->source) : QString());
        m_selfSource->setPlaceholderText(QString::fromStdString(abilityDisplayName(m_attack.name)));
        form->addRow(tr("Shown as"), m_selfSource);
        m_selfConcentration = new QLineEdit(self.has_value() ? QString::fromStdString(self->concentration) : QString());
        m_selfConcentration->setPlaceholderText(tr("No concentration"));
        form->addRow(tr("Needs concentration on"), m_selfConcentration);
        auto* ends = new QGridLayout;
        const std::vector<std::pair<const char*, QString>> events{
            {kEndsOnAttackRoll, tr("an attack roll")},
            {kEndsOnSaveEffect, tr("a saving-throw effect")},
            {kEndsOnVerbalSpell, tr("a Verbal spell")},
            {kEndsOnAnySpell, tr("any spell")},
            {kEndsOnDealsDamage, tr("dealing damage")}};
        int index = 0;
        for (const auto& [key, text] : events) {
            auto* box = new QCheckBox(text);
            box->setProperty("event", QString::fromLatin1(key));
            box->setChecked(self.has_value() && has(self->endsOn, key));
            ends->addWidget(box, index / 3, index % 3);
            m_selfEnds.push_back(box);
            ++index;
        }
        form->addRow(tr("Ends on"), ends);
        QStringList actions;
        if (self.has_value()) {
            for (const std::string& event : self->endsOn) {
                if (event.rfind(kEndsOnActionPrefix, 0) == 0) {
                    actions << QString::fromStdString(event.substr(std::string(kEndsOnActionPrefix).size()));
                }
            }
        }
        m_selfEndsActions = new QLineEdit(actions.join(QStringLiteral(", ")));
        m_selfEndsActions->setPlaceholderText(tr("Using these actions (Consume Life)"));
        form->addRow(QString(), m_selfEndsActions);
        connect(m_self, &QCheckBox::toggled, this, [this] { updateEnabled(); });
    }

    // --- A trait's Advantage or Disadvantage on attack rolls -------------------
    if (trait) {
        const std::optional<AttackModifier>& modifier = m_feature.attackModifier;
        QFrame* card = section(column, tr("Attack rolls"),
                               tr("Pack Tactics, Bloodied Fury, Sunlight Sensitivity: the Dashboard's Automatic "
                                  "attack rolls use it."));
        auto* layout = static_cast<QVBoxLayout*>(card->layout());
        m_modifier = new QCheckBox(tr("This trait changes its attack rolls"));
        m_modifier->setObjectName(QStringLiteral("effectsAttackModifier"));
        m_modifier->setChecked(modifier.has_value());
        layout->addWidget(m_modifier);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        m_modifierMode = new QComboBox;
        m_modifierMode->addItem(tr("Advantage"), true);
        m_modifierMode->addItem(tr("Disadvantage"), false);
        m_modifierMode->setCurrentIndex(modifier.has_value() && !modifier->advantage ? 1 : 0);
        form->addRow(tr("Gives"), m_modifierMode);
        m_modifierWhen = new QComboBox;
        m_modifierWhen->setObjectName(QStringLiteral("effectsModifierWhen"));
        m_modifierWhen->addItem(tr("When ticked on the Dashboard"), QString());
        m_modifierWhen->addItem(tr("While it is Bloodied"), QString::fromLatin1(kModifierWhileBloodied));
        m_modifierWhen->addItem(tr("Against a creature missing Hit Points"), QString::fromLatin1(kModifierTargetHurt));
        m_modifierWhen->addItem(tr("After it takes this damage, until the end of its next turn"),
                                QString::fromLatin1(kModifierAfterDamage));
        m_modifierWhen->setCurrentIndex(
            modifier.has_value() ? std::max(0, m_modifierWhen->findData(QString::fromStdString(modifier->when))) : 0);
        form->addRow(tr("When"), m_modifierWhen);
        m_modifierType = new QComboBox;
        for (const char* type : kDamageTypes) {
            m_modifierType->addItem(label(type), QString::fromLatin1(type));
        }
        m_modifierType->setCurrentIndex(
            modifier.has_value() ? std::max(0, m_modifierType->findData(QString::fromStdString(modifier->damageType)))
                                 : 3);
        form->addRow(tr("Damage type"), m_modifierType);
        m_modifierAsk = new QLineEdit(modifier.has_value() ? QString::fromStdString(modifier->ask) : QString());
        m_modifierAsk->setPlaceholderText(tr("an ally is within 5 feet of the target"));
        form->addRow(tr("Tick box says"), m_modifierAsk);
        m_modifierSticky = new QCheckBox(tr("Stays ticked between attacks (sunlight)"));
        m_modifierSticky->setChecked(modifier.has_value() && modifier->sticky);
        m_modifierMelee = new QCheckBox(tr("Melee attacks only"));
        m_modifierMelee->setChecked(modifier.has_value() && modifier->meleeOnly);
        m_modifierAllies = new QCheckBox(tr("Its allies' attack rolls too (an aura)"));
        m_modifierAllies->setChecked(modifier.has_value() && modifier->alliesToo);
        m_modifierActive = new QCheckBox(tr("Not while it is Incapacitated"));
        m_modifierActive->setChecked(modifier.has_value() && modifier->whileActive);
        form->addRow(QString(), m_modifierSticky);
        form->addRow(QString(), m_modifierMelee);
        form->addRow(QString(), m_modifierAllies);
        form->addRow(QString(), m_modifierActive);
        connect(m_modifier, &QCheckBox::toggled, this, [this] { updateEnabled(); });
        connect(m_modifierWhen, &QComboBox::currentIndexChanged, this, [this] { updateEnabled(); });
    }

    // --- A trait's aura -----------------------------------------------------------
    if (trait) {
        const std::optional<AuraSave>& aura = m_feature.aura;
        QFrame* card = section(column, tr("Aura"),
                               tr("Creatures that start their turn near the monster save; the Dashboard asks "
                                  "whether each one is in range."));
        auto* layout = static_cast<QVBoxLayout*>(card->layout());
        m_aura = new QCheckBox(tr("This trait is an aura"));
        m_aura->setObjectName(QStringLiteral("effectsAura"));
        m_aura->setChecked(aura.has_value());
        layout->addWidget(m_aura);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        auto* save = new QHBoxLayout;
        m_auraAbility = abilityCombo(aura.has_value() ? std::optional(aura->ability) : Ability::Wisdom);
        m_auraDc = numberBox(1, 40, aura.has_value() ? aura->dc : 10);
        m_auraDc->setPrefix(tr("DC "));
        save->addWidget(m_auraAbility);
        save->addWidget(m_auraDc);
        save->addStretch(1);
        form->addRow(tr("Save"), save);
        m_auraRange = new QLineEdit(aura.has_value() ? QString::fromStdString(aura->range) : QString());
        m_auraRange->setPlaceholderText(tr("within 10 feet of the monster"));
        form->addRow(tr("Range"), m_auraRange);
        m_auraWho = new QLineEdit(aura.has_value() ? QString::fromStdString(aura->who) : QString());
        m_auraWho->setPlaceholderText(tr("Any creature"));
        form->addRow(tr("Who"), m_auraWho);
        m_auraCondition = conditionCombo(tr("No condition"), aura.has_value() ? aura->condition : std::string());
        form->addRow(tr("On a failure (until the start of its next turn)"), m_auraCondition);
        m_auraTypes = new QLineEdit(aura.has_value() ? joinList(aura->creatureTypes) : QString());
        m_auraTypes->setPlaceholderText(tr("Every creature type (or: beast, humanoid)"));
        form->addRow(tr("Only these creature types"), m_auraTypes);
        m_auraImmune = new QCheckBox(tr("A success makes it immune for the fight"));
        m_auraImmune->setChecked(aura.has_value() && aura->immuneOnSuccess);
        m_auraWhileActive = new QCheckBox(tr("Off while the monster is Incapacitated"));
        m_auraWhileActive->setChecked(aura.has_value() && aura->whileActive);
        m_auraEnemies = new QCheckBox(tr("Enemies only (not other monsters)"));
        m_auraEnemies->setChecked(aura.has_value() && aura->enemiesOnly);
        form->addRow(QString(), m_auraImmune);
        form->addRow(QString(), m_auraWhileActive);
        form->addRow(QString(), m_auraEnemies);
        m_auraSuppressed = new QLineEdit(aura.has_value() ? joinList(aura->suppressedBy) : QString());
        m_auraSuppressed->setPlaceholderText(tr("Actions that switch it off (Illusory Appearance)"));
        form->addRow(tr("Switched off by"), m_auraSuppressed);
        connect(m_aura, &QCheckBox::toggled, this, [this] { updateEnabled(); });
    }
    column->addStretch(1);

    m_problem = new QLabel;
    m_problem->setProperty("role", QStringLiteral("banner-error"));
    m_problem->setWordWrap(true);
    m_problem->hide();
    outer->addWidget(m_problem);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("effectsOk"));
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_damage != nullptr) {
            std::string error;
            if (!parseDamageParts(m_damage->text().toStdString(), &error).has_value()) {
                m_problem->setText(QString::fromStdString(error));
                m_problem->show();
                return;
            }
        }
        for (const RiderRow& row : m_riders) {
            std::string error;
            if (!parseDamageParts(row.ongoing->text().toStdString(), &error).has_value()) {
                m_problem->setText(tr("Ongoing damage: %1").arg(QString::fromStdString(error)));
                m_problem->show();
                return;
            }
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateEnabled();
}

void EffectsDialog::addRider(const ConditionRider& rider)
{
    RiderRow row;
    auto* box = new QFrame;
    box->setObjectName(QStringLiteral("riderBox"));
    box->setProperty("role", QStringLiteral("inset"));
    box->setFrameShape(QFrame::StyledPanel);
    row.box = box;
    auto* layout = new QVBoxLayout(box);
    layout->setSpacing(6);

    auto* top = new QHBoxLayout;
    row.when = new QComboBox;
    row.when->setObjectName(QStringLiteral("riderWhen"));
    row.when->addItem(tr("On a hit"), QString::fromLatin1(kRiderOnHit));
    row.when->addItem(tr("On a failed save"), QString::fromLatin1(kRiderOnFailure));
    row.when->addItem(tr("Failing by 5 or more (instead)"), QString::fromLatin1(kRiderOnFailureBy5));
    row.when->addItem(tr("If the hit drops it to 0 HP"), QString::fromLatin1(kRiderOnZeroHp));
    row.when->setCurrentIndex(std::max(0, row.when->findData(QString::fromStdString(rider.on))));
    top->addWidget(row.when);
    top->addStretch(1);
    auto* remove = new QPushButton(tr("Remove"));
    makeQuiet(remove);
    top->addWidget(remove);
    layout->addLayout(top);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    for (std::size_t i = 0; i < kConditions.size(); ++i) {
        auto* check = new QCheckBox(label(kConditions[i]));
        check->setProperty("condition", QString::fromLatin1(kConditions[i]));
        check->setChecked(has(rider.conditions, kConditions[i]));
        grid->addWidget(check, static_cast<int>(i / 4), static_cast<int>(i % 4));
        row.conditions.push_back(check);
    }
    layout->addLayout(grid);

    auto* form = new QFormLayout;
    layout->addLayout(form);
    row.until = new QComboBox;
    row.until->addItem(tr("Until removed"), QString());
    row.until->addItem(tr("Until the start of the monster's next turn"), QString::fromLatin1(kUntilSourceStart));
    row.until->addItem(tr("Until the end of the monster's next turn"), QString::fromLatin1(kUntilSourceEnd));
    row.until->addItem(tr("Until the start of the target's next turn"), QString::fromLatin1(kUntilTargetStart));
    row.until->addItem(tr("Until the end of the target's next turn"), QString::fromLatin1(kUntilTargetEnd));
    row.until->addItem(tr("Until the end of the target's current turn"), QString::fromLatin1(kUntilTargetThisTurn));
    row.until->addItem(tr("For 1 minute"), QString::fromLatin1(kUntilMinute));
    row.until->setCurrentIndex(std::max(0, row.until->findData(QString::fromStdString(rider.until))));
    form->addRow(tr("Lasts"), row.until);

    auto* saves = new QHBoxLayout;
    row.saveEnds = new QCheckBox(tr("A save at the end of each of its turns ends it"));
    row.saveEnds->setChecked(rider.saveEnds);
    saves->addWidget(row.saveEnds);
    saves->addStretch(1);
    form->addRow(QString(), saves);
    auto* worse = new QHBoxLayout;
    row.worsensTo = conditionCombo(tr("changes nothing"),
                                   rider.worsensTo.empty() ? std::string() : rider.worsensTo.front());
    row.worseSaveEnds = new QCheckBox(tr("then saves each turn"));
    row.worseSaveEnds->setChecked(rider.worseSaveEnds);
    row.worseEndsOnDamage = new QCheckBox(tr("then ends on taking damage"));
    row.worseEndsOnDamage->setChecked(has(rider.worseEndsOn, kEndsOnTakesDamage));
    worse->addWidget(row.worsensTo, 1);
    form->addRow(tr("A failed repeat save"), worse);
    auto* worseEnds = new QHBoxLayout;
    worseEnds->addWidget(row.worseSaveEnds);
    worseEnds->addWidget(row.worseEndsOnDamage);
    worseEnds->addStretch(1);
    form->addRow(QString(), worseEnds);

    row.tiedTo = conditionCombo(tr("Nothing else"), rider.tiedTo);
    form->addRow(tr("Ends when this ends"), row.tiedTo);
    auto* early = new QHBoxLayout;
    row.endsOnDamage = new QCheckBox(tr("when it takes damage"));
    row.endsOnDamage->setChecked(has(rider.endsOn, kEndsOnTakesDamage));
    row.endsWithMonster = new QCheckBox(tr("when the monster dies"));
    row.endsWithMonster->setChecked(has(rider.endsOn, kEndsOnSourceGone));
    early->addWidget(row.endsOnDamage);
    early->addWidget(row.endsWithMonster);
    early->addStretch(1);
    form->addRow(tr("Also ends"), early);

    auto* limits = new QHBoxLayout;
    row.escapeDc = numberBox(0, 40, rider.escapeDc.value_or(0), tr("No escape DC"));
    row.escapeDc->setPrefix(tr("Escape DC "));
    row.maxSize = new QComboBox;
    row.maxSize->addItem(tr("Any size"), QString());
    for (const char* size : kSizes) {
        row.maxSize->addItem(tr("%1 or smaller").arg(QString::fromLatin1(size)), QString::fromLatin1(size));
    }
    row.maxSize->setCurrentIndex(std::max(0, row.maxSize->findData(QString::fromStdString(rider.targetMaxSize))));
    row.maxHp = numberBox(0, 999, rider.targetMaxHp.value_or(0), tr("Any Hit Points"));
    row.maxHp->setPrefix(tr("At most "));
    row.maxHp->setSuffix(tr(" HP"));
    limits->addWidget(row.maxSize, 1);
    limits->addWidget(row.maxHp, 1);
    form->addRow(tr("Targets"), limits);
    form->addRow(tr("Grapple"), row.escapeDc);
    row.exceptTypes = new QLineEdit(joinList(rider.exceptTypes));
    row.exceptTypes->setPlaceholderText(tr("Every creature (or: undead, elf)"));
    form->addRow(tr("Not these types or species"), row.exceptTypes);

    auto* ask = new QHBoxLayout;
    row.ask = new QLineEdit(rider.ask == "@advantage" ? QString() : QString::fromStdString(rider.ask));
    row.ask->setObjectName(QStringLiteral("riderAsk"));
    row.ask->setPlaceholderText(tr("Always (or a question: the boar moved 20+ feet toward it)"));
    row.ifAdvantage = new QCheckBox(tr("Only if the attack had Advantage"));
    row.ifAdvantage->setChecked(rider.ask == "@advantage");
    ask->addWidget(row.ask, 1);
    form->addRow(tr("Only if"), ask);
    form->addRow(QString(), row.ifAdvantage);
    row.refund = new QCheckBox(tr("Instead of the damage (a yes gives it back)"));
    row.refund->setChecked(rider.refundDamage);
    form->addRow(QString(), row.refund);

    auto* ongoing = new QHBoxLayout;
    row.ongoing = new QLineEdit(QString::fromStdString(formatDamageParts(rider.ongoing)));
    row.ongoing->setPlaceholderText(tr("No ongoing damage (or: 3d6 acid)"));
    row.ongoingAt = new QComboBox;
    row.ongoingAt->addItem(tr("At the start of each of the target's turns"), QString::fromLatin1(kOngoingAtTarget));
    row.ongoingAt->addItem(tr("At the start of each of the monster's turns"), QString::fromLatin1(kOngoingAtSource));
    row.ongoingAt->setCurrentIndex(rider.ongoingAt == kOngoingAtSource ? 1 : 0);
    ongoing->addWidget(row.ongoing, 1);
    form->addRow(tr("Ongoing damage"), ongoing);
    form->addRow(QString(), row.ongoingAt);

    row.endsGrapple = new QCheckBox(tr("Ends the monster's grapple on it (swallowed)"));
    row.endsGrapple->setChecked(has(rider.removes, "grappled"));
    row.stabilize = new QCheckBox(tr("Makes a dying target Stable"));
    row.stabilize->setChecked(rider.stabilize);
    row.concentrationDisadvantage = new QCheckBox(tr("Disadvantage on saving throws to maintain Concentration"));
    row.concentrationDisadvantage->setChecked(rider.concentrationDisadvantage);
    form->addRow(QString(), row.endsGrapple);
    form->addRow(QString(), row.stabilize);
    form->addRow(QString(), row.concentrationDisadvantage);

    m_riderLayout->addWidget(box);
    m_riders.push_back(row);
    connect(remove, &QPushButton::clicked, this, [this, box] {
        for (auto it = m_riders.begin(); it != m_riders.end(); ++it) {
            if (it->box == box) {
                m_riders.erase(it);
                break;
            }
        }
        box->deleteLater();
    });
}

ConditionRider EffectsDialog::readRider(const RiderRow& row) const
{
    ConditionRider rider;
    for (const QCheckBox* check : row.conditions) {
        if (check->isChecked()) {
            rider.conditions.push_back(check->property("condition").toString().toStdString());
        }
    }
    rider.on = row.when->currentData().toString().toStdString();
    rider.targetMaxSize = row.maxSize->currentData().toString().toStdString();
    if (row.maxHp->value() > 0) {
        rider.targetMaxHp = row.maxHp->value();
    }
    for (const std::string& type : splitList(row.exceptTypes->text())) {
        rider.exceptTypes.push_back(asciiLower(type));
    }
    rider.ask = row.ifAdvantage->isChecked() ? std::string("@advantage") : row.ask->text().trimmed().toStdString();
    rider.refundDamage = row.refund->isChecked();
    if (row.escapeDc->value() > 0) {
        rider.escapeDc = row.escapeDc->value();
    }
    rider.until = row.until->currentData().toString().toStdString();
    rider.tiedTo = row.tiedTo->currentData().toString().toStdString();
    rider.saveEnds = row.saveEnds->isChecked();
    const std::string worse = row.worsensTo->currentData().toString().toStdString();
    if (!worse.empty()) {
        rider.worsensTo = {worse};
        rider.worseSaveEnds = row.worseSaveEnds->isChecked();
        if (row.worseEndsOnDamage->isChecked()) {
            rider.worseEndsOn = {kEndsOnTakesDamage};
        }
        rider.saveEnds = true;  // the repeat save is what worsens it
    }
    if (row.endsOnDamage->isChecked()) {
        rider.endsOn.push_back(kEndsOnTakesDamage);
    }
    if (row.endsWithMonster->isChecked()) {
        rider.endsOn.push_back(kEndsOnSourceGone);
    }
    if (const auto parts = parseDamageParts(row.ongoing->text().toStdString()); parts.has_value() && !parts->empty()) {
        rider.ongoing = *parts;
        for (DamagePart& part : rider.ongoing) {
            part.when = DamageWhen::Always;
        }
        rider.ongoingAt = row.ongoingAt->currentData().toString().toStdString();
    }
    if (row.endsGrapple->isChecked()) {
        rider.removes = {"grappled"};
    }
    rider.stabilize = row.stabilize->isChecked();
    rider.concentrationDisadvantage = row.concentrationDisadvantage->isChecked();
    return rider;
}

void EffectsDialog::readInto(MonsterAttack& attack) const
{
    if (m_hasAttackRoll != nullptr) {
        attack.attackBonus = m_hasAttackRoll->isChecked() ? std::optional<int>(m_attackBonus->value()) : std::nullopt;
        const std::optional<Ability> ability = abilityOf(m_saveAbility);
        attack.save = ability.has_value() ? std::optional(SaveSpec{*ability, m_saveDc->value(), m_half->isChecked()})
                                          : std::nullopt;
        attack.damage = parseDamageParts(m_damage->text().toStdString()).value_or(attack.damage);
        attack.area = m_area->isChecked();
    }
    if (m_targetCondition != nullptr) {
        attack.targetCondition = asciiLower(m_targetCondition->text().trimmed().toStdString());
        attack.targetMaxSize = m_targetMaxSize->currentData().toString().toStdString();
        if (m_hpThreshold->value() > 0) {
            attack.failureHpThreshold = m_hpThreshold->value();
            attack.failureHpEffect = m_hpEffect->currentData().toString().toStdString();
        } else {
            attack.failureHpThreshold.reset();
            attack.failureHpEffect.clear();
        }
        attack.advantageIfGrappled = m_advantageIfGrappled->isChecked();
        attack.riderSave = m_riderSave->isChecked()
                               ? std::optional(SaveSpec{abilityOf(m_riderSaveAbility).value_or(Ability::Constitution),
                                                        m_riderSaveDc->value(), false})
                               : std::nullopt;
        attack.riders.clear();
        for (const RiderRow& row : m_riders) {
            ConditionRider rider = readRider(row);
            if (!rider.conditions.empty() || rider.concentrationDisadvantage) {
                attack.riders.push_back(std::move(rider));
            }
        }
        if (m_benefit->isChecked()) {
            Benefit benefit;
            const auto dice = [](const QLineEdit* edit) {
                const std::string text = edit->text().trimmed().toStdString();
                const std::optional<Dice> parsed = parseDice(text);
                return parsed.has_value() ? formatDice(*parsed) : std::string();
            };
            benefit.tempHp = dice(m_benefitTempHp);
            benefit.healing = dice(m_benefitHealing);
            benefit.advantageOnAttacks = m_benefitAdvantage->isChecked();
            benefit.acBonus = m_benefitAc->value();
            benefit.until = m_benefitUntil->currentData().toString().toStdString();
            attack.benefit = benefit;
        } else {
            attack.benefit.reset();
        }
        const QString drain = m_drain->currentData().toString();
        if (drain == QStringLiteral("-")) {
            attack.drain.reset();
        } else {
            attack.drain = HpDrain{drain.toStdString(), m_drainHeals->isChecked()};
        }
    }
    if (m_self->isChecked() && !m_selfCondition->currentData().toString().isEmpty()) {
        SelfEffect self;
        self.condition = m_selfCondition->currentData().toString().toStdString();
        self.source = m_selfSource->text().trimmed().isEmpty() ? abilityDisplayName(attack.name)
                                                              : m_selfSource->text().trimmed().toStdString();
        self.concentration = m_selfConcentration->text().trimmed().toStdString();
        for (const QCheckBox* box : m_selfEnds) {
            if (box->isChecked()) {
                self.endsOn.push_back(box->property("event").toString().toStdString());
            }
        }
        for (const std::string& action : splitList(m_selfEndsActions->text())) {
            self.endsOn.push_back(kEndsOnActionPrefix + action);
        }
        attack.selfEffect = self;
    } else {
        attack.selfEffect.reset();
    }
}

MonsterAttack EffectsDialog::attack() const
{
    MonsterAttack out = m_attack;
    readInto(out);
    return out;
}

MonsterFeature EffectsDialog::feature() const
{
    MonsterFeature out = m_feature;
    if (m_afterBloodied != nullptr) {
        out.afterDamagingBloodied = m_afterBloodied->isChecked();
    }
    MonsterAttack aimed = m_attack;
    readInto(aimed);
    out.selfEffect = aimed.selfEffect;
    aimed.selfEffect.reset();
    // Help is always aimed: you pick who gets it.
    if ((m_targeted != nullptr && m_targeted->isChecked()) || aimed.benefit.has_value()) {
        aimed.name = out.name;
        aimed.effect = out.effect;
        aimed.recharge.reset();
        aimed.perDay.reset();
        out.targeted = aimed;
    } else {
        out.targeted.reset();
    }
    if (m_modifier != nullptr) {
        if (m_modifier->isChecked()) {
            AttackModifier modifier;
            modifier.advantage = m_modifierMode->currentData().toBool();
            modifier.when = m_modifierWhen->currentData().toString().toStdString();
            if (modifier.when == kModifierAfterDamage) {
                modifier.damageType = m_modifierType->currentData().toString().toStdString();
            }
            if (modifier.when.empty()) {
                modifier.ask = m_modifierAsk->text().trimmed().toStdString();
                modifier.sticky = m_modifierSticky->isChecked();
            }
            modifier.meleeOnly = m_modifierMelee->isChecked();
            modifier.alliesToo = m_modifierAllies->isChecked();
            modifier.whileActive = m_modifierActive->isChecked();
            out.attackModifier = modifier;
        } else {
            out.attackModifier.reset();
        }
    }
    if (m_aura != nullptr) {
        if (m_aura->isChecked()) {
            AuraSave aura = out.aura.value_or(AuraSave{});
            aura.ability = abilityOf(m_auraAbility).value_or(Ability::Wisdom);
            aura.dc = m_auraDc->value();
            aura.range = m_auraRange->text().trimmed().toStdString();
            aura.who = m_auraWho->text().trimmed().toStdString();
            aura.condition = m_auraCondition->currentData().toString().toStdString();
            aura.immuneOnSuccess = m_auraImmune->isChecked();
            aura.whileActive = m_auraWhileActive->isChecked();
            aura.enemiesOnly = m_auraEnemies->isChecked();
            aura.creatureTypes.clear();
            for (const std::string& type : splitList(m_auraTypes->text())) {
                aura.creatureTypes.push_back(asciiLower(type));
            }
            aura.suppressedBy = splitList(m_auraSuppressed->text());
            out.aura = aura;
        } else {
            out.aura.reset();
        }
    }
    return out;
}

void EffectsDialog::updateEnabled()
{
    if (m_targeted != nullptr) {
        m_rollBox->setEnabled(m_targeted->isChecked());
        m_attackBonus->setEnabled(m_hasAttackRoll->isChecked());
        const bool save = abilityOf(m_saveAbility).has_value();
        m_saveDc->setEnabled(save);
        m_half->setEnabled(save);
    }
    if (m_riderSave != nullptr) {
        m_riderSaveAbility->setEnabled(m_riderSave->isChecked());
        m_riderSaveDc->setEnabled(m_riderSave->isChecked());
    }
    if (m_drain != nullptr) {
        m_drainHeals->setEnabled(m_drain->currentData().toString() != QStringLiteral("-"));
    }
    if (m_benefit != nullptr) {
        for (QWidget* widget : std::initializer_list<QWidget*>{m_benefitTempHp, m_benefitHealing, m_benefitAdvantage,
                                                              m_benefitAc, m_benefitUntil}) {
            widget->setEnabled(m_benefit->isChecked());
        }
    }
    const bool self = m_self->isChecked();
    m_selfCondition->setEnabled(self);
    m_selfSource->setEnabled(self);
    m_selfConcentration->setEnabled(self);
    m_selfEndsActions->setEnabled(self);
    for (QCheckBox* box : m_selfEnds) {
        box->setEnabled(self);
    }
    if (m_modifier != nullptr) {
        const bool on = m_modifier->isChecked();
        const QString when = m_modifierWhen->currentData().toString();
        for (QWidget* widget : std::initializer_list<QWidget*>{m_modifierMode, m_modifierWhen, m_modifierMelee,
                                                              m_modifierAllies, m_modifierActive}) {
            widget->setEnabled(on);
        }
        m_modifierType->setEnabled(on && when == QString::fromLatin1(kModifierAfterDamage));
        m_modifierAsk->setEnabled(on && when.isEmpty());
        m_modifierSticky->setEnabled(on && when.isEmpty());
    }
    if (m_aura != nullptr) {
        const bool aura = m_aura->isChecked();
        for (QWidget* widget : std::initializer_list<QWidget*>{m_auraAbility, m_auraDc, m_auraRange, m_auraWho,
                                                              m_auraCondition, m_auraImmune, m_auraWhileActive,
                                                              m_auraEnemies, m_auraTypes, m_auraSuppressed}) {
            widget->setEnabled(aura);
        }
    }
}

}  // namespace combat::ui
