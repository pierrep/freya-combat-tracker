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
    c.hp = 32;
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
    c.hp = 45;
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

TEST_CASE("saved file has schemaVersion 1 and no derived modifiers")
{
    TempDir dir;
    JsonCharacterStore store(dir.path() / "characters.json");
    store.saveAll({aria()});
    const std::string text = readFile(store.path());
    CHECK(text.find("\"schemaVersion\": 1") != std::string::npos);
    CHECK(text.find("modifier") == std::string::npos);
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

TEST_CASE("unknown schemaVersion is refused")
{
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(R"({"schemaVersion": 2, "characters": []})"));
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(R"({"characters": []})"));
    CHECK_THROWS(CharacterStoreError, parseCharactersDocument(R"({"schemaVersion": "1", "characters": []})"));
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
    odd.hp = -4;
    odd.abilities.strength = 0;
    odd.abilities.charisma = 35;
    store.saveAll({odd});
    CHECK(store.loadAll() == std::vector<Character>{odd});
}

TEST_MAIN()
