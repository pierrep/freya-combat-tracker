#pragma once

#include "core/character.h"
#include "core/dice.h"
#include "core/encounter.h"
#include "core/monster.h"
#include "core/sheet.h"

#include <optional>
#include <string>
#include <vector>

namespace combat {

// --- Damage and healing -----------------------------------------------------

struct TypedDamage {
    int amount = 0;
    std::string type;  // empty for untyped damage

    bool operator==(const TypedDamage&) const = default;
};

// Immunity makes it 0, Resistance halves it (rounded down), Vulnerability
// doubles it. Resistance applies before Vulnerability. Untyped damage is not
// changed.
int damageAfterDefenses(int amount, const std::string& type, const Defenses& defenses);

struct DamageResult {
    int rolled = 0;          // before defenses
    int taken = 0;           // after defenses
    int tempSpent = 0;
    int hpLost = 0;
    bool droppedToZero = false;
    bool died = false;
    bool instantDeath = false;  // massive damage
    int deathSaveFailures = 0;
    // Set when a concentrating combatant took damage and is still conscious.
    std::optional<int> concentrationDc;
    bool concentrationEnded = false;
    std::vector<std::string> notes;  // "fire resisted", "poison immune"
    std::vector<std::string> typesTaken{};  // damage types that got through

    bool operator==(const DamageResult&) const = default;
};

// Applies damage parts (one hit) after defenses. Temporary HP is spent first.
// A monster at 0 HP and 0 temporary HP leaves the fight. A character who drops
// to 0 is dying and has the Unconscious condition, unless the damage left over
// is at least its HP maximum, which kills it outright. Damage to a dying
// character adds a death save failure (two on a critical hit), and damage at
// least its HP maximum kills it. A stable character who takes damage is dying
// again. Dropping to 0 ends concentration; otherwise damage to a concentrating
// combatant reports the Constitution save DC. Negative amounts count as 0.
DamageResult applyDamage(Combatant& combatant, const std::vector<TypedDamage>& parts, bool critical = false);
DamageResult applyDamage(Combatant& combatant, int amount, const std::string& type = {}, bool critical = false);

struct HealingResult {
    int healed = 0;
    bool revived = false;  // was at 0 HP

