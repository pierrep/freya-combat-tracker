#include "ui/characters_page.h"

#include "core/character_store.h"
#include "core/uuid.h"
#include "ui/page_title.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace combat::ui {

namespace {

struct AbilityField {
    const char* label;
    int AbilityScores::* member;
};

const std::array<AbilityField, 6> kAbilities{{
    {QT_TRANSLATE_NOOP("CharactersPage", "Strength"), &AbilityScores::strength},
    {QT_TRANSLATE_NOOP("CharactersPage", "Dexterity"), &AbilityScores::dexterity},
    {QT_TRANSLATE_NOOP("CharactersPage", "Constitution"), &AbilityScores::constitution},
    {QT_TRANSLATE_NOOP("CharactersPage", "Intelligence"), &AbilityScores::intelligence},
    {QT_TRANSLATE_NOOP("CharactersPage", "Wisdom"), &AbilityScores::wisdom},
    {QT_TRANSLATE_NOOP("CharactersPage", "Charisma"), &AbilityScores::charisma},
}};

bool isBlank(const QString& text)
{
    return text.trimmed().isEmpty();
}

QString listLabel(const Character& character)
{
    return QString::fromStdString(character.name);
}

QLabel* sectionLabel(const QString& text)
{
    auto* label = new QLabel(text);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

void clearLayout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

int rowOf(const QObject* object)
{
    return object->property("row").toInt();
}

}  // namespace

CharactersPage::CharactersPage(CharacterStore& store, std::vector<Spell> spells, std::vector<Condition> conditions,
                               std::vector<std::string> species, const QString& attribution,
                               const QString& catalogError, QWidget* parent)
    : QWidget(parent)
    , m_store(store)
    , m_spells(std::move(spells))
    , m_conditions(std::move(conditions))
    , m_speciesNames(searchSpecies(species, ""))
    , m_catalogError(catalogError)
    , m_rng(std::random_device{}())
{
    try {
        m_characters = m_store.loadAll();
    } catch (const CharacterStoreError& error) {
        m_loadError = QString::fromStdString(error.what());
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 24, 32, 24);
    outer->setSpacing(16);
    outer->addWidget(makePageTitle(tr("Characters")));

    if (hasLoadError()) {
        auto* banner = new QLabel(tr("The characters file could not be read, so it has not been changed.\n%1")
                                      .arg(m_loadError));
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
        outer->addWidget(banner);
    }
    if (!m_catalogError.isEmpty()) {
        auto* banner = new QLabel(tr("An SRD catalog could not be read. Names you type are kept, and no description "
                                      "is invented.\n%1")
                                      .arg(m_catalogError));
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
        outer->addWidget(banner);
    }

    auto* columns = new QHBoxLayout;
    columns->setSpacing(24);
    outer->addLayout(columns, 1);

    auto* left = new QVBoxLayout;
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("characterList"));
    m_list->setMinimumWidth(220);
    m_list->setMaximumWidth(280);
    left->addWidget(m_list, 1);
    auto* listButtons = new QHBoxLayout;
    m_addButton = new QPushButton(tr("Add"));
    m_deleteButton = new QPushButton(tr("Delete"));
    listButtons->addWidget(m_addButton);
    listButtons->addWidget(m_deleteButton);
    left->addLayout(listButtons);
    columns->addLayout(left);

    m_form = new QWidget;
    auto* formLayout = new QVBoxLayout(m_form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(12);

    auto* basics = new QFormLayout;
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("nameField"));
    m_name->setPlaceholderText(tr("Required"));
    m_nameError = new QLabel(tr("Name is required."));
    m_nameError->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
    m_nameError->hide();
    auto* nameColumn = new QVBoxLayout;
    nameColumn->setSpacing(2);
    nameColumn->addWidget(m_name);
    nameColumn->addWidget(m_nameError);
    basics->addRow(tr("Name"), nameColumn);

    m_species = new QComboBox;
    m_species->setObjectName(QStringLiteral("speciesField"));
    m_species->setEditable(true);
    m_species->setInsertPolicy(QComboBox::NoInsert);
    for (const std::string& name : m_speciesNames) {
        m_species->addItem(QString::fromStdString(name));
    }
    m_speciesHint = new QLabel(tr("This species is not in the SRD list. No description is stored for it."));
    m_speciesHint->setWordWrap(true);
    m_speciesHint->hide();
    auto* speciesColumn = new QVBoxLayout;
    speciesColumn->setSpacing(2);
    speciesColumn->addWidget(m_species);
    speciesColumn->addWidget(m_speciesHint);
    basics->addRow(tr("Species"), speciesColumn);

    m_hpCurrent = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    m_hpCurrent->setObjectName(QStringLiteral("hpCurrent"));
    m_hpMax = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    m_hpMax->setObjectName(QStringLiteral("hpMax"));
    m_tempHp = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    auto* hpRow = new QHBoxLayout;
    hpRow->addWidget(new QLabel(tr("Current")));
    hpRow->addWidget(m_hpCurrent);
    hpRow->addWidget(new QLabel(tr("Maximum")));
    hpRow->addWidget(m_hpMax);
    hpRow->addWidget(new QLabel(tr("Temporary")));
    hpRow->addWidget(m_tempHp);
    hpRow->addStretch(1);
    basics->addRow(tr("HP"), hpRow);

