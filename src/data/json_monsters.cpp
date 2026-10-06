#include "data/json_monsters.h"

#include "core/text.h"
#include "data/json_monster_codec.h"
#include "data/json_util.h"

#include <utility>

namespace combat {

namespace {

using json_util::json;
using Error = MonsterDataError;

void readSchemaVersion(const json& document, const std::string& what)
{
    const std::string context = what + " file";
    const int version = json_util::readInt<Error>(document, "schemaVersion", context);
    if (version != 1 && version != kMonsterSchemaVersion) {
        throw Error(context + " has schemaVersion " + std::to_string(version) +
                    ", which this version of the app cannot read.");
    }
}

}  // namespace

std::vector<Monster> parseSrdMonsterDocument(const std::string& text)
{
    const json document = json_util::parseDocument<Error>(text, "monster catalog");
    readSchemaVersion(document, "Monster catalog");
    const std::string context = "Monster catalog file";
    const std::string source = json_util::readString<Error>(document, "source", context);
    if (source != kSrdMonsterSource) {
        throw Error("Monster catalog file: source must be \"" + std::string(kSrdMonsterSource) + "\".");
    }

    const json& list = json_util::requireArray<Error>(document, "monsters", context);
    std::vector<Monster> monsters;
    monsters.reserve(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        Monster monster = json_codec::monsterFromJson(list[i], "Monster " + std::to_string(i + 1));
        if (monster.source != kSrdMonsterSource) {
            throw Error("Monster " + std::to_string(i + 1) + ": source must be \"" + std::string(kSrdMonsterSource) +
                        "\".");
        }
        if (monster.id.empty() || monster.name.empty()) {
            throw Error("Monster " + std::to_string(i + 1) + ": id and name are required.");
        }
        monsters.push_back(std::move(monster));
    }
    return monsters;
}

std::vector<Monster> loadSrdMonsters(const std::filesystem::path& path)
{
    return parseSrdMonsterDocument(*json_util::readTextFile<Error>(path, false));
}

CustomMonsterLoadResult parseCustomMonsterDocument(const std::string& text,
                                                   const std::unordered_set<std::string>& srdIds)
{
    const json document = json_util::parseDocument<Error>(text, "custom monster");
    readSchemaVersion(document, "Custom monster");
    const std::string context = "Custom monster file";
    const json& list = json_util::requireArray<Error>(document, "monsters", context);

    CustomMonsterLoadResult result;
    std::unordered_set<std::string> seenIds;
    for (std::size_t i = 0; i < list.size(); ++i) {
        Monster monster = json_codec::monsterFromJson(list[i], "Monster " + std::to_string(i + 1));
        const std::string label = monster.id.empty() ? ("Monster " + std::to_string(i + 1)) : monster.id;
        auto skip = [&](const std::string& reason) {
            result.skipped.push_back("Skipped custom monster \"" + label + "\": " + reason);
            result.preserved.push_back(list[i].dump());
        };
        if (!monster.id.empty() && srdIds.find(monster.id) != srdIds.end()) {
            skip("its id matches an SRD monster, so it was not loaded. It is kept in the file.");
            continue;
        }
        if (monster.source == kSrdMonsterSource) {
            skip("its source is SRD, so it was not loaded. It is kept in the file.");
            continue;
        }
        if (monster.source != kCustomMonsterSource) {
            throw Error("Monster " + std::to_string(i + 1) + ": source must be \"" +
                        std::string(kCustomMonsterSource) + "\".");
        }
        if (monster.id.empty() || isBlank(monster.name)) {
            throw Error("Monster " + std::to_string(i + 1) + ": id and name are required.");
        }
        if (!seenIds.insert(monster.id).second) {
            skip("duplicate id. It is kept in the file.");
            continue;
        }
        result.monsters.push_back(std::move(monster));
    }
    return result;
}

std::string serializeCustomMonsterDocument(const std::vector<Monster>& monsters,
                                           const std::vector<std::string>& preserved)
{
    json list = json::array();
    for (const Monster& monster : monsters) {
        list.push_back(json_codec::monsterToJson(monster));
    }
    for (const std::string& row : preserved) {
        list.push_back(json::parse(row));
    }
    const json document{
        {"schemaVersion", kMonsterSchemaVersion},
        {"monsters", std::move(list)},
    };
    return document.dump(2) + "\n";
}

JsonCustomMonsterStore::JsonCustomMonsterStore(std::filesystem::path path, std::unordered_set<std::string> srdIds)
    : m_path(std::move(path))
    , m_srdIds(std::move(srdIds))
{
}

CustomMonsterLoadResult JsonCustomMonsterStore::loadAll()
{
    const std::optional<std::string> text = json_util::readTextFile<Error>(m_path, true);
    if (!text.has_value()) {
        m_preserved.clear();
        return {};
    }
    CustomMonsterLoadResult result = parseCustomMonsterDocument(*text, m_srdIds);
    m_preserved = result.preserved;
    return result;
}

void JsonCustomMonsterStore::saveAll(const std::vector<Monster>& monsters)
{
    std::unordered_set<std::string> seenIds;
    for (std::size_t i = 0; i < monsters.size(); ++i) {
        const auto problems = validateCustomMonster(monsters[i], m_srdIds);
        if (!problems.empty()) {
            throw Error("Monster " + std::to_string(i + 1) + ": " + problems.front());
        }
        if (!seenIds.insert(monsters[i].id).second) {
            throw Error("Monster " + std::to_string(i + 1) + ": duplicate id \"" + monsters[i].id + "\".");
        }
    }
    json_util::writeFileAtomically<Error>(m_path, serializeCustomMonsterDocument(monsters, m_preserved));
}

}  // namespace combat
