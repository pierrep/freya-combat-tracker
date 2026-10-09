#include "core/attack_damage.h"
#include "core/monster_catalog.h"
#include "core/spell_rules.h"
#include "data/json_monsters.h"
#include "test_harness.h"

#include <algorithm>
#include <optional>
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
    // The CC-BY statement comes first, word for word. Further credits may follow.
    CHECK_EQ(attribution.rfind(std::string(kAttribution) + "\n", 0), std::size_t{0});
}

TEST_CASE("an SRD monster exposes its attacks, including a multiattack count")
{
    const auto monsters = loadSrdMonsters(fs::path{COMBAT_TRACKER_SRD_DIR} / "monsters.json");
    const Monster* goblin = nullptr;
    const Monster* dragon = nullptr;
    for (const Monster& monster : monsters) {
        if (monster.id == "goblin-warrior") {
            goblin = &monster;
        } else if (monster.id == "adult-brass-dragon") {
            dragon = &monster;
        }
    }
    CHECK(goblin != nullptr);
    CHECK(dragon != nullptr);
    if (goblin == nullptr || dragon == nullptr) {
        return;
    }

    CHECK_EQ(goblin->attacks.size(), std::size_t{2});
    CHECK_EQ(goblin->attacks[0].name, std::string("Scimitar"));
    CHECK_EQ(goblin->attacks[0].count, 1);
    CHECK(goblin->attacks[0].effect.find("1d6 + 2") != std::string::npos);
    CHECK(goblin->attacks[0].effect.find("Slashing") != std::string::npos);
    CHECK_EQ(goblin->attacks[1].name, std::string("Shortbow"));
    CHECK_EQ(goblin->attacks[1].count, 1);
    CHECK(goblin->attacks[1].effect.find("80/320") != std::string::npos);

    const MonsterAttack* multiattack = nullptr;
    const MonsterAttack* rend = nullptr;
    for (const MonsterAttack& attack : dragon->attacks) {
        if (attack.name == "Multiattack") {
            multiattack = &attack;
        } else if (attack.name == "Rend") {
            rend = &attack;
        }
    }
    CHECK(multiattack != nullptr);
    CHECK(rend != nullptr);
    if (multiattack != nullptr) {
        CHECK_EQ(multiattack->count, 3);
        CHECK(multiattack->effect.find("three Rend attacks") != std::string::npos);
    }
    if (rend != nullptr) {
        CHECK_EQ(rend->count, 3);
        CHECK(rend->effect.find("2d10 + 6") != std::string::npos);
        CHECK(rend->effect.find("Fire") != std::string::npos);
        const std::optional<int> rolled = rollAttackDamage(rend->effect, [](int) { return 1; });
        CHECK(rolled.has_value());
        CHECK_EQ(*rolled, 1 + 1 + 6 + 1);
    }

    const MonsterAttack* sleep = nullptr;
    const MonsterAttack* breath = nullptr;
    for (const MonsterAttack& attack : dragon->attacks) {
        if (attack.name == "Sleep Breath") {
            sleep = &attack;
        } else if (attack.name == "Fire Breath (Recharge 5–6)") {
            breath = &attack;
        }
    }
    CHECK(sleep != nullptr);
    CHECK(breath != nullptr);
    if (sleep != nullptr) {
        CHECK(damageExpressions(sleep->effect).empty());
    }
    if (breath != nullptr) {
        const std::vector<DamageExpression> dice = damageExpressions(breath->effect);
        CHECK_EQ(dice.size(), std::size_t{1});
        CHECK_EQ(dice[0], (DamageExpression{10, 8, 0}));
    }
    if (multiattack != nullptr) {
        CHECK(damageExpressions(multiattack->effect).empty());
    }
}