    bool operator==(const HealingResult&) const = default;
};

// Raises current HP up to the stored maximum. Healing a combatant at 0 HP
// resets death saves and removes Unconscious. The dead are not healed.
HealingResult applyHealing(Combatant& combatant, int amount);

// A typed current HP: never below 0, never above a stored maximum. Above 0 it
// brings a dying, stable, or dead combatant back (the GM's correction).
void setHitPoints(Combatant& combatant, int current);
// Dies outright: a character skips death saves (the Solar's Slaying Bow).
void killOutright(Combatant& combatant);

// A creature's size: a monster's from its stat block; a character's from its
// species (Gnomes and Halflings are Small, everyone else Medium).
std::string creatureSize(const Combatant& combatant, const std::string& species = {});
// Why this target cannot be chosen for the action, or empty when it can:
// "Death Glare needs a Frightened creature."
// Escaping a grapple: "a Strength (Athletics) or Dexterity (Acrobatics)
// check against the grapple's escape DC", as an action. The DC of a Grappled
// condition, when known.
std::optional<int> grappleEscapeDc(const ActiveCondition& condition);
// The better of the two checks for this creature, with its bonus (less any
// Exhaustion penalty). sheet is the character's sheet, or null for a monster.
struct EscapeCheck {
    int bonus = 0;
    std::string skill;  // "Athletics" or "Acrobatics"
    Ability ability = Ability::Strength;
};
EscapeCheck escapeCheck(const Combatant& combatant, const Character* sheet);
// Ends the grapple by this grappler; conditions tied to it (Restrained "until
// the grapple ends") go with releaseConditions. False when there was none.
bool escapeGrapple(Combatant& combatant, const std::string& grapplerId);

// Charmed: "can't attack the charmer or target the charmer with damaging
// abilities or magical effects." Why the actor can't target this creature, or
// empty when it can. Only a Charmed whose cause is known counts.
std::string charmedProblem(const Combatant& actor, const Combatant& target);

// A condition list ("charmed,grappled") is met by any one of them; Grappled
// must be by the attacker when attackerId is given and the grapple's cause is
// known.
std::string targetRequirementProblem(const MonsterAttack& attack, const Combatant& target,
                                     const std::string& species = {}, const std::string& attackerId = {});
// Grappled by this combatant, or Grappled with no recorded cause.
bool isGrappledBy(const Combatant& target, const std::string& grapplerId);

// --- Conditions an action gives its target ------------------------------------

// Why the rider cannot affect this target ("Aria is Medium, larger than
// Small"), or empty when it can.
std::string riderBlocked(const ConditionRider& rider, const Combatant& target, const std::string& species = {});
// The riders for one outcome (kRiderOnHit, kRiderOnFailure, ...).
std::vector<const ConditionRider*> ridersFor(const MonsterAttack& attack, const std::string& on);
// "Bugbear Warrior's Grab": what a rider's conditions show as their cause.
std::string riderSourceName(const Combatant& attacker, const MonsterAttack& attack);

struct RiderOutcome {
    std::vector<std::string> added;
    std::vector<std::string> immune;
    std::vector<std::string> already;
    std::vector<std::string> removed;  // ended by it ("no longer Grappled")
    bool stabilized = false;
};

// Gives the target the rider's conditions, caused by the attacker: timed,
// saved against (with the action's save, or its rider save after a hit),
// tied to another condition, with ongoing damage, and so on.
RiderOutcome applyRider(Encounter& encounter, const Combatant& attacker, Combatant& target,
                        const MonsterAttack& attack, const ConditionRider& rider);

// A failed repeat save against a condition that worsens: replaces it with the
// worse conditions. Returns the ones added; empty when it does not worsen.
std::vector<std::string> worsenCondition(Combatant& combatant, const std::string& conditionId);

struct ReleasedCondition {
    std::string combatantId;
    ActiveCondition condition;
};

// Ends conditions whose cause has gone: a grapple whose grappler is
// Incapacitated, dead, or out of the fight; a condition that lasts until its
// source dies; and a condition tied to another that has ended ("Restrained
// until the grapple ends").
std::vector<ReleasedCondition> releaseConditions(Encounter& encounter);

// "On a hit: Grappled (escape DC 14; Large or smaller)." for showing with the
// action.
std::string describeRider(const ConditionRider& rider);
// Target rules, riders, and drains as sentences.
std::vector<std::string> describeActionRules(const MonsterAttack& attack);

struct DrainResult {
    int reduced = 0;  // Hit Point maximum lost
    bool died = false;
    int healed = 0;   // the monster's healing
};

// Lowers the target's Hit Point maximum by the damage taken (of the drain's
// type), down to 0, which kills it, and heals the attacker that much when the
// drain heals.
DrainResult applyDrain(Combatant& target, Combatant& attacker, const HpDrain& drain,
                       const std::vector<TypedDamage>& damage, const DamageResult& result);
// Never below 0.
void setTemporaryHitPoints(Combatant& combatant, int temporary);

// Bloodied: half its Hit Point maximum or fewer. Not a condition, a state
// some abilities check (Bloodied Fury, Rampage).
bool isBloodied(const Combatant& creature);

// A typed current HP. When maximum is empty, current is returned unchanged.
int cappedHitPoints(int current, std::optional<int> maximum);

// --- Death saves --------------------------------------------------------------

enum class DeathSaveResult { NotDying, Success, Failure, DoubleFailure, Stabilized, Died, Revived };

// A death saving throw for a dying character. 1 counts as two failures, 20
// brings the character back with 1 HP, 10 or higher (after the Exhaustion
// penalty) is a success. Three successes stabilize, three failures kill.
DeathSaveResult rollDeathSave(Combatant& combatant, int d20Face);

// Manual counters, 0 through 3. Reaching three successes stabilizes; three
// failures kills.
void adjustDeathSave(Combatant& combatant, bool success, int delta);

// Spare the Dying or a Medicine check. Only a dying character changes.
bool stabilize(Combatant& combatant);

// --- Exhaustion ---------------------------------------------------------------

// The D20 Test penalty: 2 x the Exhaustion level.
int d20Penalty(const Combatant& combatant);
// Speed reduction in feet: 5 x the Exhaustion level.
int exhaustionSpeedPenalty(int level);
// 0 through 6. Level 6 kills.
void setExhaustion(Combatant& combatant, int level);

// --- Conditions and concentration ---------------------------------------------

enum class AddConditionResult { Added, Duplicate, Immune, Empty };

// Adds the condition. A creature immune to it, an empty id, or a duplicate is
// refused. Incapacitated, Paralyzed, Petrified, Stunned, and Unconscious end
// concentration. Exhaustion is a level (setExhaustion), not a condition row.
AddConditionResult addCondition(Combatant& combatant, ActiveCondition condition);
AddConditionResult addCondition(Combatant& combatant, const std::string& conditionId);
bool removeCondition(Combatant& combatant, const std::string& conditionId);

// --- What ends a condition early ---------------------------------------------
//
// A condition records what ends it (ActiveCondition::endsOn), because the same
// condition ends differently by cause: Invisible from Hide ends on an attack
// roll, a saving-throw effect, or a Verbal spell; from the Invisibility spell on
// an attack roll, dealing damage, or casting any spell; the Will-o'-Wisp's
// Vanish on an attack roll or Consume Life; the Invisible Stalker's never.

// End triggers for Invisible gained by taking the Hide action.
std::vector<std::string> hideEndsOn();
// End triggers for the Invisibility spell (which also needs Concentration).
std::vector<std::string> invisibilitySpellEndsOn();

// The events using a monster's ability causes: "action:<name>", an attack roll
// and a saving-throw effect (an action with them), dealing damage (when it did),
// and casting a spell (a Spellcasting action, or text that says the creature
// casts one; Verbal unless it needs "no components" or "no spell components").
std::vector<std::string> actionEvents(const std::string& name, const std::string& effect, const MonsterAttack* attack,
                                      bool dealtDamage);
// Casting this spell: any spell, and a Verbal one when it has a V component.
std::vector<std::string> spellEvents(const Spell& spell);
// A spell with a Verbal component. A spell whose components are not known
// counts as Verbal, as almost every spell is.
bool spellHasVerbalComponent(const Spell& spell);

// "Gives it Invisible, with Concentration. Ends early on an attack roll or using
// Consume Life." for showing with the ability.
std::string describeSelfEffect(const SelfEffect& effect, const std::string& conditionName);

// Removes every condition one of these events ends, and the concentration any
// of them depended on (with the other conditions tied to it). Returns the
// conditions removed.
std::vector<ActiveCondition> endConditionsOn(Combatant& combatant, const std::vector<std::string>& events);
// "Until it takes damage", for damage from this attacker's action: a
// condition that spares that action (kEndsOnSparesPrefix) stays.
std::vector<ActiveCondition> endConditionsOnDamage(Combatant& combatant, const std::string& attackerId,
                                                   const std::string& actionName);

// Gives the creature the ability's condition: starts its concentration (ending
// any other) and replaces the same condition from another cause.
AddConditionResult applySelfEffect(Combatant& combatant, const SelfEffect& effect);

bool conditionEndsConcentration(const std::string& conditionId);
// Incapacitated, or a condition that includes it.
bool isIncapacitated(const Combatant& combatant);

// Constitution save DC to keep concentration: half the damage, at least 10,
// at most 30.
int concentrationDc(int damage);

// Starts concentrating on a spell id or ability name. Any other concentration
// ends first, with the conditions tied to it. Empty ends concentration.
void setConcentration(Combatant& combatant, const std::string& spellId);
// Ends concentration and removes the conditions that depended on it. Returns
// the conditions removed.
std::vector<ActiveCondition> endConcentration(Combatant& combatant);

// Builds a duration that ends at the boundary of the anchor's turn. When the
// anchor is the combatant whose turn it is and the boundary is End, the turn
// in progress does not count.
ConditionDuration makeDuration(const Encounter& encounter, const std::string& anchorId, TurnBoundary boundary,
                               int turns);

// What a condition's tag leaves out: its cause, how long it lasts, what ends
// it ("Vile Appearance", "until the end of Hag's next turn", "DC 13 Con save
// ends").
std::vector<std::string> conditionNotes(const ActiveCondition& condition, const Encounter& encounter);
std::string describeCondition(const ActiveCondition& condition, const std::string& displayName,
                              const Encounter& encounter);

// --- Rolls ----------------------------------------------------------------------

enum class RollMode { Normal, Advantage, Disadvantage };

// Combines two faces for the mode. The second face is ignored for Normal.
int pickD20(RollMode mode, int first, int second);

struct SaveRoll {
    int face = 0;
    int total = 0;
    bool success = false;
    bool automaticFailure = false;

