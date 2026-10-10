#include "ui/encounter_builder_page.h"
#include "ui/session_state.h"

#include "core/character_store.h"
#include "core/encounter_difficulty.h"
#include "core/encounter_store.h"
#include "core/monster_catalog.h"
#include "core/uuid.h"
#include "ui/page_title.h"
#include "ui/theme.h"

#include <QComboBox>
#include <QFrame>
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
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace combat::ui {

namespace {

bool isBlank(const QString& text)
{
    return text.trimmed().isEmpty();
}

}  // namespace

EncounterBuilderPage::EncounterBuilderPage(CharacterStore& characters, MonsterCatalog& catalog,
                                           EncounterStore& encounters, QWidget* parent)
    : QWidget(parent)
    , m_charactersStore(characters)
    , m_catalog(catalog)
    , m_encountersStore(encounters)
    , m_ids(std::random_device{}())
{
    try {
        m_encounters = m_encountersStore.loadAll();
    } catch (const EncounterStoreError& error) {
        m_loadError = QString::fromStdString(error.what());
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 20, 28, 20);
    outer->setSpacing(14);

    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    header->setObjectName(QStringLiteral("pageHeader"));
    header->addWidget(makePageTitle(tr("Encounter Builder")));
    header->addStretch(1);
    m_addEncounterButton = new QPushButton(tr("New encounter"));
    m_addEncounterButton->setObjectName(QStringLiteral("newEncounter"));
    makePrimary(m_addEncounterButton);
    header->addWidget(m_addEncounterButton);
    outer->addLayout(header);

    if (hasLoadError()) {
        auto* banner = new QLabel(tr("The encounters file could not be read, so it has not been changed.\n%1")
                                      .arg(m_loadError));
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setProperty("role", QStringLiteral("banner-error"));
        outer->addWidget(banner);
    }

    auto* columns = new QHBoxLayout;
    columns->setSpacing(14);
    outer->addLayout(columns, 1);

    // Left: the adventure chosen, then its encounters.
    auto* leftColumn = new QVBoxLayout;
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(14);
    auto* adventureCard = makeCard();
    adventureCard->setObjectName(QStringLiteral("adventureCard"));
    adventureCard->setFixedWidth(260);
    auto* adventureLayout = static_cast<QVBoxLayout*>(adventureCard->layout());
    adventureLayout->setContentsMargins(16, 12, 16, 12);
    adventureLayout->setSpacing(8);
    auto* adventureHeader = new QHBoxLayout;
    adventureHeader->addWidget(makeHeading(tr("Adventure")));
    adventureHeader->addStretch(1);
    m_newAdventureButton = new QPushButton(tr("New"));
    m_newAdventureButton->setObjectName(QStringLiteral("newAdventure"));
    m_newAdventureButton->setToolTip(tr("Make an adventure: a group of encounters, and the party they start with."));
    makeQuiet(m_newAdventureButton);
    adventureHeader->addWidget(m_newAdventureButton);
    adventureLayout->addLayout(adventureHeader);
    m_adventureFilter = new QComboBox;
    m_adventureFilter->setObjectName(QStringLiteral("builderAdventure"));
    m_adventureFilter->setToolTip(tr("List the encounters of one adventure."));
    adventureLayout->addWidget(m_adventureFilter);
    m_adventureDetails = new QWidget;
    auto* details = new QVBoxLayout(m_adventureDetails);
    details->setContentsMargins(0, 0, 0, 0);
    details->setSpacing(6);
    m_adventureName = new QLineEdit;
    m_adventureName->setObjectName(QStringLiteral("adventureName"));
    m_adventureName->setPlaceholderText(tr("Adventure name"));
    details->addWidget(m_adventureName);
    auto* partyRow = new QHBoxLayout;
    partyRow->setSpacing(6);
    auto* partyLabel = makeMuted(tr("New encounters start with"));
    m_adventureParty = new QComboBox;
    m_adventureParty->setObjectName(QStringLiteral("adventureParty"));
    details->addWidget(partyLabel);
    partyRow->addWidget(m_adventureParty, 1);
    m_deleteAdventureButton = new QPushButton(tr("Delete"));
    m_deleteAdventureButton->setObjectName(QStringLiteral("deleteAdventure"));
    m_deleteAdventureButton->setToolTip(tr("Delete the adventure. Its encounters stay, with no adventure."));
    makeQuiet(m_deleteAdventureButton);
    partyRow->addWidget(m_deleteAdventureButton);
    details->addLayout(partyRow);
    adventureLayout->addWidget(m_adventureDetails);
    leftColumn->addWidget(adventureCard);

    auto* listCard = makeCard();
    listCard->setFixedWidth(260);
    listCard->layout()->setContentsMargins(8, 12, 8, 8);
    auto* listHeading = makeHeading(tr("Encounters"));
    listHeading->setContentsMargins(8, 0, 0, 0);
    listCard->layout()->addWidget(listHeading);
    m_encounterList = new QListWidget;
    m_encounterList->setObjectName(QStringLiteral("encounterList"));
    static_cast<QVBoxLayout*>(listCard->layout())->addWidget(m_encounterList, 1);
    leftColumn->addWidget(listCard, 1);
    columns->addLayout(leftColumn);

    auto* right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    columns->addLayout(right, 1);
    m_emptyHint = makeMuted(tr("No encounters yet. Choose New encounter, then add characters and monsters."));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    right->addWidget(m_emptyHint, 1);

    // Right: one card for the selected encounter.
    m_editor = makeCard();
    m_editor->setObjectName(QStringLiteral("encounterEditor"));
    auto* editorLayout = static_cast<QVBoxLayout*>(m_editor->layout());
    editorLayout->setContentsMargins(20, 16, 20, 16);
    editorLayout->setSpacing(10);
    right->addWidget(m_editor, 1);

    auto* nameRow = new QHBoxLayout;
    nameRow->setSpacing(10);
    m_encounterName = new QLineEdit;
    m_encounterName->setObjectName(QStringLiteral("encounterName"));
    m_encounterName->setPlaceholderText(tr("Encounter name (required)"));
    QFont nameFont = m_encounterName->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.35);
    nameFont.setWeight(QFont::DemiBold);
    m_encounterName->setFont(nameFont);
    m_encounterName->setMaximumWidth(380);
    m_difficultyPill = new QLabel;
    m_difficultyPill->setObjectName(QStringLiteral("encounterDifficultyPill"));
    m_difficultyPill->setProperty("role", QStringLiteral("pill"));
    m_difficultyPill->hide();
    m_resetEncounterButton = new QPushButton(tr("Reset"));
    m_resetEncounterButton->setObjectName(QStringLiteral("resetEncounter"));
    m_resetEncounterButton->setToolTip(
        tr("Monsters back to full HP with no conditions, recharges, or daily uses spent. Round 1."));
    m_deleteEncounterButton = new QPushButton(tr("Delete"));
    m_deleteEncounterButton->setObjectName(QStringLiteral("deleteEncounter"));
    makeQuiet(m_deleteEncounterButton);
    nameRow->addWidget(m_encounterName, 1);
    nameRow->addWidget(m_difficultyPill);
    nameRow->addStretch(0);
    nameRow->addWidget(m_resetEncounterButton);
    nameRow->addWidget(m_deleteEncounterButton);
    editorLayout->addLayout(nameRow);
    m_nameError = new QLabel(tr("Name is required."));
    m_nameError->setProperty("role", QStringLiteral("banner-error"));
    m_nameError->hide();
    editorLayout->addWidget(m_nameError);
    m_difficulty = makeMuted(QString());
    m_difficulty->setObjectName(QStringLiteral("encounterDifficulty"));
    editorLayout->addWidget(m_difficulty);
    auto* adventureRow = new QHBoxLayout;
    adventureRow->setSpacing(8);
    adventureRow->addWidget(makeMuted(tr("Adventure")));
    m_encounterAdventure = new QComboBox;
    m_encounterAdventure->setObjectName(QStringLiteral("encounterAdventure"));
    m_encounterAdventure->setMinimumWidth(200);
    adventureRow->addWidget(m_encounterAdventure);
    adventureRow->addStretch(1);
    editorLayout->addLayout(adventureRow);
    editorLayout->addSpacing(6);

    // Who is in it, and what can be added, side by side.
    auto* panes = new QHBoxLayout;
    panes->setSpacing(24);
    editorLayout->addLayout(panes, 1);

    auto* rosterPane = new QVBoxLayout;
    rosterPane->setSpacing(8);
    auto* rosterHeader = new QHBoxLayout;
    rosterHeader->addWidget(makeHeading(tr("In this encounter")));
    rosterHeader->addStretch(1);
    m_removeCombatantButton = new QPushButton(tr("Remove"));
    m_removeCombatantButton->setObjectName(QStringLiteral("removeCombatant"));
    m_removeCombatantButton->setToolTip(tr("Remove the selected combatant from this encounter."));
    m_removeCombatantButton->setEnabled(false);
    makeQuiet(m_removeCombatantButton);
    rosterHeader->addWidget(m_removeCombatantButton);
    rosterPane->addLayout(rosterHeader);
    m_rosterHint = makeMuted(tr("No one is in this encounter yet. Add characters and monsters on the right."));
    m_rosterHint->setObjectName(QStringLiteral("encounterRosterHint"));
    rosterPane->addWidget(m_rosterHint);
    m_roster = new QListWidget;
    m_roster->setObjectName(QStringLiteral("encounterRoster"));
    m_roster->setSelectionMode(QAbstractItemView::SingleSelection);
    m_roster->setMinimumHeight(80);
    m_roster->setProperty("inset", true);
    rosterPane->addWidget(m_roster, 1);
    rosterPane->addStretch(0);
    panes->addLayout(rosterPane, 1);

    auto* addPane = new QVBoxLayout;
    addPane->setSpacing(8);
    addPane->addWidget(makeHeading(tr("Add to encounter")));
    auto* addPartyRow = new QHBoxLayout;
    m_partyCombo = new QComboBox;
    m_partyCombo->setObjectName(QStringLiteral("partyCombo"));
    m_addPartyButton = new QPushButton(tr("Add party"));
    m_addPartyButton->setObjectName(QStringLiteral("addParty"));
    m_addPartyButton->setToolTip(tr("Every member of the party. Until the fight starts, the encounter follows the "
                                    "party: who joins it is added, who leaves it goes."));
    makePrimary(m_addPartyButton);
    addPartyRow->addWidget(m_partyCombo, 1);
    addPartyRow->addWidget(m_addPartyButton);
    addPane->addLayout(addPartyRow);
    auto* partyNoteRow = new QHBoxLayout;
    m_partyNote = makeMuted(QString());
    m_partyNote->setObjectName(QStringLiteral("encounterPartyNote"));
    m_partyNote->setWordWrap(true);
    m_removePartyButton = new QPushButton(tr("Remove party"));
    m_removePartyButton->setObjectName(QStringLiteral("removeParty"));
    m_removePartyButton->setToolTip(tr("Take the party's members out. Characters added on their own stay."));
    makeQuiet(m_removePartyButton);
    partyNoteRow->addWidget(m_partyNote, 1);
    partyNoteRow->addWidget(m_removePartyButton);
    addPane->addLayout(partyNoteRow);
    auto* addCharacterRow = new QHBoxLayout;
    m_characterCombo = new QComboBox;
    m_characterCombo->setObjectName(QStringLiteral("characterCombo"));
    m_addCharacterButton = new QPushButton(tr("Add character"));
    m_addCharacterButton->setObjectName(QStringLiteral("addCharacter"));
    m_addCharacterButton->setToolTip(tr("One character on their own, not with a party (a guest)."));
    addCharacterRow->addWidget(m_characterCombo, 1);
    addCharacterRow->addWidget(m_addCharacterButton);
    addPane->addLayout(addCharacterRow);
    addPane->addSpacing(6);
    m_monsterSearch = new QLineEdit;
    m_monsterSearch->setObjectName(QStringLiteral("monsterSearch"));
    m_monsterSearch->setPlaceholderText(tr("Search monsters by name"));
    m_monsterSearch->setClearButtonEnabled(true);
    addPane->addWidget(m_monsterSearch);
    m_monsterChoices = new QListWidget;
    m_monsterChoices->setObjectName(QStringLiteral("monsterChoices"));
    m_monsterChoices->setMinimumHeight(160);
    m_monsterChoices->setProperty("inset", true);
    addPane->addWidget(m_monsterChoices, 1);
    auto* addMonsterRow = new QHBoxLayout;
    m_monsterQuantity = new QSpinBox;
    m_monsterQuantity->setObjectName(QStringLiteral("monsterQuantity"));
    m_monsterQuantity->setRange(1, 20);
    m_monsterQuantity->setPrefix(tr("× "));
    m_monsterQuantity->setMaximumWidth(70);
    m_addMonsterButton = new QPushButton(tr("Add monster"));
    m_addMonsterButton->setObjectName(QStringLiteral("addMonster"));
    makePrimary(m_addMonsterButton);
    addMonsterRow->addStretch(1);
    addMonsterRow->addWidget(makeMuted(tr("How many")));
    addMonsterRow->addWidget(m_monsterQuantity);
    addMonsterRow->addWidget(m_addMonsterButton);
    addPane->addLayout(addMonsterRow);
    auto* note = makeMuted(tr("Characters and parties come from the Characters page and custom monsters from the "
                              "Monsters page."));
    addPane->addWidget(note);
    panes->addLayout(addPane, 1);

    connect(m_encounterList, &QListWidget::currentRowChanged, this, &EncounterBuilderPage::showEncounter);
    connect(m_addEncounterButton, &QPushButton::clicked, this, &EncounterBuilderPage::addEncounter);
    connect(m_deleteEncounterButton, &QPushButton::clicked, this, &EncounterBuilderPage::deleteEncounter);
    connect(m_resetEncounterButton, &QPushButton::clicked, this, &EncounterBuilderPage::resetEncounter);
    connect(m_encounterName, &QLineEdit::textEdited, this, &EncounterBuilderPage::onEncounterNameEdited);
    connect(m_encounterName, &QLineEdit::editingFinished, this, &EncounterBuilderPage::onEncounterNameEditingFinished);
    connect(m_addCharacterButton, &QPushButton::clicked, this, &EncounterBuilderPage::addCharacter);
    connect(m_addPartyButton, &QPushButton::clicked, this, &EncounterBuilderPage::addParty);
    connect(m_removePartyButton, &QPushButton::clicked, this, &EncounterBuilderPage::removeParty);
    connect(m_adventureFilter, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &EncounterBuilderPage::onAdventureFilterChanged);
    connect(m_newAdventureButton, &QPushButton::clicked, this, &EncounterBuilderPage::addAdventure);
    connect(m_deleteAdventureButton, &QPushButton::clicked, this, &EncounterBuilderPage::deleteAdventure);
    connect(m_adventureName, &QLineEdit::textEdited, this, &EncounterBuilderPage::onAdventureNameEdited);
    connect(m_adventureParty, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &EncounterBuilderPage::onAdventurePartyChosen);
    connect(m_encounterAdventure, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &EncounterBuilderPage::onEncounterAdventureChosen);
    connect(m_addMonsterButton, &QPushButton::clicked, this, &EncounterBuilderPage::addSelectedMonster);
    connect(m_removeCombatantButton, &QPushButton::clicked, this, &EncounterBuilderPage::removeSelectedCombatant);
    connect(m_roster, &QListWidget::currentRowChanged, this,
            [this](int row) { m_removeCombatantButton->setEnabled(row >= 0 && !hasLoadError()); });
    connect(m_monsterChoices, &QListWidget::itemDoubleClicked, this, [this] { addSelectedMonster(); });
    connect(m_monsterSearch, &QLineEdit::textChanged, this, &EncounterBuilderPage::refreshMonsterChoices);
    connect(m_monsterChoices, &QListWidget::currentRowChanged, this, [this](int row) {
        m_addMonsterButton->setEnabled(row >= 0 && selectedEncounter() != nullptr && !hasLoadError());
    });

    if (hasLoadError()) {
        m_encounterList->setEnabled(false);
        m_addEncounterButton->setEnabled(false);
        m_deleteEncounterButton->setEnabled(false);
        m_resetEncounterButton->setEnabled(false);
        m_emptyHint->hide();
        m_editor->hide();
        return;
    }

    reloadFromDisk();
}

void EncounterBuilderPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!hasLoadError()) {
        reloadFromDisk();
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

void EncounterBuilderPage::reloadFromDisk()
{
    if (hasLoadError() || m_encounterList == nullptr) {
        return;
    }
    QString selectedId;
    if (const Encounter* encounter = selectedEncounter()) {
        selectedId = QString::fromStdString(encounter->id);
    } else {
        // Opening the app: back to the encounter used last time.
        selectedId = readLastEncounter(m_stateFile);
    }
    try {
        m_encounters = m_encountersStore.loadAll();
    } catch (const EncounterStoreError& error) {
        QMessageBox::warning(this, tr("Could not read encounters"), QString::fromStdString(error.what()));
        return;
    }

    reloadCharacters();
    if (m_campaignStore != nullptr) {
        try {
            m_campaign = m_campaignStore->load();
        } catch (const CampaignStoreError& error) {
            QMessageBox::warning(this, tr("Could not read parties and adventures"), QString::fromStdString(error.what()));
        }
        // An encounter not started yet follows its party.
        if (m_characterLoadError.isEmpty() &&
            syncPartyRows(m_encounters, m_campaign, m_characters, [this] { return newId(); })) {
            persist();
        }
    }
    fillPartyChoices();
    fillAdventureChoices();
    fillEncounterList(selectedId);
    refreshMonsterChoices();
    showEncounter();
}

void EncounterBuilderPage::reloadCharacters()
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
    const QSignalBlocker blocker(m_characterCombo);
    m_characterCombo->clear();
    for (const Character& character : m_characters) {
        const QString label = QString::fromStdString(character.name) + QStringLiteral("    ") +
                              QString::fromStdString(formatHitPoints(character.hp.current, character.hp.max));
        m_characterCombo->addItem(label, QString::fromStdString(character.id));
    }
    const int row = m_characterCombo->findData(previous);
    if (row >= 0) {
        m_characterCombo->setCurrentIndex(row);
    }
}

