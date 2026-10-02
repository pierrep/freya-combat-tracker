#include "ui/combat_page.h"

#include "core/character_store.h"
#include "core/combat_rules.h"
#include "core/encounter_store.h"
#include "core/monster_catalog.h"
#include "ui/page_title.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <limits>
#include <optional>

namespace combat::ui {

namespace {

QSpinBox* makeNumberBox()
{
    auto* box = new QSpinBox;
    box->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    box->setMaximumWidth(140);
    return box;
}

}  // namespace

CombatPage::CombatPage(CharacterStore& characters, MonsterCatalog& catalog, EncounterStore& encounters,
                       std::vector<Spell> spells, std::vector<Condition> conditions, QWidget* parent)
    : QWidget(parent)
    , m_charactersStore(characters)
    , m_catalog(catalog)
    , m_encountersStore(encounters)
    , m_spells(std::move(spells))
    , m_conditions(std::move(conditions))
    , m_dice(std::random_device{}())
{
    try {
        m_encounters = m_encountersStore.loadAll();
    } catch (const EncounterStoreError& error) {
        m_loadError = QString::fromStdString(error.what());
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 24, 32, 24);
    outer->setSpacing(16);
    outer->addWidget(makePageTitle(tr("Dashboard")));

    if (hasLoadError()) {
        auto* banner = new QLabel(tr("The encounters file could not be read, so it has not been changed.\n%1")
                                      .arg(m_loadError));
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
        outer->addWidget(banner);
    }

    auto* encounterForm = new QFormLayout;
    m_encounterCombo = new QComboBox;
    m_encounterCombo->setObjectName(QStringLiteral("encounterCombo"));
    encounterForm->addRow(tr("Encounter"), m_encounterCombo);
    outer->addLayout(encounterForm);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* rightHost = new QWidget;
    auto* right = new QVBoxLayout(rightHost);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(12);
    scroll->setWidget(rightHost);
    outer->addWidget(scroll, 1);

    m_emptyHint = new QLabel(tr("Choose an encounter to run the fight. Create encounters in Encounter Builder."));
    m_emptyHint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(m_emptyHint);

    m_fight = new QWidget;
    m_fight->setObjectName(QStringLiteral("combatFight"));
    auto* fightLayout = new QVBoxLayout(m_fight);
    fightLayout->setContentsMargins(0, 0, 0, 0);
    fightLayout->setSpacing(12);

    auto* hpNote = new QLabel(tr("Hit points, temporary HP, conditions, concentration, and death saves in this fight "
                                 "are a separate copy. Spell slots and Finish rest are saved on the character."));
    hpNote->setObjectName(QStringLiteral("fightHpNote"));
    hpNote->setWordWrap(true);
    fightLayout->addWidget(hpNote);

    m_roundLabel = new QLabel;
    m_roundLabel->setObjectName(QStringLiteral("roundLabel"));
    m_activeLabel = new QLabel;
    m_activeLabel->setObjectName(QStringLiteral("activeCombatant"));
    QFont activeFont = m_activeLabel->font();
    activeFont.setPointSizeF(activeFont.pointSizeF() * 1.25);
    activeFont.setBold(true);
    m_activeLabel->setFont(activeFont);
    auto* turnRow = new QHBoxLayout;
    turnRow->addWidget(m_roundLabel);
    turnRow->addWidget(m_activeLabel, 1);
    fightLayout->addLayout(turnRow);

    auto* turnButtons = new QHBoxLayout;
    m_previousTurnButton = new QPushButton(tr("Previous turn"));
    m_previousTurnButton->setObjectName(QStringLiteral("previousTurn"));
    m_nextTurnButton = new QPushButton(tr("Next turn"));
    m_nextTurnButton->setObjectName(QStringLiteral("nextTurn"));
    m_nextRoundButton = new QPushButton(tr("Next round"));
    m_nextRoundButton->setObjectName(QStringLiteral("nextRound"));
    turnButtons->addWidget(m_previousTurnButton);
    turnButtons->addWidget(m_nextTurnButton);
    turnButtons->addWidget(m_nextRoundButton);
    turnButtons->addStretch(1);
    fightLayout->addLayout(turnButtons);

    m_rollAllButton = new QPushButton(tr("Roll initiative for all monsters"));
    m_rollAllButton->setObjectName(QStringLiteral("rollAllMonsters"));
    fightLayout->addWidget(m_rollAllButton, 0, Qt::AlignLeft);
    m_rollNote = new QLabel;
    m_rollNote->setObjectName(QStringLiteral("rollNote"));
    m_rollNote->setWordWrap(true);
    m_rollNote->hide();
    fightLayout->addWidget(m_rollNote);

    m_combatantList = new QListWidget;
    m_combatantList->setObjectName(QStringLiteral("combatantList"));
    m_combatantList->setMinimumHeight(140);
    fightLayout->addWidget(m_combatantList);

    m_noCombatantHint = new QLabel(tr("No one is in this fight yet. Add characters and monsters in Encounter Builder."));
    fightLayout->addWidget(m_noCombatantHint);

    m_combatantForm = new QWidget;
    auto* form = new QFormLayout(m_combatantForm);

    m_damageAmount = makeNumberBox();
    m_damageAmount->setRange(0, std::numeric_limits<int>::max());
    m_damageAmount->setValue(0);
    m_damageButton = new QPushButton(tr("Apply damage to the selected combatant"));
    m_damageButton->setObjectName(QStringLiteral("applyDamage"));
    auto* damageRow = new QHBoxLayout;
    damageRow->addWidget(m_damageAmount);
    damageRow->addWidget(m_damageButton);
    damageRow->addStretch(1);
    form->addRow(tr("Damage"), damageRow);

    m_healAmount = makeNumberBox();
    m_healAmount->setRange(0, std::numeric_limits<int>::max());
    m_healAmount->setValue(0);
    m_healButton = new QPushButton(tr("Apply healing to the selected combatant"));
    m_healButton->setObjectName(QStringLiteral("applyHealing"));
    auto* healRow = new QHBoxLayout;
    healRow->addWidget(m_healAmount);
    healRow->addWidget(m_healButton);
    healRow->addStretch(1);
    form->addRow(tr("Healing"), healRow);

    auto* divider = new QFrame;
    divider->setObjectName(QStringLiteral("combatantDivider"));
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);
    form->addRow(divider);

