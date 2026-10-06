#include "ui/characters_page.h"

#include "core/character_store.h"
#include "core/combat_rules.h"
#include "core/uuid.h"
#include "data/pdf_import.h"
#include "ui/page_title.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
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
#include <QRegularExpression>
#include <QScrollArea>
#include <QHideEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
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
    return QString::fromStdString(character.name) + QStringLiteral("    ") +
           QString::fromStdString(formatHitPoints(character.hp.current, character.hp.max));
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

QString joinTypes(const std::vector<std::string>& types)
{
    QStringList list;
    for (const std::string& type : types) {
        list << QString::fromStdString(type);
    }
    return list.join(QStringLiteral(", "));
}

QString damageTypeList()
{
    QStringList list;
    for (const char* type : kDamageTypes) {
        list << QString::fromLatin1(type);
    }
    return list.join(QStringLiteral(", "));
}

// "Fire, poison" -> {"fire", "poison"}. Words that are not damage types go to
// unknown and are not stored.
std::vector<std::string> parseTypes(const QString& text, QStringList& unknown)
{
    std::vector<std::string> types;
    for (const QString& piece : text.split(QRegularExpression(QStringLiteral("[,;/]|\\s+and\\s+")),
                                           Qt::SkipEmptyParts)) {
        const std::string type = piece.trimmed().toLower().toStdString();
        if (type.empty()) {
            continue;
        }
        if (!isDamageType(type)) {
            unknown << piece.trimmed();
            continue;
        }
        if (std::find(types.begin(), types.end(), type) == types.end()) {
            types.push_back(type);
        }
    }
    return types;
}

}  // namespace

