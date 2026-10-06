#include "data/json_character_store.h"
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

Character aria()
{
    Character c;
    c.id = "2f0a1c5e-7b34-4d1a-9c88-6e5b0a1d44f2";
    c.name = "Aria";
    c.hp.current = 32;
    c.hp.max = 32;
    c.ac = 16;
    c.abilities = {10, 16, 14, 12, 13, 8};
    c.passivePerception = 13;
    return c;
}

Character borin()
{
    Character c;
    c.id = "9d1e2f3a-4b5c-4d6e-8f70-112233445566";
    c.name = "Borin";
    c.hp.current = 45;
    c.hp.max = 45;
    c.ac = 18;
    c.abilities = {17, 10, 16, 8, 12, 10};
    c.passivePerception = 11;
    return c;
}

const char* kPlanExample = R"({
  "schemaVersion": 1,
  "characters": [
    {
      "id": "2f0a1c5e-7b34-4d1a-9c88-6e5b0a1d44f2",
      "name": "Aria",
      "hp": 32,
      "ac": 16,
      "abilities": {
        "strength": 10,
        "dexterity": 16,
        "constitution": 14,
        "intelligence": 12,
        "wisdom": 13,
        "charisma": 8
      },
      "passivePerception": 13
    }
  ]
})";

}  // namespace

TEST_CASE("missing file loads as an empty roster")
{
    TempDir dir;
    JsonCharacterStore store(dir.path() / "characters.json");
    CHECK(store.loadAll().empty());
    CHECK(!fs::exists(store.path()));
}

TEST_CASE("save then load round-trips every field")
{
    TempDir dir;
    JsonCharacterStore store(dir.path() / "characters.json");
    const std::vector<Character> roster{aria(), borin()};
    store.saveAll(roster);

    JsonCharacterStore reopened(dir.path() / "characters.json");
    CHECK(reopened.loadAll() == roster);
}

TEST_CASE("saved file has schemaVersion 3 and no derived modifiers")
{
    TempDir dir;
    JsonCharacterStore store(dir.path() / "characters.json");
    store.saveAll({aria()});
    const std::string text = readFile(store.path());
    CHECK(text.find("\"schemaVersion\": 4") != std::string::npos);
    CHECK(text.find("\"current\": 32") != std::string::npos);
    CHECK(text.find("\"max\": 32") != std::string::npos);
    CHECK(text.find("modifier") == std::string::npos);
    CHECK(!fs::exists(dir.path() / "characters.v1.json"));
}

TEST_CASE("empty roster saves and reloads")
{
    TempDir dir;
    JsonCharacterStore store(dir.path() / "characters.json");
    store.saveAll({aria()});
    store.saveAll({});
    CHECK(store.loadAll().empty());
}

TEST_CASE("plan example document parses")
{
    const auto characters = parseCharactersDocument(kPlanExample);
    CHECK_EQ(characters.size(), std::size_t{1});
    CHECK(characters[0] == aria());
}

TEST_CASE("save creates missing folders and leaves no temp file")
{
    TempDir dir;
    const fs::path path = dir.path() / "nested" / "combat-tracker" / "characters.json";
    JsonCharacterStore store(path);
    store.saveAll({aria()});
    CHECK(fs::exists(path));

    auto temp = path;
    temp += ".tmp";
    CHECK(!fs::exists(temp));
    CHECK_EQ(fs::directory_iterator(path.parent_path())->path().filename(), fs::path("characters.json"));
}

TEST_CASE("save replaces an existing file")
{
    TempDir dir;
    JsonCharacterStore store(dir.path() / "characters.json");
    store.saveAll({aria()});
    store.saveAll({borin()});
    const auto loaded = store.loadAll();
    CHECK_EQ(loaded.size(), std::size_t{1});
    CHECK(loaded[0] == borin());
}

TEST_CASE("stale temp file from an interrupted save does not affect load or save")
{
    TempDir dir;
    const fs::path path = dir.path() / "characters.json";
    JsonCharacterStore store(path);
    store.saveAll({aria()});

    auto temp = path;
    temp += ".tmp";
    writeFile(temp, "{ half-written");
    CHECK(store.loadAll() == std::vector<Character>{aria()});

    store.saveAll({aria(), borin()});
    CHECK_EQ(store.loadAll().size(), std::size_t{2});
    CHECK(!fs::exists(temp));
}

TEST_CASE("unparseable file throws and is not overwritten by load")
{
    TempDir dir;
    const fs::path path = dir.path() / "characters.json";
    const std::string garbage = "{ not json";
    writeFile(path, garbage);
    JsonCharacterStore store(path);
    CHECK_THROWS(CharacterStoreError, store.loadAll());
    CHECK_EQ(readFile(path), garbage);
}

TEST_CASE("unknown schemaVersion is refused and version 2 and 3 empty rosters load")
{
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(R"({"schemaVersion": 5, "characters": []})"));
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(R"({"characters": []})"));
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(R"({"schemaVersion": "1", "characters": []})"));
    CHECK(parseCharactersDocument(R"({"schemaVersion": 2, "characters": []})").empty());
    CHECK(parseCharactersDocument(R"({"schemaVersion": 3, "characters": []})").empty());
    CHECK(parseCharactersDocument(R"({"schemaVersion": 4, "characters": []})").empty());
}

TEST_CASE("non-integer numeric fields are rejected")
{
    std::string doc = kPlanExample;
    doc.replace(doc.find("\"hp\": 32"), 8, "\"hp\": 32.5");
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(doc));

    doc = kPlanExample;
    doc.replace(doc.find("\"ac\": 16"), 8, "\"ac\": \"16\"");
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(doc));

    doc = kPlanExample;
    doc.replace(doc.find("\"wisdom\": 13"), 12, "\"wisdom\": 99999999999");
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(doc));
}