    m_combatantName = new QLabel;
    m_combatantName->setObjectName(QStringLiteral("combatantName"));
    form->addRow(tr("Name"), m_combatantName);
    m_combatantSource = new QLabel;
    form->addRow(tr("From"), m_combatantSource);

    m_attacksSection = new QWidget;
    m_attacksSection->setObjectName(QStringLiteral("combatantAttacks"));
    auto* attacksLayout = new QVBoxLayout(m_attacksSection);
    attacksLayout->setContentsMargins(0, 0, 0, 0);
    attacksLayout->setSpacing(4);
    auto* attacksHeading = new QLabel(tr("Attacks"));
    QFont attacksFont = attacksHeading->font();
    attacksFont.setBold(true);
    attacksHeading->setFont(attacksFont);
    m_attacks = new QLabel;
    m_attacks->setWordWrap(true);
    m_attacks->setTextInteractionFlags(Qt::TextSelectableByMouse);
    attacksLayout->addWidget(attacksHeading);
    attacksLayout->addWidget(m_attacks);
    form->addRow(m_attacksSection);

    m_initiative = makeNumberBox();
    m_initiative->setObjectName(QStringLiteral("initiativeField"));
    form->addRow(tr("Initiative"), m_initiative);
    m_bonusLabel = new QLabel;
    m_bonusLabel->setWordWrap(true);
    form->addRow(tr("Bonus"), m_bonusLabel);
    m_rerollButton = new QPushButton(tr("Reroll this monster"));
    m_rerollButton->setObjectName(QStringLiteral("rerollMonster"));
    form->addRow(QString(), m_rerollButton);
    m_acLabel = new QLabel;
    m_acLabel->setObjectName(QStringLiteral("acField"));
    form->addRow(tr("AC"), m_acLabel);
    m_hp = makeNumberBox();
    m_hp->setObjectName(QStringLiteral("hpField"));
    m_maxHpLabel = new QLabel;
    m_maxHpLabel->setObjectName(QStringLiteral("maxHpField"));
    auto* hpRow = new QHBoxLayout;
    hpRow->setSpacing(0);
    hpRow->addWidget(m_hp);
    hpRow->addWidget(m_maxHpLabel);
    hpRow->addStretch(1);
    form->addRow(tr("HP"), hpRow);
    m_tempHp = makeNumberBox();
    m_tempHp->setObjectName(QStringLiteral("tempHpField"));
    form->addRow(tr("Temporary HP"), m_tempHp);

