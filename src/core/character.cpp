#include "core/character.h"

#include "core/text.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace combat {

namespace {

int clampToInt(long long value)
{
    if (value > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    if (value < std::numeric_limits<int>::min()) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(value);
}

}  // namespace

const char* abilityKey(Ability ability)
{
    return kSavingThrows[static_cast<std::size_t>(ability)].key;
}

const char* abilityLabel(Ability ability)
{
    return kSavingThrows[static_cast<std::size_t>(ability)].label;
}

const char* abilityShort(Ability ability)
{
    static constexpr std::array<const char*, 6> names{{"Str", "Dex", "Con", "Int", "Wis", "Cha"}};
    return names[static_cast<std::size_t>(ability)];
}

std::optional<Ability> abilityFromKey(const std::string& key)
{
    for (const Ability ability : kAbilityOrder) {
        if (equalsInsensitive(key, abilityKey(ability)) || equalsInsensitive(key, abilityShort(ability))) {
            return ability;
        }
    }
    return std::nullopt;
}

int abilityScore(const AbilityScores& scores, Ability ability)
{
    switch (ability) {
    case Ability::Strength:
        return scores.strength;
    case Ability::Dexterity:
        return scores.dexterity;
    case Ability::Constitution:
        return scores.constitution;
    case Ability::Intelligence:
        return scores.intelligence;
    case Ability::Wisdom:
        return scores.wisdom;
    case Ability::Charisma:
        return scores.charisma;
    }
    return 10;
}

int& abilityScoreRef(AbilityScores& scores, Ability ability)
{
    switch (ability) {
    case Ability::Strength:
        return scores.strength;
    case Ability::Dexterity:
        return scores.dexterity;
    case Ability::Constitution:
        return scores.constitution;
    case Ability::Intelligence:
        return scores.intelligence;
    case Ability::Wisdom:
        return scores.wisdom;
    case Ability::Charisma:
        break;
    }
    return scores.charisma;
}

bool saveProficient(const SavingThrows& saves, Ability ability)
{
    return saves.*kSavingThrows[static_cast<std::size_t>(ability)].member;
}

bool isDamageType(const std::string& type)
{
    return std::find(kDamageTypes.begin(), kDamageTypes.end(), type) != kDamageTypes.end();
}

int abilityModifier(int score)
{
    const long long delta = static_cast<long long>(score) - 10;
    const long long modifier = delta >= 0 ? delta / 2 : -((-delta + 1) / 2);
    return static_cast<int>(modifier);
}

std::string formatModifier(int modifier)
{
    return (modifier >= 0 ? "+" : "") + std::to_string(modifier);
}

std::string formatHitPoints(int current, std::optional<int> maximum)
{
    if (!maximum.has_value()) {
        return std::to_string(current);
    }
    return std::to_string(current) + " / " + std::to_string(*maximum);
}

std::vector<std::string> validateCharacter(const Character& character)
{
    std::vector<std::string> problems;
    if (isBlank(character.name)) {
        problems.emplace_back("Name is required.");
    }
    if (character.id.empty()) {
        problems.emplace_back("Id is required.");
    }
    if (character.hp.current < 0 || character.hp.max < 0 || character.tempHp < 0) {
        problems.emplace_back("Hit points cannot be negative.");
    }
    if (character.exhaustion < 0 || character.exhaustion > 6) {
        problems.emplace_back("Exhaustion must be 0 through 6.");
    }

    for (const ClassLevel& row : character.classes) {
        if (isBlank(row.name)) {
            problems.emplace_back("Class name is required.");
            break;
        }
    }

    std::unordered_set<std::string> spellIds;
    for (const CharacterSpell& spell : character.spells) {
        if (spell.id.empty()) {
            if (isBlank(spell.name)) {
                problems.emplace_back("Spell needs an id or a name.");
                break;
            }
            continue;
        }
        if (!spellIds.insert(spell.id).second) {
            problems.emplace_back("Duplicate spell id.");
            break;
        }
    }

    std::unordered_set<int> slotLevels;
    for (const SpellSlot& slot : character.spellSlots) {
        if (slot.level < 1) {
            problems.emplace_back("Spell slot level must be at least 1.");
            break;
        }
        if (!slotLevels.insert(slot.level).second) {
            problems.emplace_back("Duplicate spell slot level.");
            break;
        }
    }

    for (const GearItem& item : character.gear) {
        if (isBlank(item.name)) {
            problems.emplace_back("Gear name is required.");
            break;
        }
    }
    return problems;
}

int totalClassLevel(const Character& character)
{
    long long total = 0;
    for (const ClassLevel& row : character.classes) {
        total += row.level;
    }
    return clampToInt(total);
}

int proficiencyBonusForLevel(int totalLevel)
{
    if (totalLevel <= 4) {
        return 2;
    }
    if (totalLevel >= 29) {
        return 9;
    }
    return 2 + (totalLevel - 1) / 4;
}

int proficiencyBonus(const Character& character)
{
    return character.proficiencyOverride.value_or(proficiencyBonusForLevel(totalClassLevel(character)));
}

int initiativeModifier(const Character& character)
{
    return character.initiativeOverride.value_or(abilityModifier(character.abilities.dexterity));
}

int saveBonus(int abilityScore, bool proficient, int proficiencyBonus)
{
    long long total = abilityModifier(abilityScore);
    if (proficient) {
        total += proficiencyBonus;
    }
    return clampToInt(total);
}

int characterSaveBonus(const Character& character, Ability ability)
{
    return saveBonus(abilityScore(character.abilities, ability), saveProficient(character.savingThrows, ability),
                     proficiencyBonus(character));
}

int hitDieForClass(const std::string& className)
{
    const std::string name = asciiLower(trim(className));
    if (name == "barbarian") {
        return 12;
    }
    if (name == "fighter" || name == "paladin" || name == "ranger") {
        return 10;
    }
    if (name == "sorcerer" || name == "wizard") {
        return 6;
    }
    return 8;
}

}  // namespace combat
