#include "core/uuid.h"

#include <array>

namespace combat {

std::string generateUuidV4(const Random64& random)
{
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t half = 0; half < 2; ++half) {
        std::uint64_t value = random();
        for (std::size_t i = 0; i < 8; ++i) {
            bytes[half * 8 + i] = static_cast<std::uint8_t>(value & 0xffU);
            value >>= 8;
        }
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);

    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out.push_back('-');
        }
        out.push_back(hex[bytes[i] >> 4]);
        out.push_back(hex[bytes[i] & 0x0fU]);
    }
    return out;
}

}  // namespace combat
