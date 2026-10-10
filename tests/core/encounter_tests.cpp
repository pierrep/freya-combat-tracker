#include "core/campaign.h"
#include "core/combat_rules.h"
#include "core/encounter.h"
#include "test_harness.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <climits>
#include <optional>
#include <string>
#include <vector>

using namespace combat;

namespace {

Combatant fighter(std::string id, std::string name, int initiative)
{
    Combatant combatant;
    combatant.id = std::move(id);
    combatant.source = kCombatantSourceCharacter;
    combatant.sourceId = combatant.id + "-sheet";
    combatant.name = std::move(name);
    combatant.initiative = initiative;
    combatant.hp = 20;
    combatant.ac = 16;
    return combatant;
}

Combatant goblin(std::string id, int initiative, int bonus)
{
    Combatant combatant;
    combatant.id = std::move(id);
    combatant.source = kCombatantSourceMonster;
    combatant.sourceId = "goblin-warrior";
    combatant.name = "Goblin Warrior";
    combatant.initiative = initiative;
    combatant.initiativeBonus = bonus;
    combatant.hp = 10;
    combatant.ac = 15;
    return combatant;
}

Encounter fightWith(std::vector<Combatant> combatants, int turnIndex = 0)
{
    Encounter encounter;
    encounter.id = "enc";
    encounter.name = "Goblin ambush";
    encounter.round = 1;
    encounter.turnIndex = turnIndex;
    encounter.combatants = std::move(combatants);
    return encounter;
}

class Sequence {
public:
    explicit Sequence(std::vector<int> rolls)
        : m_rolls(std::move(rolls))
    {
    }

    int operator()()
    {
        CHECK(m_index < m_rolls.size());
        return m_rolls[m_index++];
    }

    std::size_t used() const { return m_index; }

private:
    std::vector<int> m_rolls;
    std::size_t m_index = 0;
};

}  // namespace

TEST_CASE("initiative sort is highest first and keeps ties in their previous order")
{
    std::vector<Combatant> combatants{
        fighter("a", "Aria", 10),
        goblin("b", 15, 2),
        fighter("c", "Borin", 10),
        goblin("d", 15, 2),
    };
    const int active = sortByInitiative(combatants, 2);
    CHECK_EQ(combatants[0].id, std::string("b"));
    CHECK_EQ(combatants[1].id, std::string("d"));
    CHECK_EQ(combatants[2].id, std::string("a"));
    CHECK_EQ(combatants[3].id, std::string("c"));
    CHECK_EQ(active, 3);
}

TEST_CASE("roll all monsters uses d20 plus the snapshotted bonus and skips characters")
{
    Encounter encounter = fightWith({
        fighter("aria", "Aria", 4),
        goblin("g1", 0, 2),
        goblin("g2", 0, 5),
    });
    encounter.combatants[1].name = "Goblin Warrior 1";
    encounter.combatants[2].name = "Goblin Warrior 2";
    Sequence dice({20, 1});
    const int missing = rollAllMonsterInitiatives(encounter, [&dice] { return dice(); });
    CHECK_EQ(missing, 0);
    CHECK_EQ(dice.used(), 2U);
    CHECK_EQ(encounter.combatants[0].id, std::string("g1"));
    CHECK_EQ(encounter.combatants[0].initiative, 22);
    CHECK_EQ(encounter.combatants[1].id, std::string("g2"));
    CHECK_EQ(encounter.combatants[1].initiative, 6);
    CHECK_EQ(encounter.combatants[2].id, std::string("aria"));
    CHECK_EQ(encounter.combatants[2].initiative, 4);
    CHECK_EQ(encounter.turnIndex, 2);
}

TEST_CASE("a monster with no stored bonus rolls d20 plus zero")
{
    Encounter encounter = fightWith({goblin("g", 3, 9)});
    encounter.combatants[0].initiativeBonus.reset();
    Sequence dice({8});
    const int missing = rollAllMonsterInitiatives(encounter, [&dice] { return dice(); });
    CHECK_EQ(missing, 1);
    CHECK_EQ(encounter.combatants[0].initiative, 8);
    CHECK(!encounter.combatants[0].initiativeBonus.has_value());
}

