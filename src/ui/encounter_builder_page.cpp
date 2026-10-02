#include "ui/encounter_builder_page.h"

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
    outer->setContentsMargins(32, 24, 32, 24);
    outer->setSpacing(16);
    outer->addWidget(makePageTitle(tr("Encounter Builder")));

    if (hasLoadError()) {
        auto* banner = new QLabel(tr("The encounters file could not be read, so it has not been changed.\n%1")
                                      .arg(m_loadError));
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
        outer->addWidget(banner);
    }

    auto* note = new QLabel(tr("Create characters on the Characters page and custom monsters on the Monsters page. "
                               "This page adds ones that already exist."));
    note->setWordWrap(true);
    outer->addWidget(note);

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
    m_resetEncounterButton = new QPushButton(tr("Reset"));
    m_resetEncounterButton->setObjectName(QStringLiteral("resetEncounter"));
    listButtons->addWidget(m_addEncounterButton);
    listButtons->addWidget(m_deleteEncounterButton);
    listButtons->addWidget(m_resetEncounterButton);
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

    m_emptyHint = new QLabel(tr("Add an encounter, then add characters and monsters."));
    m_emptyHint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(m_emptyHint);

    m_editor = new QWidget;
    m_editor->setObjectName(QStringLiteral("encounterEditor"));
    auto* editorLayout = new QVBoxLayout(m_editor);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(12);

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
    editorLayout->addLayout(nameForm);

    m_rosterHint = new QLabel(tr("No one is in this encounter yet."));
    m_rosterHint->setObjectName(QStringLiteral("encounterRosterHint"));
    editorLayout->addWidget(m_rosterHint);
    m_roster = new QListWidget;
    m_roster->setObjectName(QStringLiteral("encounterRoster"));
    m_roster->setSelectionMode(QAbstractItemView::NoSelection);
    m_roster->setFocusPolicy(Qt::NoFocus);
    m_roster->setMinimumHeight(80);
    m_roster->setMaximumHeight(160);
    editorLayout->addWidget(m_roster);

    auto* addCharacterRow = new QHBoxLayout;
    m_characterCombo = new QComboBox;
    m_characterCombo->setObjectName(QStringLiteral("characterCombo"));
    m_addCharacterButton = new QPushButton(tr("Add character"));
    m_addCharacterButton->setObjectName(QStringLiteral("addCharacter"));
    addCharacterRow->addWidget(m_characterCombo, 1);
    addCharacterRow->addWidget(m_addCharacterButton);
    editorLayout->addLayout(addCharacterRow);

    m_monsterSearch = new QLineEdit;
    m_monsterSearch->setObjectName(QStringLiteral("monsterSearch"));
    m_monsterSearch->setPlaceholderText(tr("Search monsters by name"));
    editorLayout->addWidget(m_monsterSearch);
    auto* addMonsterRow = new QHBoxLayout;
    m_monsterChoices = new QListWidget;
    m_monsterChoices->setObjectName(QStringLiteral("monsterChoices"));
    m_monsterChoices->setMaximumHeight(180);
    m_addMonsterButton = new QPushButton(tr("Add monster"));
    m_addMonsterButton->setObjectName(QStringLiteral("addMonster"));
    addMonsterRow->addWidget(m_monsterChoices, 1);
    addMonsterRow->addWidget(m_addMonsterButton, 0, Qt::AlignTop);
    editorLayout->addLayout(addMonsterRow);
    editorLayout->addStretch(1);
    right->addWidget(m_editor, 1);

    connect(m_encounterList, &QListWidget::currentRowChanged, this, &EncounterBuilderPage::showEncounter);
    connect(m_addEncounterButton, &QPushButton::clicked, this, &EncounterBuilderPage::addEncounter);
    connect(m_deleteEncounterButton, &QPushButton::clicked, this, &EncounterBuilderPage::deleteEncounter);
    connect(m_resetEncounterButton, &QPushButton::clicked, this, &EncounterBuilderPage::resetEncounter);
    connect(m_encounterName, &QLineEdit::textEdited, this, &EncounterBuilderPage::onEncounterNameEdited);
    connect(m_encounterName, &QLineEdit::editingFinished, this, &EncounterBuilderPage::onEncounterNameEditingFinished);
    connect(m_addCharacterButton, &QPushButton::clicked, this, &EncounterBuilderPage::addCharacter);
    connect(m_addMonsterButton, &QPushButton::clicked, this, &EncounterBuilderPage::addSelectedMonster);
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
        QString label = QString::fromStdString(monster.name) + QStringLiteral("    ") +
                        QString::fromStdString(formatHitPoints(monster.hp, monster.hp));
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
    resetMonsterHitPoints(*encounter);
    persist();
    rebuildRoster();
}

void EncounterBuilderPage::showEncounter()
{
    Encounter* encounter = selectedEncounter();
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
        const QString label = QString::fromStdString(combatant.name) + QStringLiteral("    ") +
                              QString::fromStdString(formatHitPoints(combatant.hp, combatant.maxHp));
        m_roster->addItem(label);
    }
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
    encounter->combatants.push_back(
        makeMonsterCombatant(*monster, encounter->combatants, generateUuidV4([this] { return m_ids(); })));
    encounter->turnIndex = sortByInitiative(encounter->combatants, encounter->turnIndex);
    persist();
    rebuildRoster();
}

}  // namespace combat::ui
