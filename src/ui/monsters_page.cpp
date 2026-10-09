#include "ui/monsters_page.h"

#include "core/stat_block_reader.h"
#include "ui/effects_dialog.h"

#include "core/combat_rules.h"
#include "core/custom_monster_store.h"
#include "core/text.h"
#include "core/uuid.h"
#include "ui/page_title.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QHideEvent>
#include <QShowEvent>
#include <QPlainTextEdit>
#include <QTimer>

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
#include <QTabWidget>
#include <QFrame>
#include <QVBoxLayout>

#include <QStringList>

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

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
    QString label = QString::fromStdString(monster.name);
    if (!monster.challengeRating.empty()) {
        label += QStringLiteral("    CR ") + QString::fromStdString(monster.challengeRating);
    }
    if (monster.source == kCustomMonsterSource) {
        label += QStringLiteral("    Custom");
    }
    return label;
}

QString scoreText(int score)
{
    return QStringLiteral("%1  (%2)")
        .arg(score)
        .arg(QString::fromStdString(formatModifier(abilityModifier(score))));
}

QString hpText(const Monster& monster)
{
    const QString hp = QString::fromStdString(formatHitPoints(monster.hp, monster.hp));
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

QString joinList(const std::vector<std::string>& list)
{
    QStringList items;
    for (const std::string& item : list) {
        items << QString::fromStdString(item);
    }
    return items.join(QStringLiteral(", "));
}

std::vector<std::string> splitList(const QString& text)
{
    std::vector<std::string> items;
    for (const QString& piece : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const std::string item = piece.trimmed().toLower().toStdString();
        if (!item.empty() && std::find(items.begin(), items.end(), item) == items.end()) {
            items.push_back(item);
        }
    }
    return items;
}

QString savesText(const Monster& monster)
{
    QStringList items;
    for (const Ability ability : kAbilityOrder) {
        const auto& bonus = monster.savingThrows[static_cast<std::size_t>(ability)];
        if (bonus.has_value()) {
            items << QStringLiteral("%1 %2").arg(QString::fromLatin1(abilityShort(ability)),
                                                QString::fromStdString(formatModifier(*bonus)));
        }
    }
    return items.join(QStringLiteral(", "));
}

QString defensesText(const Monster& monster)
{
    QStringList lines;
    if (!monster.defenses.resistances.empty()) {
        lines << QObject::tr("Resistances: %1").arg(joinList(monster.defenses.resistances));
    }
    if (!monster.defenses.immunities.empty()) {
        lines << QObject::tr("Damage immunities: %1").arg(joinList(monster.defenses.immunities));
    }
    if (!monster.defenses.vulnerabilities.empty()) {
        lines << QObject::tr("Vulnerabilities: %1").arg(joinList(monster.defenses.vulnerabilities));
    }
    if (!monster.conditionImmunities.empty()) {
        lines << QObject::tr("Condition immunities: %1").arg(joinList(monster.conditionImmunities));
    }
    return lines.join(QLatin1Char('\n'));
}

// What an ability does to its user, under its text.
void addSelfEffectNote(QVBoxLayout* layout, const std::optional<SelfEffect>& effect)
{
    if (!effect.has_value()) {
        return;
    }
    std::string condition = effect->condition;
    if (!condition.empty()) {
        condition[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(condition[0])));
    }
    auto* note = makeMuted(QString::fromStdString(describeSelfEffect(*effect, condition)));
    layout->addWidget(note);
}

// Who an action can target and what it does to them, as the fight will play it.
void addRuleNotes(QVBoxLayout* layout, const MonsterAttack& attack)
{
    for (const std::string& line : describeActionRules(attack)) {
        auto* note = makeMuted(QString::fromStdString(line));
        note->setWordWrap(true);
        note->setObjectName(QStringLiteral("actionRule"));
        layout->addWidget(note);
    }
}

// The effects summary under an entry in the editor.
QString effectsText(const MonsterAttack* attack, const std::optional<SelfEffect>& self,
                    const std::optional<AuraSave>& aura, const std::optional<AttackModifier>& modifier = std::nullopt)
{
    QStringList lines;
    if (modifier.has_value()) {
        lines << QString::fromStdString(describeAttackModifier(*modifier));
    }
    if (attack != nullptr) {
        for (const std::string& line : describeActionRules(*attack)) {
            lines << QString::fromStdString(line);
        }
    }
    if (self.has_value()) {
        std::string condition = self->condition;
        if (!condition.empty()) {
            condition[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(condition[0])));
        }
        lines << QString::fromStdString(describeSelfEffect(*self, condition));
    }
    if (aura.has_value()) {
        lines << QObject::tr("Aura: DC %1 %2 save at the start of each creature's turn.")
                     .arg(aura->dc)
                     .arg(QString::fromLatin1(abilityLabel(aura->ability)));
    }
    return lines.isEmpty() ? QObject::tr("No effects in a fight beyond its rolls.") : lines.join(QLatin1Char('\n'));
}

QLabel* effectsSummary(const MonsterAttack* attack, const std::optional<SelfEffect>& self,
                       const std::optional<AuraSave>& aura,
                       const std::optional<AttackModifier>& modifier = std::nullopt)
{
    auto* label = makeMuted(effectsText(attack, self, aura, modifier));
    label->setObjectName(QStringLiteral("effectsSummary"));
    label->setWordWrap(true);
    return label;
}

