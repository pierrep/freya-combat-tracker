#pragma once

#include "core/character.h"

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

// One entry from a monster's Actions. count is how many times Multiattack
// grants that attack (1 when the SRD does not give it a number). The
// Multiattack row's count is how many attacks that action makes.
struct MonsterAttack {
    std::string name;
    std::string effect;
    int count = 1;

    bool operator==(const MonsterAttack&) const = default;
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
    // Empty when the monster has no actions, including a custom monster that
    // was saved before attacks were stored.
    std::vector<MonsterAttack> attacks;

    bool operator==(const Monster&) const = default;
};

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
// An id that matches an SRD slug, or a source of srd-5.2.1, is rejected.
std::vector<std::string> validateCustomMonster(const Monster& monster,
                                               const std::unordered_set<std::string>& srdIds);

}  // namespace combat
