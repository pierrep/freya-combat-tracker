#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace combat {

// One NdM expression, with the signed modifier from "+ 6" or "- 1".
// modifier is 0 when the text has no modifier.
struct DamageExpression {
    int count = 0;
    int sides = 0;
    int modifier = 0;

    bool operator==(const DamageExpression&) const = default;
};

// Returns one face, from 1 through sides. The caller owns the RNG.
using RollDie = std::function<int(int sides)>;

// Dice this effect deals as damage. A formula counts when its clause deals
// damage or is an ongoing loss of hit points. Durations, ability drains,
// hit-point-maximum reductions, and penalties subtracted from damage rolls
// are left out. "D20" is not a die.
std::vector<DamageExpression> damageExpressions(const std::string& effect);

// Rolls every damage expression and adds the totals. Empty when the effect
// deals no damage, or when rollDie is empty. A negative sum is reported as 0.
std::optional<int> rollAttackDamage(const std::string& effect, const RollDie& rollDie);

}  // namespace combat
