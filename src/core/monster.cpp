#include "core/monster.h"

#include <algorithm>
#include <cctype>

namespace combat {

namespace {

std::string asciiLower(std::string text)
{
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool isBlank(const std::string& text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

bool containsInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) {
        return true;
    }
    return asciiLower(haystack).find(asciiLower(needle)) != std::string::npos;
}

int compareNames(const std::string& left, const std::string& right)
{
    const std::string a = asciiLower(left);
    const std::string b = asciiLower(right);
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

int sourceRank(const Monster& monster)
{
    return monster.source == kSrdMonsterSource ? 0 : 1;
}

}  // namespace

std::vector<Monster> searchMonsters(const std::vector<Monster>& monsters, const MonsterQuery& query)
{
    std::vector<Monster> matches;
    matches.reserve(monsters.size());
    for (const Monster& monster : monsters) {
        if (!containsInsensitive(monster.name, query.nameSubstring)) {
            continue;
        }
        if (query.creatureType.has_value() && monster.creatureType != *query.creatureType) {
            continue;
        }
        if (query.challengeRating.has_value() && monster.challengeRating != *query.challengeRating) {
            continue;
        }
        matches.push_back(monster);
    }

    std::sort(matches.begin(), matches.end(), [](const Monster& left, const Monster& right) {
        const int nameOrder = compareNames(left.name, right.name);
        if (nameOrder != 0) {
            return nameOrder < 0;
        }
        const int leftRank = sourceRank(left);
        const int rightRank = sourceRank(right);
        if (leftRank != rightRank) {
            return leftRank < rightRank;
        }
        return left.id < right.id;
    });
    return matches;
}

std::vector<std::string> validateCustomMonster(const Monster& monster,
                                               const std::unordered_set<std::string>& srdIds)
{
    std::vector<std::string> problems;
    if (monster.id.empty()) {
        problems.emplace_back("Id is required.");
    }
    if (isBlank(monster.name)) {
        problems.emplace_back("Name is required.");
    }
    if (monster.source != kCustomMonsterSource) {
        problems.emplace_back("Custom monsters must use the custom source.");
    }
    if (!monster.id.empty() && srdIds.find(monster.id) != srdIds.end()) {
        problems.emplace_back("Id matches an SRD monster.");
    }
    return problems;
}

}  // namespace combat
