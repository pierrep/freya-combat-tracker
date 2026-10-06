#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace combat {

enum class Ability { Strength = 0, Dexterity, Constitution, Intelligence, Wisdom, Charisma };

inline constexpr std::array<Ability, 6> kAbilityOrder{{Ability::Strength, Ability::Dexterity, Ability::Constitution,
                                                        Ability::Intelligence, Ability::Wisdom, Ability::Charisma}};

// JSON key ("strength") and SRD label ("Strength").
const char* abilityKey(Ability ability);
const char* abilityLabel(Ability ability);
// Three-letter tag: "Str", "Dex", ...
const char* abilityShort(Ability ability);
std::optional<Ability> abilityFromKey(const std::string& key);

struct AbilityScores {
    int strength = 10;
    int dexterity = 10;
    int constitution = 10;
    int intelligence = 10;
    int wisdom = 10;
    int charisma = 10;

    bool operator==(const AbilityScores&) const = default;
};

int abilityScore(const AbilityScores& scores, Ability ability);
int& abilityScoreRef(AbilityScores& scores, Ability ability);

struct HitPoints {
    int current = 0;
    int max = 0;

    bool operator==(const HitPoints&) const = default;
};

struct ClassLevel {
    std::string name;
    int level = 1;
    std::string subclass;
    // Hit Point Dice of this class spent since the last Long Rest.
    int hitDiceSpent = 0;

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
// Cantrips are not slots. A Pact Magic row comes back on a Short Rest too.
struct SpellSlot {
    int level = 1;
    int current = 0;
    int max = 0;
    bool shortRest = false;

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

bool saveProficient(const SavingThrows& saves, Ability ability);

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

// Damage types are lower-case SRD names ("fire", "slashing").
struct Defenses {
    std::vector<std::string> resistances;
    std::vector<std::string> immunities;
    std::vector<std::string> vulnerabilities;

    bool empty() const { return resistances.empty() && immunities.empty() && vulnerabilities.empty(); }
    bool operator==(const Defenses&) const = default;
};

inline constexpr std::array<const char*, 13> kDamageTypes{{"acid", "bludgeoning", "cold", "fire", "force", "lightning",
                                                           "necrotic", "piercing", "poison", "psychic", "radiant",
                                                           "slashing", "thunder"}};
bool isDamageType(const std::string& type);

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
    // Empty means the Dexterity modifier. Set when the sheet's total differs
    // (a feat, a class feature, or an imported total).
    std::optional<int> initiativeOverride;
    // Empty means the SRD table for the total level.
    std::optional<int> proficiencyOverride;
    AbilityScores abilities;
    int passivePerception = 10;
    std::string species;
    std::vector<ClassLevel> classes;
    SavingThrows savingThrows;
    SkillProficiencies skills;
    std::vector<CharacterSpell> spells;
    std::vector<SpellSlot> spellSlots;
    std::vector<GearItem> gear;
    Defenses defenses;
    // 0 through 6. Carried between fights like hit points.
    int exhaustion = 0;
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

// "12 / 30". When maximum is empty, the current number alone.
std::string formatHitPoints(int current, std::optional<int> maximum);

// Light checks for a stored character. Returns an empty list when the
// character is valid. Hit points, temporary HP, and exhaustion must not be
// negative; exhaustion is at most 6.
std::vector<std::string> validateCharacter(const Character& character);

// Sum of class levels. The SRD proficiency table uses that total.
int totalClassLevel(const Character& character);

// SRD 5.2.1 Proficiency Bonus table (level or CR): up to 4 is +2, then +1 for
// each further band of 4, through 29-30 at +9.
int proficiencyBonusForLevel(int totalLevel);

// The override when one is stored, otherwise the table for the total level.
int proficiencyBonus(const Character& character);

// The override when one is stored, otherwise the Dexterity modifier.
int initiativeModifier(const Character& character);

// Ability modifier plus the proficiency bonus when proficient.
int saveBonus(int abilityScore, bool proficient, int proficiencyBonus);
int characterSaveBonus(const Character& character, Ability ability);

// Hit Point Die size for an SRD class name (Barbarian 12, Fighter 10, ...).
// Unknown classes use 8.
int hitDieForClass(const std::string& className);

}  // namespace combat
