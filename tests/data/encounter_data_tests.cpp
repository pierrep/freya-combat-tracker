#include "core/combat_rules.h"
#include "data/json_encounters.h"
#include "data/json_history.h"
#include "test_harness.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

using namespace combat;
namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir()
    {
        std::random_device rd;
        m_path = fs::temp_directory_path() / ("combat-tracker-test-" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(m_path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

void writeFile(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

const char* kPlanExample = R"({
  "schemaVersion": 1,
  "encounters": [
    {
      "id": "8c1d0a22-6e4b-4f0a-9b11-2a6c5d7e8f90",
      "name": "Goblin ambush",
      "round": 1,
      "turnIndex": 0,
      "combatants": [
        {
          "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
          "source": "character",
          "sourceId": "2f0a1c5e-7b34-4d1a-9c88-6e5b0a1d44f2",
          "name": "Aria",
          "initiative": 18,
          "hp": 32,
          "ac": 16
        },
        {
          "id": "b2c3d4e5-f6a7-8901-bcde-f12345678901",
          "source": "monster",
          "sourceId": "goblin-warrior",
          "name": "Goblin Warrior 1",
          "initiative": 12,
          "initiativeBonus": 2,
          "hp": 10,
          "ac": 15
        }
      ]
    }
  ]
})";

}  // namespace

TEST_CASE("missing encounters file loads as an empty list")
{
    TempDir dir;
    JsonEncounterStore store(dir.path() / "encounters.json");
    CHECK(store.loadAll().empty());
}

TEST_CASE("plan example encounter parses and keeps the snapshotted fields")
{
    const std::vector<Encounter> encounters = parseEncountersDocument(kPlanExample);
    CHECK_EQ(encounters.size(), 1U);
    const Encounter& encounter = encounters[0];
    CHECK_EQ(encounter.id, std::string("8c1d0a22-6e4b-4f0a-9b11-2a6c5d7e8f90"));
    CHECK_EQ(encounter.name, std::string("Goblin ambush"));
    CHECK_EQ(encounter.round, 1);
    CHECK_EQ(encounter.turnIndex, 0);
    CHECK_EQ(encounter.combatants.size(), 2U);

    const Combatant& aria = encounter.combatants[0];
    CHECK_EQ(aria.source, std::string(kCombatantSourceCharacter));
    CHECK_EQ(aria.sourceId, std::string("2f0a1c5e-7b34-4d1a-9c88-6e5b0a1d44f2"));
    CHECK_EQ(aria.name, std::string("Aria"));
    CHECK_EQ(aria.initiative, 18);
    CHECK(!aria.initiativeBonus.has_value());
    CHECK_EQ(aria.hp, 32);
    CHECK(!aria.maxHp.has_value());
    CHECK_EQ(aria.tempHp, 0);
    CHECK(aria.conditions.empty());
    CHECK(aria.concentration.empty());
    CHECK_EQ(aria.deathSaves.successes, 0);
    CHECK_EQ(aria.deathSaves.failures, 0);
    CHECK_EQ(aria.ac, 16);

    const Combatant& goblin = encounter.combatants[1];
    CHECK_EQ(goblin.source, std::string(kCombatantSourceMonster));
    CHECK_EQ(goblin.sourceId, std::string("goblin-warrior"));
    CHECK_EQ(goblin.name, std::string("Goblin Warrior 1"));
    CHECK_EQ(goblin.initiative, 12);
    CHECK(goblin.initiativeBonus.has_value());
    CHECK_EQ(*goblin.initiativeBonus, 2);
    CHECK_EQ(goblin.hp, 10);
    CHECK_EQ(goblin.ac, 15);
}

TEST_CASE("encounters round-trip and a tie keeps the saved order")
{
    TempDir dir;
    const fs::path path = dir.path() / "nested" / "encounters.json";
    JsonEncounterStore store(path);

    Encounter encounter;
    encounter.id = "enc-1";
    encounter.name = "Door";
    encounter.round = 2;
    encounter.turnIndex = 1;
    Combatant first;
    first.id = "c1";
    first.source = kCombatantSourceCharacter;
    first.sourceId = "sheet-1";
    first.name = "Aria";
    first.initiative = 15;
    first.hp = 30;
    first.ac = 16;
    Combatant second;
    second.id = "c2";
    second.source = kCombatantSourceMonster;
    second.sourceId = "wolf";
    second.name = "Wolf 1";
    second.initiative = 15;
    second.initiativeBonus = 2;
    second.hp = 11;
    second.ac = 12;
    encounter.combatants = {first, second};

    store.saveAll({encounter});
    const std::string text = readFile(path);
    CHECK(text.find("\"schemaVersion\": 4") != std::string::npos);
    CHECK(text.find("\"initiativeBonus\"") != std::string::npos);
    const auto bonus = text.find("\"initiativeBonus\"");
    const auto aria = text.find("\"Aria\"");
    CHECK(aria != std::string::npos);
    CHECK(bonus != std::string::npos);
    CHECK(aria < bonus);

    const std::vector<Encounter> loaded = store.loadAll();
    CHECK_EQ(loaded.size(), 1U);
    CHECK(loaded[0] == encounter);
    CHECK(!fs::exists(path.string() + ".tmp"));
}

TEST_CASE("a monster with no initiative bonus is stored without that field")
{
    Combatant wolf;
    wolf.id = "w";
    wolf.source = kCombatantSourceMonster;
    wolf.sourceId = "wolf";
    wolf.name = "Wolf 1";
    wolf.initiative = 7;
    wolf.hp = 11;
    wolf.ac = 12;
    Encounter encounter;
    encounter.id = "enc";
    encounter.name = "Woods";
    encounter.combatants = {wolf};

    const std::string text = serializeEncountersDocument({encounter});
    CHECK(text.find("initiativeBonus") == std::string::npos);
    const Encounter loaded = parseEncountersDocument(text)[0];
    CHECK(!loaded.combatants[0].initiativeBonus.has_value());
    CHECK_EQ(loaded.combatants[0].initiative, 7);
}

TEST_CASE("unparseable encounters file throws and is not overwritten by load")
{
    TempDir dir;
    const fs::path path = dir.path() / "encounters.json";
    const std::string original = "{ not json";
    writeFile(path, original);
    JsonEncounterStore store(path);
    CHECK_THROWS(EncounterStoreError, store.loadAll());
    CHECK_EQ(readFile(path), original);
}

TEST_CASE("unknown encounters schemaVersion is refused and version 2 empty list loads")
{
    CHECK_THROWS(EncounterStoreError, parseEncountersDocument(R"({"schemaVersion": 5, "encounters": []})"));
    CHECK(parseEncountersDocument(R"({"schemaVersion": 4, "encounters": []})").empty());
    CHECK(parseEncountersDocument(R"({"schemaVersion": 2, "encounters": []})").empty());
    CHECK(parseEncountersDocument(R"({"schemaVersion": 3, "encounters": []})").empty());
}

TEST_CASE("a character initiative bonus and a bad turn index are refused")
{
    CHECK_THROWS(EncounterStoreError, parseEncountersDocument(R"({
      "schemaVersion": 1,
      "encounters": [{
        "id": "enc",
        "name": "Ambush",
        "round": 1,
        "turnIndex": 0,
        "combatants": [{
          "id": "c",
          "source": "character",
          "sourceId": "sheet",
          "name": "Aria",
          "initiative": 10,
          "initiativeBonus": 3,
          "hp": 20,
          "ac": 15
        }]
      }]
    })"));

    CHECK_THROWS(EncounterStoreError, parseEncountersDocument(R"({
      "schemaVersion": 1,
      "encounters": [{
        "id": "enc",
        "name": "Ambush",
        "round": 1,
        "turnIndex": 4,
        "combatants": []
      }]
    })"));
}

TEST_CASE("saving an invalid encounter throws and keeps the previous file")
{
    TempDir dir;
    const fs::path path = dir.path() / "encounters.json";
    JsonEncounterStore store(path);
    Encounter good;
    good.id = "enc";
    good.name = "Ambush";
    store.saveAll({good});
    const std::string original = readFile(path);

    Encounter bad = good;
    bad.name = " ";
    CHECK_THROWS(EncounterStoreError, store.saveAll({bad}));
    CHECK_EQ(readFile(path), original);

    Encounter copy = good;
    copy.id = good.id;
    CHECK_THROWS(EncounterStoreError, store.saveAll({good, copy}));
    CHECK_EQ(readFile(path), original);
}

TEST_CASE("version 3 keeps durations, save-ends, dying state, economy, uses, and the stat block")
{
    Monster monster;
    monster.id = "red";
    monster.name = "Red Dragon";
    monster.size = "Huge";
    monster.creatureType = "Dragon";
    monster.hp = 200;
    monster.ac = 19;
    monster.source = kSrdMonsterSource;
    monster.legendaryActionUses = 3;
    MonsterAttack breath;
    breath.name = "Fire Breath (Recharge 5–6)";
    breath.effect = "Dexterity Saving Throw: DC 21.";
    breath.save = SaveSpec{Ability::Dexterity, 21, true};
    breath.damage = {DamagePart{"17d6", "fire", DamageWhen::Always}};
    breath.recharge = 5;
    MonsterAttack ball;
    ball.name = "Fireball (1/Day)";
    ball.effect = "Casts it.";
    ball.perDay = 1;
    monster.attacks = {breath, ball};
    monster.defenses.immunities = {"fire"};

    Encounter encounter;
    encounter.id = "e";
    encounter.name = "Lair";
    encounter.combatants.push_back(makeMonsterCombatant(monster, "d"));
    Combatant aria;
    aria.id = "a";
    aria.source = kCombatantSourceCharacter;
    aria.sourceId = "sheet";
    aria.name = "Aria";
    aria.hp = 0;
    aria.maxHp = 30;
    aria.deathSaves = {1, 2};
    aria.exhaustion = 2;
    aria.conditions.push_back(ActiveCondition{"unconscious", std::nullopt, std::nullopt});
    aria.conditions.push_back(ActiveCondition{"poisoned", ConditionDuration{"d", TurnBoundary::End, 2, true},
                                              SaveEnds{Ability::Constitution, 13}});
    aria.conditions.push_back(ActiveCondition{"frightened", std::nullopt, SaveEnds{Ability::Wisdom, 15}});
    aria.conditions.back().saveEnds->manual = true;
    encounter.combatants.push_back(aria);
    encounter.combatants[0].expended.push_back(breath.name);
    encounter.combatants[0].usesRemaining[ball.name] = 0;
    encounter.combatants[0].economy.reactionUsed = true;

    const std::string text = serializeEncountersDocument({encounter});
    const auto loaded = parseEncountersDocument(text);
    CHECK_EQ(loaded.size(), std::size_t{1});
    CHECK(loaded[0] == encounter);
    CHECK(loaded[0].combatants[0].statBlock.has_value());
    CHECK_EQ(loaded[0].combatants[0].statBlock->attacks[0].damage[0].dice, std::string("17d6"));
}

TEST_CASE("version 2 conditions are plain ids, exhaustion becomes a level, and negative HP loads as 0")
{
    const std::string version2 = R"({"schemaVersion": 2, "encounters": [{"id": "e", "name": "Old", "round": 2,
        "turnIndex": 0, "combatants": [{"id": "g", "source": "monster", "sourceId": "goblin-warrior",
        "name": "Goblin", "initiative": 12, "initiativeBonus": 2, "hp": -3, "maxHp": 10, "tempHp": 0, "ac": 15,
        "conditions": ["prone", "exhaustion"], "concentration": "", "deathSaves": {"successes": 0, "failures": 9}}]}]})";
    const auto loaded = parseEncountersDocument(version2);
    const Combatant& goblin = loaded[0].combatants[0];
    CHECK_EQ(goblin.hp, 0);
    CHECK_EQ(goblin.conditions.size(), std::size_t{1});
    CHECK_EQ(goblin.conditions[0].id, std::string("prone"));
    CHECK_EQ(goblin.exhaustion, 1);
    CHECK_EQ(goblin.deathSaves.failures, 3);
    CHECK(!goblin.statBlock.has_value());
}

