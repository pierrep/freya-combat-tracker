// Offscreen tests that drive the real pages. Run with QT_QPA_PLATFORM=offscreen.

#include "core/combat_rules.h"
#include "core/monster_catalog.h"
#include "data/json_character_store.h"
#include "data/json_encounters.h"
#include "data/json_monsters.h"
#include "data/json_sheet.h"
#include "test_harness.h"
#include "ui/combat_page.h"
#include "ui/dice_overlay.h"
#include "ui/main_window.h"
#include "ui/monsters_page.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QThread>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>

using namespace combat;
namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir()
    {
        std::random_device rd;
        m_path = fs::temp_directory_path() / ("freya-ui-" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(m_path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

const fs::path kSrd = COMBAT_TRACKER_SRD_DIR;

Monster srd(const std::vector<Monster>& monsters, const std::string& id)
{
    for (const Monster& monster : monsters) {
        if (monster.id == id) {
            return monster;
        }
    }
    throw test::Failure("no SRD monster " + id);
}

// Everything the main window needs, backed by files in a temporary folder.
struct App {
    TempDir dir;
    JsonCharacterStore characters{dir.path() / "characters.json"};
    JsonEncounterStore encounters{dir.path() / "encounters.json"};
    std::vector<Monster> srdMonsters = loadSrdMonsters(kSrd / "monsters.json");
    MergedMonsterCatalog catalog{srdMonsters};
    JsonCustomMonsterStore custom{dir.path() / "custom-monsters.json", catalog.srdIds()};
    std::vector<Spell> spells = loadSpellCatalog(kSrd / "spells.json");
    std::vector<Condition> conditions = loadConditionCatalog(kSrd / "conditions.json");
    std::vector<std::string> species = loadSpeciesCatalog(kSrd / "species.json");
    std::unique_ptr<ui::MainWindow> window;

    void open()
    {
        window = std::make_unique<ui::MainWindow>(characters, catalog, custom, encounters, spells, conditions, species,
                                                  QStringLiteral("SRD"), QString(), QString());
        window->resize(1200, 900);
        window->show();
        QApplication::processEvents();
    }

    template <class T>
    T* find(const char* name)
    {
        T* widget = window->findChild<T*>(QString::fromLatin1(name));
        if (widget == nullptr) {
            throw test::Failure(std::string("no widget ") + name);
        }
        return widget;
    }

    // Answers the first open check at the top of the Dashboard with this
    // button ("promptRoll", "promptPassed", "promptFailed", ...).
    void answer(const char* button)
    {
        QApplication::processEvents();
        find<QPushButton>(button)->click();
        QApplication::processEvents();
    }

    QPushButton* button(const QString& text)
    {
        for (QPushButton* candidate : window->findChildren<QPushButton*>()) {
            if (candidate->text() == text && candidate->isVisible()) {
                return candidate;
            }
        }
        throw test::Failure("no visible button " + text.toStdString());
    }

    QTreeWidgetItem* row(const char* list, const std::string& combatantId)
    {
        QTreeWidget* tree = find<QTreeWidget>(list);
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString().toStdString() == combatantId) {
                return tree->topLevelItem(i);
            }
        }
        return nullptr;
    }

    void select(const std::string& combatantId)
    {
        for (const char* list : {"initiativeList", "zeroHpList"}) {
            if (QTreeWidgetItem* item = row(list, combatantId)) {
                find<QTreeWidget>(list)->setCurrentItem(item);
                QApplication::processEvents();
                return;
            }
        }
        throw test::Failure("no row for " + combatantId);
    }

    void clickTarget(const std::string& combatantId)
    {
        for (const char* list : {"initiativeList", "zeroHpList"}) {
            if (QTreeWidgetItem* item = row(list, combatantId)) {
                // As a mouse click does: the row becomes current, then the click.
                find<QTreeWidget>(list)->setCurrentItem(item);
                emit find<QTreeWidget>(list)->itemClicked(item, 1);
                QApplication::processEvents();
                return;
            }
        }
        throw test::Failure("no row for " + combatantId);
    }

    Encounter saved()
    {
        window->findChild<QWidget*>()->setFocus();
        return encounters.loadAll().at(0);
    }

    static const Combatant& in(const Encounter& encounter, const std::string& id)
    {
        for (const Combatant& combatant : encounter.combatants) {
            if (combatant.id == id) {
                return combatant;
            }
        }
        throw test::Failure("missing combatant " + id);
    }
};

// Answers the next message box with this button.
void answerNextBox(QMessageBox::StandardButton answer)
{
    QTimer::singleShot(0, [answer] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            box->button(answer)->click();
        }
    });
}

Character fighter()
{
    Character character;
    character.id = "aria-sheet";
    character.name = "Aria";
    character.hp = {12, 12};
    character.ac = 12;
    character.abilities.constitution = 14;
    character.classes.push_back(ClassLevel{"Fighter", 1, "", 0});
    character.savingThrows.constitution = true;
    return character;
}

// The aimed button ("Targets…" on a bonus action, reaction, or legendary
// action) on the row whose title starts with this name.
QPushButton* featureButton(App& app, const QString& name)
{
    for (QPushButton* button : app.window->findChildren<QPushButton*>(QStringLiteral("featureTarget"))) {
        for (QLabel* label : button->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (button->isVisible() && label->property("featureName").toString().startsWith(name)) {
                return button;
            }
        }
    }
    return nullptr;
}

ActiveCondition plain(const std::string& id)
{
    ActiveCondition condition;
    condition.id = id;
    return condition;
}


}  // namespace

TEST_CASE("a dragon's Multiattack spends one action for three Rends, then the turn passes")
{
    App app;
    Character aria = fighter();
    aria.hp = {400, 400};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Lair";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "adult-red-dragon"), "dragon"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 5;
    app.encounters.saveAll({encounter});
    app.open();

    app.select("dragon");
    app.button(QStringLiteral("Multiattack"))->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "dragon").economy.attacksRemaining, 3);
    // Fire Breath cannot be one of the three.
    bool breathEnabled = true;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
        if (candidate->isVisible() && candidate->text() == QStringLiteral("Targets…")) {
            breathEnabled = candidate->isEnabled();
        }
    }
    CHECK(!breathEnabled);

    for (int attack = 0; attack < 3; ++attack) {
        app.select("dragon");
        app.button(QStringLiteral("Attack"))->click();
        QApplication::processEvents();
        app.clickTarget("aria");
    }
    const Encounter after = app.saved();
    CHECK_EQ(after.turnIndex, 1);  // passed to Aria
    CHECK_EQ(App::in(after, "dragon").economy.attacksRemaining, 0);
    // Aria is still in the turn order whether she was hit or not.
    CHECK(app.row("initiativeList", "aria") != nullptr);
    CHECK(app.find<QListWidget>("fightLog")->count() >= 4);

    // One undo step per change: undo the turn pass and the last attack.
    app.button(QStringLiteral("Undo"))->click();
    QApplication::processEvents();
    CHECK_EQ(app.saved().turnIndex, 0);
}

TEST_CASE("Cloud of Insects imposes Disadvantage on the concentration save its damage causes")
{
    App app;
    Character aria = fighter();
    aria.hp = {400, 400};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Lair";
    encounter.started = true;
    encounter.turnIndex = 1;  // Aria's turn, so the dragon can take a legendary action
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "adult-black-dragon"), "dragon"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    encounter.combatants[1].concentration = "bless";
    app.encounters.saveAll({encounter});
    app.open();

    app.select("dragon");
    QPushButton* cloud = featureButton(app, QStringLiteral("Cloud of Insects"));
    CHECK(cloud != nullptr);
    CHECK(cloud->isEnabled());
    cloud->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    CHECK(app.find<QLabel>("promptText")->text().contains(QStringLiteral("Disadvantage on concentration saves")));
    app.answer("promptFailed");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    const Combatant after = App::in(app.saved(), "aria");
    CHECK(hasConcentrationDisadvantage(after));
    CHECK_EQ(after.concentration, std::string("bless"));
    CHECK(after.hp < 400);
    const QString prompt = app.find<QLabel>("promptText")->text();
    CHECK(prompt.contains(QStringLiteral("with Disadvantage")));
    CHECK(prompt.contains(QStringLiteral("Constitution")));

    // The save the damage causes is rolled at Disadvantage, not one die.
    app.answer("promptRoll");
    bool rolledAtDisadvantage = false;
    auto* log = app.find<QListWidget>("fightLog");
    for (int i = 0; i < log->count(); ++i) {
        if (log->item(i)->text().contains(QStringLiteral(", disadvantage"))) {
            rolledAtDisadvantage = true;
        }
    }
    CHECK(rolledAtDisadvantage);
    CHECK(hasConcentrationDisadvantage(App::in(app.saved(), "aria")));
}

TEST_CASE("a dying character keeps a turn, death saves are prompted, and healing brings them back")
{
    App app;
    Character aria = fighter();
    aria.hp.current = 3;
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 10;
    app.encounters.saveAll({encounter});
    app.open();

    app.select("aria");
    app.find<QSpinBox>("damageAmount")->setValue(5);
    app.find<QPushButton>("applyDamage")->click();
    QApplication::processEvents();
    Encounter fight = app.saved();
    CHECK_EQ(App::in(fight, "aria").hp, 0);
    CHECK(isDying(App::in(fight, "aria")));
    CHECK(app.row("initiativeList", "aria") != nullptr);
    const int sheetHpAtZero = app.characters.loadAll()[0].hp.current;
    CHECK_EQ(sheetHpAtZero, 0);

    app.find<QPushButton>("nextTurn")->click();  // goblin -> aria
    QApplication::processEvents();
    CHECK_EQ(app.saved().turnIndex, 1);
    CHECK(app.find<QWidget>("promptPanel")->isVisible());
    // The creature's name is bold in the prompt.
    CHECK(app.find<QLabel>("promptText")->text().contains(QStringLiteral("<b>Aria</b> is dying")));

    app.select("aria");
    app.find<QSpinBox>("damageAmount")->setValue(2);
    app.find<QPushButton>("applyDamage")->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "aria").deathSaves.failures, 1);

    app.find<QSpinBox>("healAmount")->setValue(4);
    app.find<QPushButton>("applyHealing")->click();
    QApplication::processEvents();
    fight = app.saved();
    CHECK_EQ(App::in(fight, "aria").hp, 4);
    CHECK_EQ(App::in(fight, "aria").deathSaves.failures, 0);
    CHECK(!hasCondition(App::in(fight, "aria"), "unconscious"));
    const int sheetHpHealed = app.characters.loadAll()[0].hp.current;
    CHECK_EQ(sheetHpHealed, 4);
}

TEST_CASE("the death save prompt is the only Roll, and resolving it ends the dying character's turn")
{
    App app;
    Character aria = fighter();
    aria.hp.current = 0;
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 10;
    encounter.combatants[1].conditions.push_back(ActiveCondition{"unconscious", std::nullopt, std::nullopt});
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChild<ui::CombatPage*>()->setAutoPass(false);

    app.find<QPushButton>("nextTurn")->click();  // goblin -> aria: death save prompt
    QApplication::processEvents();
    CHECK_EQ(app.saved().turnIndex, 1);
    app.select("aria");
    CHECK(app.window->findChild<QPushButton*>(QStringLiteral("rollDeathSave")) == nullptr);
    CHECK(app.find<QPushButton>("deathSuccessUp")->isVisible());
    CHECK(app.find<QWidget>("deathSaveButtons")->isVisible());

    app.find<QPushButton>("promptFailed")->click();
    QApplication::processEvents();
    Encounter fight = app.saved();
    CHECK_EQ(App::in(fight, "aria").deathSaves.failures, 1);
    CHECK_EQ(fight.turnIndex, 0);  // back to the goblin
    CHECK_EQ(fight.round, 2);

    // One Undo takes back the save and the end of the turn together.
    app.find<QPushButton>("undoFight")->click();
    QApplication::processEvents();
    fight = app.saved();
    CHECK_EQ(App::in(fight, "aria").deathSaves.failures, 0);
    CHECK_EQ(fight.turnIndex, 1);
    CHECK_EQ(fight.round, 1);
}

TEST_CASE("a character stable at 0 HP shows only that, without death save buttons or Stabilize")
{
    App app;
    Character aria = fighter();
    aria.hp.current = 0;
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[1].stable = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.select("aria");
    const auto stabilizeShown = [&app] {
        for (QPushButton* button : app.window->findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("Stabilize") && button->isVisible()) {
                return true;
            }
        }
        return false;
    };
    CHECK(app.find<QLabel>("deathStatus")->isVisible());
    CHECK(app.find<QLabel>("deathStatus")->text() == QStringLiteral("Stable at 0 HP"));
    CHECK(!app.find<QWidget>("deathSaveButtons")->isVisible());
    CHECK(!stabilizeShown());
}

TEST_CASE("a hit on a stable character at 0 HP is a death save failure (two for a critical), and it is dying again")
{
    App app;
    Character aria = fighter();
    aria.hp = {0, 60};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 10;
    encounter.combatants[1].stable = true;
    encounter.combatants[1].conditions = {plain("unconscious"), plain("poisoned"), plain("paralyzed")};
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChildren<ui::CombatPage*>().front()->setAutoPass(false);
    app.select("goblin");
    // Attack until one lands (Unconscious: Advantage; within 5 feet: a critical).
    Combatant hit;
    for (int tries = 0; tries < 40; ++tries) {
        app.button(QStringLiteral("Attack"))->click();
        QApplication::processEvents();
        app.clickTarget("aria");
        hit = App::in(app.saved(), "aria");
        if (hit.deathSaves.failures > 0 || hit.dead) {
            break;
        }
        app.find<QPushButton>("undoFight")->click();
        QApplication::processEvents();
    }
    CHECK(!hit.dead);
    CHECK(!hit.stable);
    CHECK_EQ(hit.deathSaves.failures, 2);
    CHECK(isDying(hit));
}