    m_ac = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    basics->addRow(tr("AC"), m_ac);
    m_speed = new QLineEdit;
    m_speed->setObjectName(QStringLiteral("speedField"));
    m_speed->setPlaceholderText(tr("for example, 30 ft."));
    basics->addRow(tr("Speed"), m_speed);
    m_initiative = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    basics->addRow(tr("Initiative bonus"), m_initiative);
    m_proficiency = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    basics->addRow(tr("Proficiency bonus"), m_proficiency);
    m_passivePerception = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    basics->addRow(tr("Passive Perception"), m_passivePerception);
    formLayout->addLayout(basics);

    formLayout->addWidget(sectionLabel(tr("Ability scores")));
    auto* abilityGrid = new QGridLayout;
    abilityGrid->setHorizontalSpacing(16);
    abilityGrid->addWidget(new QLabel(tr("Score")), 0, 1);
    abilityGrid->addWidget(new QLabel(tr("Modifier")), 0, 2);
    abilityGrid->addWidget(new QLabel(tr("Save")), 0, 3);
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        const int row = static_cast<int>(i) + 1;
        abilityGrid->addWidget(new QLabel(tr(kAbilities[i].label)), row, 0);
        m_scores[i] = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        abilityGrid->addWidget(m_scores[i], row, 1);
        m_modifiers[i] = new QLabel;
        m_modifiers[i]->setMinimumWidth(40);
        abilityGrid->addWidget(m_modifiers[i], row, 2);
        m_saves[i] = new QCheckBox(tr("Proficient"));
        abilityGrid->addWidget(m_saves[i], row, 3);
    }
    abilityGrid->setColumnStretch(4, 1);
    formLayout->addLayout(abilityGrid);

    formLayout->addWidget(sectionLabel(tr("Skills")));
    auto* skillGrid = new QGridLayout;
    skillGrid->setHorizontalSpacing(16);
    for (std::size_t i = 0; i < kSkills.size(); ++i) {
        const int row = static_cast<int>(i % 9);
        const int column = static_cast<int>(i / 9);
        m_skillBoxes[i] = new QCheckBox(tr("%1 (%2)").arg(tr(kSkills[i].name), tr(kSkills[i].ability)));
        skillGrid->addWidget(m_skillBoxes[i], row, column);
    }
    formLayout->addLayout(skillGrid);

    formLayout->addWidget(sectionLabel(tr("Classes")));
    m_classLayout = new QVBoxLayout;
    formLayout->addLayout(m_classLayout);
    auto* addClassButton = new QPushButton(tr("Add class"));
    addClassButton->setObjectName(QStringLiteral("addClass"));
    formLayout->addWidget(addClassButton, 0, Qt::AlignLeft);

    formLayout->addWidget(sectionLabel(tr("Spells")));
    m_spellSearch = new QLineEdit;
    m_spellSearch->setObjectName(QStringLiteral("spellSearch"));
    m_spellSearch->setPlaceholderText(tr("Search SRD spells"));
    formLayout->addWidget(m_spellSearch);
    m_spellMatches = new QListWidget;
    m_spellMatches->setObjectName(QStringLiteral("spellMatches"));
    m_spellMatches->setMaximumHeight(140);
    formLayout->addWidget(m_spellMatches);
    auto* addSpellButton = new QPushButton(tr("Add spell"));
    addSpellButton->setObjectName(QStringLiteral("addSpell"));
    formLayout->addWidget(addSpellButton, 0, Qt::AlignLeft);
    auto* customRow = new QHBoxLayout;
    m_customSpell = new QLineEdit;
    m_customSpell->setObjectName(QStringLiteral("customSpell"));
    m_customSpell->setPlaceholderText(tr("Spell name that is not in the SRD"));
    auto* addCustomButton = new QPushButton(tr("Add spell by name"));
    customRow->addWidget(m_customSpell, 1);
    customRow->addWidget(addCustomButton);
    formLayout->addLayout(customRow);

    m_spellList = new QListWidget;
    m_spellList->setObjectName(QStringLiteral("characterSpells"));
    m_spellList->setMaximumHeight(140);
    formLayout->addWidget(m_spellList);
    auto* spellButtons = new QHBoxLayout;
    m_prepared = new QCheckBox(tr("Prepared"));
    m_prepared->setObjectName(QStringLiteral("prepared"));
    auto* removeSpellButton = new QPushButton(tr("Remove spell"));
    spellButtons->addWidget(m_prepared);
    spellButtons->addWidget(removeSpellButton);
    spellButtons->addStretch(1);
    formLayout->addLayout(spellButtons);
    m_spellDescription = new QLabel;
    m_spellDescription->setObjectName(QStringLiteral("spellDescription"));
    m_spellDescription->setWordWrap(true);
    m_spellDescription->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formLayout->addWidget(m_spellDescription);

    formLayout->addWidget(sectionLabel(tr("Spell slots")));
    m_slotLayout = new QVBoxLayout;
    formLayout->addLayout(m_slotLayout);
    auto* addSlotButton = new QPushButton(tr("Add spell slot"));
    addSlotButton->setObjectName(QStringLiteral("addSlot"));
    formLayout->addWidget(addSlotButton, 0, Qt::AlignLeft);

    formLayout->addWidget(sectionLabel(tr("Gear")));
    m_gearLayout = new QVBoxLayout;
    formLayout->addLayout(m_gearLayout);
    auto* addGearButton = new QPushButton(tr("Add gear"));
    addGearButton->setObjectName(QStringLiteral("addGear"));
    formLayout->addWidget(addGearButton, 0, Qt::AlignLeft);

    formLayout->addWidget(sectionLabel(tr("Conditions")));
    m_conditionPicker = new QComboBox;
    m_conditionPicker->setObjectName(QStringLiteral("conditionPicker"));
    for (const Condition& condition : searchConditions(m_conditions, "")) {
        m_conditionPicker->addItem(QString::fromStdString(condition.name), QString::fromStdString(condition.id));
    }
    auto* addConditionButton = new QPushButton(tr("Add condition"));
    addConditionButton->setObjectName(QStringLiteral("addCondition"));
    auto* conditionAddRow = new QHBoxLayout;
    conditionAddRow->addWidget(m_conditionPicker, 1);
    conditionAddRow->addWidget(addConditionButton);
    formLayout->addLayout(conditionAddRow);
    m_conditionList = new QListWidget;
    m_conditionList->setObjectName(QStringLiteral("characterConditions"));
    m_conditionList->setMaximumHeight(120);
    formLayout->addWidget(m_conditionList);
    auto* removeConditionButton = new QPushButton(tr("Remove condition"));
    formLayout->addWidget(removeConditionButton, 0, Qt::AlignLeft);
    m_conditionDescription = new QLabel;
    m_conditionDescription->setObjectName(QStringLiteral("conditionDescription"));
    m_conditionDescription->setWordWrap(true);
    m_conditionDescription->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formLayout->addWidget(m_conditionDescription);

    formLayout->addWidget(sectionLabel(tr("Death saves")));
    auto* deathRow = new QHBoxLayout;
    m_deathSuccesses = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    m_deathFailures = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    deathRow->addWidget(new QLabel(tr("Successes")));
    deathRow->addWidget(m_deathSuccesses);
    deathRow->addWidget(new QLabel(tr("Failures")));
    deathRow->addWidget(m_deathFailures);
    deathRow->addStretch(1);
    formLayout->addLayout(deathRow);

    formLayout->addWidget(sectionLabel(tr("Notes")));
    m_notes = new QPlainTextEdit;
    m_notes->setObjectName(QStringLiteral("notesField"));
    m_notes->setMinimumHeight(80);
    formLayout->addWidget(m_notes);

    m_emptyHint = new QLabel(tr("Add a character to get started."));
    m_emptyHint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(m_emptyHint);
    rightLayout->addWidget(m_form, 1);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(right);
    columns->addWidget(scroll, 1);

    auto* credit = new QLabel(attribution.isEmpty() ? tr("The SRD attribution file could not be read.") : attribution);
    credit->setObjectName(QStringLiteral("srdAttribution"));
    credit->setWordWrap(true);
    credit->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont creditFont = credit->font();
    creditFont.setPointSizeF(creditFont.pointSizeF() * 0.9);
    credit->setFont(creditFont);
    outer->addWidget(credit);

    for (const Character& character : m_characters) {
        m_list->addItem(listLabel(character));
    }

    connect(m_list, &QListWidget::currentRowChanged, this, &CharactersPage::showSelected);
    connect(m_addButton, &QPushButton::clicked, this, &CharactersPage::addCharacter);
    connect(m_deleteButton, &QPushButton::clicked, this, &CharactersPage::deleteSelected);
    connect(m_name, &QLineEdit::textEdited, this, &CharactersPage::onNameEdited);
    connect(m_name, &QLineEdit::editingFinished, this, &CharactersPage::onNameEditingFinished);
    connect(m_species, &QComboBox::currentTextChanged, this, &CharactersPage::onSpeciesEdited);
    connect(m_speed, &QLineEdit::textEdited, this, &CharactersPage::onSpeedEdited);
    connect(m_notes, &QPlainTextEdit::textChanged, this, &CharactersPage::onNotesChanged);
    connect(addClassButton, &QPushButton::clicked, this, &CharactersPage::addClass);
    connect(m_spellSearch, &QLineEdit::textChanged, this, &CharactersPage::onSpellSearch);
    connect(addSpellButton, &QPushButton::clicked, this, &CharactersPage::addCatalogSpell);
    connect(m_spellMatches, &QListWidget::itemActivated, this, &CharactersPage::addCatalogSpell);
    connect(addCustomButton, &QPushButton::clicked, this, &CharactersPage::addCustomSpell);
    connect(m_spellList, &QListWidget::currentRowChanged, this, &CharactersPage::onSpellSelected);
    connect(m_prepared, &QCheckBox::toggled, this, &CharactersPage::onPreparedToggled);
    connect(removeSpellButton, &QPushButton::clicked, this, &CharactersPage::removeSelectedSpell);
    connect(addSlotButton, &QPushButton::clicked, this, &CharactersPage::addSlot);
    connect(addGearButton, &QPushButton::clicked, this, &CharactersPage::addGear);
    connect(addConditionButton, &QPushButton::clicked, this, &CharactersPage::addCondition);
    connect(m_conditionList, &QListWidget::currentRowChanged, this, &CharactersPage::onConditionSelected);
    connect(removeConditionButton, &QPushButton::clicked, this, &CharactersPage::removeSelectedCondition);

    const auto numberBoxes = {m_hpCurrent, m_hpMax, m_tempHp, m_ac, m_initiative, m_proficiency, m_passivePerception,
                              m_deathSuccesses, m_deathFailures};
    for (QSpinBox* box : numberBoxes) {
        connect(box, &QSpinBox::valueChanged, this, &CharactersPage::onNumberChanged);
    }
    for (QSpinBox* box : m_scores) {
        connect(box, &QSpinBox::valueChanged, this, &CharactersPage::onNumberChanged);
    }
    for (QCheckBox* box : m_saves) {
        connect(box, &QCheckBox::toggled, this, &CharactersPage::onNumberChanged);
    }
    for (QCheckBox* box : m_skillBoxes) {
        connect(box, &QCheckBox::toggled, this, &CharactersPage::onNumberChanged);
    }

    onSpellSearch(QString());

    if (hasLoadError()) {
        m_list->setEnabled(false);
        m_addButton->setEnabled(false);
        m_deleteButton->setEnabled(false);
        m_emptyHint->hide();
        m_form->hide();
        return;
    }

    if (!m_characters.empty()) {
        m_list->setCurrentRow(0);
    }
    showSelected();
}