void EncounterBuilderPage::refreshMonsterChoices()
{
    if (m_monsterChoices == nullptr || m_monsterSearch == nullptr) {
        return;
    }
    MonsterQuery query;
    query.nameSubstring = m_monsterSearch->text().toStdString();
    const std::vector<Monster> matches = m_catalog.search(query);
    const QSignalBlocker blocker(m_monsterChoices);
    m_monsterChoices->clear();
    for (const Monster& monster : matches) {
        QString label = QString::fromStdString(monster.name);
        if (!monster.challengeRating.empty()) {
            label += QStringLiteral("    CR ") + QString::fromStdString(monster.challengeRating);
        }
        if (monster.source == kCustomMonsterSource) {
            label += QStringLiteral("    Custom");
        }
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, QString::fromStdString(monster.id));
        m_monsterChoices->addItem(item);
    }
    m_addMonsterButton->setEnabled(false);
}

Encounter* EncounterBuilderPage::selectedEncounter()
{
    if (m_encounterList == nullptr) {
        return nullptr;
    }
    const QListWidgetItem* item = m_encounterList->currentItem();
    if (item == nullptr) {
        return nullptr;
    }
    const std::string id = item->data(Qt::UserRole).toString().toStdString();
    for (Encounter& encounter : m_encounters) {
        if (encounter.id == id) {
            return &encounter;
        }
    }
    return nullptr;
}

