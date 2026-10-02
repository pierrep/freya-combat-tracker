#include "ui/monsters_page.h"

#include "core/custom_monster_store.h"
#include "core/uuid.h"
#include "ui/page_title.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace combat::ui {

namespace {

struct AbilityField {
    const char* label;
    int AbilityScores::*member;
};

const std::array<AbilityField, 6> kAbilities{{
    {QT_TRANSLATE_NOOP("MonstersPage", "Strength"), &AbilityScores::strength},
    {QT_TRANSLATE_NOOP("MonstersPage", "Dexterity"), &AbilityScores::dexterity},
    {QT_TRANSLATE_NOOP("MonstersPage", "Constitution"), &AbilityScores::constitution},
    {QT_TRANSLATE_NOOP("MonstersPage", "Intelligence"), &AbilityScores::intelligence},
    {QT_TRANSLATE_NOOP("MonstersPage", "Wisdom"), &AbilityScores::wisdom},
    {QT_TRANSLATE_NOOP("MonstersPage", "Charisma"), &AbilityScores::charisma},
}};

bool isBlank(const QString& text)
{
    return text.trimmed().isEmpty();
}

QString listLabel(const Monster& monster)
{
    const QString name = QString::fromStdString(monster.name);
    if (monster.source == kCustomMonsterSource) {
        return name + QStringLiteral("    Custom");
    }
    return name;
}

QString scoreText(int score)
{
    return QStringLiteral("%1  (%2)")
        .arg(score)
        .arg(QString::fromStdString(formatModifier(abilityModifier(score))));
}

QString hpText(const Monster& monster)
{
    const QString hp = QString::number(monster.hp);
    if (monster.hitDice.empty()) {
        return hp;
    }
    return hp + QStringLiteral(" (") + QString::fromStdString(monster.hitDice) + QLatin1Char(')');
}

double challengeValue(const QString& rating, bool* numeric)
{
    bool ok = false;
    if (rating.contains(QLatin1Char('/'))) {
        const QStringList parts = rating.split(QLatin1Char('/'));
        if (parts.size() == 2) {
            bool okNumerator = false;
            bool okDenominator = false;
            const double numerator = parts[0].toDouble(&okNumerator);
            const double denominator = parts[1].toDouble(&okDenominator);
            if (okNumerator && okDenominator && denominator != 0.0) {
                *numeric = true;
                return numerator / denominator;
            }
        }
    }
    const double value = rating.toDouble(&ok);
    *numeric = ok;
    return ok ? value : 0.0;
}

}  // namespace

