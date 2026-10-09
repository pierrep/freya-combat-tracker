#pragma once

#include "core/encounter.h"
#include "core/monster.h"

#include <optional>
#include <string>
#include <vector>

namespace combat {

// How an automatic spell (no attack roll and no save) lands on the current
// Hit Points. Power Word Kill kills at or under the threshold and damages
// above it. Power Word Stun grants its condition only at or under it.
struct SpellStrike {
    bool applyDamage = true;
    bool applyConditions = true;
    bool kill = false;
};

// One SRD spell as an attack the combat page can aim. slotLevel 0 uses the
// spell's own level. saveDc 0 leaves a save spell unbuilt. An attack spell
// with no attack bonus uses saveDc - 8 when a DC was given.
std::optional<MonsterAttack> spellAsAttack(const std::string& name, int slotLevel, int saveDc,
                                           std::optional<int> attackBonus);

// Combat spells named in an action or feature ("At Will: Fireball", "casts
// Web"). An action that already rolls an attack or a save, or that already
// gives a condition to the user, is left as it is.
std::vector<MonsterAttack> actionableSpells(const Monster& monster, const MonsterAttack& action);
std::vector<MonsterAttack> actionableSpells(const Monster& monster, const MonsterFeature& feature);

SpellStrike resolveSpellStrike(const MonsterAttack& attack, int currentHp);

// Combat spells named in a spell list or a "casts" sentence, as the attack
// would be named ("Fear", "Acid Arrow (level 3)").
std::vector<std::string> combatSpellMentions(const std::string& text);

// How many SRD spells have a combat rule. For tests.
int spellCombatRuleCount();

}  // namespace combat
