#pragma once

#include "core/monster.h"

#include <string>
#include <vector>

namespace combat {

struct CustomMonsterLoadResult {
    std::vector<Monster> monsters;
    // Rows that were left out, with a reason. The SRD catalog is never changed
    // to absorb them. A later save writes only the monsters that were loaded.
    std::vector<std::string> skipped;
};

// Loads, saves, and deletes custom monsters. SRD rows never go through it.
class CustomMonsterStore {
public:
    virtual ~CustomMonsterStore() = default;

    virtual CustomMonsterLoadResult loadAll() = 0;
    virtual void saveAll(const std::vector<Monster>& monsters) = 0;
};

}  // namespace combat
