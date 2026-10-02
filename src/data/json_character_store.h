#pragma once

#include "core/character_store.h"

#include <filesystem>
#include <string>
#include <vector>

namespace combat {

inline constexpr int kCharactersSchemaVersion = 1;

// Reads and writes characters.json. The caller chooses the path; this class
// never looks up platform folders.
//
// - A missing file loads as an empty roster.
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
