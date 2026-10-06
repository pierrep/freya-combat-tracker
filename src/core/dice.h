#pragma once

#include <functional>
#include <optional>
#include <string>

namespace combat {

// Returns one face, from 1 through sides. The caller owns the RNG, so core
// never reads the clock or an entropy source.
using RollDie = std::function<int(int sides)>;

// "2d6+3", "1d4 - 1", "17d6", or a flat "5". Spaces are allowed around the
// sign. count is 0 for a flat number.
struct Dice {
    int count = 0;
    int sides = 0;
    int modifier = 0;

    bool operator==(const Dice&) const = default;
};

std::optional<Dice> parseDice(const std::string& text);
std::string formatDice(const Dice& dice);

// Rolls the dice. A critical hit rolls the dice twice (modifiers once). The
// result is never below 0.
int rollDice(const Dice& dice, const RollDie& rollDie, bool critical = false);

// The average a stat block prints, rounded down.
int averageDice(const Dice& dice);

}  // namespace combat