TEST_CASE("the Phase Spider's bite makes a character Stable only when it drops them to 0, not when they are already down")
{
    App app;
    Character aria = fighter();
    aria.hp = {0, 200};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Web";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "phase-spider"), "spider"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 10;
    encounter.combatants[1].stable = true;
    encounter.combatants[1].conditions = {plain("unconscious"), plain("poisoned"), plain("paralyzed")};
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChildren<ui::CombatPage*>().front()->setAutoPass(false);
    app.select("spider");
    Combatant hit;
    for (int tries = 0; tries < 40; ++tries) {
        app.button(QStringLiteral("Attack"))->click();
        QApplication::processEvents();
        app.clickTarget("aria");
        hit = App::in(app.saved(), "aria");
        if (hit.deathSaves.failures > 0 || hit.dead) {
            break;
        }
        app.find<QPushButton>("undoFight")->click();
        QApplication::processEvents();
    }
    CHECK(!hit.stable);
    CHECK_EQ(hit.deathSaves.failures, 2);
}

TEST_CASE("an action shows a tick box only for the \"or\" or extra damage the GM must judge, named for it")
{
    App app;
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Woods";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "boar"), "boar"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "mimic"), "mimic"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    app.encounters.saveAll({encounter});
    app.open();
    auto choices = [&app]() {
        QStringList texts;
        for (QCheckBox* box : app.window->findChildren<QCheckBox*>(QStringLiteral("damageChoice"))) {
            if (box->isVisible()) {
                texts << box->accessibleName();
            }
        }
        return texts;
    };
    app.select("boar");
    const QStringList boar = choices();
    CHECK_EQ(boar.size(), 1);
    CHECK(boar.at(0).contains(QStringLiteral("moved 20+ feet")));
    CHECK(boar.at(0).contains(QStringLiteral("extra 1d6 piercing")));
    app.select("mimic");  // grappled-by-the-mimic damage: the app sees it, so no box
    CHECK(choices().isEmpty());
    app.select("goblin");
    CHECK(choices().isEmpty());
    CHECK(app.find<QComboBox>("rollMode")->currentText() == QStringLiteral("Automatic"));
}

TEST_CASE("Pack Tactics is a tick box for one attack, and Automatic says it gave Advantage")
{
    App app;
    app.characters.saveAll({fighter()});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Woods";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "wolf"), "wolf"));
    encounter.combatants.push_back(makeCharacterCombatant(fighter(), "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 5;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChild<ui::CombatPage*>()->setAutoPass(false);
    app.select("wolf");
    QCheckBox* pack = nullptr;
    for (QCheckBox* box : app.window->findChildren<QCheckBox*>(QStringLiteral("rollQuestion"))) {
        if (box->isVisible() && box->accessibleName().startsWith(QStringLiteral("Pack Tactics"))) {
            pack = box;
        }
    }
    CHECK(pack != nullptr);
    pack->setChecked(true);
    app.button(QStringLiteral("Attack"))->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    auto* log = app.find<QListWidget>("fightLog");
    bool said = false;
    for (int i = 0; i < log->count(); ++i) {
        said = said || log->item(i)->text().contains(QStringLiteral("advantage: Pack Tactics"));
    }
    CHECK(said);
    app.select("wolf");
    for (QCheckBox* box : app.window->findChildren<QCheckBox*>(QStringLiteral("rollQuestion"))) {
        if (box->isVisible()) {
            CHECK(!box->isChecked());  // used up by that attack
        }
    }
}

TEST_CASE("War Cry picks a creature to help, and a character heals another with the help cursor")
{
    App app;
    Character aria = fighter();
    Character bryn = fighter();
    bryn.id = "bryn-sheet";
    bryn.name = "Bryn";
    bryn.hp = {0, 12};
    app.characters.saveAll({aria, bryn});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Pass";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "frost-giant"), "giant"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeCharacterCombatant(bryn, "bryn"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 15;
    encounter.combatants[2].initiative = 10;
    encounter.combatants[3].initiative = 5;
    encounter.combatants[3].conditions.push_back(ActiveCondition{"unconscious", std::nullopt, std::nullopt});
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChild<ui::CombatPage*>()->setAutoPass(false);

    app.select("giant");
    QPushButton* cry = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("featureTarget"))) {
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (candidate->isVisible() && label->property("featureName").toString().startsWith(QStringLiteral("War Cry"))) {
                cry = candidate;
            }
        }
    }
    CHECK(cry != nullptr);
    CHECK(cry->text() == QStringLiteral("Help…"));
    CHECK(!cry->icon().isNull());
    cry->click();
    QApplication::processEvents();
    app.clickTarget("goblin");
    Encounter fight = app.saved();
    const Combatant goblin = App::in(fight, "goblin");
    CHECK(goblin.tempHp >= 7);
    CHECK_EQ(goblin.timedEffects.size(), std::size_t{1});
    CHECK(App::in(fight, "giant").economy.bonusActionUsed);
    CHECK(app.row("initiativeList", "goblin")->text(4).contains(QStringLiteral("Advantage (War Cry)")));

    app.select("aria");
    app.find<QSpinBox>("characterHealAmount")->setValue(5);
    app.find<QPushButton>("characterHeal")->click();
    QApplication::processEvents();
    app.clickTarget("bryn");
    fight = app.saved();
    CHECK_EQ(App::in(fight, "bryn").hp, 5);
    CHECK(!hasCondition(App::in(fight, "bryn"), "unconscious"));
    CHECK(!App::in(fight, "aria").economy.actionUsed);
}

TEST_CASE("Bloodied shows on the card and in the log, not in the turn order's Status, as it is not a condition")
{
    App app;
    app.characters.saveAll({fighter()});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    app.encounters.saveAll({encounter});
    app.open();
    app.select("goblin");
    CHECK(!app.find<QLabel>("bloodiedTag")->isVisible());
    app.find<QSpinBox>("damageAmount")->setValue(5);  // 10 HP -> 5
    app.find<QPushButton>("applyDamage")->click();
    QApplication::processEvents();
    app.select("goblin");
    CHECK(!app.row("initiativeList", "goblin")->text(4).contains(QStringLiteral("Bloodied")));
    CHECK(app.find<QLabel>("bloodiedTag")->isVisible());
    auto* log = app.find<QListWidget>("fightLog");
    bool said = false;
    for (int i = 0; i < log->count(); ++i) {
        said = said || log->item(i)->text().contains(QStringLiteral("is now Bloodied"));
    }
    CHECK(said);
    CHECK(App::in(app.saved(), "goblin").conditions.empty());
}

TEST_CASE("removing a combatant is undoable and nothing else is reverted")
{
    App app;
    app.characters.saveAll({fighter()});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "g1"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "g2"));
    app.encounters.saveAll({encounter});
    app.open();

    app.select("g1");
    app.find<QSpinBox>("damageAmount")->setValue(3);
    app.find<QPushButton>("applyDamage")->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "g1").hp, 7);

    app.select("g2");
    answerNextBox(QMessageBox::Yes);
    app.find<QPushButton>("removeFromFight")->click();
    QApplication::processEvents();
    Encounter edited = app.saved();
    CHECK_EQ(edited.combatants.size(), std::size_t{1});
    CHECK_EQ(edited.combatants[0].name, std::string("Goblin Warrior"));

    app.button(QStringLiteral("Undo"))->click();
    QApplication::processEvents();
    edited = app.saved();
    CHECK_EQ(edited.combatants.size(), std::size_t{2});
    CHECK_EQ(App::in(edited, "g1").hp, 7);  // the earlier damage stays
}

TEST_CASE("a breath weapon rolls once, each target saves, and it must recharge")
{
    App app;
    Character aria = fighter();
    aria.hp = {400, 400};
    Character bryn = fighter();
    bryn.id = "bryn-sheet";
    bryn.name = "Bryn";
    bryn.hp = {400, 400};
    app.characters.saveAll({aria, bryn});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Lair";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "adult-red-dragon"), "dragon"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeCharacterCombatant(bryn, "bryn"));
    app.encounters.saveAll({encounter});
    app.open();
    app.select("dragon");
    app.button(QStringLiteral("Targets…"))->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    app.clickTarget("bryn");
    CHECK_EQ(App::in(app.saved(), "aria").hp, 400);  // each save is asked first
    app.answer("promptRoll");
    app.answer("promptRoll");
    const Encounter fight = app.saved();
    CHECK(App::in(fight, "aria").hp < 400);
    CHECK(App::in(fight, "bryn").hp < 400);
    CHECK_EQ(App::in(fight, "dragon").expended.size(), std::size_t{1});
    CHECK(App::in(fight, "dragon").economy.actionUsed);
}

TEST_CASE("undo takes the undone step's lines out of the log")
{
    App app;
    Character aria = fighter();
    aria.hp = {400, 400};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 5;
    app.encounters.saveAll({encounter});
    app.open();
    QListWidget* log = app.find<QListWidget>("fightLog");

    app.select("goblin");
    app.find<QSpinBox>("damageAmount")->setValue(3);
    app.find<QPushButton>("applyDamage")->click();
    QApplication::processEvents();
    const int afterDamage = log->count();
    CHECK(afterDamage >= 1);

    // Readying the attack and hitting is one step: undo removes both lines.
    app.select("goblin");
    app.button(QStringLiteral("Attack"))->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    CHECK(log->count() > afterDamage);
    app.button(QStringLiteral("Undo"))->click();
    QApplication::processEvents();
    CHECK_EQ(log->count(), afterDamage);

    app.button(QStringLiteral("Undo"))->click();
    QApplication::processEvents();
    CHECK_EQ(log->count(), 0);
    CHECK_EQ(App::in(app.saved(), "goblin").hp, 10);
}

TEST_CASE("the Options page turns off passing a monster's turn")
{
    App app;
    Character aria = fighter();
    aria.hp = {400, 400};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 5;
    app.encounters.saveAll({encounter});
    app.open();

    app.window->findChild<QAction*>(QStringLiteral("pageAction4"))->trigger();
    QApplication::processEvents();
    CHECK(app.window->currentPage() == ui::MainWindow::OptionsIndex);
    QApplication::processEvents();
    app.find<QCheckBox>("autoPass")->setChecked(false);
    app.window->showPage(ui::MainWindow::DashboardIndex);
    QApplication::processEvents();

    app.select("goblin");
    app.button(QStringLiteral("Attack"))->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    const Encounter after = app.saved();
    CHECK(App::in(after, "goblin").economy.actionUsed);
    CHECK_EQ(after.turnIndex, 0);  // still the goblin's turn
}

TEST_CASE("an invisible monster that attacks is no longer Invisible")
{
    App app;
    Character aria = fighter();
    aria.hp = {400, 400};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 5;
    ActiveCondition hidden;
    hidden.id = "invisible";
    hidden.source = "Hide";
    hidden.endsOn = hideEndsOn();
    encounter.combatants[0].conditions.push_back(hidden);
    app.encounters.saveAll({encounter});
    app.open();

    app.select("goblin");
    app.button(QStringLiteral("Attack"))->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    CHECK(!hasCondition(App::in(app.saved(), "goblin"), "invisible"));
}

TEST_CASE("an invisible dragon's breath weapon ends Invisible")
{
    App app;
    Character aria = fighter();
    aria.hp = {400, 400};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Lair";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "adult-red-dragon"), "dragon"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    ActiveCondition hidden;
    hidden.id = "invisible";
    hidden.source = "Hide";
    hidden.endsOn = hideEndsOn();
    encounter.combatants[0].conditions.push_back(hidden);
    app.encounters.saveAll({encounter});
    app.open();
    app.select("dragon");
    app.button(QStringLiteral("Targets…"))->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    CHECK(!hasCondition(App::in(app.saved(), "dragon"), "invisible"));
}

TEST_CASE("the wisp's Vanish makes it Invisible and concentrating, and undo takes both back")
{
    App app;
    Character aria = fighter();
    aria.hp = {400, 400};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Marsh";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "will-o-wisp"), "wisp"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 18;
    encounter.combatants[1].initiative = 5;
    app.encounters.saveAll({encounter});
    app.open();

    app.select("wisp");
    std::vector<QPushButton*> uses;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>()) {
        if (candidate->isVisible() && candidate->text() == QStringLiteral("Use")) {
            uses.push_back(candidate);
        }
    }
    CHECK(!uses.empty());
    // Find the Vanish row by its title next to the button.
    QPushButton* vanish = nullptr;
    for (QPushButton* candidate : uses) {
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (label->text() == QStringLiteral("Vanish")) {
                vanish = candidate;
            }
        }
    }
    CHECK(vanish != nullptr);
    vanish->click();
    QApplication::processEvents();
    Combatant wisp = App::in(app.saved(), "wisp");
    CHECK(hasCondition(wisp, "invisible"));
    CHECK_EQ(wisp.concentration, std::string("Vanish"));

    app.find<QPushButton>("undoFight")->click();
    QApplication::processEvents();
    const Combatant undone = App::in(app.saved(), "wisp");
    CHECK(!hasCondition(undone, "invisible"));
    CHECK(undone.concentration.empty());
}