TEST_CASE("an SRD monster exposes a trait and a legendary action, and one without them omits them")
{
    const auto monsters = loadSrdMonsters(fs::path{COMBAT_TRACKER_SRD_DIR} / "monsters.json");
    const Monster* aboleth = nullptr;
    const Monster* assassin = nullptr;
    const Monster* axeBeak = nullptr;
    for (const Monster& monster : monsters) {
        if (monster.id == "aboleth") {
            aboleth = &monster;
        } else if (monster.id == "assassin") {
            assassin = &monster;
        } else if (monster.id == "axe-beak") {
            axeBeak = &monster;
        }
    }
    CHECK(aboleth != nullptr);
    CHECK(assassin != nullptr);
    CHECK(axeBeak != nullptr);
    if (aboleth == nullptr || assassin == nullptr || axeBeak == nullptr) {
        return;
    }

    const MonsterFeature* amphibious = nullptr;
    const MonsterFeature* lash = nullptr;
    for (const MonsterFeature& feature : aboleth->traits) {
        if (feature.name == "Amphibious") {
            amphibious = &feature;
        }
    }
    for (const MonsterFeature& feature : aboleth->legendaryActions) {
        if (feature.name == "Lash") {
            lash = &feature;
        }
    }
    CHECK(amphibious != nullptr);
    CHECK(lash != nullptr);
    if (amphibious != nullptr) {
        CHECK(amphibious->effect.find("breathe air and water") != std::string::npos);
    }
    if (lash != nullptr) {
        CHECK(lash->effect.find("Tentacle") != std::string::npos);
    }
    CHECK(aboleth->bonusActions.empty());
    CHECK(aboleth->reactions.empty());

    const MonsterFeature* evasion = nullptr;
    const MonsterFeature* cunning = nullptr;
    for (const MonsterFeature& feature : assassin->traits) {
        if (feature.name == "Evasion") {
            evasion = &feature;
        }
    }
    for (const MonsterFeature& feature : assassin->bonusActions) {
        if (feature.name == "Cunning Action") {
            cunning = &feature;
        }
    }
    CHECK(evasion != nullptr);
    CHECK(cunning != nullptr);
    if (cunning != nullptr) {
        CHECK(cunning->effect.find("Disengage") != std::string::npos);
    }
    bool repeatedShortsword = false;
    for (const MonsterFeature& feature : assassin->traits) {
        if (feature.name == "Shortsword") {
            repeatedShortsword = true;
        }
    }
    for (const MonsterFeature& feature : assassin->bonusActions) {
        if (feature.name == "Shortsword") {
            repeatedShortsword = true;
        }
    }
    for (const MonsterFeature& feature : assassin->reactions) {
        if (feature.name == "Shortsword") {
            repeatedShortsword = true;
        }
    }
    for (const MonsterFeature& feature : assassin->legendaryActions) {
        if (feature.name == "Shortsword") {
            repeatedShortsword = true;
        }
    }
    CHECK(!repeatedShortsword);
    CHECK(assassin->reactions.empty());
    CHECK(assassin->legendaryActions.empty());

    CHECK(axeBeak->traits.empty());
    CHECK(axeBeak->bonusActions.empty());
    CHECK(axeBeak->reactions.empty());
    CHECK(axeBeak->legendaryActions.empty());
}

TEST_CASE("the Will-o'-Wisp's Consume Life knows its target and what a failure does")
{
    const auto monsters = loadSrdMonsters(fs::path{COMBAT_TRACKER_SRD_DIR} / "monsters.json");
    const MonsterAttack* consume = nullptr;
    for (const Monster& monster : monsters) {
        for (const MonsterFeature& feature : monster.bonusActions) {
            if (monster.id == "will-o-wisp" && feature.name == "Consume Life" && feature.targeted.has_value()) {
                consume = &*feature.targeted;
            }
        }
    }
    CHECK(consume != nullptr);
    if (consume == nullptr) {
        return;
    }
    CHECK(consume->targetAtZeroHp);
    CHECK((consume->targetExceptTypes == std::vector<std::string>{"undead", "construct"}));
    CHECK(consume->failureHpThreshold == std::optional<int>{0});
    CHECK_EQ(consume->failureHpEffect, std::string("dies"));
    CHECK_EQ(consume->failureSelfHealing, std::string("3d6"));
}

