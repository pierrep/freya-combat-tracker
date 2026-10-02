#include "core/character.h"
#include "core/uuid.h"
#include "test_harness.h"

#include <climits>
#include <cstdint>
#include <regex>
#include <vector>

using namespace combat;

TEST_CASE("abilityModifier follows floor((score - 10) / 2)")
{
    CHECK_EQ(abilityModifier(10), 0);
    CHECK_EQ(abilityModifier(11), 0);
    CHECK_EQ(abilityModifier(12), 1);
    CHECK_EQ(abilityModifier(16), 3);
    CHECK_EQ(abilityModifier(20), 5);
    CHECK_EQ(abilityModifier(30), 10);
    CHECK_EQ(abilityModifier(9), -1);
    CHECK_EQ(abilityModifier(8), -1);
    CHECK_EQ(abilityModifier(7), -2);
    CHECK_EQ(abilityModifier(1), -5);
    CHECK_EQ(abilityModifier(0), -5);
    CHECK_EQ(abilityModifier(-1), -6);
}

TEST_CASE("abilityModifier handles extreme ints without overflow")
{
    CHECK_EQ(abilityModifier(INT_MAX), (INT_MAX - 10) / 2);
    CHECK_EQ(abilityModifier(INT_MIN), static_cast<int>((static_cast<long long>(INT_MIN) - 10) / 2));
}

TEST_CASE("formatModifier shows a sign")
{
    CHECK_EQ(formatModifier(3), std::string("+3"));
    CHECK_EQ(formatModifier(0), std::string("+0"));
    CHECK_EQ(formatModifier(-1), std::string("-1"));
}

TEST_CASE("validateCharacter requires a non-blank name")
{
    Character c;
    c.id = "id-1";
    c.name = "Aria";
    CHECK(validateCharacter(c).empty());

    c.name = "";
    CHECK_EQ(validateCharacter(c).size(), std::size_t{1});
    c.name = "   \t";
    CHECK_EQ(validateCharacter(c).size(), std::size_t{1});
}

TEST_CASE("validateCharacter allows unusual scores")
{
    Character c;
    c.id = "id-1";
    c.name = "Odd";
    c.hp.current = -5;
    c.hp.max = -1;
    c.ac = 0;
    c.abilities.strength = 0;
    c.abilities.charisma = 40;
    c.passivePerception = -3;
    CHECK(validateCharacter(c).empty());
}

TEST_CASE("validateCharacter requires an id")
{
    Character c;
    c.name = "Aria";
    CHECK_EQ(validateCharacter(c).size(), std::size_t{1});
}

TEST_CASE("generateUuidV4 produces a v4 UUID from the supplied RNG")
{
    std::vector<std::uint64_t> sequence{0x0123456789abcdefULL, 0xfedcba9876543210ULL};
    std::size_t next = 0;
    const std::string id = generateUuidV4([&] { return sequence[next++]; });

    static const std::regex shape("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
    CHECK(std::regex_match(id, shape));
    CHECK_EQ(next, std::size_t{2});

    next = 0;
    CHECK_EQ(generateUuidV4([&] { return sequence[next++]; }), id);
}

TEST_MAIN()
