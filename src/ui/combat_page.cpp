#include "ui/combat_page.h"

#include "core/attack_damage.h"
#include "core/character_store.h"
#include "core/combat_rules.h"
#include "core/encounter_store.h"
#include "core/monster_catalog.h"
#include "ui/page_title.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <array>
#include <limits>
#include <optional>

namespace combat::ui {

namespace {

void clearLayout(QLayout* layout)
{
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

// Tip at the hotspot, so the click lands where the sword points.
QCursor swordCursor()
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#d9dde6")));
    QPolygon blade;
    blade << QPoint(16, 1) << QPoint(20, 8) << QPoint(17, 22) << QPoint(15, 22) << QPoint(12, 8);
    painter.drawPolygon(blade);
    painter.setBrush(QColor(QStringLiteral("#8a5a2b")));
    painter.drawRect(QRect(8, 21, 16, 3));
    painter.setBrush(QColor(QStringLiteral("#5c3a1e")));
    painter.drawRect(QRect(15, 24, 3, 6));
    painter.setBrush(QColor(QStringLiteral("#c2a15a")));
    painter.drawEllipse(QRect(13, 29, 6, 3));
    painter.end();
    return QCursor(pixmap, 16, 1);
}

QSpinBox* makeNumberBox()
{
    auto* box = new QSpinBox;
    box->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    box->setMaximumWidth(140);
    return box;
}

QTreeWidget* makeCombatantTree(const QStringList& headers, int stretchColumn)
{
    auto* tree = new QTreeWidget;
    tree->setColumnCount(static_cast<int>(headers.size()));
    tree->setHeaderLabels(headers);
    tree->setRootIsDecorated(false);
    tree->setIndentation(0);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setAllColumnsShowFocus(true);
    tree->setUniformRowHeights(true);
    tree->setMinimumHeight(140);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(stretchColumn, QHeaderView::Stretch);
    for (int column = 0; column < tree->columnCount(); ++column) {
        if (column == stretchColumn) {
            continue;
        }
        tree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    return tree;
}

int indexOfId(const Encounter& encounter, const std::string& id)
{
    for (int i = 0; i < static_cast<int>(encounter.combatants.size()); ++i) {
        if (encounter.combatants[static_cast<std::size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

QTreeWidgetItem* addCombatantRow(QTreeWidget* tree, const Combatant& combatant, bool active, bool initiativeColumns)
{
    auto* item = new QTreeWidgetItem(tree);
    const QString name = QString::fromStdString(combatant.name);
    const QString hp = QString::fromStdString(formatHitPoints(combatant.hp, combatant.maxHp));
    item->setData(0, Qt::UserRole, QString::fromStdString(combatant.id));
    if (initiativeColumns) {
        item->setText(0, active ? QStringLiteral("●") : QString());
        item->setTextAlignment(0, Qt::AlignCenter);
        item->setText(1, QString::number(combatant.initiative));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(2, name);
        item->setText(3, QString::number(combatant.ac));
        item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(4, hp);
        item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
    } else {
        item->setText(0, name);
        item->setText(1, hp);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
    return item;
}

}  // namespace

CombatPage::~CombatPage()
{
    disarmAttack();
}

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

    m_roundLabel = new QLabel;
    m_roundLabel->setObjectName(QStringLiteral("roundLabel"));
    m_activeLabel = new QLabel;
    m_activeLabel->setObjectName(QStringLiteral("activeCombatant"));
    QFont activeFont = m_activeLabel->font();
    activeFont.setPointSizeF(activeFont.pointSizeF() * 1.25);
    activeFont.setBold(true);
    m_activeLabel->setFont(activeFont);
    m_hitLabel = new QLabel;
    m_hitLabel->setObjectName(QStringLiteral("attackHit"));
    m_hitLabel->hide();
    auto* turnRow = new QHBoxLayout;
    turnRow->addWidget(m_roundLabel);
    turnRow->addWidget(m_activeLabel);
    turnRow->addWidget(m_hitLabel);
    turnRow->addStretch(1);
    fightLayout->addLayout(turnRow);

    auto* turnButtons = new QHBoxLayout;
    m_previousTurnButton = new QPushButton(tr("Previous turn"));
    m_previousTurnButton->setObjectName(QStringLiteral("previousTurn"));
    m_nextTurnButton = new QPushButton(tr("Next turn"));
    m_nextTurnButton->setObjectName(QStringLiteral("nextTurn"));
    m_undoButton = new QPushButton(tr("Undo"));
    m_undoButton->setObjectName(QStringLiteral("undoFight"));
    m_undoButton->setEnabled(false);
    m_damageAmount = makeNumberBox();
    m_damageAmount->setRange(0, std::numeric_limits<int>::max());
    m_damageAmount->setMaximumWidth(70);
    m_damageAmount->setValue(0);
    m_damageButton = new QPushButton(tr("Apply damage"));
    m_damageButton->setObjectName(QStringLiteral("applyDamage"));
    m_healAmount = makeNumberBox();
    m_healAmount->setRange(0, std::numeric_limits<int>::max());
    m_healAmount->setMaximumWidth(70);
    m_healAmount->setValue(0);
    m_healButton = new QPushButton(tr("Apply healing"));
    m_healButton->setObjectName(QStringLiteral("applyHealing"));
    turnButtons->addWidget(m_previousTurnButton);
    turnButtons->addWidget(m_nextTurnButton);
    turnButtons->addWidget(m_undoButton);
    turnButtons->addSpacing(24);
    turnButtons->addWidget(m_damageAmount);
    turnButtons->addWidget(m_damageButton);
    turnButtons->addSpacing(16);
    turnButtons->addWidget(m_healAmount);
    turnButtons->addWidget(m_healButton);
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

    auto* lists = new QHBoxLayout;
    lists->setSpacing(16);
    m_initiativeList = makeCombatantTree({QString(), QString(), tr("Name"), tr("AC"), tr("HP"), tr("Attacks")}, 2);
    m_initiativeList->setObjectName(QStringLiteral("initiativeList"));
    m_zeroHpList = makeCombatantTree({tr("Name"), tr("HP")}, 0);
    m_zeroHpList->setObjectName(QStringLiteral("zeroHpList"));
    lists->addWidget(m_initiativeList, 3);
    lists->addWidget(m_zeroHpList, 2);
    fightLayout->addLayout(lists);

    m_noCombatantHint = new QLabel(tr("No one is in this fight yet. Add characters and monsters in Encounter Builder."));
    fightLayout->addWidget(m_noCombatantHint);

    m_combatantForm = new QWidget;
    auto* form = new QFormLayout(m_combatantForm);
    m_combatantFormLayout = form;

    m_attacksSection = new QWidget;
    m_attacksSection->setObjectName(QStringLiteral("combatantAttacks"));
    auto* attacksLayout = new QVBoxLayout(m_attacksSection);
    attacksLayout->setContentsMargins(0, 0, 0, 0);
    attacksLayout->setSpacing(4);
    auto* attacksHeading = new QLabel(tr("Attacks"));
    QFont attacksFont = attacksHeading->font();
    attacksFont.setBold(true);
    attacksHeading->setFont(attacksFont);
    m_attackRows = new QVBoxLayout;
    m_attackRows->setContentsMargins(0, 0, 0, 0);
    m_attackRows->setSpacing(8);
    attacksLayout->addWidget(attacksHeading);
    attacksLayout->addLayout(m_attackRows);
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
    m_deathSavesHost = new QWidget;
    auto* deathRow = new QHBoxLayout(m_deathSavesHost);
    deathRow->setContentsMargins(0, 0, 0, 0);
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
    form->addRow(tr("Death saves"), m_deathSavesHost);

    m_derivedLabel = new QLabel;
    m_derivedLabel->setObjectName(QStringLiteral("derivedModifiers"));
    m_derivedLabel->setWordWrap(true);
    form->addRow(tr("From the sheet"), m_derivedLabel);

    m_slotHost = new QWidget;
    m_slotLayout = new QVBoxLayout(m_slotHost);
    m_slotLayout->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Spell slots"), m_slotHost);

    fightLayout->addWidget(m_combatantForm);

    auto* orderButtons = new QHBoxLayout;
    m_removeButton = new QPushButton(tr("Remove"));
    orderButtons->addWidget(m_removeButton);
    orderButtons->addStretch(1);
    fightLayout->addLayout(orderButtons);
    fightLayout->addStretch(1);

    auto* hpNote = new QLabel(tr("Hit points, temporary HP, conditions, concentration, and death saves in this fight "
                                 "are kept with the encounter. Spell slots are saved on the character."));
    hpNote->setObjectName(QStringLiteral("fightHpNote"));
    hpNote->setWordWrap(true);
    fightLayout->addWidget(hpNote);

    right->addWidget(m_fight, 1);

    connect(m_encounterCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &CombatPage::showEncounter);
    connect(m_initiativeList, &QTreeWidget::itemClicked, this, &CombatPage::applyArmedDamage);
    connect(m_zeroHpList, &QTreeWidget::itemClicked, this, &CombatPage::applyArmedDamage);
    connect(m_initiativeList, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem* previous) {
                if (m_populating) {
                    return;
                }
                rememberArmedSelection(previous, m_zeroHpList);
                if (current != nullptr) {
                    const QSignalBlocker blocker(m_zeroHpList);
                    m_zeroHpList->setCurrentItem(nullptr);
                }
                showCombatant();
            });
    connect(m_zeroHpList, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem* previous) {
                if (m_populating) {
                    return;
                }
                rememberArmedSelection(previous, m_initiativeList);
                if (current != nullptr) {
                    const QSignalBlocker blocker(m_initiativeList);
                    m_initiativeList->setCurrentItem(nullptr);
                }
                showCombatant();
            });
    connect(m_initiative, &QSpinBox::valueChanged, this, &CombatPage::onInitiativeChanged);
    connect(m_initiative, &QSpinBox::editingFinished, this, &CombatPage::onInitiativeEditingFinished);
    connect(m_hp, &QSpinBox::editingFinished, this, [this] { closeFightEdit(FightEdit::HitPoints); });
    connect(m_tempHp, &QSpinBox::editingFinished, this, [this] { closeFightEdit(FightEdit::TemporaryHp); });
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
    connect(m_rollAllButton, &QPushButton::clicked, this, &CombatPage::rollAll);
    connect(m_rerollButton, &QPushButton::clicked, this, &CombatPage::rerollSelected);
    connect(m_removeButton, &QPushButton::clicked, this, &CombatPage::removeSelected);
    connect(m_previousTurnButton, &QPushButton::clicked, this, &CombatPage::previousTurn);
    connect(m_nextTurnButton, &QPushButton::clicked, this, &CombatPage::nextTurn);
    connect(m_undoButton, &QPushButton::clicked, this, &CombatPage::undoLastChange);
    auto* cancelAttack = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    cancelAttack->setContext(Qt::WindowShortcut);
    connect(cancelAttack, &QShortcut::activated, this, &CombatPage::disarmAttack);

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
    if (encounter == nullptr) {
        return nullptr;
    }
    QTreeWidgetItem* item = nullptr;
    if (m_initiativeList != nullptr && m_initiativeList->currentItem() != nullptr) {
        item = m_initiativeList->currentItem();
    } else if (m_zeroHpList != nullptr) {
        item = m_zeroHpList->currentItem();
    }
    if (item == nullptr) {
        return nullptr;
    }
    const int index = indexOfId(*encounter, item->data(0, Qt::UserRole).toString().toStdString());
    if (index < 0) {
        return nullptr;
    }
    return &encounter->combatants[static_cast<std::size_t>(index)];
}

void CombatPage::refreshListedHitPoints(const Combatant& combatant)
{
    const QString id = QString::fromStdString(combatant.id);
    const QString hp = QString::fromStdString(formatHitPoints(combatant.hp, combatant.maxHp));
    const bool inOrder = isInInitiative(combatant);
    QTreeWidget* list = inOrder ? m_initiativeList : m_zeroHpList;
    const int column = inOrder ? 4 : 1;
    if (list == nullptr) {
        return;
    }
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = list->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == id) {
            item->setText(column, hp);
            return;
        }
    }
}

void CombatPage::updateTurnLabels()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    m_roundLabel->setText(tr("Round %1").arg(encounter->round));
    const int count = static_cast<int>(encounter->combatants.size());
    const bool hasLiving = !initiativeOrder(encounter->combatants).empty();
    const int index = encounter->turnIndex;
    const bool livingTurn =
        index >= 0 && index < count && isInInitiative(encounter->combatants[static_cast<std::size_t>(index)]);
    if (count == 0) {
        m_activeLabel->setText(tr("No one is in this fight yet."));
    } else if (!livingTurn) {
        m_activeLabel->setText(tr("No one is above 0 HP."));
    } else {
        const QString name = QString::fromStdString(encounter->combatants[static_cast<std::size_t>(index)].name);
        m_activeLabel->setText(tr("%1's turn").arg(name));
    }
    m_previousTurnButton->setEnabled(hasLiving);
    m_nextTurnButton->setEnabled(hasLiving);
    syncAttackCount();

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
        m_undo.reset();
        m_openEdit = FightEdit::None;
        m_undoEncounterId.clear();
        if (m_undoButton != nullptr) {
            m_undoButton->setEnabled(false);
        }
        m_attacksUsedById.clear();
        m_countedCombatantId.clear();
        m_countedRound = 0;
        setHitText({});
        disarmAttack();
        return;
    }
    if (m_undoEncounterId != encounter->id) {
        if (!m_undoEncounterId.empty()) {
            m_undo.reset();
            m_openEdit = FightEdit::None;
            m_undoButton->setEnabled(false);
            disarmAttack();
        }
        m_attacksUsedById.clear();
        m_countedCombatantId.clear();
        m_countedRound = 0;
        setHitText({});
        m_undoEncounterId = encounter->id;
    }
    m_rollNote->hide();
    m_rollNote->clear();
    if (assignMonsterCopyNames(encounter->combatants)) {
        persist();
    }
    rebuildCombatantList({});
}