    bool operator==(const SaveRoll&) const = default;
};

// Saving throw with the combatant's stored bonus minus the Exhaustion
// penalty. Paralyzed, Petrified, Stunned, and Unconscious fail Strength and
// Dexterity saves automatically.
SaveRoll rollSave(const Combatant& combatant, Ability ability, int dc, int face);

struct AttackRoll {
    int face = 0;
    int total = 0;
    bool hit = false;
    bool critical = false;

    bool operator==(const AttackRoll&) const = default;
};

// d20 + bonus - attacker's Exhaustion penalty against AC. A natural 20 always
// hits and is a critical hit; a natural 1 always misses.
AttackRoll resolveAttackRoll(int attackBonus, int targetAc, int attackerPenalty, int face);

// Advantage and Disadvantage from conditions. They cancel when both apply.
RollMode suggestedAttackMode(const Combatant& attacker, const Combatant& target, bool melee);

// With the action's own Advantage too ("with Advantage if the target is
// Grappled by the ankheg").
RollMode suggestedAttackMode(const Combatant& attacker, const Combatant& target, const MonsterAttack& attack);

// A melee hit on a Paralyzed or Unconscious target is a critical hit.
bool meleeHitIsCritical(const Combatant& target);
bool isMeleeAttack(const MonsterAttack& attack);

struct DamageOptions {
    bool critical = false;
    bool advantage = false;
    // For "or" and extra parts with no condition recorded.
    bool alternative = false;
    bool conditional = false;
    // The conditions that hold this time (DamagePart::condition values): the
    // ones the app sees (damageConditionsMet) and the ones the GM ticked.
    std::vector<std::string> conditionsMet{};

