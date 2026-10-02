#include "data/json_encounters.h"
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
    CHECK(text.find("\"schemaVersion\": 2") != std::string::npos);
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
    CHECK_THROWS(EncounterStoreError, parseEncountersDocument(R"({"schemaVersion": 3, "encounters": []})"));
    CHECK(parseEncountersDocument(R"({"schemaVersion": 2, "encounters": []})").empty());
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
