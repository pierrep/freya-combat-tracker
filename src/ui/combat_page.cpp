#include "ui/combat_page.h"

#include "core/character_store.h"
#include "core/encounter_store.h"
#include "core/monster_catalog.h"
#include "core/uuid.h"
#include "ui/page_title.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <limits>

namespace combat::ui {

namespace {

bool isBlank(const QString& text)
{
    return text.trimmed().isEmpty();
}

QSpinBox* makeNumberBox()
{
    auto* box = new QSpinBox;
    box->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    box->setMaximumWidth(140);
    return box;
}

}  // namespace

CombatPage::CombatPage(CharacterStore& characters, MonsterCatalog& catalog, EncounterStore& encounters,
                       QWidget* parent)
    : QWidget(parent)
    , m_charactersStore(characters)
    , m_catalog(catalog)
    , m_encountersStore(encounters)
    , m_dice(std::random_device{}())
    , m_ids(std::random_device{}())
{
    try {
        m_encounters = m_encountersStore.loadAll();
    } catch (const EncounterStoreError& error) {
        m_loadError = QString::fromStdString(error.what());
    }
    reloadCharacters();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 24, 32, 24);
    outer->setSpacing(16);
    outer->addWidget(makePageTitle(tr("Combat")));

    if (hasLoadError()) {
        auto* banner = new QLabel(tr("The encounters file could not be read, so it has not been changed.\n%1")
                                      .arg(m_loadError));
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
        outer->addWidget(banner);
    }

    auto* columns = new QHBoxLayout;
    columns->setSpacing(24);
    outer->addLayout(columns, 1);

    auto* left = new QVBoxLayout;
    m_encounterList = new QListWidget;
    m_encounterList->setObjectName(QStringLiteral("encounterList"));
    m_encounterList->setMinimumWidth(200);
    m_encounterList->setMaximumWidth(260);
    left->addWidget(m_encounterList, 1);
    auto* listButtons = new QHBoxLayout;
    m_addEncounterButton = new QPushButton(tr("Add"));
    m_deleteEncounterButton = new QPushButton(tr("Delete"));
    listButtons->addWidget(m_addEncounterButton);
    listButtons->addWidget(m_deleteEncounterButton);
    left->addLayout(listButtons);
    columns->addLayout(left);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* rightHost = new QWidget;
    auto* right = new QVBoxLayout(rightHost);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(12);
    scroll->setWidget(rightHost);
    columns->addWidget(scroll, 1);

    m_emptyHint = new QLabel(tr("Add an encounter to start a fight."));
    m_emptyHint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(m_emptyHint);

    m_fight = new QWidget;
    m_fight->setObjectName(QStringLiteral("combatFight"));
    auto* fightLayout = new QVBoxLayout(m_fight);
    fightLayout->setContentsMargins(0, 0, 0, 0);
    fightLayout->setSpacing(12);

    m_encounterName = new QLineEdit;
    m_encounterName->setObjectName(QStringLiteral("encounterName"));
    m_encounterName->setPlaceholderText(tr("Required"));
    m_nameError = new QLabel(tr("Name is required."));
    m_nameError->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
    m_nameError->hide();
    auto* nameColumn = new QVBoxLayout;
    nameColumn->setSpacing(2);
    nameColumn->addWidget(m_encounterName);
    nameColumn->addWidget(m_nameError);
    auto* nameForm = new QFormLayout;
    nameForm->addRow(tr("Encounter"), nameColumn);
    fightLayout->addLayout(nameForm);

    auto* hpNote = new QLabel(tr("Hit points in this fight are a separate copy. Changing them does not change the "
                                 "character or the monster."));
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

    m_noCombatantHint = new QLabel(tr("Add a character or a monster."));
    fightLayout->addWidget(m_noCombatantHint);