void CombatPage::rebuildCombatantList(const std::string& selectId)
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    keepTurnInInitiative(*encounter);
    syncAttackCount();
    std::string id = selectId;
    if (id.empty() && !encounter->combatants.empty()) {
        const int turn = encounter->turnIndex;
        if (turn >= 0 && turn < static_cast<int>(encounter->combatants.size())) {
            id = encounter->combatants[static_cast<std::size_t>(turn)].id;
        }
    }
    m_populating = true;
    {
        const QSignalBlocker initiativeBlocker(m_initiativeList);
        const QSignalBlocker zeroBlocker(m_zeroHpList);
        m_initiativeList->clear();
        m_zeroHpList->clear();
        QTreeWidgetItem* selectItem = nullptr;
        QTreeWidget* selectList = nullptr;
        for (int i = 0; i < static_cast<int>(encounter->combatants.size()); ++i) {
            const Combatant& combatant = encounter->combatants[static_cast<std::size_t>(i)];
            const bool down = !isInInitiative(combatant);
            QTreeWidget* list = down ? m_zeroHpList : m_initiativeList;
            QTreeWidgetItem* item =
                addCombatantRow(list, combatant, !down && i == encounter->turnIndex, !down);
            if (!down && isMonsterCombatant(combatant)) {
                const std::string label =
                    attacksUsedLabel(attacksUsedFor(combatant.id), allotmentFor(combatant));
                item->setText(5, QString::fromStdString(label));
                item->setTextAlignment(5, Qt::AlignRight | Qt::AlignVCenter);
            }
            if (combatant.id == id) {
                selectItem = item;
                selectList = list;
            }
        }
        if (selectList != m_initiativeList) {
            m_initiativeList->setCurrentItem(nullptr);
        }
        if (selectList != m_zeroHpList) {
            m_zeroHpList->setCurrentItem(nullptr);
        }
        if (selectItem != nullptr && selectList != nullptr) {
            selectList->setCurrentItem(selectItem);
        }
    }
    m_initiativeList->viewport()->update();
    m_zeroHpList->viewport()->update();
    m_populating = false;
    updateTurnLabels();
    showCombatant();
}