MonstersPage::MonstersPage(MergedMonsterCatalog& catalog, CustomMonsterStore& customStore, const QString& attribution,
                           const QString& catalogError, QWidget* parent)
    : QWidget(parent)
    , m_catalog(catalog)
    , m_store(customStore)
    , m_rng(std::random_device{}())
{
    try {
        const CustomMonsterLoadResult loaded = m_store.loadAll();
        m_custom = loaded.monsters;
        m_catalog.setCustomMonsters(m_custom);
        if (!loaded.skipped.empty()) {
            m_customError.clear();
            // Skipped rows are a warning, not a hard failure. Edits can still be saved.
        }
        if (!loaded.skipped.empty()) {
            QStringList lines;
            for (const std::string& skipped : loaded.skipped) {
                lines.push_back(QString::fromStdString(skipped));
            }
            // Stashed on the catalog error label's sibling below.
            setProperty("skippedMonsters", lines.join(QLatin1Char('\n')));
        }
    } catch (const MonsterDataError& error) {
        m_customError = QString::fromStdString(error.what());
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 24, 32, 24);
    outer->setSpacing(12);
    outer->addWidget(makePageTitle(tr("Monsters")));

    if (!catalogError.isEmpty()) {
        auto* banner = new QLabel(tr("The SRD catalog could not be read.\n%1").arg(catalogError));
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
        outer->addWidget(banner);
    }
    if (!m_customError.isEmpty()) {
        auto* banner = new QLabel(tr("The custom monster file could not be read, so it has not been changed.\n%1")
                                      .arg(m_customError));
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
        outer->addWidget(banner);
    }
    const QString skipped = property("skippedMonsters").toString();
    if (!skipped.isEmpty()) {
        auto* banner = new QLabel(tr("Some custom monsters were skipped and were not loaded.\n%1").arg(skipped));
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setStyleSheet(QStringLiteral("QLabel { color: #8a5a00; }"));
        outer->addWidget(banner);
    }

    auto* filters = new QHBoxLayout;
    filters->setSpacing(8);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("monsterSearch"));
    m_search->setPlaceholderText(tr("Search by name"));
    m_search->setClearButtonEnabled(true);
    filters->addWidget(m_search, 1);
    m_typeFilter = new QComboBox;
    m_typeFilter->setObjectName(QStringLiteral("monsterTypeFilter"));
    m_crFilter = new QComboBox;
    m_crFilter->setObjectName(QStringLiteral("monsterCrFilter"));
    filters->addWidget(m_typeFilter);
    filters->addWidget(m_crFilter);
    outer->addLayout(filters);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(24);
    outer->addLayout(columns, 1);

    auto* left = new QVBoxLayout;
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("monsterList"));
    m_list->setMinimumWidth(240);
    m_list->setMaximumWidth(320);
    left->addWidget(m_list, 1);
    auto* listButtons = new QHBoxLayout;
    m_addButton = new QPushButton(tr("Add"));
    m_deleteButton = new QPushButton(tr("Delete"));
    listButtons->addWidget(m_addButton);
    listButtons->addWidget(m_deleteButton);
    left->addLayout(listButtons);
    columns->addLayout(left);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);

    m_emptyHint = new QLabel(tr("No monsters match."));
    m_emptyHint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    rightLayout->addWidget(m_emptyHint);

    m_stat = new QWidget;
    auto* statLayout = new QVBoxLayout(m_stat);
    statLayout->setContentsMargins(0, 0, 0, 0);
    statLayout->setSpacing(8);
    m_statName = new QLabel;
    m_statName->setObjectName(QStringLiteral("monsterStatName"));
    m_statName->setWordWrap(true);
    QFont nameFont = m_statName->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.4);
    nameFont.setBold(true);
    m_statName->setFont(nameFont);
    statLayout->addWidget(m_statName);
    m_statType = new QLabel;
    m_statType->setWordWrap(true);
    statLayout->addWidget(m_statType);

    auto* statForm = new QFormLayout;
    m_statAc = new QLabel;
    m_statHp = new QLabel;
    m_statSpeed = new QLabel;
    m_statSpeed->setWordWrap(true);
    m_statInitiative = new QLabel;
    m_statPerception = new QLabel;
    m_statChallenge = new QLabel;
    statForm->addRow(tr("AC"), m_statAc);
    statForm->addRow(tr("HP"), m_statHp);
    statForm->addRow(tr("Speed"), m_statSpeed);
    statForm->addRow(tr("Initiative"), m_statInitiative);
    statForm->addRow(tr("Passive Perception"), m_statPerception);
    statForm->addRow(tr("Challenge Rating"), m_statChallenge);
    statLayout->addLayout(statForm);

    auto* abilityHeading = new QLabel(tr("Ability scores"));
    QFont headingFont = abilityHeading->font();
    headingFont.setBold(true);
    abilityHeading->setFont(headingFont);
    statLayout->addWidget(abilityHeading);
    auto* statGrid = new QGridLayout;
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        const int row = static_cast<int>(i);
        statGrid->addWidget(new QLabel(tr(kAbilities[i].label)), row, 0);
        m_statScores[i] = new QLabel;
        statGrid->addWidget(m_statScores[i], row, 1);
    }
    statGrid->setColumnStretch(2, 1);
    statLayout->addLayout(statGrid);
    statLayout->addStretch(1);
    rightLayout->addWidget(m_stat);

    m_form = new QWidget;
    auto* formLayout = new QVBoxLayout(m_form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(12);
    auto* basics = new QFormLayout;
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("monsterNameField"));
    m_nameError = new QLabel(tr("Name is required."));
    m_nameError->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
    m_nameError->hide();
    auto* nameColumn = new QVBoxLayout;
    nameColumn->setSpacing(2);
    nameColumn->addWidget(m_name);
    nameColumn->addWidget(m_nameError);
    basics->addRow(tr("Name"), nameColumn);
    m_size = new QLineEdit;
    basics->addRow(tr("Size"), m_size);
    m_creatureType = new QLineEdit;
    basics->addRow(tr("Type"), m_creatureType);
    m_ac = makeNumberBox();
    basics->addRow(tr("AC"), m_ac);
    m_hp = makeNumberBox();
    basics->addRow(tr("HP"), m_hp);
    m_hitDice = new QLineEdit;
    m_hitDice->setPlaceholderText(tr("3d6"));
    basics->addRow(tr("Hit dice"), m_hitDice);
    m_speed = new QLineEdit;
    basics->addRow(tr("Speed"), m_speed);
    m_initiative = makeNumberBox();
    basics->addRow(tr("Initiative bonus"), m_initiative);
    m_passivePerception = makeNumberBox();
    basics->addRow(tr("Passive Perception"), m_passivePerception);
    m_challengeRating = new QLineEdit;
    m_challengeRating->setPlaceholderText(tr("1/4"));
    basics->addRow(tr("Challenge rating"), m_challengeRating);
    formLayout->addLayout(basics);

    auto* editHeading = new QLabel(tr("Ability scores"));
    editHeading->setFont(headingFont);
    formLayout->addWidget(editHeading);
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
    rightLayout->addWidget(m_form);
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

    connect(m_list, &QListWidget::currentRowChanged, this, &MonstersPage::showSelected);
    connect(m_addButton, &QPushButton::clicked, this, &MonstersPage::addMonster);
    connect(m_deleteButton, &QPushButton::clicked, this, &MonstersPage::deleteSelected);
    connect(m_search, &QLineEdit::textChanged, this, &MonstersPage::onSearchChanged);
    connect(m_typeFilter, &QComboBox::currentIndexChanged, this, &MonstersPage::onFilterChanged);
    connect(m_crFilter, &QComboBox::currentIndexChanged, this, &MonstersPage::onFilterChanged);
    connect(m_name, &QLineEdit::textEdited, this, &MonstersPage::onNameEdited);
    connect(m_name, &QLineEdit::editingFinished, this, &MonstersPage::onNameEditingFinished);
    for (QLineEdit* field : {m_size, m_creatureType, m_hitDice, m_speed, m_challengeRating}) {
        connect(field, &QLineEdit::textEdited, this, &MonstersPage::onFormEdited);
        connect(field, &QLineEdit::editingFinished, this, &MonstersPage::onTextFinished);
    }
    for (QSpinBox* box : {m_hp, m_ac, m_initiative, m_passivePerception}) {
        connect(box, &QSpinBox::valueChanged, this, &MonstersPage::onFormEdited);
    }
    for (QSpinBox* box : m_scores) {
        connect(box, &QSpinBox::valueChanged, this, &MonstersPage::onFormEdited);
    }

    if (!m_customError.isEmpty()) {
        m_addButton->setEnabled(false);
        m_deleteButton->setEnabled(false);
    }

    rebuildFilters();
    refreshResults();
}