TEST_CASE("the wisp's Consume Life kills a stable, paralyzed character at 0 HP on a failed save and heals the wisp")
{
    App app;
    Character aria = fighter();
    aria.hp = {0, 12};
    Character bryn = fighter();
    bryn.id = "bryn-sheet";
    bryn.name = "Bryn";
    app.characters.saveAll({aria, bryn});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Marsh";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "will-o-wisp"), "wisp"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeCharacterCombatant(bryn, "bryn"));
    encounter.combatants[0].initiative = 18;
    encounter.combatants[0].hp = 5;
    encounter.combatants[1].initiative = 5;
    encounter.combatants[1].hp = 0;
    encounter.combatants[1].stable = true;
    encounter.combatants[1].conditions = {plain("unconscious"), plain("poisoned"), plain("paralyzed")};
    encounter.combatants[2].initiative = 3;
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChildren<ui::CombatPage*>().front()->setAutoPass(false);
    app.select("wisp");

    // Bryn is up: Consume Life refuses a creature with Hit Points.
    QPushButton* consume = featureButton(app, QStringLiteral("Consume Life"));
    CHECK(consume != nullptr);
    consume->click();
    QApplication::processEvents();
    app.clickTarget("bryn");
    CHECK(app.window->findChild<QPushButton*>(QStringLiteral("promptRoll")) == nullptr);
    CHECK(!App::in(app.saved(), "wisp").economy.bonusActionUsed);

    // Aria at 0 HP: a Constitution save at the top of the page (Paralyzed
    // fails only Strength and Dexterity saves). The table says she failed.
    app.clickTarget("aria");
    QApplication::processEvents();
    auto* text = app.find<QLabel>("promptText");
    CHECK(text->text().contains(QStringLiteral("DC 10 Constitution")));
    CHECK(text->text().contains(QStringLiteral("dies")));
    app.answer("promptFailed");
    const Encounter after = app.saved();
    CHECK(App::in(after, "aria").dead);
    CHECK(App::in(after, "wisp").hp >= 8);  // 5 + 3d6
    CHECK(App::in(after, "wisp").economy.bonusActionUsed);
}

TEST_CASE("a monster's save asks Roll, Saved or Failed; a save the target fails automatically is not asked")
{
    App app;
    Character aria = fighter();
    aria.hp = {400, 400};
    Character bryn = fighter();
    bryn.id = "bryn-sheet";
    bryn.name = "Bryn";
    bryn.hp = {400, 400};
    Character cole = fighter();
    cole.id = "cole-sheet";
    cole.name = "Cole";
    cole.hp = {400, 400};
    app.characters.saveAll({aria, bryn, cole});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Lair";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "adult-red-dragon"), "dragon"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeCharacterCombatant(bryn, "bryn"));
    encounter.combatants.push_back(makeCharacterCombatant(cole, "cole"));
    encounter.combatants[3].conditions = {plain("paralyzed")};  // fails Dexterity saves
    app.encounters.saveAll({encounter});
    app.open();
    app.select("dragon");
    app.button(QStringLiteral("Targets…"))->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    app.clickTarget("bryn");
    app.clickTarget("cole");
    QApplication::processEvents();
    Encounter fight = app.saved();
    const int full = 400 - App::in(fight, "cole").hp;  // failed at once: the whole roll
    CHECK(full > 0);
    CHECK_EQ(App::in(fight, "aria").hp, 400);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);  // the rows rebuilt over
    // One creature at a time: Aria's save shows, Bryn's waits.
    CHECK_EQ(app.window->findChildren<QLabel*>(QStringLiteral("promptText")).size(), qsizetype{1});
    CHECK(app.find<QLabel>("promptText")->text().contains(QStringLiteral("Aria")));
    CHECK(app.find<QLabel>("promptQueued")->text().contains(QStringLiteral("Bryn")));

    app.answer("promptPassed");  // Aria: half
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(app.find<QLabel>("promptText")->text().contains(QStringLiteral("Bryn")));
    CHECK(app.window->findChild<QLabel*>(QStringLiteral("promptQueued")) == nullptr);
    app.answer("promptFailed");  // Bryn: all of it
    fight = app.saved();
    CHECK_EQ(400 - App::in(fight, "aria").hp, full / 2);
    CHECK_EQ(400 - App::in(fight, "bryn").hp, full);

    // Undo puts Bryn's question back, to be answered again.
    app.find<QPushButton>("undoFight")->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "bryn").hp, 400);
    app.answer("promptFailed");
    CHECK_EQ(400 - App::in(app.saved(), "bryn").hp, full);
}

TEST_CASE("Invisible added by hand from the Invisibility spell starts concentration")
{
    App app;
    app.characters.saveAll({fighter()});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    app.encounters.saveAll({encounter});
    app.open();
    app.select("goblin");
    auto* picker = app.find<QComboBox>("conditionPicker");
    picker->setCurrentIndex(picker->findData(QStringLiteral("invisible")));
    app.find<QComboBox>("invisibleCause")->setCurrentIndex(1);
    app.find<QPushButton>("addCondition")->click();
    QApplication::processEvents();
    Combatant goblin = App::in(app.saved(), "goblin");
    CHECK(hasCondition(goblin, "invisible"));
    CHECK_EQ(goblin.concentration, std::string("invisibility"));

    app.find<QPushButton>("clearConcentration")->click();
    QApplication::processEvents();
    goblin = App::in(app.saved(), "goblin");
    CHECK(!hasCondition(goblin, "invisible"));
}

TEST_CASE("concentration offers only concentration spells, and Concentrate and End share one spot")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[0].timedEffects.emplace_back(
        std::string(kTimedConcentrationDisadvantagePrefix) + "Cloud of Insects", ConditionDuration{});
    app.encounters.saveAll({encounter});
    app.open();
    app.select("aria");
    app.find<QTabWidget>("combatantTabs")->setCurrentIndex(1);
    QApplication::processEvents();

    auto* matches = app.find<QListWidget>("concentrationMatches");
    CHECK_EQ(matches->height(), 48);
    bool bless = false;
    for (int i = 0; i < matches->count(); ++i) {
        const std::string id = matches->item(i)->data(Qt::UserRole).toString().toStdString();
        const std::optional<Spell> spell = findSpellById(app.spells, id);
        CHECK(spell.has_value());
        CHECK(spell->concentration);
        CHECK(id != "fireball");
        bless = bless || id == "bless";
    }
    CHECK(bless);
    CHECK(matches->count() > 0);

    app.find<QLineEdit>("concentrationSearch")->setText(QStringLiteral("fire"));
    QApplication::processEvents();
    for (int i = 0; i < matches->count(); ++i) {
        const std::string id = matches->item(i)->data(Qt::UserRole).toString().toStdString();
        const std::optional<Spell> spell = findSpellById(app.spells, id);
        CHECK(spell.has_value());
        CHECK(spell->concentration);
        CHECK(id != "fireball");
        CHECK(id != "fire-bolt");
    }

    QWidget* page = app.find<QLabel>("concentrationLabel")->parentWidget();
    auto topOf = [page](QWidget* widget) { return widget->mapTo(page, QPoint(0, 0)).y(); };
    auto leftOf = [page](QWidget* widget) { return widget->mapTo(page, QPoint(0, 0)).x(); };
    QLabel* heading = nullptr;
    for (QLabel* label : page->findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("Concentration")) {
            heading = label;
        }
    }
    CHECK(heading != nullptr);
    auto* status = app.find<QLabel>("concentrationLabel");
    CHECK(topOf(status) > topOf(heading));
    CHECK(status->text().contains(QStringLiteral("Disadvantage")));

    auto* concentrate = app.find<QPushButton>("setConcentration");
    auto* end = app.find<QPushButton>("clearConcentration");
    CHECK(concentrate->isVisible());
    CHECK(!end->isVisible());
    const int buttonX = leftOf(concentrate);

    app.find<QLineEdit>("concentrationSearch")->setText(QStringLiteral("bless"));
    QApplication::processEvents();
    int blessRow = -1;
    for (int i = 0; i < matches->count(); ++i) {
        if (matches->item(i)->data(Qt::UserRole).toString() == QStringLiteral("bless")) {
            blessRow = i;
        }
    }
    CHECK(blessRow >= 0);
    matches->setCurrentRow(blessRow);
    concentrate->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "aria").concentration, std::string("bless"));
    CHECK(status->text().contains(QStringLiteral("Bless")));
    CHECK(status->text().contains(QStringLiteral("Disadvantage")));
    CHECK(topOf(status) > topOf(heading));
    CHECK(!concentrate->isVisible());
    CHECK(end->isVisible());
    CHECK_EQ(leftOf(end), buttonX);

    end->click();
    QApplication::processEvents();
    CHECK(App::in(app.saved(), "aria").concentration.empty());
    CHECK(concentrate->isVisible());
    CHECK(!end->isVisible());
    CHECK_EQ(leftOf(concentrate), buttonX);
}

TEST_CASE("a character Charmed by the Vampire can't attack it, but can attack another monster")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Castle";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "vampire"), "vampire"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 5;
    encounter.combatants[1].initiative = 3;
    encounter.combatants[2].initiative = 15;
    ActiveCondition charmed;
    charmed.id = "charmed";
    charmed.source = "Charm";
    charmed.byId = "vampire";
    encounter.combatants[2].conditions.push_back(charmed);
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();

    app.select("aria");
    app.find<QSpinBox>("characterAttackDamage")->setValue(6);
    app.find<QPushButton>("characterAttack")->click();
    QApplication::processEvents();
    app.clickTarget("vampire");
    Encounter after = app.saved();
    CHECK_EQ(App::in(after, "vampire").hp, App::in(encounter, "vampire").hp);
    CHECK(!App::in(after, "aria").economy.actionUsed);
    auto* log = app.find<QListWidget>("fightLog");
    bool refused = false;
    for (int i = 0; i < log->count(); ++i) {
        refused = refused || log->item(i)->text().contains(QStringLiteral("Charmed by Vampire"));
    }
    CHECK(refused);

    app.clickTarget("goblin");  // still ready: another target is fine
    after = app.saved();
    CHECK(App::in(after, "goblin").hp < App::in(encounter, "goblin").hp);
    CHECK(App::in(after, "aria").economy.actionUsed);
}

TEST_CASE("a creature grappled by the Vampire's Grave Strike can spend its action to escape")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Castle";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "vampire"), "vampire"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 5;
    encounter.combatants[1].initiative = 15;
    ActiveCondition grappled;
    grappled.id = "grappled";
    grappled.source = "Vampire's Grave Strike, escape DC 14";
    grappled.byId = "vampire";
    grappled.escapeDc = 14;
    ActiveCondition restrained;  // "Restrained until the grapple ends"
    restrained.id = "restrained";
    restrained.byId = "vampire";
    restrained.tiedTo = "grappled";
    encounter.combatants[1].conditions = {grappled, restrained};
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();

    app.select("aria");
    auto* escape = app.find<QPushButton>("escapeGrapple");
    CHECK(escape->isEnabled());
    escape->click();
    QApplication::processEvents();
    CHECK(App::in(app.saved(), "aria").economy.actionUsed);
    CHECK(app.find<QLabel>("promptText")->text().contains(QStringLiteral("DC 14")));

    app.answer("promptFailed");  // the table rolled it
    CHECK(hasCondition(App::in(app.saved(), "aria"), "grappled"));
    app.select("aria");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);  // the rows rebuilt over
    CHECK(!app.find<QPushButton>("escapeGrapple")->isEnabled());  // the action is spent

    app.find<QPushButton>("undoFight")->click();  // back to before the failed roll
    QApplication::processEvents();
    CHECK(App::in(app.saved(), "aria").economy.actionUsed);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK_EQ(app.window->findChildren<QLabel*>(QStringLiteral("promptText")).size(), qsizetype{1});
    app.answer("promptPassed");
    const Combatant free = App::in(app.saved(), "aria");
    CHECK(!hasCondition(free, "grappled"));
    CHECK(!hasCondition(free, "restrained"));
}

TEST_CASE("each creature returns to the card tab it last had open")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Road";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 10;
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    auto* tabs = app.find<QTabWidget>("combatantTabs");

    app.select("goblin");
    CHECK_EQ(tabs->currentIndex(), 0);  // Actions the first time
    tabs->setCurrentIndex(2);           // Details
    app.select("aria");
    CHECK_EQ(tabs->currentIndex(), 0);
    tabs->setCurrentIndex(1);           // Conditions
    app.select("goblin");
    CHECK_EQ(tabs->currentIndex(), 2);
    app.select("aria");
    CHECK_EQ(tabs->currentIndex(), 1);
    // A change to the fight redraws the card without moving the tab.
    app.find<QPushButton>("nextTurn")->click();
    QApplication::processEvents();
    app.select("aria");
    CHECK_EQ(tabs->currentIndex(), 1);
}

