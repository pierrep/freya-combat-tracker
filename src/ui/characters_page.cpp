#include "ui/characters_page.h"

#include "core/character_store.h"
#include "core/uuid.h"
#include "ui/page_title.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <limits>

namespace combat::ui {

namespace {

struct AbilityField {
    const char* label;
    int AbilityScores::*member;
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

}  // namespace

CharactersPage::CharactersPage(CharacterStore& store, QWidget* parent)
    : QWidget(parent)
    , m_store(store)
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

    auto* right = new QVBoxLayout;
    m_emptyHint = new QLabel(tr("Add a character to get started."));
    m_emptyHint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(m_emptyHint);

    m_form = new QWidget;
    auto* formLayout = new QVBoxLayout(m_form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(16);

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
    m_hp = makeNumberBox();
    basics->addRow(tr("HP"), m_hp);
    m_ac = makeNumberBox();
    basics->addRow(tr("AC"), m_ac);
    m_passivePerception = makeNumberBox();
    basics->addRow(tr("Passive Perception"), m_passivePerception);
    formLayout->addLayout(basics);

    auto* abilityHeading = new QLabel(tr("Ability scores"));
    QFont headingFont = abilityHeading->font();
    headingFont.setBold(true);
    abilityHeading->setFont(headingFont);
    formLayout->addWidget(abilityHeading);

    auto* abilityGrid = new QGridLayout;
    abilityGrid->setHorizontalSpacing(16);
    abilityGrid->addWidget(new QLabel(tr("Score")), 0, 1);
    abilityGrid->addWidget(new QLabel(tr("Modifier")), 0, 2);
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        const int row = static_cast<int>(i) + 1;
        abilityGrid->addWidget(new QLabel(tr(kAbilities[i].label)), row, 0);
        m_scores[i] = makeNumberBox();
        abilityGrid->addWidget(m_scores[i], row, 1);
        m_modifiers[i] = new QLabel;
        m_modifiers[i]->setMinimumWidth(40);
        abilityGrid->addWidget(m_modifiers[i], row, 2);
    }
    abilityGrid->setColumnStretch(3, 1);
    formLayout->addLayout(abilityGrid);
    formLayout->addStretch(1);

    right->addWidget(m_form, 1);
    columns->addLayout(right, 1);

    for (const Character& character : m_characters) {
        m_list->addItem(listLabel(character));
    }

    connect(m_list, &QListWidget::currentRowChanged, this, &CharactersPage::showSelected);
    connect(m_addButton, &QPushButton::clicked, this, &CharactersPage::addCharacter);
    connect(m_deleteButton, &QPushButton::clicked, this, &CharactersPage::deleteSelected);
    connect(m_name, &QLineEdit::textEdited, this, &CharactersPage::onNameEdited);
    connect(m_name, &QLineEdit::editingFinished, this, &CharactersPage::onNameEditingFinished);
    for (QSpinBox* box : {m_hp, m_ac, m_passivePerception}) {
        connect(box, &QSpinBox::valueChanged, this, &CharactersPage::onNumberChanged);
    }
    for (QSpinBox* box : m_scores) {
        connect(box, &QSpinBox::valueChanged, this, &CharactersPage::onNumberChanged);
    }

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

QSpinBox* CharactersPage::makeNumberBox()
{
    auto* box = new QSpinBox;
    box->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    box->setMaximumWidth(140);
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
        return;
    }

    m_populating = true;
    m_name->setText(QString::fromStdString(character->name));
    m_nameError->hide();
    m_hp->setValue(character->hp);
    m_ac->setValue(character->ac);
    m_passivePerception->setValue(character->passivePerception);
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        m_scores[i]->setValue(character->abilities.*kAbilities[i].member);
    }
    m_populating = false;
    updateModifierLabels();
}

void CharactersPage::onNameEdited(const QString& text)
{
    Character* character = selected();
    if (character == nullptr) {
        return;
    }
    // A blank name is never stored; the last valid name stays until the field
    // has text again.
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

void CharactersPage::onNumberChanged()
{
    Character* character = selected();
    if (m_populating || character == nullptr) {
        return;
    }
    character->hp = m_hp->value();
    character->ac = m_ac->value();
    character->passivePerception = m_passivePerception->value();
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        character->abilities.*kAbilities[i].member = m_scores[i]->value();
    }
    updateModifierLabels();
    persist();
}

void CharactersPage::updateModifierLabels()
{
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        m_modifiers[i]->setText(QString::fromStdString(formatModifier(abilityModifier(m_scores[i]->value()))));
    }
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