QSpinBox* MonstersPage::makeNumberBox()
{
    auto* box = new QSpinBox;
    box->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    box->setMaximumWidth(140);
    return box;
}

QString MonstersPage::currentId() const
{
    const QListWidgetItem* item = m_list->currentItem();
    if (item == nullptr) {
        return {};
    }
    return item->data(Qt::UserRole).toString();
}

const Monster* MonstersPage::selectedVisible() const
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= static_cast<int>(m_visible.size())) {
        return nullptr;
    }
    return &m_visible[static_cast<std::size_t>(row)];
}

Monster* MonstersPage::selectedCustom()
{
    const Monster* shown = selectedVisible();
    if (shown == nullptr || shown->source != kCustomMonsterSource) {
        return nullptr;
    }
    for (Monster& monster : m_custom) {
        if (monster.id == shown->id) {
            return &monster;
        }
    }
    return nullptr;
}

void MonstersPage::rebuildFilters()
{
    const QString previousType = m_typeFilter->currentIndex() > 0 ? m_typeFilter->currentData().toString() : QString();
    const QString previousCr = m_crFilter->currentIndex() > 0 ? m_crFilter->currentData().toString() : QString();

    std::vector<std::string> types;
    std::vector<std::string> ratings;
    for (const Monster& monster : m_catalog.search(MonsterQuery{})) {
        if (std::find(types.begin(), types.end(), monster.creatureType) == types.end()) {
            types.push_back(monster.creatureType);
        }
        if (std::find(ratings.begin(), ratings.end(), monster.challengeRating) == ratings.end()) {
            ratings.push_back(monster.challengeRating);
        }
    }
    std::sort(types.begin(), types.end());
    std::sort(ratings.begin(), ratings.end(), [](const std::string& left, const std::string& right) {
        bool leftNumeric = false;
        bool rightNumeric = false;
        const double leftValue = challengeValue(QString::fromStdString(left), &leftNumeric);
        const double rightValue = challengeValue(QString::fromStdString(right), &rightNumeric);
        if (leftNumeric != rightNumeric) {
            return leftNumeric;
        }
        if (leftNumeric && leftValue != rightValue) {
            return leftValue < rightValue;
        }
        return left < right;
    });

    const QSignalBlocker blockType(m_typeFilter);
    const QSignalBlocker blockCr(m_crFilter);
    m_typeFilter->clear();
    m_typeFilter->addItem(tr("Any type"));
    for (const std::string& type : types) {
        const QString text = QString::fromStdString(type);
        m_typeFilter->addItem(text.isEmpty() ? tr("(blank type)") : text, text);
    }
    m_crFilter->clear();
    m_crFilter->addItem(tr("Any challenge rating"));
    for (const std::string& rating : ratings) {
        const QString text = QString::fromStdString(rating);
        m_crFilter->addItem(text.isEmpty() ? tr("(blank challenge rating)") : text, text);
    }

    const int typeIndex = previousType.isNull() ? -1 : m_typeFilter->findData(previousType);
    m_typeFilter->setCurrentIndex(typeIndex > 0 ? typeIndex : 0);
    const int crIndex = previousCr.isNull() ? -1 : m_crFilter->findData(previousCr);
    m_crFilter->setCurrentIndex(crIndex > 0 ? crIndex : 0);
}

