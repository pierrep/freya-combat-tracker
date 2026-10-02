#include "data/json_sheet.h"
#include "test_harness.h"

#include <filesystem>
#include <string>

using namespace combat;
namespace fs = std::filesystem;

TEST_CASE("packaged spell catalog loads SRD acid arrow")
{
    const fs::path path = fs::path(COMBAT_TRACKER_SRD_DIR) / "spells.json";
    const auto spells = loadSpellCatalog(path);
    CHECK_EQ(spells.size(), std::size_t{339});
    const auto acid = findSpellById(spells, "acid-arrow");
    CHECK(acid.has_value());
    CHECK_EQ(acid->name, std::string("Acid Arrow"));
    CHECK_EQ(acid->level, 2);
    CHECK_EQ(acid->school, std::string("Evocation"));
    CHECK_EQ(acid->castingTime, std::string("Action"));
    CHECK_EQ(acid->range, std::string("90 feet"));
    CHECK_EQ(acid->duration, std::string("Instantaneous"));
    CHECK(!acid->concentration);
    CHECK(acid->description.find("shimmering green arrow") != std::string::npos);

    const auto bless = findSpellById(spells, "bless");
    CHECK(bless.has_value());
    CHECK(bless->concentration);
    CHECK(bless->duration.find("Concentration") != std::string::npos);

    const auto searched = searchSpells(spells, "aCiD");
    CHECK(!searched.empty());
    CHECK_EQ(searched.front().id, std::string("acid-arrow"));
    CHECK(searchSpells(spells, "not-a-real-spell").empty());
    const auto listed = searchSpells(spells, "");
    CHECK_EQ(listed.size(), spells.size());
    CHECK_EQ(listed.front().name, std::string("Acid Arrow"));
    CHECK_EQ(listed.back().name, std::string("Zone of Truth"));
}

TEST_CASE("packaged condition catalog loads the fifteen SRD conditions")
{
    const auto conditions = loadConditionCatalog(fs::path(COMBAT_TRACKER_SRD_DIR) / "conditions.json");
    CHECK_EQ(conditions.size(), std::size_t{15});
    const auto blinded = findConditionById(conditions, "blinded");
    CHECK(blinded.has_value());
    CHECK(blinded->description.find("Can't See") != std::string::npos);
    CHECK(blinded->description.find("Advantage") != std::string::npos);
    CHECK(findConditionById(conditions, "exhaustion").has_value());
    const auto listed = searchConditions(conditions, "");
    CHECK_EQ(listed.front().name, std::string("Blinded"));
    CHECK_EQ(listed.back().name, std::string("Unconscious"));
}

TEST_CASE("packaged species catalog is the SRD name list")
{
    const auto species = loadSpeciesCatalog(fs::path(COMBAT_TRACKER_SRD_DIR) / "species.json");
    CHECK_EQ(species.size(), std::size_t{9});
    CHECK_EQ(species.front(), std::string("Dragonborn"));
    CHECK_EQ(species.back(), std::string("Tiefling"));
    const auto found = searchSpecies(species, "gnome");
    CHECK_EQ(found.size(), std::size_t{1});
    CHECK_EQ(found.front(), std::string("Gnome"));
}

TEST_CASE("catalog files refuse an unknown schema or the wrong source")
{
    CHECK_THROWS(CatalogError, parseSpellCatalog(R"({"schemaVersion": 2, "source": "srd-5.2.1", "spells": []})"));
    CHECK_THROWS(CatalogError, parseConditionCatalog(R"({"schemaVersion": 1, "source": "homebrew", "conditions": []})"));
    CHECK_THROWS(CatalogError, parseSpeciesCatalog(R"({"schemaVersion": 1, "source": "srd-5.2.1"})"));
}
