#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace combat {

struct AbilityScores {
    int strength = 10;
    int dexterity = 10;
    int constitution = 10;
    int intelligence = 10;
    int wisdom = 10;
    int charisma = 10;

    bool operator==(const AbilityScores&) const = default;
};

struct HitPoints {
    int current = 0;
    int max = 0;

    bool operator==(const HitPoints&) const = default;
};

struct ClassLevel {
    std::string name;
    int level = 1;
    std::string subclass;

    bool operator==(const ClassLevel&) const = default;
};

// An SRD spell is stored by id. A spell that is not in the catalog is stored
// by name and has an empty id. No description is stored on the character.
struct CharacterSpell {
    std::string id;
    std::string name;
    bool prepared = false;

    bool operator==(const CharacterSpell&) const = default;
};

// One row per slot level the character uses. Current and max are both typed.
// Cantrips are not slots.
struct SpellSlot {
    int level = 1;
    int current = 0;
    int max = 0;

    bool operator==(const SpellSlot&) const = default;
};

struct GearItem {
    std::string name;
    int quantity = 1;
    bool equipped = false;

    bool operator==(const GearItem&) const = default;
};

struct SavingThrows {
    bool strength = false;
    bool dexterity = false;
    bool constitution = false;
    bool intelligence = false;
    bool wisdom = false;
    bool charisma = false;

    bool operator==(const SavingThrows&) const = default;
};

struct SkillProficiencies {
    bool acrobatics = false;
    bool animalHandling = false;
    bool arcana = false;
    bool athletics = false;
    bool deception = false;
    bool history = false;
    bool insight = false;
    bool intimidation = false;
    bool investigation = false;
    bool medicine = false;
    bool nature = false;
    bool perception = false;
    bool performance = false;
    bool persuasion = false;
    bool religion = false;
    bool sleightOfHand = false;
    bool stealth = false;
    bool survival = false;

    bool operator==(const SkillProficiencies&) const = default;
};

struct DeathSaves {
    int successes = 0;
    int failures = 0;

    bool operator==(const DeathSaves&) const = default;
};

// Where a character came from, when an import recorded it. Empty when the
// character was typed in. No remote id is stored.
struct CharacterImport {
    std::string source;
    std::string importedAt;
    std::string fileName;

    bool operator==(const CharacterImport&) const = default;
};

struct Character {
    std::string id;
    std::string name;
    HitPoints hp;
    int tempHp = 0;
    int ac = 10;
    std::string speed;
    int initiativeBonus = 0;
    int proficiencyBonus = 0;
    AbilityScores abilities;
    int passivePerception = 10;
    std::string species;
    std::vector<ClassLevel> classes;
    SavingThrows savingThrows;
    SkillProficiencies skills;
    std::vector<CharacterSpell> spells;
    std::vector<SpellSlot> spellSlots;
    std::vector<GearItem> gear;
    std::vector<std::string> conditions;
    DeathSaves deathSaves;
    std::string notes;
    std::optional<CharacterImport> external;

    bool operator==(const Character&) const = default;
};

struct SaveField {
    const char* key;
    const char* label;
    bool SavingThrows::* member;
};

struct SkillField {
    const char* key;
    const char* name;
    const char* ability;
    bool SkillProficiencies::* member;
};

// Keys match the JSON. Labels and skill abilities are the SRD 5.2.1 names.
inline constexpr std::array<SaveField, 6> kSavingThrows{{
    {"strength", "Strength", &SavingThrows::strength},
    {"dexterity", "Dexterity", &SavingThrows::dexterity},
    {"constitution", "Constitution", &SavingThrows::constitution},
    {"intelligence", "Intelligence", &SavingThrows::intelligence},
    {"wisdom", "Wisdom", &SavingThrows::wisdom},
    {"charisma", "Charisma", &SavingThrows::charisma},
}};

inline constexpr std::array<SkillField, 18> kSkills{{
    {"acrobatics", "Acrobatics", "Dexterity", &SkillProficiencies::acrobatics},
    {"animalHandling", "Animal Handling", "Wisdom", &SkillProficiencies::animalHandling},
    {"arcana", "Arcana", "Intelligence", &SkillProficiencies::arcana},
    {"athletics", "Athletics", "Strength", &SkillProficiencies::athletics},
    {"deception", "Deception", "Charisma", &SkillProficiencies::deception},
    {"history", "History", "Intelligence", &SkillProficiencies::history},
    {"insight", "Insight", "Wisdom", &SkillProficiencies::insight},
    {"intimidation", "Intimidation", "Charisma", &SkillProficiencies::intimidation},
    {"investigation", "Investigation", "Intelligence", &SkillProficiencies::investigation},
    {"medicine", "Medicine", "Wisdom", &SkillProficiencies::medicine},
    {"nature", "Nature", "Intelligence", &SkillProficiencies::nature},
    {"perception", "Perception", "Wisdom", &SkillProficiencies::perception},
    {"performance", "Performance", "Charisma", &SkillProficiencies::performance},
    {"persuasion", "Persuasion", "Charisma", &SkillProficiencies::persuasion},
    {"religion", "Religion", "Intelligence", &SkillProficiencies::religion},
    {"sleightOfHand", "Sleight of Hand", "Dexterity", &SkillProficiencies::sleightOfHand},
    {"stealth", "Stealth", "Dexterity", &SkillProficiencies::stealth},
    {"survival", "Survival", "Wisdom", &SkillProficiencies::survival},
}};

// floor((score - 10) / 2), rounding toward negative infinity for low scores.
int abilityModifier(int score);

// Formats a modifier the way a stat block shows it: "+3", "+0", "-1".
std::string formatModifier(int modifier);

// Light checks for a stored character. Scores, hit points, and bonuses are
// not range-checked. Returns an empty list when the character is valid.
std::vector<std::string> validateCharacter(const Character& character);

}  // namespace combat