    m_combatantForm = new QWidget;
    auto* form = new QFormLayout(m_combatantForm);
    m_combatantName = new QLabel;
    m_combatantName->setObjectName(QStringLiteral("combatantName"));
    form->addRow(tr("Name"), m_combatantName);
    m_combatantSource = new QLabel;
    form->addRow(tr("From"), m_combatantSource);
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
    form->addRow(tr("HP"), m_hp);
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

    auto* addCharacterRow = new QHBoxLayout;
    m_characterCombo = new QComboBox;
    m_characterCombo->setObjectName(QStringLiteral("characterCombo"));
    m_addCharacterButton = new QPushButton(tr("Add character"));
    m_addCharacterButton->setObjectName(QStringLiteral("addCharacter"));
    addCharacterRow->addWidget(m_characterCombo, 1);
    addCharacterRow->addWidget(m_addCharacterButton);
    fightLayout->addLayout(addCharacterRow);
    if (!m_characterLoadError.isEmpty()) {
        auto* characterError = new QLabel(tr("Characters could not be read, so none can be added.\n%1")
                                              .arg(m_characterLoadError));
        characterError->setWordWrap(true);
        characterError->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
        fightLayout->addWidget(characterError);
    }

    m_monsterSearch = new QLineEdit;
    m_monsterSearch->setObjectName(QStringLiteral("monsterSearch"));
    m_monsterSearch->setPlaceholderText(tr("Search monsters by name"));
    fightLayout->addWidget(m_monsterSearch);
    auto* addMonsterRow = new QHBoxLayout;
    m_monsterChoices = new QListWidget;
    m_monsterChoices->setObjectName(QStringLiteral("monsterChoices"));
    m_monsterChoices->setMaximumHeight(140);
    m_addMonsterButton = new QPushButton(tr("Add monster"));
    m_addMonsterButton->setObjectName(QStringLiteral("addMonster"));
    addMonsterRow->addWidget(m_monsterChoices, 1);
    addMonsterRow->addWidget(m_addMonsterButton, 0, Qt::AlignTop);
    fightLayout->addLayout(addMonsterRow);
    fightLayout->addStretch(1);

    right->addWidget(m_fight, 1);