void clearRows(QVBoxLayout* rows)
{
    while (QLayoutItem* item = rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
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
    outer->setContentsMargins(28, 20, 28, 16);
    outer->setSpacing(14);

    // Header: the page and what can be made from it.
    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    header->setObjectName(QStringLiteral("pageHeader"));
    header->addWidget(makePageTitle(tr("Monsters")));
    header->addStretch(1);
    m_deleteButton = new QPushButton(tr("Delete"));
    m_deleteButton->setObjectName(QStringLiteral("deleteMonster"));
    m_deleteButton->setToolTip(tr("Delete this custom monster. SRD monsters cannot be deleted."));
    makeQuiet(m_deleteButton);
    m_copyButton = new QPushButton(tr("Copy as custom"));
    m_copyButton->setObjectName(QStringLiteral("copyMonster"));
    m_copyButton->setToolTip(tr("Make an editable custom copy of this stat block."));
    m_addButton = new QPushButton(tr("New monster"));
    m_addButton->setObjectName(QStringLiteral("newMonster"));
    makePrimary(m_addButton);
    header->addWidget(m_deleteButton);
    header->addWidget(m_copyButton);
    header->addWidget(m_addButton);
    outer->addLayout(header);

    auto addBanner = [outer](const QString& text, const char* role) {
        auto* banner = new QLabel(text);
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setProperty("role", QString::fromLatin1(role));
        outer->addWidget(banner);
    };
    if (!catalogError.isEmpty()) {
        addBanner(tr("The SRD catalog could not be read.\n%1").arg(catalogError), "banner-error");
    }
    if (!m_customError.isEmpty()) {
        addBanner(tr("The custom monster file could not be read, so it has not been changed.\n%1").arg(m_customError),
                  "banner-error");
    }
    const QString skipped = property("skippedMonsters").toString();
    if (!skipped.isEmpty()) {
        addBanner(tr("Some custom monsters were skipped and were not loaded.\n%1").arg(skipped), "banner-note");
    }

    auto* columns = new QHBoxLayout;
    columns->setSpacing(14);
    outer->addLayout(columns, 1);

    // Left: find a monster.
    auto* listCard = makeCard();
    listCard->setFixedWidth(320);
    auto* listLayout = static_cast<QVBoxLayout*>(listCard->layout());
    listLayout->setContentsMargins(12, 12, 8, 8);
    listLayout->setSpacing(8);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("monsterSearch"));
    m_search->setPlaceholderText(tr("Search by name"));
    m_search->setClearButtonEnabled(true);
    listLayout->addWidget(m_search);
    auto* filters = new QHBoxLayout;
    filters->setSpacing(8);
    m_typeFilter = new QComboBox;
    m_typeFilter->setObjectName(QStringLiteral("monsterTypeFilter"));
    m_typeFilter->setMinimumWidth(10);
    m_crFilter = new QComboBox;
    m_crFilter->setObjectName(QStringLiteral("monsterCrFilter"));
    m_crFilter->setMinimumWidth(10);
    filters->addWidget(m_typeFilter, 1);
    filters->addWidget(m_crFilter, 1);
    listLayout->addLayout(filters);
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("monsterList"));
    listLayout->addWidget(m_list, 1);
    columns->addWidget(listCard);

    // Right: the stat block, or the editor for a custom monster.
    auto* detailCard = makeCard();
    auto* rightLayout = static_cast<QVBoxLayout*>(detailCard->layout());
    rightLayout->setContentsMargins(20, 16, 8, 12);
    columns->addWidget(detailCard, 1);

    m_emptyHint = makeMuted(tr("No monsters match. Clear the search or the filters."));
    m_emptyHint->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(m_emptyHint, 1);

    auto* statScroll = new QScrollArea;
    statScroll->setWidgetResizable(true);
    statScroll->setFrameShape(QFrame::NoFrame);
    statScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* statBody = new QWidget;
    statScroll->setWidget(statBody);
    m_stat = statScroll;
    auto* statLayout = new QVBoxLayout(statBody);
    statLayout->setContentsMargins(0, 0, 12, 0);
    statLayout->setSpacing(8);
    m_statName = new QLabel;
    m_statName->setObjectName(QStringLiteral("monsterStatName"));
    m_statName->setWordWrap(true);
    QFont nameFont = m_statName->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.5);
    nameFont.setWeight(QFont::DemiBold);
    m_statName->setFont(nameFont);
    m_statChallenge = new QLabel;
    m_statChallenge->setObjectName(QStringLiteral("monsterStatChallenge"));
    m_statChallenge->setProperty("role", QStringLiteral("pill"));
    m_statChallenge->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* nameRow = new QHBoxLayout;
    nameRow->setSpacing(16);
    nameRow->addWidget(m_statName, 1);
    nameRow->addWidget(m_statChallenge, 0, Qt::AlignRight | Qt::AlignVCenter);
    statLayout->addLayout(nameRow);
    m_statType = makeMuted(QString());
    statLayout->addWidget(m_statType);

    auto* statForm = new QFormLayout;
    statForm->setHorizontalSpacing(20);
    statForm->setVerticalSpacing(6);
    statForm->setContentsMargins(0, 8, 0, 0);
    m_statAc = new QLabel;
    m_statHp = new QLabel;
    m_statSpeed = new QLabel;
    m_statSpeed->setWordWrap(true);
    m_statInitiative = new QLabel;
    m_statPerception = new QLabel;
    statForm->addRow(makeMuted(tr("AC")), m_statAc);
    statForm->addRow(makeMuted(tr("HP")), m_statHp);
    statForm->addRow(makeMuted(tr("Speed")), m_statSpeed);
    statForm->addRow(makeMuted(tr("Initiative")), m_statInitiative);
    statForm->addRow(makeMuted(tr("Passive Perception")), m_statPerception);
    m_statSaves = new QLabel;
    m_statSaves->setObjectName(QStringLiteral("monsterStatSaves"));
    statForm->addRow(makeMuted(tr("Saving throws")), m_statSaves);
    m_statDefenses = new QLabel;
    m_statDefenses->setObjectName(QStringLiteral("monsterStatDefenses"));
    m_statDefenses->setWordWrap(true);
    statForm->addRow(makeMuted(tr("Defenses")), m_statDefenses);
    auto* challengeGap = new QLabel(QStringLiteral(" "));
    challengeGap->setObjectName(QStringLiteral("challengeRatingGap"));
    statForm->addRow(QString(), challengeGap);
    statLayout->addLayout(statForm);

    QFont headingFont = font();
    headingFont.setWeight(QFont::DemiBold);
    // Six scores across, the way a printed stat block shows them.
    auto* abilityStrip = new QFrame;
    abilityStrip->setProperty("role", QStringLiteral("strip"));
    auto* statGrid = new QGridLayout(abilityStrip);
    statGrid->setContentsMargins(12, 8, 12, 8);
    statGrid->setHorizontalSpacing(12);
    statGrid->setVerticalSpacing(2);
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        const int column = static_cast<int>(i);
        auto* label = makeMuted(tr(kAbilities[i].label));
        label->setWordWrap(false);
        label->setAlignment(Qt::AlignCenter);
        statGrid->addWidget(label, 0, column);
        m_statScores[i] = new QLabel;
        m_statScores[i]->setAlignment(Qt::AlignCenter);
        statGrid->addWidget(m_statScores[i], 1, column);
        statGrid->setColumnStretch(column, 1);
    }
    statLayout->addSpacing(4);
    statLayout->addWidget(abilityStrip);
    const QStringList featureHeadings{tr("Traits"), tr("Bonus Actions"), tr("Reactions"), tr("Legendary Actions")};
    const QStringList statFeatureNames{QStringLiteral("monsterTraits"), QStringLiteral("monsterBonusActions"),
                                       QStringLiteral("monsterReactions"), QStringLiteral("monsterLegendaryActions")};
    auto makeFeatureSection = [&](const QString& objectName, const QString& heading, int topMargin) {
        auto* section = new QWidget;
        section->setObjectName(objectName);
        auto* layout = new QVBoxLayout(section);
        layout->setContentsMargins(0, topMargin, 0, 0);
        layout->setSpacing(8);
        auto* title = new QLabel(heading);
        title->setFont(headingFont);
        auto* rows = new QVBoxLayout;
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(8);
        layout->addWidget(title);
        layout->addLayout(rows);
        section->hide();
        return std::pair<QWidget*, QVBoxLayout*>{section, rows};
    };
    for (int i = 0; i < 4; ++i) {
        const auto made = makeFeatureSection(statFeatureNames[i], featureHeadings[i], 8);
        m_statFeatures[static_cast<std::size_t>(i)].section = made.first;
        m_statFeatures[static_cast<std::size_t>(i)].rows = made.second;
    }
    m_statAttacks = new QWidget;
    m_statAttacks->setObjectName(QStringLiteral("monsterAttacks"));
    auto* statAttackLayout = new QVBoxLayout(m_statAttacks);
    statAttackLayout->setContentsMargins(0, 8, 0, 0);
    statAttackLayout->setSpacing(8);
    auto* statAttackHeading = new QLabel(tr("Actions"));
    statAttackHeading->setFont(headingFont);
    m_statAttackRows = new QVBoxLayout;
    m_statAttackRows->setContentsMargins(0, 0, 0, 0);
    m_statAttackRows->setSpacing(8);
    statAttackLayout->addWidget(statAttackHeading);
    statAttackLayout->addLayout(m_statAttackRows);
    m_statAttacks->hide();
    statLayout->addWidget(m_statAttacks);
    // Bonus actions, reactions, and legendary actions, then traits at the bottom.
    for (std::size_t i = 1; i < m_statFeatures.size(); ++i) {
        statLayout->addWidget(m_statFeatures[i].section);
    }
    statLayout->addWidget(m_statFeatures[0].section);
    statLayout->addStretch(1);
    rightLayout->addWidget(m_stat, 1);

    // The editor: three tabs, each scrolling on its own.
    auto* formTabs = new QTabWidget;
    formTabs->setObjectName(QStringLiteral("monsterForm"));
    formTabs->setDocumentMode(true);
    m_form = formTabs;
    auto newTab = [formTabs](const QString& title) {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 14, 12, 8);
        layout->setSpacing(12);
        // Grow the page to fit the editor instead of squeezing the text boxes.
        layout->setSizeConstraint(QLayout::SetMinimumSize);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(page);
        formTabs->addTab(scroll, title);
        return layout;
    };
    QVBoxLayout* formLayout = newTab(tr("Basics"));
    QVBoxLayout* actionsTab = newTab(tr("Actions"));
    QVBoxLayout* traitsTab = newTab(tr("Traits and reactions"));
    auto* basics = new QFormLayout;
    basics->setHorizontalSpacing(16);
    basics->setVerticalSpacing(8);
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("monsterNameField"));
    m_nameError = new QLabel(tr("Name is required."));
    m_nameError->setProperty("role", QStringLiteral("banner-error"));
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
    m_ac->setRange(0, 99);
    basics->addRow(tr("AC"), m_ac);
    m_hp = makeNumberBox();
    m_hp->setRange(1, 9999);
    m_hpSlash = new QLabel;
    auto* hpRow = new QHBoxLayout;
    hpRow->setSpacing(0);
    hpRow->addWidget(m_hp);
    hpRow->addWidget(m_hpSlash);
    hpRow->addStretch(1);
    basics->addRow(tr("HP"), hpRow);
    m_hitDice = new QLineEdit;
    m_hitDice->setPlaceholderText(tr("3d6"));
    basics->addRow(tr("Hit dice"), m_hitDice);
    m_speed = new QLineEdit;
    basics->addRow(tr("Speed"), m_speed);
    m_initiative = makeNumberBox();
    m_initiative->setRange(-20, 40);
    basics->addRow(tr("Initiative bonus"), m_initiative);
    m_passivePerception = makeNumberBox();
    m_passivePerception->setRange(0, 99);
    basics->addRow(tr("Passive Perception"), m_passivePerception);
    m_challengeRating = new QLineEdit;
    m_challengeRating->setPlaceholderText(tr("1/4"));
    basics->addRow(tr("Challenge rating"), m_challengeRating);
    m_xp = new QLineEdit;
    m_xp->setObjectName(QStringLiteral("monsterXp"));
    m_xp->setPlaceholderText(tr("blank uses the challenge rating"));
    basics->addRow(tr("XP"), m_xp);
    m_saveBonuses = new QLineEdit;
    m_saveBonuses->setObjectName(QStringLiteral("monsterSaves"));
    m_saveBonuses->setPlaceholderText(tr("for example, Dex +6, Wis +7"));
    basics->addRow(tr("Saving throws"), m_saveBonuses);
    m_resistances = new QLineEdit;
    m_resistances->setPlaceholderText(tr("for example, fire, cold"));
    basics->addRow(tr("Resistances"), m_resistances);
    m_immunities = new QLineEdit;
    basics->addRow(tr("Damage immunities"), m_immunities);
    m_vulnerabilities = new QLineEdit;
    basics->addRow(tr("Vulnerabilities"), m_vulnerabilities);
    m_conditionImmunities = new QLineEdit;
    m_conditionImmunities->setPlaceholderText(tr("for example, poisoned, charmed"));
    basics->addRow(tr("Condition immunities"), m_conditionImmunities);
    m_legendaryUses = makeNumberBox();
    m_legendaryUses->setRange(0, 9);
    basics->addRow(tr("Legendary action uses"), m_legendaryUses);
    formLayout->addLayout(basics);
    m_formHint = new QLabel;
    m_formHint->setWordWrap(true);
    m_formHint->setProperty("role", QStringLiteral("banner-error"));
    m_formHint->hide();
    formLayout->addWidget(m_formHint);

    formLayout->addWidget(makeHeading(tr("Ability scores")));
    auto* abilityGrid = new QGridLayout;
    abilityGrid->setHorizontalSpacing(16);
    abilityGrid->addWidget(makeMuted(tr("Score")), 0, 1);
    abilityGrid->addWidget(makeMuted(tr("Modifier")), 0, 2);
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        const int row = static_cast<int>(i) + 1;
        abilityGrid->addWidget(new QLabel(tr(kAbilities[i].label)), row, 0);
        m_scores[i] = makeNumberBox();
        m_scores[i]->setRange(1, 30);
        abilityGrid->addWidget(m_scores[i], row, 1);
        m_modifiers[i] = new QLabel;
        m_modifiers[i]->setMinimumWidth(40);
        abilityGrid->addWidget(m_modifiers[i], row, 2);
    }
    abilityGrid->setColumnStretch(3, 1);
    formLayout->addLayout(abilityGrid);
    formLayout->addStretch(1);
    const QStringList formFeatureNames{QStringLiteral("monsterFormTraits"), QStringLiteral("monsterFormBonusActions"),
                                       QStringLiteral("monsterFormReactions"),
                                       QStringLiteral("monsterFormLegendaryActions")};
    for (int i = 0; i < 4; ++i) {
        const auto made = makeFeatureSection(formFeatureNames[i], featureHeadings[i], 0);
        m_formFeatures[static_cast<std::size_t>(i)].section = made.first;
        m_formFeatures[static_cast<std::size_t>(i)].rows = made.second;
        auto* add = new QPushButton(tr("Add entry"));
        add->setObjectName(QStringLiteral("addFeature%1").arg(i));
        makeQuiet(add);
        static_cast<QVBoxLayout*>(made.first->layout())->addWidget(add, 0, Qt::AlignLeft);
        connect(add, &QPushButton::clicked, this, [this, i] {
            Monster* monster = selectedCustom();
            if (monster == nullptr) {
                return;
            }
            std::vector<MonsterFeature>* list[] = {&monster->traits, &monster->bonusActions, &monster->reactions,
                                                   &monster->legendaryActions};
            list[i]->push_back(MonsterFeature{tr("New entry").toStdString(), {}});
            commitCustom();
            rebuildFormFeatures(i);
        });
    }
    traitsTab->addWidget(m_formFeatures[0].section);
    m_formAttacks = new QWidget;
    m_formAttacks->setObjectName(QStringLiteral("monsterFormAttacks"));
    auto* formAttackLayout = new QVBoxLayout(m_formAttacks);
    formAttackLayout->setContentsMargins(0, 0, 0, 0);
    formAttackLayout->setSpacing(8);
    auto* formAttackHeading = makeHeading(tr("Actions"));
    m_formAttackRows = new QVBoxLayout;
    m_formAttackRows->setContentsMargins(0, 0, 0, 0);
    m_formAttackRows->setSpacing(8);
    formAttackLayout->addWidget(formAttackHeading);
    formAttackLayout->addLayout(m_formAttackRows);
    auto* addAttack = new QPushButton(tr("Add action"));
    addAttack->setObjectName(QStringLiteral("addMonsterAction"));
    makeQuiet(addAttack);
    formAttackLayout->addWidget(addAttack, 0, Qt::AlignLeft);
    connect(addAttack, &QPushButton::clicked, this, [this] {
        Monster* monster = selectedCustom();
        if (monster == nullptr) {
            return;
        }
        MonsterAttack attack;
        attack.name = tr("New action").toStdString();
        monster->attacks.push_back(attack);
        commitCustom();
        rebuildFormAttacks();
    });
    actionsTab->addWidget(m_formAttacks);
    actionsTab->addWidget(m_formFeatures[1].section);
    actionsTab->addWidget(m_formFeatures[3].section);
    actionsTab->addStretch(1);
    traitsTab->addWidget(m_formFeatures[2].section);
    traitsTab->addStretch(1);
    rightLayout->addWidget(m_form, 1);

    auto* credit = makeMuted(attribution.isEmpty() ? tr("The SRD attribution file could not be read.") : attribution);
    credit->setObjectName(QStringLiteral("srdAttribution"));
    credit->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont creditFont = credit->font();
    creditFont.setPointSizeF(creditFont.pointSizeF() * 0.85);
    credit->setFont(creditFont);
    outer->addWidget(credit);

    connect(m_list, &QListWidget::currentRowChanged, this, &MonstersPage::showSelected);
    connect(m_addButton, &QPushButton::clicked, this, &MonstersPage::addMonster);
    connect(m_copyButton, &QPushButton::clicked, this, &MonstersPage::copyToCustom);
    for (QLineEdit* field : {m_xp, m_saveBonuses, m_resistances, m_immunities, m_vulnerabilities, m_conditionImmunities}) {
        connect(field, &QLineEdit::textEdited, this, &MonstersPage::onDefenseFieldsEdited);
    }
    connect(m_legendaryUses, &QSpinBox::valueChanged, this, &MonstersPage::onDefenseFieldsEdited);
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, &MonstersPage::saveNow);
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
        m_copyButton->setEnabled(false);
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
    m_copyButton->setEnabled(monster != nullptr && m_customError.isEmpty());
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
        const QString saves = savesText(*monster);
        m_statSaves->setText(saves.isEmpty() ? tr("Ability modifiers") : saves);
        const QString defenses = defensesText(*monster);
        m_statDefenses->setText(defenses.isEmpty() ? tr("None") : defenses);
        if (monster->challengeRating.empty()) {
            m_statChallenge->clear();
        } else {
            m_statChallenge->setText(
                tr("CR %1 (%2 XP)").arg(QString::fromStdString(monster->challengeRating)).arg(monsterXp(*monster)));
        }
        for (std::size_t i = 0; i < kAbilities.size(); ++i) {
            m_statScores[i]->setText(scoreText(monster->abilities.*kAbilities[i].member));
        }
        showAttackList(m_statAttacks, m_statAttackRows, monster->attacks);
        showFeatureList(m_statFeatures[0].section, m_statFeatures[0].rows, monster->traits);
        showFeatureList(m_statFeatures[1].section, m_statFeatures[1].rows, monster->bonusActions);
        showFeatureList(m_statFeatures[2].section, m_statFeatures[2].rows, monster->reactions);
        showFeatureList(m_statFeatures[3].section, m_statFeatures[3].rows, monster->legendaryActions);
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
    m_hpSlash->setText(QStringLiteral(" / %1").arg(monster->hp));
    m_hitDice->setText(QString::fromStdString(monster->hitDice));
    m_speed->setText(QString::fromStdString(monster->speed));
    m_initiative->setValue(monster->initiativeBonus);
    m_passivePerception->setValue(monster->passivePerception);
    m_challengeRating->setText(QString::fromStdString(monster->challengeRating));
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        m_scores[i]->setValue(monster->abilities.*kAbilities[i].member);
        m_modifiers[i]->setText(QString::fromStdString(formatModifier(abilityModifier(m_scores[i]->value()))));
    }
    m_xp->setText(monster->xp.has_value() ? QString::number(*monster->xp) : QString());
    m_saveBonuses->setText(savesText(*monster));
    m_resistances->setText(joinList(monster->defenses.resistances));
    m_immunities->setText(joinList(monster->defenses.immunities));
    m_vulnerabilities->setText(joinList(monster->defenses.vulnerabilities));
    m_conditionImmunities->setText(joinList(monster->conditionImmunities));
    m_legendaryUses->setValue(monster->legendaryActionUses);
    m_formHint->hide();
    m_populating = false;
    rebuildFormAttacks();
    for (int kind = 0; kind < 4; ++kind) {
        rebuildFormFeatures(kind);
    }
}

