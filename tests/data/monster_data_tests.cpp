#include "core/monster_catalog.h"
#include "data/json_monsters.h"
#include "test_harness.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>

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

Monster customMonster(std::string id, std::string name)
{
    Monster monster;
    monster.id = std::move(id);
    monster.name = std::move(name);
    monster.size = "Medium";
    monster.creatureType = "Humanoid";
    monster.ac = 12;
    monster.hp = 18;
    monster.hitDice = "4d8";
    monster.speed = "30 ft.";
    monster.initiativeBonus = 1;
    monster.abilities = {14, 12, 13, 10, 11, 8};
    monster.passivePerception = 10;
    monster.challengeRating = "1/2";
    monster.source = kCustomMonsterSource;
    return monster;
}

const char* kTinySrd = R"JSON({
  "schemaVersion": 1,
  "source": "srd-5.2.1",
  "monsters": [
    {
      "id": "goblin-warrior",
      "name": "Goblin Warrior",
      "size": "Small",
      "creatureType": "Fey (Goblinoid)",
      "ac": 15,
      "hp": 10,
      "hitDice": "3d6",
      "speed": "30 ft.",
      "initiativeBonus": 2,
      "abilities": {
        "strength": 8,
        "dexterity": 15,
        "constitution": 10,
        "intelligence": 10,
        "wisdom": 8,
        "charisma": 8
      },
      "passivePerception": 9,
      "challengeRating": "1/4",
      "source": "srd-5.2.1"
    },
    {
      "id": "wolf",
      "name": "Wolf",
      "size": "Medium",
      "creatureType": "Beast",
      "ac": 12,
      "hp": 11,
      "hitDice": "2d8 + 2",
      "speed": "40 ft.",
      "initiativeBonus": 2,
      "abilities": {
        "strength": 14,
        "dexterity": 15,
        "constitution": 12,
        "intelligence": 3,
        "wisdom": 12,
        "charisma": 6
      },
      "passivePerception": 15,
      "challengeRating": "1/4",
      "source": "srd-5.2.1"
    }
  ]
})JSON";

const char* kAttribution =
    "This work includes material from the System Reference Document 5.2.1 (\u201cSRD 5.2.1\u201d) by Wizards of "
    "the Coast LLC, available at https://www.dndbeyond.com/srd. The SRD 5.2.1 is licensed under the Creative "
    "Commons Attribution 4.0 International License, available at https://creativecommons.org/licenses/by/4.0/legalcode.";

}  // namespace

TEST_CASE("packaged SRD catalog loads, and every row is srd-5.2.1")
{
    const fs::path srdDir{COMBAT_TRACKER_SRD_DIR};
    const auto monsters = loadSrdMonsters(srdDir / "monsters.json");
    CHECK_EQ(monsters.size(), std::size_t{330});
    for (const Monster& monster : monsters) {
        CHECK_EQ(monster.source, std::string(kSrdMonsterSource));
        CHECK(!monster.id.empty());
        CHECK(!monster.name.empty());
    }

    MergedMonsterCatalog catalog(monsters);
    const auto goblin = catalog.findById("goblin-warrior");
    CHECK(goblin.has_value());
    CHECK_EQ(goblin->name, std::string("Goblin Warrior"));
    CHECK_EQ(goblin->size, std::string("Small"));
    CHECK_EQ(goblin->creatureType, std::string("Fey (Goblinoid)"));
    CHECK_EQ(goblin->ac, 15);
    CHECK_EQ(goblin->hp, 10);
    CHECK_EQ(goblin->hitDice, std::string("3d6"));
    CHECK_EQ(goblin->speed, std::string("30 ft."));
    CHECK_EQ(goblin->initiativeBonus, 2);
    CHECK_EQ(goblin->abilities.strength, 8);
    CHECK_EQ(goblin->abilities.dexterity, 15);
    CHECK_EQ(goblin->abilities.constitution, 10);
    CHECK_EQ(goblin->abilities.intelligence, 10);
    CHECK_EQ(goblin->abilities.wisdom, 8);
    CHECK_EQ(goblin->abilities.charisma, 8);
    CHECK_EQ(goblin->passivePerception, 9);
    CHECK_EQ(goblin->challengeRating, std::string("1/4"));
    CHECK(!catalog.findById("not-a-monster").has_value());

    MonsterQuery byName;
    byName.nameSubstring = "ABOLETH";
    const auto named = catalog.search(byName);
    CHECK_EQ(named.size(), std::size_t{1});
    CHECK_EQ(named[0].id, std::string("aboleth"));

    byName.nameSubstring = "gObLiN w";
    bool sawGoblinByName = false;
    for (const Monster& monster : catalog.search(byName)) {
        std::string lower = monster.name;
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        CHECK(lower.find("goblin w") != std::string::npos);
        if (monster.id == "goblin-warrior") {
            sawGoblinByName = true;
        }
    }
    CHECK(sawGoblinByName);

    MonsterQuery filtered;
    filtered.creatureType = "Fey (Goblinoid)";
    filtered.challengeRating = "1/4";
    bool sawGoblin = false;
    for (const Monster& monster : catalog.search(filtered)) {
        CHECK_EQ(monster.creatureType, std::string("Fey (Goblinoid)"));
        CHECK_EQ(monster.challengeRating, std::string("1/4"));
        if (monster.id == "goblin-warrior") {
            sawGoblin = true;
        }
    }
    CHECK(sawGoblin);

    const std::string attribution = readFile(srdDir / "ATTRIBUTION.txt");
    CHECK_EQ(attribution, std::string(kAttribution) + "\n");
}