CharactersPage::CharactersPage(CharacterStore& store, std::vector<Spell> spells, std::vector<std::string> species,
                               const QString& attribution, const QString& catalogError, QWidget* parent)
    : QWidget(parent)
    , m_store(store)
    , m_spells(std::move(spells))
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
    outer->setContentsMargins(28, 20, 28, 16);
    outer->setSpacing(14);

    // Header: the page and the two ways to make a character.
    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    header->setObjectName(QStringLiteral("pageHeader"));
    header->addWidget(makePageTitle(tr("Characters")));
    header->addStretch(1);
    m_importButton = new QPushButton(tr("Import PDF"));
    m_importButton->setObjectName(QStringLiteral("importPdf"));
    m_importButton->setToolTip(tr("Read a D&D Beyond \"Export to PDF\" character sheet."));
    m_addButton = new QPushButton(tr("New character"));
    m_addButton->setObjectName(QStringLiteral("newCharacter"));
    makePrimary(m_addButton);
    header->addWidget(m_importButton);
    header->addWidget(m_addButton);
    outer->addLayout(header);

    auto addBanner = [outer](const QString& text) {
        auto* banner = new QLabel(text);
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setProperty("role", QStringLiteral("banner-error"));
        outer->addWidget(banner);
    };
    if (hasLoadError()) {
        addBanner(tr("The characters file could not be read, so it has not been changed.\n%1").arg(m_loadError));
    }
    if (!m_catalogError.isEmpty()) {
        addBanner(tr("An SRD catalog could not be read. Names you type are kept, and no description "
                     "is invented.\n%1")
                      .arg(m_catalogError));
    }

    auto* columns = new QHBoxLayout;
    columns->setSpacing(14);
    outer->addLayout(columns, 1);

    // Left: the party.
    auto* listCard = makeCard();
    listCard->setFixedWidth(260);
    listCard->layout()->setContentsMargins(8, 12, 8, 8);
    auto* listHeading = makeHeading(tr("Party"));
    listHeading->setContentsMargins(8, 0, 0, 0);
    listCard->layout()->addWidget(listHeading);
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("characterList"));
    listCard->layout()->addWidget(m_list);
    columns->addWidget(listCard);

    // Right: the selected sheet. Name and rests stay put; the tabs hold the rest.
    m_form = makeCard();
    auto* sheet = static_cast<QVBoxLayout*>(m_form->layout());
    sheet->setSpacing(10);
    auto* nameRow = new QHBoxLayout;
    nameRow->setSpacing(8);
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("nameField"));
    m_name->setPlaceholderText(tr("Name (required)"));
    QFont nameFont = m_name->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.35);
    nameFont.setWeight(QFont::DemiBold);
    m_name->setFont(nameFont);
    m_name->setMaximumWidth(360);
    m_shortRestButton = new QPushButton(tr("Short rest"));
    m_shortRestButton->setObjectName(QStringLiteral("shortRest"));
    m_shortRestButton->setToolTip(tr("Spend Hit Point Dice and restore Pact Magic slots."));
    m_longRestButton = new QPushButton(tr("Long rest"));
    m_longRestButton->setObjectName(QStringLiteral("longRest"));
    m_longRestButton->setToolTip(
        tr("All HP, no temporary HP, every spell slot and Hit Point Die, and one Exhaustion level less."));
    m_deleteButton = new QPushButton(tr("Delete"));
    m_deleteButton->setObjectName(QStringLiteral("deleteCharacter"));
    makeQuiet(m_deleteButton);
    nameRow->addWidget(m_name, 1);
    nameRow->addStretch(0);
    nameRow->addWidget(m_shortRestButton);
    nameRow->addWidget(m_longRestButton);
    nameRow->addSpacing(6);
    nameRow->addWidget(m_deleteButton);
    sheet->addLayout(nameRow);
    m_nameError = new QLabel(tr("Name is required."));
    m_nameError->setProperty("role", QStringLiteral("banner-error"));
    m_nameError->hide();
    sheet->addWidget(m_nameError);

    // The numbers a GM looks at most, on one line.
    auto* vitals = new QHBoxLayout;
    vitals->setSpacing(8);
    // Hit points are never negative.
    m_hpCurrent = makeNumberBox(0, 9999);
    m_hpCurrent->setObjectName(QStringLiteral("hpCurrent"));
    m_hpCurrent->setMaximumWidth(80);
    m_hpMax = makeNumberBox(0, 9999);
    m_hpMax->setObjectName(QStringLiteral("hpMax"));
    m_hpMax->setMaximumWidth(80);
    m_tempHp = makeNumberBox(0, 9999);
    m_tempHp->setObjectName(QStringLiteral("tempHp"));
    m_tempHp->setMaximumWidth(70);
    m_ac = makeNumberBox(0, 99);
    m_ac->setMaximumWidth(64);
    m_exhaustion = makeNumberBox(0, 6);
    m_exhaustion->setObjectName(QStringLiteral("exhaustion"));
    m_exhaustion->setMaximumWidth(56);
    m_exhaustionNote = makeMuted(QString());
    m_exhaustionNote->setWordWrap(false);
    vitals->addWidget(makeMuted(tr("HP")));
    vitals->addWidget(m_hpCurrent);
    vitals->addWidget(makeMuted(QStringLiteral("/")));
    vitals->addWidget(m_hpMax);
    vitals->addSpacing(12);
    vitals->addWidget(makeMuted(tr("Temporary")));
    vitals->addWidget(m_tempHp);
    vitals->addSpacing(12);
    vitals->addWidget(makeMuted(tr("AC")));
    vitals->addWidget(m_ac);
    vitals->addSpacing(12);
    vitals->addWidget(makeMuted(tr("Exhaustion")));
    vitals->addWidget(m_exhaustion);
    vitals->addWidget(m_exhaustionNote);
    vitals->addStretch(1);
    sheet->addLayout(vitals);

    auto* tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("characterTabs"));
    tabs->setDocumentMode(true);
    sheet->addWidget(tabs, 1);
    auto newTab = [tabs](const QString& title) {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 14, 12, 8);
        layout->setSpacing(10);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(page);
        tabs->addTab(scroll, title);
        return layout;
    };
    auto newForm = [] {
        auto* form = new QFormLayout;
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(10);
        form->setLabelAlignment(Qt::AlignLeft);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        return form;
    };

    // Overview: who they are and how they defend.
    QVBoxLayout* overview = newTab(tr("Overview"));
    auto* basics = newForm();
    m_species = new QComboBox;
    m_species->setObjectName(QStringLiteral("speciesField"));
    m_species->setEditable(true);
    m_species->setInsertPolicy(QComboBox::NoInsert);
    m_species->setMinimumWidth(240);
    m_species->setMaximumWidth(360);
    for (const std::string& name : m_speciesNames) {
        m_species->addItem(QString::fromStdString(name));
    }
    m_speciesHint = makeMuted(tr("This species is not in the SRD list. No description is stored for it."));
    m_speciesHint->hide();
    auto* speciesColumn = new QVBoxLayout;
    speciesColumn->setSpacing(2);
    speciesColumn->addWidget(m_species);
    speciesColumn->addWidget(m_speciesHint);
    basics->addRow(makeMuted(tr("Species")), speciesColumn);
    m_speed = new QLineEdit;
    m_speed->setObjectName(QStringLiteral("speedField"));
    m_speed->setPlaceholderText(tr("for example, 30 ft."));
    m_speed->setMaximumWidth(360);
    basics->addRow(makeMuted(tr("Speed")), m_speed);
    // Initiative and proficiency are worked out from the sheet. An override is
    // for a feat, a feature, or a total the sheet cannot see.
    m_initiativeOverride = new QCheckBox(tr("Override"));
    m_initiativeOverride->setObjectName(QStringLiteral("initiativeOverride"));
    m_initiative = makeNumberBox(-30, 30);
    m_initiative->setObjectName(QStringLiteral("initiativeBonus"));
    m_initiative->setMaximumWidth(64);
    m_initiativeDerived = new QLabel;
    auto* initiativeRow = new QHBoxLayout;
    initiativeRow->addWidget(m_initiativeDerived);
    initiativeRow->addSpacing(8);
    initiativeRow->addWidget(m_initiativeOverride);
    initiativeRow->addWidget(m_initiative);
    initiativeRow->addStretch(1);
    basics->addRow(makeMuted(tr("Initiative")), initiativeRow);
    m_proficiencyOverride = new QCheckBox(tr("Override"));
    m_proficiencyOverride->setObjectName(QStringLiteral("proficiencyOverride"));
    m_proficiency = makeNumberBox(0, 20);
    m_proficiency->setObjectName(QStringLiteral("proficiencyBonus"));
    m_proficiency->setMaximumWidth(64);
    m_proficiencyDerived = new QLabel;
    auto* proficiencyRow = new QHBoxLayout;
    proficiencyRow->addWidget(m_proficiencyDerived);
    proficiencyRow->addSpacing(8);
    proficiencyRow->addWidget(m_proficiencyOverride);
    proficiencyRow->addWidget(m_proficiency);
    proficiencyRow->addStretch(1);
    basics->addRow(makeMuted(tr("Proficiency bonus")), proficiencyRow);
    m_passivePerception = makeNumberBox(0, 99);
    m_passivePerception->setMaximumWidth(64);
    basics->addRow(makeMuted(tr("Passive Perception")), m_passivePerception);
    auto* defenseHeading = makeHeading(tr("Damage resistances and immunities"));
    defenseHeading->setContentsMargins(0, 10, 0, 0);
    basics->addRow(defenseHeading);
    QFormLayout* defenses = basics;
    m_resistances = new QLineEdit;
    m_resistances->setObjectName(QStringLiteral("resistances"));
    m_resistances->setPlaceholderText(tr("for example, fire, poison"));
    m_immunities = new QLineEdit;
    m_immunities->setObjectName(QStringLiteral("immunities"));
    m_vulnerabilities = new QLineEdit;
    m_vulnerabilities->setObjectName(QStringLiteral("vulnerabilities"));
    defenses->addRow(makeMuted(tr("Resistant")), m_resistances);
    defenses->addRow(makeMuted(tr("Immune")), m_immunities);
    defenses->addRow(makeMuted(tr("Vulnerable")), m_vulnerabilities);
    for (QLineEdit* edit : {m_resistances, m_immunities, m_vulnerabilities}) {
        edit->setMaximumWidth(520);
    }
    overview->addLayout(basics);
    m_defenseHint = new QLabel;
    m_defenseHint->setWordWrap(true);
    m_defenseHint->setProperty("role", QStringLiteral("banner-error"));
    m_defenseHint->hide();
    overview->addWidget(m_defenseHint);
    overview->addStretch(1);

    // Abilities and skills.
    QVBoxLayout* abilities = newTab(tr("Abilities and skills"));
    // Six tiles, as on the Dashboard: the modifier large, the score to edit,
    // and the saving throw with its proficiency.
    auto* abilityGrid = new QGridLayout;
    abilityGrid->setHorizontalSpacing(8);
    abilityGrid->setVerticalSpacing(8);
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        auto* tile = new QFrame;
        tile->setProperty("role", QStringLiteral("tile"));
        auto* tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(10, 8, 10, 10);
        tileLayout->setSpacing(4);
        auto* name = makeMuted(tr(kAbilities[i].label));
        name->setWordWrap(false);
        name->setAlignment(Qt::AlignCenter);
        m_modifiers[i] = new QLabel;
        QFont big = m_modifiers[i]->font();
        big.setPointSizeF(big.pointSizeF() * 1.6);
        big.setWeight(QFont::DemiBold);
        m_modifiers[i]->setFont(big);
        m_modifiers[i]->setAlignment(Qt::AlignCenter);
        m_scores[i] = makeNumberBox(1, 30);
        m_scores[i]->setFixedWidth(56);
        m_scores[i]->setAlignment(Qt::AlignCenter);
        m_scores[i]->setToolTip(tr("Ability score"));
        m_saves[i] = new QCheckBox;
        m_saves[i]->setToolTip(tr("Proficient in this saving throw"));
        m_saveBonuses[i] = makeMuted(QString());
        m_saveBonuses[i]->setWordWrap(false);
        auto* saveRow = new QHBoxLayout;
        saveRow->setSpacing(4);
        saveRow->addStretch(1);
        saveRow->addWidget(m_saves[i]);
        saveRow->addWidget(m_saveBonuses[i]);
        saveRow->addStretch(1);
        tileLayout->addWidget(name);
        tileLayout->addWidget(m_modifiers[i]);
        tileLayout->addWidget(m_scores[i], 0, Qt::AlignHCenter);
        tileLayout->addSpacing(2);
        tileLayout->addLayout(saveRow);
        abilityGrid->addWidget(tile, 0, static_cast<int>(i));
        abilityGrid->setColumnStretch(static_cast<int>(i), 1);
    }
    abilities->addLayout(abilityGrid);
    abilities->addWidget(makeMuted(tr("Tick a saving throw the character is proficient in.")));
    abilities->addSpacing(8);
    abilities->addWidget(makeHeading(tr("Skill proficiencies")));
    auto* skillGrid = new QGridLayout;
    skillGrid->setHorizontalSpacing(16);
    skillGrid->setVerticalSpacing(4);
    for (std::size_t i = 0; i < kSkills.size(); ++i) {
        const int row = static_cast<int>(i % 9);
        const int column = static_cast<int>(i / 9);
        m_skillBoxes[i] = new QCheckBox(tr("%1 (%2)").arg(tr(kSkills[i].name), tr(kSkills[i].ability)));
        skillGrid->addWidget(m_skillBoxes[i], row, column);
    }
    abilities->addLayout(skillGrid);
    abilities->addStretch(1);

    // Classes, slots, and spells.
    QVBoxLayout* magic = newTab(tr("Classes and spells"));
    auto* classHeader = new QHBoxLayout;
    classHeader->addWidget(makeHeading(tr("Classes")));
    classHeader->addStretch(1);
    auto* addClassButton = new QPushButton(tr("Add class"));
    addClassButton->setObjectName(QStringLiteral("addClass"));
    makeQuiet(addClassButton);
    classHeader->addWidget(addClassButton);
    magic->addLayout(classHeader);
    m_classLayout = new QVBoxLayout;
    magic->addLayout(m_classLayout);

    magic->addSpacing(6);
    auto* slotHeader = new QHBoxLayout;
    slotHeader->addWidget(makeHeading(tr("Spell slots")));
    slotHeader->addStretch(1);
    auto* addSlotButton = new QPushButton(tr("Add spell slot"));
    addSlotButton->setObjectName(QStringLiteral("addSlot"));
    makeQuiet(addSlotButton);
    slotHeader->addWidget(addSlotButton);
    magic->addLayout(slotHeader);
    m_slotLayout = new QVBoxLayout;
    magic->addLayout(m_slotLayout);

    magic->addSpacing(6);
    magic->addWidget(makeHeading(tr("Spells")));
    auto* spellColumns = new QHBoxLayout;
    spellColumns->setSpacing(16);
    auto* known = new QVBoxLayout;
    known->setSpacing(6);
    known->addWidget(makeMuted(tr("Known")));
    m_spellList = new QListWidget;
    m_spellList->setProperty("inset", true);
    m_spellList->setObjectName(QStringLiteral("characterSpells"));
    m_spellList->setMinimumHeight(150);
    known->addWidget(m_spellList);
    auto* spellButtons = new QHBoxLayout;
    m_prepared = new QCheckBox(tr("Prepared"));
    m_prepared->setObjectName(QStringLiteral("prepared"));
    auto* removeSpellButton = new QPushButton(tr("Remove spell"));
    makeQuiet(removeSpellButton);
    spellButtons->addWidget(m_prepared);
    spellButtons->addStretch(1);
    spellButtons->addWidget(removeSpellButton);
    known->addLayout(spellButtons);
    auto* catalog = new QVBoxLayout;
    catalog->setSpacing(6);
    catalog->addWidget(makeMuted(tr("Add from the SRD")));
    m_spellSearch = new QLineEdit;
    m_spellSearch->setObjectName(QStringLiteral("spellSearch"));
    m_spellSearch->setPlaceholderText(tr("Search SRD spells"));
    m_spellSearch->setClearButtonEnabled(true);
    catalog->addWidget(m_spellSearch);
    m_spellMatches = new QListWidget;
    m_spellMatches->setProperty("inset", true);
    m_spellMatches->setObjectName(QStringLiteral("spellMatches"));
    m_spellMatches->setMinimumHeight(110);
    catalog->addWidget(m_spellMatches);
    auto* addSpellButton = new QPushButton(tr("Add spell"));
    addSpellButton->setObjectName(QStringLiteral("addSpell"));
    catalog->addWidget(addSpellButton, 0, Qt::AlignRight);
    auto* customRow = new QHBoxLayout;
    m_customSpell = new QLineEdit;
    m_customSpell->setObjectName(QStringLiteral("customSpell"));
    m_customSpell->setPlaceholderText(tr("Spell name that is not in the SRD"));
    auto* addCustomButton = new QPushButton(tr("Add by name"));
    customRow->addWidget(m_customSpell, 1);
    customRow->addWidget(addCustomButton);
    catalog->addLayout(customRow);
    spellColumns->addLayout(known, 1);
    spellColumns->addLayout(catalog, 1);
    magic->addLayout(spellColumns);
    m_spellDescription = makeMuted(QString());
    m_spellDescription->setObjectName(QStringLiteral("spellDescription"));
    m_spellDescription->setTextInteractionFlags(Qt::TextSelectableByMouse);
    magic->addWidget(m_spellDescription);
    magic->addStretch(1);

    // Gear and notes.
    QVBoxLayout* gear = newTab(tr("Gear and notes"));
    auto* gearHeader = new QHBoxLayout;
    gearHeader->addWidget(makeHeading(tr("Gear")));
    gearHeader->addStretch(1);
    auto* addGearButton = new QPushButton(tr("Add gear"));
    addGearButton->setObjectName(QStringLiteral("addGear"));
    makeQuiet(addGearButton);
    gearHeader->addWidget(addGearButton);
    gear->addLayout(gearHeader);
    m_gearLayout = new QVBoxLayout;
    gear->addLayout(m_gearLayout);
    gear->addSpacing(6);
    gear->addWidget(makeHeading(tr("Notes")));
    m_notes = new QPlainTextEdit;
    m_notes->setObjectName(QStringLiteral("notesField"));
    m_notes->setMinimumHeight(160);
    gear->addWidget(m_notes, 1);

    m_emptyHint = makeMuted(tr("No characters yet. Choose New character, or import a D&D Beyond PDF."));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    auto* right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    right->addWidget(m_emptyHint, 1);
    right->addWidget(m_form, 1);
    columns->addLayout(right, 1);

    auto* credit = makeMuted(attribution.isEmpty() ? tr("The SRD attribution file could not be read.") : attribution);
    credit->setObjectName(QStringLiteral("srdAttribution"));
    credit->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont creditFont = credit->font();
    creditFont.setPointSizeF(creditFont.pointSizeF() * 0.85);
    credit->setFont(creditFont);
    outer->addWidget(credit);

    for (const Character& character : m_characters) {
        m_list->addItem(listLabel(character));
    }

    connect(m_list, &QListWidget::currentRowChanged, this, &CharactersPage::showSelected);
    connect(m_addButton, &QPushButton::clicked, this, &CharactersPage::addCharacter);
    connect(m_importButton, &QPushButton::clicked, this, &CharactersPage::importPdf);
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
    connect(m_shortRestButton, &QPushButton::clicked, this, &CharactersPage::shortRestSelected);
    connect(m_longRestButton, &QPushButton::clicked, this, &CharactersPage::longRestSelected);
    connect(m_initiativeOverride, &QCheckBox::toggled, this, &CharactersPage::onNumberChanged);
    connect(m_proficiencyOverride, &QCheckBox::toggled, this, &CharactersPage::onNumberChanged);
    for (QLineEdit* edit : {m_resistances, m_immunities, m_vulnerabilities}) {
        connect(edit, &QLineEdit::textEdited, this, &CharactersPage::onDefensesEdited);
    }

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, &CharactersPage::saveNow);

    const auto numberBoxes = {m_hpCurrent, m_hpMax, m_tempHp, m_ac, m_initiative, m_proficiency, m_passivePerception,
                              m_exhaustion};
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
        m_importButton->setEnabled(false);
        m_shortRestButton->setEnabled(false);
        m_longRestButton->setEnabled(false);
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
    flushPendingSave();
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