    connect(m_encounterList, &QListWidget::currentRowChanged, this, &CombatPage::showEncounter);
    connect(m_addEncounterButton, &QPushButton::clicked, this, &CombatPage::addEncounter);
    connect(m_deleteEncounterButton, &QPushButton::clicked, this, &CombatPage::deleteEncounter);
    connect(m_encounterName, &QLineEdit::textEdited, this, &CombatPage::onEncounterNameEdited);
    connect(m_encounterName, &QLineEdit::editingFinished, this, &CombatPage::onEncounterNameEditingFinished);
    connect(m_combatantList, &QListWidget::currentRowChanged, this, &CombatPage::showCombatant);
    connect(m_initiative, &QSpinBox::valueChanged, this, &CombatPage::onInitiativeChanged);
    connect(m_initiative, &QSpinBox::editingFinished, this, &CombatPage::onInitiativeEditingFinished);
    connect(m_hp, &QSpinBox::valueChanged, this, &CombatPage::onHpChanged);
    connect(m_rollAllButton, &QPushButton::clicked, this, &CombatPage::rollAll);
    connect(m_rerollButton, &QPushButton::clicked, this, &CombatPage::rerollSelected);
    connect(m_moveUpButton, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_moveDownButton, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(m_removeButton, &QPushButton::clicked, this, &CombatPage::removeSelected);
    connect(m_addCharacterButton, &QPushButton::clicked, this, &CombatPage::addCharacter);
    connect(m_addMonsterButton, &QPushButton::clicked, this, &CombatPage::addSelectedMonster);
    connect(m_monsterChoices, &QListWidget::itemDoubleClicked, this, [this] { addSelectedMonster(); });
    connect(m_monsterSearch, &QLineEdit::textChanged, this, &CombatPage::refreshMonsterChoices);
    connect(m_monsterChoices, &QListWidget::currentRowChanged, this, [this](int row) {
        m_addMonsterButton->setEnabled(row >= 0 && selectedEncounter() != nullptr && !hasLoadError());
    });
    connect(m_previousTurnButton, &QPushButton::clicked, this, &CombatPage::previousTurn);
    connect(m_nextTurnButton, &QPushButton::clicked, this, &CombatPage::nextTurn);
    connect(m_nextRoundButton, &QPushButton::clicked, this, &CombatPage::nextRound);

    reloadCharacters();

    for (const Encounter& encounter : m_encounters) {
        m_encounterList->addItem(QString::fromStdString(encounter.name));
    }
    refreshMonsterChoices();

    if (hasLoadError()) {
        m_encounterList->setEnabled(false);
        m_addEncounterButton->setEnabled(false);
        m_deleteEncounterButton->setEnabled(false);
        m_emptyHint->hide();
        m_fight->hide();
        return;
    }

    if (!m_encounters.empty()) {
        m_encounterList->setCurrentRow(0);
    }
    showEncounter();
}

void CombatPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!hasLoadError()) {
        reloadCharacters();
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

void CombatPage::reloadCharacters()
{
    try {
        m_characters = m_charactersStore.loadAll();
        m_characterLoadError.clear();
    } catch (const CharacterStoreError& error) {
        m_characterLoadError = QString::fromStdString(error.what());
        return;
    }
    if (m_characterCombo == nullptr) {
        return;
    }
    const QString previous = m_characterCombo->currentData().toString();
    QSignalBlocker blocker(m_characterCombo);
    m_characterCombo->clear();
    for (const Character& character : m_characters) {
        m_characterCombo->addItem(QString::fromStdString(character.name), QString::fromStdString(character.id));
    }
    const int row = m_characterCombo->findData(previous);
    if (row >= 0) {
        m_characterCombo->setCurrentIndex(row);
    }
    const bool canAdd = !m_characters.empty() && !hasLoadError();
    m_characterCombo->setEnabled(canAdd);
    m_addCharacterButton->setEnabled(canAdd && selectedEncounter() != nullptr);
}

void CombatPage::refreshMonsterChoices()
{
    if (m_monsterChoices == nullptr || m_monsterSearch == nullptr) {
        return;
    }
    MonsterQuery query;
    query.nameSubstring = m_monsterSearch->text().toStdString();
    const std::vector<Monster> matches = m_catalog.search(query);
    QSignalBlocker blocker(m_monsterChoices);
    m_monsterChoices->clear();
    for (const Monster& monster : matches) {
        auto* item = new QListWidgetItem(QString::fromStdString(monster.name));
        if (monster.source == kCustomMonsterSource) {
            item->setText(item->text() + QStringLiteral("    Custom"));
        }
        item->setData(Qt::UserRole, QString::fromStdString(monster.id));
        m_monsterChoices->addItem(item);
    }
    m_addMonsterButton->setEnabled(false);
}

int CombatPage::rollD20()
{
    std::uniform_int_distribution<int> face(1, 20);
    return face(m_dice);
}

Encounter* CombatPage::selectedEncounter()
{
    if (m_encounterList == nullptr) {
        return nullptr;
    }
    const int row = m_encounterList->currentRow();
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
    return marker + QString::number(combatant.initiative) + QStringLiteral("    ") +
           QString::fromStdString(combatant.name) + QStringLiteral("    AC ") + QString::number(combatant.ac) +
           QStringLiteral("    HP ") + QString::number(combatant.hp);
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

void CombatPage::addEncounter()
{
    Encounter encounter;
    encounter.id = generateUuidV4([this] { return m_ids(); });
    encounter.name = tr("New encounter").toStdString();
    m_encounters.push_back(encounter);
    m_encounterList->addItem(QString::fromStdString(encounter.name));
    persist();
    m_encounterList->setCurrentRow(static_cast<int>(m_encounters.size()) - 1);
    m_encounterName->setFocus();
    m_encounterName->selectAll();
}

void CombatPage::deleteEncounter()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Delete encounter"),
        tr("Delete %1? This cannot be undone.").arg(QString::fromStdString(encounter->name)));
    if (answer != QMessageBox::Yes) {
        return;
    }
    const int row = m_encounterList->currentRow();
    m_encounters.erase(m_encounters.begin() + row);
    delete m_encounterList->takeItem(row);
    persist();
    showEncounter();
}