TEST_CASE("saves that only some creature types take carry those types")
{
    const auto monsters = loadSrdMonsters(fs::path{COMBAT_TRACKER_SRD_DIR} / "monsters.json");
    const auto action = [&monsters](const std::string& id, const std::string& name) -> const MonsterAttack* {
        for (const Monster& monster : monsters) {
            for (const MonsterAttack& attack : monster.attacks) {
                if (monster.id == id && attack.name.rfind(name, 0) == 0) {
                    return &attack;
                }
            }
            for (const MonsterFeature& feature : monster.legendaryActions) {
                if (monster.id == id && feature.name.rfind(name, 0) == 0 && feature.targeted.has_value()) {
                    return &*feature.targeted;
                }
            }
        }
        return nullptr;
    };
    const MonsterAttack* visage = action("ghost", "Horrific Visage");
    const MonsterAttack* possession = action("ghost", "Possession");
    const MonsterAttack* song = action("harpy", "Luring Song");
    const MonsterAttack* disrupt = action("lich", "Disrupt Life");
    CHECK(visage != nullptr);
    CHECK(possession != nullptr);
    CHECK(song != nullptr);
    CHECK(disrupt != nullptr);
    if (visage == nullptr || possession == nullptr || song == nullptr || disrupt == nullptr) {
        return;
    }
    CHECK((visage->targetExceptTypes == std::vector<std::string>{"undead"}));
    CHECK((possession->targetTypes == std::vector<std::string>{"humanoid"}));
    CHECK((song->targetTypes == std::vector<std::string>{"humanoid", "giant"}));
    CHECK((disrupt->targetExceptTypes == std::vector<std::string>{"undead"}));
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
    Monster home = customMonster("33333333-3333-4333-8333-333333333333", "Goblin Warrior");
    MonsterFeature vanish;
    vanish.name = "Vanish";
    vanish.effect = "The goblin has the Invisible condition until its Concentration ends.";
    vanish.selfEffect = SelfEffect{"invisible", "Vanish", "Vanish", {"attackRoll"}};
    vanish.perDay = 2;
    home.bonusActions.push_back(vanish);
    MonsterFeature parry;
    parry.name = "Parry (Recharge 5-6)";
    parry.effect = "The goblin adds 2 to its AC against one melee attack.";
    parry.recharge = 5;
    home.reactions.push_back(parry);
    // Conditions on a hit, a targeted bonus action, and a drain round-trip too.
    MonsterAttack grab;
    grab.name = "Grab";
    grab.effect = "Melee Attack Roll: +4. Hit: 9 (2d6 + 2) Bludgeoning damage.";
    grab.attackBonus = 4;
    grab.damage = {DamagePart{"2d6+2", "bludgeoning", DamageWhen::Always}};
    ConditionRider held;
    held.conditions = {"grappled"};
    held.targetMaxSize = "Medium";
    held.escapeDc = 12;
    ConditionRider pinned;
    pinned.conditions = {"restrained"};
    pinned.tiedTo = "grappled";
    pinned.ongoing = {DamagePart{"1d6", "acid", DamageWhen::Always}};
    pinned.ongoingAt = kOngoingAtTarget;
    grab.riders = {held, pinned};
    grab.riderSave = SaveSpec{Ability::Constitution, 12, false};
    grab.drain = HpDrain{"necrotic", true};
    grab.advantageIfGrappled = true;
    home.attacks.push_back(grab);
    MonsterFeature glare;
    glare.name = "Glare";
    glare.effect = "Wisdom Saving Throw: DC 12, one creature. Failure: Frightened.";
    MonsterAttack aimed;
    aimed.name = glare.name;
    aimed.effect = glare.effect;
    aimed.save = SaveSpec{Ability::Wisdom, 12, false};
    ConditionRider scared;
    scared.conditions = {"frightened"};
    scared.on = kRiderOnFailure;
    scared.saveEnds = true;
    scared.worsensTo = {"paralyzed"};
    scared.worseSaveEnds = true;
    aimed.riders = {scared};
    glare.targeted = aimed;
    home.bonusActions.push_back(glare);
    MonsterFeature fury;
    fury.name = "Bloodied Fury";
    fury.effect = "While Bloodied, it has Advantage on melee attack rolls.";
    AttackModifier furyRule;
    furyRule.when = kModifierWhileBloodied;
    furyRule.meleeOnly = true;
    fury.attackModifier = furyRule;
    home.traits.push_back(fury);
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
    CHECK(text.find("\"schemaVersion\": 2") != std::string::npos);
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
    CHECK_EQ(loaded.preserved.size(), std::size_t{2});

    // A later save writes the skipped rows back, so they are not lost.
    store.saveAll(loaded.monsters);
    const std::string saved = readFile(path);
    CHECK(saved.find("Not the SRD goblin") != std::string::npos);
    CHECK(saved.find("44444444-4444-4444-8444-444444444444") != std::string::npos);
    const auto again = store.loadAll();
    CHECK_EQ(again.monsters.size(), std::size_t{1});
    CHECK_EQ(again.skipped.size(), std::size_t{2});
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
    CHECK_THROWS(MonsterDataError, parseCustomMonsterDocument(R"({"schemaVersion": 3, "monsters": []})", {}));
    CHECK_THROWS(MonsterDataError, parseSrdMonsterDocument(R"({"schemaVersion": 1, "source": "custom", "monsters": []})"));
}

