#include "core/monster_catalog.h"

namespace combat {

MergedMonsterCatalog::MergedMonsterCatalog(std::vector<Monster> srd, std::vector<Monster> custom)
    : m_srd(std::move(srd))
    , m_custom(std::move(custom))
{
}

std::vector<Monster> MergedMonsterCatalog::search(const MonsterQuery& query) const
{
    std::vector<Monster> merged;
    merged.reserve(m_srd.size() + m_custom.size());
    merged.insert(merged.end(), m_srd.begin(), m_srd.end());
    merged.insert(merged.end(), m_custom.begin(), m_custom.end());
    return searchMonsters(merged, query);
}

std::optional<Monster> MergedMonsterCatalog::findById(const std::string& id) const
{
    for (const Monster& monster : m_srd) {
        if (monster.id == id) {
            return monster;
        }
    }
    for (const Monster& monster : m_custom) {
        if (monster.id == id) {
            return monster;
        }
    }
    return std::nullopt;
}

void MergedMonsterCatalog::setCustomMonsters(std::vector<Monster> custom)
{
    m_custom = std::move(custom);
}

bool MergedMonsterCatalog::updateCustomMonster(const Monster& monster)
{
    for (Monster& existing : m_custom) {
        if (existing.id != monster.id) {
            continue;
        }
        existing = monster;
        existing.source = kCustomMonsterSource;
        return true;
    }
    return false;
}

std::unordered_set<std::string> MergedMonsterCatalog::srdIds() const
{
    std::unordered_set<std::string> ids;
    ids.reserve(m_srd.size());
    for (const Monster& monster : m_srd) {
        ids.insert(monster.id);
    }
    return ids;
}

}  // namespace combat