void EncounterBuilderPage::persist()
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

void EncounterBuilderPage::addEncounter()
{
    Encounter encounter;
    encounter.id = generateUuidV4([this] { return m_ids(); });
    encounter.name = tr("New encounter").toStdString();
    encounter.started = false;  // initiative first, then Start combat
    // In the adventure chosen, with its party.
    if (const Adventure* adventure = chosenAdventure()) {
        encounter.adventureId = adventure->id;
        if (const Party* party = findParty(m_campaign, adventure->partyId)) {
            applyParty(encounter, *party, m_characters, [this] { return newId(); });
        }
    }
    m_encounters.push_back(encounter);
    auto* item = new QListWidgetItem(QString::fromStdString(encounter.name));
    item->setData(Qt::UserRole, QString::fromStdString(encounter.id));
    m_encounterList->addItem(item);
    persist();
    m_encounterList->setCurrentItem(item);
    m_encounterName->setFocus();
    m_encounterName->selectAll();
}

void EncounterBuilderPage::deleteEncounter()
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
    const std::string id = encounter->id;
    std::erase_if(m_encounters, [&id](const Encounter& candidate) { return candidate.id == id; });
    delete m_encounterList->takeItem(m_encounterList->currentRow());
    persist();
    showEncounter();
}

void EncounterBuilderPage::resetEncounter()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || hasLoadError()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Reset encounter"),
        tr("Reset %1? Monsters go back to full HP with no conditions, recharges, or daily uses spent, and the fight "
           "starts again at round 1. Characters keep their HP; their conditions are removed and their initiative "
           "is cleared so it can be entered again.")
            .arg(QString::fromStdString(encounter->name)));
    if (answer != QMessageBox::Yes) {
        return;
    }
    resetMonsters(*encounter);
    persist();
    rebuildRoster();
}

