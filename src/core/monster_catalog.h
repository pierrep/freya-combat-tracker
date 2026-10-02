#pragma once

#include "core/monster.h"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace combat {

// A merged view of the read-only SRD catalog and the custom-monster file.
// Callers search and look up ids here; they do not open the files themselves.
class MonsterCatalog {
public:
    virtual ~MonsterCatalog() = default;

    virtual std::vector<Monster> search(const MonsterQuery& query) const = 0;
    virtual std::optional<Monster> findById(const std::string& id) const = 0;
};

class MergedMonsterCatalog final : public MonsterCatalog {
public:
    MergedMonsterCatalog(std::vector<Monster> srd, std::vector<Monster> custom = {});

    std::vector<Monster> search(const MonsterQuery& query) const override;
    std::optional<Monster> findById(const std::string& id) const override;

    void setCustomMonsters(std::vector<Monster> custom);

    const std::vector<Monster>& srdMonsters() const { return m_srd; }
    const std::vector<Monster>& customMonsters() const { return m_custom; }
    std::unordered_set<std::string> srdIds() const;

private:
    std::vector<Monster> m_srd;
    std::vector<Monster> m_custom;
};

}  // namespace combat