    m_conditionPicker = new QComboBox;
    m_conditionPicker->setObjectName(QStringLiteral("conditionPicker"));
    for (const Condition& condition : searchConditions(m_conditions, "")) {
        m_conditionPicker->addItem(QString::fromStdString(condition.name), QString::fromStdString(condition.id));
    }
    auto* addConditionButton = new QPushButton(tr("Add condition"));
    addConditionButton->setObjectName(QStringLiteral("addCondition"));
    // The picker lists the longest SRD names. Keeping the button on that same
    // row crushes it at the default window width, so it sits on the next row.
    form->addRow(tr("Condition"), m_conditionPicker);
    form->addRow(QString(), addConditionButton);
    m_conditionList = new QListWidget;
    m_conditionList->setObjectName(QStringLiteral("combatantConditions"));
    m_conditionList->setMaximumHeight(100);
    form->addRow(QString(), m_conditionList);
    auto* removeConditionButton = new QPushButton(tr("Remove condition"));
    form->addRow(QString(), removeConditionButton);
    m_conditionText = new QLabel;
    m_conditionText->setObjectName(QStringLiteral("conditionText"));
    m_conditionText->setWordWrap(true);
    m_conditionText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QString(), m_conditionText);

    m_concentrationLabel = new QLabel;
    m_concentrationLabel->setObjectName(QStringLiteral("concentrationLabel"));
    m_concentrationLabel->setWordWrap(true);
    form->addRow(tr("Concentration"), m_concentrationLabel);
    m_spellSearch = new QLineEdit;
    m_spellSearch->setObjectName(QStringLiteral("concentrationSearch"));
    m_spellSearch->setPlaceholderText(tr("Search SRD spells"));
    form->addRow(QString(), m_spellSearch);
    m_spellMatches = new QListWidget;
    m_spellMatches->setObjectName(QStringLiteral("concentrationMatches"));
    m_spellMatches->setMaximumHeight(100);
    form->addRow(QString(), m_spellMatches);
    auto* setConcentrationButton = new QPushButton(tr("Set concentration"));
    setConcentrationButton->setObjectName(QStringLiteral("setConcentration"));
    auto* clearConcentrationButton = new QPushButton(tr("Clear concentration"));
    clearConcentrationButton->setObjectName(QStringLiteral("clearConcentration"));
    auto* concentrationButtons = new QHBoxLayout;
    concentrationButtons->addWidget(setConcentrationButton);
    concentrationButtons->addWidget(clearConcentrationButton);
    concentrationButtons->addStretch(1);
    form->addRow(QString(), concentrationButtons);

    m_deathSuccessLabel = new QLabel(QStringLiteral("0"));
    m_deathFailureLabel = new QLabel(QStringLiteral("0"));
    auto* deathRow = new QHBoxLayout;
    auto* successUp = new QPushButton(tr("Success +"));
    auto* successDown = new QPushButton(tr("Success -"));
    auto* failureUp = new QPushButton(tr("Failure +"));
    auto* failureDown = new QPushButton(tr("Failure -"));
    successUp->setObjectName(QStringLiteral("deathSuccessUp"));
    failureUp->setObjectName(QStringLiteral("deathFailureUp"));
    deathRow->addWidget(successDown);
    deathRow->addWidget(m_deathSuccessLabel);
    deathRow->addWidget(successUp);
    deathRow->addSpacing(16);
    deathRow->addWidget(failureDown);
    deathRow->addWidget(m_deathFailureLabel);
    deathRow->addWidget(failureUp);
    deathRow->addStretch(1);
    form->addRow(tr("Death saves"), deathRow);

    m_derivedLabel = new QLabel;
    m_derivedLabel->setObjectName(QStringLiteral("derivedModifiers"));
    m_derivedLabel->setWordWrap(true);
    form->addRow(tr("From the sheet"), m_derivedLabel);

    m_slotHost = new QWidget;
    m_slotLayout = new QVBoxLayout(m_slotHost);
    m_slotLayout->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Spell slots"), m_slotHost);
    m_restButton = new QPushButton(tr("Finish rest"));
    m_restButton->setObjectName(QStringLiteral("finishRest"));
    form->addRow(QString(), m_restButton);

    fightLayout->addWidget(m_combatantForm);

    auto* orderButtons = new QHBoxLayout;
    m_moveUpButton = new QPushButton(tr("Move up"));
    m_moveDownButton = new QPushButton(tr("Move down"));
    m_removeButton = new QPushButton(tr("Remove"));
    orderButtons->addWidget(m_moveUpButton);
    orderButtons->addWidget(m_moveDownButton);
    orderButtons->addWidget(m_removeButton);
    orderButtons->addStretch(1);
    fightLayout->addLayout(orderButtons);
    fightLayout->addStretch(1);

    right->addWidget(m_fight, 1);

    connect(m_encounterCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &CombatPage::showEncounter);
    connect(m_combatantList, &QListWidget::currentRowChanged, this, &CombatPage::showCombatant);
    connect(m_initiative, &QSpinBox::valueChanged, this, &CombatPage::onInitiativeChanged);
    connect(m_initiative, &QSpinBox::editingFinished, this, &CombatPage::onInitiativeEditingFinished);
    connect(m_hp, &QSpinBox::valueChanged, this, &CombatPage::onHpChanged);
    connect(m_tempHp, &QSpinBox::valueChanged, this, &CombatPage::onTempHpChanged);
    connect(m_damageButton, &QPushButton::clicked, this, &CombatPage::applySelectedDamage);
    connect(m_healButton, &QPushButton::clicked, this, &CombatPage::applySelectedHealing);
    connect(addConditionButton, &QPushButton::clicked, this, &CombatPage::addSelectedCondition);
    connect(removeConditionButton, &QPushButton::clicked, this, &CombatPage::removeListedCondition);
    connect(m_conditionList, &QListWidget::currentRowChanged, this, &CombatPage::showConditionText);
    connect(m_spellSearch, &QLineEdit::textChanged, this, &CombatPage::refreshConcentrationChoices);
    connect(setConcentrationButton, &QPushButton::clicked, this, &CombatPage::setSelectedConcentration);
    connect(clearConcentrationButton, &QPushButton::clicked, this, &CombatPage::clearSelectedConcentration);
    connect(successUp, &QPushButton::clicked, this, [this] { adjustSelectedDeathSave(true, 1); });
    connect(successDown, &QPushButton::clicked, this, [this] { adjustSelectedDeathSave(true, -1); });
    connect(failureUp, &QPushButton::clicked, this, [this] { adjustSelectedDeathSave(false, 1); });
    connect(failureDown, &QPushButton::clicked, this, [this] { adjustSelectedDeathSave(false, -1); });
    connect(m_restButton, &QPushButton::clicked, this, &CombatPage::restSelectedCharacter);
    connect(m_rollAllButton, &QPushButton::clicked, this, &CombatPage::rollAll);
    connect(m_rerollButton, &QPushButton::clicked, this, &CombatPage::rerollSelected);
    connect(m_moveUpButton, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_moveDownButton, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(m_removeButton, &QPushButton::clicked, this, &CombatPage::removeSelected);
    connect(m_previousTurnButton, &QPushButton::clicked, this, &CombatPage::previousTurn);
    connect(m_nextTurnButton, &QPushButton::clicked, this, &CombatPage::nextTurn);
    connect(m_nextRoundButton, &QPushButton::clicked, this, &CombatPage::nextRound);

    refreshConcentrationChoices(QString());

    if (hasLoadError()) {
        m_encounterCombo->setEnabled(false);
        m_emptyHint->hide();
        m_fight->hide();
        return;
    }

    reloadCharacters();
    reloadEncounters();
}

void CombatPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!hasLoadError()) {
        reloadCharacters();
        reloadEncounters();
    }
    if (m_reportedLoadError || !hasLoadError()) {
        return;
    }
    m_reportedLoadError = true;
    QTimer::singleShot(0, this, [this] {
        QMessageBox::critical(this, tr("Could not read encounters"),
                              tr("%1\n\nThe file has been left unchanged. Encounters cannot be edited until it "
                                 "is fixed or moved aside.")
                                  .arg(m_loadError));
    });
}

void CombatPage::reloadEncounters()
{
    if (hasLoadError() || m_encounterCombo == nullptr) {
        return;
    }
    const QString selectedId = m_encounterCombo->currentData().toString();
    try {
        m_encounters = m_encountersStore.loadAll();
    } catch (const EncounterStoreError& error) {
        QMessageBox::warning(this, tr("Could not read encounters"), QString::fromStdString(error.what()));
        return;
    }
    int select = -1;
    {
        const QSignalBlocker blocker(m_encounterCombo);
        m_encounterCombo->clear();
        for (int i = 0; i < static_cast<int>(m_encounters.size()); ++i) {
            const Encounter& encounter = m_encounters[static_cast<std::size_t>(i)];
            const QString id = QString::fromStdString(encounter.id);
            m_encounterCombo->addItem(QString::fromStdString(encounter.name), id);
            if (!selectedId.isEmpty() && id == selectedId) {
                select = i;
            }
        }
        if (select < 0 && !m_encounters.empty()) {
            select = 0;
        }
        if (select >= 0) {
            m_encounterCombo->setCurrentIndex(select);
        }
    }
    showEncounter();
}

void CombatPage::reloadCharacters()
{
    try {
        m_characters = m_charactersStore.loadAll();
    } catch (const CharacterStoreError&) {
        return;
    }
}

int CombatPage::rollD20()
{
    std::uniform_int_distribution<int> face(1, 20);
    return face(m_dice);
}

Encounter* CombatPage::selectedEncounter()
{
    if (m_encounterCombo == nullptr) {
        return nullptr;
    }
    const int row = m_encounterCombo->currentIndex();
    if (row < 0 || row >= static_cast<int>(m_encounters.size())) {
        return nullptr;
    }
    return &m_encounters[static_cast<std::size_t>(row)];
}

Combatant* CombatPage::selectedCombatant()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || m_combatantList == nullptr) {
        return nullptr;
    }
    const int row = m_combatantList->currentRow();
    if (row < 0 || row >= static_cast<int>(encounter->combatants.size())) {
        return nullptr;
    }
    return &encounter->combatants[static_cast<std::size_t>(row)];
}

QString CombatPage::combatantLabel(const Combatant& combatant, bool active) const
{
    const QString marker = active ? QStringLiteral("● ") : QStringLiteral("   ");
    QString label = marker + QString::number(combatant.initiative) + QStringLiteral("    ") +
                    QString::fromStdString(combatant.name) + QStringLiteral("    AC ") +
                    QString::number(combatant.ac) + QStringLiteral("    HP ") +
                    QString::fromStdString(formatHitPoints(combatant.hp, combatant.maxHp));
    if (combatant.tempHp != 0) {
        label += tr("    temp %1").arg(combatant.tempHp);
    }
    return label;
}

void CombatPage::updateCombatantItemText(int row)
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || row < 0 || row >= m_combatantList->count()) {
        return;
    }
    const bool active = row == encounter->turnIndex;
    m_combatantList->item(row)->setText(combatantLabel(encounter->combatants[static_cast<std::size_t>(row)], active));
}