TEST_CASE("a character's Attack deals typed damage to the clicked target, with resistances")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Crypt";
    Monster skeleton = srd(app.srdMonsters, "goblin-warrior");
    skeleton.defenses.resistances = {"slashing"};
    encounter.combatants.push_back(makeMonsterCombatant(skeleton, "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 5;
    encounter.combatants[1].initiative = 15;
    ActiveCondition hidden;
    hidden.id = "invisible";
    hidden.source = "Hide";
    hidden.endsOn = hideEndsOn();
    encounter.combatants[1].conditions.push_back(hidden);
    app.encounters.saveAll({encounter});
    app.open();

    app.select("aria");
    app.find<QSpinBox>("characterAttackDamage")->setValue(6);
    app.find<QPushButton>("characterAttack")->click();
    QApplication::processEvents();
    auto logSays = [&app](const QString& text) {
        auto* log = app.find<QListWidget>("fightLog");
        for (int i = 0; i < log->count(); ++i) {
            if (log->item(i)->text().contains(text)) {
                return true;
            }
        }
        return false;
    };
    CHECK(!logSays(QStringLiteral("Click")));  // readying an action logs nothing
    app.clickTarget("goblin");
    const Encounter after = app.saved();
    CHECK_EQ(App::in(after, "goblin").hp, App::in(encounter, "goblin").hp - 3);  // halved by resistance
    CHECK(!hasCondition(App::in(after, "aria"), "invisible"));
    CHECK(App::in(after, "aria").economy.actionUsed);
    CHECK(logSays(QStringLiteral("Aria hits")));

    // One Undo takes back the whole attack, and its log lines.
    app.find<QPushButton>("undoFight")->click();
    QApplication::processEvents();
    const Encounter undone = app.saved();
    CHECK_EQ(App::in(undone, "goblin").hp, App::in(encounter, "goblin").hp);
    CHECK(hasCondition(App::in(undone, "aria"), "invisible"));
    CHECK(!App::in(undone, "aria").economy.actionUsed);
    CHECK(!logSays(QStringLiteral("Aria hits")));
}

TEST_CASE("Damage hits every selected creature, with half for those who saved, as one undo step")
{
    App app;
    app.characters.saveAll({fighter()});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "g1"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "g2"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "g3"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 12;
    encounter.combatants[2].initiative = 8;
    app.encounters.saveAll({encounter});
    app.open();

    QTreeWidget* order = app.find<QTreeWidget>("initiativeList");
    app.select("g1");
    app.row("initiativeList", "g2")->setSelected(true);
    QApplication::processEvents();
    app.find<QPushButton>("openDamage")->click();
    QApplication::processEvents();
    CHECK(app.find<QWidget>("hitPanel")->isVisible());
    for (QCheckBox* box : app.window->findChildren<QCheckBox*>(QStringLiteral("savedHalf"))) {
        if (box->property("combatantId").toString() == QStringLiteral("g2")) {
            box->setChecked(true);
        }
    }
    app.find<QSpinBox>("damageAmount")->setValue(8);
    app.find<QPushButton>("applyDamage")->click();
    QApplication::processEvents();
    Encounter after = app.saved();
    CHECK_EQ(App::in(after, "g1").hp, 2);
    CHECK_EQ(App::in(after, "g2").hp, 6);
    CHECK_EQ(App::in(after, "g3").hp, 10);
    CHECK(!app.find<QWidget>("hitPanel")->isVisible());

    app.button(QStringLiteral("Undo"))->click();
    QApplication::processEvents();
    after = app.saved();
    CHECK_EQ(App::in(after, "g1").hp, 10);
    CHECK_EQ(App::in(after, "g2").hp, 10);
    (void)order;
}

TEST_CASE("the HP box takes a change as well as a total")
{
    App app;
    app.characters.saveAll({fighter()});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    app.encounters.saveAll({encounter});
    app.open();
    app.select("goblin");
    auto* hp = app.find<QSpinBox>("hpField");
    QLineEdit* edit = hp->findChild<QLineEdit*>();
    edit->setText(QStringLiteral("-7"));
    hp->interpretText();
    QApplication::processEvents();
    CHECK_EQ(hp->value(), 3);
    app.window->findChild<ui::CombatPage*>()->flushPendingSave();  // typed edits save after a pause
    CHECK_EQ(App::in(app.saved(), "goblin").hp, 3);
    edit->setText(QStringLiteral("+4"));
    hp->interpretText();
    QApplication::processEvents();
    app.window->findChild<ui::CombatPage*>()->flushPendingSave();
    CHECK_EQ(App::in(app.saved(), "goblin").hp, 7);
}

TEST_CASE("the Basilisk's gaze is aimed like an action, restrains, and petrifies on a second failure")
{
    App app;
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Lair";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "basilisk"), "basilisk"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 5;
    encounter.combatants[1].saveBonuses.fill(-40);  // fails every save
    app.encounters.saveAll({encounter});
    app.open();
    app.select("basilisk");
    auto gazeButton = [&app]() -> QPushButton* {
        for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("featureTarget"))) {
            if (!candidate->isVisible()) {
                continue;
            }
            for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
                if (label->property("featureName").toString().startsWith(QStringLiteral("Petrifying Gaze"))) {
                    return candidate;
                }
            }
        }
        return nullptr;
    };
    QPushButton* gaze = gazeButton();
    CHECK(gaze != nullptr);
    CHECK(gaze->isEnabled());
    CHECK(gaze->text() == QStringLiteral("Targets…"));
    gaze->click();
    QApplication::processEvents();
    app.clickTarget("goblin");
    app.answer("promptFailed");  // the table rolled it
    Encounter after = app.saved();
    CHECK_EQ(App::in(after, "basilisk").expended.size(), std::size_t{1});
    CHECK(App::in(after, "basilisk").economy.bonusActionUsed);
    CHECK(hasCondition(App::in(after, "goblin"), "restrained"));

    // The goblin repeats the save at the end of its next turn and fails.
    app.find<QPushButton>("nextTurn")->click();  // basilisk -> goblin
    app.find<QPushButton>("nextTurn")->click();  // goblin's turn ends
    QApplication::processEvents();
    app.find<QPushButton>("promptFailed")->click();
    QApplication::processEvents();
    after = app.saved();
    CHECK(!hasCondition(App::in(after, "goblin"), "restrained"));
    CHECK(hasCondition(App::in(after, "goblin"), "petrified"));
}

TEST_CASE("Engulf grapples, blinds, and restrains on a failed save, and hurts at the start of the target's turn")
{
    App app;
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Bog";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "shambling-mound"), "mound"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "knight"), "knight"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 5;
    encounter.combatants[1].saveBonuses.fill(-40);
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChild<ui::CombatPage*>()->setAutoPass(false);
    app.select("mound");
    QPushButton* engulf = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (candidate->isVisible() && label->text() == QStringLiteral("Engulf")) {
                engulf = candidate;
            }
        }
    }
    CHECK(engulf != nullptr);
    engulf->click();
    QApplication::processEvents();
    app.clickTarget("knight");
    app.answer("promptRoll");
    Combatant knight = App::in(app.saved(), "knight");
    CHECK(hasCondition(knight, "grappled"));
    CHECK(hasCondition(knight, "blinded"));
    CHECK(hasCondition(knight, "restrained"));
    CHECK_EQ(knight.hp, 52);

    app.find<QPushButton>("nextTurn")->click();  // mound -> knight: 3d6 lightning
    QApplication::processEvents();
    knight = App::in(app.saved(), "knight");
    CHECK(knight.hp < 52);
    CHECK(knight.hp >= 52 - 18);
}

TEST_CASE("the Sea Hag's Vile Appearance asks at the start of a character's turn")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "sea-hag"), "hag"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    app.encounters.saveAll({encounter});
    app.open();

    app.find<QPushButton>("nextTurn")->click();  // hag -> aria
    QApplication::processEvents();
    CHECK(app.find<QWidget>("promptPanel")->isVisible());
    app.find<QPushButton>("promptFailed")->click();
    QApplication::processEvents();
    Combatant hero = App::in(app.saved(), "aria");
    CHECK(hasCondition(hero, "frightened"));

    // Frightened lasts until the start of Aria's next turn, when she is asked again.
    app.find<QPushButton>("nextTurn")->click();  // aria -> hag
    QApplication::processEvents();
    CHECK(hasCondition(App::in(app.saved(), "aria"), "frightened"));
    app.find<QPushButton>("nextTurn")->click();  // hag -> aria
    QApplication::processEvents();
    CHECK(!hasCondition(App::in(app.saved(), "aria"), "frightened"));
    app.find<QPushButton>("promptPassed")->click();  // saved: immune for the fight
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "aria").auraImmunities.size(), std::size_t{1});
    app.find<QPushButton>("nextTurn")->click();
    app.find<QPushButton>("nextTurn")->click();
    QApplication::processEvents();
    CHECK(!app.find<QWidget>("promptPanel")->isVisible());
}

TEST_CASE("the Sea Hag's Illusory Appearance switches its aura off")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "sea-hag"), "hag"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    app.encounters.saveAll({encounter});
    app.open();
    app.select("hag");
    QPushButton* illusion = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
        if (!candidate->isVisible()) {
            continue;
        }
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (label->text().startsWith(QStringLiteral("Illusory Appearance"))) {
                illusion = candidate;
            }
        }
    }
    CHECK(illusion != nullptr);
    illusion->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "hag").aurasOff.size(), std::size_t{1});
    app.find<QPushButton>("nextTurn")->click();
    QApplication::processEvents();
    CHECK(!app.find<QWidget>("promptPanel")->isVisible());
}

TEST_CASE("Start combat ends the initiative phase and asks about auras on the first turn")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.started = false;
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "sea-hag"), "hag"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    app.encounters.saveAll({encounter});
    app.open();

    QPushButton* start = app.find<QPushButton>("nextTurn");
    CHECK(start->text() == QStringLiteral("Start combat"));
    CHECK(app.window->findChild<QPushButton*>(QStringLiteral("previousTurn")) == nullptr);  // Undo does it
    CHECK(!app.find<QWidget>("promptPanel")->isVisible());
    start->click();
    QApplication::processEvents();
    CHECK(app.saved().started);
    CHECK(start->text() == QStringLiteral("Next turn"));
    // Aria acts first, so she is asked about Vile Appearance now.
    CHECK(app.find<QWidget>("promptPanel")->isVisible());
}

TEST_CASE("the initiative phase lists every character on the right to type their initiative in")
{
    App app;
    Character aria = fighter();
    Character bryn = fighter();
    bryn.id = "bryn-sheet";
    bryn.name = "Bryn";
    app.characters.saveAll({aria, bryn});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Road";
    encounter.started = false;
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeCharacterCombatant(bryn, "bryn"));
    encounter.combatants[0].initiative = 12;
    app.encounters.saveAll({encounter});
    app.open();

    // Nobody is selected, so the right shows the list: characters only.
    CHECK(app.find<QWidget>("initiativeEntry")->isVisible());
    CHECK(!app.find<QWidget>("selectedName")->isVisible());
    const QList<QSpinBox*> fields = app.window->findChildren<QSpinBox*>(QStringLiteral("initiativeEntryField"));
    CHECK_EQ(fields.size(), qsizetype{2});
    CHECK(fields[0]->property("combatantId").toString() == QStringLiteral("aria"));
    CHECK(fields[1]->property("combatantId").toString() == QStringLiteral("bryn"));
    CHECK(app.find<QLabel>("initiativeEntryCount")->text() == QStringLiteral("0 of 2 entered"));

    fields[1]->setValue(18);
    emit fields[1]->editingFinished();
    fields[0]->setValue(7);
    emit fields[0]->editingFinished();
    QApplication::processEvents();
    Encounter fight = app.saved();
    CHECK_EQ(App::in(fight, "bryn").initiative, 18);
    CHECK_EQ(App::in(fight, "aria").initiative, 7);
    CHECK_EQ(fight.combatants[0].id, std::string("bryn"));  // the Turn order is sorted
    CHECK_EQ(fight.combatants[2].id, std::string("aria"));
    CHECK(app.row("initiativeList", "bryn")->text(0) == QStringLiteral("18"));
    CHECK(app.find<QLabel>("initiativeEntryCount")->text() == QStringLiteral("2 of 2 entered"));
    // The list keeps its rows while typing, so the focus does not jump.
    const QList<QSpinBox*> after = app.window->findChildren<QSpinBox*>(QStringLiteral("initiativeEntryField"));
    CHECK(after.contains(fields[0]) && after.contains(fields[1]));

    // Undo takes back the whole pass in one step.
    app.find<QPushButton>("undoFight")->click();
    QApplication::processEvents();
    fight = app.saved();
    CHECK_EQ(App::in(fight, "bryn").initiative, 0);
    CHECK_EQ(App::in(fight, "aria").initiative, 0);
    CHECK(fields[0]->value() == 0 && fields[1]->value() == 0);

    // Selecting a creature shows it; Initiative list goes back.
    app.select("goblin");
    CHECK(!app.find<QWidget>("initiativeEntry")->isVisible());
    CHECK(app.find<QWidget>("selectedName")->isVisible());
    app.find<QPushButton>("showInitiativeEntry")->click();
    QApplication::processEvents();
    CHECK(app.find<QWidget>("initiativeEntry")->isVisible());

    // Starting with characters still at 0 asks first.
    answerNextBox(QMessageBox::No);
    app.find<QPushButton>("nextTurn")->click();
    QApplication::processEvents();
    CHECK(!app.saved().started);
    fields[0]->setValue(15);
    fields[1]->setValue(9);
    app.find<QPushButton>("nextTurn")->click();
    QApplication::processEvents();
    CHECK(app.saved().started);
    CHECK(!app.find<QWidget>("initiativeEntry")->isVisible());

    // Undo straight after Start combat goes back to the initiative list.
    app.find<QPushButton>("undoFight")->click();
    QApplication::processEvents();
    CHECK(!app.saved().started);
    CHECK(app.find<QWidget>("initiativeEntry")->isVisible());
    CHECK(!app.find<QWidget>("selectedName")->isVisible());
    app.find<QPushButton>("nextTurn")->click();
    QApplication::processEvents();
    CHECK(app.saved().started);
    CHECK(app.find<QPushButton>("showInitiativeEntry")->isHidden());
}