int CharactersPage::count() const
{
    return static_cast<int>(m_characters.size());
}

void CharactersPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    reloadRoster();
}

void CharactersPage::reloadRoster()
{
    if (hasLoadError() || m_list == nullptr) {
        return;
    }
    std::vector<Character> loaded;
    try {
        loaded = m_store.loadAll();
    } catch (const CharacterStoreError& error) {
        QMessageBox::warning(this, tr("Could not read characters"), QString::fromStdString(error.what()));
        return;
    }
    std::string selectedId;
    if (const Character* character = selected()) {
        selectedId = character->id;
    }
    const int previousCount = count();
    m_characters = std::move(loaded);
    m_list->clear();
    int selectRow = m_characters.empty() ? -1 : 0;
    for (int i = 0; i < count(); ++i) {
        m_list->addItem(listLabel(m_characters[static_cast<std::size_t>(i)]));
        if (m_characters[static_cast<std::size_t>(i)].id == selectedId) {
            selectRow = i;
        }
    }
    if (selectRow >= 0) {
        m_list->setCurrentRow(selectRow);
    } else {
        showSelected();
    }
    if (count() != previousCount) {
        emit countChanged(count());
    }
}

QSpinBox* CharactersPage::makeNumberBox(int minimum, int maximum)
{
    auto* box = new QSpinBox;
    box->setRange(minimum, maximum);
    box->setMaximumWidth(120);
    box->setKeyboardTracking(true);
    return box;
}

