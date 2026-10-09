#pragma once

#include "core/character.h"
#include "core/dice.h"
#include "core/monster.h"

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace combat {

inline constexpr const char* kCombatantSourceCharacter = "character";
inline constexpr const char* kCombatantSourceMonster = "monster";

enum class TurnBoundary { Start, End };

// How long a condition lasts. turnsRemaining counts how many times the
// boundary of the anchor combatant's turn has to pass. "Until the end of the
// goblin's next turn" is anchor goblin, End, 1. When it was added during the
// anchor's own turn and the boundary is End, skipNext makes that turn's end
// not count.
struct ConditionDuration {
    std::string anchorId;
    TurnBoundary boundary = TurnBoundary::End;
    int turnsRemaining = 1;
    bool skipNext = false;

    bool operator==(const ConditionDuration&) const = default;
};

// "Repeats the save at the end of each of its turns, ending the effect on a
// success."
struct SaveEnds {
    Ability ability = Ability::Constitution;
    int dc = 10;
    // A failed save turns the condition into these ("Second Failure: the
    // target has the Petrified condition instead"). Empty: it stays.
    std::vector<std::string> worsensTo{};
    bool worseSaveEnds = false;
    std::vector<std::string> worseEndsOn{};

    bool operator==(const SaveEnds&) const = default;
};

struct ActiveCondition {
    std::string id;
    std::optional<ConditionDuration> duration;
    std::optional<SaveEnds> saveEnds;
    // What caused it, shown with it: "Hide", "Invisibility", "Vanish". Empty
    // when it was added by hand with no cause.
    std::string source{};
    // Events that end it early (kEndsOn... in monster.h).
    std::vector<std::string> endsOn{};
    // The concentration it depends on (a spell id or ability name). It ends
    // when that concentration ends, and ending it early ends the concentration.
    std::string concentration{};
    // The combatant whose action caused it: who is grappling, whose turns time
    // it. Empty when added by hand.
    std::string byId{};
    // Another condition from the same cause it ends with: Restrained "until the
    // grapple ends" is tied to Grappled.
    std::string tiedTo{};
    // Damage while it lasts, at the start of each of the creature's turns
    // (kOngoingAtTarget) or of the causing monster's turns (kOngoingAtSource).
    std::vector<DamagePart> ongoing{};
    std::string ongoingAt{};
    // A grapple's escape DC ("Grappled (escape DC 14)").
    std::optional<int> escapeDc{};

    bool operator==(const ActiveCondition&) const = default;
};

// What a combatant has spent this round. Reset at the start of its turn.
struct TurnEconomy {
    bool actionUsed = false;
    bool bonusActionUsed = false;
    bool reactionUsed = false;
    // Attacks left from a Multiattack action taken this turn.
    int attacksRemaining = 0;
    // How many more of each attack that Multiattack allows ("two Claw attacks
    // and uses Bite": Claw 2, Bite 1). Empty when it names no counts that add
    // up (the hydra's Bites), and then only the total limits them.
    std::map<std::string, int> multiattackLeft{};
    // Legendary Action uses left. Reset at the start of the monster's turn.
    int legendaryRemaining = 0;
    // An attack a legendary or bonus action allows ("it makes one Rend
    // attack", Rampage's Bite).
    std::string grantedAttack;
    // Features whose trigger happened this turn (Rampage, after damaging a
    // Bloodied creature).
    std::vector<std::string> triggered{};

    bool operator==(const TurnEconomy&) const = default;
};

// One row in a fight. AC, maximum HP, defenses, and saves are copies. A
// character's copies are refreshed from the sheet each time the fight is
// opened; its current HP, temporary HP, and exhaustion are the sheet's and are
// written back. A monster's stat block is copied when it is added, so later
// catalog edits do not change a fight in progress.
struct Combatant {
    std::string id;
    std::string source;
    std::string sourceId;
    std::string name;
    int initiative = 0;
    // Set for a monster when the bonus was stored. Empty means the bonus was
    // missing: rolls use +0, and the UI says so. Characters leave this empty.
    std::optional<int> initiativeBonus;
    int hp = 0;
    // Empty when a version 1 fight did not store a maximum.
    std::optional<int> maxHp;
    // How far a drain (a Wraith's Life Drain) has lowered the maximum this
    // fight. maxHp is already lowered; this lets a sheet refresh keep it.
    int maxHpReduction = 0;
    int tempHp = 0;
    int ac = 10;
    std::vector<ActiveCondition> conditions;
    // An SRD spell id or a monster ability name ("Vanish"), or empty when the
    // combatant is not concentrating.
    std::string concentration;
    DeathSaves deathSaves;
    // A character at 0 HP with three successes, or stabilized by someone.
    bool stable = false;
    // Three failed death saves, massive damage, or Exhaustion 6.
    bool dead = false;
    int exhaustion = 0;
    Defenses defenses;
    std::vector<std::string> conditionImmunities;
    std::array<int, 6> saveBonuses{};
    TurnEconomy economy;
    // Recharge actions spent and not yet recharged, by action name.
    std::vector<std::string> expended;
    // X/Day actions, by action name, with the uses left.
    std::map<std::string, int> usesRemaining;
    // This monster's auras that are switched off (the Sea Hag in its illusion).
    std::vector<std::string> aurasOff;
    // Auras this creature saved against and is immune to for the fight, as
    // "<monster row id>/<aura name>".
    std::vector<std::string> auraImmunities;
    // Timed rules, by name: a trait's ("Aversion to Fire"), Advantage on attack
    // rolls someone gave it ("advantage:War Cry"), Disadvantage on saves to
    // maintain Concentration ("concentrationDisadvantage:Cloud of Insects"),
    // or more AC ("ac:2:Shimmering Shield", taken off again when it ends).
    // They end like conditions' durations.
    std::vector<std::pair<std::string, ConditionDuration>> timedEffects{};
    // The monster's stat block when it was added. Empty for characters and for
    // fights saved before stat blocks were kept.
    std::optional<Monster> statBlock;

    bool operator==(const Combatant&) const = default;
};

struct Encounter {
    std::string id;
    std::string name;
    int round = 1;
    int turnIndex = 0;
    // False during the initiative phase, before Start combat: nobody has a
    // turn yet. Fights saved before this was kept count as started.
    bool started = true;
    std::vector<Combatant> combatants;

    bool operator==(const Encounter&) const = default;
};

// Returns one face of a d20, from 1 through 20.
using RollD20 = std::function<int()>;

std::vector<std::string> validateCombatant(const Combatant& combatant);
std::vector<std::string> validateEncounter(const Encounter& encounter);

// The AC a timed effect adds ("ac:2:Shimmering Shield" is 2), or 0.
int timedAcBonus(const std::string& effectName);

// "concentrationDisadvantage:Cloud of Insects": Disadvantage on saves to
// maintain Concentration until the duration ends.
inline constexpr const char* kTimedConcentrationDisadvantagePrefix = "concentrationDisadvantage:";

bool isMonsterCombatant(const Combatant& combatant);
bool isCharacterCombatant(const Combatant& combatant);
bool hasCondition(const Combatant& combatant, const std::string& conditionId);

// A character at 0 HP who is neither stable nor dead.
bool isDying(const Combatant& combatant);

// Who takes turns. A monster leaves the order at 0 HP and 0 temporary HP. A
// character stays while dying or stable, because a dying character still
// rolls death saves on its turn, and leaves only when dead.
bool isInInitiative(const Combatant& combatant);

// One monster of a source keeps that monster's name. Two or more are
// "Name 1", "Name 2", in list order, until each already has its own number.
// Returns true when a name changed.
bool assignMonsterCopyNames(std::vector<Combatant>& combatants);

Combatant makeCharacterCombatant(const Character& character, const std::string& combatantId);
Combatant makeMonsterCombatant(const Monster& monster, const std::string& combatantId);

// Copies AC, maximum HP, current and temporary HP, exhaustion, defenses, and
// saves from the sheet onto a character combatant. Returns true when anything
// changed.
bool refreshCharacterCombatant(Combatant& combatant, const Character& character);

// Fills the copies a monster row did not store (stat block, defenses, saves,
// X/Day uses) from the catalog row. An SRD stat block stored by an older
// version gets the rules it lacks (conditions its actions give, target rules,
// auras, aimed bonus actions). Returns true when anything changed.
bool fillMonsterSnapshot(Combatant& combatant, const Monster& monster);
// Conditions a monster's traits give it from the start (the Invisible
// Stalker's Invisible). Skips ones it already has.
void applyTraitEffects(Combatant& combatant, const Monster& monster);

// True when this character is already in the fight.
bool encounterHasCharacter(const Encounter& encounter, const std::string& characterId);

// Starts the fight over for monsters: current HP back to the stored maximum,
// temporary HP, conditions, concentration, exhaustion, recharges, X/Day uses,
// and this round's spending cleared. Characters keep their HP and sheet; their
// initiative goes back to 0, to be typed in again, and their conditions and
// concentration are cleared (one still at 0 HP stays Unconscious). Round goes back to 1 and the turn to the top of the
// order, in the initiative phase.
void resetMonsters(Encounter& encounter);

// Writes a character combatant's current HP, temporary HP, and exhaustion onto
// the matching sheet. Returns false for a monster, or when no roster row has
// that source id.
bool carryCharacterHitPoints(std::vector<Character>& characters, const Combatant& combatant);

// Highest initiative first. Equal initiatives keep their previous order.
// Returns the new index of the combatant who was at activeIndex.
int sortByInitiative(std::vector<Combatant>& combatants, int activeIndex);

struct MoveResult {
    int movedTo = 0;
    int turnIndex = 0;
};
MoveResult moveCombatant(std::vector<Combatant>& combatants, int index, int direction, int turnIndex);

// Removes the combatant at index and returns the turn index that keeps the
// same combatant, or whoever slid into the removed slot.
int removeCombatant(std::vector<Combatant>& combatants, int index, int turnIndex);

// Rolls every monster: d20 + the snapshotted bonus - 2 x exhaustion. With
// group set, monsters from the same stat block share one roll. Then the list
// is sorted and the active combatant stays the same person. Returns how many
// monsters had no bonus stored.
int rollAllMonsterInitiatives(Encounter& encounter, const RollD20& rollD20, bool group = false);

bool rerollMonsterInitiative(Encounter& encounter, const std::string& combatantId, const RollD20& rollD20,
                             bool* bonusMissing = nullptr);

// Rolls for every character, for a table that lets the app roll: d20 + the
// bonus from its sheet (bonuses, by combatant id; +0 when absent) - 2 x
// exhaustion. Then the list is sorted as for monsters.
void rollAllCharacterInitiatives(Encounter& encounter, const RollD20& rollD20,
                                 const std::map<std::string, int>& bonuses);

std::vector<int> initiativeOrder(const std::vector<Combatant>& combatants);

// If the turn marker is on someone who has left the initiative order, moves it
// forward to the next combatant still in that order without changing the
// round.
void keepTurnInInitiative(Encounter& encounter);

// Something the start or end of a turn produced, for the page to show.
struct TurnEvent {
    enum class Kind {
        ConditionEnded,   // a duration ran out
        SaveToEnd,        // a save-ends condition: repeat the save now
        DeathSave,        // a dying character's turn began
        Recharged,        // a recharge roll succeeded
        NotRecharged,     // a recharge roll failed
        OngoingDamage,    // a condition's damage at the start of a turn
    };
    Kind kind = Kind::ConditionEnded;
    std::string combatantId;
    std::string conditionId;
    std::string actionName;
    Ability ability = Ability::Constitution;
    int dc = 0;
    int roll = 0;
    // OngoingDamage: the dice, and the monster that caused it.
    std::vector<DamagePart> damage{};
    std::string sourceId{};
    std::string source{};

    bool operator==(const TurnEvent&) const = default;
};

// Ends the current turn and starts the next one in the initiative order.
// Wrapping past the last starts the next round. End of turn: conditions
// anchored to the end of this combatant's turn count down, and its save-ends
// conditions ask for a save. Start of turn: that combatant's economy resets,
// legendary uses come back, conditions anchored to the start of its turn count
// down, expended recharge actions roll a d6 (with rollDie), and a dying
// character is asked for a death save.
std::vector<TurnEvent> advanceTurn(Encounter& encounter, const RollDie& rollDie = {});

// Ends the initiative phase: round 1 begins with the first creature in the
// order, and that creature's start of turn runs (as advanceTurn does for later
// turns), so its auras, death saves, and durations are handled too.
std::vector<TurnEvent> startCombat(Encounter& encounter, const RollDie& rollDie = {});

// Next round at the first combatant in the order. Runs the same start of turn
// effects as advanceTurn.
std::vector<TurnEvent> advanceRound(Encounter& encounter, const RollDie& rollDie = {});

// The fight, and the roster when that edit wrote sheets, as they were before
// one change.
struct FightUndo {
    Encounter encounter;
    std::optional<std::vector<Character>> roster;
};

void restoreFightUndo(Encounter& encounter, std::vector<Character>& characters, const FightUndo& undo);

}  // namespace combat