void CombatPage::showCombatant()
{
    if (m_populating) {
        return;
    }
    Combatant* combatant = selectedCombatant();
    const Encounter* encounter = selectedEncounter();
    m_combatantForm->setVisible(combatant != nullptr);
    m_noCombatantHint->setVisible(encounter != nullptr && combatant == nullptr);
    m_removeButton->setEnabled(combatant != nullptr);
    setCharacterSheetControlsVisible(combatant != nullptr && !isMonsterCombatant(*combatant));
    if (combatant == nullptr) {
        m_rerollButton->setEnabled(false);
        showAttacks();
        return;
    }

    m_populating = true;
    if (isMonsterCombatant(*combatant)) {
        m_rerollButton->setEnabled(true);
        if (combatant->initiativeBonus.has_value()) {
            m_bonusLabel->setText(tr("Initiative bonus %1")
                                      .arg(QString::fromStdString(formatModifier(*combatant->initiativeBonus))));
        } else {
            m_bonusLabel->setText(tr("The initiative bonus was missing. Rolls use +0."));
        }
    } else {
        m_rerollButton->setEnabled(false);
        m_bonusLabel->setText(tr("Type this character's initiative. Characters are not rolled."));
    }
    m_initiative->setValue(combatant->initiative);
    m_acLabel->setText(QString::number(combatant->ac));
    const bool wasIn = isInInitiative(*combatant);
    const int hpBefore = combatant->hp;
    clampAndCarryHitPoints(*combatant);
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
    if (combatant->hp != hpBefore || wasIn != isInInitiative(*combatant)) {
        persist();
        if (wasIn != isInInitiative(*combatant)) {
            const std::string id = combatant->id;
            rebuildCombatantList(id);
            return;
        }
        refreshListedHitPoints(*combatant);
    }
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
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Initiative, nullptr, snapshot);
    combatant->initiative = value;
    const QString id = QString::fromStdString(combatant->id);
    for (int i = 0; i < m_initiativeList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_initiativeList->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == id) {
            item->setText(1, QString::number(combatant->initiative));
            break;
        }
    }
    if (fresh && fightChanged(snapshot, nullptr)) {
        keepUndo(FightEdit::Initiative, std::move(snapshot));
    }
    persist();
}