Character* CharactersPage::selected()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= count()) {
        return nullptr;
    }
    return &m_characters[static_cast<std::size_t>(row)];
}

const Character* CharactersPage::selected() const
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= count()) {
        return nullptr;
    }
    return &m_characters[static_cast<std::size_t>(row)];
}

void CharactersPage::addCharacter()
{
    Character character;
    character.id = generateUuidV4([this] { return m_rng(); });
    character.name = tr("New character").toStdString();
    m_characters.push_back(character);
    m_list->addItem(listLabel(character));
    persist();
    emit countChanged(count());

    m_list->setCurrentRow(count() - 1);
    m_name->setFocus();
    m_name->selectAll();
}

void CharactersPage::deleteSelected()
{
    const Character* character = selected();
    if (character == nullptr) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Delete character"),
        tr("Delete %1? This cannot be undone.").arg(QString::fromStdString(character->name)));
    if (answer != QMessageBox::Yes) {
        return;
    }

    const int row = m_list->currentRow();
    m_characters.erase(m_characters.begin() + row);
    delete m_list->takeItem(row);
    persist();
    emit countChanged(count());
    showSelected();
}

void CharactersPage::showSelected()
{
    const Character* character = selected();
    m_deleteButton->setEnabled(character != nullptr);
    m_form->setVisible(character != nullptr);
    m_emptyHint->setVisible(character == nullptr);
    if (character == nullptr) {
        clearLayout(m_classLayout);
        clearLayout(m_slotLayout);
        clearLayout(m_gearLayout);
        m_spellList->clear();
        m_conditionList->clear();
        m_spellDescription->clear();
        m_conditionDescription->clear();
        return;
    }

    m_populating = true;
    m_name->setText(QString::fromStdString(character->name));
    m_nameError->hide();
    m_species->setCurrentText(QString::fromStdString(character->species));
    const bool knownSpecies = character->species.empty() ||
                              std::find(m_speciesNames.begin(), m_speciesNames.end(), character->species) !=
                                  m_speciesNames.end();
    m_speciesHint->setVisible(!knownSpecies);
    m_hpCurrent->setValue(character->hp.current);
    m_hpMax->setValue(character->hp.max);
    m_tempHp->setValue(character->tempHp);
    m_ac->setValue(character->ac);
    m_speed->setText(QString::fromStdString(character->speed));
    m_initiative->setValue(character->initiativeBonus);
    m_proficiency->setValue(character->proficiencyBonus);
    m_passivePerception->setValue(character->passivePerception);
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        m_scores[i]->setValue(character->abilities.*kAbilities[i].member);
        m_saves[i]->setChecked(character->savingThrows.*kSavingThrows[i].member);
    }
    for (std::size_t i = 0; i < kSkills.size(); ++i) {
        m_skillBoxes[i]->setChecked(character->skills.*kSkills[i].member);
    }
    m_deathSuccesses->setValue(character->deathSaves.successes);
    m_deathFailures->setValue(character->deathSaves.failures);
    m_notes->setPlainText(QString::fromStdString(character->notes));
    m_populating = false;
    updateModifierLabels();
    rebuildClasses();
    refreshSpellList(character->spells.empty() ? -1 : 0);
    rebuildSlots();
    rebuildGear();
    refreshConditions(character->conditions.empty() ? -1 : 0);
}