    bool operator==(const DamageOptions&) const = default;
};

// The kDamageIf... conditions that hold for this attacker against this target
// (the target is Bloodied, the attacker is, the attacker is grappling it).
// Without a target, only the attacker's.
std::vector<std::string> damageConditionsMet(const Combatant& attacker, const Combatant* target);

// Why suggestedAttackMode picked Advantage or Disadvantage: "Aria is
// Prone", "Goblin is Poisoned". Empty for a straight roll.
std::vector<std::string> attackModeReasons(const Combatant& attacker, const Combatant& target,
                                           const MonsterAttack& attack);

// A tick box the GM answers before a monster attacks, for a trait the app
// cannot check: Pack Tactics ("an ally is within 5 feet of the target"),
// Sunlight Sensitivity ("it is in sunlight"), an ally's Aura of Authority.
struct RollQuestion {
    std::string key;       // "<monster id>/<trait name>"
    std::string sourceId;  // the monster with the trait
    std::string trait;
    std::string text;
    bool advantage = true;
    bool sticky = false;   // stays ticked between attacks (sunlight)

    bool operator==(const RollQuestion&) const = default;
};

std::vector<RollQuestion> attackRollQuestions(const Encounter& encounter, const Combatant& attacker);

// "Advantage on its melee attack rolls while it is Bloodied (worked out in the
// fight)." for showing with the trait.
std::string describeAttackModifier(const AttackModifier& modifier);

struct AttackModeChoice {
    RollMode mode = RollMode::Normal;
    std::vector<std::string> advantages;     // why: "Aria is Prone", "Pack Tactics"
    std::vector<std::string> disadvantages;
};

// Automatic attack rolls: Advantage and Disadvantage from conditions and from
// traits (Bloodied Fury, Blood Frenzy, Aversion to Fire, and the ticked
// questions). Both cancel to a straight roll.
AttackModeChoice decideAttackMode(const Encounter& encounter, const Combatant& attacker, const Combatant& target,
                                  const MonsterAttack& attack, const std::vector<std::string>& ticked = {});

struct BenefitResult {
    int tempHp = 0;          // rolled
    bool tempHpKept = false; // it already had more, which it keeps
    int healed = 0;
    bool advantage = false;
    int acBonus = 0;
};

// A helpful action on the creature it picked: Temporary Hit Points (the
// higher of old and new), healing, Advantage on attack rolls and more AC until
// the monster's turn boundary.
BenefitResult applyBenefit(const Encounter& encounter, const Combatant& source, Combatant& target,
                           const MonsterAttack& action, const RollDie& rollDie);

// After damage: traits that change attack rolls when the creature takes a
// damage type ("If the golem takes Fire damage, it has Disadvantage on attack
// rolls ... until the end of its next turn"). Returns the traits started.
std::vector<std::string> noteDamageTaken(const Encounter& encounter, Combatant& target, const DamageResult& result);

// Rolls the parts that apply for these options. A critical hit rolls dice
// twice. Ongoing parts are never rolled here.
std::vector<TypedDamage> rollDamageParts(const std::vector<DamagePart>& parts, const DamageOptions& options,
                                         const RollDie& rollDie);
// Halves every part (rounded down), for a successful save.
std::vector<TypedDamage> halveDamage(const std::vector<TypedDamage>& parts);
int totalDamage(const std::vector<TypedDamage>& parts);
std::string describeDamage(const std::vector<TypedDamage>& parts);

// --- Action economy ---------------------------------------------------------

struct Availability {
    bool available = false;
    std::string reason;  // why not, when not available
};

// Whether a monster can use this entry from its Actions now.
// - Multiattack: on its turn, while its action is unused.
// - An entry named in Multiattack: while that action has attacks left.
// - An attack a legendary action allowed: once, off its turn.
// - Anything else: on its turn, as its action.
// A recharge action that has been used, or an X/Day action with no uses
// left, is not available. Incapacitated creatures cannot act.
Availability actionAvailability(const Combatant& combatant, const MonsterAttack& attack, bool theirTurn);

// Spends what the use costs. Returns false, and changes nothing, when it is
// not available.
bool useAction(Combatant& combatant, const MonsterAttack& attack, bool theirTurn);

// Trait: a limited trait used when the GM says so (Legendary Resistance,
// 3/Day). It costs nothing but its use.
enum class FeatureKind { BonusAction, Reaction, Legendary, Trait };

// The economy only: whether the bonus action, the reaction, or a legendary use
// is free.
Availability featureAvailability(const Combatant& combatant, FeatureKind kind, bool theirTurn);
// Also not available while it recharges or has no uses left today.
Availability featureAvailability(const Combatant& combatant, FeatureKind kind, const MonsterFeature& feature,
                                 bool theirTurn);

// Spends a bonus action, the reaction, or one legendary use, and the
// feature's recharge or one of its uses today. A legendary
// action that names an attack ("makes one Rend attack") allows that attack
// once. Returns false when not available.
bool useFeature(Combatant& combatant, FeatureKind kind, const MonsterFeature& feature, bool theirTurn);

// After the monster damaged a creature that was already Bloodied: its
// features with that trigger (Rampage) can be used this turn. Returns their
// names.
std::vector<std::string> noteDamagedBloodied(Combatant& monster);

// The action is used and no Multiattack attacks or granted attack are left.
bool monsterActionSpent(const Combatant& combatant);

// Clears this round's spending for the start of the combatant's turn.
void startTurnEconomy(Combatant& combatant);

// --- Auras -------------------------------------------------------------------

struct AuraCheck {
    std::string sourceId;  // the monster
    std::string name;      // the trait
    AuraSave aura;

