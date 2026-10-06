#include "core/attack_damage.h"
#include "core/combat_rules.h"
#include "test_harness.h"

#include <optional>
#include <string>
#include <vector>

using namespace combat;

TEST_CASE("rend damage sums every expression and attacks without dice do not roll")
{
    const std::string rend =
        "Melee Attack Roll: +11, reach 10 ft. Hit: 17 (2d10 + 6) Slashing damage plus 4 (1d8) Fire damage.";
    const std::vector<DamageExpression> expressions = damageExpressions(rend);
    CHECK_EQ(expressions.size(), std::size_t{2});
    CHECK_EQ(expressions[0], (DamageExpression{2, 10, 6}));
    CHECK_EQ(expressions[1], (DamageExpression{1, 8, 0}));

    std::vector<int> faces{1, 10, 8};
    std::size_t next = 0;
    const std::optional<int> total = rollAttackDamage(rend, [&](int sides) {
        CHECK_EQ(sides, next < 2 ? 10 : 8);
        const int face = faces.at(next);
        ++next;
        return face;
    });
    CHECK(total.has_value());
    CHECK_EQ(*total, 1 + 10 + 6 + 8);
    CHECK_EQ(next, faces.size());

    const std::optional<int> compact =
        rollAttackDamage("Hit: 12 (2d10+6) Slashing damage plus 4 (1d8) Fire damage.", [](int sides) {
            return sides;
        });
    CHECK(compact.has_value());
    CHECK_EQ(*compact, 10 + 10 + 6 + 8);

    CHECK(damageExpressions(
              "The dragon makes three Rend attacks. It can replace one attack with a use of (A) Sleep Breath.")
              .empty());
    CHECK(damageExpressions("Constitution Saving Throw: DC 18, each creature in a 60-foot Cone. Failure: The target "
                            "has the Incapacitated condition until the end of its next turn, when it has the "
                            "Unconscious condition.")
              .empty());
    CHECK(damageExpressions("Failure: The target has Disadvantage on Strength-based D20 Tests and subtracts 3 (1d6) "
                            "from its damage rolls.")
              .empty());
    CHECK(!rollAttackDamage("no dice here", [](int) { return 1; }).has_value());

    const std::string shadow =
        "Hit: 5 (1d6 + 2) Necrotic damage, and the target's Strength score decreases by 1d4. A Shadow rises from the "
        "corpse 1d4 hours later.";
    const std::vector<DamageExpression> shadowDice = damageExpressions(shadow);
    CHECK_EQ(shadowDice.size(), std::size_t{1});
    CHECK_EQ(shadowDice[0], (DamageExpression{1, 6, 2}));
}

TEST_CASE("rolled attack damage is the total applied to hit points")
{
    const std::string rend =
        "Melee Attack Roll: +11, reach 10 ft. Hit: 17 (2d10 + 6) Slashing damage plus 4 (1d8) Fire damage.";
    std::vector<int> faces{1, 10, 8};
    std::size_t next = 0;
    const std::optional<int> total = rollAttackDamage(rend, [&](int) {
        const int face = faces.at(next);
        ++next;
        return face;
    });
    CHECK(total.has_value());
    CHECK_EQ(*total, 25);

    Combatant combatant;
    combatant.hp = 40;
    combatant.tempHp = 5;
    applyDamage(combatant, *total);
    CHECK_EQ(combatant.tempHp, 0);
    CHECK_EQ(combatant.hp, 20);
}
