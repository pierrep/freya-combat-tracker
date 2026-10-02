#pragma once

#include "core/character.h"
#include "core/monster.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace combat {

inline constexpr const char* kCombatantSourceCharacter = "character";
inline constexpr const char* kCombatantSourceMonster = "monster";

// One row in a fight. AC, temporary HP, maximum HP, and a monster's initiative
// bonus are copies taken when the combatant was added. A character's current
// HP is the same number as the sheet: edits here write that number back, and
// the next encounter copies it. Monster hit points stay on this row. Later
// edits to the monster catalog do not change this row.
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
    // Empty when a version 1 fight did not store a maximum. Healing is not
    // capped in that case. New combatants snapshot the maximum they had when
    // they were added.
    std::optional<int> maxHp;
    int tempHp = 0;
    int ac = 10;
    std::vector<std::string> conditions;
    // An SRD spell id, or empty when the combatant is not concentrating.
    std::string concentration;
    DeathSaves deathSaves;

    bool operator==(const Combatant&) const = default;
};

struct Encounter {
    std::string id;
    std::string name;
    int round = 1;
    int turnIndex = 0;
    std::vector<Combatant> combatants;

    bool operator==(const Encounter&) const = default;
};

// Returns one face of a d20, from 1 through 20. The caller owns the RNG, so
// core never reads the clock.
using RollD20 = std::function<int()>;

// Light checks. An empty list means the value can be stored.
std::vector<std::string> validateCombatant(const Combatant& combatant);
std::vector<std::string> validateEncounter(const Encounter& encounter);

bool isMonsterCombatant(const Combatant& combatant);

// Still in the initiative order while current HP or temporary HP is above 0.
// Both have to be 0 before the combatant leaves that order. Damage still
// spends temporary HP before current HP.
inline bool isInInitiative(const Combatant& combatant)
{
    return combatant.hp > 0 || combatant.tempHp > 0;
}

// One monster of a source keeps that monster's name. Two or more are
// "Name 1", "Name 2", in list order, until each already has its own number.
// A combatant at 0 HP still counts. Returns true when a name changed.
bool assignMonsterCopyNames(std::vector<Combatant>& combatants);

Combatant makeCharacterCombatant(const Character& character, const std::string& combatantId);
Combatant makeMonsterCombatant(const Monster& monster, const std::vector<Combatant>& existing,
                               const std::string& combatantId);

// Sets each monster's current HP to the maximum stored on that row. A monster
// with no stored maximum is left as it is. Characters, temporary HP,
// conditions, and initiative are unchanged. The monster catalog is not read.
void resetMonsterHitPoints(Encounter& encounter);

// Writes a character combatant's current HP onto the matching sheet. Returns
// false for a monster, or when no roster row has that source id. Maximum HP
// and temporary HP on the sheet are left alone.
bool carryCharacterHitPoints(std::vector<Character>& characters, const Combatant& combatant);

// Highest initiative first. Equal initiatives keep their previous order.
// Returns the new index of the combatant who was at activeIndex. An empty list
// returns 0.
int sortByInitiative(std::vector<Combatant>& combatants, int activeIndex);

// Swaps the combatant at index with the next combatant in that direction who
// is still in the initiative order. Someone at 0 current HP and 0 temporary
// HP stays in the vector and is not moved. turnIndex follows the same
// combatant it pointed at. A move past either end leaves the list unchanged.
struct MoveResult {
    int movedTo = 0;
    int turnIndex = 0;
};
MoveResult moveCombatant(std::vector<Combatant>& combatants, int index, int direction, int turnIndex);

// Removes the combatant at index. The returned turn index stays on the same
// combatant when someone else was removed. Removing the active combatant
// leaves the turn on whoever slid into that slot, or wraps to the start when
// the last combatant was removed.
int removeCombatant(std::vector<Combatant>& combatants, int index, int turnIndex);

// Rolls every monster: d20 + the snapshotted bonus (0 when the bonus is
// missing). Characters are left alone. Copies roll separately. Then the list
// is sorted and the active combatant stays the same person. Returns how many
// monsters had no bonus stored.
int rollAllMonsterInitiatives(Encounter& encounter, const RollD20& rollD20);

// Rolls one monster and sorts again. Other combatants' initiative totals stay
// as they were. Returns false, and does not roll, when the id is missing or
// the combatant is not a monster. Sets bonusMissing when the roll used +0
// because no bonus was stored.
bool rerollMonsterInitiative(Encounter& encounter, const std::string& combatantId, const RollD20& rollD20,
                             bool* bonusMissing = nullptr);

// Indices, in vector order, of combatants still in the initiative order. A
// combatant with both current HP and temporary HP at 0 stays in the vector,
// so healing them, or giving them temporary HP, puts them back in the same
// slot.
std::vector<int> initiativeOrder(const std::vector<Combatant>& combatants);

// If the turn marker is on someone who has left the initiative order, moves
// it forward to the next combatant still in that order, wrapping without
// changing the round. A marker already on someone in the order stays. If
// nobody is left in the order, the index is left as it is.
void keepTurnInInitiative(Encounter& encounter);

// Next combatant still in the initiative order. Wrapping past the last of
// those starts the next round at the first. An empty fight, or one where
// everyone has left the order, does nothing.
void advanceTurn(Encounter& encounter);

// Previous combatant still in the initiative order. Wrapping past the first
// of those goes to the last and, when the round is above 1, back one round.
// Round never drops below 1.
void retreatTurn(Encounter& encounter);

// Starts the next round at the first combatant still in the initiative
// order. If everyone has left it, the turn goes to the start of the vector.
// An empty fight does nothing.
void advanceRound(Encounter& encounter);

}  // namespace combat