void CombatPage::updateTurnLabels()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    m_roundLabel->setText(tr("Round %1").arg(encounter->round));
    const bool hasCombatants = !encounter->combatants.empty();
    if (!hasCombatants) {
        m_activeLabel->setText(tr("No one is in this fight yet."));
    } else {
        const int index = encounter->turnIndex;
        const QString name =
            QString::fromStdString(encounter->combatants[static_cast<std::size_t>(index)].name);
        m_activeLabel->setText(tr("%1's turn").arg(name));
    }
    m_previousTurnButton->setEnabled(hasCombatants);
    m_nextTurnButton->setEnabled(hasCombatants);
    m_nextRoundButton->setEnabled(hasCombatants);

    bool anyMonster = false;
    for (const Combatant& combatant : encounter->combatants) {
        if (isMonsterCombatant(combatant)) {
            anyMonster = true;
            break;
        }
    }
    m_rollAllButton->setEnabled(anyMonster);
}

void CombatPage::persist()
{
    if (hasLoadError()) {
        return;
    }
    try {
        m_encountersStore.saveAll(m_encounters);
    } catch (const EncounterStoreError& error) {
        QMessageBox::warning(this, tr("Could not save encounters"), QString::fromStdString(error.what()));
    }
}

void CombatPage::showEncounter()
{
    Encounter* encounter = selectedEncounter();
    m_fight->setVisible(encounter != nullptr);
    m_emptyHint->setVisible(encounter == nullptr && !hasLoadError());
    if (encounter == nullptr) {
        return;
    }
    m_rollNote->hide();
    m_rollNote->clear();
    rebuildCombatantList({});
}

void CombatPage::rebuildCombatantList(const std::string& selectId)
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    int selectRow = encounter->combatants.empty() ? -1 : encounter->turnIndex;
    if (!selectId.empty()) {
        for (int i = 0; i < static_cast<int>(encounter->combatants.size()); ++i) {
            if (encounter->combatants[static_cast<std::size_t>(i)].id == selectId) {
                selectRow = i;
                break;
            }
        }
    }
    m_populating = true;
    {
        QSignalBlocker blocker(m_combatantList);
        m_combatantList->clear();
        for (int i = 0; i < static_cast<int>(encounter->combatants.size()); ++i) {
            const Combatant& combatant = encounter->combatants[static_cast<std::size_t>(i)];
            m_combatantList->addItem(combatantLabel(combatant, i == encounter->turnIndex));
        }
        if (selectRow >= 0) {
            m_combatantList->setCurrentRow(selectRow);
        }
    }
    m_populating = false;
    updateTurnLabels();
    showCombatant();
}

void CombatPage::showCombatant()
{
    if (m_populating) {
        return;
    }
    const Combatant* combatant = selectedCombatant();
    const Encounter* encounter = selectedEncounter();
    m_combatantForm->setVisible(combatant != nullptr);
    m_noCombatantHint->setVisible(encounter != nullptr && combatant == nullptr);
    const int row = m_combatantList->currentRow();
    const int count = encounter == nullptr ? 0 : static_cast<int>(encounter->combatants.size());
    m_moveUpButton->setEnabled(combatant != nullptr && row > 0);
    m_moveDownButton->setEnabled(combatant != nullptr && row >= 0 && row + 1 < count);
    m_removeButton->setEnabled(combatant != nullptr);
    if (combatant == nullptr) {
        m_rerollButton->setEnabled(false);
        showAttacks();
        return;
    }

    m_populating = true;
    m_combatantName->setText(QString::fromStdString(combatant->name));
    if (isMonsterCombatant(*combatant)) {
        m_combatantSource->setText(tr("Monster"));
        m_rerollButton->setEnabled(true);
        if (combatant->initiativeBonus.has_value()) {
            m_bonusLabel->setText(tr("Initiative bonus %1")
                                      .arg(QString::fromStdString(formatModifier(*combatant->initiativeBonus))));
        } else {
            m_bonusLabel->setText(tr("The initiative bonus was missing. Rolls use +0."));
        }
    } else {
        m_combatantSource->setText(tr("Character"));
        m_rerollButton->setEnabled(false);
        m_bonusLabel->setText(tr("Type this character's initiative. Characters are not rolled."));
    }
    m_initiative->setValue(combatant->initiative);
    m_acLabel->setText(QString::number(combatant->ac));
    m_hp->setValue(combatant->hp);
    m_tempHp->setValue(combatant->tempHp);
    if (combatant->maxHp.has_value()) {
        m_maxHpLabel->setText(QStringLiteral(" / %1").arg(*combatant->maxHp));
        m_maxHpLabel->setVisible(true);
    } else {
        m_maxHpLabel->clear();
        m_maxHpLabel->setVisible(false);
    }
    {
        const QSignalBlocker blocker(m_conditionList);
        m_conditionList->clear();
        for (const std::string& id : combatant->conditions) {
            const std::optional<Condition> condition = findConditionById(m_conditions, id);
            const QString name =
                condition.has_value() ? QString::fromStdString(condition->name) : QString::fromStdString(id);
            auto* item = new QListWidgetItem(name);
            item->setData(Qt::UserRole, QString::fromStdString(id));
            m_conditionList->addItem(item);
        }
        if (m_conditionList->count() > 0) {
            m_conditionList->setCurrentRow(0);
        }
    }
    if (combatant->concentration.empty()) {
        m_concentrationLabel->setText(tr("Not concentrating."));
    } else {
        const std::optional<Spell> spell = findSpellById(m_spells, combatant->concentration);
        if (spell.has_value()) {
            m_concentrationLabel->setText(QString::fromStdString(spell->name));
        } else {
            m_concentrationLabel->setText(tr("No SRD spell is stored for %1.")
                                              .arg(QString::fromStdString(combatant->concentration)));
        }
    }
    m_deathSuccessLabel->setText(QString::number(combatant->deathSaves.successes));
    m_deathFailureLabel->setText(QString::number(combatant->deathSaves.failures));
    m_populating = false;
    showAttacks();
    showConditionText();
    updateDerivedModifiers();
    rebuildSlotButtons();
}