void MonstersPage::showAttackList(QWidget* section, QVBoxLayout* rows, const std::vector<MonsterAttack>& attacks)
{
    if (section == nullptr || rows == nullptr) {
        return;
    }
    while (QLayoutItem* item = rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    section->setVisible(!attacks.empty());
    for (const MonsterAttack& attack : attacks) {
        auto* block = new QWidget;
        auto* blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(0, 0, 0, 0);
        blockLayout->setSpacing(2);
        QString title = QString::fromStdString(abilityDisplayName(attack.name));
        if (attack.count != 1) {
            title = tr("%1 × %2").arg(title).arg(attack.count);
        }
        auto* name = new QLabel(title);
        name->setObjectName(QStringLiteral("actionTitle"));
        name->setWordWrap(true);
        name->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QFont nameFont = name->font();
        nameFont.setBold(true);
        name->setFont(nameFont);
        auto* effect = new QLabel(QString::fromStdString(attack.effect));
        effect->setWordWrap(true);
        effect->setTextInteractionFlags(Qt::TextSelectableByMouse);
        blockLayout->addWidget(name);
        const std::string summary = attackSummary(attack);
        if (!summary.empty()) {
            auto* rolls = new QLabel(QString::fromStdString(summary));
            rolls->setWordWrap(true);
            rolls->setObjectName(QStringLiteral("actionSummary"));
            rolls->setProperty("role", QStringLiteral("muted"));
            rolls->setProperty("summary", true);
            QFont summaryFont = rolls->font();
            summaryFont.setItalic(true);
            rolls->setFont(summaryFont);
            blockLayout->addWidget(rolls);
        }
        blockLayout->addWidget(effect);
        addSelfEffectNote(blockLayout, attack.selfEffect);
        addRuleNotes(blockLayout, attack);
        rows->addWidget(block);
    }
}

void MonstersPage::showFeatureList(QWidget* section, QVBoxLayout* rows, const std::vector<MonsterFeature>& features)
{
    if (section == nullptr || rows == nullptr) {
        return;
    }
    while (QLayoutItem* item = rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    section->setVisible(!features.empty());
    for (const MonsterFeature& feature : features) {
        auto* block = new QWidget;
        auto* blockLayout = new QVBoxLayout(block);
        blockLayout->setContentsMargins(0, 0, 0, 0);
        blockLayout->setSpacing(2);
        auto* name = new QLabel(QString::fromStdString(abilityDisplayName(feature.name)));
        name->setObjectName(QStringLiteral("actionTitle"));
        name->setWordWrap(true);
        name->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QFont nameFont = name->font();
        nameFont.setBold(true);
        name->setFont(nameFont);
        auto* effect = new QLabel(QString::fromStdString(feature.effect));
        effect->setWordWrap(true);
        effect->setTextInteractionFlags(Qt::TextSelectableByMouse);
        blockLayout->addWidget(name);
        blockLayout->addWidget(effect);
        addSelfEffectNote(blockLayout, feature.selfEffect);
        if (feature.attackModifier.has_value()) {
            auto* note = makeMuted(QString::fromStdString(describeAttackModifier(*feature.attackModifier)));
            note->setWordWrap(true);
            note->setObjectName(QStringLiteral("attackModifierNote"));
            blockLayout->addWidget(note);
        }
        if (feature.targeted.has_value()) {
            addRuleNotes(blockLayout, *feature.targeted);
        }
        if (feature.aura.has_value()) {
            blockLayout->addWidget(makeMuted(tr("Aura: in a fight, the Dashboard asks about this save at the start of "
                                                "each opponent's turn.")));
        }
        rows->addWidget(block);
    }
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
    Monster monster;
    monster.name = tr("New monster").toStdString();
    monster.size = "Medium";
    monster.ac = 10;
    monster.hp = 1;
    monster.speed = "30 ft.";
    monster.passivePerception = 10;
    addCustom(std::move(monster));
}

void MonstersPage::copyToCustom()
{
    const Monster* shown = selectedVisible();
    if (shown == nullptr) {
        return;
    }
    Monster copy = *shown;
    copy.name = tr("%1 (copy)").arg(QString::fromStdString(shown->name)).toStdString();
    addCustom(std::move(copy));
}

void MonstersPage::addCustom(Monster monster)
{
    if (!m_customError.isEmpty()) {
        return;
    }
    monster.id = generateUuidV4([this] { return m_rng(); });
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
    m_catalog.updateCustomMonster(*monster);
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
    m_hpSlash->setText(QStringLiteral(" / %1").arg(monster->hp));
    monster->hitDice = m_hitDice->text().toStdString();
    monster->speed = m_speed->text().toStdString();
    monster->initiativeBonus = m_initiative->value();
    monster->passivePerception = m_passivePerception->value();
    monster->challengeRating = m_challengeRating->text().toStdString();
    for (std::size_t i = 0; i < kAbilities.size(); ++i) {
        monster->abilities.*kAbilities[i].member = m_scores[i]->value();
        m_modifiers[i]->setText(QString::fromStdString(formatModifier(abilityModifier(m_scores[i]->value()))));
    }
    m_catalog.updateCustomMonster(*monster);
    if (QListWidgetItem* item = m_list->currentItem()) {
        item->setText(listLabel(*monster));
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
    m_savePending = true;
    m_saveTimer->start();
}

void MonstersPage::flushPendingSave()
{
    if (m_savePending) {
        m_saveTimer->stop();
        saveNow();
    }
}

void MonstersPage::saveNow()
{
    m_savePending = false;
    if (!m_customError.isEmpty()) {
        return;
    }
    try {
        m_store.saveAll(m_custom);
    } catch (const MonsterDataError& error) {
        QMessageBox::warning(this, tr("Could not save custom monsters"), QString::fromStdString(error.what()));
    }
}

void MonstersPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Rows filled before the style sheet reached the list keep their old
    // height; lay them out again now that it has.
    m_list->doItemsLayout();
}

void MonstersPage::hideEvent(QHideEvent* event)
{
    flushPendingSave();
    QWidget::hideEvent(event);
}

void MonstersPage::commitCustom()
{
    Monster* monster = selectedCustom();
    if (monster == nullptr) {
        return;
    }
    m_catalog.updateCustomMonster(*monster);
    if (Monster* visible = const_cast<Monster*>(selectedVisible())) {
        if (visible->id == monster->id) {
            *visible = *monster;
        }
    }
    persist();
}

void MonstersPage::onDefenseFieldsEdited()
{
    Monster* monster = selectedCustom();
    if (m_populating || monster == nullptr) {
        return;
    }
    QStringList problems;
    const QString xp = m_xp->text().trimmed();
    if (xp.isEmpty()) {
        monster->xp.reset();
    } else {
        bool ok = false;
        const int value = xp.toInt(&ok);
        if (ok && value >= 0) {
            monster->xp = value;
        } else {
            problems << tr("XP must be a whole number.");
        }
    }
    std::array<std::optional<int>, 6> saves{};
    for (const QString& piece : m_saveBonuses->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList words = piece.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        bool ok = false;
        const auto ability = words.size() == 2 ? abilityFromKey(words[0].toStdString()) : std::nullopt;
        const int bonus = words.size() == 2 ? words[1].toInt(&ok) : 0;
        if (!ability.has_value() || !ok) {
            problems << tr("Write saving throws as \"Dex +6, Wis +7\".");
            break;
        }
        saves[static_cast<std::size_t>(*ability)] = bonus;
    }
    monster->savingThrows = saves;
    const auto types = [&problems](const QString& text) {
        std::vector<std::string> list = splitList(text);
        for (const std::string& type : list) {
            if (!isDamageType(type)) {
                problems << tr("\"%1\" is not a damage type.").arg(QString::fromStdString(type));
            }
        }
        list.erase(std::remove_if(list.begin(), list.end(), [](const std::string& type) { return !isDamageType(type); }),
                   list.end());
        return list;
    };
    monster->defenses.resistances = types(m_resistances->text());
    monster->defenses.immunities = types(m_immunities->text());
    monster->defenses.vulnerabilities = types(m_vulnerabilities->text());
    monster->conditionImmunities = splitList(m_conditionImmunities->text());
    monster->legendaryActionUses = m_legendaryUses->value();
    m_formHint->setText(problems.join(QLatin1Char('\n')));
    m_formHint->setVisible(!problems.isEmpty());
    commitCustom();
}

void MonstersPage::rebuildFormAttacks()
{
    clearRows(m_formAttackRows);
    Monster* monster = selectedCustom();
    m_formAttacks->setVisible(monster != nullptr);
    if (monster == nullptr) {
        return;
    }
    for (int i = 0; i < static_cast<int>(monster->attacks.size()); ++i) {
        const MonsterAttack& attack = monster->attacks[static_cast<std::size_t>(i)];
        auto* block = new QFrame;
        block->setFrameShape(QFrame::StyledPanel);
        auto* grid = new QGridLayout(block);
        auto* name = new QLineEdit(QString::fromStdString(attack.name));
        name->setPlaceholderText(tr("Name (Multiattack for the Multiattack action)"));
        auto* count = new QSpinBox;
        count->setRange(1, 20);
        count->setValue(attack.count);
        count->setToolTip(tr("On Multiattack: how many attacks it makes. Elsewhere: how many Multiattack grants."));
        auto* hasBonus = new QCheckBox(tr("Attack roll"));
        hasBonus->setChecked(attack.attackBonus.has_value());
        auto* bonus = new QSpinBox;
        bonus->setRange(-10, 30);
        bonus->setPrefix(QStringLiteral("+"));
        bonus->setValue(attack.attackBonus.value_or(0));
        auto* saveAbility = new QComboBox;
        saveAbility->addItem(tr("No save"));
        for (const Ability ability : kAbilityOrder) {
            saveAbility->addItem(QString::fromLatin1(abilityLabel(ability)));
        }
        saveAbility->setCurrentIndex(attack.save.has_value() ? static_cast<int>(attack.save->ability) + 1 : 0);
        auto* dc = new QSpinBox;
        dc->setRange(1, 40);
        dc->setPrefix(tr("DC "));
        dc->setValue(attack.save.has_value() ? attack.save->dc : 10);
        auto* half = new QCheckBox(tr("Half on success"));
        half->setChecked(attack.save.has_value() && attack.save->halfOnSuccess);
        auto* damage = new QLineEdit(QString::fromStdString(formatDamageParts(attack.damage)));
        damage->setPlaceholderText(tr("1d6+2 slashing, 1d4 slashing advantage"));
        auto* readDamage = new QPushButton(tr("Read from text"));
        readDamage->setObjectName(QStringLiteral("readEntryText"));
        readDamage->setToolTip(tr("Fill the roll, save, damage, limits, and effects from the text below."));
        auto* effects = new QPushButton(tr("Effects…"));
        effects->setObjectName(QStringLiteral("editEffects"));
        effects->setToolTip(tr("Who it can target, the conditions it gives, and what it does to the monster."));
        QLabel* summary = effectsSummary(&attack, attack.selfEffect, std::nullopt);
        auto* recharge = new QSpinBox;
        recharge->setRange(0, 6);
        recharge->setSpecialValueText(tr("No recharge"));
        recharge->setPrefix(tr("Recharge "));
        recharge->setValue(attack.recharge.value_or(0));
        auto* perDay = new QSpinBox;
        perDay->setRange(0, 9);
        perDay->setSpecialValueText(tr("No daily limit"));
        perDay->setSuffix(tr("/Day"));
        perDay->setValue(attack.perDay.value_or(0));
        auto* area = new QCheckBox(tr("Area"));
        area->setChecked(attack.area);
        auto* inMulti = new QCheckBox(tr("Part of Multiattack"));
        inMulti->setChecked(attack.inMultiattack);
        auto* effect = new QPlainTextEdit(QString::fromStdString(attack.effect));
        effect->setMinimumHeight(56);
        effect->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        effect->setMaximumHeight(80);
        auto* problem = new QLabel;
        problem->setStyleSheet(QStringLiteral("QLabel { color: #b00020; }"));
        problem->setWordWrap(true);
        problem->hide();
        auto* remove = new QPushButton(tr("Remove"));
        makeQuiet(remove);
        grid->addWidget(name, 0, 0, 1, 3);
        grid->addWidget(count, 0, 3);
        grid->addWidget(remove, 0, 4);
        grid->addWidget(hasBonus, 1, 0);
        grid->addWidget(bonus, 1, 1);
        grid->addWidget(saveAbility, 1, 2);
        grid->addWidget(dc, 1, 3);
        grid->addWidget(half, 1, 4);
        grid->addWidget(damage, 2, 0, 1, 4);
        grid->addWidget(readDamage, 2, 4);
        grid->addWidget(recharge, 3, 0);
        grid->addWidget(perDay, 3, 1);
        grid->addWidget(area, 3, 2);
        grid->addWidget(inMulti, 3, 3, 1, 2);
        grid->addWidget(effect, 4, 0, 1, 5);
        grid->addWidget(summary, 5, 0, 1, 4);
        grid->addWidget(effects, 5, 4, Qt::AlignTop);
        grid->addWidget(problem, 6, 0, 1, 5);
        m_formAttackRows->addWidget(block);

        auto apply = [this, i, name, count, hasBonus, bonus, saveAbility, dc, half, damage, recharge, perDay, area,
                      inMulti, effect, problem, summary] {
            Monster* owner = selectedCustom();
            if (m_populating || owner == nullptr || i >= static_cast<int>(owner->attacks.size())) {
                return;
            }
            MonsterAttack& row = owner->attacks[static_cast<std::size_t>(i)];
            if (!isBlank(name->text())) {
                row.name = name->text().trimmed().toStdString();
            }
            row.count = count->value();
            row.attackBonus = hasBonus->isChecked() ? std::optional<int>(bonus->value()) : std::nullopt;
            bonus->setEnabled(hasBonus->isChecked());
            if (saveAbility->currentIndex() > 0) {
                row.save = SaveSpec{kAbilityOrder[static_cast<std::size_t>(saveAbility->currentIndex() - 1)],
                                    dc->value(), half->isChecked()};
            } else {
                row.save.reset();
            }
            dc->setEnabled(saveAbility->currentIndex() > 0);
            half->setEnabled(saveAbility->currentIndex() > 0);
            std::string error;
            const auto parts = parseDamageParts(damage->text().toStdString(), &error);
            if (parts.has_value()) {
                row.damage = *parts;
                problem->hide();
            } else {
                problem->setText(QString::fromStdString(error));
                problem->show();
            }
            row.recharge = recharge->value() > 0 ? std::optional<int>(recharge->value()) : std::nullopt;
            row.perDay = perDay->value() > 0 ? std::optional<int>(perDay->value()) : std::nullopt;
            row.area = area->isChecked();
            row.inMultiattack = inMulti->isChecked();
            const std::string text = effect->toPlainText().toStdString();
            if (text != row.effect) {
                // Effects follow the text until they are edited by hand.
                if (effectsMatch(row, readEntry(row.name, row.effect))) {
                    applyEffects(row, readEntry(row.name, text));
                    summary->setText(effectsText(&row, row.selfEffect, std::nullopt));
                }
                row.effect = text;
            }
            commitCustom();
        };
        connect(name, &QLineEdit::textEdited, this, apply);
        connect(count, &QSpinBox::valueChanged, this, apply);
        connect(hasBonus, &QCheckBox::toggled, this, apply);
        connect(bonus, &QSpinBox::valueChanged, this, apply);
        connect(saveAbility, &QComboBox::currentIndexChanged, this, apply);
        connect(dc, &QSpinBox::valueChanged, this, apply);
        connect(half, &QCheckBox::toggled, this, apply);
        connect(damage, &QLineEdit::textEdited, this, apply);
        connect(recharge, &QSpinBox::valueChanged, this, apply);
        connect(perDay, &QSpinBox::valueChanged, this, apply);
        connect(area, &QCheckBox::toggled, this, apply);
        connect(inMulti, &QCheckBox::toggled, this, apply);
        connect(effect, &QPlainTextEdit::textChanged, this, apply);
        connect(readDamage, &QPushButton::clicked, this, [this, i, name, effect] {
            Monster* owner = selectedCustom();
            if (owner == nullptr || i >= static_cast<int>(owner->attacks.size())) {
                return;
            }
            MonsterAttack& row = owner->attacks[static_cast<std::size_t>(i)];
            row.effect = effect->toPlainText().toStdString();
            if (!isBlank(name->text())) {
                row.name = name->text().trimmed().toStdString();
            }
            EntryReading reading = readEntry(row.name, row.effect);
            if (reading.damage.empty()) {
                // Untyped damage from the text, as before.
                MonsterAttack probe;
                probe.effect = row.effect;
                reading.damage = attackDamageParts(probe);
            }
            applyReading(row, reading);
            commitCustom();
            QTimer::singleShot(0, this, [this] { rebuildFormAttacks(); });
        });
        connect(effects, &QPushButton::clicked, this, [this, i] {
            Monster* owner = selectedCustom();
            if (owner == nullptr || i >= static_cast<int>(owner->attacks.size())) {
                return;
            }
            EffectsDialog dialog(owner->attacks[static_cast<std::size_t>(i)], this);
            if (dialog.exec() != QDialog::Accepted) {
                return;
            }
            owner = selectedCustom();
            if (owner == nullptr || i >= static_cast<int>(owner->attacks.size())) {
                return;
            }
            owner->attacks[static_cast<std::size_t>(i)] = dialog.attack();
            commitCustom();
            QTimer::singleShot(0, this, [this] { rebuildFormAttacks(); });
        });
        connect(remove, &QPushButton::clicked, this, [this, i] {
            Monster* owner = selectedCustom();
            if (owner == nullptr || i >= static_cast<int>(owner->attacks.size())) {
                return;
            }
            owner->attacks.erase(owner->attacks.begin() + i);
            commitCustom();
            QTimer::singleShot(0, this, [this] { rebuildFormAttacks(); });
        });
        bonus->setEnabled(hasBonus->isChecked());
        dc->setEnabled(saveAbility->currentIndex() > 0);
        half->setEnabled(saveAbility->currentIndex() > 0);
    }
}

void MonstersPage::rebuildFormFeatures(int kind)
{
    FeatureSection& section = m_formFeatures[static_cast<std::size_t>(kind)];
    clearRows(section.rows);
    Monster* monster = selectedCustom();
    section.section->setVisible(monster != nullptr);
    if (monster == nullptr) {
        return;
    }
    std::vector<MonsterFeature>* lists[] = {&monster->traits, &monster->bonusActions, &monster->reactions,
                                            &monster->legendaryActions};
    const std::vector<MonsterFeature>& features = *lists[kind];
    for (int i = 0; i < static_cast<int>(features.size()); ++i) {
        auto* block = new QWidget;
        auto* layout = new QVBoxLayout(block);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* top = new QHBoxLayout;
        auto* name = new QLineEdit(QString::fromStdString(features[static_cast<std::size_t>(i)].name));
        auto* remove = new QPushButton(tr("Remove"));
        makeQuiet(remove);
        top->addWidget(name, 1);
        // Traits, bonus actions, and reactions can recharge or be limited per day.
        QSpinBox* recharge = nullptr;
        QSpinBox* perDay = nullptr;
        if (kind == 0 || kind == 1 || kind == 2) {
            const MonsterFeature& feature = features[static_cast<std::size_t>(i)];
            recharge = new QSpinBox;
            recharge->setObjectName(QStringLiteral("featureRecharge"));
            recharge->setRange(0, 6);
            recharge->setSpecialValueText(tr("No recharge"));
            recharge->setPrefix(tr("Recharge "));
            recharge->setValue(feature.recharge.value_or(0));
            recharge->setToolTip(tr("Lowest d6 roll that recharges it at the start of the monster's turn."));
            perDay = new QSpinBox;
            perDay->setObjectName(QStringLiteral("featurePerDay"));
            perDay->setRange(0, 9);
            perDay->setSpecialValueText(tr("No daily limit"));
            perDay->setSuffix(tr("/Day"));
            perDay->setValue(feature.perDay.value_or(0));
            top->addWidget(recharge);
            top->addWidget(perDay);
        }
        auto* effects = new QPushButton(tr("Effects…"));
        effects->setObjectName(QStringLiteral("editEffects"));
        top->addWidget(effects);
        top->addWidget(remove);
        auto* effect = new QPlainTextEdit(QString::fromStdString(features[static_cast<std::size_t>(i)].effect));
        effect->setMinimumHeight(48);
        effect->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        effect->setMaximumHeight(72);
        layout->addLayout(top);
        layout->addWidget(effect);
        {
            const MonsterFeature& feature = features[static_cast<std::size_t>(i)];
            layout->addWidget(effectsSummary(feature.targeted.has_value() ? &*feature.targeted : nullptr,
                                             feature.selfEffect, feature.aura, feature.attackModifier));
        }
        section.rows->addWidget(block);
        connect(effects, &QPushButton::clicked, this, [this, kind, i] {
            Monster* owner = selectedCustom();
            if (owner == nullptr) {
                return;
            }
            std::vector<MonsterFeature>* ownerLists[] = {&owner->traits, &owner->bonusActions, &owner->reactions,
                                                         &owner->legendaryActions};
            if (i >= static_cast<int>(ownerLists[kind]->size())) {
                return;
            }
            EffectsDialog dialog((*ownerLists[kind])[static_cast<std::size_t>(i)], kind == 0, this);
            if (dialog.exec() != QDialog::Accepted) {
                return;
            }
            owner = selectedCustom();
            if (owner == nullptr) {
                return;
            }
            std::vector<MonsterFeature>* lists2[] = {&owner->traits, &owner->bonusActions, &owner->reactions,
                                                     &owner->legendaryActions};
            if (i >= static_cast<int>(lists2[kind]->size())) {
                return;
            }
            (*lists2[kind])[static_cast<std::size_t>(i)] = dialog.feature();
            commitCustom();
            QTimer::singleShot(0, this, [this, kind] { rebuildFormFeatures(kind); });
        });
        auto apply = [this, kind, i, name, effect, recharge, perDay] {
            Monster* owner = selectedCustom();
            if (m_populating || owner == nullptr) {
                return;
            }
            std::vector<MonsterFeature>* ownerLists[] = {&owner->traits, &owner->bonusActions, &owner->reactions,
                                                         &owner->legendaryActions};
            std::vector<MonsterFeature>& list = *ownerLists[kind];
            if (i >= static_cast<int>(list.size())) {
                return;
            }
            MonsterFeature& row = list[static_cast<std::size_t>(i)];
            const std::string oldName = row.name;
            const std::string oldEffect = row.effect;
            if (!isBlank(name->text())) {
                row.name = name->text().trimmed().toStdString();
            }
            row.effect = effect->toPlainText().toStdString();
            // A bonus action, reaction, or legendary action with a save or an
            // attack roll is aimed like an action, read from its text, until
            // its effects are edited by hand.
            // A trait's Advantage or Disadvantage on attack rolls follows its
            // text the same way.
            if (kind == 0 && (row.effect != oldEffect || row.name != oldName) &&
                row.attackModifier == readAttackModifier(oldName, oldEffect)) {
                row.attackModifier = readAttackModifier(row.name, row.effect);
            }
            if (kind != 0 && row.effect != oldEffect &&
                row.afterDamagingBloodied == readsAfterDamagingBloodied(oldEffect)) {
                row.afterDamagingBloodied = readsAfterDamagingBloodied(row.effect);
            }
            if (kind != 0 && row.effect != oldEffect) {
                const std::optional<MonsterAttack> before = targetedFromText(oldName, oldEffect);
                const bool untouched =
                    (!row.targeted.has_value() && !before.has_value()) ||
                    (row.targeted.has_value() && before.has_value() && *row.targeted == [&] {
                        MonsterAttack copy = *before;
                        copy.name = row.targeted->name;
                        copy.effect = row.targeted->effect;
                        return copy;
                    }());
                if (untouched) {
                    row.targeted = targetedFromText(row.name, row.effect);
                }
            }
            if (row.targeted.has_value()) {
                row.targeted->name = row.name;
                row.targeted->effect = row.effect;
            }
            if (recharge != nullptr) {
                list[static_cast<std::size_t>(i)].recharge =
                    recharge->value() > 0 ? std::optional<int>(recharge->value()) : std::nullopt;
            }
            if (perDay != nullptr) {
                list[static_cast<std::size_t>(i)].perDay =
                    perDay->value() > 0 ? std::optional<int>(perDay->value()) : std::nullopt;
            }
            commitCustom();
        };
        connect(name, &QLineEdit::textEdited, this, apply);
        connect(effect, &QPlainTextEdit::textChanged, this, apply);
        if (recharge != nullptr) {
            connect(recharge, &QSpinBox::valueChanged, this, apply);
            connect(perDay, &QSpinBox::valueChanged, this, apply);
        }
        connect(remove, &QPushButton::clicked, this, [this, kind, i] {
            Monster* owner = selectedCustom();
            if (owner == nullptr) {
                return;
            }
            std::vector<MonsterFeature>* ownerLists[] = {&owner->traits, &owner->bonusActions, &owner->reactions,
                                                         &owner->legendaryActions};
            std::vector<MonsterFeature>& list = *ownerLists[kind];
            if (i >= static_cast<int>(list.size())) {
                return;
            }
            list.erase(list.begin() + i);
            commitCustom();
            QTimer::singleShot(0, this, [this, kind] { rebuildFormFeatures(kind); });
        });
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