TEST_CASE("reroll changes one monster and leaves the other totals alone")
{
    Encounter encounter = fightWith({
        goblin("g1", 12, 2),
        fighter("aria", "Aria", 18),
        goblin("g2", 9, 1),
    });
    encounter.turnIndex = 1;
    Sequence dice({3});
    bool missing = true;
    CHECK(rerollMonsterInitiative(encounter, "g2", [&dice] { return dice(); }, &missing));
    CHECK(!missing);
    CHECK_EQ(dice.used(), 1U);
    CHECK_EQ(encounter.combatants[0].id, std::string("aria"));
    CHECK_EQ(encounter.combatants[0].initiative, 18);
    CHECK_EQ(encounter.combatants[1].id, std::string("g1"));
    CHECK_EQ(encounter.combatants[1].initiative, 12);
    CHECK_EQ(encounter.combatants[2].id, std::string("g2"));
    CHECK_EQ(encounter.combatants[2].initiative, 4);
    CHECK_EQ(encounter.turnIndex, 0);
}

TEST_CASE("reroll refuses a character and does not roll")
{
    Encounter encounter = fightWith({fighter("aria", "Aria", 11)});
    Sequence dice({20});
    CHECK(!rerollMonsterInitiative(encounter, "aria", [&dice] { return dice(); }));
    CHECK(!rerollMonsterInitiative(encounter, "missing", [&dice] { return dice(); }));
    CHECK_EQ(dice.used(), 0U);
    CHECK_EQ(encounter.combatants[0].initiative, 11);
}

TEST_CASE("a typed initiative replaces the rolled total when the list is sorted")
{
    std::vector<Combatant> combatants{
        goblin("g", 22, 2),
        fighter("aria", "Aria", 4),
    };
    combatants[1].initiative = 30;
    const int active = sortByInitiative(combatants, 0);
    CHECK_EQ(combatants[0].id, std::string("aria"));
    CHECK_EQ(combatants[0].initiative, 30);
    CHECK_EQ(combatants[1].initiative, 22);
    CHECK_EQ(active, 1);
}

TEST_CASE("initiative addition saturates instead of overflowing")
{
    Encounter encounter = fightWith({goblin("g", 0, INT_MAX)});
    Sequence dice({20});
    rollAllMonsterInitiatives(encounter, [&dice] { return dice(); });
    CHECK_EQ(encounter.combatants[0].initiative, INT_MAX);
}

TEST_CASE("turn controls wrap the order and the round")
{
    Encounter encounter = fightWith({
        fighter("a", "Aria", 18),
        goblin("g", 12, 2),
    });
    advanceTurn(encounter);
    CHECK_EQ(encounter.turnIndex, 1);
    CHECK_EQ(encounter.round, 1);
    advanceTurn(encounter);
    CHECK_EQ(encounter.turnIndex, 0);
    CHECK_EQ(encounter.round, 2);
    advanceTurn(encounter);
    CHECK_EQ(encounter.turnIndex, 1);
    CHECK_EQ(encounter.round, 2);
    advanceRound(encounter);
    CHECK_EQ(encounter.turnIndex, 0);
    CHECK_EQ(encounter.round, 3);
}