void CharactersPage::onNameEdited(const QString& text)
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    m_nameError->setVisible(isBlank(text));
    if (isBlank(text)) {
        return;
    }
    character->name = text.toStdString();
    m_list->currentItem()->setText(text);
    persist();
}

void CharactersPage::onNameEditingFinished()
{
    const Character* character = selected();
    if (character == nullptr || !isBlank(m_name->text())) {
        return;
    }
    m_name->setText(QString::fromStdString(character->name));
    m_nameError->hide();
}

void CharactersPage::onSpeciesEdited(const QString& text)
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    character->species = text.toStdString();
    const bool known = text.trimmed().isEmpty() ||
                       std::find(m_speciesNames.begin(), m_speciesNames.end(), character->species) !=
                           m_speciesNames.end();
    m_speciesHint->setVisible(!known);
    persist();
}

void CharactersPage::onNumberChanged()
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    character->hp.current = m_hpCurrent->value();
    character->hp.max = m_hpMax->value();
    character->tempHp = m_tempHp->value();
    character->ac = m_ac->value();
    character->initiativeBonus = m_initiative->value();
    character->proficiencyBonus = m_proficiency->value();
    character->passivePerception = m_passivePerception->value();
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        character->abilities.*kAbilities[i].member = m_scores[i]->value();
        character->savingThrows.*kSavingThrows[i].member = m_saves[i]->isChecked();
    }
    for (std::size_t i = 0; i < kSkills.size(); ++i) {
        character->skills.*kSkills[i].member = m_skillBoxes[i]->isChecked();
    }
    character->deathSaves.successes = m_deathSuccesses->value();
    character->deathSaves.failures = m_deathFailures->value();
    updateModifierLabels();
    persist();
}

void CharactersPage::onSpeedEdited(const QString& text)
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    character->speed = text.toStdString();
    persist();
}

void CharactersPage::onNotesChanged()
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    character->notes = m_notes->toPlainText().toStdString();
    persist();
}

void CharactersPage::updateModifierLabels()
{
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        m_modifiers[i]->setText(QString::fromStdString(formatModifier(abilityModifier(m_scores[i]->value()))));
    }
}

void CharactersPage::addClass()
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    ClassLevel row;
    row.name = tr("New class").toStdString();
    row.level = 1;
    character->classes.push_back(std::move(row));
    persist();
    rebuildClasses();
}

void CharactersPage::onClassNameEdited(const QString& text)
{
    Character* character = selected();
    if (m_populating || character == nullptr || isBlank(text)) {
        return;
    }
    const int index = rowOf(sender());
    if (index < 0 || static_cast<std::size_t>(index) >= character->classes.size()) {
        return;
    }
    character->classes[static_cast<std::size_t>(index)].name = text.toStdString();
    persist();
}

void CharactersPage::onClassNameFinished()
{
    const Character* character = selected();
    auto* edit = qobject_cast<QLineEdit*>(sender());
    if (character == nullptr || edit == nullptr || !isBlank(edit->text())) {
        return;
    }
    const int index = rowOf(edit);
    if (index < 0 || static_cast<std::size_t>(index) >= character->classes.size()) {
        return;
    }
    edit->setText(QString::fromStdString(character->classes[static_cast<std::size_t>(index)].name));
}

void CharactersPage::onClassLevelChanged(int value)
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    const int index = rowOf(sender());
    if (index < 0 || static_cast<std::size_t>(index) >= character->classes.size()) {
        return;
    }
    character->classes[static_cast<std::size_t>(index)].level = value;
    persist();
}

void CharactersPage::onSubclassEdited(const QString& text)
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    const int index = rowOf(sender());
    if (index < 0 || static_cast<std::size_t>(index) >= character->classes.size()) {
        return;
    }
    character->classes[static_cast<std::size_t>(index)].subclass = text.toStdString();
    persist();
}

void CharactersPage::onClassRemove()
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    const int index = rowOf(sender());
    if (index < 0 || static_cast<std::size_t>(index) >= character->classes.size()) {
        return;
    }
    character->classes.erase(character->classes.begin() + index);
    persist();
    QTimer::singleShot(0, this, [this] { rebuildClasses(); });
}