TEST_CASE("missing or blank name is rejected")
{
    std::string doc = kPlanExample;
    doc.replace(doc.find("\"Aria\""), 6, "\"  \"");
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(doc));

    doc = kPlanExample;
    doc.replace(doc.find("\"name\": \"Aria\","), 15, "");
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(doc));
}

TEST_CASE("missing ability score is rejected")
{
    std::string doc = kPlanExample;
    doc.replace(doc.find("\"charisma\": 8"), 13, "\"luck\": 8");
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(doc));
}

TEST_CASE("saving an invalid character throws and keeps the previous file")
{
    TempDir dir;
    const fs::path path = dir.path() / "characters.json";
    JsonCharacterStore store(path);
    store.saveAll({aria()});
    const std::string before = readFile(path);

    Character nameless = borin();
    nameless.name = "";
    CHECK_THROWS(CharacterStoreError, store.saveAll({aria(), nameless}));
    CHECK_EQ(readFile(path), before);
}

TEST_CASE("unusual scores round-trip unchanged")
{
    TempDir dir;
    JsonCharacterStore store(dir.path() / "characters.json");
    Character odd = aria();
    odd.hp.current = 0;
    odd.hp.max = 0;
    odd.abilities.strength = 0;
    odd.abilities.charisma = 35;
    store.saveAll({odd});
    CHECK(store.loadAll() == std::vector<Character>{odd});
}

TEST_CASE("version 1 hp migrates in memory and the file is not rewritten")
{
    TempDir dir;
    const fs::path path = dir.path() / "characters.json";
    writeFile(path, kPlanExample);
    JsonCharacterStore store(path);
    const auto loaded = store.loadAll();
    CHECK_EQ(loaded.size(), std::size_t{1});
    CHECK(loaded[0] == aria());
    CHECK_EQ(loaded[0].hp.current, 32);
    CHECK_EQ(loaded[0].hp.max, 32);
    CHECK_EQ(loaded[0].tempHp, 0);
    CHECK(loaded[0].classes.empty());
    CHECK(loaded[0].spells.empty());
    CHECK(loaded[0].gear.empty());
    CHECK_EQ(readFile(path), std::string(kPlanExample));
    CHECK(!fs::exists(dir.path() / "characters.v1.json"));
}

TEST_CASE("saving over a version 1 file keeps one copy and writes version 4")
{
    TempDir dir;
    const fs::path path = dir.path() / "characters.json";
    writeFile(path, kPlanExample);
    JsonCharacterStore store(path);
    const auto loaded = store.loadAll();
    store.saveAll(loaded);

    const fs::path backup = dir.path() / "characters.v1.json";
    CHECK_EQ(readFile(backup), std::string(kPlanExample));
    const std::string saved = readFile(path);
    CHECK(saved.find("\"schemaVersion\": 4") != std::string::npos);
    CHECK(saved.find("\"current\": 32") != std::string::npos);
    CHECK(saved.find("\"max\": 32") != std::string::npos);

    store.saveAll(loaded);
    CHECK_EQ(readFile(backup), std::string(kPlanExample));
}

TEST_CASE("sheet fields round-trip and descriptions are not stored")
{
    Character character = aria();
    character.species = "House elf";
    character.speed = "30 ft.";
    character.initiativeOverride = 3;
    character.proficiencyOverride = 2;
    character.tempHp = 5;
    character.classes.push_back(ClassLevel{"Wizard", 5, "Abjurer"});
    character.savingThrows.intelligence = true;
    character.skills.arcana = true;
    character.spells.push_back(CharacterSpell{"acid-arrow", "", true});
    character.spells.push_back(CharacterSpell{"", "Pocket Star", false});
    character.spellSlots.push_back(SpellSlot{1, 3, 4, false});
    character.spellSlots.push_back(SpellSlot{3, 1, 2, true});
    character.classes[0].hitDiceSpent = 2;
    character.gear.push_back(GearItem{"Longsword", 1, true});
    character.defenses.resistances = {"fire"};
    character.exhaustion = 2;
    character.notes = "Watch the door.";

    TempDir dir;
    JsonCharacterStore store(dir.path() / "characters.json");
    store.saveAll({character});
    CHECK(store.loadAll() == std::vector<Character>{character});
    const std::string text = readFile(store.path());
    CHECK(text.find("shimmering") == std::string::npos);
    CHECK(text.find("acid-arrow") != std::string::npos);
    CHECK(text.find("Pocket Star") != std::string::npos);
}

TEST_CASE("schema 2 rejects a missing field, a duplicate spell, and a slot below 1")
{
    const std::string doc = serializeCharactersDocument({aria()});
    const std::string notesField = ",\n      \"notes\": \"\"";
    const auto notes = doc.find(notesField);
    CHECK(notes != std::string::npos);
    std::string missing = doc;
    missing.erase(notes, notesField.size());
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(missing));

    Character duplicate = aria();
    duplicate.spells.push_back(CharacterSpell{"acid-arrow", "", false});
    duplicate.spells.push_back(CharacterSpell{"acid-arrow", "", true});
    CHECK(!validateCharacter(duplicate).empty());

    Character cantripSlot = aria();
    cantripSlot.spellSlots.push_back(SpellSlot{0, 1, 1});
    CHECK(!validateCharacter(cantripSlot).empty());
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(serializeCharactersDocument({cantripSlot})));

    TempDir dir;
    JsonCharacterStore store(dir.path() / "characters.json");
    store.saveAll({aria()});
    const std::string before = readFile(store.path());
    CHECK_THROWS(CharacterStoreError, store.saveAll({cantripSlot}));
    CHECK_EQ(readFile(store.path()), before);
}

TEST_MAIN()