void CharactersPage::importPdf()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import a character PDF"), QString(),
                                                      tr("PDF files (*.pdf);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    PdfImportResult imported;
    try {
        imported = importCharacterPdf(std::filesystem::path(path.toStdString()), m_spells);
    } catch (const PdfImportError& error) {
        QMessageBox::warning(this, tr("Could not import PDF"), QString::fromStdString(error.what()));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Review PDF import"));
    dialog.resize(560, 460);
    auto* layout = new QVBoxLayout(&dialog);
    auto* report = new QPlainTextEdit(QString::fromStdString(formatPdfImportReport(imported.report)));
    report->setObjectName(QStringLiteral("importReport"));
    report->setReadOnly(true);
    layout->addWidget(report);

    auto* picker = new QComboBox;
    picker->setObjectName(QStringLiteral("replaceCharacter"));
    for (const Character& character : m_characters) {
        picker->addItem(listLabel(character));
    }
    if (!m_characters.empty()) {
        layout->addWidget(new QLabel(tr("Replace this character")));
        layout->addWidget(picker);
    }

    auto* buttons = new QDialogButtonBox;
    auto* createButton = buttons->addButton(tr("Create new character"), QDialogButtonBox::AcceptRole);
    createButton->setObjectName(QStringLiteral("createImportedCharacter"));
    auto* replaceButton = buttons->addButton(tr("Replace"), QDialogButtonBox::ActionRole);
    replaceButton->setObjectName(QStringLiteral("replaceImportedCharacter"));
    replaceButton->setEnabled(!m_characters.empty());
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(replaceButton, &QPushButton::clicked, &dialog, [this, &dialog, picker] {
        const int row = picker->currentIndex();
        if (row < 0 || row >= count()) {
            return;
        }
        const auto answer = QMessageBox::question(
            &dialog, tr("Replace character"),
            tr("Replace the imported fields on %1? Its id stays the same. Encounters are not changed.")
                .arg(listLabel(m_characters[static_cast<std::size_t>(row)])));
        if (answer != QMessageBox::Yes) {
            return;
        }
        dialog.done(2);
    });

    const int choice = dialog.exec();
    if (choice == QDialog::Accepted) {
        Character character = imported.character;
        character.id = generateUuidV4([this] { return m_rng(); });
        if (character.name.empty()) {
            character.name = tr("Imported character").toStdString();
        }
        m_characters.push_back(std::move(character));
        m_list->addItem(listLabel(m_characters.back()));
        persist();
        emit countChanged(count());
        m_list->setCurrentRow(count() - 1);
        return;
    }
    if (choice != 2) {
        return;
    }

    const int row = picker->currentIndex();
    if (row < 0 || row >= count()) {
        return;
    }
    Character& target = m_characters[static_cast<std::size_t>(row)];
    applyImportedCharacter(target, imported);
    m_list->item(row)->setText(listLabel(target));
    persist();
    if (m_list->currentRow() == row) {
        showSelected();
    } else {
        m_list->setCurrentRow(row);
    }
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
        m_spellDescription->clear();
        m_shortRestButton->setEnabled(false);
        m_longRestButton->setEnabled(false);
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
    m_initiativeOverride->setChecked(character->initiativeOverride.has_value());
    m_initiative->setValue(initiativeModifier(*character));
    m_proficiencyOverride->setChecked(character->proficiencyOverride.has_value());
    m_proficiency->setValue(proficiencyBonus(*character));
    m_exhaustion->setValue(character->exhaustion);
    m_resistances->setText(joinTypes(character->defenses.resistances));
    m_immunities->setText(joinTypes(character->defenses.immunities));
    m_vulnerabilities->setText(joinTypes(character->defenses.vulnerabilities));
    m_defenseHint->hide();
    m_shortRestButton->setEnabled(true);
    m_longRestButton->setEnabled(true);
    m_passivePerception->setValue(character->passivePerception);
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        m_scores[i]->setValue(character->abilities.*kAbilities[i].member);
        m_saves[i]->setChecked(character->savingThrows.*kSavingThrows[i].member);
    }
    for (std::size_t i = 0; i < kSkills.size(); ++i) {
        m_skillBoxes[i]->setChecked(character->skills.*kSkills[i].member);
    }
    m_notes->setPlainText(QString::fromStdString(character->notes));
    m_populating = false;
    updateModifierLabels();
    rebuildClasses();
    refreshSpellList(character->spells.empty() ? -1 : 0);
    rebuildSlots();
    rebuildGear();
    updateDerivedLabels();
    // The Party list shows the HP; a rest or an import may have changed it.
    if (QListWidgetItem* item = m_list->currentItem()) {
        item->setText(listLabel(*character));
    }
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
    m_list->currentItem()->setText(listLabel(*character));
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
    character->hp.max = m_hpMax->value();
    character->hp.current = cappedHitPoints(m_hpCurrent->value(), character->hp.max);
    if (m_hpCurrent->value() != character->hp.current) {
        const QSignalBlocker blocker(m_hpCurrent);
        m_hpCurrent->setValue(character->hp.current);
    }
    character->tempHp = m_tempHp->value();
    character->ac = m_ac->value();
    character->exhaustion = m_exhaustion->value();
    character->passivePerception = m_passivePerception->value();
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        character->abilities.*kAbilities[i].member = m_scores[i]->value();
        character->savingThrows.*kSavingThrows[i].member = m_saves[i]->isChecked();
    }
    for (std::size_t i = 0; i < kSkills.size(); ++i) {
        character->skills.*kSkills[i].member = m_skillBoxes[i]->isChecked();
    }
    // An override box only counts while its check box is on. Turning it on
    // starts from the worked-out value.
    if (m_initiativeOverride->isChecked()) {
        character->initiativeOverride = m_initiative->value();
    } else {
        character->initiativeOverride.reset();
    }
    if (m_proficiencyOverride->isChecked()) {
        character->proficiencyOverride = m_proficiency->value();
    } else {
        character->proficiencyOverride.reset();
    }
    updateModifierLabels();
    updateDerivedLabels();
    if (QListWidgetItem* item = m_list->currentItem()) {
        item->setText(listLabel(*character));
    }
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
    const Character* character = selected();
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        m_modifiers[i]->setText(QString::fromStdString(formatModifier(abilityModifier(m_scores[i]->value()))));
        const int bonus = character != nullptr ? characterSaveBonus(*character, kAbilityOrder[i])
                                               : abilityModifier(m_scores[i]->value());
        m_saveBonuses[i]->setText(tr("Save %1").arg(QString::fromStdString(formatModifier(bonus))));
        m_saveBonuses[i]->setProperty("role", m_saves[i]->isChecked() ? QStringLiteral("accent") : QStringLiteral("muted"));
        m_saveBonuses[i]->style()->unpolish(m_saveBonuses[i]);
        m_saveBonuses[i]->style()->polish(m_saveBonuses[i]);
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
    ClassLevel& row = character->classes[static_cast<std::size_t>(index)];
    row.level = value;
    row.hitDiceSpent = std::min(row.hitDiceSpent, row.level);
    persist();
    updateDerivedLabels();
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
        auto* level = makeNumberBox(1, 20);
        level->setValue(row.level);
        level->setToolTip(tr("Level"));
        auto* hitDice = new QLabel(tr("d%1, %2/%3 Hit Point Dice")
                                       .arg(hitDieForClass(row.name))
                                       .arg(hitDiceRemaining(row))
                                       .arg(row.level));
        level->setProperty("row", i);
        auto* subclass = new QLineEdit(QString::fromStdString(row.subclass));
        subclass->setPlaceholderText(tr("Subclass"));
        subclass->setProperty("row", i);
        auto* remove = new QPushButton(tr("Remove"));
        makeQuiet(remove);
        remove->setProperty("row", i);
        layout->addWidget(name, 2);
        layout->addWidget(level);
        layout->addWidget(subclass, 2);
        layout->addWidget(hitDice);
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