void CharactersPage::rebuildClasses()
{
    clearLayout(m_classLayout);
    const Character* character = selected();
    if (character == nullptr) {
        return;
    }
    for (int i = 0; i < static_cast<int>(character->classes.size()); ++i) {
        const ClassLevel& row = character->classes[static_cast<std::size_t>(i)];
        auto* line = new QWidget;
        auto* layout = new QHBoxLayout(line);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* name = new QLineEdit(QString::fromStdString(row.name));
        name->setPlaceholderText(tr("Class"));
        name->setProperty("row", i);
        auto* level = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        level->setValue(row.level);
        level->setToolTip(tr("Level"));
        level->setProperty("row", i);
        auto* subclass = new QLineEdit(QString::fromStdString(row.subclass));
        subclass->setPlaceholderText(tr("Subclass"));
        subclass->setProperty("row", i);
        auto* remove = new QPushButton(tr("Remove"));
        remove->setProperty("row", i);
        layout->addWidget(name, 2);
        layout->addWidget(level);
        layout->addWidget(subclass, 2);
        layout->addWidget(remove);
        m_classLayout->addWidget(line);
        connect(name, &QLineEdit::textEdited, this, &CharactersPage::onClassNameEdited);
        connect(name, &QLineEdit::editingFinished, this, &CharactersPage::onClassNameFinished);
        connect(level, &QSpinBox::valueChanged, this, &CharactersPage::onClassLevelChanged);
        connect(subclass, &QLineEdit::textEdited, this, &CharactersPage::onSubclassEdited);
        connect(remove, &QPushButton::clicked, this, &CharactersPage::onClassRemove);
    }
}

QString spellRowLabel(const CharacterSpell& entry, const std::vector<Spell>& catalog)
{
    QString name;
    QString detail;
    if (!entry.id.empty()) {
        const std::optional<Spell> spell = findSpellById(catalog, entry.id);
        if (spell.has_value()) {
            name = QString::fromStdString(spell->name);
            detail = spell->level == 0 ? QObject::tr("cantrip") : QObject::tr("level %1").arg(spell->level);
        } else {
            name = QString::fromStdString(entry.id);
        }
    } else {
        name = QString::fromStdString(entry.name);
    }
    if (entry.prepared) {
        detail = detail.isEmpty() ? QObject::tr("prepared") : detail + QObject::tr(", prepared");
    }
    if (detail.isEmpty()) {
        return name;
    }
    return name + QStringLiteral("  (") + detail + QLatin1Char(')');
}

void CharactersPage::onSpellSearch(const QString& text)
{
    m_spellMatches->clear();
    for (const Spell& spell : searchSpells(m_spells, text.toStdString())) {
        const QString level = spell.level == 0 ? tr("cantrip") : tr("level %1").arg(spell.level);
        auto* item = new QListWidgetItem(QString::fromStdString(spell.name) + QStringLiteral("  (") + level +
                                          QLatin1Char(')'));
        item->setData(Qt::UserRole, QString::fromStdString(spell.id));
        m_spellMatches->addItem(item);
    }
}

void CharactersPage::addCatalogSpell()
{
    Character* character = selected();
    const QListWidgetItem* item = m_spellMatches->currentItem();
    if (character == nullptr || item == nullptr) {
        return;
    }
    const std::string id = item->data(Qt::UserRole).toString().toStdString();
    for (int i = 0; i < static_cast<int>(character->spells.size()); ++i) {
        if (character->spells[static_cast<std::size_t>(i)].id == id) {
            m_spellList->setCurrentRow(i);
            return;
        }
    }
    CharacterSpell entry;
    entry.id = id;
    character->spells.push_back(std::move(entry));
    persist();
    refreshSpellList(static_cast<int>(character->spells.size()) - 1);
}

void CharactersPage::addCustomSpell()
{
    Character* character = selected();
    if (character == nullptr || isBlank(m_customSpell->text())) {
        return;
    }
    CharacterSpell entry;
    entry.name = m_customSpell->text().trimmed().toStdString();
    character->spells.push_back(std::move(entry));
    m_customSpell->clear();
    persist();
    refreshSpellList(static_cast<int>(character->spells.size()) - 1);
}

void CharactersPage::onSpellSelected()
{
    const Character* character = selected();
    const int row = m_spellList->currentRow();
    if (character == nullptr || row < 0 || static_cast<std::size_t>(row) >= character->spells.size()) {
        m_prepared->setEnabled(false);
        m_spellDescription->clear();
        return;
    }
    const CharacterSpell& entry = character->spells[static_cast<std::size_t>(row)];
    m_prepared->setEnabled(true);
    {
        const QSignalBlocker blocker(m_prepared);
        m_prepared->setChecked(entry.prepared);
    }
    if (!entry.id.empty()) {
        const std::optional<Spell> spell = findSpellById(m_spells, entry.id);
        if (spell.has_value()) {
            m_spellDescription->setText(QString::fromStdString(spell->description));
            return;
        }
    }
    m_spellDescription->setText(tr("No SRD description is stored for this spell."));
}

void CharactersPage::onPreparedToggled(bool prepared)
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    const int row = m_spellList->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= character->spells.size()) {
        return;
    }
    character->spells[static_cast<std::size_t>(row)].prepared = prepared;
    if (QListWidgetItem* item = m_spellList->item(row)) {
        item->setText(spellRowLabel(character->spells[static_cast<std::size_t>(row)], m_spells));
    }
    persist();
}

void CharactersPage::removeSelectedSpell()
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    const int row = m_spellList->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= character->spells.size()) {
        return;
    }
    character->spells.erase(character->spells.begin() + row);
    persist();
    const int next = character->spells.empty() ? -1 : std::min(row, static_cast<int>(character->spells.size()) - 1);
    refreshSpellList(next);
}

void CharactersPage::refreshSpellList(int selectRow)
{
    const Character* character = selected();
    const QSignalBlocker blocker(m_spellList);
    m_spellList->clear();
    if (character == nullptr) {
        onSpellSelected();
        return;
    }
    for (const CharacterSpell& entry : character->spells) {
        m_spellList->addItem(spellRowLabel(entry, m_spells));
    }
    if (selectRow >= 0 && selectRow < m_spellList->count()) {
        m_spellList->setCurrentRow(selectRow);
    }
    onSpellSelected();
}