TEST_CASE("missing custom monster file loads as an empty list")
{
    TempDir dir;
    JsonCustomMonsterStore store(dir.path() / "custom-monsters.json", {"goblin-warrior"});
    const auto loaded = store.loadAll();
    CHECK(loaded.monsters.empty());
    CHECK(loaded.skipped.empty());
    CHECK(!fs::exists(store.path()));
}

TEST_CASE("custom monsters round-trip and merge into one search list")
{
    TempDir dir;
    const auto srd = parseSrdMonsterDocument(kTinySrd);
    MergedMonsterCatalog catalog(srd);
    JsonCustomMonsterStore store(dir.path() / "custom-monsters.json", catalog.srdIds());
    const Monster home = customMonster("33333333-3333-4333-8333-333333333333", "Goblin Warrior");
    store.saveAll({home});

    JsonCustomMonsterStore reopened(store.path(), catalog.srdIds());
    const auto loaded = reopened.loadAll();
    CHECK(loaded.skipped.empty());
    CHECK(loaded.monsters == std::vector<Monster>({home}));

    catalog.setCustomMonsters(loaded.monsters);
    MonsterQuery query;
    query.nameSubstring = "goblin";
    const auto found = catalog.search(query);
    CHECK_EQ(found.size(), std::size_t{2});
    CHECK_EQ(found[0].source, std::string(kSrdMonsterSource));
    CHECK_EQ(found[1].source, std::string(kCustomMonsterSource));
    CHECK_EQ(found[1].id, home.id);

    const std::string text = readFile(store.path());
    CHECK(text.find("\"schemaVersion\": 1") != std::string::npos);
    CHECK(text.find("monsters.json") == std::string::npos);
}

TEST_CASE("custom load skips an SRD slug and an SRD source without changing the file")
{
    TempDir dir;
    const fs::path path = dir.path() / "custom-monsters.json";
    const std::string document = R"({
  "schemaVersion": 1,
  "monsters": [
    {
      "id": "goblin-warrior",
      "name": "Not the SRD goblin",
      "size": "Small",
      "creatureType": "Fey",
      "ac": 10,
      "hp": 4,
      "hitDice": "1d6",
      "speed": "30 ft.",
      "initiativeBonus": 0,
      "abilities": {"strength": 10, "dexterity": 10, "constitution": 10, "intelligence": 10, "wisdom": 10, "charisma": 10},
      "passivePerception": 10,
      "challengeRating": "0",
      "source": "custom"
    },
    {
      "id": "44444444-4444-4444-8444-444444444444",
      "name": "Smuggled",
      "size": "Medium",
      "creatureType": "Beast",
      "ac": 10,
      "hp": 4,
      "hitDice": "1d8",
      "speed": "30 ft.",
      "initiativeBonus": 0,
      "abilities": {"strength": 10, "dexterity": 10, "constitution": 10, "intelligence": 10, "wisdom": 10, "charisma": 10},
      "passivePerception": 10,
      "challengeRating": "0",
      "source": "srd-5.2.1"
    },
    {
      "id": "55555555-5555-4555-8555-555555555555",
      "name": "Kept",
      "size": "Medium",
      "creatureType": "Humanoid",
      "ac": 11,
      "hp": 5,
      "hitDice": "1d8",
      "speed": "30 ft.",
      "initiativeBonus": 1,
      "abilities": {"strength": 10, "dexterity": 12, "constitution": 10, "intelligence": 10, "wisdom": 10, "charisma": 10},
      "passivePerception": 10,
      "challengeRating": "0",
      "source": "custom"
    }
  ]
})";
    writeFile(path, document);
    JsonCustomMonsterStore store(path, {"goblin-warrior"});
    const auto loaded = store.loadAll();
    CHECK_EQ(readFile(path), document);
    CHECK_EQ(loaded.monsters.size(), std::size_t{1});
    CHECK_EQ(loaded.monsters[0].name, std::string("Kept"));
    CHECK_EQ(loaded.skipped.size(), std::size_t{2});
    CHECK(loaded.skipped[0].find("goblin-warrior") != std::string::npos);
    CHECK(loaded.skipped[1].find("44444444-4444-4444-8444-444444444444") != std::string::npos);
}

TEST_CASE("custom save rejects an SRD id and an SRD source and keeps the previous file")
{
    TempDir dir;
    const fs::path path = dir.path() / "custom-monsters.json";
    JsonCustomMonsterStore store(path, {"goblin-warrior"});
    const Monster kept = customMonster("66666666-6666-4666-8666-666666666666", "Kept");
    store.saveAll({kept});
    const std::string before = readFile(path);

    Monster colliding = kept;
    colliding.id = "goblin-warrior";
    CHECK_THROWS(MonsterDataError, store.saveAll({colliding}));
    CHECK_EQ(readFile(path), before);

    Monster sourced = kept;
    sourced.source = kSrdMonsterSource;
    CHECK_THROWS(MonsterDataError, store.saveAll({sourced}));
    CHECK_EQ(readFile(path), before);
}

TEST_CASE("unparseable custom monster file throws and is not overwritten by load")
{
    TempDir dir;
    const fs::path path = dir.path() / "custom-monsters.json";
    const std::string garbage = "{ not json";
    writeFile(path, garbage);
    JsonCustomMonsterStore store(path, {});
    CHECK_THROWS(MonsterDataError, store.loadAll());
    CHECK_EQ(readFile(path), garbage);
    CHECK_THROWS(MonsterDataError, parseCustomMonsterDocument(R"({"schemaVersion": 2, "monsters": []})", {}));
    CHECK_THROWS(MonsterDataError, parseSrdMonsterDocument(R"({"schemaVersion": 1, "source": "custom", "monsters": []})"));
}