void CharactersPage::onSlotShortRestToggled(bool shortRest)
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    const int index = rowOf(sender());
    if (index < 0 || static_cast<std::size_t>(index) >= character->spellSlots.size()) {
        return;
    }
    character->spellSlots[static_cast<std::size_t>(index)].shortRest = shortRest;
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
        auto* current = makeNumberBox(0, 99);
        current->setValue(slot.current);
        current->setProperty("row", i);
        current->setProperty("field", QStringLiteral("current"));
        current->setToolTip(tr("Slots remaining"));
        auto* maximum = makeNumberBox(0, 99);
        maximum->setValue(slot.max);
        maximum->setProperty("row", i);
        maximum->setProperty("field", QStringLiteral("max"));
        maximum->setToolTip(tr("Slot maximum"));
        auto* remove = new QPushButton(tr("Remove"));
        makeQuiet(remove);
        remove->setProperty("row", i);
        layout->addWidget(new QLabel(tr("Level")));
        layout->addWidget(level);
        layout->addWidget(new QLabel(tr("Current")));
        layout->addWidget(current);
        layout->addWidget(new QLabel(tr("Maximum")));
        layout->addWidget(maximum);
        auto* pact = new QCheckBox(tr("Short rest"));
        pact->setToolTip(tr("Pact Magic: these slots come back on a Short Rest too."));
        pact->setChecked(slot.shortRest);
        pact->setProperty("row", i);
        layout->addWidget(pact);
        layout->addWidget(remove);
        layout->addStretch(1);
        m_slotLayout->addWidget(line);
        connect(pact, &QCheckBox::toggled, this, &CharactersPage::onSlotShortRestToggled);
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
        auto* quantity = makeNumberBox(0, std::numeric_limits<int>::max());
        quantity->setValue(item.quantity);
        quantity->setProperty("row", i);
        quantity->setToolTip(tr("Quantity"));
        auto* equipped = new QCheckBox(tr("Equipped"));
        equipped->setChecked(item.equipped);
        equipped->setProperty("row", i);
        auto* remove = new QPushButton(tr("Remove"));
        makeQuiet(remove);
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

