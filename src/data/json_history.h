#pragma once

// The Dashboard's undo history, kept between runs in history.json beside
// encounters.json. It holds the log and open checks of the encounter that was
// on screen and up to the undo limit of earlier steps, each a whole copy of
// its encounter and the character roster. With it come the encounters and
// roster as they were at the save, so a later run can tell whether the files
// still match: steps for an encounter that changed elsewhere, or all of them
// if the roster changed, are dropped rather than undone onto other data.

#include "core/character.h"
#include "core/encounter.h"
#include "core/monster.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace combat {

class HistoryError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// An open check at the top of the Dashboard (a concentration save, a death
// save, an aura, a rider question), in plain fields.
struct HistoryPrompt {
    int kind = 0;
    std::string combatantId;
    std::string conditionId;
    std::string sourceId;
    std::string auraName;
    int ability = 0;
    int dc = 10;
    std::optional<MonsterAttack> attack;
    std::vector<std::size_t> riders;
    int refund = 0;
    // A monster action's save: the damage a failure deals, as amount and
    // type pairs, and its flags.
    std::vector<std::pair<int, std::string>> damage;
    bool advantage = false;
    bool afterHit = false;
    bool wasBloodied = false;
    bool critical = false;  // a Shield question: the hit was critical

    bool operator==(const HistoryPrompt&) const = default;
};

// One undo step: everything as it was before the change.
struct HistoryStep {
    std::string encounterId;
    Encounter encounter;
    std::vector<Character> roster;
    std::vector<std::string> log;
    std::vector<HistoryPrompt> prompts;
    std::string selectionId;

    bool operator==(const HistoryStep&) const = default;
};

struct FightHistory {
    // The data the steps were taken against, as saved.
    std::vector<Character> roster;
    std::vector<Encounter> encounters;
    // The encounter on screen, its log (newest first), and its open checks.
    std::string shownEncounterId;
    std::vector<std::string> log;
    std::vector<HistoryPrompt> prompts;
    // Oldest first; the last is undone first.
    std::vector<HistoryStep> steps;

    bool operator==(const FightHistory&) const = default;
};

std::string serializeHistory(const FightHistory& history);
// Throws HistoryError on a malformed document.
FightHistory parseHistory(const std::string& text);

// Missing, unreadable, or malformed: empty. History is a convenience, so a
// bad file never stops the app.
std::optional<FightHistory> loadHistoryFile(const std::filesystem::path& path);
// Throws HistoryError when the file cannot be written.
void saveHistoryFile(const std::filesystem::path& path, const FightHistory& history);

}  // namespace combat
