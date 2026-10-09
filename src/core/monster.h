#pragma once

#include "core/character.h"
#include "core/dice.h"

#include <array>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace combat {

inline constexpr const char* kSrdMonsterSource = "srd-5.2.1";
inline constexpr const char* kCustomMonsterSource = "custom";

// Thrown when a monster file cannot be read or a custom monster cannot be saved.
class MonsterDataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// When a damage part applies.
// - Always: every hit or failed save.
// - Advantage: added when the attack roll had Advantage.
// - AdvantageAlt: replaces the part before it when the attack had Advantage.
// - Alternative: replaces the part before it when the GM picks it (Bloodied,
//   charged 20 feet, two-handed).
// - Conditional: extra damage the GM opts into (a charge, a rider).
// - Ongoing: damage on later turns. Never rolled with the hit; it is listed.
enum class DamageWhen { Always, Advantage, AdvantageAlt, Alternative, Conditional, Ongoing };

const char* damageWhenKey(DamageWhen when);
std::optional<DamageWhen> damageWhenFromKey(const std::string& key);

// What an "or" or extra damage part needs, when the app can tell by itself.
// Anything else in DamagePart::condition is the stat block's own words, asked
// as a tick box on the action: "the boar moved 20+ feet straight toward it
// immediately before the hit".
inline constexpr const char* kDamageIfTargetBloodied = "@targetBloodied";
inline constexpr const char* kDamageIfSelfBloodied = "@selfBloodied";
inline constexpr const char* kDamageIfGrappledBySelf = "@grappledBySelf";

struct DamagePart {
    std::string dice;   // "1d6+2", or a flat "1"
    std::string type;   // lower-case damage type, or empty when untyped
    DamageWhen when = DamageWhen::Always;
    // For Alternative and Conditional parts: when it applies (kDamageIf..., or
    // the stat block's words). Empty: the GM decides with the generic choice.
    std::string condition{};

    bool operator==(const DamagePart&) const = default;
};

struct SaveSpec {
    Ability ability = Ability::Dexterity;
    int dc = 10;
    bool halfOnSuccess = false;
    // The target saves with Advantage when this holds; only the GM knows, so
    // it is a tick box ("the vampire or its allies are fighting it", Charm
    // Person).
    std::string advantageIf{};

    bool operator==(const SaveSpec&) const = default;
};

// When a rider applies (ConditionRider::on).
inline constexpr const char* kRiderOnHit = "hit";
inline constexpr const char* kRiderOnFailure = "failure";
inline constexpr const char* kRiderOnFailureBy5 = "failureBy5";  // replaces the failure riders
inline constexpr const char* kRiderOnZeroHp = "zeroHp";          // the hit dropped the target to 0
inline constexpr const char* kRiderOnCast = "cast";              // granted as the spell is cast

// How long a rider's condition lasts (ConditionRider::until). Empty is until
// it is removed (or something in endsOn ends it).
inline constexpr const char* kUntilSourceStart = "sourceStart";  // start of the monster's next turn
inline constexpr const char* kUntilSourceEnd = "sourceEnd";
inline constexpr const char* kUntilTargetStart = "targetStart";
inline constexpr const char* kUntilTargetEnd = "targetEnd";
inline constexpr const char* kUntilTargetThisTurn = "targetThisTurn";  // "until the end of its turn"
inline constexpr const char* kUntilMinute = "minute";                  // 10 rounds

// Where ongoing damage lands (ConditionRider::ongoingAt).
inline constexpr const char* kOngoingAtTarget = "target";  // start of each of the target's turns
inline constexpr const char* kOngoingAtSource = "source";  // start of each of the monster's turns

// One entry from a monster's Actions. On the Multiattack row, count is how
// many attacks (and uses) that action makes. Elsewhere count is how many
// times Multiattack grants that entry (1 when the SRD does not number it).
// What can end a condition early, as the 2024 rules word it. "action:<name>"
// is using that action ("action:Consume Life").
inline constexpr const char* kEndsOnAttackRoll = "attackRoll";
inline constexpr const char* kEndsOnSaveEffect = "saveEffect";
inline constexpr const char* kEndsOnVerbalSpell = "verbalSpell";
inline constexpr const char* kEndsOnAnySpell = "anySpell";
inline constexpr const char* kEndsOnDealsDamage = "dealsDamage";
inline constexpr const char* kEndsOnActionPrefix = "action:";
// The creature with the condition takes damage ("until it takes damage").
inline constexpr const char* kEndsOnTakesDamage = "takesDamage";
// With kEndsOnTakesDamage: damage from this action of the creature that gave
// the condition does not end it ("spares:Bite": the Vampire's Charm and its
// Bite).
inline constexpr const char* kEndsOnSparesPrefix = "spares:";
// The monster that caused it dies or leaves the fight ("until the aboleth dies").
inline constexpr const char* kEndsOnSourceGone = "sourceGone";