void CharactersPage::onDefensesEdited()
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    QStringList unknown;
    character->defenses.resistances = parseTypes(m_resistances->text(), unknown);
    character->defenses.immunities = parseTypes(m_immunities->text(), unknown);
    character->defenses.vulnerabilities = parseTypes(m_vulnerabilities->text(), unknown);
    m_defenseHint->setVisible(!unknown.isEmpty());
    m_defenseHint->setText(tr("Not a damage type, so it is ignored: %1. The types are %2.")
                               .arg(unknown.join(QStringLiteral(", ")), damageTypeList()));
    persist();
}

void CharactersPage::updateDerivedLabels()
{
    const Character* character = selected();
    if (character == nullptr) {
        return;
    }
    const int dexterity = abilityModifier(character->abilities.dexterity);
    const int table = proficiencyBonusForLevel(totalClassLevel(*character));
    m_initiativeDerived->setText(tr("Dexterity %1").arg(QString::fromStdString(formatModifier(dexterity))));
    m_proficiencyDerived->setText(tr("Level %1 table %2")
                                      .arg(totalClassLevel(*character))
                                      .arg(QString::fromStdString(formatModifier(table))));
    m_initiative->setEnabled(m_initiativeOverride->isChecked());
    m_proficiency->setEnabled(m_proficiencyOverride->isChecked());
    if (!m_initiativeOverride->isChecked() && m_initiative->value() != dexterity) {
        const QSignalBlocker blocker(m_initiative);
        m_initiative->setValue(dexterity);
    }
    if (!m_proficiencyOverride->isChecked() && m_proficiency->value() != table) {
        const QSignalBlocker blocker(m_proficiency);
        m_proficiency->setValue(table);
    }
    const int level = character->exhaustion;
    m_exhaustionNote->setText(level == 0 ? tr("None")
                              : level >= 6
                                  ? tr("Level 6: the character dies.")
                                  : tr("D20 Tests -%1, Speed -%2 ft.").arg(2 * level).arg(exhaustionSpeedPenalty(level)));
}