TEST_CASE("the packaged SRD catalog carries structured attacks, defenses, saves, and XP")
{
    const auto monsters = loadSrdMonsters(fs::path(COMBAT_TRACKER_SRD_DIR) / "monsters.json");
    auto find = [&](const std::string& id) {
        for (const Monster& monster : monsters) {
            if (monster.id == id) {
                return monster;
            }
        }
        throw test::Failure("missing " + id);
    };
    const Monster dragon = find("adult-red-dragon");
    CHECK_EQ(dragon.legendaryActionUses, 3);
    CHECK_EQ(dragon.defenses.immunities, (std::vector<std::string>{"fire"}));
    CHECK(dragon.savingThrows[static_cast<std::size_t>(Ability::Dexterity)].has_value());
    CHECK_EQ(monsterXp(dragon), 18000);
    const MonsterAttack& rend = dragon.attacks[1];
    CHECK_EQ(rend.name, std::string("Rend"));
    CHECK_EQ(*rend.attackBonus, 14);
    CHECK(rend.inMultiattack);
    CHECK_EQ(rend.damage.size(), std::size_t{2});
    CHECK_EQ(rend.damage[1].type, std::string("fire"));
    const MonsterAttack& breath = dragon.attacks[2];
    CHECK(breath.save.has_value());
    CHECK_EQ(breath.save->dc, 21);
    CHECK(breath.save->halfOnSuccess);
    CHECK(breath.area);
    CHECK_EQ(*breath.recharge, 5);

    const Monster goblin = find("goblin-warrior");
    CHECK(goblin.attacks[0].damage[1].when == DamageWhen::Advantage);
    const Monster behir = find("behir");
    CHECK_EQ(behir.attacks[0].count, 2);
    for (const Monster& monster : monsters) {
        for (const MonsterAttack& attack : monster.attacks) {
            CHECK(attack.effect.find(". Pegasus") == std::string::npos);
        }
    }
}

