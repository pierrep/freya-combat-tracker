#include "core/encounter_difficulty.h"

#include <algorithm>
#include <array>

namespace combat {

namespace {

constexpr std::array<XpBudget, 20> kBudgets{{
    {50, 75, 100},       {100, 150, 200},     {150, 225, 400},     {250, 375, 500},     {500, 750, 1100},
    {600, 1000, 1400},   {750, 1300, 1700},   {1000, 1700, 2100},  {1300, 2000, 2600},  {1600, 2300, 3100},
    {1900, 2900, 4100},  {2200, 3700, 4700},  {2600, 4200, 5400},  {2900, 4900, 6200},  {3300, 5400, 7800},
    {3800, 6100, 9800},  {4500, 7200, 11700}, {5000, 8700, 14200}, {5500, 10700, 17200}, {6400, 13200, 22000},
}};

}  // namespace

XpBudget xpBudgetForLevel(int level)
{
    return kBudgets[static_cast<std::size_t>(std::clamp(level, 1, 20) - 1)];
}

XpBudget partyXpBudget(const std::vector<int>& levels)
{
    XpBudget total;
    for (const int level : levels) {
        const XpBudget one = xpBudgetForLevel(level);
        total.low += one.low;
        total.moderate += one.moderate;
        total.high += one.high;
    }
    return total;
}

const char* difficultyLabel(Difficulty difficulty)
{
    switch (difficulty) {
    case Difficulty::None:
        return "No rating";
    case Difficulty::BelowLow:
        return "Below Low";
    case Difficulty::Low:
        return "Low";
    case Difficulty::Moderate:
        return "Moderate";
    case Difficulty::High:
        return "High";
    case Difficulty::AboveHigh:
        return "Above High";
    }
    return "No rating";
}

Difficulty rateEncounter(int monsterXp, const XpBudget& budget)
{
    if (budget.high <= 0 || monsterXp <= 0) {
        return Difficulty::None;
    }
    if (monsterXp > budget.high) {
        return Difficulty::AboveHigh;
    }
    if (monsterXp > budget.moderate) {
        return Difficulty::High;
    }
    if (monsterXp > budget.low) {
        return Difficulty::Moderate;
    }
    if (monsterXp == budget.low) {
        return Difficulty::Low;
    }
    return monsterXp * 2 >= budget.low ? Difficulty::Low : Difficulty::BelowLow;
}

EncounterRating rateEncounter(const Encounter& encounter, const std::vector<Character>& roster)
{
    EncounterRating rating;
    std::vector<int> levels;
    for (const Combatant& combatant : encounter.combatants) {
        if (isMonsterCombatant(combatant)) {
            if (combatant.statBlock.has_value()) {
                rating.monsterXp += monsterXp(*combatant.statBlock);
            }
            continue;
        }
        ++rating.characters;
        const auto sheet = std::find_if(roster.begin(), roster.end(),
                                        [&](const Character& character) { return character.id == combatant.sourceId; });
        const int level = sheet == roster.end() ? 0 : totalClassLevel(*sheet);
        if (level < 1) {
            ++rating.levelsUnknown;
            continue;
        }
        levels.push_back(level);
    }
    rating.budget = partyXpBudget(levels);
    rating.difficulty = rateEncounter(rating.monsterXp, rating.budget);
    return rating;
}

}  // namespace combat