    bool operator==(const AuraCheck&) const = default;
};

// The auras that may affect this creature as its turn starts: from other
// monsters in the fight that are not dead, not switched off, and (for "while
// active" auras) not Incapacitated, unless it is already immune. Allies are
// asked too, except for an "enemies only" aura, which skips other monsters.
// Creatures of the wrong type ("only a Beast or Humanoid") and creatures immune
// to the aura's condition (with nothing else at stake) are not asked.
std::vector<AuraCheck> aurasAtTurnStart(const Encounter& encounter, const std::string& combatantId);

// A creature's type as one lowercase word: "Humanoid (Wizard)" is "humanoid",
// "Swarm of Tiny Beasts" is "beast". Characters are Humanoids.
std::string creatureTypeKey(const Combatant& combatant);

// A failed save: the aura's condition until the start of the target's next
// turn. Returns the result of adding it.
AddConditionResult applyAuraFailure(const Encounter& encounter, Combatant& target, const AuraCheck& check);
// A successful save against an aura that grants immunity.
void markAuraImmune(Combatant& target, const AuraCheck& check);
// The table row for a failure roll ("does nothing"), or empty.
std::string auraFailureOutcome(const AuraSave& aura, int roll);
// Using this action switches off the monster's auras that it suppresses.
// Returns the names switched off.
std::vector<std::string> suppressAuras(Combatant& monster, const std::string& actionName);

// --- Spell slots and rests --------------------------------------------------

// Decrements the current count of the slot at this level by one.
bool spendSpellSlot(Character& character, int level);

// --- Standard actions a feature grants ---------------------------------------

// "The goblin takes the Disengage or Hide action." gives two choices,
// {Disengage} and {Hide}; "takes the Dash and Disengage actions" gives one,
// {Dash, Disengage}. Empty when the text names none.
std::vector<std::vector<std::string>> standardActionChoices(const std::string& effect);

// The SRD rules for Dash, Disengage, Dodge, Hide, Help, or Search.
std::string standardActionRules(const std::string& action);

// Dash, Disengage, and Dodge as timed effects shown in the Status column:
// Dash and Disengage last to the end of the creature's turn, Dodge to the
// start of its next. Hide is a check (hideCheckBonus); Help and Search are
// left to the table. False when the action has nothing to track.
bool takeStandardAction(Combatant& combatant, const std::string& action);

// Hide is a DC 15 Dexterity (Stealth) check.
inline constexpr int kHideDc = 15;
// The Stealth bonus a stat block lists, or the Dexterity modifier, less
// Exhaustion.
int hideCheckBonus(const Combatant& combatant);

int hitDiceRemaining(const ClassLevel& row);

// Long Rest: all HP, no temporary HP, every spell slot and Hit Point Die
// back, and one Exhaustion level removed.
void longRest(Character& character);

struct ShortRestResult {
    int healed = 0;
    int diceSpent = 0;

    bool operator==(const ShortRestResult&) const = default;
};

// Short Rest: spends up to diceByClass[i] Hit Point Dice of class i, each
// rolling the die plus the Constitution modifier (at least 0), and restores
// Pact Magic slots.
ShortRestResult shortRest(Character& character, const std::vector<int>& diceByClass, const RollDie& rollDie);

}  // namespace combat