void CombatPage::onInitiativeChanged(int value)
{
    Combatant* combatant = selectedCombatant();
    if (m_populating || combatant == nullptr) {
        return;
    }
    combatant->initiative = value;
    updateCombatantItemText(m_combatantList->currentRow());
    persist();
}

void CombatPage::onInitiativeEditingFinished()
{
    Encounter* encounter = selectedEncounter();
    Combatant* combatant = selectedCombatant();
    if (m_populating || encounter == nullptr || combatant == nullptr) {
        return;
    }
    const std::string id = combatant->id;
    encounter->turnIndex = sortByInitiative(encounter->combatants, encounter->turnIndex);
    rebuildCombatantList(id);
    persist();
}

void CombatPage::onHpChanged(int value)
{
    Combatant* combatant = selectedCombatant();
    if (m_populating || combatant == nullptr) {
        return;
    }
    combatant->hp = value;
    if (carryCharacterHitPoints(m_characters, *combatant)) {
        saveCharacters();
    }
    updateCombatantItemText(m_combatantList->currentRow());
    persist();
}

void CombatPage::onTempHpChanged(int value)
{
    Combatant* combatant = selectedCombatant();
    if (m_populating || combatant == nullptr) {
        return;
    }
    combatant->tempHp = value;
    updateCombatantItemText(m_combatantList->currentRow());
    persist();
}

Character* CombatPage::characterFor(const Combatant& combatant)
{
    if (isMonsterCombatant(combatant)) {
        return nullptr;
    }
    for (Character& character : m_characters) {
        if (character.id == combatant.sourceId) {
            return &character;
        }
    }
    return nullptr;
}

void CombatPage::saveCharacters()
{
    try {
        m_charactersStore.saveAll(m_characters);
    } catch (const CharacterStoreError& error) {
        QMessageBox::warning(this, tr("Could not save characters"), QString::fromStdString(error.what()));
    }
}

void CombatPage::showAttacks()
{
    const Encounter* encounter = selectedEncounter();
    const Combatant* turn = nullptr;
    if (encounter != nullptr && encounter->turnIndex >= 0 &&
        encounter->turnIndex < static_cast<int>(encounter->combatants.size())) {
        turn = &encounter->combatants[static_cast<std::size_t>(encounter->turnIndex)];
    }
    const Combatant* selected = selectedCombatant();
    const bool monster = turn != nullptr && selected == turn && isMonsterCombatant(*turn);
    m_attacksSection->setVisible(monster);
    if (!monster) {
        m_attacks->clear();
        return;
    }
    const std::optional<Monster> lookedUp = m_catalog.findById(turn->sourceId);
    if (!lookedUp.has_value() || lookedUp->attacks.empty()) {
        m_attacks->setText(tr("No attacks are stored for this monster."));
        return;
    }
    QStringList lines;
    for (const MonsterAttack& attack : lookedUp->attacks) {
        lines << tr("%1 × %2").arg(QString::fromStdString(attack.name)).arg(attack.count);
        lines << QString::fromStdString(attack.effect);
        lines << QString();
    }
    m_attacks->setText(lines.join(QStringLiteral("\n")).trimmed());
}

void CombatPage::applySelectedDamage()
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr) {
        return;
    }
    if (!applyDamage(*combatant, m_damageAmount->value())) {
        return;
    }
    if (carryCharacterHitPoints(m_characters, *combatant)) {
        saveCharacters();
    }
    m_damageAmount->setValue(0);
    const std::string id = combatant->id;
    rebuildCombatantList(id);
    persist();
}

void CombatPage::applySelectedHealing()
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr) {
        return;
    }
    if (!applyHealing(*combatant, m_healAmount->value())) {
        return;
    }
    if (carryCharacterHitPoints(m_characters, *combatant)) {
        saveCharacters();
    }
    m_healAmount->setValue(0);
    const std::string id = combatant->id;
    rebuildCombatantList(id);
    persist();
}

void CombatPage::addSelectedCondition()
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr || m_conditionPicker->currentIndex() < 0) {
        return;
    }
    const std::string id = m_conditionPicker->currentData().toString().toStdString();
    if (!addCondition(*combatant, id)) {
        for (int i = 0; i < m_conditionList->count(); ++i) {
            if (m_conditionList->item(i)->data(Qt::UserRole).toString().toStdString() == id) {
                m_conditionList->setCurrentRow(i);
                break;
            }
        }
        return;
    }
    persist();
    showCombatant();
}

