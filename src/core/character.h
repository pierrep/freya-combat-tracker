#pragma once

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

struct Character {
    std::string id;
    std::string name;
    int hp = 0;
    int ac = 10;
    AbilityScores abilities;
    int passivePerception = 10;

    bool operator==(const Character&) const = default;
};

// floor((score - 10) / 2), rounding toward negative infinity for low scores.
int abilityModifier(int score);

// Formats a modifier the way a stat block shows it: "+3", "+0", "-1".
std::string formatModifier(int modifier);

// Light v1 checks. Integer fields are always integers in C++, so only the name
// can fail here; the JSON reader rejects non-integer values before this point.
// Returns an empty list when the character is valid.
std::vector<std::string> validateCharacter(const Character& character);

}  // namespace combat