TEST_CASE("the packaged SRD catalog says what invisibility abilities do")
{
    const auto monsters = loadSrdMonsters(fs::path{COMBAT_TRACKER_SRD_DIR} / "monsters.json");
    auto monster = [&monsters](const std::string& id) -> const Monster& {
        for (const Monster& candidate : monsters) {
            if (candidate.id == id) {
                return candidate;
            }
        }
        throw test::Failure("missing " + id);
    };
    const Monster& wisp = monster("will-o-wisp");
    const auto vanish = std::find_if(wisp.bonusActions.begin(), wisp.bonusActions.end(),
                                     [](const MonsterFeature& feature) { return feature.name == "Vanish"; });
    CHECK(vanish != wisp.bonusActions.end());
    CHECK(vanish->selfEffect.has_value());
    CHECK_EQ(vanish->selfEffect->concentration, std::string("Vanish"));
    CHECK(std::find(vanish->selfEffect->endsOn.begin(), vanish->selfEffect->endsOn.end(), "action:Consume Life") !=
          vanish->selfEffect->endsOn.end());

    const Monster& imp = monster("imp");
    const auto invisibility = std::find_if(imp.attacks.begin(), imp.attacks.end(),
                                           [](const MonsterAttack& attack) { return attack.name == "Invisibility"; });
    CHECK(invisibility != imp.attacks.end());
    CHECK(invisibility->selfEffect.has_value());
    CHECK_EQ(invisibility->selfEffect->concentration, std::string("invisibility"));

    const Monster& basilisk = monster("basilisk");
    CHECK_EQ(basilisk.bonusActions.at(0).recharge.value_or(0), 4);
    const Monster& mage = monster("mage");
    const auto misty = std::find_if(mage.bonusActions.begin(), mage.bonusActions.end(),
                                    [](const MonsterFeature& feature) { return feature.name == "Misty Step (3/Day)"; });
    CHECK(misty != mage.bonusActions.end());
    CHECK_EQ(misty->perDay.value_or(0), 3);

    const Monster& hag = monster("sea-hag");
    const auto vile = std::find_if(hag.traits.begin(), hag.traits.end(),
                                   [](const MonsterFeature& feature) { return feature.name == "Vile Appearance"; });
    CHECK(vile != hag.traits.end());
    CHECK(vile->aura.has_value());
    CHECK_EQ(vile->aura->dc, 11);
    CHECK(vile->aura->ability == Ability::Wisdom);
    CHECK_EQ(vile->aura->condition, std::string("frightened"));
    CHECK(vile->aura->immuneOnSuccess);
    CHECK((vile->aura->creatureTypes == std::vector<std::string>{"beast", "humanoid"}));
    const auto glare = std::find_if(hag.attacks.begin(), hag.attacks.end(), [](const MonsterAttack& attack) {
        return attack.name.rfind("Death Glare", 0) == 0;
    });
    CHECK(glare != hag.attacks.end());
    CHECK_EQ(glare->targetCondition, std::string("frightened"));
    CHECK_EQ(glare->failureHpThreshold.value_or(0), 20);
    const Monster& mouther = monster("gibbering-mouther");
    CHECK_EQ(mouther.traits.at(0).aura.has_value() || mouther.traits.at(1).aura.has_value(), true);

    const Monster& stalker = monster("invisible-stalker");
    const auto trait = std::find_if(stalker.traits.begin(), stalker.traits.end(),
                                    [](const MonsterFeature& feature) { return feature.name == "Invisibility"; });
    CHECK(trait != stalker.traits.end());
    CHECK(trait->selfEffect.has_value());
    CHECK(trait->selfEffect->endsOn.empty());
}