void CombatPage::showEncounter()
{
    Encounter* encounter = selectedEncounter();
    m_deleteEncounterButton->setEnabled(encounter != nullptr && !hasLoadError());
    m_fight->setVisible(encounter != nullptr);
    m_emptyHint->setVisible(encounter == nullptr);
    m_addCharacterButton->setEnabled(encounter != nullptr && m_characterLoadError.isEmpty() && !m_characters.empty());
    m_characterCombo->setEnabled(encounter != nullptr && m_characterLoadError.isEmpty() && !m_characters.empty());
    m_monsterSearch->setEnabled(encounter != nullptr);
    m_monsterChoices->setEnabled(encounter != nullptr);
    m_addMonsterButton->setEnabled(encounter != nullptr && m_monsterChoices->currentRow() >= 0);
    if (encounter == nullptr) {
        return;
    }
    m_populating = true;
    m_encounterName->setText(QString::fromStdString(encounter->name));
    m_nameError->hide();
    m_populating = false;
    m_rollNote->hide();
    m_rollNote->clear();
    rebuildCombatantList({});
}

void CombatPage::onEncounterNameEdited(const QString& text)
{
    Encounter* encounter = selectedEncounter();
    if (m_populating || encounter == nullptr) {
        return;
    }
    m_nameError->setVisible(isBlank(text));
    if (isBlank(text)) {
        return;
    }
    encounter->name = text.toStdString();
    if (QListWidgetItem* item = m_encounterList->currentItem()) {
        item->setText(text);
    }
    persist();
}

void CombatPage::onEncounterNameEditingFinished()
{
    const Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || !isBlank(m_encounterName->text())) {
        return;
    }
    m_encounterName->setText(QString::fromStdString(encounter->name));
    m_nameError->hide();
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
    m_populating = false;
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
    updateCombatantItemText(m_combatantList->currentRow());
    persist();
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

void CombatPage::addCharacter()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    const QString id = m_characterCombo->currentData().toString();
    const Character* character = nullptr;
    for (const Character& candidate : m_characters) {
        if (QString::fromStdString(candidate.id) == id) {
            character = &candidate;
            break;
        }
    }
    if (character == nullptr) {
        return;
    }
    Combatant combatant = makeCharacterCombatant(*character, generateUuidV4([this] { return m_ids(); }));
    const std::string combatantId = combatant.id;
    encounter->combatants.push_back(std::move(combatant));
    encounter->turnIndex = sortByInitiative(encounter->combatants, encounter->turnIndex);
    rebuildCombatantList(combatantId);
    persist();
}

void CombatPage::addSelectedMonster()
{
    Encounter* encounter = selectedEncounter();
    QListWidgetItem* item = m_monsterChoices->currentItem();
    if (encounter == nullptr || item == nullptr) {
        return;
    }
    const auto monster = m_catalog.findById(item->data(Qt::UserRole).toString().toStdString());
    if (!monster.has_value()) {
        return;
    }
    Combatant combatant = makeMonsterCombatant(*monster, encounter->combatants, generateUuidV4([this] { return m_ids(); }));
    const std::string combatantId = combatant.id;
    encounter->combatants.push_back(std::move(combatant));
    encounter->turnIndex = sortByInitiative(encounter->combatants, encounter->turnIndex);
    rebuildCombatantList(combatantId);
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