void EncounterBuilderPage::setStateFile(const QString& path)
{
    m_stateFile = path;
    const QString id = readLastEncounter(path);
    if (const int adventure = m_adventureFilter->findData(readLastAdventure(path)); adventure >= 0) {
        const QSignalBlocker blocker(m_adventureFilter);
        m_adventureFilter->setCurrentIndex(adventure);
        fillAdventureChoices();
        fillEncounterList(id);
        showEncounter();
    }
    for (int i = 0; i < m_encounterList->count(); ++i) {
        if (m_encounterList->item(i)->data(Qt::UserRole).toString() == id) {
            m_encounterList->setCurrentRow(i);
        }
    }
}

void EncounterBuilderPage::showEncounter()
{
    Encounter* encounter = selectedEncounter();
    if (encounter != nullptr) {
        writeLastEncounter(m_stateFile, QString::fromStdString(encounter->id));
    }
    const bool canEdit = encounter != nullptr && !hasLoadError();
    m_deleteEncounterButton->setEnabled(canEdit);
    m_resetEncounterButton->setEnabled(canEdit);
    m_editor->setVisible(canEdit);
    m_emptyHint->setVisible(encounter == nullptr && !hasLoadError());
    const bool canAddCharacter = canEdit && m_characterLoadError.isEmpty() && !m_characters.empty();
    m_addCharacterButton->setEnabled(canAddCharacter);
    m_characterCombo->setEnabled(canAddCharacter);
    const bool canAddParty = canAddCharacter && !m_campaign.parties.empty();
    m_addPartyButton->setEnabled(canAddParty);
    m_partyCombo->setEnabled(canAddParty);
    m_encounterAdventure->setEnabled(canEdit);
    if (encounter != nullptr) {
        const QSignalBlocker blocker(m_encounterAdventure);
        m_encounterAdventure->clear();
        m_encounterAdventure->addItem(tr("No adventure"), QString());
        for (const Adventure& adventure : m_campaign.adventures) {
            m_encounterAdventure->addItem(QString::fromStdString(adventure.name), QString::fromStdString(adventure.id));
        }
        const int row = m_encounterAdventure->findData(QString::fromStdString(encounter->adventureId));
        m_encounterAdventure->setCurrentIndex(row >= 0 ? row : 0);
        const Party* party = findParty(m_campaign, encounter->partyId);
        m_partyNote->setText(party == nullptr ? QString()
                             : encounter->started
                                 ? tr("Party: %1 (the fight has started, so its characters stay as they are).")
                                       .arg(QString::fromStdString(party->name))
                                 : tr("Party: %1. Who joins or leaves it joins or leaves this encounter, until the "
                                      "fight starts.")
                                       .arg(QString::fromStdString(party->name)));
        m_partyNote->setVisible(party != nullptr);
        m_removePartyButton->setVisible(party != nullptr);
        m_removePartyButton->setEnabled(canEdit);
        if (party != nullptr) {
            if (const int at = m_partyCombo->findData(QString::fromStdString(party->id)); at >= 0) {
                m_partyCombo->setCurrentIndex(at);
            }
        }
    }
    m_monsterSearch->setEnabled(canEdit);
    m_monsterChoices->setEnabled(canEdit);
    m_addMonsterButton->setEnabled(canEdit && m_monsterChoices->currentRow() >= 0);
    if (encounter == nullptr) {
        return;
    }
    m_populating = true;
    m_encounterName->setText(QString::fromStdString(encounter->name));
    m_nameError->hide();
    m_populating = false;
    bool changed = assignMonsterCopyNames(encounter->combatants);
    for (Combatant& combatant : encounter->combatants) {
        if (isCharacterCombatant(combatant)) {
            // HP, AC, and the rest follow the sheet, as on the Dashboard.
            if (m_characterLoadError.isEmpty()) {
                for (const Character& character : m_characters) {
                    if (character.id == combatant.sourceId) {
                        changed = refreshCharacterCombatant(combatant, character) || changed;
                    }
                }
            }
        } else if (const auto monster = m_catalog.findById(combatant.sourceId)) {
            changed = fillMonsterSnapshot(combatant, *monster) || changed;
        }
    }
    if (changed) {
        persist();
    }
    rebuildRoster();
}

