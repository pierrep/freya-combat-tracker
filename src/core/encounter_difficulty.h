#pragma once

#include "core/encounter.h"

#include <string>
#include <vector>

namespace combat {

// 2024 Dungeon Master's Guide XP budget per character, by level (1-20).
struct XpBudget {
    int low = 0;
    int moderate = 0;
    int high = 0;

    bool operator==(const XpBudget&) const = default;
};

XpBudget xpBudgetForLevel(int level);
XpBudget partyXpBudget(const std::vector<int>& levels);

enum class Difficulty { None, BelowLow, Low, Moderate, High, AboveHigh };

const char* difficultyLabel(Difficulty difficulty);
Difficulty rateEncounter(int monsterXp, const XpBudget& budget);

struct EncounterRating {
    int monsterXp = 0;
    XpBudget budget;
    Difficulty difficulty = Difficulty::None;
    int characters = 0;
    int levelsUnknown = 0;  // characters whose sheet has no class level
};

// Sums monster XP from each row's stat block (or the CR when no block was
// kept) and the party's budget from the roster's class levels.
EncounterRating rateEncounter(const Encounter& encounter, const std::vector<Character>& roster);

}  // namespace combat