void MonstersPage::refreshResults()
{
    const QString selected = currentId();
    MonsterQuery query;
    query.nameSubstring = m_search->text().toStdString();
    if (m_typeFilter->currentIndex() > 0) {
        query.creatureType = m_typeFilter->currentData().toString().toStdString();
    }
    if (m_crFilter->currentIndex() > 0) {
        query.challengeRating = m_crFilter->currentData().toString().toStdString();
    }
    m_visible = m_catalog.search(query);

    const QSignalBlocker blockList(m_list);
    m_list->clear();
    int rowToSelect = -1;
    for (std::size_t i = 0; i < m_visible.size(); ++i) {
        auto* item = new QListWidgetItem(listLabel(m_visible[i]));
        item->setData(Qt::UserRole, QString::fromStdString(m_visible[i].id));
        m_list->addItem(item);
        if (item->data(Qt::UserRole).toString() == selected) {
            rowToSelect = static_cast<int>(i);
        }
    }
    if (rowToSelect < 0 && !m_visible.empty()) {
        rowToSelect = 0;
    }
    if (rowToSelect >= 0) {
        m_list->setCurrentRow(rowToSelect);
    }
    showSelected();
}

void MonstersPage::showSelected()
{
    const Monster* monster = selectedVisible();
    const bool custom = monster != nullptr && monster->source == kCustomMonsterSource;
    m_deleteButton->setEnabled(custom && m_customError.isEmpty());
    m_stat->setVisible(monster != nullptr && !custom);
    m_form->setVisible(custom);
    m_emptyHint->setVisible(monster == nullptr);
    if (monster == nullptr) {
        return;
    }

    if (!custom) {
        m_statName->setText(QString::fromStdString(monster->name));
        const QString size = QString::fromStdString(monster->size);
        const QString type = QString::fromStdString(monster->creatureType);
        m_statType->setText(size.isEmpty() ? type : size + QLatin1Char(' ') + type);
        m_statAc->setText(QString::number(monster->ac));
        m_statHp->setText(hpText(*monster));
        m_statSpeed->setText(QString::fromStdString(monster->speed));
        m_statInitiative->setText(QString::fromStdString(formatModifier(monster->initiativeBonus)));
        m_statPerception->setText(QString::number(monster->passivePerception));
        m_statChallenge->setText(QString::fromStdString(monster->challengeRating));
        for (std::size_t i = 0; i < kAbilities.size(); ++i) {
            m_statScores[i]->setText(scoreText(monster->abilities.*kAbilities[i].member));
        }
        return;
    }

    m_populating = true;
    const QString name = QString::fromStdString(monster->name);
    if (m_name->text() != name || !m_name->hasFocus()) {
        m_name->setText(name);
    }
    m_nameError->setVisible(isBlank(m_name->text()));
    m_size->setText(QString::fromStdString(monster->size));
    m_creatureType->setText(QString::fromStdString(monster->creatureType));
    m_ac->setValue(monster->ac);
    m_hp->setValue(monster->hp);
    m_hitDice->setText(QString::fromStdString(monster->hitDice));
    m_speed->setText(QString::fromStdString(monster->speed));
    m_initiative->setValue(monster->initiativeBonus);
    m_passivePerception->setValue(monster->passivePerception);
    m_challengeRating->setText(QString::fromStdString(monster->challengeRating));
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        m_scores[i]->setValue(monster->abilities.*kAbilities[i].member);
        m_modifiers[i]->setText(QString::fromStdString(formatModifier(abilityModifier(m_scores[i]->value()))));
    }
    m_populating = false;
}