TEST_CASE("Legendary Resistance has a Use button that counts down its uses")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Lair";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "aboleth"), "aboleth"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    app.encounters.saveAll({encounter});
    app.open();
    app.select("aboleth");
    QPushButton* resist = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>()) {
        if (!candidate->isVisible() || candidate->text() != QStringLiteral("Use")) {
            continue;
        }
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (label->property("featureName").toString().startsWith(QStringLiteral("Legendary Resistance"))) {
                resist = candidate;
            }
        }
    }
    CHECK(resist != nullptr);
    resist->click();
    QApplication::processEvents();
    const Combatant aboleth = App::in(app.saved(), "aboleth");
    CHECK_EQ(aboleth.usesRemaining.at("Legendary Resistance (3/Day, or 4/Day in Lair)"), 2);
}

TEST_CASE("checks show one creature at a time: Death Glare's save first, then the next character's Vile Appearance")
{
    App app;
    Character aria = fighter();
    Character bryn = fighter();
    bryn.id = "bryn-sheet";
    bryn.name = "Bryn";
    app.characters.saveAll({aria, bryn});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "sea-hag"), "hag"));
    encounter.combatants.push_back(makeCharacterCombatant(bryn, "bryn"));  // Bryn goes after the hag
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    encounter.combatants[2].initiative = 5;
    encounter.combatants[2].conditions = {plain("frightened")};
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChildren<ui::CombatPage*>().front()->setAutoPass(false);
    app.select("hag");
    QPushButton* glare = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (candidate->isVisible() && label->text().startsWith(QStringLiteral("Death Glare"))) {
                glare = candidate;
            }
        }
    }
    CHECK(glare != nullptr);
    glare->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    app.find<QPushButton>("nextTurn")->click();  // hag -> Bryn: Vile Appearance
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto shown = [&app] {
        QStringList texts;
        for (QLabel* label : app.window->findChildren<QLabel*>(QStringLiteral("promptText"))) {
            texts << label->text();
        }
        return texts;
    };
    CHECK_EQ(shown().size(), qsizetype{1});
    CHECK(shown().front().contains(QStringLiteral("Death Glare")));
    CHECK(app.find<QLabel>("promptQueued")->text().contains(QStringLiteral("Bryn")));

    app.answer("promptPassed");  // Aria resists the glare
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK_EQ(shown().size(), qsizetype{1});
    CHECK(shown().front().contains(QStringLiteral("Vile Appearance")));
    CHECK(shown().front().contains(QStringLiteral("Bryn")));
}

TEST_CASE("after its breath weapon, a dragon with nothing left passes its turn once targets are done")
{
    App app;
    Character aria = fighter();
    aria.hp = {200, 200};
    Character bryn = fighter();
    bryn.id = "bryn-sheet";
    bryn.name = "Bryn";
    bryn.hp = {200, 200};
    app.characters.saveAll({aria, bryn});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Glacier";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "young-white-dragon"), "dragon"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeCharacterCombatant(bryn, "bryn"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    encounter.combatants[2].initiative = 5;
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.select("dragon");
    QPushButton* breath = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (candidate->isVisible() && label->text().startsWith(QStringLiteral("Cold Breath"))) {
                breath = candidate;
            }
        }
    }
    CHECK(breath != nullptr);
    breath->click();
    QApplication::processEvents();
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        bool ending = false;  // ready: the button ends it
        for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
            ending = ending || (candidate->isVisible() && candidate->text() == QStringLiteral("End"));
        }
        CHECK(ending);
    }
    app.clickTarget("aria");
    app.clickTarget("bryn");
    CHECK_EQ(app.saved().turnIndex, 0);  // still choosing creatures in the cone
    // The creatures caught are highlighted; the card stays on the dragon.
    const auto rowSelected = [&app](const char* id) {
        auto* list = app.find<QTreeWidget>("initiativeList");
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            if (list->topLevelItem(i)->data(0, Qt::UserRole).toString() == QLatin1String(id)) {
                return list->topLevelItem(i)->isSelected();
            }
        }
        return false;
    };
    CHECK(rowSelected("aria"));
    CHECK(rowSelected("bryn"));
    CHECK(!rowSelected("dragon"));
    CHECK(app.find<QLabel>("selectedName")->text() == QStringLiteral("Young White Dragon"));

    // Escape: done choosing. Nothing else to do, so the turn passes.
    for (QShortcut* shortcut : app.window->findChildren<QShortcut*>()) {
        if (shortcut->key() == QKeySequence(Qt::Key_Escape)) {
            emit shortcut->activated();
        }
    }
    QApplication::processEvents();
    CHECK_EQ(app.saved().turnIndex, 1);
    // The saves are still asked, one creature at a time.
    CHECK(app.find<QLabel>("promptText")->text().contains(QStringLiteral("Cold Breath")));
}

TEST_CASE("using the Vampire's Multiattack and then an attack keeps the Actions tab where it was")
{
    App app;
    Character aria = fighter();
    aria.hp = {300, 300};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Castle";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "vampire"), "vampire"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 5;
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->resize(1100, 640);  // short enough that the actions scroll
    app.window->show();
    app.window->activateWindow();  // so buttons take the focus, as with a mouse
    QApplication::processEvents();
    app.window->findChildren<ui::CombatPage*>().front()->setAutoPass(false);
    app.select("vampire");
    QScrollBar* bar = nullptr;
    for (QScrollArea* area : app.window->findChildren<QScrollArea*>()) {
        if (area->isVisible() && area->isAncestorOf(app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage")).front())) {
            bar = area->verticalScrollBar();
        }
    }
    CHECK(bar != nullptr);
    CHECK(bar->maximum() > 0);
    const auto visibleButton = [&app](const QString& text) -> QPushButton* {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (QPushButton* button : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
            if (button->isVisible() && button->isEnabled() && button->text() == text) {
                return button;
            }
        }
        return nullptr;
    };
    QPushButton* multiattack = visibleButton(QStringLiteral("Multiattack"));
    CHECK(multiattack != nullptr);
    multiattack->setFocus(Qt::MouseFocusReason);  // as a mouse click does
    multiattack->click();
    for (int k = 0; k < 5; ++k) { QApplication::processEvents(); QCoreApplication::sendPostedEvents(); }
    CHECK_EQ(bar->value(), 0);
    QPushButton* attack = visibleButton(QStringLiteral("Attack"));
    CHECK(attack != nullptr);
    attack->setFocus(Qt::MouseFocusReason);
    attack->click();
    for (int k = 0; k < 5; ++k) { QApplication::processEvents(); QCoreApplication::sendPostedEvents(); }
    app.clickTarget("aria");
    for (int k = 0; k < 5; ++k) { QApplication::processEvents(); QCoreApplication::sendPostedEvents(); }
    CHECK_EQ(bar->value(), 0);  // not down at the Legendary Actions
}

TEST_CASE("the Ghost's Horrific Visage can't target the ghost itself")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Manor";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "ghost"), "ghost"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 5;
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChildren<ui::CombatPage*>().front()->setAutoPass(false);
    app.select("ghost");
    QPushButton* visage = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (candidate->isVisible() && label->text().startsWith(QStringLiteral("Horrific Visage"))) {
                visage = candidate;
            }
        }
    }
    CHECK(visage != nullptr);
    visage->click();
    QApplication::processEvents();
    app.clickTarget("ghost");
    CHECK(app.window->findChild<QLabel*>(QStringLiteral("promptText")) == nullptr);
    CHECK(!App::in(app.saved(), "ghost").economy.actionUsed);  // nothing spent on itself
    auto* list = app.find<QTreeWidget>("initiativeList");
    CHECK(list->selectedItems().isEmpty());  // and its row is not highlighted
    bool said = false;
    auto* log = app.find<QListWidget>("fightLog");
    for (int i = 0; i < log->count(); ++i) {
        said = said || log->item(i)->text().contains(QStringLiteral("can't target itself"));
    }
    CHECK(said);

    app.clickTarget("aria");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(app.find<QLabel>("promptText")->text().contains(QStringLiteral("Aria")));
    CHECK_EQ(list->selectedItems().size(), qsizetype{1});
}

TEST_CASE("Horrific Visage doesn't affect Undead: the Vampire is turned away, Aria saves")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Manor";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "ghost"), "ghost"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "vampire"), "vampire"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    encounter.combatants[2].initiative = 5;
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChildren<ui::CombatPage*>().front()->setAutoPass(false);
    app.select("ghost");
    QPushButton* visage = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (candidate->isVisible() && label->text().startsWith(QStringLiteral("Horrific Visage"))) {
                visage = candidate;
            }
        }
    }
    CHECK(visage != nullptr);
    visage->click();
    QApplication::processEvents();
    app.clickTarget("vampire");
    CHECK(app.window->findChild<QLabel*>(QStringLiteral("promptText")) == nullptr);
    bool said = false;
    auto* log = app.find<QListWidget>("fightLog");
    for (int i = 0; i < log->count(); ++i) {
        said = said || log->item(i)->text().contains(QStringLiteral("doesn't affect Undead"));
    }
    CHECK(said);
    app.clickTarget("aria");
    app.answer("promptFailed");
    const Encounter after = app.saved();
    CHECK(!hasCondition(App::in(after, "vampire"), "frightened"));
    CHECK(hasCondition(App::in(after, "aria"), "frightened"));
}

TEST_CASE("ending an action that is still choosing targets selects its user again")
{
    App app;
    Character aria = fighter();
    aria.hp = {200, 200};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Glacier";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "young-white-dragon"), "dragon"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 5;
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChildren<ui::CombatPage*>().front()->setAutoPass(false);  // the turn stays
    app.select("dragon");
    QPushButton* breath = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (candidate->isVisible() && label->text().startsWith(QStringLiteral("Cold Breath"))) {
                breath = candidate;
            }
        }
    }
    CHECK(breath != nullptr);
    breath->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    CHECK(app.find<QLabel>("selectedName")->text() == QStringLiteral("Young White Dragon"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QPushButton* end = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
        end = candidate->isVisible() && candidate->text() == QStringLiteral("End") ? candidate : end;
    }
    CHECK(end != nullptr);
    end->click();
    QApplication::processEvents();
    auto* list = app.find<QTreeWidget>("initiativeList");
    CHECK(list->currentItem() != nullptr);
    CHECK(list->currentItem()->data(0, Qt::UserRole).toString() == QStringLiteral("dragon"));
    CHECK_EQ(list->selectedItems().size(), qsizetype{1});
    CHECK(app.find<QLabel>("selectedName")->text() == QStringLiteral("Young White Dragon"));
}

TEST_CASE("an empty Dashboard with no encounters shows no round or turn")
{
    App app;
    app.open();
    CHECK(app.find<QLabel>("roundLabel")->text().isEmpty());
    CHECK(app.find<QLabel>("activeCombatant")->text().isEmpty());
    CHECK(!app.find<QPushButton>("nextTurn")->isEnabled());
}

TEST_CASE("thrown dice settle by tipping onto the face nearest the viewer, which shows the roll")
{
    QWidget page;
    page.resize(900, 640);
    page.show();
    ui::DiceOverlay overlay(&page);
    std::vector<ui::ThrownDie> dice;
    for (const int sides : {4, 6, 8, 10, 12, 20, 20, 20, 100}) {
        dice.push_back(ui::ThrownDie{sides, sides == 100 ? 47 : sides - 1});
    }
    for (int round = 0; round < 3; ++round) {
        overlay.throwDice(dice, QString());
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 1800) {
            QApplication::processEvents();
            QThread::msleep(4);
        }
        const std::vector<int> up = overlay.facesTowardViewer();
        const std::vector<int> shown = overlay.shownFaces();
        CHECK_EQ(up.size(), shown.size());
        for (std::size_t i = 0; i < up.size() && i < shown.size(); ++i) {
            // A percentile die's tens and units read 0-9 on faces numbered 1-10.
            const bool percentile = i + 2 >= up.size();
            CHECK_EQ(percentile ? up[i] % 10 : up[i], percentile ? (i + 2 == up.size() ? 4 : 7) : shown[i]);
        }
        CHECK(overlay.largestSettleTurn() < 75.0f);  // a tip, not a flip
    }
}

