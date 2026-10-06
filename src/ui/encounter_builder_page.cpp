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

    // Left: the saved encounters.
    auto* listCard = makeCard();
    listCard->setFixedWidth(260);
    listCard->layout()->setContentsMargins(8, 12, 8, 8);
    auto* listHeading = makeHeading(tr("Encounters"));
    listHeading->setContentsMargins(8, 0, 0, 0);
    listCard->layout()->addWidget(listHeading);
    m_encounterList = new QListWidget;
    m_encounterList->setObjectName(QStringLiteral("encounterList"));
    static_cast<QVBoxLayout*>(listCard->layout())->addWidget(m_encounterList, 1);
    columns->addWidget(listCard);

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
    auto* addCharacterRow = new QHBoxLayout;
    m_characterCombo = new QComboBox;
    m_characterCombo->setObjectName(QStringLiteral("characterCombo"));
    m_addCharacterButton = new QPushButton(tr("Add character"));
    m_addCharacterButton->setObjectName(QStringLiteral("addCharacter"));
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
    auto* note = makeMuted(tr("Characters come from the Characters page and custom monsters from the Monsters page."));
    addPane->addWidget(note);
    panes->addLayout(addPane, 1);

    connect(m_encounterList, &QListWidget::currentRowChanged, this, &EncounterBuilderPage::showEncounter);
    connect(m_addEncounterButton, &QPushButton::clicked, this, &EncounterBuilderPage::addEncounter);
    connect(m_deleteEncounterButton, &QPushButton::clicked, this, &EncounterBuilderPage::deleteEncounter);
    connect(m_resetEncounterButton, &QPushButton::clicked, this, &EncounterBuilderPage::resetEncounter);
    connect(m_encounterName, &QLineEdit::textEdited, this, &EncounterBuilderPage::onEncounterNameEdited);
    connect(m_encounterName, &QLineEdit::editingFinished, this, &EncounterBuilderPage::onEncounterNameEditingFinished);
    connect(m_addCharacterButton, &QPushButton::clicked, this, &EncounterBuilderPage::addCharacter);
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

    int select = -1;
    {
        const QSignalBlocker blocker(m_encounterList);
        m_encounterList->clear();
        for (int i = 0; i < static_cast<int>(m_encounters.size()); ++i) {
            const Encounter& encounter = m_encounters[static_cast<std::size_t>(i)];
            m_encounterList->addItem(QString::fromStdString(encounter.name));
            if (!selectedId.isEmpty() && QString::fromStdString(encounter.id) == selectedId) {
                select = i;
            }
        }
        if (select < 0 && !m_encounters.empty()) {
            select = 0;
        }
        if (select >= 0) {
            m_encounterList->setCurrentRow(select);
        }
    }
    reloadCharacters();
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
    const int row = m_encounterList->currentRow();
    if (row < 0 || row >= static_cast<int>(m_encounters.size())) {
        return nullptr;
    }
    return &m_encounters[static_cast<std::size_t>(row)];
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
    m_encounters.push_back(encounter);
    m_encounterList->addItem(QString::fromStdString(encounter.name));
    persist();
    m_encounterList->setCurrentRow(static_cast<int>(m_encounters.size()) - 1);
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
    const int row = m_encounterList->currentRow();
    m_encounters.erase(m_encounters.begin() + row);
    delete m_encounterList->takeItem(row);
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
    for (int i = 0; i < static_cast<int>(m_encounters.size()); ++i) {
        if (QString::fromStdString(m_encounters[static_cast<std::size_t>(i)].id) == id) {
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
        if (encounter->combatants[static_cast<std::size_t>(i)].id == id) {
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
    encounter->combatants.push_back(makeCharacterCombatant(*character, generateUuidV4([this] { return m_ids(); })));
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

}  // namespace combat::ui
