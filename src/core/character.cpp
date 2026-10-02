#include "core/character.h"

#include <algorithm>
#include <cctype>

namespace combat {

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

std::vector<std::string> validateCharacter(const Character& character)
{
    std::vector<std::string> problems;
    const bool blankName = std::all_of(character.name.begin(), character.name.end(),
                                       [](unsigned char c) { return std::isspace(c) != 0; });
    if (blankName) {
        problems.emplace_back("Name is required.");
    }
    if (character.id.empty()) {
        problems.emplace_back("Id is required.");
    }
    return problems;
}

}  // namespace combat