TEST_CASE("a condition keeps its cause, its end triggers, and its concentration")
{
    Encounter encounter;
    encounter.id = "e";
    encounter.name = "Marsh";
    Combatant wisp;
    wisp.id = "w";
    wisp.source = kCombatantSourceMonster;
    wisp.sourceId = "will-o-wisp";
    wisp.name = "Will-o'-Wisp";
    wisp.hp = 27;
    wisp.maxHp = 27;
    wisp.concentration = "Vanish";
    ActiveCondition vanish;
    vanish.id = "invisible";
    vanish.source = "Vanish";
    vanish.endsOn = {"attackRoll", "action:Consume Life"};
    vanish.concentration = "Vanish";
    wisp.conditions.push_back(vanish);
    encounter.combatants.push_back(wisp);
    const std::vector<Encounter> loaded = parseEncountersDocument(serializeEncountersDocument({encounter}));
    CHECK(loaded.at(0).combatants.at(0).conditions.at(0) == vanish);
    CHECK_EQ(loaded.at(0).combatants.at(0).concentration, std::string("Vanish"));
}

TEST_CASE("an Invisible saved before version 4 is treated as the Hide action's")
{
    const std::string version3 = R"({"schemaVersion": 3, "encounters": [{"id": "e", "name": "Old", "round": 1,
        "turnIndex": 0, "combatants": [{"id": "g", "source": "monster", "sourceId": "goblin-warrior",
        "name": "Goblin", "initiative": 12, "hp": 7, "ac": 15, "tempHp": 0, "maxHp": 7,
        "conditions": [{"id": "invisible"}, {"id": "prone"}], "concentration": "",
        "deathSaves": {"successes": 0, "failures": 0}}]}]})";
    const std::vector<Encounter> loaded = parseEncountersDocument(version3);
    const Combatant& goblin = loaded.at(0).combatants.at(0);
    CHECK_EQ(goblin.conditions.at(0).source, std::string("Hide"));
    CHECK(goblin.conditions.at(0).endsOn == hideEndsOn());
    CHECK(goblin.conditions.at(1).endsOn.empty());
}