void CombatPage::onInitiativeEditingFinished()
{
    Encounter* encounter = selectedEncounter();
    Combatant* combatant = selectedCombatant();
    if (m_populating || encounter == nullptr || combatant == nullptr) {
        closeFightEdit(FightEdit::Initiative);
        return;
    }
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Initiative, nullptr, snapshot);
    const std::string id = combatant->id;
    encounter->turnIndex = sortByInitiative(encounter->combatants, encounter->turnIndex);
    if (fresh && fightChanged(snapshot, nullptr)) {
        keepUndo(FightEdit::Initiative, std::move(snapshot));
    }
    closeFightEdit(FightEdit::Initiative);
    rebuildCombatantList(id);
    persist();
}

void CombatPage::onHpChanged(int value)
{
    Combatant* combatant = selectedCombatant();
    if (m_populating || combatant == nullptr) {
        return;
    }
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::HitPoints, combatant, snapshot);
    const bool wasIn = isInInitiative(*combatant);
    combatant->hp = value;
    clampAndCarryHitPoints(*combatant);
    if (m_hp->value() != combatant->hp) {
        const QSignalBlocker blocker(m_hp);
        m_hp->setValue(combatant->hp);
    }
    if (wasIn != isInInitiative(*combatant)) {
        const std::string id = combatant->id;
        rebuildCombatantList(id);
    } else {
        refreshListedHitPoints(*combatant);
    }
    if (fresh && fightChanged(snapshot, combatant)) {
        keepUndo(FightEdit::HitPoints, std::move(snapshot));
    }
    persist();
}

