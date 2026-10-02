#pragma once

#include "core/character_store.h"

#include <filesystem>
#include <string>
#include <vector>

namespace combat {

inline constexpr int kCharactersSchemaVersion = 3;

// Reads and writes characters.json. The caller chooses the path; this class
// never looks up platform folders.
//
// - A missing file loads as an empty roster.
// - schemaVersion 1 is migrated in memory: the old hp number becomes
//   hp.current and hp.max, temporary HP starts at 0, and the new lists start
//   empty. Loading does not rewrite the file.
// - schemaVersion 2 loads as well. external stays empty unless that object
//   is already in the file. Loading does not rewrite the file.
// - The first save over a version 1 file copies those original bytes to
//   <stem>.v1<extension> (characters.json becomes characters.v1.json) when
//   that copy is not already there, then writes schemaVersion 3.
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