void CharactersPage::longRestSelected()
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    longRest(*character);
    persist();
    showSelected();
}

void CharactersPage::shortRestSelected()
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Short rest"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Hit Point Dice to spend. Each rolls its die plus the Constitution modifier (%1).")
                                     .arg(QString::fromStdString(
                                         formatModifier(abilityModifier(character->abilities.constitution))))));
    auto* form = new QFormLayout;
    std::vector<QSpinBox*> boxes;
    for (const ClassLevel& row : character->classes) {
        auto* box = new QSpinBox;
        box->setRange(0, hitDiceRemaining(row));
        form->addRow(tr("%1 (d%2, %3 left)")
                         .arg(QString::fromStdString(row.name))
                         .arg(hitDieForClass(row.name))
                         .arg(hitDiceRemaining(row)),
                     box);
        boxes.push_back(box);
    }
    if (boxes.empty()) {
        layout->addWidget(new QLabel(tr("Add a class to spend Hit Point Dice. Pact Magic slots still come back.")));
    }
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    std::vector<int> dice;
    for (QSpinBox* box : boxes) {
        dice.push_back(box->value());
    }
    const ShortRestResult result = shortRest(*character, dice, [this](int sides) {
        std::uniform_int_distribution<int> face(1, sides);
        return face(m_rng);
    });
    persist();
    showSelected();
    QMessageBox::information(this, tr("Short rest"),
                             tr("Spent %1 Hit Point Dice and regained %2 HP.").arg(result.diceSpent).arg(result.healed));
}

void CharactersPage::persist()
{
    if (hasLoadError()) {
        return;
    }
    m_savePending = true;
    m_saveTimer->start();
}

void CharactersPage::flushPendingSave()
{
    if (m_savePending) {
        m_saveTimer->stop();
        saveNow();
    }
}

void CharactersPage::saveNow()
{
    m_savePending = false;
    if (hasLoadError()) {
        return;
    }
    try {
        m_store.saveAll(m_characters);
    } catch (const CharacterStoreError& error) {
        QMessageBox::warning(this, tr("Could not save characters"), QString::fromStdString(error.what()));
    }
}

void CharactersPage::hideEvent(QHideEvent* event)
{
    flushPendingSave();
    QWidget::hideEvent(event);
}

}  // namespace combat::ui