void CombatPage::onTempHpChanged(int value)
{
    Combatant* combatant = selectedCombatant();
    if (m_populating || combatant == nullptr) {
        return;
    }
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::TemporaryHp, nullptr, snapshot);
    const bool wasIn = isInInitiative(*combatant);
    combatant->tempHp = value;
    if (wasIn != isInInitiative(*combatant)) {
        const std::string id = combatant->id;
        rebuildCombatantList(id);
    }
    if (fresh && fightChanged(snapshot, nullptr)) {
        keepUndo(FightEdit::TemporaryHp, std::move(snapshot));
    }
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

void CombatPage::setCharacterSheetControlsVisible(bool visible)
{
    if (m_combatantFormLayout == nullptr) {
        return;
    }
    m_combatantFormLayout->setRowVisible(m_deathSavesHost, visible);
    m_combatantFormLayout->setRowVisible(m_derivedLabel, visible);
    m_combatantFormLayout->setRowVisible(m_slotHost, visible);
}

void CombatPage::clampAndCarryHitPoints(Combatant& combatant)
{
    combatant.hp = cappedHitPoints(combatant.hp, combatant.maxHp);
    Character* character = characterFor(combatant);
    const int sheetBefore = character == nullptr ? 0 : character->hp.current;
    if (!carryCharacterHitPoints(m_characters, combatant) || character == nullptr) {
        return;
    }
    combatant.hp = character->hp.current;
    if (character->hp.current != sheetBefore) {
        saveCharacters();
    }
}

void CombatPage::clearAttackRows()
{
    clearLayout(m_attackRows);
}

void CombatPage::showAttacks()
{
    clearAttackRows();
    const Combatant* selected = selectedCombatant();
    const bool monster = selected != nullptr && isMonsterCombatant(*selected);
    m_attacksSection->setVisible(monster);
    if (!monster) {
        return;
    }
    const std::optional<Monster> lookedUp = m_catalog.findById(selected->sourceId);
    if (!lookedUp.has_value() || lookedUp->attacks.empty()) {
        auto* empty = new QLabel(tr("No attacks are stored for this monster."));
        empty->setWordWrap(true);
        m_attackRows->addWidget(empty);
        return;
    }
    const Encounter* encounter = selectedEncounter();
    bool theirTurn = false;
    if (encounter != nullptr && encounter->turnIndex >= 0 &&
        encounter->turnIndex < static_cast<int>(encounter->combatants.size())) {
        theirTurn = encounter->combatants[static_cast<std::size_t>(encounter->turnIndex)].id == selected->id;
    }
    const int allotment = attackAllotment(lookedUp->attacks);
    const bool buttons = theirTurn && attackButtonsAvailable(attacksUsedFor(selected->id), allotment);
    for (const MonsterAttack& attack : lookedUp->attacks) {
        auto* block = new QWidget;
        auto* blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(0, 0, 0, 0);
        blockLayout->setSpacing(2);
        const QString title = tr("%1 × %2").arg(QString::fromStdString(attack.name)).arg(attack.count);
        auto* name = new QLabel(title);
        name->setWordWrap(true);
        blockLayout->addWidget(name);
        const bool damage = !damageExpressions(attack.effect).empty();
        if (buttons && damage) {
            auto* button = new QPushButton(title);
            button->setObjectName(QStringLiteral("rollAttackDamage"));
            const std::string effect = attack.effect;
            connect(button, &QPushButton::clicked, this, [this, effect] {
                if (m_armedDamage.has_value() && m_armedEffect == effect) {
                    disarmAttack();
                    return;
                }
                const std::optional<int> total = rollAttackDamage(effect, [this](int sides) {
                    std::uniform_int_distribution<int> face(1, sides);
                    return face(m_dice);
                });
                if (total.has_value()) {
                    armAttack(effect, *total);
                }
            });
            blockLayout->addWidget(button);
        } else if (buttons && isUseAbilityAction(attack)) {
            auto* button = new QPushButton(tr("Use ability"));
            button->setObjectName(QStringLiteral("useAbility"));
            connect(button, &QPushButton::clicked, this, &CombatPage::useSelectedAbility);
            blockLayout->addWidget(button);
        }
        auto* effectLabel = new QLabel(QString::fromStdString(attack.effect));
        effectLabel->setWordWrap(true);
        effectLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        blockLayout->addWidget(effectLabel);
        m_attackRows->addWidget(block);
    }
}

