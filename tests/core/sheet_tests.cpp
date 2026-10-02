#include "core/character.h"
#include "core/sheet.h"
#include "test_harness.h"

#include <string>

using namespace combat;

namespace {

Spell spell(std::string id, std::string name, int level)
{
    Spell row;
    row.id = std::move(id);
    row.name = std::move(name);
    row.level = level;
    row.school = "Evocation";
    row.castingTime = "Action";
    row.range = "60 feet";
    row.duration = "Instantaneous";
    row.description = "A test description.";
    return row;
}

}  // namespace

TEST_CASE("spell search is a case-insensitive name substring and sorts by name")
{
    const std::vector<Spell> spells{spell("zeta", "Zeta Bolt", 1), spell("acid-arrow", "Acid Arrow", 2),
                                    spell("aid", "Aid", 2)};
    const auto all = searchSpells(spells, "");
    CHECK_EQ(all.size(), std::size_t{3});
    CHECK_EQ(all[0].id, std::string("acid-arrow"));
    CHECK_EQ(all[1].id, std::string("aid"));
    CHECK_EQ(all[2].id, std::string("zeta"));

    const auto found = searchSpells(spells, "ARROW");
    CHECK_EQ(found.size(), std::size_t{1});
    CHECK_EQ(found[0].name, std::string("Acid Arrow"));
    CHECK(searchSpells(spells, "nope").empty());
}

TEST_CASE("findSpellById returns the row or nothing")
{
    const std::vector<Spell> spells{spell("acid-arrow", "Acid Arrow", 2)};
    const auto found = findSpellById(spells, "acid-arrow");
    CHECK(found.has_value());
    CHECK_EQ(found->level, 2);
    CHECK(!findSpellById(spells, "missing").has_value());
}

TEST_CASE("condition and species search match the monster catalog rule")
{
    const std::vector<Condition> conditions{
        Condition{"stunned", "Stunned", "A stunned creature.", std::vector<std::string>{}},
        Condition{"blinded", "Blinded", "A blinded creature.", std::vector<std::string>{}}};
    const auto found = searchConditions(conditions, "blind");
    CHECK_EQ(found.size(), std::size_t{1});
    CHECK_EQ(found[0].id, std::string("blinded"));
    CHECK(searchConditions(conditions, " ").empty());
    const auto blinded = findConditionById(conditions, "blinded");
    CHECK(blinded.has_value());
    CHECK_EQ(blinded->description, std::string("A blinded creature."));

    const auto species = searchSpecies({"Human", "Elf", "Dwarf"}, "");
    CHECK_EQ(species.front(), std::string("Dwarf"));
    CHECK_EQ(species.back(), std::string("Human"));
    CHECK_EQ(searchSpecies({"Elf", "Tiefling"}, "ELF").size(), std::size_t{1});
}

TEST_CASE("skill flags use the SRD skill table")
{
    CHECK_EQ(kSkills.size(), std::size_t{18});
    CHECK_EQ(std::string(kSkills[0].key), std::string("acrobatics"));
    CHECK_EQ(std::string(kSkills[0].ability), std::string("Dexterity"));
    CHECK_EQ(std::string(kSkills[1].key), std::string("animalHandling"));
    CHECK_EQ(std::string(kSkills[1].ability), std::string("Wisdom"));
    CHECK_EQ(std::string(kSkills[15].key), std::string("sleightOfHand"));
    CHECK_EQ(std::string(kSkills[15].ability), std::string("Dexterity"));
    CHECK_EQ(kSavingThrows.size(), std::size_t{6});
    CHECK_EQ(std::string(kSavingThrows[4].key), std::string("wisdom"));
}

TEST_CASE("validateCharacter checks classes, spells, slots, gear, and conditions")
{
    Character character;
    character.id = "id-1";
    character.name = "Aria";
    character.classes.push_back(ClassLevel{"", 1, ""});
    CHECK(!validateCharacter(character).empty());

    character.classes[0].name = "Wizard";
    character.spells.push_back(CharacterSpell{"", "   ", false});
    CHECK(!validateCharacter(character).empty());

    character.spells[0].name = "Pocket Star";
    character.gear.push_back(GearItem{"  ", 1, false});
    CHECK(!validateCharacter(character).empty());

    character.gear[0].name = "Rope";
    character.conditions.push_back("blinded");
    character.conditions.push_back("blinded");
    CHECK(!validateCharacter(character).empty());

    character.conditions.pop_back();
    character.spellSlots.push_back(SpellSlot{1, 2, 2});
    character.spellSlots.push_back(SpellSlot{1, 0, 2});
    CHECK(!validateCharacter(character).empty());

    character.spellSlots.pop_back();
    CHECK(validateCharacter(character).empty());
}
