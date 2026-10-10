#pragma once

#include "core/encounter.h"
#include "core/monster.h"
#include "core/sheet.h"

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
// Every spell a bonus action, reaction or legendary action can cast ("casts
// Counterspell or Shield"), in the order named: the combat spells above, the
// ones with their own rule here (Shield's +5 AC, Counterspell's save, Cure
// Wounds and Healing Word's healing), and every other spell in the catalog as
// one that is only cast (castOnly). An empty catalog gives the combat spells.
std::vector<MonsterAttack> actionableSpells(const Monster& monster, const MonsterFeature& feature,
                                            const std::vector<Spell>& catalog);

// The Shield a creature's reaction can cast, from its stat block: the feature
// and the spell as built above. Nothing when no reaction casts Shield.
struct ShieldReaction {
    MonsterFeature feature;
    MonsterAttack spell;
};
std::optional<ShieldReaction> shieldReactionOf(const Monster& monster, const std::vector<Spell>& catalog);
// How much AC Shield adds, and the timed effect it is kept as.
inline constexpr int kShieldAcBonus = 5;
// Under Shield now (its +5 AC is on the creature): Magic Missile does nothing.
bool hasShieldUp(const Combatant& combatant);

SpellStrike resolveSpellStrike(const MonsterAttack& attack, int currentHp);

// Combat spells named in a spell list or a "casts" sentence, as the attack
// would be named ("Fear", "Acid Arrow (level 3)").
std::vector<std::string> combatSpellMentions(const std::string& text);

// What a sustained spell costs to use again.
enum class FollowUpCost {
    Action,       // a Magic action (Call Lightning's next bolt)
    BonusAction,  // Spiritual Weapon's next attack
    Free,         // no action: its area hurts a creature that enters it
};

struct SpellFollowUp {
    MonsterAttack attack;  // aimed like the spell itself; no new concentration
    FollowUpCost cost = FollowUpCost::Action;
};

// The record kept when a creature casts a concentration spell it can use
// again on later turns (Call Lightning, Moonbeam, Spiritual Weapon) or whose
// area keeps hurting (Spirit Guardians). Empty for other spells.
std::optional<SustainedSpell> sustainedSpellFor(const MonsterAttack& cast);

// Those later uses, at the level it was cast with: "Call Lightning (again)"
// for a Magic action, "Spirit Guardians (in the aura)" for no action.
std::vector<SpellFollowUp> spellFollowUps(const SustainedSpell& sustained);

// How many SRD spells have a combat rule. For tests.
int spellCombatRuleCount();

}  // namespace combat