void MonstersPage::onSearchChanged()
{
    if (m_populating) {
        return;
    }
    refreshResults();
}

void MonstersPage::onFilterChanged()
{
    if (m_populating) {
        return;
    }
    refreshResults();
}

void MonstersPage::addMonster()
{
    if (!m_customError.isEmpty()) {
        return;
    }
    Monster monster;
    monster.id = generateUuidV4([this] { return m_rng(); });
    monster.name = tr("New monster").toStdString();
    monster.size = "Medium";
    monster.ac = 10;
    monster.hp = 1;
    monster.speed = "30 ft.";
    monster.passivePerception = 10;
    monster.source = kCustomMonsterSource;
    m_custom.push_back(monster);
    m_catalog.setCustomMonsters(m_custom);
    persist();

    {
        const QSignalBlocker blockSearch(m_search);
        m_search->clear();
    }
    rebuildFilters();
    refreshResults();
    selectId(QString::fromStdString(monster.id));
    m_name->setFocus();
    m_name->selectAll();
}

void MonstersPage::deleteSelected()
{
    Monster* monster = selectedCustom();
    if (monster == nullptr) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Delete monster"),
        tr("Delete %1? This cannot be undone.").arg(QString::fromStdString(monster->name)));
    if (answer != QMessageBox::Yes) {
        return;
    }
    const std::string id = monster->id;
    m_custom.erase(std::remove_if(m_custom.begin(), m_custom.end(),
                                  [&id](const Monster& candidate) { return candidate.id == id; }),
                   m_custom.end());
    m_catalog.setCustomMonsters(m_custom);
    persist();
    rebuildFilters();
    refreshResults();
}

void MonstersPage::onNameEdited(const QString& text)
{
    Monster* monster = selectedCustom();
    if (m_populating || monster == nullptr) {
        return;
    }
    m_nameError->setVisible(isBlank(text));
    if (isBlank(text)) {
        return;
    }
    monster->name = text.toStdString();
    if (QListWidgetItem* item = m_list->currentItem()) {
        item->setText(listLabel(*monster));
    }
    if (Monster* visible = const_cast<Monster*>(selectedVisible())) {
        if (visible->id == monster->id) {
            visible->name = monster->name;
        }
    }
    persist();
}

void MonstersPage::onNameEditingFinished()
{
    const Monster* monster = selectedCustom();
    if (monster == nullptr) {
        return;
    }
    if (isBlank(m_name->text())) {
        m_name->setText(QString::fromStdString(monster->name));
        m_nameError->hide();
        return;
    }
    refreshResults();
    selectId(QString::fromStdString(monster->id));
}

void MonstersPage::onFormEdited()
{
    Monster* monster = selectedCustom();
    if (m_populating || monster == nullptr) {
        return;
    }
    monster->size = m_size->text().toStdString();
    monster->creatureType = m_creatureType->text().toStdString();
    monster->ac = m_ac->value();
    monster->hp = m_hp->value();
    monster->hitDice = m_hitDice->text().toStdString();
    monster->speed = m_speed->text().toStdString();
    monster->initiativeBonus = m_initiative->value();
    monster->passivePerception = m_passivePerception->value();
    monster->challengeRating = m_challengeRating->text().toStdString();
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        monster->abilities.*kAbilities[i].member = m_scores[i]->value();
        m_modifiers[i]->setText(QString::fromStdString(formatModifier(abilityModifier(m_scores[i]->value()))));
    }
    if (Monster* visible = const_cast<Monster*>(selectedVisible())) {
        if (visible->id == monster->id) {
            *visible = *monster;
        }
    }
    persist();
}

void MonstersPage::onTextFinished()
{
    if (m_populating || selectedCustom() == nullptr) {
        return;
    }
    const QString id = currentId();
    rebuildFilters();
    refreshResults();
    selectId(id);
}

void MonstersPage::persist()
{
    if (!m_customError.isEmpty()) {
        return;
    }
    try {
        m_store.saveAll(m_custom);
    } catch (const MonsterDataError& error) {
        QMessageBox::warning(this, tr("Could not save custom monsters"), QString::fromStdString(error.what()));
    }
}

void MonstersPage::selectId(const QString& id)
{
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(Qt::UserRole).toString() == id) {
            m_list->setCurrentRow(row);
            showSelected();
            return;
        }
    }
}

}  // namespace combat::ui
