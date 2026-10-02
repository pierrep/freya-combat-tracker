#include "core/character.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace combat {

namespace {

bool isBlank(const std::string& text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

}  // namespace

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

    std::unordered_set<std::string> conditionIds;
    for (const std::string& id : character.conditions) {
        if (id.empty()) {
            problems.emplace_back("Condition id is required.");
            break;
        }
        if (!conditionIds.insert(id).second) {
            problems.emplace_back("Duplicate condition id.");
            break;
        }
    }

    return problems;
}

}  // namespace combat
