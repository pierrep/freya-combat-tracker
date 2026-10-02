#pragma once

#include "core/custom_monster_store.h"
#include "core/monster.h"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace combat {

inline constexpr int kMonsterSchemaVersion = 1;

// Reads the packaged SRD catalog. The file is never written.
// A missing file, a parse error, or an unknown schemaVersion throws
// MonsterDataError.
std::vector<Monster> loadSrdMonsters(const std::filesystem::path& path);
std::vector<Monster> parseSrdMonsterDocument(const std::string& text);

// Reads and writes custom-monsters.json. The caller chooses the path.
//
// - A missing file loads as an empty list.
// - A file that cannot be parsed, or has an unknown schemaVersion, throws
//   MonsterDataError and is left untouched.
// - A row whose id matches an SRD slug, or whose source is srd-5.2.1, is
//   skipped and reported. It does not replace the SRD monster.
// - saveAll writes a temporary file in the same folder and then renames it
//   over the real file. A custom monster whose id collides with an SRD slug,
//   or whose source is not "custom", is rejected and the previous file stays.
class JsonCustomMonsterStore final : public CustomMonsterStore {
public:
    JsonCustomMonsterStore(std::filesystem::path path, std::unordered_set<std::string> srdIds);

    CustomMonsterLoadResult loadAll() override;
    void saveAll(const std::vector<Monster>& monsters) override;

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
    std::unordered_set<std::string> m_srdIds;
};

CustomMonsterLoadResult parseCustomMonsterDocument(const std::string& text,
                                                   const std::unordered_set<std::string>& srdIds);
std::string serializeCustomMonsterDocument(const std::vector<Monster>& monsters);

}  // namespace combat
