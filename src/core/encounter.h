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

// One row in a fight. HP, AC, and a monster's initiative bonus are copies taken
// when the combatant was added. Later edits to the sheet or the monster do not
// change this row, and edits here do not write back.
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
    int ac = 10;

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

// "Goblin Warrior 1", then "Goblin Warrior 2". The number is one higher than
// the highest numbered copy of this source id already in the fight.
std::string nextMonsterCopyName(const std::string& monsterName, const std::string& sourceId,
                                const std::vector<Combatant>& existing);

Combatant makeCharacterCombatant(const Character& character, const std::string& combatantId);
Combatant makeMonsterCombatant(const Monster& monster, const std::vector<Combatant>& existing,
                               const std::string& combatantId);

// Highest initiative first. Equal initiatives keep their previous order.
// Returns the new index of the combatant who was at activeIndex. An empty list
// returns 0.
int sortByInitiative(std::vector<Combatant>& combatants, int activeIndex);

// Swaps the combatant at index one step up (direction -1) or down (direction +1).
// turnIndex follows the same combatant it pointed at. A move past either end
// leaves the list unchanged.
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

// Next combatant. Wrapping past the end starts the next round at the top.
// An empty fight does nothing.
void advanceTurn(Encounter& encounter);

// Previous combatant. Wrapping past the start goes to the last combatant and,
// when the round is above 1, back one round. Round never drops below 1.
void retreatTurn(Encounter& encounter);

// Starts the next round at the first combatant. An empty fight does nothing.
void advanceRound(Encounter& encounter);

}  // namespace combat