void EncounterBuilderPage::onEncounterNameEdited(const QString& text)
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

void EncounterBuilderPage::onEncounterNameEditingFinished()
{
    const Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || !isBlank(m_encounterName->text())) {
        return;
    }
    m_encounterName->setText(QString::fromStdString(encounter->name));
    m_nameError->hide();
}

void EncounterBuilderPage::rebuildRoster()
{
    const Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    m_roster->clear();
    m_rosterHint->setVisible(encounter->combatants.empty());
    m_roster->setVisible(!encounter->combatants.empty());
    for (const Combatant& combatant : encounter->combatants) {
        QString label = QString::fromStdString(combatant.name) + QStringLiteral("    ") +
                        QString::fromStdString(formatHitPoints(combatant.hp, combatant.maxHp));
        if (isCharacterCombatant(combatant) && !encounter->partyId.empty() && !combatant.partyMember) {
            label += tr("    guest");
        }
        if (combatant.statBlock.has_value()) {
            label += tr("    CR %1, %2 XP")
                         .arg(QString::fromStdString(combatant.statBlock->challengeRating))
                         .arg(monsterXp(*combatant.statBlock));
        }
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, QString::fromStdString(combatant.id));
        m_roster->addItem(item);
    }
    m_removeCombatantButton->setEnabled(false);
    updateDifficulty();
}

void EncounterBuilderPage::updateDifficulty()
{
    const Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        m_difficulty->clear();
        m_difficultyPill->hide();
        return;
    }
    const EncounterRating rating = rateEncounter(*encounter, m_characters);
    m_difficultyPill->setVisible(rating.characters > 0);
    m_difficultyPill->setText(tr(difficultyLabel(rating.difficulty)));
    const char* level = "";
    switch (rating.difficulty) {
    case Difficulty::BelowLow:
    case Difficulty::Low:
        level = "low";
        break;
    case Difficulty::Moderate:
        level = "moderate";
        break;
    case Difficulty::High:
    case Difficulty::AboveHigh:
        level = "high";
        break;
    case Difficulty::None:
        break;
    }
    m_difficultyPill->setProperty("level", QString::fromLatin1(level));
    m_difficultyPill->style()->unpolish(m_difficultyPill);
    m_difficultyPill->style()->polish(m_difficultyPill);
    if (rating.characters == 0) {
        m_difficulty->setText(tr("Monster XP %1. Add characters with class levels to rate the difficulty.")
                                  .arg(rating.monsterXp));
        return;
    }
    QString text = tr("Monster XP %1 of a party budget of Low %2, Moderate %3, High %4.")
                       .arg(rating.monsterXp)
                       .arg(rating.budget.low)
                       .arg(rating.budget.moderate)
                       .arg(rating.budget.high);
    if (rating.levelsUnknown > 0) {
        text += tr(" %n character(s) without a class level are not counted.", nullptr, rating.levelsUnknown);
    }
    m_difficulty->setText(text);
}

void EncounterBuilderPage::removeSelectedCombatant()
{
    Encounter* encounter = selectedEncounter();
    const QListWidgetItem* item = m_roster->currentItem();
    if (encounter == nullptr || item == nullptr || hasLoadError()) {
        return;
    }
    const std::string id = item->data(Qt::UserRole).toString().toStdString();
    for (int i = 0; i < static_cast<int>(encounter->combatants.size()); ++i) {
        const Combatant& row = encounter->combatants[static_cast<std::size_t>(i)];
        if (row.id == id) {
            if (row.partyMember) {
                // The party would bring them back: left out of this one.
                encounter->partyLeftOut.push_back(row.sourceId);
            }
            encounter->turnIndex = removeCombatant(encounter->combatants, i, encounter->turnIndex);
            break;
        }
    }
    assignMonsterCopyNames(encounter->combatants);
    persist();
    rebuildRoster();
}