void CombatPage::applySelectedDamage()
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr) {
        return;
    }
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, isMonsterCombatant(*combatant) ? nullptr : combatant, snapshot);
    if (!applyDamage(*combatant, m_damageAmount->value())) {
        return;
    }
    clampAndCarryHitPoints(*combatant);
    if (fresh && fightChanged(snapshot, isMonsterCombatant(*combatant) ? nullptr : combatant)) {
        keepUndo(FightEdit::Once, std::move(snapshot));
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
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, isMonsterCombatant(*combatant) ? nullptr : combatant, snapshot);
    if (!applyHealing(*combatant, m_healAmount->value())) {
        return;
    }
    clampAndCarryHitPoints(*combatant);
    if (fresh && fightChanged(snapshot, isMonsterCombatant(*combatant) ? nullptr : combatant)) {
        keepUndo(FightEdit::Once, std::move(snapshot));
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
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, nullptr, snapshot);
    if (!addCondition(*combatant, id)) {
        for (int i = 0; i < m_conditionList->count(); ++i) {
            if (m_conditionList->item(i)->data(Qt::UserRole).toString().toStdString() == id) {
                m_conditionList->setCurrentRow(i);
                break;
            }
        }
        return;
    }
    if (fresh) {
        keepUndo(FightEdit::Once, std::move(snapshot));
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
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, nullptr, snapshot);
    if (!removeCondition(*combatant, item->data(Qt::UserRole).toString().toStdString())) {
        return;
    }
    if (fresh) {
        keepUndo(FightEdit::Once, std::move(snapshot));
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
    const std::string spellId = item->data(Qt::UserRole).toString().toStdString();
    if (combatant->concentration == spellId) {
        return;
    }
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, nullptr, snapshot);
    setConcentration(*combatant, spellId);
    if (fresh) {
        keepUndo(FightEdit::Once, std::move(snapshot));
    }
    m_concentrationLabel->setText(item->text());
    persist();
}

void CombatPage::clearSelectedConcentration()
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr || combatant->concentration.empty()) {
        return;
    }
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, nullptr, snapshot);
    setConcentration(*combatant, "");
    if (fresh) {
        keepUndo(FightEdit::Once, std::move(snapshot));
    }
    m_concentrationLabel->setText(tr("Not concentrating."));
    persist();
}

void CombatPage::adjustSelectedDeathSave(bool success, int delta)
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr) {
        return;
    }
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, nullptr, snapshot);
    adjustDeathSave(*combatant, success, delta);
    if (fresh) {
        keepUndo(FightEdit::Once, std::move(snapshot));
    }
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
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, combatant, snapshot);
    if (!spendSpellSlot(*character, level)) {
        return;
    }
    if (fresh) {
        keepUndo(FightEdit::Once, std::move(snapshot));
    }
    saveCharacters();
    // Rebuild after this click returns. The Spend button lives in the slot
    // layout, and rebuilding deletes it.
    QTimer::singleShot(0, this, [this] { rebuildSlotButtons(); });
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
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, nullptr, snapshot);
    const int missing = rollAllMonsterInitiatives(*encounter, [this] { return rollD20(); });
    if (fresh && fightChanged(snapshot, nullptr)) {
        keepUndo(FightEdit::Once, std::move(snapshot));
    }
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
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, nullptr, snapshot);
    bool missing = false;
    if (!rerollMonsterInitiative(*encounter, id, [this] { return rollD20(); }, &missing)) {
        return;
    }
    if (fresh && fightChanged(snapshot, nullptr)) {
        keepUndo(FightEdit::Once, std::move(snapshot));
    }
    if (missing) {
        m_rollNote->setText(tr("The initiative bonus was missing. That roll used +0."));
        m_rollNote->show();
    }
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
    const int index = indexOfId(*encounter, combatant->id);
    encounter->turnIndex = removeCombatant(encounter->combatants, index, encounter->turnIndex);
    assignMonsterCopyNames(encounter->combatants);
    rebuildCombatantList({});
    persist();
}

void CombatPage::previousTurn()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, nullptr, snapshot);
    retreatTurn(*encounter);
    if (fresh && fightChanged(snapshot, nullptr)) {
        keepUndo(FightEdit::Once, std::move(snapshot));
    }
    rebuildCombatantList({});
    persist();
}

void CombatPage::nextTurn()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, nullptr, snapshot);
    advanceTurn(*encounter);
    if (fresh && fightChanged(snapshot, nullptr)) {
        keepUndo(FightEdit::Once, std::move(snapshot));
    }
    rebuildCombatantList({});
    persist();
}