void CharactersPage::addSlot()
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    int level = 1;
    const auto used = [&](int candidate) {
        for (const SpellSlot& slot : character->spellSlots) {
            if (slot.level == candidate) {
                return true;
            }
        }
        return false;
    };
    while (level <= 99 && used(level)) {
        ++level;
    }
    if (level > 99) {
        return;
    }
    SpellSlot slot;
    slot.level = level;
    character->spellSlots.push_back(slot);
    persist();
    rebuildSlots();
}

void CharactersPage::onSlotLevelChanged(int value)
{
    Character* character = selected();
    auto* box = qobject_cast<QSpinBox*>(sender());
    if (m_populating || character == nullptr || box == nullptr) {
        return;
    }
    const int index = rowOf(box);
    if (index < 0 || static_cast<std::size_t>(index) >= character->spellSlots.size()) {
        return;
    }
    for (int i = 0; i < static_cast<int>(character->spellSlots.size()); ++i) {
        if (i != index && character->spellSlots[static_cast<std::size_t>(i)].level == value) {
            const QSignalBlocker blocker(box);
            box->setValue(character->spellSlots[static_cast<std::size_t>(index)].level);
            return;
        }
    }
    character->spellSlots[static_cast<std::size_t>(index)].level = value;
    persist();
}

void CharactersPage::onSlotNumberChanged()
{
    Character* character = selected();
    auto* box = qobject_cast<QSpinBox*>(sender());
    if (m_populating || character == nullptr || box == nullptr) {
        return;
    }
    const int index = rowOf(box);
    if (index < 0 || static_cast<std::size_t>(index) >= character->spellSlots.size()) {
        return;
    }
    SpellSlot& slot = character->spellSlots[static_cast<std::size_t>(index)];
    if (box->property("field").toString() == QLatin1String("max")) {
        slot.max = box->value();
    } else {
        slot.current = box->value();
    }
    persist();
}

void CharactersPage::onSlotRemove()
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    const int index = rowOf(sender());
    if (index < 0 || static_cast<std::size_t>(index) >= character->spellSlots.size()) {
        return;
    }
    character->spellSlots.erase(character->spellSlots.begin() + index);
    persist();
    QTimer::singleShot(0, this, [this] { rebuildSlots(); });
}

void CharactersPage::rebuildSlots()
{
    clearLayout(m_slotLayout);
    const Character* character = selected();
    if (character == nullptr) {
        return;
    }
    for (int i = 0; i < static_cast<int>(character->spellSlots.size()); ++i) {
        const SpellSlot& slot = character->spellSlots[static_cast<std::size_t>(i)];
        auto* line = new QWidget;
        auto* layout = new QHBoxLayout(line);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* level = makeNumberBox(1, 99);
        level->setValue(slot.level);
        level->setProperty("row", i);
        level->setToolTip(tr("Slot level"));
        auto* current = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        current->setValue(slot.current);
        current->setProperty("row", i);
        current->setProperty("field", QStringLiteral("current"));
        current->setToolTip(tr("Slots remaining"));
        auto* maximum = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        maximum->setValue(slot.max);
        maximum->setProperty("row", i);
        maximum->setProperty("field", QStringLiteral("max"));
        maximum->setToolTip(tr("Slot maximum"));
        auto* remove = new QPushButton(tr("Remove"));
        remove->setProperty("row", i);
        layout->addWidget(new QLabel(tr("Level")));
        layout->addWidget(level);
        layout->addWidget(new QLabel(tr("Current")));
        layout->addWidget(current);
        layout->addWidget(new QLabel(tr("Maximum")));
        layout->addWidget(maximum);
        layout->addWidget(remove);
        layout->addStretch(1);
        m_slotLayout->addWidget(line);
        connect(level, &QSpinBox::valueChanged, this, &CharactersPage::onSlotLevelChanged);
        connect(current, &QSpinBox::valueChanged, this, &CharactersPage::onSlotNumberChanged);
        connect(maximum, &QSpinBox::valueChanged, this, &CharactersPage::onSlotNumberChanged);
        connect(remove, &QPushButton::clicked, this, &CharactersPage::onSlotRemove);
    }
}

void CharactersPage::addGear()
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    GearItem item;
    item.name = tr("New gear").toStdString();
    item.quantity = 1;
    character->gear.push_back(std::move(item));
    persist();
    rebuildGear();
}

void CharactersPage::onGearNameEdited(const QString& text)
{
    Character* character = selected();
    if (m_populating || character == nullptr || isBlank(text)) {
        return;
    }
    const int index = rowOf(sender());
    if (index < 0 || static_cast<std::size_t>(index) >= character->gear.size()) {
        return;
    }
    character->gear[static_cast<std::size_t>(index)].name = text.toStdString();
    persist();
}

void CharactersPage::onGearNameFinished()
{
    const Character* character = selected();
    auto* edit = qobject_cast<QLineEdit*>(sender());
    if (character == nullptr || edit == nullptr || !isBlank(edit->text())) {
        return;
    }
    const int index = rowOf(edit);
    if (index < 0 || static_cast<std::size_t>(index) >= character->gear.size()) {
        return;
    }
    edit->setText(QString::fromStdString(character->gear[static_cast<std::size_t>(index)].name));
}