void EncounterBuilderPage::addCharacter()
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
    if (encounterHasCharacter(*encounter, character->id)) {
        QMessageBox::information(this, tr("Already in this encounter"),
                                 tr("%1 is already in this encounter. A character can join a fight once, because "
                                    "the fight writes hit points back to that one sheet.")
                                     .arg(QString::fromStdString(character->name)));
        return;
    }
    Combatant row = makeCharacterCombatant(*character, newId());
    if (const Party* party = findParty(m_campaign, encounter->partyId);
        party != nullptr && std::find(party->characterIds.begin(), party->characterIds.end(), character->id) !=
                                party->characterIds.end()) {
        row.partyMember = true;  // back in, with the party
        std::erase(encounter->partyLeftOut, character->id);
    }
    encounter->combatants.push_back(std::move(row));
    encounter->turnIndex = sortByInitiative(encounter->combatants, encounter->turnIndex);
    persist();
    rebuildRoster();
}

void EncounterBuilderPage::addSelectedMonster()
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
    for (int copy = 0; copy < m_monsterQuantity->value(); ++copy) {
        encounter->combatants.push_back(makeMonsterCombatant(*monster, generateUuidV4([this] { return m_ids(); })));
    }
    assignMonsterCopyNames(encounter->combatants);
    encounter->turnIndex = sortByInitiative(encounter->combatants, encounter->turnIndex);
    persist();
    rebuildRoster();
}

std::string EncounterBuilderPage::newId()
{
    return generateUuidV4([this] { return m_ids(); });
}

void EncounterBuilderPage::setCampaign(CampaignStore* campaign)
{
    m_campaignStore = campaign;
    if (!hasLoadError()) {
        reloadFromDisk();
    }
}

void EncounterBuilderPage::saveCampaign()
{
    if (m_campaignStore == nullptr) {
        return;
    }
    try {
        m_campaignStore->save(m_campaign);
    } catch (const CampaignStoreError& error) {
        QMessageBox::warning(this, tr("Could not save adventures"), QString::fromStdString(error.what()));
    }
}

Adventure* EncounterBuilderPage::chosenAdventure()
{
    return findAdventure(m_campaign, m_adventureFilter->currentData().toString().toStdString());
}

void EncounterBuilderPage::fillPartyChoices()
{
    const QString previous = m_partyCombo->currentData().toString();
    {
        const QSignalBlocker blocker(m_partyCombo);
        m_partyCombo->clear();
        for (const Party& party : m_campaign.parties) {
            const int size = static_cast<int>(party.characterIds.size());
            const QString name = QString::fromStdString(party.name);
            m_partyCombo->addItem(size == 1 ? tr("%1 (1 character)").arg(name)
                                            : tr("%1 (%2 characters)").arg(name).arg(size),
                                  QString::fromStdString(party.id));
        }
        if (const int row = m_partyCombo->findData(previous); row >= 0) {
            m_partyCombo->setCurrentIndex(row);
        }
    }
    if (m_partyCombo->count() == 0) {
        m_partyCombo->addItem(tr("No parties yet (Characters page)"), QString());
    }
}

void EncounterBuilderPage::fillAdventureChoices()
{
    QString choice = m_adventureFilter->count() > 0 ? m_adventureFilter->currentData().toString()
                                                    : readLastAdventure(m_stateFile);
    {
        const QSignalBlocker blocker(m_adventureFilter);
        m_adventureFilter->clear();
        m_adventureFilter->addItem(tr("All encounters"), QString::fromLatin1(kAllAdventures));
        for (const Adventure& adventure : m_campaign.adventures) {
            m_adventureFilter->addItem(QString::fromStdString(adventure.name), QString::fromStdString(adventure.id));
        }
        if (!m_campaign.adventures.empty()) {
            m_adventureFilter->addItem(tr("No adventure"), QString::fromLatin1(kNoAdventure));
        }
        const int row = m_adventureFilter->findData(choice);
        m_adventureFilter->setCurrentIndex(row >= 0 ? row : 0);
    }
    const Adventure* adventure = chosenAdventure();
    m_adventureDetails->setVisible(adventure != nullptr);
    if (adventure == nullptr) {
        return;
    }
    const QSignalBlocker nameBlocker(m_adventureName);
    if (m_adventureName->text() != QString::fromStdString(adventure->name)) {
        m_adventureName->setText(QString::fromStdString(adventure->name));
    }
    const QSignalBlocker partyBlocker(m_adventureParty);
    m_adventureParty->clear();
    m_adventureParty->addItem(tr("No party"), QString());
    for (const Party& party : m_campaign.parties) {
        m_adventureParty->addItem(QString::fromStdString(party.name), QString::fromStdString(party.id));
    }
    const int row = m_adventureParty->findData(QString::fromStdString(adventure->partyId));
    m_adventureParty->setCurrentIndex(row >= 0 ? row : 0);
}

