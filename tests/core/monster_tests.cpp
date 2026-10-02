#include "core/monster.h"
#include "core/monster_catalog.h"
#include "test_harness.h"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

using namespace combat;

namespace {

Monster makeMonster(std::string id, std::string name, std::string type, std::string cr, std::string source)
{
    Monster monster;
    monster.id = std::move(id);
    monster.name = std::move(name);
    monster.size = "Small";
    monster.creatureType = std::move(type);
    monster.ac = 15;
    monster.hp = 10;
    monster.hitDice = "3d6";
    monster.speed = "30 ft.";
    monster.initiativeBonus = 2;
    monster.abilities = {8, 15, 10, 10, 8, 8};
    monster.passivePerception = 9;
    monster.challengeRating = std::move(cr);
    monster.source = std::move(source);
    return monster;
}

Monster goblin()
{
    return makeMonster("goblin-warrior", "Goblin Warrior", "Fey (Goblinoid)", "1/4", kSrdMonsterSource);
}

Monster wolf()
{
    return makeMonster("wolf", "Wolf", "Beast", "1/4", kSrdMonsterSource);
}

std::vector<std::string> namesOf(const std::vector<Monster>& monsters)
{
    std::vector<std::string> names;
    names.reserve(monsters.size());
    for (const Monster& monster : monsters) {
        names.push_back(monster.name);
    }
    return names;
}

}  // namespace

TEST_CASE("blank monster query lists every monster sorted by name")
{
    const std::vector<Monster> monsters{wolf(), goblin()};
    const auto found = searchMonsters(monsters, {});
    CHECK(namesOf(found) == std::vector<std::string>({"Goblin Warrior", "Wolf"}));
}

TEST_CASE("monster name search is a case-insensitive substring")
{
    const std::vector<Monster> monsters{goblin(), wolf()};
    MonsterQuery query;
    query.nameSubstring = "GOBLIN";
    const auto found = searchMonsters(monsters, query);
    CHECK_EQ(found.size(), std::size_t{1});
    CHECK_EQ(found[0].id, std::string("goblin-warrior"));

    query.nameSubstring = "zzz";
    CHECK(searchMonsters(monsters, query).empty());
}

TEST_CASE("monster name search does not look at type or challenge rating")
{
    const std::vector<Monster> monsters{wolf()};
    MonsterQuery query;
    query.nameSubstring = "beast";
    CHECK(searchMonsters(monsters, query).empty());
    query.nameSubstring = "1/4";
    CHECK(searchMonsters(monsters, query).empty());
}

TEST_CASE("creature type and challenge rating filters are exact")
{
    Monster hobgoblin = makeMonster("hobgoblin-warrior", "Hobgoblin Warrior", "Fey (Goblinoid)", "1/2", kSrdMonsterSource);
    const std::vector<Monster> monsters{goblin(), wolf(), hobgoblin};

    MonsterQuery typeOnly;
    typeOnly.creatureType = "Fey (Goblinoid)";
    CHECK(namesOf(searchMonsters(monsters, typeOnly)) ==
          std::vector<std::string>({"Goblin Warrior", "Hobgoblin Warrior"}));

    MonsterQuery notAPrefix;
    notAPrefix.creatureType = "Fey";
    CHECK(searchMonsters(monsters, notAPrefix).empty());

    MonsterQuery crOnly;
    crOnly.challengeRating = "1/4";
    CHECK(namesOf(searchMonsters(monsters, crOnly)) == std::vector<std::string>({"Goblin Warrior", "Wolf"}));

    MonsterQuery both;
    both.nameSubstring = "gob";
    both.creatureType = "Fey (Goblinoid)";
    both.challengeRating = "1/4";
    const auto found = searchMonsters(monsters, both);
    CHECK_EQ(found.size(), std::size_t{1});
    CHECK_EQ(found[0].id, std::string("goblin-warrior"));
}

TEST_CASE("equal names put the SRD row before the custom row")
{
    Monster homebrew = goblin();
    homebrew.id = "11111111-1111-4111-8111-111111111111";
    homebrew.source = kCustomMonsterSource;
    homebrew.hp = 30;

    MergedMonsterCatalog catalog({goblin()}, {homebrew});
    const auto found = catalog.search({});
    CHECK_EQ(found.size(), std::size_t{2});
    CHECK_EQ(found[0].source, std::string(kSrdMonsterSource));
    CHECK_EQ(found[1].source, std::string(kCustomMonsterSource));
    CHECK_EQ(found[1].hp, 30);

    const auto srd = catalog.findById("goblin-warrior");
    CHECK(srd.has_value());
    CHECK_EQ(srd->hp, 10);
    const auto custom = catalog.findById(homebrew.id);
    CHECK(custom.has_value());
    CHECK_EQ(custom->hp, 30);
    CHECK(!catalog.findById("missing").has_value());
}

TEST_CASE("findById prefers the SRD row when a custom id collides")
{
    Monster homebrew = goblin();
    homebrew.source = kCustomMonsterSource;
    homebrew.hp = 99;
    MergedMonsterCatalog catalog({goblin()}, {homebrew});
    const auto found = catalog.findById("goblin-warrior");
    CHECK(found.has_value());
    CHECK_EQ(found->source, std::string(kSrdMonsterSource));
    CHECK_EQ(found->hp, 10);
}

TEST_CASE("custom save rejects a blank name, an SRD source, and an SRD id")
{
    const std::unordered_set<std::string> srdIds{"goblin-warrior"};
    Monster monster = goblin();
    monster.id = "22222222-2222-4222-8222-222222222222";
    monster.source = kCustomMonsterSource;
    CHECK(validateCustomMonster(monster, srdIds).empty());

    monster.name = "  ";
    CHECK(!validateCustomMonster(monster, srdIds).empty());

    monster.name = "Goblin Warrior";
    monster.source = kSrdMonsterSource;
    CHECK(!validateCustomMonster(monster, srdIds).empty());

    monster.source = kCustomMonsterSource;
    monster.id = "goblin-warrior";
    CHECK(!validateCustomMonster(monster, srdIds).empty());

    monster.id.clear();
    CHECK(!validateCustomMonster(monster, srdIds).empty());
}