// A condition an ability gives the creature that uses it: the Imp's
// Invisibility, the Will-o'-Wisp's Vanish. On a trait it is there from the
// start of the fight (the Invisible Stalker).
struct SelfEffect {
    std::string condition;      // condition id, "invisible"
    std::string source;         // shown with the condition, "Vanish"
    std::string concentration;  // spell id or ability name it needs; empty for none
    std::vector<std::string> endsOn;

    bool operator==(const SelfEffect&) const = default;
};

// A trait that makes creatures save when they start their turn near the
// monster: the Sea Hag's Vile Appearance, the Ghast's Stench. The app cannot
// see distances, so it asks the GM whether each creature is in range.
struct AuraOutcome {
    int upTo = 0;      // highest die face for this row ("1-4" is 4)
    std::string text;  // "does nothing"
    bool operator==(const AuraOutcome&) const = default;
};

struct AuraSave {
    Ability ability = Ability::Wisdom;
    int dc = 10;
    std::string range;                      // "within 30 feet of the hag"
    std::string who;                        // "a Beast or Humanoid that can see the hag's true form"; empty for any
    std::string condition;                  // on a failure, until the start of the target's next turn
    bool immuneOnSuccess = false;           // a success makes it immune for the rest of the fight
    bool whileActive = false;               // only while the monster is not Incapacitated
    bool enemiesOnly = false;               // "any enemy" (the Pit Fiend); otherwise allies too
    std::vector<std::string> creatureTypes; // only these ("beast", "humanoid"); empty for any
    int failureDie = 0;                     // a failure rolls this die (Gibbering's d8)
    std::vector<AuraOutcome> failureTable;  // what each roll means
    std::vector<std::string> suppressedBy;  // actions that switch it off ("Illusory Appearance")

    bool operator==(const AuraSave&) const = default;
};

// A condition an action gives its target: "If the target is a Large or
// smaller creature, it has the Grappled condition (escape DC 14)", "Failure:
// The target has the Frightened condition until the end of the mummy's next
// turn."
struct ConditionRider {
    std::vector<std::string> conditions;  // condition ids, given together
    std::string on = kRiderOnHit;         // kRiderOn...
    // Who it can affect: the largest size, the most Hit Points (the Incubus's
    // Nightmare), and creature types or species it skips ("undead", "elf").
    std::string targetMaxSize{};
    std::optional<int> targetMaxHp{};
    std::vector<std::string> exceptTypes{};
    // Something only the GM knows, asked as a yes-or-no question: "the boar
    // moved 20+ feet straight toward it immediately before the hit".
    // "@advantage" is answered by the attack roll itself.
    std::string ask{};
    // On a yes, the damage just dealt is given back (the rug grapples instead).
    bool refundDamage = false;
    std::optional<int> escapeDc{};
    std::string until{};   // kUntil...
    std::string tiedTo{};  // another condition from the same hit it ends with ("grappled")
    bool saveEnds = false; // repeats the save at the end of each of its turns
    // A repeat save the GM triggers by hand (the spell names its own trigger,
    // like losing line of sight). Ignored when saveEnds is set.
    bool saveOnDemand = false;
    std::vector<std::string> endsOn{};
    // A failed repeat save turns it into these instead ("Second Failure").
    std::vector<std::string> worsensTo{};
    bool worseSaveEnds = false;
    std::vector<std::string> worseEndsOn{};
    // Damage at the start of each of the target's or the monster's turns
    // while the condition lasts.
    std::vector<DamagePart> ongoing{};
    std::string ongoingAt{};
    // Conditions from this monster it ends ("swallowed, and no longer Grappled").
    std::vector<std::string> removes{};
    // The target becomes Stable (the Phase Spider's bite at 0 HP).
    bool stabilize = false;

    // "Disadvantage on saving throws to maintain Concentration" (the Black
    // Dragon's Cloud of Insects). Not a condition: a timed effect on the target.
    bool concentrationDisadvantage = false;

    // Spell id this condition needs. It ends when that concentration ends.
    std::string concentration{};

    bool operator==(const ConditionRider&) const = default;
};