TEST_CASE("the dice tray rolls by hand, and its dice and the app's are thrown across the page")
{
    App app;
    Character aria = fighter();
    aria.hp = {300, 300};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Road";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 10;
    encounter.started = true;
    app.encounters.saveAll({encounter});
    app.open();
    auto* overlay = app.find<ui::DiceOverlay>("diceOverlay");
    CHECK(!overlay->active());

    app.find<QPushButton>("openDice")->click();
    QApplication::processEvents();
    CHECK(app.find<QWidget>("dicePanel")->isVisible());
    // With the option off, a roll is only logged.
    app.find<QCheckBox>("showDice")->setChecked(false);
    app.find<QPushButton>("rollTray")->click();  // an empty tray rolls a d20
    QApplication::processEvents();
    CHECK(!overlay->active());
    CHECK(app.find<QListWidget>("fightLog")->item(0)->text().contains(QStringLiteral("1d20")));
    app.find<QPushButton>("clearTray")->click();
    app.find<QCheckBox>("showDice")->setChecked(true);
    for (QPushButton* die : app.window->findChildren<QPushButton*>(QStringLiteral("trayDie"))) {
        if (die->property("sides").toInt() == 6) {
            die->click();
            die->click();
        }
    }
    CHECK(app.find<QLabel>("trayDice")->text() == QStringLiteral("2d6"));
    app.find<QSpinBox>("trayModifier")->setValue(3);
    app.find<QPushButton>("rollTray")->click();
    QApplication::processEvents();
    CHECK(overlay->active());
    const std::vector<int> faces = overlay->shownFaces();
    CHECK_EQ(faces.size(), std::size_t{2});
    const int total = faces[0] + faces[1] + 3;
    auto* log = app.find<QListWidget>("fightLog");
    CHECK(log->item(0)->text().contains(QStringLiteral("2d6")));
    CHECK(log->item(0)->text().endsWith(QStringLiteral("= %1").arg(total)));

    // An attack throws its d20s (and its damage on a hit).
    app.select("goblin");
    app.button(QStringLiteral("Attack"))->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    QApplication::processEvents();
    const std::vector<int> attack = overlay->shownFaces();
    CHECK(!attack.empty());
    CHECK(attack.front() >= 1);
    CHECK(attack.front() <= 20);
    // A straight roll throws one d20 (and its damage on a hit), not two.
    const std::vector<int> kinds = overlay->shownSides();
    CHECK_EQ(std::count(kinds.begin(), kinds.end(), 20), std::ptrdiff_t{1});
}

TEST_CASE("Death Glare cannot target a creature that is not Frightened")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "sea-hag"), "hag"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    app.encounters.saveAll({encounter});
    app.open();
    app.select("hag");
    QPushButton* glare = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("rollAttackDamage"))) {
        if (!candidate->isVisible()) {
            continue;
        }
        for (QLabel* label : candidate->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (label->text().startsWith(QStringLiteral("Death Glare"))) {
                glare = candidate;
            }
        }
    }
    CHECK(glare != nullptr);
    glare->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    const Encounter after = app.saved();
    CHECK(App::in(after, "hag").expended.empty());  // not spent
    CHECK_EQ(App::in(after, "aria").hp, 12);
}

TEST_CASE("a custom monster's actions read their conditions from the text, and the Effects dialog edits them")
{
    App app;
    app.open();
    app.window->findChild<QAction*>(QStringLiteral("pageAction2"))->trigger();
    QApplication::processEvents();
    app.find<QPushButton>("newMonster")->click();
    QApplication::processEvents();
    app.find<QPushButton>("addMonsterAction")->click();
    QApplication::processEvents();
    auto* actions = app.find<QWidget>("monsterFormAttacks");
    QPlainTextEdit* text = actions->findChildren<QPlainTextEdit*>().at(0);
    text->setPlainText(QStringLiteral(
        "Melee Attack Roll: +4, reach 5 ft. Hit: 9 (2d6 + 2) Bludgeoning damage. If the target is a Medium or "
        "smaller creature, it has the Grappled condition (escape DC 12)."));
    QApplication::processEvents();
    auto* page = app.window->findChild<ui::MonstersPage*>();
    page->flushPendingSave();
    Monster saved = app.custom.loadAll().monsters.at(0);
    CHECK_EQ(saved.attacks.at(0).riders.size(), std::size_t{1});
    CHECK_EQ(saved.attacks.at(0).riders[0].conditions, std::vector<std::string>{"grappled"});
    CHECK_EQ(saved.attacks.at(0).riders[0].escapeDc.value_or(0), 12);
    CHECK_EQ(saved.attacks.at(0).riders[0].targetMaxSize, std::string("Medium"));

    // Read from text fills the roll and the typed damage too.
    actions->findChild<QPushButton*>(QStringLiteral("readEntryText"))->click();
    QApplication::processEvents();
    page->flushPendingSave();
    saved = app.custom.loadAll().monsters.at(0);
    CHECK_EQ(saved.attacks.at(0).attackBonus.value_or(0), 4);
    CHECK_EQ(saved.attacks.at(0).damage.at(0).type, std::string("bludgeoning"));

    // The Effects dialog adds a second condition by hand.
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        dialog->findChild<QPushButton*>(QStringLiteral("addRider"))->click();
        QApplication::processEvents();
        const auto boxes = dialog->findChildren<QFrame*>(QStringLiteral("riderBox"));
        for (QCheckBox* check : boxes.back()->findChildren<QCheckBox*>()) {
            if (check->property("condition").isValid()) {
                check->setChecked(check->property("condition").toString() == QStringLiteral("restrained"));
            }
        }
        const auto tied = boxes.back()->findChildren<QComboBox*>();
        for (QComboBox* combo : tied) {
            const int grappled = combo->findData(QStringLiteral("grappled"));
            if (grappled >= 0 && combo->itemText(0).startsWith(QStringLiteral("Nothing"))) {
                combo->setCurrentIndex(grappled);
            }
        }
        dialog->findChild<QPushButton*>(QStringLiteral("effectsOk"))->click();
    });
    app.find<QWidget>("monsterFormAttacks")->findChild<QPushButton*>(QStringLiteral("editEffects"))->click();
    QApplication::processEvents();
    page->flushPendingSave();
    saved = app.custom.loadAll().monsters.at(0);
    CHECK_EQ(saved.attacks.at(0).riders.size(), std::size_t{2});
    CHECK_EQ(saved.attacks.at(0).riders[1].conditions, std::vector<std::string>{"restrained"});
    CHECK_EQ(saved.attacks.at(0).riders[1].tiedTo, std::string("grappled"));

    // Edited by hand: changing the text no longer replaces the effects.
    QPlainTextEdit* again = app.find<QWidget>("monsterFormAttacks")->findChildren<QPlainTextEdit*>().at(0);
    again->setPlainText(again->toPlainText() + QStringLiteral(" It roars."));
    QApplication::processEvents();
    page->flushPendingSave();
    CHECK_EQ(app.custom.loadAll().monsters.at(0).attacks.at(0).riders.size(), std::size_t{2});

    // A bonus action with a save is aimed like an action.
    app.find<QPushButton>("addFeature1")->click();
    QApplication::processEvents();
    QPlainTextEdit* bonus = app.find<QWidget>("monsterFormBonusActions")->findChildren<QPlainTextEdit*>().at(0);
    bonus->setPlainText(QStringLiteral("Wisdom Saving Throw: DC 13, one creature the monster can see within 30 feet. "
                                       "Failure: The target has the Frightened condition until the end of the "
                                       "monster's next turn."));
    QApplication::processEvents();
    page->flushPendingSave();
    saved = app.custom.loadAll().monsters.at(0);
    CHECK(saved.bonusActions.at(0).targeted.has_value());
    CHECK_EQ(saved.bonusActions.at(0).targeted->save->dc, 13);
    CHECK_EQ(saved.bonusActions.at(0).targeted->riders.at(0).until, std::string(kUntilSourceEnd));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    return test::runAll();
}

TEST_CASE("a dead target is not attacked and the attack is not spent")
{
    App app;
    Character aria = fighter();
    aria.hp = {0, 12};
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Aftermath";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[1].dead = true;
    app.encounters.saveAll({encounter});
    app.open();
    app.select("goblin");
    app.button(QStringLiteral("Attack"))->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    CHECK(!App::in(app.saved(), "goblin").economy.actionUsed);
}

TEST_CASE("the builder shows a character's current HP from the sheet")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Road";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    app.encounters.saveAll({encounter});
    app.open();

    // The sheet changes while the app is open (the Characters page saved it).
    aria.hp = {5, 14};
    app.characters.saveAll({aria});
    app.window->findChild<QAction*>(QStringLiteral("pageAction3"))->trigger();
    QApplication::processEvents();
    QListWidget* roster = nullptr;
    for (QListWidget* list : app.window->findChildren<QListWidget*>()) {
        for (int i = 0; i < list->count(); ++i) {
            if (list->isVisible() && list->item(i)->text().startsWith(QStringLiteral("Aria"))) {
                roster = list;
            }
        }
    }
    CHECK(roster != nullptr);
    CHECK(roster->item(0)->text().contains(QStringLiteral("5 / 14")));
    CHECK_EQ(App::in(app.saved(), "aria").hp, 5);
}

TEST_CASE("the builder adds several monsters at once, refuses a second copy of a character, and rates difficulty")
{
    App app;
    app.characters.saveAll({fighter()});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Ambush";
    app.encounters.saveAll({encounter});
    app.open();
    app.window->showPage(ui::MainWindow::EncounterBuilderIndex);
    QApplication::processEvents();
    app.find<QPushButton>("addCharacter")->click();
    QApplication::processEvents();
    answerNextBox(QMessageBox::Ok);
    app.find<QPushButton>("addCharacter")->click();
    QApplication::processEvents();
    QListWidget* choices = app.find<QListWidget>("monsterChoices");
    for (int i = 0; i < choices->count(); ++i) {
        if (choices->item(i)->data(Qt::UserRole).toString() == QStringLiteral("goblin-warrior")) {
            choices->setCurrentRow(i);
        }
    }
    app.find<QSpinBox>("monsterQuantity")->setValue(3);
    app.find<QPushButton>("addMonster")->click();
    QApplication::processEvents();
    const Encounter saved = app.encounters.loadAll().at(0);
    CHECK_EQ(saved.combatants.size(), std::size_t{4});
    CHECK_EQ(saved.combatants[1].name, std::string("Goblin Warrior 1"));
    CHECK(saved.combatants[1].statBlock.has_value());
    CHECK(app.find<QLabel>("encounterDifficulty")->text().contains(QStringLiteral("150")));
}

TEST_CASE("The turn order marks whose turn it is without a column of its own")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    app.encounters.saveAll({encounter});
    app.open();

    auto* list = app.find<QTreeWidget>("initiativeList");
    CHECK_EQ(list->columnCount(), 5);
    CHECK(list->headerItem()->text(0) == QStringLiteral("Init"));
    CHECK(app.row("initiativeList", "aria")->text(0) == QStringLiteral("20"));
    CHECK(app.row("initiativeList", "aria")->text(1) == QStringLiteral("Aria"));
    CHECK(app.row("initiativeList", "aria")->data(0, Qt::UserRole + 3).toBool());
    CHECK(!app.row("initiativeList", "goblin")->data(0, Qt::UserRole + 3).toBool());

    // Hit Point boxes take four digits; Remove from fight sits by the tabs.
    CHECK_EQ(app.find<QSpinBox>("hpField")->maximum(), 9999);
    CHECK_EQ(app.find<QSpinBox>("tempHpField")->maximum(), 9999);
    auto* tabs = app.find<QTabWidget>("combatantTabs");
    CHECK(tabs->cornerWidget(Qt::TopRightCorner)->isAncestorOf(app.find<QPushButton>("removeFromFight")));
    CHECK(app.window->findChild<QLabel*>(QStringLiteral("initiativeBonusLabel")) == nullptr);
}

TEST_CASE("Initiative, its bonus, and Reroll sit under an Initiative heading on the Details tab")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    app.encounters.saveAll({encounter});
    app.open();

    auto* section = app.find<QWidget>("initiativeSection");
    CHECK(app.find<QWidget>("combatantDetails")->isAncestorOf(section));
    auto* field = app.find<QSpinBox>("initiativeField");
    auto* reroll = app.find<QPushButton>("rerollMonster");
    auto* bonus = app.find<QLabel>("initiativeBonusValue");
    CHECK(section->isAncestorOf(field) && section->isAncestorOf(reroll) && section->isAncestorOf(bonus));

    app.select("goblin");
    CHECK_EQ(field->value(), 10);
    CHECK(bonus->text() == QStringLiteral("+2"));
    CHECK(!reroll->isHidden());

    app.select("aria");
    CHECK_EQ(field->value(), 20);
    CHECK(bonus->text() == QString::fromStdString(formatModifier(initiativeModifier(aria))));
    CHECK(reroll->isHidden());  // players roll their own
}

TEST_CASE("HP, Temp HP, and Bloodied sit by the name; Exhaustion is on the Conditions tab")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    app.encounters.saveAll({encounter});
    app.open();
    app.select("aria");

    QWidget* card = app.find<QWidget>("selectedName")->parentWidget();
    auto rowOf = [card](QWidget* widget) { return widget->mapTo(card, widget->rect().center()).y(); };
    auto* hp = app.find<QSpinBox>("hpField");
    auto* temp = app.find<QSpinBox>("tempHpField");
    auto* exhaustion = app.find<QComboBox>("exhaustionField");
    auto* effects = app.find<QLabel>("exhaustionEffects");
    const int nameRow = rowOf(app.find<QWidget>("selectedName"));
    CHECK(std::abs(rowOf(hp) - nameRow) < 12);
    CHECK(std::abs(rowOf(temp) - nameRow) < 12);
    QWidget* conditionsTab = app.find<QWidget>("conditionText")->parentWidget();
    CHECK(conditionsTab->isAncestorOf(exhaustion));
    CHECK(conditionsTab->isAncestorOf(effects));
    CHECK(card->isAncestorOf(app.find<QWidget>("bloodiedTag")));
    bool tempLabel = false;
    for (QLabel* label : card->findChildren<QLabel*>()) {
        tempLabel = tempLabel || (label->text() == QStringLiteral("Temp\nHP") && label->isVisible());
    }
    CHECK(tempLabel);

    CHECK_EQ(exhaustion->count(), 7);
    CHECK(effects->text() == QStringLiteral("No effects."));
    exhaustion->setCurrentIndex(2);
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "aria").exhaustion, 2);
    CHECK(effects->text().startsWith(QStringLiteral("D20 Tests -4, Speed -10 ft.")));
    app.find<QPushButton>("undoFight")->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "aria").exhaustion, 0);
    CHECK_EQ(exhaustion->currentIndex(), 0);
}

