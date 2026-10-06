#pragma once

#include "core/character.h"

#include <stdexcept>
#include <vector>

namespace combat {

class CharacterStoreError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Loads and saves the whole roster. Implementations throw CharacterStoreError
// when the stored roster cannot be read or written.
class CharacterStore {
public:
    virtual ~CharacterStore() = default;

    virtual std::vector<Character> loadAll() = 0;
    virtual void saveAll(const std::vector<Character>& characters) = 0;
};

}  // namespace combat