void CombatPage::applyArmedDamage(QTreeWidgetItem* item, int /*column*/)
{
    if (!m_armedDamage.has_value() || item == nullptr) {
        return;
    }
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    const int index = indexOfId(*encounter, item->data(0, Qt::UserRole).toString().toStdString());
    if (index < 0) {
        return;
    }
    const int amount = *m_armedDamage;
    const std::string attackerId = m_armedAttackerId;
    const bool restorePriorSelection = m_hasSelectionBeforeAttack;
    const std::string priorSelection = m_selectionBeforeAttack;
    Combatant& combatant = encounter->combatants[static_cast<std::size_t>(index)];
    const QString targetName = QString::fromStdString(combatant.name);
    QString attackerName;
    const int turn = encounter->turnIndex;
    if (turn >= 0 && turn < static_cast<int>(encounter->combatants.size())) {
        attackerName = QString::fromStdString(encounter->combatants[static_cast<std::size_t>(turn)].name);
    }
    const Combatant* sheet = isMonsterCombatant(combatant) ? nullptr : &combatant;
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, sheet, snapshot);
    if (restorePriorSelection) {
        m_capturedSelectionId = priorSelection;
    } else if (const Combatant* selected = selectedCombatant()) {
        m_capturedSelectionId = selected->id;
    } else {
        m_capturedSelectionId = std::string();
    }
    disarmAttack();

    int attackerIndex = -1;
    int allotment = 1;
    if (!attackerId.empty()) {
        attackerIndex = indexOfId(*encounter, attackerId);
        if (attackerIndex >= 0) {
            const Combatant& attacker = encounter->combatants[static_cast<std::size_t>(attackerIndex)];
            if (isMonsterCombatant(attacker)) {
                const std::optional<Monster> monster = m_catalog.findById(attacker.sourceId);
                if (monster.has_value()) {
                    allotment = attackAllotment(monster->attacks);
                }
            } else {
                attackerIndex = -1;
            }
        }
    }

    m_suppressUndo = true;
    const int usedBefore = attackerId.empty() ? 0 : attacksUsedFor(attackerId);
    const std::optional<int> attacksUsed =
        completeMonsterAttack(*encounter, index, amount, m_characters, attackerIndex, usedBefore, allotment);
    if (!attacksUsed.has_value()) {
        m_suppressUndo = false;
        return;
    }
    if (fresh) {
        keepUndo(FightEdit::Once, std::move(snapshot));
    }
    setHitText(tr("%1 hit %2 for %3 damage").arg(attackerName, targetName).arg(amount));
    if (!attackerId.empty()) {
        m_attacksUsedById[attackerId] = *attacksUsed;
    }
    if (!isMonsterCombatant(encounter->combatants[static_cast<std::size_t>(index)])) {
        saveCharacters();
    }
    rebuildCombatantList(currentTurnId());
    persist();
    m_suppressUndo = false;
}

void CombatPage::useSelectedAbility()
{
    Encounter* encounter = selectedEncounter();
    Combatant* selected = selectedCombatant();
    if (encounter == nullptr || selected == nullptr || !isMonsterCombatant(*selected)) {
        return;
    }
    const int turn = encounter->turnIndex;
    if (turn < 0 || turn >= static_cast<int>(encounter->combatants.size()) ||
        encounter->combatants[static_cast<std::size_t>(turn)].id != selected->id) {
        return;
    }
    const std::string attackerId = selected->id;
    const int allotment = allotmentFor(*selected);
    const int usedBefore = attacksUsedFor(attackerId);
    if (!attackButtonsAvailable(usedBefore, allotment)) {
        return;
    }
    FightUndo snapshot;
    const bool fresh = beginUndo(FightEdit::Once, nullptr, snapshot);
    m_capturedSelectionId = attackerId;
    disarmAttack();
    m_suppressUndo = true;
    const std::optional<int> attacksUsed = spendMonsterAction(*encounter, turn, usedBefore, allotment);
    if (!attacksUsed.has_value()) {
        m_suppressUndo = false;
        return;
    }
    if (fresh) {
        keepUndo(FightEdit::Once, std::move(snapshot));
    }
    m_attacksUsedById[attackerId] = *attacksUsed;
    rebuildCombatantList(currentTurnId());
    persist();
    m_suppressUndo = false;
}

void CombatPage::armAttack(const std::string& effect, int total)
{
    m_armedEffect = effect;
    m_armedDamage = total;
    m_armedAttackerId.clear();
    m_hasSelectionBeforeAttack = false;
    m_selectionBeforeAttack.clear();
    if (const Encounter* encounter = selectedEncounter()) {
        const int turn = encounter->turnIndex;
        if (turn >= 0 && turn < static_cast<int>(encounter->combatants.size())) {
            const Combatant& attacker = encounter->combatants[static_cast<std::size_t>(turn)];
            if (isMonsterCombatant(attacker)) {
                m_armedAttackerId = attacker.id;
            }
        }
    }
    if (!m_swordCursor) {
        QApplication::setOverrideCursor(swordCursor());
        m_swordCursor = true;
    }
}

void CombatPage::disarmAttack()
{
    m_armedDamage.reset();
    m_armedEffect.clear();
    m_armedAttackerId.clear();
    m_hasSelectionBeforeAttack = false;
    m_selectionBeforeAttack.clear();
    if (!m_swordCursor) {
        return;
    }
    QApplication::restoreOverrideCursor();
    m_swordCursor = false;
}

void CombatPage::rememberArmedSelection(QTreeWidgetItem* previous, QTreeWidget* otherList)
{
    if (!m_armedDamage.has_value() || m_populating) {
        return;
    }
    QTreeWidgetItem* source = previous;
    if (source == nullptr && otherList != nullptr) {
        source = otherList->currentItem();
    }
    if (source == nullptr) {
        m_selectionBeforeAttack.clear();
    } else {
        m_selectionBeforeAttack = source->data(0, Qt::UserRole).toString().toStdString();
    }
    m_hasSelectionBeforeAttack = true;
}

