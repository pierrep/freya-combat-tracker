#pragma once

#include "core/character_store.h"

#include <filesystem>
#include <string>
#include <vector>

namespace combat {

inline constexpr int kCharactersSchemaVersion = 4;

// Reads and writes characters.json. The caller chooses the path; this class
// never looks up platform folders.
//
// - A missing file loads as an empty roster.
// - schemaVersion 1 is migrated in memory: the old hp number becomes
//   hp.current and hp.max, and the new lists start empty.
// - schemaVersions 2 and 3 load too. Their typed proficiency and initiative
//   bonuses become overrides only when they differ from what the sheet works
//   out (and are not 0). Their sheet conditions and death saves are dropped:
//   those belong to a fight.
// - Version 4 adds defenses, exhaustion, Hit Point Dice spent, Pact Magic
//   slots, and optional overrides.
// - Negative hit points from older builds load as 0. Loading never rewrites
//   the file.
// - The first save over an older file copies its original bytes to
//   <stem>.v<N><extension> (characters.json becomes characters.v3.json) when
//   that copy is not already there, then writes version 4.
// - A file that cannot be parsed, or has an unknown schemaVersion, throws
//   CharacterStoreError and is left untouched.
// - saveAll writes a temporary file in the same folder and then renames it over
//   the real file, so an interrupted save keeps the previous file.
class JsonCharacterStore final : public CharacterStore {
public:
    explicit JsonCharacterStore(std::filesystem::path path);

    std::vector<Character> loadAll() override;
    void saveAll(const std::vector<Character>& characters) override;

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

std::vector<Character> parseCharactersDocument(const std::string& text);
std::string serializeCharactersDocument(const std::vector<Character>& characters);

}  // namespace combat