void EncounterBuilderPage::fillEncounterList(const QString& selectedId)
{
    const std::string choice = m_adventureFilter->currentData().toString().toStdString();
    const QSignalBlocker blocker(m_encounterList);
    m_encounterList->clear();
    int select = -1;
    for (const Encounter& encounter : m_encounters) {
        if (!inAdventureChoice(encounter, choice)) {
            continue;
        }
        auto* item = new QListWidgetItem(QString::fromStdString(encounter.name));
        item->setData(Qt::UserRole, QString::fromStdString(encounter.id));
        m_encounterList->addItem(item);
        if (!selectedId.isEmpty() && QString::fromStdString(encounter.id) == selectedId) {
            select = m_encounterList->count() - 1;
        }
    }
    if (select < 0 && m_encounterList->count() > 0) {
        select = 0;
    }
    if (select >= 0) {
        m_encounterList->setCurrentRow(select);
    }
    m_emptyHint->setText(m_encounters.empty() || choice == kAllAdventures
                             ? tr("No encounters yet. Choose New encounter, then add characters and monsters.")
                             : tr("No encounters in this adventure yet. New encounter makes one here."));
}

void EncounterBuilderPage::onAdventureFilterChanged()
{
    writeLastAdventure(m_stateFile, m_adventureFilter->currentData().toString());
    const QListWidgetItem* item = m_encounterList->currentItem();
    const QString selected = item != nullptr ? item->data(Qt::UserRole).toString() : QString();
    fillAdventureChoices();
    fillEncounterList(selected);
    showEncounter();
}

void EncounterBuilderPage::addAdventure()
{
    Adventure adventure;
    adventure.id = newId();
    adventure.name = tr("New adventure").toStdString();
    m_campaign.adventures.push_back(adventure);
    saveCampaign();
    fillAdventureChoices();
    m_adventureFilter->setCurrentIndex(m_adventureFilter->findData(QString::fromStdString(adventure.id)));
    m_adventureName->setFocus();
    m_adventureName->selectAll();
}

void EncounterBuilderPage::deleteAdventure()
{
    const Adventure* adventure = chosenAdventure();
    if (adventure == nullptr) {
        return;
    }
    const std::string id = adventure->id;
    const int count = static_cast<int>(std::count_if(m_encounters.begin(), m_encounters.end(),
                                                     [&id](const Encounter& encounter) { return encounter.adventureId == id; }));
    const auto answer = QMessageBox::question(
        this, tr("Delete adventure"),
        count == 0 ? tr("Delete %1?").arg(QString::fromStdString(adventure->name))
                   : tr("Delete %1? Its %n encounter(s) stay, with no adventure.", nullptr, count)
                         .arg(QString::fromStdString(adventure->name)));
    if (answer != QMessageBox::Yes) {
        return;
    }
    std::erase_if(m_campaign.adventures, [&id](const Adventure& candidate) { return candidate.id == id; });
    saveCampaign();
    if (forgetAdventure(m_encounters, id) > 0) {
        persist();
    }
    m_adventureFilter->setCurrentIndex(0);  // all encounters
    fillAdventureChoices();
    fillEncounterList(QString());
    showEncounter();
}

void EncounterBuilderPage::onAdventureNameEdited(const QString& text)
{
    Adventure* adventure = chosenAdventure();
    if (adventure == nullptr || isBlank(text)) {
        return;
    }
    adventure->name = text.trimmed().toStdString();
    saveCampaign();
    m_adventureFilter->setItemText(m_adventureFilter->currentIndex(), text.trimmed());
}

void EncounterBuilderPage::onAdventurePartyChosen()
{
    Adventure* adventure = chosenAdventure();
    if (adventure == nullptr) {
        return;
    }
    adventure->partyId = m_adventureParty->currentData().toString().toStdString();
    saveCampaign();
}

void EncounterBuilderPage::onEncounterAdventureChosen()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || hasLoadError()) {
        return;
    }
    encounter->adventureId = m_encounterAdventure->currentData().toString().toStdString();
    persist();
    // Moved out of the adventure listed: it stays selected under All.
    const QString id = QString::fromStdString(encounter->id);
    if (!inAdventureChoice(*encounter, m_adventureFilter->currentData().toString().toStdString())) {
        const QSignalBlocker blocker(m_adventureFilter);
        m_adventureFilter->setCurrentIndex(m_adventureFilter->findData(QString::fromStdString(encounter->adventureId)) >= 0
                                               ? m_adventureFilter->findData(QString::fromStdString(encounter->adventureId))
                                               : 0);
        writeLastAdventure(m_stateFile, m_adventureFilter->currentData().toString());
        fillAdventureChoices();
        fillEncounterList(id);
        showEncounter();
    }
}

void EncounterBuilderPage::addParty()
{
    Encounter* encounter = selectedEncounter();
    const Party* party = findParty(m_campaign, m_partyCombo->currentData().toString().toStdString());
    if (encounter == nullptr || party == nullptr || hasLoadError()) {
        return;
    }
    applyParty(*encounter, *party, m_characters, [this] { return newId(); });
    persist();
    showEncounter();
}

void EncounterBuilderPage::removeParty()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || hasLoadError()) {
        return;
    }
    removePartyRows(*encounter);
    persist();
    showEncounter();
}

}  // namespace combat::ui