void CombatPage::undoLastChange()
{
    if (!m_undo.has_value()) {
        return;
    }
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || encounter->id != m_undo->fight.encounter.id) {
        m_undo.reset();
        m_openEdit = FightEdit::None;
        m_undoButton->setEnabled(false);
        return;
    }
    const PageUndo undo = std::move(*m_undo);
    m_undo.reset();
    m_openEdit = FightEdit::None;
    m_undoButton->setEnabled(false);
    m_undoing = true;
    const bool sheetRestored = restoreFightUndo(*encounter, m_characters, undo.fight);
    m_attacksUsedById = undo.attacksUsedById;
    m_countedCombatantId = undo.countedCombatantId;
    m_countedRound = undo.countedRound;
    setHitText(undo.hitText);
    if (undo.fight.sheet.has_value() && sheetRestored) {
        saveCharacters();
    }
    std::string selectId;
    if (undo.selectedCombatantId.has_value()) {
        selectId = *undo.selectedCombatantId;
    } else if (const Combatant* selected = selectedCombatant()) {
        selectId = selected->id;
    }
    rebuildCombatantList(selectId);
    persist();
    m_undoing = false;
}

void CombatPage::setHitText(const QString& text)
{
    m_hitText = text;
    if (m_hitLabel == nullptr) {
        return;
    }
    m_hitLabel->setText(text);
    m_hitLabel->setVisible(!text.isEmpty());
}

void CombatPage::syncAttackCount()
{
    const Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    std::string turnId;
    const int turn = encounter->turnIndex;
    if (turn >= 0 && turn < static_cast<int>(encounter->combatants.size())) {
        turnId = encounter->combatants[static_cast<std::size_t>(turn)].id;
    }
    if (turnId != m_countedCombatantId || encounter->round != m_countedRound) {
        if (!turnId.empty()) {
            m_attacksUsedById[turnId] = 0;
        }
        m_countedCombatantId = std::move(turnId);
        m_countedRound = encounter->round;
    }
}

int CombatPage::attacksUsedFor(const std::string& id) const
{
    const auto found = m_attacksUsedById.find(id);
    if (found == m_attacksUsedById.end()) {
        return 0;
    }
    return found->second;
}

int CombatPage::allotmentFor(const Combatant& combatant) const
{
    if (!isMonsterCombatant(combatant)) {
        return 1;
    }
    const std::optional<Monster> monster = m_catalog.findById(combatant.sourceId);
    if (!monster.has_value()) {
        return 1;
    }
    return attackAllotment(monster->attacks);
}

std::string CombatPage::currentTurnId()
{
    const Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return {};
    }
    const int turn = encounter->turnIndex;
    if (turn < 0 || turn >= static_cast<int>(encounter->combatants.size())) {
        return {};
    }
    return encounter->combatants[static_cast<std::size_t>(turn)].id;
}

bool CombatPage::beginUndo(FightEdit edit, const Combatant* sheetCombatant, FightUndo& snapshot)
{
    if (m_populating || m_undoing || m_suppressUndo || edit == FightEdit::None) {
        return false;
    }
    if (m_undo.has_value() && m_openEdit == edit && edit != FightEdit::Once) {
        return false;
    }
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return false;
    }
    snapshot.encounter = *encounter;
    snapshot.sheet.reset();
    if (sheetCombatant != nullptr) {
        if (const Character* character = characterFor(*sheetCombatant)) {
            snapshot.sheet = *character;
        }
    }
    m_capturedAttacksUsedById = m_attacksUsedById;
    m_capturedCountedCombatantId = m_countedCombatantId;
    m_capturedCountedRound = m_countedRound;
    m_capturedHitText = m_hitText;
    m_capturedSelectionId.reset();
    return true;
}

void CombatPage::keepUndo(FightEdit edit, FightUndo snapshot)
{
    PageUndo page;
    page.fight = std::move(snapshot);
    page.attacksUsedById = m_capturedAttacksUsedById;
    page.countedCombatantId = m_capturedCountedCombatantId;
    page.countedRound = m_capturedCountedRound;
    page.hitText = m_capturedHitText;
    page.selectedCombatantId = m_capturedSelectionId;
    m_undo = std::move(page);
    m_openEdit = edit;
    m_undoButton->setEnabled(true);
}

bool CombatPage::fightChanged(const FightUndo& snapshot, const Combatant* sheetCombatant)
{
    const Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return false;
    }
    if (*encounter != snapshot.encounter) {
        return true;
    }
    if (!snapshot.sheet.has_value() || sheetCombatant == nullptr) {
        return false;
    }
    const Character* character = characterFor(*sheetCombatant);
    return character == nullptr || *character != *snapshot.sheet;
}

void CombatPage::closeFightEdit(FightEdit edit)
{
    if (m_openEdit == edit) {
        m_openEdit = FightEdit::None;
    }
}

}  // namespace combat::ui