TEST_CASE("a monster at 0 hp is out of turn order and returns to the same initiative slot")
{
    Encounter encounter = fightWith({
        fighter("aria", "Aria", 20),
        goblin("goblin", 15, 2),
        goblin("bard", 5, 1),
    });
    encounter.combatants[0].hp = 12;
    encounter.combatants[1].hp = 10;
    encounter.combatants[2].hp = 8;

    const std::vector<int> before = initiativeOrder(encounter.combatants);
    CHECK_EQ(before.size(), 3U);
    CHECK_EQ(before[0], 0);
    CHECK_EQ(before[1], 1);
    CHECK_EQ(before[2], 2);

    encounter.combatants[1].hp = 0;
    encounter.turnIndex = 1;
    keepTurnInInitiative(encounter);
    CHECK_EQ(encounter.turnIndex, 2);
    CHECK_EQ(encounter.round, 1);
    const std::vector<int> dropped = initiativeOrder(encounter.combatants);
    CHECK_EQ(dropped.size(), 2U);
    CHECK_EQ(dropped[0], 0);
    CHECK_EQ(dropped[1], 2);

    encounter.combatants[1].hp = 4;
    const std::vector<int> healed = initiativeOrder(encounter.combatants);
    CHECK_EQ(healed.size(), 3U);
    CHECK_EQ(healed[0], 0);
    CHECK_EQ(healed[1], 1);
    CHECK_EQ(healed[2], 2);
    CHECK_EQ(encounter.combatants[0].id, std::string("aria"));
    CHECK_EQ(encounter.combatants[1].id, std::string("goblin"));
    CHECK_EQ(encounter.combatants[2].id, std::string("bard"));

    encounter.combatants[2].hp = 0;
    encounter.turnIndex = 2;
    keepTurnInInitiative(encounter);
    CHECK_EQ(encounter.turnIndex, 0);
    CHECK_EQ(encounter.round, 1);

    encounter.combatants[2].hp = 8;
    encounter.turnIndex = 0;
    encounter.combatants[1].hp = 0;
    keepTurnInInitiative(encounter);
    CHECK_EQ(encounter.turnIndex, 0);
    advanceTurn(encounter);
    CHECK_EQ(encounter.turnIndex, 2);
    CHECK_EQ(encounter.round, 1);

    const std::string goblinName = encounter.combatants[1].name;
    const std::string ariaName = encounter.combatants[0].name;
    encounter.combatants[1].tempHp = 5;
    const std::vector<int> shielded = initiativeOrder(encounter.combatants);
    CHECK_EQ(shielded.size(), 3U);
    CHECK_EQ(shielded[1], 1);
    CHECK_EQ(encounter.combatants[1].name, goblinName);

    applyDamage(encounter.combatants[1], 5);
    CHECK_EQ(encounter.combatants[1].tempHp, 0);
    CHECK_EQ(encounter.combatants[1].hp, 0);
    CHECK_EQ(encounter.combatants[1].name, goblinName);
    CHECK_EQ(encounter.combatants[0].name, ariaName);
    encounter.turnIndex = 1;
    keepTurnInInitiative(encounter);
    CHECK_EQ(encounter.turnIndex, 2);
    CHECK_EQ(encounter.round, 1);
    const std::vector<int> afterDamage = initiativeOrder(encounter.combatants);
    CHECK_EQ(afterDamage.size(), 2U);
    CHECK_EQ(afterDamage[0], 0);
    CHECK_EQ(afterDamage[1], 2);
}

TEST_CASE("turn controls do nothing when the fight is empty")
{
    Encounter encounter = fightWith({});
    advanceTurn(encounter);
    advanceRound(encounter);
    CHECK_EQ(encounter.round, 1);
    CHECK_EQ(encounter.turnIndex, 0);
}

TEST_CASE("move up and down follows the active combatant")
{
    std::vector<Combatant> combatants{
        fighter("a", "Aria", 10),
        goblin("b", 10, 2),
        fighter("c", "Borin", 10),
    };
    const MoveResult up = moveCombatant(combatants, 2, -1, 2);
    CHECK_EQ(up.movedTo, 1);
    CHECK_EQ(up.turnIndex, 1);
    CHECK_EQ(combatants[1].id, std::string("c"));
    CHECK_EQ(combatants[2].id, std::string("b"));
    const MoveResult stuck = moveCombatant(combatants, 0, -1, up.turnIndex);
    CHECK_EQ(stuck.movedTo, 0);
    CHECK_EQ(stuck.turnIndex, 1);
    CHECK_EQ(combatants[0].id, std::string("a"));
}

TEST_CASE("removing a combatant keeps the turn on the right person")
{
    std::vector<Combatant> combatants{
        fighter("a", "Aria", 18),
        goblin("b", 12, 2),
        fighter("c", "Borin", 8),
    };
    CHECK_EQ(removeCombatant(combatants, 0, 2), 1);
    CHECK_EQ(combatants[1].id, std::string("c"));
    CHECK_EQ(removeCombatant(combatants, 1, 1), 0);
    CHECK_EQ(combatants.size(), 1U);
    CHECK_EQ(combatants[0].id, std::string("b"));
}