void CombatPage::removeListedCondition()
{
    Combatant* combatant = selectedCombatant();
    QListWidgetItem* item = m_conditionList->currentItem();
    if (combatant == nullptr || item == nullptr) {
        return;
    }
    if (!removeCondition(*combatant, item->data(Qt::UserRole).toString().toStdString())) {
        return;
    }
    persist();
    showCombatant();
}

void CombatPage::showConditionText()
{
    QListWidgetItem* item = m_conditionList->currentItem();
    if (item == nullptr) {
        m_conditionText->clear();
        return;
    }
    const std::string id = item->data(Qt::UserRole).toString().toStdString();
    const std::optional<Condition> condition = findConditionById(m_conditions, id);
    if (!condition.has_value()) {
        m_conditionText->setText(tr("No SRD description is stored for this condition."));
        return;
    }
    QString text = QString::fromStdString(condition->description);
    if (!condition->tags.empty()) {
        text += QStringLiteral("\n");
        for (const std::string& tag : condition->tags) {
            text += QStringLiteral("\n• ");
            text += QString::fromStdString(tag);
        }
    }
    m_conditionText->setText(text);
}

void CombatPage::refreshConcentrationChoices(const QString& text)
{
    if (m_spellMatches == nullptr) {
        return;
    }
    m_spellMatches->clear();
    for (const Spell& spell : searchSpells(m_spells, text.toStdString())) {
        auto* item = new QListWidgetItem(QString::fromStdString(spell.name));
        item->setData(Qt::UserRole, QString::fromStdString(spell.id));
        m_spellMatches->addItem(item);
    }
}

void CombatPage::setSelectedConcentration()
{
    Combatant* combatant = selectedCombatant();
    const QListWidgetItem* item = m_spellMatches->currentItem();
    if (combatant == nullptr || item == nullptr) {
        return;
    }
    setConcentration(*combatant, item->data(Qt::UserRole).toString().toStdString());
    m_concentrationLabel->setText(item->text());
    persist();
}

void CombatPage::clearSelectedConcentration()
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr) {
        return;
    }
    setConcentration(*combatant, "");
    m_concentrationLabel->setText(tr("Not concentrating."));
    persist();
}

void CombatPage::adjustSelectedDeathSave(bool success, int delta)
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr) {
        return;
    }
    adjustDeathSave(*combatant, success, delta);
    m_deathSuccessLabel->setText(QString::number(combatant->deathSaves.successes));
    m_deathFailureLabel->setText(QString::number(combatant->deathSaves.failures));
    persist();
}

void CombatPage::rebuildSlotButtons()
{
    while (QLayoutItem* item = m_slotLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const Combatant* combatant = selectedCombatant();
    m_restButton->setEnabled(false);
    if (combatant == nullptr || isMonsterCombatant(*combatant)) {
        auto* note = new QLabel(tr("Spell slots are on a character sheet."));
        note->setWordWrap(true);
        m_slotLayout->addWidget(note);
        return;
    }
    Character* character = characterFor(*combatant);
    if (character == nullptr) {
        auto* note = new QLabel(tr("This character is not in the roster, so spell slots cannot be changed."));
        note->setWordWrap(true);
        m_slotLayout->addWidget(note);
        return;
    }
    if (character->spellSlots.empty()) {
        auto* note = new QLabel(tr("This character has no spell slots."));
        note->setWordWrap(true);
        m_slotLayout->addWidget(note);
        return;
    }
    m_restButton->setEnabled(true);
    for (const SpellSlot& slot : character->spellSlots) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(tr("Level %1: %2 / %3").arg(slot.level).arg(slot.current).arg(slot.max));
        auto* spend = new QPushButton(tr("Spend"));
        spend->setProperty("level", slot.level);
        spend->setEnabled(slot.current >= 1);
        layout->addWidget(label);
        layout->addWidget(spend);
        layout->addStretch(1);
        m_slotLayout->addWidget(row);
        connect(spend, &QPushButton::clicked, this, &CombatPage::spendSelectedSlot);
    }
}

void CombatPage::spendSelectedSlot()
{
    Combatant* combatant = selectedCombatant();
    auto* button = qobject_cast<QPushButton*>(sender());
    if (combatant == nullptr || button == nullptr) {
        return;
    }
    Character* character = characterFor(*combatant);
    if (character == nullptr) {
        return;
    }
    const int level = button->property("level").toInt();
    if (!spendSpellSlot(*character, level)) {
        return;
    }
    saveCharacters();
    // Rebuild after this click returns. The Spend button lives in the slot
    // layout, and rebuilding deletes it.
    QTimer::singleShot(0, this, [this] { rebuildSlotButtons(); });
}

void CombatPage::restSelectedCharacter()
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr) {
        return;
    }
    Character* character = characterFor(*combatant);
    if (character == nullptr) {
        return;
    }
    finishRest(*character);
    saveCharacters();
    rebuildSlotButtons();
}