TEST_CASE("the undo history round-trips through history.json, and a bad file loads as none")
{
    Character aria;
    aria.id = "aria";
    aria.name = "Aria";
    aria.hp = {12, 12};
    Monster goblin;
    goblin.id = "goblin";
    goblin.name = "Goblin";
    goblin.hp = 7;
    goblin.source = "srd-5.2.1";
    MonsterAttack scimitar;
    scimitar.name = "Scimitar";
    scimitar.attackBonus = 4;
    DamagePart slash;
    slash.dice = "1d6+2";
    slash.type = "slashing";
    scimitar.damage = {slash};
    goblin.attacks = {scimitar};

    Encounter fight;
    fight.id = "fight";
    fight.name = "Road";
    fight.combatants = {makeCharacterCombatant(aria, "a"), makeMonsterCombatant(goblin, "g")};
    Encounter before = fight;
    fight.combatants[0].hp = 5;

    HistoryPrompt prompt;
    prompt.kind = 4;
    prompt.combatantId = "a";
    prompt.sourceId = "g";
    prompt.attack = scimitar;
    prompt.riders = {0, 2};
    prompt.refund = 3;
    HistoryPrompt save;  // a breath weapon's save, its damage already rolled
    save.kind = 6;
    save.combatantId = "a";
    save.sourceId = "g";
    save.attack = scimitar;
    save.damage = {{21, "fire"}, {4, ""}};
    save.advantage = true;
    save.afterHit = true;
    save.wasBloodied = true;
    HistoryStep step;
    step.encounterId = "fight";
    step.encounter = before;
    step.roster = {aria};
    step.log = {"R1 Goblin: Scimitar hits Aria for 7."};
    step.prompts = {prompt};
    step.selectionId = "g";
    FightHistory history;
    history.roster = {aria};
    history.encounters = {fight};
    history.shownEncounterId = "fight";
    history.log = {"R1 Goblin: Scimitar hits Aria for 7.", "R1 Combat starts."};
    history.prompts = {prompt, save};
    history.steps = {step};

    const FightHistory back = parseHistory(serializeHistory(history));
    CHECK(back == history);

    TempDir dir;
    const fs::path file = dir.path() / "history.json";
    CHECK(!loadHistoryFile(file).has_value());  // missing
    saveHistoryFile(file, history);
    CHECK(loadHistoryFile(file).has_value());
    CHECK(*loadHistoryFile(file) == history);
    writeFile(file, "{ not json");
    CHECK(!loadHistoryFile(file).has_value());  // malformed: no history, no error
}