// What a helpful action does for the creature it picks (the Frost Giant's War
// Cry, the Unicorn's Shimmering Shield): Temporary Hit Points, healing,
// Advantage on attack rolls, and more AC, until a boundary of the monster's
// turn (kUntilSourceStart or kUntilSourceEnd).
struct Benefit {
    std::string tempHp{};    // dice, "2d10+5"
    std::string healing{};   // dice
    bool advantageOnAttacks = false;
    int acBonus = 0;
    std::string until{};

    bool operator==(const Benefit&) const = default;
};

// "The target's Hit Point maximum decreases by an amount equal to the Necrotic
// damage taken." Empty type is all the damage taken.
struct HpDrain {
    std::string type;
    bool heals = false;  // the monster regains that many Hit Points

    bool operator==(const HpDrain&) const = default;
};

struct MonsterAttack {
    std::string name;
    std::string effect;
    int count = 1;
    std::optional<int> attackBonus;
    std::optional<SaveSpec> save;
    std::vector<DamagePart> damage;
    // Lowest d6 face that recharges it ("Recharge 5-6" is 5).
    std::optional<int> recharge;
    std::optional<int> perDay;
    // A save effect that can catch several creatures (a Cone, a Line, ...).
    bool area = false;
    // Named in the Multiattack text, so a use can come out of that action.
    bool inMultiattack = false;
    std::optional<SelfEffect> selfEffect{};
    // Who it can target: a condition the target must have ("one Frightened
    // creature", "one creature Grappled by the chuul") and the largest size.
    std::string targetCondition{};
    std::string targetMaxSize{};
    // Only these creature types ("humanoid" for Charm Person); empty is any.
    std::vector<std::string> targetTypes{};
    // Never these ("one living creature" skips Undead and Constructs).
    std::vector<std::string> targetExceptTypes{};
    // Only a creature with 0 Hit Points (the Will-o'-Wisp's Consume Life).
    bool targetAtZeroHp = false;
    // On a failed save, a target with this many Hit Points or fewer drops to
    // 0 (Death Glare) or dies (Slaying Bow) instead of taking the damage.
    std::optional<int> failureHpThreshold{};
    std::string failureHpEffect{};  // "dropsToZero" or "dies"
    // A failed save heals the monster this much ("the wisp regains 10 (3d6)").
    std::string failureSelfHealing{};
    std::vector<ConditionRider> riders{};
    // An attack roll whose hit makes the target save before its riders
    // ("If the target is a creature, it is subjected to the following effect").
    std::optional<SaveSpec> riderSave{};
    std::optional<HpDrain> drain{};
    // "+5 (with Advantage if the target is Grappled by the ankheg)".
    bool advantageIfGrappled = false;
    // A helpful action: picks a creature to help instead of one to hurt.
    std::optional<Benefit> benefit{};
    // A spell cast from a list. Multiattack counts it as this action
    // ("Spellcasting", or the Succubus's "Charm") rather than the spell name.
    std::string multiattackAs{};
    // Half the damage on a miss (Acid Arrow's splash).
    bool halfDamageOnMiss = false;
    // The caster may be the target (Invisibility). selfOnly is "on itself":
    // there is nothing to click.
    bool allowSelf = false;
    bool selfOnly = false;
    // Rays, darts, and extra targets. An area stays aimed until End.
    int strikes = 1;
    // The same creature can be chosen again (Scorching Ray, Magic Missile).
    bool repeatSameTarget = false;
    // Spell id the caster concentrates on. Empty when the spell is not one.
    std::string concentration{};
    // The save a condition repeats later, when casting itself is not a save
    // (Power Word Stun).
    std::optional<SaveSpec> repeatSave{};

    bool operator==(const MonsterAttack&) const = default;
};

bool isMultiattack(const MonsterAttack& attack);

// A trait, bonus action, reaction, or legendary action. Read-only text.
// When a trait's Advantage or Disadvantage on attack rolls applies
// (AttackModifier::when). Empty: when the GM ticks it (AttackModifier::ask).
inline constexpr const char* kModifierWhileBloodied = "selfBloodied";
inline constexpr const char* kModifierTargetHurt = "targetHurt";       // "doesn't have all its Hit Points"
inline constexpr const char* kModifierAfterDamage = "afterDamage";     // "If it takes Fire damage ... until the end of its next turn"

// A trait that changes the monster's attack rolls: Pack Tactics, Bloodied
// Fury, Sunlight Sensitivity, the Hobgoblin Captain's Aura of Authority.
struct AttackModifier {
    bool advantage = true;       // false: Disadvantage
    std::string when{};          // kModifier..., or empty for a GM tick
    // The tick box's words: "an ally is within 5 feet of the target".
    std::string ask{};
    // The tick stays until unticked (in sunlight); otherwise it is for one attack.
    bool sticky = false;
    bool meleeOnly = false;
    // Also the monster's allies' attack rolls (Aura of Authority).
    bool alliesToo = false;
    // Not while the monster is Incapacitated.
    bool whileActive = false;
    std::string damageType{};    // kModifierAfterDamage: "fire"

