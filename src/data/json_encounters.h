#pragma once

#include "core/encounter_store.h"

#include <filesystem>
#include <string>
#include <vector>

namespace combat {

inline constexpr int kEncounterSchemaVersion = 4;

// Reads and writes encounters.json. The caller chooses the path; this class
// never looks up platform folders.
//
// - A missing file loads as an empty list.
// - schemaVersion 1 still loads. Those combatants get tempHp 0, no maximum
//   HP, no conditions, no concentration, and death saves at 0.
// - schemaVersion 2 still loads. Its conditions are plain ids with no
//   duration. Monster rows have no stat block; the page fills one from the
//   catalog. Negative hit points from older builds load as 0.
// - schemaVersion 3 still loads. Its conditions do not say what caused them;
//   an Invisible from then is treated as the Hide action's.
// - Loading never rewrites the file. The next save writes version 4, where a
//   condition can carry its cause, its end triggers, and its concentration.
// - A file that cannot be parsed, or has an unknown schemaVersion, throws
//   EncounterStoreError and is left untouched.
// - saveAll writes a temporary file in the same folder and then renames it over
//   the real file, so an interrupted save keeps the previous file.
// - Combatant order is stored as-is, so a tie that was moved up or down stays
//   that way across a save.
class JsonEncounterStore final : public EncounterStore {
public:
    explicit JsonEncounterStore(std::filesystem::path path);

    std::vector<Encounter> loadAll() override;
    void saveAll(const std::vector<Encounter>& encounters) override;

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

std::vector<Encounter> parseEncountersDocument(const std::string& text);
std::string serializeEncountersDocument(const std::vector<Encounter>& encounters);

}  // namespace combat