TEST_CASE("Whose turn and how many turns show only once a condition is timed")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    app.encounters.saveAll({encounter});
    app.open();
    app.select("aria");
    app.find<QTabWidget>("combatantTabs")->setCurrentIndex(1);
    QApplication::processEvents();

    auto* kind = app.find<QComboBox>("durationKind");
    auto* anchor = app.find<QComboBox>("durationAnchor");
    CHECK(kind->isVisible());
    CHECK(kind->currentIndex() == 0);  // until removed
    CHECK(!anchor->isVisible());
    kind->setCurrentIndex(2);  // until the end of
    QApplication::processEvents();
    CHECK(anchor->isVisible());
    kind->setCurrentIndex(0);
    QApplication::processEvents();
    CHECK(!anchor->isVisible());
}

TEST_CASE("Conditions show as coloured tags: click one for its rules, x removes it")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    ActiveCondition prone;
    prone.id = "prone";
    ActiveCondition poisoned;
    poisoned.id = "poisoned";
    encounter.combatants[0].conditions = {prone, poisoned};
    app.encounters.saveAll({encounter});
    app.open();
    app.select("aria");
    app.find<QTabWidget>("combatantTabs")->setCurrentIndex(1);
    QApplication::processEvents();

    auto chips = [&app] {
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QList<QFrame*> shown;
        for (QFrame* chip : app.window->findChildren<QFrame*>(QStringLiteral("conditionChip"))) {
            if (!chip->isHidden()) {
                shown.push_back(chip);
            }
        }
        return shown;
    };
    CHECK_EQ(chips().size(), qsizetype{2});
    CHECK(chips()[0]->property("selected").toBool());  // the first one's rules show
    // The tags are on the card above the tabs, not inside them.
    auto* tabs = app.find<QTabWidget>("combatantTabs");
    CHECK(!tabs->isAncestorOf(chips()[0]));
    CHECK(chips()[0]->mapTo(app.window.get(), QPoint()).y() < tabs->mapTo(app.window.get(), QPoint()).y());
    CHECK(app.find<QLabel>("conditionHeading")->text() == QStringLiteral("Prone"));
    const QString proneRules = app.find<QLabel>("conditionText")->text();
    CHECK(!proneRules.isEmpty());
    // Different conditions, different colours.
    CHECK(chips()[0]->styleSheet() != chips()[1]->styleSheet());

    chips()[1]->findChild<QPushButton*>(QStringLiteral("conditionChipName"))->click();
    QApplication::processEvents();
    CHECK(chips()[1]->property("selected").toBool());
    CHECK(chips()[1]->property("conditionId").toString() == QStringLiteral("poisoned"));
    CHECK(app.find<QLabel>("conditionText")->text() != proneRules);
    CHECK(app.find<QLabel>("conditionHeading")->text() == QStringLiteral("Poisoned"));

    chips()[0]->findChild<QToolButton*>(QStringLiteral("removeConditionChip"))->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "aria").conditions.size(), std::size_t{1});
    CHECK_EQ(chips().size(), qsizetype{1});
    chips()[0]->findChild<QToolButton*>(QStringLiteral("removeConditionChip"))->click();
    QApplication::processEvents();
    CHECK(chips().isEmpty());
    CHECK(app.find<QWidget>("combatantConditions")->isHidden());
    CHECK(app.find<QLabel>("conditionHeading")->isHidden());
}

TEST_CASE("Action buttons sit in a column on the left of their text and stay on the card")
{
    App app;
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Hill";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "frost-giant"), "giant"));
    encounter.combatants[0].initiative = 20;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->resize(1000, 900);  // a narrow card
    app.select("giant");
    QApplication::processEvents();

    auto* tabs = app.find<QTabWidget>("combatantTabs");
    std::vector<int> lefts;
    for (QPushButton* button : app.window->findChildren<QPushButton*>()) {
        const QString name = button->objectName();
        if (!button->isVisible() || (name != QStringLiteral("rollAttackDamage") && name != QStringLiteral("featureTarget") &&
                                     name != QStringLiteral("featureUse"))) {
            continue;
        }
        const QRect onTabs(button->mapTo(tabs, QPoint()), button->size());
        CHECK(tabs->rect().contains(onTabs));
        lefts.push_back(onTabs.left());
        // Its title is to its right.
        for (QLabel* label : button->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (label->isVisible() && label->font().bold()) {
                CHECK(label->mapTo(tabs, QPoint()).x() > onTabs.right());
            }
        }
    }
    CHECK(lefts.size() >= std::size_t{4});
    CHECK(std::all_of(lefts.begin(), lefts.end(), [&lefts](int x) { return x == lefts.front(); }));
}

TEST_CASE("The card shows Armor Class inside a shield, and the turn order's AC heading sits on a small one")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    app.encounters.saveAll({encounter});
    app.open();
    app.select("aria");

    auto* shield = app.find<QLabel>("acShield");
    CHECK(shield->isVisible());
    CHECK(!shield->pixmap().isNull());
    CHECK(shield->accessibleName() == QStringLiteral("AC 12"));
    CHECK(shield->toolTip() == QStringLiteral("Armor Class 12"));
    auto* list = app.find<QTreeWidget>("initiativeList");
    CHECK(list->headerItem()->text(2) == QStringLiteral("AC"));
    CHECK(list->headerItem()->toolTip(2) == QStringLiteral("Armor Class"));
    CHECK(list->header()->sectionSize(2) >= 30);
}

TEST_CASE("The Party list's HP follows the sheet, rests, and the fight")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Shore";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->showPage(ui::MainWindow::CharactersIndex);
    QApplication::processEvents();

    auto* list = app.find<QListWidget>("characterList");
    auto rowText = [list] { return list->item(0)->text(); };
    CHECK(rowText().contains(QStringLiteral("12 / 12")));

    // Typed on the sheet.
    auto* current = app.find<QSpinBox>("hpCurrent");
    current->setValue(7);
    QApplication::processEvents();
    CHECK(rowText().contains(QStringLiteral("7 / 12")));
    // Typed digit by digit in the box, as a person does.
    current->findChild<QLineEdit*>()->setText(QStringLiteral("5"));
    QApplication::processEvents();
    CHECK(rowText().contains(QStringLiteral("5 / 12")));
    app.find<QSpinBox>("hpMax")->setValue(20);
    QApplication::processEvents();
    CHECK(rowText().contains(QStringLiteral("5 / 20")));

    // Changed in the fight, then back on the Characters page.
    app.window->showPage(ui::MainWindow::DashboardIndex);
    QApplication::processEvents();
    app.select("aria");
    auto* hp = app.find<QSpinBox>("hpField");
    hp->setValue(2);
    QApplication::processEvents();
    app.window->showPage(ui::MainWindow::CharactersIndex);
    QApplication::processEvents();
    CHECK(rowText().contains(QStringLiteral("2 / 20")));

    // A Long Rest brings it back to full.
    app.find<QPushButton>("longRest")->click();
    QApplication::processEvents();
    CHECK(rowText().contains(QStringLiteral("20 / 20")));
}

TEST_CASE("Opening the app again returns to the encounter used last")
{
    App app;
    Encounter first;
    first.id = "first";
    first.name = "Road";
    Encounter second;
    second.id = "second";
    second.name = "Cave";
    app.encounters.saveAll({first, second});
    QTemporaryDir folder;
    const QString options = folder.filePath(QStringLiteral("options.ini"));

    app.open();
    app.window->setOptionsFile(options);
    QApplication::processEvents();
    auto* combo = app.find<QComboBox>("encounterCombo");
    CHECK(combo->currentData().toString() == QStringLiteral("first"));
    combo->setCurrentIndex(combo->findData(QStringLiteral("second")));
    QApplication::processEvents();

    // Quit and start again with the same options file.
    app.window.reset();
    app.open();
    app.window->setOptionsFile(options);
    QApplication::processEvents();
    combo = app.find<QComboBox>("encounterCombo");
    CHECK(combo->currentData().toString() == QStringLiteral("second"));
    app.window->showPage(ui::MainWindow::EncounterBuilderIndex);
    QApplication::processEvents();
    CHECK_EQ(app.find<QListWidget>("encounterList")->currentRow(), 1);
}

TEST_CASE("A condition's tag shows only its name; its cause and duration are highlighted under its heading")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Bog";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 20;
    ActiveCondition poisoned;
    poisoned.id = "poisoned";
    poisoned.source = "Vile Appearance";
    encounter.combatants[0].conditions = {poisoned};
    app.encounters.saveAll({encounter});
    app.open();
    app.select("aria");
    app.find<QTabWidget>("combatantTabs")->setCurrentIndex(1);
    QApplication::processEvents();

    QPushButton* tag = nullptr;
    for (QPushButton* candidate : app.window->findChildren<QPushButton*>(QStringLiteral("conditionChipName"))) {
        if (!candidate->isHidden()) {
            tag = candidate;
        }
    }
    CHECK(tag != nullptr);
    CHECK(tag->text() == QStringLiteral("Poisoned"));
    CHECK(tag->toolTip().contains(QStringLiteral("Vile Appearance")));
    CHECK(app.find<QLabel>("conditionHeading")->text() == QStringLiteral("Poisoned"));
    auto* detail = app.find<QLabel>("conditionDetail");
    CHECK(detail->isVisible());
    CHECK(detail->text() == QStringLiteral("Vile Appearance"));
    // Under the heading, above the rules.
    const int detailY = detail->mapTo(app.window.get(), QPoint()).y();
    CHECK(detailY > app.find<QLabel>("conditionHeading")->mapTo(app.window.get(), QPoint()).y());
    CHECK(detailY < app.find<QLabel>("conditionText")->mapTo(app.window.get(), QPoint()).y());
}

TEST_CASE("Initiative is rolled from the initiative list, and Reset in Encounter Builder goes back to it")
{
    App app;
    Character aria = fighter();
    Character bryn = fighter();
    bryn.id = "bryn-sheet";
    bryn.name = "Bryn";
    app.characters.saveAll({aria, bryn});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Road";
    encounter.started = false;
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeCharacterCombatant(bryn, "bryn"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    app.encounters.saveAll({encounter});
    app.open();

    // Both roll buttons are on the initiative list, not the Turn order.
    auto* entry = app.find<QWidget>("initiativeEntry");
    CHECK(entry->isVisible());
    CHECK(entry->isAncestorOf(app.find<QPushButton>("rollAllMonsters")));
    CHECK(entry->isAncestorOf(app.find<QPushButton>("rollAllCharacters")));
    CHECK(app.find<QPushButton>("rollAllCharacters")->text() == QStringLiteral("Roll player initiative"));
    CHECK(app.find<QLabel>("initiativeEntryMonsterCount")->text() == QStringLiteral("not rolled yet"));

    app.find<QPushButton>("rollAllCharacters")->click();
    QApplication::processEvents();
    Encounter fight = app.saved();
    bool logged = false;
    auto* log = app.find<QListWidget>("fightLog");
    for (int i = 0; i < log->count(); ++i) {
        logged = logged || log->item(i)->text().contains(QStringLiteral("Rolled initiative for the players"));
    }
    CHECK(logged);
    CHECK_EQ(App::in(fight, "goblin").initiative, 0);  // monsters are rolled on their own
    app.find<QPushButton>("rollAllMonsters")->click();
    QApplication::processEvents();
    CHECK(app.find<QLabel>("initiativeEntryMonsterCount")->text() == QStringLiteral("1 of 1 rolled"));

    // Start, then Reset in Encounter Builder: everyone's initiative is cleared
    // and the Dashboard opens on the initiative list.
    app.find<QPushButton>("nextTurn")->click();
    QApplication::processEvents();
    CHECK(app.saved().started);
    app.select("goblin");
    CHECK(!entry->isVisible());
    app.window->showPage(ui::MainWindow::EncounterBuilderIndex);
    QApplication::processEvents();
    answerNextBox(QMessageBox::Yes);
    app.find<QPushButton>("resetEncounter")->click();
    QApplication::processEvents();
    fight = app.saved();
    CHECK(!fight.started);
    for (const Combatant& combatant : fight.combatants) {
        CHECK_EQ(combatant.initiative, 0);
    }
    app.window->showPage(ui::MainWindow::DashboardIndex);
    QApplication::processEvents();
    CHECK(entry->isVisible());
    CHECK(app.find<QLabel>("initiativeEntryMonsterCount")->text() == QStringLiteral("not rolled yet"));
}

TEST_CASE("A long turn order takes height from the log, down to three lines")
{
    auto logHeightFor = [](int goblins, int* listHeight, int* rowsShown) {
        App app;
        Encounter encounter;
        encounter.id = "fight";
        encounter.name = "Horde";
        for (int i = 0; i < goblins; ++i) {
            encounter.combatants.push_back(
                makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "g" + std::to_string(i)));
            encounter.combatants.back().initiative = 10 + i;
        }
        encounter.started = true;
        app.encounters.saveAll({encounter});
        app.open();
        app.window->resize(1200, 900);
        for (int i = 0; i < 5; ++i) {
            QApplication::sendPostedEvents();
            QApplication::processEvents();
        }
        auto* list = app.find<QTreeWidget>("initiativeList");
        *listHeight = list->height();
        int shown = 0;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            const QRect rect = list->visualItemRect(list->topLevelItem(i));
            shown += list->viewport()->rect().contains(rect) ? 1 : 0;
        }
        *rowsShown = shown;
        auto* log = app.find<QListWidget>("fightLog");
        CHECK(log->height() >= log->minimumHeight());
        return log->height();
    };
    int fewList = 0;
    int fewRows = 0;
    int manyList = 0;
    int manyRows = 0;
    const int fewLog = logHeightFor(3, &fewList, &fewRows);
    const int manyLog = logHeightFor(34, &manyList, &manyRows);
    CHECK_EQ(fewRows, 3);
    CHECK(manyLog < fewLog);        // the log gave up height
    CHECK(manyList > fewList);      // to the turn order
    CHECK(manyRows > 24);           // which shows most of the rows
    CHECK(manyLog < fewLog * 2 / 3); // down to its three-line minimum
}