    bool operator==(const AttackModifier&) const = default;
};

struct MonsterFeature {
    std::string name;
    std::string effect;
    std::optional<SelfEffect> selfEffect{};
    // As on an action: lowest d6 face that recharges it, and uses per day.
    // Bonus actions and reactions (the Basilisk's gaze, a mage's Shield).
    std::optional<int> recharge{};
    std::optional<int> perDay{};
    // A trait that asks creatures to save at the start of their turns.
    std::optional<AuraSave> aura{};
    // A bonus action, reaction, or legendary action with a save or an attack
    // roll, aimed at targets like an action (the Medusa's Petrifying Gaze).
    // Its name and effect are the feature's.
    std::optional<MonsterAttack> targeted{};
    // A trait that changes attack rolls.
    std::optional<AttackModifier> attackModifier{};
    // Usable only right after the monster damages a creature that was already
    // Bloodied (Rampage).
    bool afterDamagingBloodied = false;

    bool operator==(const MonsterFeature&) const = default;
};

struct Monster {
    std::string id;
    std::string name;
    std::string size;
    std::string creatureType;
    int ac = 10;
    int hp = 1;
    std::string hitDice;
    std::string speed;
    int initiativeBonus = 0;
    AbilityScores abilities;
    int passivePerception = 10;
    std::string challengeRating;
    std::string source;
    std::vector<MonsterAttack> attacks;
    std::vector<MonsterFeature> traits;
    std::vector<MonsterFeature> bonusActions;
    std::vector<MonsterFeature> reactions;
    std::vector<MonsterFeature> legendaryActions;
    int legendaryActionUses = 0;
    Defenses defenses;
    std::vector<std::string> conditionImmunities;
    // Saving throws the stat block lists. Others use the ability modifier.
    std::array<std::optional<int>, 6> savingThrows{};
    // Skill bonuses the stat block lists, by lower-case skill ("stealth": 6).
    std::map<std::string, int> skills;
    // Empty means the CR table.
    std::optional<int> xp;

    bool operator==(const Monster&) const = default;
};

int monsterSaveBonus(const Monster& monster, Ability ability);

// XP for a challenge rating string ("1/4", "17"). 0 for an unknown rating.
int xpForChallengeRating(const std::string& challengeRating);
int monsterXp(const Monster& monster);

// The damage parts for an attack. A custom attack typed before parts were
// stored has its effect text read instead: every damage clause counts, and
// none has a type.
std::vector<DamagePart> attackDamageParts(const MonsterAttack& attack);

// Damage parts as one line for editing: "1d6+2 slashing, 1d4 slashing
// advantage". Each comma-separated piece is dice, then an optional damage type,
// then an optional when ("advantage", "alternative", "conditional",
// "ongoing", "advantageAlt"), then optionally "if" and when it applies
// ("1d6 piercing conditional if the boar moved 20+ feet straight toward it").
std::string formatDamageParts(const std::vector<DamagePart>& parts);
// "the target is Bloodied" for kDamageIfTargetBloodied; the stat block's words
// as they are.
std::string describeDamageCondition(const std::string& condition);
// Empty text is no parts. Returns empty, with a reason, when a piece cannot be
// read.
std::optional<std::vector<DamagePart>> parseDamageParts(const std::string& text, std::string* error = nullptr);

// One line for the stat block: "+14 to hit, 1d10+8 slashing + 2d4 fire" or
// "DC 21 Dex save (half on success), 17d6 fire, area, Recharge 5-6".
std::string attackSummary(const MonsterAttack& attack);

// The name shown on an ability. A trailing "(Recharge 5-6)" stays in the stored
// name so uses still match, and is left off the title.
std::string abilityDisplayName(const std::string& name);

// Empty nameSubstring lists every monster. Creature type and challenge rating,
// when set, are exact matches. Results are sorted by name; equal names put the
// SRD row before the custom row.
struct MonsterQuery {
    std::string nameSubstring;
    std::optional<std::string> creatureType;
    std::optional<std::string> challengeRating;
};

std::vector<Monster> searchMonsters(const std::vector<Monster>& monsters, const MonsterQuery& query);

// Problems that reject a custom save. Empty means the monster can be saved.
std::vector<std::string> validateCustomMonster(const Monster& monster,
                                               const std::unordered_set<std::string>& srdIds);

}  // namespace combat