void CharactersPage::onGearQuantityChanged(int value)
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    const int index = rowOf(sender());
    if (index < 0 || static_cast<std::size_t>(index) >= character->gear.size()) {
        return;
    }
    character->gear[static_cast<std::size_t>(index)].quantity = value;
    persist();
}

void CharactersPage::onGearEquippedToggled(bool equipped)
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    const int index = rowOf(sender());
    if (index < 0 || static_cast<std::size_t>(index) >= character->gear.size()) {
        return;
    }
    character->gear[static_cast<std::size_t>(index)].equipped = equipped;
    persist();
}

void CharactersPage::onGearRemove()
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    const int index = rowOf(sender());
    if (index < 0 || static_cast<std::size_t>(index) >= character->gear.size()) {
        return;
    }
    character->gear.erase(character->gear.begin() + index);
    persist();
    QTimer::singleShot(0, this, [this] { rebuildGear(); });
}

void CharactersPage::rebuildGear()
{
    clearLayout(m_gearLayout);
    const Character* character = selected();
    if (character == nullptr) {
        return;
    }
    for (int i = 0; i < static_cast<int>(character->gear.size()); ++i) {
        const GearItem& item = character->gear[static_cast<std::size_t>(i)];
        auto* line = new QWidget;
        auto* layout = new QHBoxLayout(line);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* name = new QLineEdit(QString::fromStdString(item.name));
        name->setPlaceholderText(tr("Name"));
        name->setProperty("row", i);
        auto* quantity = makeNumberBox(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        quantity->setValue(item.quantity);
        quantity->setProperty("row", i);
        quantity->setToolTip(tr("Quantity"));
        auto* equipped = new QCheckBox(tr("Equipped"));
        equipped->setChecked(item.equipped);
        equipped->setProperty("row", i);
        auto* remove = new QPushButton(tr("Remove"));
        remove->setProperty("row", i);
        layout->addWidget(name, 2);
        layout->addWidget(quantity);
        layout->addWidget(equipped);
        layout->addWidget(remove);
        m_gearLayout->addWidget(line);
        connect(name, &QLineEdit::textEdited, this, &CharactersPage::onGearNameEdited);
        connect(name, &QLineEdit::editingFinished, this, &CharactersPage::onGearNameFinished);
        connect(quantity, &QSpinBox::valueChanged, this, &CharactersPage::onGearQuantityChanged);
        connect(equipped, &QCheckBox::toggled, this, &CharactersPage::onGearEquippedToggled);
        connect(remove, &QPushButton::clicked, this, &CharactersPage::onGearRemove);
    }
}

void CharactersPage::addCondition()
{
    Character* character = selected();
    if (character == nullptr || m_conditionPicker->currentIndex() < 0) {
        return;
    }
    const std::string id = m_conditionPicker->currentData().toString().toStdString();
    if (id.empty()) {
        return;
    }
    for (int i = 0; i < static_cast<int>(character->conditions.size()); ++i) {
        if (character->conditions[static_cast<std::size_t>(i)] == id) {
            m_conditionList->setCurrentRow(i);
            return;
        }
    }
    character->conditions.push_back(id);
    persist();
    refreshConditions(static_cast<int>(character->conditions.size()) - 1);
}

void CharactersPage::onConditionSelected()
{
    const Character* character = selected();
    const int row = m_conditionList->currentRow();
    if (character == nullptr || row < 0 || static_cast<std::size_t>(row) >= character->conditions.size()) {
        m_conditionDescription->clear();
        return;
    }
    const std::string& id = character->conditions[static_cast<std::size_t>(row)];
    const std::optional<Condition> condition = findConditionById(m_conditions, id);
    if (condition.has_value()) {
        m_conditionDescription->setText(QString::fromStdString(condition->description));
        return;
    }
    m_conditionDescription->setText(tr("No SRD description is stored for this condition."));
}

void CharactersPage::removeSelectedCondition()
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    const int row = m_conditionList->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= character->conditions.size()) {
        return;
    }
    character->conditions.erase(character->conditions.begin() + row);
    persist();
    const int next =
        character->conditions.empty() ? -1 : std::min(row, static_cast<int>(character->conditions.size()) - 1);
    refreshConditions(next);
}

void CharactersPage::refreshConditions(int selectRow)
{
    const Character* character = selected();
    const QSignalBlocker blocker(m_conditionList);
    m_conditionList->clear();
    if (character == nullptr) {
        onConditionSelected();
        return;
    }
    for (const std::string& id : character->conditions) {
        const std::optional<Condition> condition = findConditionById(m_conditions, id);
        const QString label =
            condition.has_value() ? QString::fromStdString(condition->name) : QString::fromStdString(id);
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, QString::fromStdString(id));
        m_conditionList->addItem(item);
    }
    if (selectRow >= 0 && selectRow < m_conditionList->count()) {
        m_conditionList->setCurrentRow(selectRow);
    }
    onConditionSelected();
}

void CharactersPage::persist()
{
    if (hasLoadError()) {
        return;
    }
    try {
        m_store.saveAll(m_characters);
    } catch (const CharacterStoreError& error) {
        QMessageBox::warning(this, tr("Could not save characters"), QString::fromStdString(error.what()));
    }
}

}  // namespace combat::ui