TEST_CASE("the packaged SRD catalog gives actions their conditions")
{
    const auto monsters = loadSrdMonsters(fs::path{COMBAT_TRACKER_SRD_DIR} / "monsters.json");
    auto entry = [&monsters](const std::string& id, const std::string& name) -> MonsterAttack {
        for (const Monster& monster : monsters) {
            if (monster.id != id) {
                continue;
            }
            for (const MonsterAttack& attack : monster.attacks) {
                if (attack.name.rfind(name, 0) == 0) {
                    return attack;
                }
            }
            for (const auto* list : {&monster.bonusActions, &monster.reactions, &monster.legendaryActions}) {
                for (const MonsterFeature& feature : *list) {
                    if (feature.name.rfind(name, 0) == 0 && feature.targeted.has_value()) {
                        return *feature.targeted;
                    }
                }
            }
        }
        throw test::Failure("missing " + id + " " + name);
    };
    const MonsterAttack grab = entry("bugbear-warrior", "Grab");
    CHECK_EQ(grab.riders.size(), std::size_t{1});
    CHECK_EQ(grab.riders[0].conditions, std::vector<std::string>{"grappled"});
    CHECK_EQ(grab.riders[0].targetMaxSize, std::string("Medium"));
    CHECK_EQ(grab.riders[0].escapeDc.value_or(0), 12);

    const MonsterAttack chain = entry("chain-devil", "Chain");
    CHECK_EQ(chain.riders.size(), std::size_t{2});
    CHECK(chain.riders[0].tiedTo.empty());
    CHECK_EQ(chain.riders[1].tiedTo, std::string("grappled"));

    const MonsterAttack sting = entry("assassin", "Shortsword");
    CHECK_EQ(sting.riders.at(0).until, std::string(kUntilSourceStart));

    const MonsterAttack claw = entry("ghoul", "Claw");
    CHECK(claw.riderSave.has_value());
    CHECK_EQ(claw.riders.at(0).on, std::string(kRiderOnFailure));
    CHECK((claw.riders.at(0).exceptTypes == std::vector<std::string>{"undead", "elf"}));

    const MonsterAttack gore = entry("boar", "Gore");
    CHECK(!gore.riders.at(0).ask.empty());

    const MonsterAttack gaze = entry("medusa", "Petrifying Gaze");
    CHECK(gaze.save.has_value());
    CHECK_EQ(gaze.riders.at(0).worsensTo, std::vector<std::string>{"petrified"});

    const MonsterAttack nightmare = entry("incubus", "Nightmare");
    CHECK_EQ(nightmare.riders.at(0).targetMaxHp.value_or(0), 20);

    const MonsterAttack trample = entry("elephant", "Trample");
    CHECK_EQ(trample.targetCondition, std::string("prone"));

    const MonsterAttack drain = entry("vampire-spawn", "Bite");
    CHECK(drain.drain.has_value());
    CHECK(drain.drain->heals);

    for (const Monster& monster : monsters) {
        if (monster.id == "wolf") {
            CHECK(monster.traits.at(0).attackModifier.has_value());
            CHECK(!monster.traits.at(0).attackModifier->ask.empty());
        }
        if (monster.id == "flesh-golem") {
            bool aversion = false;
            for (const MonsterFeature& trait : monster.traits) {
                aversion = aversion || (trait.attackModifier.has_value() && trait.attackModifier->damageType == "fire");
            }
            CHECK(aversion);
        }
    }
    const MonsterAttack swallow = entry("purple-worm", "Swallow");
    CHECK_EQ(swallow.riders.at(0).removes, std::vector<std::string>{"grappled"});
    CHECK_EQ(swallow.riders.at(0).ongoingAt, std::string(kOngoingAtSource));

    const MonsterAttack insects = entry("adult-black-dragon", "Cloud of Insects");
    CHECK(insects.riders.at(0).concentrationDisadvantage);
    CHECK_EQ(insects.riders.at(0).on, std::string(kRiderOnFailure));
    CHECK_EQ(insects.riders.at(0).until, std::string(kUntilTargetEnd));
    const MonsterAttack ancient = entry("ancient-black-dragon", "Cloud of Insects");
    CHECK(ancient.riders.at(0).concentrationDisadvantage);
}

