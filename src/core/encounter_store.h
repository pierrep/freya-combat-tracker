#pragma once

#include "core/encounter.h"

#include <stdexcept>
#include <vector>

namespace combat {

class EncounterStoreError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Loads and saves every encounter. Implementations throw EncounterStoreError
// when the file cannot be read or written.
class EncounterStore {
public:
    virtual ~EncounterStore() = default;

    virtual std::vector<Encounter> loadAll() = 0;
    virtual void saveAll(const std::vector<Encounter>& encounters) = 0;
};

}  // namespace combat