void CombatPage::updateDerivedModifiers()
{
    const Combatant* combatant = selectedCombatant();
    if (combatant == nullptr || isMonsterCombatant(*combatant)) {
        m_derivedLabel->setText(tr("Derived modifiers are shown for a character."));
        return;
    }
    const Character* character = characterFor(*combatant);
    if (character == nullptr) {
        m_derivedLabel->setText(tr("This character is not in the roster, so derived modifiers are not shown."));
        return;
    }
    const int totalLevel = totalClassLevel(*character);
    const int proficiency = proficiencyBonusForLevel(totalLevel);
    const std::array<int AbilityScores::*, 6> scoreMembers{{
        &AbilityScores::strength,
        &AbilityScores::dexterity,
        &AbilityScores::constitution,
        &AbilityScores::intelligence,
        &AbilityScores::wisdom,
        &AbilityScores::charisma,
    }};
    QStringList modifiers;
    QStringList saves;
    for (std::size_t i = 0; i < kSavingThrows.size(); ++i) {
        const int score = character->abilities.*scoreMembers[i];
        modifiers << tr("%1 %2").arg(tr(kSavingThrows[i].label),
                                      QString::fromStdString(formatModifier(abilityModifier(score))));
        const bool proficient = character->savingThrows.*kSavingThrows[i].member;
        QString saveText =
            tr("%1 %2").arg(tr(kSavingThrows[i].label),
                            QString::fromStdString(formatModifier(saveBonus(score, proficient, proficiency))));
        if (proficient) {
            saveText += tr(" proficient");
        }
        saves << saveText;
    }
    QStringList lines;
    lines << tr("Ability modifiers: %1").arg(modifiers.join(QStringLiteral(", ")));
    lines << tr("Proficiency bonus %1 from total level %2.")
                 .arg(QString::fromStdString(formatModifier(proficiency)))
                 .arg(totalLevel);
    if (character->proficiencyBonus != proficiency) {
        lines << tr("The typed proficiency bonus on the sheet is %1. Saves below use the level table.")
                     .arg(QString::fromStdString(formatModifier(character->proficiencyBonus)));
    }
    lines << tr("Saves: %1").arg(saves.join(QStringLiteral(", ")));
    lines << tr("Dexterity initiative modifier %1. The initiative box is the total the turn order uses.")
                 .arg(QString::fromStdString(formatModifier(dexterityInitiativeModifier(*character))));
    if (character->initiativeBonus != 0) {
        lines << tr("The typed initiative bonus on the sheet is %1.")
                     .arg(QString::fromStdString(formatModifier(character->initiativeBonus)));
    }
    m_derivedLabel->setText(lines.join(QStringLiteral("\n")));
}

void CombatPage::rollAll()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    const int missing = rollAllMonsterInitiatives(*encounter, [this] { return rollD20(); });
    if (missing == 0) {
        m_rollNote->hide();
        m_rollNote->clear();
    } else if (missing == 1) {
        m_rollNote->setText(tr("1 monster had no initiative bonus stored. That roll used +0."));
        m_rollNote->show();
    } else {
        m_rollNote->setText(
            tr("%1 monsters had no initiative bonus stored. Those rolls used +0.").arg(missing));
        m_rollNote->show();
    }
    rebuildCombatantList({});
    persist();
}

void CombatPage::rerollSelected()
{
    Encounter* encounter = selectedEncounter();
    Combatant* combatant = selectedCombatant();
    if (encounter == nullptr || combatant == nullptr || !isMonsterCombatant(*combatant)) {
        return;
    }
    const std::string id = combatant->id;
    bool missing = false;
    if (!rerollMonsterInitiative(*encounter, id, [this] { return rollD20(); }, &missing)) {
        return;
    }
    if (missing) {
        m_rollNote->setText(tr("The initiative bonus was missing. That roll used +0."));
        m_rollNote->show();
    }
    rebuildCombatantList(id);
    persist();
}

void CombatPage::moveSelected(int direction)
{
    Encounter* encounter = selectedEncounter();
    Combatant* combatant = selectedCombatant();
    if (encounter == nullptr || combatant == nullptr) {
        return;
    }
    const std::string id = combatant->id;
    const MoveResult moved =
        moveCombatant(encounter->combatants, m_combatantList->currentRow(), direction, encounter->turnIndex);
    encounter->turnIndex = moved.turnIndex;
    rebuildCombatantList(id);
    persist();
}

void CombatPage::removeSelected()
{
    Encounter* encounter = selectedEncounter();
    Combatant* combatant = selectedCombatant();
    if (encounter == nullptr || combatant == nullptr) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Remove combatant"),
        tr("Remove %1 from this fight? The character or monster itself is not deleted.")
            .arg(QString::fromStdString(combatant->name)));
    if (answer != QMessageBox::Yes) {
        return;
    }
    const int index = m_combatantList->currentRow();
    encounter->turnIndex = removeCombatant(encounter->combatants, index, encounter->turnIndex);
    rebuildCombatantList({});
    persist();
}

void CombatPage::previousTurn()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    retreatTurn(*encounter);
    rebuildCombatantList({});
    persist();
}

void CombatPage::nextTurn()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    advanceTurn(*encounter);
    rebuildCombatantList({});
    persist();
}

void CombatPage::nextRound()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    advanceRound(*encounter);
    rebuildCombatantList({});
    persist();
}

}  // namespace combat::ui