TEST_CASE("creature spell lists become the spells that deal damage or grant a condition")
{
    const auto monsters = loadSrdMonsters(fs::path{COMBAT_TRACKER_SRD_DIR} / "monsters.json");
    const auto monsterNamed = [&monsters](const std::string& id) -> const Monster* {
        for (const Monster& monster : monsters) {
            if (monster.id == id) {
                return &monster;
            }
        }
        return nullptr;
    };
    const auto attackNamed = [](const Monster& monster, const std::string& name) -> const MonsterAttack* {
        for (const MonsterAttack& attack : monster.attacks) {
            if (attack.name == name) {
                return &attack;
            }
        }
        return nullptr;
    };
    const Monster* dragon = monsterNamed("adult-black-dragon");
    CHECK(dragon != nullptr);
    if (dragon != nullptr) {
        const MonsterAttack* casting = attackNamed(*dragon, "Spellcasting");
        CHECK(casting != nullptr);
        if (casting != nullptr) {
            const std::vector<MonsterAttack> spells = actionableSpells(*dragon, *casting);
            std::vector<std::string> names;
            for (const MonsterAttack& spell : spells) {
                names.push_back(spell.name);
                // Multiattack names Acid Arrow; Fear and the rest are a separate action.
                const bool named = spell.name == "Acid Arrow (level 3)";
                CHECK_EQ(spell.inMultiattack, named);
                CHECK_EQ(spell.multiattackAs, named ? std::string("Spellcasting") : std::string());
            }
            CHECK(std::find(names.begin(), names.end(), "Acid Arrow (level 3)") != names.end());
            CHECK(std::find(names.begin(), names.end(), "Fear") != names.end());
            CHECK(std::find(names.begin(), names.end(), "Vitriolic Sphere") != names.end());
            CHECK(std::find(names.begin(), names.end(), "Detect Magic") == names.end());
            for (const MonsterAttack& spell : spells) {
                if (spell.name == "Acid Arrow (level 3)") {
                    CHECK_EQ(spell.attackBonus.value_or(0), 9);
                    CHECK_EQ(spell.damage.at(0).dice, std::string("5d4"));
                    CHECK(spell.halfDamageOnMiss);
                }
                if (spell.name == "Fear") {
                    CHECK(spell.area);
                    CHECK(spell.save.has_value());
                    CHECK(spell.save->ability == Ability::Wisdom);
                    CHECK_EQ(spell.save->dc, 17);
                    CHECK_EQ(spell.riders.at(0).conditions.at(0), std::string("frightened"));
                }
            }
        }
        bool fear = false;
        for (const MonsterFeature& feature : dragon->legendaryActions) {
            if (feature.name != "Frightful Presence") {
                continue;
            }
            const std::vector<MonsterAttack> spells = actionableSpells(*dragon, feature);
            CHECK_EQ(spells.size(), std::size_t{1});
            if (!spells.empty()) {
                CHECK_EQ(spells.front().name, std::string("Fear"));
                CHECK_EQ(spells.front().save->dc, 17);
                CHECK(!spells.front().inMultiattack);
            }
            fear = true;
        }
        CHECK(fear);
    }

    const Monster* vampire = monsterNamed("vampire");
    CHECK(vampire != nullptr);
    if (vampire != nullptr) {
        bool charm = false;
        bool command = false;
        for (const MonsterFeature& feature : vampire->bonusActions) {
            if (feature.name.rfind("Charm", 0) == 0) {
                CHECK(actionableSpells(*vampire, feature).empty());
                charm = true;
            }
        }
        for (const MonsterFeature& feature : vampire->legendaryActions) {
            if (feature.name == "Beguile") {
                const std::vector<MonsterAttack> spells = actionableSpells(*vampire, feature);
                CHECK_EQ(spells.size(), std::size_t{1});
                if (!spells.empty()) {
                    CHECK_EQ(spells.front().name, std::string("Command"));
                    CHECK_EQ(spells.front().save->dc, 17);
                }
                command = true;
            }
        }
        CHECK(charm);
        CHECK(command);
    }

    const Monster* drider = monsterNamed("drider");
    CHECK(drider != nullptr);
    if (drider != nullptr) {
        bool magic = false;
        for (const MonsterFeature& feature : drider->bonusActions) {
            if (feature.name.rfind("Magic of the Spider Queen", 0) != 0) {
                continue;
            }
            const std::vector<MonsterAttack> spells = actionableSpells(*drider, feature);
            std::vector<std::string> names;
            for (const MonsterAttack& spell : spells) {
                names.push_back(spell.name);
                CHECK_EQ(spell.save->dc, 14);
                CHECK(!spell.perDay.has_value());
            }
            CHECK(std::find(names.begin(), names.end(), "Faerie Fire") != names.end());
            CHECK(std::find(names.begin(), names.end(), "Web") != names.end());
            CHECK(std::find(names.begin(), names.end(), "Darkness") == names.end());
            magic = true;
        }
        CHECK(magic);
    }

    const Monster* imp = monsterNamed("imp");
    CHECK(imp != nullptr);
    if (imp != nullptr) {
        const MonsterAttack* invisibility = attackNamed(*imp, "Invisibility");
        CHECK(invisibility != nullptr);
        if (invisibility != nullptr) {
            CHECK(actionableSpells(*imp, *invisibility).empty());
        }
    }

    const Monster* archmage = monsterNamed("archmage");
    CHECK(archmage != nullptr);
    if (archmage != nullptr) {
        const MonsterAttack* casting = attackNamed(*archmage, "Spellcasting");
        CHECK(casting != nullptr);
        if (casting != nullptr) {
            const std::vector<MonsterAttack> spells = actionableSpells(*archmage, *casting);
            bool bolt = false;
            bool cone = false;
            for (const MonsterAttack& spell : spells) {
                CHECK(!spell.inMultiattack);
                if (spell.name == "Lightning Bolt (level 7)") {
                    CHECK_EQ(spell.damage.at(0).dice, std::string("12d6"));
                    CHECK_EQ(spell.perDay.value_or(0), 2);
                    CHECK_EQ(spell.save->dc, 17);
                    bolt = true;
                }
                if (spell.name == "Cone of Cold (level 9)") {
                    CHECK_EQ(spell.damage.at(0).dice, std::string("12d8"));
                    CHECK_EQ(spell.perDay.value_or(0), 1);
                    cone = true;
                }
            }
            CHECK(bolt);
            CHECK(cone);
        }
    }
}

