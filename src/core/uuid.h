#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace combat {

// Returns a random 64-bit value. The caller owns the RNG, so core never reads
// the clock or a system entropy source itself.
using Random64 = std::function<std::uint64_t()>;

// Builds a lowercase RFC 4122 version 4 UUID string from two random draws.
std::string generateUuidV4(const Random64& random);

}  // namespace combat
