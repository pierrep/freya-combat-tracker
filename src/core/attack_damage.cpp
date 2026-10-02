#include "core/attack_damage.h"

#include <cctype>
#include <limits>

namespace combat {

namespace {

bool isDigit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

// Reads a non-negative int. On overflow the remaining digits are consumed and
// the parse fails, so the scan cannot loop on the same character.
bool parseInt(const std::string& text, std::size_t& index, int& value)
{
    if (index >= text.size() || !isDigit(static_cast<unsigned char>(text[index]))) {
        return false;
    }
    long long parsed = 0;
    while (index < text.size() && isDigit(static_cast<unsigned char>(text[index]))) {
        parsed = parsed * 10 + (text[index] - '0');
        ++index;
        if (parsed > std::numeric_limits<int>::max()) {
            while (index < text.size() && isDigit(static_cast<unsigned char>(text[index]))) {
                ++index;
            }
            return false;
        }
    }
    value = static_cast<int>(parsed);
    return true;
}

std::size_t clauseStart(const std::string& text, std::size_t matchStart)
{
    std::size_t index = matchStart;
    while (index > 0) {
        const char boundary = text[index - 1];
        if (boundary == '.' || boundary == ',' || boundary == '\n') {
            return index;
        }
        --index;
    }
    return 0;
}

std::size_t clauseEnd(const std::string& text, std::size_t matchEnd)
{
    std::size_t index = matchEnd;
    while (index < text.size()) {
        const char boundary = text[index];
        if (boundary == '.' || boundary == ',' || boundary == '\n') {
            return index;
        }
        ++index;
    }
    return text.size();
}

std::string lowerClause(const std::string& text, std::size_t begin, std::size_t end)
{
    std::string clause;
    clause.reserve(end - begin);
    for (std::size_t index = begin; index < end; ++index) {
        clause.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(text[index]))));
    }
    return clause;
}

bool clauseDealsDamage(const std::string& clause)
{
    if (clause.find("subtract") != std::string::npos || clause.find("maximum") != std::string::npos ||
        clause.find("hour") != std::string::npos) {
        return false;
    }
    if (clause.find("damage") != std::string::npos) {
        return true;
    }
    return clause.find("hit point") != std::string::npos && clause.find("lose") != std::string::npos;
}

}  // namespace

std::vector<DamageExpression> damageExpressions(const std::string& effect)
{
    std::vector<DamageExpression> found;
    std::size_t index = 0;
    while (index < effect.size()) {
        if (!isDigit(static_cast<unsigned char>(effect[index]))) {
            ++index;
            continue;
        }
        const std::size_t start = index;
        int count = 0;
        if (!parseInt(effect, index, count) || count < 1) {
            if (index == start) {
                ++index;
            }
            continue;
        }
        if (index >= effect.size() || effect[index] != 'd') {
            continue;
        }
        ++index;
        const std::size_t sidesPos = index;
        int sides = 0;
        if (!parseInt(effect, index, sides) || sides < 1) {
            if (index == sidesPos) {
                ++index;
            }
            continue;
        }
        int modifier = 0;
        const std::size_t afterSides = index;
        std::size_t modifierPos = index;
        while (modifierPos < effect.size() && (effect[modifierPos] == ' ' || effect[modifierPos] == '\t')) {
            ++modifierPos;
        }
        if (modifierPos < effect.size() && (effect[modifierPos] == '+' || effect[modifierPos] == '-')) {
            const char sign = effect[modifierPos];
            ++modifierPos;
            while (modifierPos < effect.size() && (effect[modifierPos] == ' ' || effect[modifierPos] == '\t')) {
                ++modifierPos;
            }
            int magnitude = 0;
            if (parseInt(effect, modifierPos, magnitude)) {
                modifier = sign == '-' ? -magnitude : magnitude;
                index = modifierPos;
            } else {
                index = afterSides;
            }
        }
        if (clauseDealsDamage(lowerClause(effect, clauseStart(effect, start), clauseEnd(effect, index)))) {
            found.push_back(DamageExpression{count, sides, modifier});
        }
    }
    return found;
}

std::optional<int> rollAttackDamage(const std::string& effect, const RollDie& rollDie)
{
    const std::vector<DamageExpression> expressions = damageExpressions(effect);
    if (expressions.empty() || !rollDie) {
        return std::nullopt;
    }
    long long total = 0;
    for (const DamageExpression& expression : expressions) {
        for (int die = 0; die < expression.count; ++die) {
            total += rollDie(expression.sides);
        }
        total += expression.modifier;
    }
    if (total < 0) {
        total = 0;
    }
    if (total > std::numeric_limits<int>::max()) {
        total = std::numeric_limits<int>::max();
    }
    return static_cast<int>(total);
}

}  // namespace combat
