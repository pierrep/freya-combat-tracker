#include "core/combat_rules.h"

#include <algorithm>
#include <limits>

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

bool applyDamage(Combatant& combatant, int amount)
{
    if (amount < 0) {
        return false;
    }
    long long remaining = amount;
    if (combatant.tempHp > 0) {
        const long long temp = combatant.tempHp;
        const long long spent = std::min(temp, remaining);
        combatant.tempHp = static_cast<int>(temp - spent);
        remaining -= spent;
    }
    const long long next = static_cast<long long>(combatant.hp) - remaining;
    combatant.hp = next < 0 ? 0 : clampToInt(next);
    return true;
}

bool applyHealing(Combatant& combatant, int amount)
{
    if (amount < 0) {
        return false;
    }
    long long next = static_cast<long long>(combatant.hp) + amount;
    if (combatant.maxHp.has_value()) {
        next = std::min(next, static_cast<long long>(*combatant.maxHp));
    }
    combatant.hp = clampToInt(next);
    return true;
}

int cappedHitPoints(int current, std::optional<int> maximum)
{
    if (!maximum.has_value() || current <= *maximum) {
        return current;
    }
    return *maximum;
}

bool addCondition(Combatant& combatant, const std::string& conditionId)
{
    if (conditionId.empty()) {
        return false;
    }
    if (std::find(combatant.conditions.begin(), combatant.conditions.end(), conditionId) !=
        combatant.conditions.end()) {
        return false;
    }
    combatant.conditions.push_back(conditionId);
    return true;
}

bool removeCondition(Combatant& combatant, const std::string& conditionId)
{
    const auto it = std::find(combatant.conditions.begin(), combatant.conditions.end(), conditionId);
    if (it == combatant.conditions.end()) {
        return false;
    }
    combatant.conditions.erase(it);
    return true;
}

void setConcentration(Combatant& combatant, const std::string& spellId)
{
    combatant.concentration = spellId;
}

void adjustDeathSave(Combatant& combatant, bool success, int delta)
{
    int& field = success ? combatant.deathSaves.successes : combatant.deathSaves.failures;
    field = clampToInt(static_cast<long long>(field) + delta);
}

bool spendSpellSlot(Character& character, int level)
{
    for (SpellSlot& slot : character.spellSlots) {
        if (slot.level != level) {
            continue;
        }
        if (slot.current < 1) {
            return false;
        }
        slot.current -= 1;
        return true;
    }
    return false;
}

void finishRest(Character& character)
{
    for (SpellSlot& slot : character.spellSlots) {
        slot.current = slot.max;
    }
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
    if (totalLevel <= 8) {
        return 3;
    }
    if (totalLevel <= 12) {
        return 4;
    }
    if (totalLevel <= 16) {
        return 5;
    }
    if (totalLevel <= 20) {
        return 6;
    }
    if (totalLevel <= 24) {
        return 7;
    }
    if (totalLevel <= 28) {
        return 8;
    }
    return 9;
}

int saveBonus(int abilityScore, bool proficient, int proficiencyBonus)
{
    long long total = abilityModifier(abilityScore);
    if (proficient) {
        total += proficiencyBonus;
    }
    return clampToInt(total);
}

int dexterityInitiativeModifier(const Character& character)
{
    return abilityModifier(character.abilities.dexterity);
}

}  // namespace combat
