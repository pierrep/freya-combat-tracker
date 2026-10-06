#pragma once

#include "core/custom_monster_store.h"
#include "core/monster.h"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace combat {

// schemaVersion 2 adds structured attacks, defenses, saves, and XP. Version 1
// files still load.
inline constexpr int kMonsterSchemaVersion = 2;

// Reads the packaged SRD catalog. The file is never written. A missing file,
// a parse error, or an unknown schemaVersion throws MonsterDataError.
std::vector<Monster> loadSrdMonsters(const std::filesystem::path& path);
std::vector<Monster> parseSrdMonsterDocument(const std::string& text);

// Reads and writes custom-monsters.json. The caller chooses the path.
//
// - A missing file loads as an empty list.
// - A file that cannot be parsed, or has an unknown schemaVersion, throws
//   MonsterDataError and is left untouched.
// - A row whose id matches an SRD slug, whose source is srd-5.2.1, or whose id
//   repeats is skipped and reported. It is kept as it was and written back on
//   the next save, so it is never lost.
// - saveAll writes a temporary file and renames it over the real file.
class JsonCustomMonsterStore final : public CustomMonsterStore {
public:
    JsonCustomMonsterStore(std::filesystem::path path, std::unordered_set<std::string> srdIds);

    CustomMonsterLoadResult loadAll() override;
    void saveAll(const std::vector<Monster>& monsters) override;

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
    std::unordered_set<std::string> m_srdIds;
    std::vector<std::string> m_preserved;
};

CustomMonsterLoadResult parseCustomMonsterDocument(const std::string& text,
                                                   const std::unordered_set<std::string>& srdIds);
std::string serializeCustomMonsterDocument(const std::vector<Monster>& monsters,
                                           const std::vector<std::string>& preserved = {});

}  // namespace combat
