#include "core/sheet.h"

#include "core/text.h"

#include <algorithm>

namespace combat {


std::vector<Spell> searchSpells(const std::vector<Spell>& spells, const std::string& nameSubstring)
{
    std::vector<Spell> matches;
    matches.reserve(spells.size());
    for (const Spell& spell : spells) {
        if (containsInsensitive(spell.name, nameSubstring)) {
            matches.push_back(spell);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const Spell& left, const Spell& right) {
        const int nameOrder = compareNames(left.name, right.name);
        if (nameOrder != 0) {
            return nameOrder < 0;
        }
        return left.id < right.id;
    });
    return matches;
}

std::optional<Spell> findSpellById(const std::vector<Spell>& spells, const std::string& id)
{
    for (const Spell& spell : spells) {
        if (spell.id == id) {
            return spell;
        }
    }
    return std::nullopt;
}

std::vector<Condition> searchConditions(const std::vector<Condition>& conditions, const std::string& nameSubstring)
{
    std::vector<Condition> matches;
    matches.reserve(conditions.size());
    for (const Condition& condition : conditions) {
        if (containsInsensitive(condition.name, nameSubstring)) {
            matches.push_back(condition);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const Condition& left, const Condition& right) {
        const int nameOrder = compareNames(left.name, right.name);
        if (nameOrder != 0) {
            return nameOrder < 0;
        }
        return left.id < right.id;
    });
    return matches;
}

std::optional<Condition> findConditionById(const std::vector<Condition>& conditions, const std::string& id)
{
    for (const Condition& condition : conditions) {
        if (condition.id == id) {
            return condition;
        }
    }
    return std::nullopt;
}

std::vector<std::string> searchSpecies(const std::vector<std::string>& species, const std::string& nameSubstring)
{
    std::vector<std::string> matches;
    matches.reserve(species.size());
    for (const std::string& name : species) {
        if (containsInsensitive(name, nameSubstring)) {
            matches.push_back(name);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const std::string& left, const std::string& right) {
        return compareNames(left, right) < 0;
    });
    return matches;
}

}  // namespace combat
