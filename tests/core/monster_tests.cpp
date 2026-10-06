#include "core/encounter.h"
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

TEST_CASE("renaming a custom monster keeps that name when it is added")
{
    Monster monster = makeMonster("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "New monster", "Monstrosity", "3",
                                  kCustomMonsterSource);
    MergedMonsterCatalog catalog({}, {monster});
    CHECK_EQ(catalog.findById(monster.id)->name, std::string("New monster"));

    monster.name = "Cave Fisher";
    monster.hp = 58;
    CHECK(catalog.updateCustomMonster(monster));
    const auto lookedUp = catalog.findById(monster.id);
    CHECK(lookedUp.has_value());
    CHECK_EQ(lookedUp->name, std::string("Cave Fisher"));
    CHECK_EQ(lookedUp->hp, 58);

    MonsterQuery query;
    query.nameSubstring = "fisher";
    const auto matches = catalog.search(query);
    CHECK_EQ(matches.size(), std::size_t{1});
    CHECK_EQ(matches[0].name, std::string("Cave Fisher"));

    query.nameSubstring = "New monster";
    CHECK(catalog.search(query).empty());

    const Combatant added = makeMonsterCombatant(*lookedUp, "combatant-1");
    CHECK_EQ(added.name, std::string("Cave Fisher"));
    CHECK_EQ(added.sourceId, monster.id);
    CHECK(added.name.find("New monster") == std::string::npos);
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

TEST_CASE("damage parts round-trip through the one-line editor text")
{
    const std::vector<DamagePart> parts{{"1d6+2", "slashing", DamageWhen::Always},
                                        {"1d4", "slashing", DamageWhen::Advantage},
                                        {"3", "", DamageWhen::Ongoing}};
    const std::string text = formatDamageParts(parts);
    CHECK_EQ(text, std::string("1d6+2 slashing, 1d4 slashing advantage, 3 ongoing"));
    const auto parsed = parseDamageParts(text);
    CHECK(parsed.has_value());
    CHECK(*parsed == parts);
    const auto spaced = parseDamageParts("2d6 + 3 Fire");
    CHECK(spaced.has_value());
    CHECK_EQ((*spaced)[0].dice, std::string("2d6+3"));
    CHECK_EQ((*spaced)[0].type, std::string("fire"));
    std::string error;
    CHECK(!parseDamageParts("lots of fire", &error).has_value());
    CHECK(!error.empty());
    CHECK(!parseDamageParts("1d6 sparkly").has_value());
    CHECK(parseDamageParts("").value().empty());

    MonsterAttack breath;
    breath.save = SaveSpec{Ability::Dexterity, 21, true};
    breath.damage = {DamagePart{"17d6", "fire", DamageWhen::Always}};
    breath.area = true;
    breath.recharge = 5;
    CHECK_EQ(attackSummary(breath), std::string("DC 21 Dex save (half on success), 17d6 fire, area, Recharge 5-6"));
}