TEST_CASE("one monster has no copy suffix and two of the same type are numbered")
{
    Monster monster;
    monster.id = "goblin-warrior";
    monster.name = "Goblin Warrior";
    monster.hp = 10;
    monster.ac = 15;
    monster.initiativeBonus = 2;

    std::vector<Combatant> existing;
    const Combatant first = makeMonsterCombatant(monster, "id-1");
    existing.push_back(first);
    assignMonsterCopyNames(existing);
    CHECK_EQ(existing[0].name, std::string("Goblin Warrior"));
    CHECK_EQ(first.name, std::string("Goblin Warrior"));

    monster.initiativeBonus = 9;
    monster.hp = 99;
    const Combatant second = makeMonsterCombatant(monster, "id-2");
    existing.push_back(second);
    assignMonsterCopyNames(existing);
    CHECK_EQ(existing[0].name, std::string("Goblin Warrior 1"));
    CHECK_EQ(existing[1].name, std::string("Goblin Warrior 2"));
    existing[1].hp = 0;
    existing[1].tempHp = 0;
    assignMonsterCopyNames(existing);
    CHECK_EQ(existing[0].name, std::string("Goblin Warrior 1"));
    CHECK_EQ(existing[1].name, std::string("Goblin Warrior 2"));
    CHECK_EQ(first.id, std::string("id-1"));
    CHECK_EQ(second.id, std::string("id-2"));
    CHECK_EQ(*first.initiativeBonus, 2);
    CHECK_EQ(first.hp, 10);
    CHECK_EQ(*second.initiativeBonus, 9);
    CHECK_EQ(second.hp, 99);
}

TEST_CASE("a character combatant copies hp and ac and is not given a bonus")
{
    Character character;
    character.id = "2f0a1c5e-7b34-4d1a-9c88-6e5b0a1d44f2";
    character.name = "Aria";
    character.hp.current = 32;
    character.hp.max = 40;
    character.ac = 16;
    const Combatant combatant = makeCharacterCombatant(character, "combatant");
    CHECK_EQ(combatant.source, std::string(kCombatantSourceCharacter));
    CHECK_EQ(combatant.sourceId, character.id);
    CHECK_EQ(combatant.name, std::string("Aria"));
    CHECK_EQ(combatant.hp, 32);
    CHECK(combatant.maxHp.has_value());
    CHECK_EQ(*combatant.maxHp, 40);
    CHECK_EQ(combatant.ac, 16);
    CHECK_EQ(combatant.initiative, 0);
    CHECK(!combatant.initiativeBonus.has_value());
}

TEST_CASE("reset restores monster hp and a later encounter copies the carried character hp")
{
    Character aria;
    aria.id = "aria";
    aria.name = "Aria";
    aria.hp.current = 30;
    aria.hp.max = 30;
    aria.ac = 16;

    Monster monster;
    monster.id = "goblin-warrior";
    monster.name = "Goblin Warrior";
    monster.hp = 10;
    monster.ac = 15;

    Encounter first;
    first.id = "fight-1";
    first.name = "First";
    first.combatants.push_back(makeCharacterCombatant(aria, "aria-row"));
    first.combatants.push_back(makeMonsterCombatant(monster, "goblin-row"));
    first.combatants[0].hp = 12;
    first.combatants[1].hp = 3;

    std::vector<Character> roster{aria};
    CHECK(carryCharacterHitPoints(roster, first.combatants[0]));
    CHECK_EQ(roster[0].hp.current, 12);
    CHECK_EQ(roster[0].hp.max, 30);
    CHECK(!carryCharacterHitPoints(roster, first.combatants[1]));
    CHECK_EQ(roster[0].hp.current, 12);

    Encounter second;
    second.combatants.push_back(makeCharacterCombatant(roster[0], "aria-again"));
    CHECK_EQ(second.combatants[0].hp, 12);
    CHECK(second.combatants[0].maxHp.has_value());
    CHECK_EQ(*second.combatants[0].maxHp, 30);

    first.combatants[0].initiative = 17;
    first.combatants[0].conditions.push_back(ActiveCondition{"prone", std::nullopt, std::nullopt});
    first.combatants[0].conditions.push_back(ActiveCondition{"blinded", std::nullopt, std::nullopt});
    first.combatants[0].concentration = "bless";
    first.combatants[1].initiative = 14;
    resetMonsters(first);
    CHECK_EQ(first.combatants[0].initiative, 0);
    CHECK_EQ(first.combatants[1].initiative, 0);  // monsters roll again too
    CHECK(first.combatants[0].conditions.empty());
    CHECK(first.combatants[0].concentration.empty());
    CHECK_EQ(first.combatants[1].hp, 10);
    CHECK_EQ(*first.combatants[1].maxHp, 10);
    CHECK_EQ(first.combatants[0].hp, 12);
    CHECK_EQ(roster[0].hp.current, 12);

    Combatant legacy;
    legacy.source = kCombatantSourceMonster;
    legacy.hp = 4;
    Encounter oldFight;
    oldFight.combatants.push_back(legacy);
    resetMonsters(oldFight);
    CHECK_EQ(oldFight.combatants[0].hp, 4);

    // A character still at 0 HP keeps Unconscious and nothing else.
    Encounter downed;
    downed.combatants.push_back(makeCharacterCombatant(roster[0], "down"));
    downed.combatants[0].hp = 0;
    downed.combatants[0].conditions.push_back(ActiveCondition{"unconscious", std::nullopt, std::nullopt});
    downed.combatants[0].conditions.push_back(ActiveCondition{"poisoned", std::nullopt, std::nullopt});
    resetMonsters(downed);
    CHECK_EQ(downed.combatants[0].conditions.size(), std::size_t{1});
    CHECK(hasCondition(downed.combatants[0], "unconscious"));

    Combatant missing = first.combatants[0];
    missing.sourceId = "not-on-the-roster";
    CHECK(!carryCharacterHitPoints(roster, missing));
    CHECK_EQ(roster[0].hp.current, 12);
}