TEST_CASE("every combat spell a creature names becomes an attack")
{
    const auto monsters = loadSrdMonsters(fs::path{COMBAT_TRACKER_SRD_DIR} / "monsters.json");
    std::string missing;
    int buttons = 0;
    const auto checkBuilt = [&](const std::string& where, const std::vector<std::string>& mentions,
                                const std::vector<MonsterAttack>& built, bool alreadyResolved) {
        if (alreadyResolved) {
            if (!built.empty()) {
                missing += where + " also built a spell\n";
            }
            return;
        }
        for (const std::string& mention : mentions) {
            const bool found = std::any_of(built.begin(), built.end(), [&mention](const MonsterAttack& spell) {
                return spell.name == mention;
            });
            if (!found) {
                missing += where + " did not build " + mention + "\n";
            }
        }
        for (const MonsterAttack& spell : built) {
            ++buttons;
            if (spell.strikes < 1) {
                missing += where + " " + spell.name + " has no strikes\n";
            }
            if (spell.effect.find(" casts ") == std::string::npos) {
                missing += spell.name + " does not say it casts\n";
            }
            if (spell.save.has_value() && spell.save->dc <= 0) {
                missing += where + " " + spell.name + " has no save DC\n";
            }
            for (const DamagePart& part : spell.damage) {
                if (part.dice.empty()) {
                    missing += spell.name + " has empty damage dice\n";
                }
            }
        }
    };
    for (const Monster& monster : monsters) {
        for (const MonsterAttack& attack : monster.attacks) {
            const bool resolved = isMultiattack(attack) || attack.attackBonus.has_value() || attack.save.has_value() ||
                                  attack.selfEffect.has_value();
            checkBuilt(monster.name + " / " + attack.name, combatSpellMentions(attack.effect),
                       actionableSpells(monster, attack), resolved);
        }
        for (const auto* features : {&monster.traits, &monster.bonusActions, &monster.reactions, &monster.legendaryActions}) {
            for (const MonsterFeature& feature : *features) {
                const bool resolved = feature.targeted.has_value() || feature.selfEffect.has_value();
                checkBuilt(monster.name + " / " + feature.name, combatSpellMentions(feature.effect),
                           actionableSpells(monster, feature), resolved);
            }
        }
    }
    if (!missing.empty() || buttons < 40) {
        throw test::Failure(missing + "spell buttons: " + std::to_string(buttons));
    }
}
