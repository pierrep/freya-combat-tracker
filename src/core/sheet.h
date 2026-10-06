#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace combat {

// Thrown when a packaged spell, condition, or species catalog cannot be read.
class CatalogError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Every packaged SRD file names this source.
inline constexpr const char* kSrdSource = "srd-5.2.1";

inline constexpr int kCatalogSchemaVersion = 1;
inline constexpr int kConditionCatalogSchemaVersion = 2;

struct Spell {
    std::string id;
    std::string name;
    int level = 0;
    std::string school;
    std::string castingTime;
    std::string range;
    std::string duration;
    bool concentration = false;
    // "V", "S", "M". Empty when the catalog did not say.
    std::vector<std::string> components;
    std::string description;

    bool operator==(const Spell&) const = default;
};

struct Condition {
    std::string id;
    std::string name;
    std::string description;
    // Short mechanical notes copied from the SRD entry. Empty when the effect
    // depends on something this app does not track, or when the wording is
    // not a single unconditional result.
    std::vector<std::string> tags;

    bool operator==(const Condition&) const = default;
};

// Empty nameSubstring lists every row, sorted by name. Matching is a
// case-insensitive substring of the name, the same rule as the monster catalog.
std::vector<Spell> searchSpells(const std::vector<Spell>& spells, const std::string& nameSubstring);
std::optional<Spell> findSpellById(const std::vector<Spell>& spells, const std::string& id);

std::vector<Condition> searchConditions(const std::vector<Condition>& conditions, const std::string& nameSubstring);
std::optional<Condition> findConditionById(const std::vector<Condition>& conditions, const std::string& id);

std::vector<std::string> searchSpecies(const std::vector<std::string>& species, const std::string& nameSubstring);

}  // namespace combat