TEST_CASE("encounter checks reject a blank name, a bad turn, and a character bonus")
{
    Encounter encounter = fightWith({fighter("a", "Aria", 1)});
    encounter.name = "   ";
    CHECK(!validateEncounter(encounter).empty());

    encounter.name = "Ambush";
    encounter.round = 0;
    CHECK(!validateEncounter(encounter).empty());

    encounter.round = 1;
    encounter.turnIndex = 3;
    CHECK(!validateEncounter(encounter).empty());

    encounter.turnIndex = 0;
    encounter.combatants[0].initiativeBonus = 2;
    CHECK(!validateEncounter(encounter).empty());

    encounter.combatants[0].initiativeBonus.reset();
    encounter.combatants.push_back(fighter("a", "Again", 1));
    CHECK(!validateEncounter(encounter).empty());
}

TEST_CASE("rolling for the players adds each sheet's bonus and sorts the order")
{
    Character aria;
    aria.id = "aria";
    aria.name = "Aria";
    aria.hp = {10, 10};
    Character bryn = aria;
    bryn.id = "bryn";
    bryn.name = "Bryn";
    Monster goblin;
    goblin.id = "goblin";
    goblin.name = "Goblin";
    goblin.hp = 7;
    Encounter fight;
    fight.started = false;
    fight.combatants.push_back(makeCharacterCombatant(aria, "a"));
    fight.combatants.push_back(makeCharacterCombatant(bryn, "b"));
    fight.combatants.push_back(makeMonsterCombatant(goblin, "g"));
    fight.combatants[1].exhaustion = 1;
    fight.combatants[2].initiative = 12;
    std::vector<int> faces{5, 15};
    std::size_t next = 0;
    rollAllCharacterInitiatives(fight, [&] { return faces[next++]; }, {{"a", 3}, {"b", 1}});
    CHECK_EQ(fight.combatants[0].id, std::string("b"));  // 15 + 1 - 2 = 14
    CHECK_EQ(fight.combatants[0].initiative, 14);
    CHECK_EQ(fight.combatants[1].id, std::string("g"));  // untouched at 12
    CHECK_EQ(fight.combatants[1].initiative, 12);
    CHECK_EQ(fight.combatants[2].initiative, 8);  // 5 + 3
}

