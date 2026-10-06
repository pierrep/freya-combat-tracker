#include "core/dice.h"

#include "core/text.h"

#include <cctype>
#include <limits>

namespace combat {

namespace {

bool readNumber(const std::string& text, std::size_t& index, int& value)
{
    if (index >= text.size() || std::isdigit(static_cast<unsigned char>(text[index])) == 0) {
        return false;
    }
    long long parsed = 0;
    while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])) != 0) {
        parsed = parsed * 10 + (text[index] - '0');
        if (parsed > 100000) {
            return false;
        }
        ++index;
    }
    value = static_cast<int>(parsed);
    return true;
}

void skipSpaces(const std::string& text, std::size_t& index)
{
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
}

}  // namespace

std::optional<Dice> parseDice(const std::string& raw)
{
    const std::string text = asciiLower(trim(raw));
    std::size_t index = 0;
    int first = 0;
    if (!readNumber(text, index, first)) {
        return std::nullopt;
    }
    Dice dice;
    if (index < text.size() && text[index] == 'd') {
        ++index;
        int sides = 0;
        if (!readNumber(text, index, sides) || first < 1 || sides < 1) {
            return std::nullopt;
        }
        dice.count = first;
        dice.sides = sides;
    } else {
        dice.modifier = first;
        skipSpaces(text, index);
        return index == text.size() ? std::optional<Dice>(dice) : std::nullopt;
    }
    skipSpaces(text, index);
    if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
        const bool negative = text[index] == '-';
        ++index;
        skipSpaces(text, index);
        int modifier = 0;
        if (!readNumber(text, index, modifier)) {
            return std::nullopt;
        }
        dice.modifier = negative ? -modifier : modifier;
    }
    skipSpaces(text, index);
    if (index != text.size()) {
        return std::nullopt;
    }
    return dice;
}

std::string formatDice(const Dice& dice)
{
    if (dice.count == 0) {
        return std::to_string(dice.modifier);
    }
    std::string text = std::to_string(dice.count) + "d" + std::to_string(dice.sides);
    if (dice.modifier > 0) {
        text += "+" + std::to_string(dice.modifier);
    } else if (dice.modifier < 0) {
        text += "-" + std::to_string(-dice.modifier);
    }
    return text;
}

int rollDice(const Dice& dice, const RollDie& rollDie, bool critical)
{
    long long total = dice.modifier;
    const int count = critical ? dice.count * 2 : dice.count;
    for (int i = 0; i < count && rollDie; ++i) {
        total += rollDie(dice.sides);
    }
    if (total < 0) {
        return 0;
    }
    if (total > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(total);
}

int averageDice(const Dice& dice)
{
    const long long twice = static_cast<long long>(dice.count) * (dice.sides + 1) + 2LL * dice.modifier;
    const long long average = twice / 2;
    return average < 0 ? 0 : static_cast<int>(average);
}

}  // namespace combat
