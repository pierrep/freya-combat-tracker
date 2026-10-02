#include "core/combat_rules.h"
#include "test_harness.h"

#include <string>

using namespace combat;

namespace {

Combatant fighter()
{
    Combatant combatant;
    combatant.id = "c";
    combatant.source = kCombatantSourceCharacter;
    combatant.sourceId = "sheet";
    combatant.name = "Aria";
    combatant.hp = 10;
    combatant.maxHp = 12;
    combatant.tempHp = 3;
    return combatant;
}

Character wizard()
{
    Character character;
    character.id = "sheet";
    character.name = "Aria";
    character.abilities.dexterity = 16;
    character.abilities.wisdom = 8;
    character.classes.push_back(ClassLevel{"Wizard", 5, ""});
    character.savingThrows.intelligence = true;
    character.abilities.intelligence = 18;
    character.proficiencyBonus = 9;
    character.spellSlots.push_back(SpellSlot{1, 2, 4});
    character.spellSlots.push_back(SpellSlot{2, 0, 3});
    return character;
}

}  // namespace

TEST_CASE("damage spends temporary HP before current HP and stops at zero")
{
    Combatant combatant = fighter();
    CHECK(applyDamage(combatant, 5));
    CHECK_EQ(combatant.tempHp, 0);
    CHECK_EQ(combatant.hp, 8);

    CHECK(applyDamage(combatant, 100));
    CHECK_EQ(combatant.hp, 0);
    CHECK_EQ(combatant.tempHp, 0);

    const int hp = combatant.hp;
    CHECK(!applyDamage(combatant, -1));
    CHECK_EQ(combatant.hp, hp);
}

TEST_CASE("healing does not pass the stored maximum or change temporary HP")
{
    Combatant combatant = fighter();
    combatant.hp = 10;
    CHECK(applyHealing(combatant, 5));
    CHECK_EQ(combatant.hp, 12);
    CHECK_EQ(combatant.tempHp, 3);

    CHECK(applyHealing(combatant, 4));
    CHECK_EQ(combatant.hp, 12);

    combatant.maxHp = std::nullopt;
    CHECK(applyHealing(combatant, 4));
    CHECK_EQ(combatant.hp, 16);

    combatant.maxHp = 10;
    CHECK(applyHealing(combatant, 3));
    CHECK_EQ(combatant.hp, 10);

    CHECK(!applyHealing(combatant, -2));
    CHECK_EQ(combatant.hp, 10);
}

TEST_CASE("healing and a typed hit point value both stop at the stored maximum")
{
    Combatant combatant;
    combatant.hp = 28;
    combatant.maxHp = 30;
    CHECK(applyHealing(combatant, 5));
    CHECK_EQ(combatant.hp, 30);
    CHECK_EQ(cappedHitPoints(45, combatant.maxHp), 30);
    combatant.hp = cappedHitPoints(45, combatant.maxHp);
    CHECK_EQ(combatant.hp, 30);

    combatant.maxHp.reset();
    CHECK(applyHealing(combatant, 8));
    CHECK_EQ(combatant.hp, 38);
    CHECK_EQ(cappedHitPoints(50, std::nullopt), 50);
}

TEST_CASE("conditions are unique ids and concentration is set by the user")
{
    Combatant combatant = fighter();
    CHECK(addCondition(combatant, "blinded"));
    CHECK(!addCondition(combatant, "blinded"));
    CHECK(!addCondition(combatant, ""));
    CHECK_EQ(combatant.conditions.size(), std::size_t{1});
    CHECK(removeCondition(combatant, "blinded"));
    CHECK(!removeCondition(combatant, "blinded"));

    setConcentration(combatant, "bless");
    CHECK_EQ(combatant.concentration, std::string("bless"));
    setConcentration(combatant, "");
    CHECK(combatant.concentration.empty());
}

TEST_CASE("death saves are counters without a cap at three")
{
    Combatant combatant = fighter();
    adjustDeathSave(combatant, true, 1);
    adjustDeathSave(combatant, true, 1);
    adjustDeathSave(combatant, false, 1);
    adjustDeathSave(combatant, true, -1);
    CHECK_EQ(combatant.deathSaves.successes, 1);
    CHECK_EQ(combatant.deathSaves.failures, 1);
    adjustDeathSave(combatant, true, 5);
    CHECK_EQ(combatant.deathSaves.successes, 6);
}

TEST_CASE("spending a slot stops at zero and rest restores the stored maximum")
{
    Character character = wizard();
    CHECK(spendSpellSlot(character, 1));
    CHECK_EQ(character.spellSlots[0].current, 1);
    CHECK(!spendSpellSlot(character, 2));
    CHECK_EQ(character.spellSlots[1].current, 0);
    CHECK(!spendSpellSlot(character, 9));
    character.spellSlots[0].current = 9;
    finishRest(character);
    CHECK_EQ(character.spellSlots[0].current, 4);
    CHECK_EQ(character.spellSlots[1].current, 3);
}

TEST_CASE("proficiency follows the SRD table and saves add it only when proficient")
{
    CHECK_EQ(proficiencyBonusForLevel(0), 2);
    CHECK_EQ(proficiencyBonusForLevel(4), 2);
    CHECK_EQ(proficiencyBonusForLevel(5), 3);
    CHECK_EQ(proficiencyBonusForLevel(20), 6);
    CHECK_EQ(proficiencyBonusForLevel(21), 7);
    CHECK_EQ(proficiencyBonusForLevel(30), 9);
    CHECK_EQ(proficiencyBonusForLevel(31), 9);

    Character character = wizard();
    CHECK_EQ(totalClassLevel(character), 5);
    const int proficiency = proficiencyBonusForLevel(totalClassLevel(character));
    CHECK_EQ(proficiency, 3);
    CHECK_EQ(saveBonus(character.abilities.intelligence, true, proficiency), 7);
    CHECK_EQ(saveBonus(character.abilities.wisdom, false, proficiency), -1);
    CHECK_EQ(dexterityInitiativeModifier(character), 3);
}