namespace {

Character partyCharacter(const std::string& id, const std::string& name)
{
    Character character;
    character.id = id;
    character.name = name;
    character.hp = {20, 20};
    character.ac = 14;
    return character;
}

std::function<std::string()> counterIds()
{
    auto next = std::make_shared<int>(0);
    return [next] { return "row-" + std::to_string(++*next); };
}

std::vector<std::string> characterRows(const Encounter& encounter)
{
    std::vector<std::string> ids;
    for (const Combatant& combatant : encounter.combatants) {
        if (isCharacterCombatant(combatant)) {
            ids.push_back(combatant.sourceId + (combatant.partyMember ? "*" : ""));
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

}  // namespace

TEST_CASE("a party in an encounter not started follows its members; once started, its characters stay")
{
    const std::vector<Character> roster{partyCharacter("a", "Aria"), partyCharacter("b", "Bryn"),
                                        partyCharacter("c", "Cael")};
    Campaign campaign;
    campaign.parties.push_back(Party{"p", "Lanterns", {"a", "b"}});
    const auto ids = counterIds();
    Encounter encounter;
    encounter.id = "e";
    encounter.started = false;
    // Cael came on their own, a guest.
    encounter.combatants.push_back(makeCharacterCombatant(roster[2], ids()));
    CHECK(applyParty(encounter, campaign.parties[0], roster, ids));
    CHECK_EQ(encounter.partyId, std::string("p"));
    CHECK(characterRows(encounter) == (std::vector<std::string>{"a*", "b*", "c"}));
    CHECK(!applyParty(encounter, campaign.parties[0], roster, ids));  // nothing more to do

    // Bryn leaves, Cael joins: Bryn's row goes, Cael's is the party's, no second one.
    campaign.parties[0].characterIds = {"a", "c"};
    CHECK(syncPartyRows(encounter, campaign, roster, ids));
    CHECK(characterRows(encounter) == (std::vector<std::string>{"a*", "c*"}));

    // Aria taken out of this encounter by hand stays out.
    encounter.partyLeftOut.push_back("a");
    for (int i = 0; i < static_cast<int>(encounter.combatants.size()); ++i) {
        if (encounter.combatants[static_cast<std::size_t>(i)].sourceId == "a") {
            encounter.turnIndex = removeCombatant(encounter.combatants, i, encounter.turnIndex);
            break;
        }
    }
    CHECK(!syncPartyRows(encounter, campaign, roster, ids));
    CHECK(characterRows(encounter) == std::vector<std::string>{"c*"});

    // Started: the fight's characters stay as they are.
    encounter.started = true;
    campaign.parties[0].characterIds = {"b"};
    CHECK(!syncPartyRows(encounter, campaign, roster, ids));
    CHECK(characterRows(encounter) == std::vector<std::string>{"c*"});

    // Taking the party out leaves guests; a deleted party leaves everyone, as guests.
    Encounter other;
    other.started = false;
    other.combatants.push_back(makeCharacterCombatant(roster[1], ids()));  // Bryn, a guest
    applyParty(other, Party{"q", "Pair", {"a", "c"}}, roster, ids);
    CHECK(characterRows(other) == (std::vector<std::string>{"a*", "b", "c*"}));
    Encounter forgotten = other;
    removePartyRows(other);
    CHECK(characterRows(other) == std::vector<std::string>{"b"});
    CHECK(other.partyId.empty());
    std::vector<Encounter> list{forgotten};
    CHECK_EQ(forgetParty(list, "q"), 1);
    CHECK(characterRows(list[0]) == (std::vector<std::string>{"a", "b", "c"}));
    CHECK(list[0].partyId.empty());
    // A party that no longer exists does nothing.
    Encounter orphan;
    orphan.started = false;
    orphan.partyId = "gone";
    CHECK(!syncPartyRows(orphan, campaign, roster, ids));
}

TEST_CASE("an adventure lists its encounters, and deleting it leaves them with none")
{
    Encounter a;
    a.id = "a";
    a.adventureId = "lost-mine";
    Encounter b;
    b.id = "b";
    CHECK(inAdventureChoice(a, kAllAdventures));
    CHECK(inAdventureChoice(b, kAllAdventures));
    CHECK(inAdventureChoice(a, "lost-mine"));
    CHECK(!inAdventureChoice(b, "lost-mine"));
    CHECK(inAdventureChoice(b, kNoAdventure));
    CHECK(!inAdventureChoice(a, kNoAdventure));
    std::vector<Encounter> list{a, b};
    CHECK_EQ(forgetAdventure(list, "lost-mine"), 1);
    CHECK(list[0].adventureId.empty());
    Campaign campaign;
    campaign.adventures.push_back(Adventure{"lost-mine", "Lost Mine", "p"});
    CHECK(findAdventure(campaign, "lost-mine") != nullptr);
    CHECK(findAdventure(campaign, "") == nullptr);
    CHECK(findParty(campaign, "p") == nullptr);
}