TEST_CASE("The Vampire's Multiattack allows two Grave Strikes and Bite, also in a fight saved before the fix")
{
    App app;
    Monster vampire = srd(app.srdMonsters, "vampire");
    const MonsterAttack* multi = nullptr;
    for (const MonsterAttack& attack : vampire.attacks) {
        if (isMultiattack(attack)) {
            multi = &attack;
        }
    }
    CHECK(multi != nullptr);
    CHECK_EQ(multi->count, 3);
    for (const MonsterAttack& attack : vampire.attacks) {
        if (!isMultiattack(attack)) {
            CHECK(attack.inMultiattack);
        }
    }

    // A fight saved with the old reading: Multiattack of 1, nothing in it.
    Combatant stored = makeMonsterCombatant(vampire, "vampire");
    for (MonsterAttack& attack : stored.statBlock->attacks) {
        attack.count = 1;
        attack.inMultiattack = false;
    }
    stored.initiative = 20;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Castle";
    encounter.combatants = {stored, makeCharacterCombatant(aria, "aria")};
    encounter.combatants[1].initiative = 5;
    encounter.started = true;  // the vampire's turn
    encounter.turnIndex = 0;
    app.encounters.saveAll({encounter});
    app.open();
    app.select("vampire");

    app.button(QStringLiteral("Multiattack"))->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "vampire").economy.attacksRemaining, 3);
    int usable = 0;
    for (const char* name : {"rollAttackDamage"}) {
        for (QPushButton* button : app.window->findChildren<QPushButton*>(QString::fromLatin1(name))) {
            usable += button->isVisible() && button->isEnabled() && button->text() != QStringLiteral("Multiattack") ? 1 : 0;
        }
    }
    CHECK_EQ(usable, 2);  // Grave Strike and Bite
}

TEST_CASE("A condition's rules are its SRD text once, without the tags repeating it")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Field";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    ActiveCondition prone;
    prone.id = "prone";
    encounter.combatants[0].conditions = {prone};
    app.encounters.saveAll({encounter});
    app.open();
    app.select("aria");
    const QString rules = app.find<QLabel>("conditionText")->text();
    CHECK(!rules.isEmpty());
    CHECK_EQ(rules.count(QStringLiteral("You have Disadvantage on attack rolls.")), qsizetype{1});
    CHECK(!rules.contains(QStringLiteral("•")));
}

TEST_CASE("Undo and the log carry over when the app is closed and opened again")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Road";
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 10;
    app.encounters.saveAll({encounter});
    QTemporaryDir folder;
    const QString history = folder.filePath(QStringLiteral("history.json"));

    auto openWithHistory = [&app, &history] {
        app.open();
        app.window->setHistoryFile(history);
        QApplication::processEvents();
    };
    auto quit = [&app] {
        app.window->close();
        QApplication::processEvents();
        app.window.reset();
    };
    auto logHas = [&app](const QString& text) {
        auto* log = app.find<QListWidget>("fightLog");
        for (int i = 0; i < log->count(); ++i) {
            if (log->item(i)->text().contains(text)) {
                return true;
            }
        }
        return false;
    };

    openWithHistory();
    app.select("goblin");
    app.find<QSpinBox>("damageAmount")->setValue(3);
    app.find<QPushButton>("applyDamage")->click();
    QApplication::processEvents();
    app.find<QSpinBox>("damageAmount")->setValue(2);
    app.find<QPushButton>("applyDamage")->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "goblin").hp, 5);
    quit();

    // Next run: the log is back and both steps undo.
    openWithHistory();
    CHECK(app.find<QPushButton>("undoFight")->isEnabled());
    CHECK(logHas(QStringLiteral("Goblin")));
    app.find<QPushButton>("undoFight")->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "goblin").hp, 7);
    app.find<QPushButton>("undoFight")->click();
    QApplication::processEvents();
    CHECK_EQ(App::in(app.saved(), "goblin").hp, 10);
    CHECK(!app.find<QPushButton>("undoFight")->isEnabled());
    quit();

    // A fight changed while the app was closed loses its history.
    openWithHistory();
    app.select("goblin");
    app.find<QSpinBox>("damageAmount")->setValue(4);
    app.find<QPushButton>("applyDamage")->click();
    QApplication::processEvents();
    quit();
    std::vector<Encounter> files = app.encounters.loadAll();
    files[0].round = 3;  // edited elsewhere
    app.encounters.saveAll(files);
    openWithHistory();
    CHECK(!app.find<QPushButton>("undoFight")->isEnabled());
    CHECK(!logHas(QStringLiteral("Goblin")));
}

TEST_CASE("Nimble Escape offers Disengage and Hide, each with its rules and its result")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Woods";
    encounter.combatants.push_back(makeMonsterCombatant(srd(app.srdMonsters, "goblin-warrior"), "goblin"));
    encounter.combatants.push_back(makeCharacterCombatant(aria, "aria"));
    encounter.combatants[0].initiative = 15;
    encounter.combatants[1].initiative = 5;
    encounter.turnIndex = 0;  // the goblin's turn
    app.encounters.saveAll({encounter});
    app.open();
    app.select("goblin");

    auto buttonFor = [&app](const QString& action) -> QPushButton* {
        for (QPushButton* button : app.window->findChildren<QPushButton*>(QStringLiteral("standardAction"))) {
            if (button->isVisible() && button->property("action").toString() == action) {
                return button;
            }
        }
        return nullptr;
    };
    CHECK(buttonFor(QStringLiteral("Disengage")) != nullptr);
    CHECK(buttonFor(QStringLiteral("Hide")) != nullptr);
    int rules = 0;
    for (QLabel* label : app.window->findChildren<QLabel*>(QStringLiteral("standardActionRules"))) {
        rules += label->isVisible() ? 1 : 0;
    }
    CHECK_EQ(rules, 2);

    // Disengage: the bonus action is spent and it shows until its turn ends.
    buttonFor(QStringLiteral("Disengage"))->click();
    QApplication::processEvents();
    Combatant goblin = App::in(app.saved(), "goblin");
    CHECK(goblin.economy.bonusActionUsed);
    CHECK(app.row("initiativeList", "goblin")->text(4).contains(QStringLiteral("Disengage")));
    CHECK(!buttonFor(QStringLiteral("Hide"))->isEnabled());  // one bonus action
    app.find<QPushButton>("undoFight")->click();
    QApplication::processEvents();

    // Hide: a Stealth check at the top; Hidden gives Invisible from Hide.
    buttonFor(QStringLiteral("Hide"))->click();
    QApplication::processEvents();
    CHECK(app.find<QWidget>("promptPanel")->isVisible());
    CHECK(app.find<QLabel>("promptText")->text().contains(QStringLiteral("Stealth")));
    CHECK(app.find<QLabel>("promptText")->text().contains(QStringLiteral("+6")));
    app.find<QPushButton>("promptPassed")->click();
    QApplication::processEvents();
    goblin = App::in(app.saved(), "goblin");
    CHECK(hasCondition(goblin, "invisible"));
    bool fromHide = false;
    for (const ActiveCondition& condition : goblin.conditions) {
        fromHide = fromHide || (condition.id == "invisible" && condition.source == "Hide");
    }
    CHECK(fromHide);
}

TEST_CASE("The Vampire's Charm casts Charm Person: a Wisdom save against Charmed, with Advantage when ticked")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Castle";
    encounter.combatants = {makeMonsterCombatant(srd(app.srdMonsters, "vampire"), "vampire"),
                            makeCharacterCombatant(aria, "aria")};
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 5;
    encounter.turnIndex = 0;
    app.encounters.saveAll({encounter});
    app.open();
    app.select("vampire");

    QCheckBox* advantage = nullptr;
    for (QCheckBox* box : app.window->findChildren<QCheckBox*>(QStringLiteral("saveAdvantageChoice"))) {
        advantage = box->isVisible() ? box : advantage;
    }
    CHECK(advantage != nullptr);
    CHECK(advantage->accessibleName().contains(QStringLiteral("fighting it")));
    advantage->setChecked(true);

    QPushButton* charm = nullptr;
    for (QPushButton* button : app.window->findChildren<QPushButton*>(QStringLiteral("featureTarget"))) {
        for (QLabel* label : button->parentWidget()->parentWidget()->findChildren<QLabel*>()) {
            if (button->isVisible() && label->property("featureName").toString().startsWith(QStringLiteral("Charm"))) {
                charm = button;
            }
        }
    }
    CHECK(charm != nullptr);
    charm->click();
    QApplication::processEvents();
    app.clickTarget("aria");
    app.answer("promptRoll");
    auto* log = app.find<QListWidget>("fightLog");
    bool saved = false;
    bool withAdvantage = false;
    for (int i = 0; i < log->count(); ++i) {
        saved = saved || log->item(i)->text().contains(QStringLiteral("WIS save"), Qt::CaseInsensitive);
        withAdvantage = withAdvantage || log->item(i)->text().contains(QStringLiteral("saves with Advantage"));
    }
    CHECK(saved);
    CHECK(withAdvantage);
    CHECK(App::in(app.saved(), "vampire").economy.bonusActionUsed);
}

TEST_CASE("Invisibility cast on the caster stays, and the caster concentrates on it")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Lair";
    encounter.combatants = {makeMonsterCombatant(srd(app.srdMonsters, "adult-blue-dragon"), "dragon"),
                            makeCharacterCombatant(aria, "aria")};
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 5;
    encounter.turnIndex = 0;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChildren<ui::CombatPage*>().front()->setAutoPass(false);
    app.select("dragon");
    app.button(QStringLiteral("Invisibility"))->click();
    QApplication::processEvents();
    app.clickTarget("dragon");
    const Combatant dragon = App::in(app.saved(), "dragon");
    CHECK(hasCondition(dragon, "invisible"));
    CHECK_EQ(dragon.concentration, std::string("invisibility"));
    auto* log = app.find<QListWidget>("fightLog");
    bool lost = false;
    for (int i = 0; i < log->count(); ++i) {
        lost = lost || log->item(i)->text().contains(QStringLiteral("no longer"));
        lost = lost || log->item(i)->text().contains(QStringLiteral("concentration ends"));
    }
    CHECK(!lost);
}

TEST_CASE("Invisibility on another creature ends when the caster stops concentrating")
{
    App app;
    Character aria = fighter();
    app.characters.saveAll({aria});
    Encounter encounter;
    encounter.id = "fight";
    encounter.name = "Lair";
    encounter.combatants = {makeMonsterCombatant(srd(app.srdMonsters, "adult-blue-dragon"), "dragon"),
                            makeCharacterCombatant(aria, "aria")};
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 5;
    encounter.turnIndex = 0;
    app.encounters.saveAll({encounter});
    app.open();
    app.window->findChildren<ui::CombatPage*>().front()->setAutoPass(false);
    app.select("dragon");
    app.button(QStringLiteral("Invisibility"))->click();
    QApplication::processEvents();
    app.clickTarget("aria");

    Combatant ally = App::in(app.saved(), "aria");
    Combatant dragon = App::in(app.saved(), "dragon");
    CHECK(hasCondition(ally, "invisible"));
    CHECK(!hasCondition(dragon, "invisible"));
    CHECK_EQ(dragon.concentration, std::string("invisibility"));

    app.select("dragon");
    app.find<QPushButton>("clearConcentration")->click();
    QApplication::processEvents();
    ally = App::in(app.saved(), "aria");
    dragon = App::in(app.saved(), "dragon");
    CHECK(!hasCondition(ally, "invisible"));
    CHECK(dragon.concentration.empty());
    auto* log = app.find<QListWidget>("fightLog");
    bool ended = false;
    for (int i = 0; i < log->count(); ++i) {
        ended = ended || log->item(i)->text().contains(QStringLiteral("Aria is no longer Invisible"));
    }
    CHECK(ended);
}
