#include "core/monster.h"

#include "core/attack_damage.h"
#include "core/text.h"

#include <algorithm>
#include <utility>

namespace combat {

namespace {

int sourceRank(const Monster& monster)
{
    return monster.source == kSrdMonsterSource ? 0 : 1;
}

constexpr std::pair<const char*, DamageWhen> kWhenKeys[] = {
    {"always", DamageWhen::Always},           {"advantage", DamageWhen::Advantage},
    {"advantageAlt", DamageWhen::AdvantageAlt}, {"alternative", DamageWhen::Alternative},
    {"conditional", DamageWhen::Conditional}, {"ongoing", DamageWhen::Ongoing},
};

}  // namespace

const char* damageWhenKey(DamageWhen when)
{
    for (const auto& [key, value] : kWhenKeys) {
        if (value == when) {
            return key;
        }
    }
    return "always";
}

std::optional<DamageWhen> damageWhenFromKey(const std::string& key)
{
    for (const auto& [name, value] : kWhenKeys) {
        if (key == name) {
            return value;
        }
    }
    return std::nullopt;
}

bool isMultiattack(const MonsterAttack& attack)
{
    // Also a form-limited one: "Multiattack (Vampire Form Only)".
    return attack.name == "Multiattack" || attack.name.rfind("Multiattack (", 0) == 0;
}

int monsterSaveBonus(const Monster& monster, Ability ability)
{
    const std::optional<int>& listed = monster.savingThrows[static_cast<std::size_t>(ability)];
    if (listed.has_value()) {
        return *listed;
    }
    return abilityModifier(abilityScore(monster.abilities, ability));
}

int xpForChallengeRating(const std::string& rating)
{
    static const std::pair<const char*, int> table[] = {
        {"0", 10},      {"1/8", 25},    {"1/4", 50},    {"1/2", 100},   {"1", 200},     {"2", 450},
        {"3", 700},     {"4", 1100},    {"5", 1800},    {"6", 2300},    {"7", 2900},    {"8", 3900},
        {"9", 5000},    {"10", 5900},   {"11", 7200},   {"12", 8400},   {"13", 10000},  {"14", 11500},
        {"15", 13000},  {"16", 15000},  {"17", 18000},  {"18", 20000},  {"19", 22000},  {"20", 25000},
        {"21", 33000},  {"22", 41000},  {"23", 50000},  {"24", 62000},  {"25", 75000},  {"26", 90000},
        {"27", 105000}, {"28", 120000}, {"29", 135000}, {"30", 155000},
    };
    const std::string key = trim(rating);
    for (const auto& [name, xp] : table) {
        if (key == name) {
            return xp;
        }
    }
    return 0;
}

int monsterXp(const Monster& monster)
{
    return monster.xp.value_or(xpForChallengeRating(monster.challengeRating));
}

std::vector<DamagePart> attackDamageParts(const MonsterAttack& attack)
{
    if (!attack.damage.empty() || isMultiattack(attack)) {
        return attack.damage;
    }
    std::vector<DamagePart> parts;
    for (const DamageExpression& expression : damageExpressions(attack.effect)) {
        parts.push_back(DamagePart{formatDice(Dice{expression.count, expression.sides, expression.modifier}), {},
                                   DamageWhen::Always});
    }
    return parts;
}

std::string describeDamageCondition(const std::string& condition)
{
    if (condition == kDamageIfTargetBloodied) {
        return "the target is Bloodied";
    }
    if (condition == kDamageIfSelfBloodied) {
        return "it is Bloodied";
    }
    if (condition == kDamageIfGrappledBySelf) {
        return "the target is Grappled by it";
    }
    return condition;
}

namespace {

std::string damageConditionFromWords(const std::string& words)
{
    const std::string text = trim(words);
    for (const char* token : {kDamageIfTargetBloodied, kDamageIfSelfBloodied, kDamageIfGrappledBySelf}) {
        if (equalsInsensitive(text, describeDamageCondition(token)) || text == token) {
            return token;
        }
    }
    return text;
}

}  // namespace

std::string formatDamageParts(const std::vector<DamagePart>& parts)
{
    std::string text;
    for (const DamagePart& part : parts) {
        if (!text.empty()) {
            text += ", ";
        }
        text += part.dice;
        if (!part.type.empty()) {
            text += " " + part.type;
        }
        if (part.when != DamageWhen::Always) {
            text += std::string(" ") + damageWhenKey(part.when);
        }
        if (!part.condition.empty()) {
            text += " if " + describeDamageCondition(part.condition);
        }
    }
    return text;
}

std::optional<std::vector<DamagePart>> parseDamageParts(const std::string& text, std::string* error)
{
    std::vector<DamagePart> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto comma = text.find(',', start);
        const std::string piece = trim(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        start = comma == std::string::npos ? text.size() + 1 : comma + 1;
        if (piece.empty()) {
            continue;
        }
        // Dice may be written "1d6 + 2": join everything up to the first word
        // that is not part of the dice.
        std::vector<std::string> words;
        std::size_t at = 0;
        while (at < piece.size()) {
            const auto space = piece.find(' ', at);
            const std::string word = piece.substr(at, space == std::string::npos ? std::string::npos : space - at);
            if (!word.empty()) {
                words.push_back(word);
            }
            at = space == std::string::npos ? piece.size() : space + 1;
        }
        std::string dice;
        std::size_t index = 0;
        while (index < words.size()) {
            const std::string candidate = dice + words[index];
            const bool looksLikeDice = parseDice(candidate).has_value() || words[index] == "+" || words[index] == "-" ||
                                       (!dice.empty() && (dice.back() == '+' || dice.back() == '-'));
            if (!looksLikeDice) {
                break;
            }
            dice = candidate;
            ++index;
        }
        if (!parseDice(dice).has_value()) {
            if (error != nullptr) {
                *error = "\"" + piece + "\" does not start with dice such as 2d6+3.";
            }
            return std::nullopt;
        }
        DamagePart part;
        part.dice = formatDice(*parseDice(dice));
        for (; index < words.size(); ++index) {
            const std::string word = asciiLower(words[index]);
            if (word == "if") {
                // "1d6 piercing conditional if the boar moved 20+ feet ..."
                std::string rest;
                for (std::size_t more = index + 1; more < words.size(); ++more) {
                    rest += (rest.empty() ? "" : " ") + words[more];
                }
                part.condition = damageConditionFromWords(rest);
                break;
            }
            if (isDamageType(word) && part.type.empty()) {
                part.type = word;
            } else if (const auto when = damageWhenFromKey(words[index]); when.has_value()) {
                part.when = *when;
            } else if (const auto lowered = damageWhenFromKey(word); lowered.has_value()) {
                part.when = *lowered;
            } else {
                if (error != nullptr) {
                    *error = "\"" + words[index] + "\" is not a damage type or a when (advantage, alternative, "
                                                     "conditional, ongoing).";
                }
                return std::nullopt;
            }
        }
        parts.push_back(std::move(part));
    }
    return parts;
}

std::string abilityDisplayName(const std::string& name)
{
    const std::string marker = " (Recharge ";
    const auto start = name.rfind(marker);
    if (start == std::string::npos || name.empty() || name.back() != ')') {
        return name;
    }
    if (name.find(')', start) != name.size() - 1) {
        return name;
    }
    return name.substr(0, start);
}

std::string attackSummary(const MonsterAttack& attack)
{
    std::vector<std::string> pieces;
    if (attack.attackBonus.has_value()) {
        pieces.push_back(formatModifier(*attack.attackBonus) + " to hit");
    }
    if (attack.save.has_value()) {
        pieces.push_back("DC " + std::to_string(attack.save->dc) + " " + abilityShort(attack.save->ability) + " save" +
                         (attack.save->halfOnSuccess ? " (half on success)" : ""));
    }
    std::string damage;
    for (const DamagePart& part : attack.damage) {
        std::string one = part.dice + (part.type.empty() ? "" : " " + part.type);
        switch (part.when) {
        case DamageWhen::Always:
            break;
        case DamageWhen::Advantage:
            one += " with Advantage";
            break;
        case DamageWhen::AdvantageAlt:
            one = "or " + one + " with Advantage";
            break;
        case DamageWhen::Alternative:
            one = "or " + one + (part.condition.empty() ? "" : " if " + describeDamageCondition(part.condition));
            break;
        case DamageWhen::Conditional:
            one += part.condition.empty() ? " if it applies" : " if " + describeDamageCondition(part.condition);
            break;
        case DamageWhen::Ongoing:
            one += " on later turns";
            break;
        }
        if (!damage.empty()) {
            damage += part.when == DamageWhen::Alternative || part.when == DamageWhen::AdvantageAlt ? " " : " + ";
        }
        damage += one;
    }
    if (!damage.empty()) {
        pieces.push_back(damage);
    }
    if (attack.area) {
        pieces.emplace_back("area");
    }
    if (attack.recharge.has_value()) {
        pieces.push_back(*attack.recharge >= 6 ? std::string("Recharge 6")
                                               : "Recharge " + std::to_string(*attack.recharge) + "-6");
    }
    if (attack.perDay.has_value()) {
        pieces.push_back(std::to_string(*attack.perDay) + "/Day");
    }
    std::string text;
    for (const std::string& piece : pieces) {
        if (!text.empty()) {
            text += ", ";
        }
        text += piece;
    }
    return text;
}

std::vector<Monster> searchMonsters(const std::vector<Monster>& monsters, const MonsterQuery& query)
{
    std::vector<Monster> matches;
    matches.reserve(monsters.size());
    for (const Monster& monster : monsters) {
        if (!containsInsensitive(monster.name, query.nameSubstring)) {
            continue;
        }
        if (query.creatureType.has_value() && monster.creatureType != *query.creatureType) {
            continue;
        }
        if (query.challengeRating.has_value() && monster.challengeRating != *query.challengeRating) {
            continue;
        }
        matches.push_back(monster);
    }

    std::sort(matches.begin(), matches.end(), [](const Monster& left, const Monster& right) {
        const int nameOrder = compareNames(left.name, right.name);
        if (nameOrder != 0) {
            return nameOrder < 0;
        }
        const int leftRank = sourceRank(left);
        const int rightRank = sourceRank(right);
        if (leftRank != rightRank) {
            return leftRank < rightRank;
        }
        return left.id < right.id;
    });
    return matches;
}

std::vector<std::string> validateCustomMonster(const Monster& monster,
                                               const std::unordered_set<std::string>& srdIds)
{
    std::vector<std::string> problems;
    if (monster.id.empty()) {
        problems.emplace_back("Id is required.");
    }
    if (isBlank(monster.name)) {
        problems.emplace_back("Name is required.");
    }
    if (monster.source != kCustomMonsterSource) {
        problems.emplace_back("Custom monsters must use the custom source.");
    }
    if (!monster.id.empty() && srdIds.find(monster.id) != srdIds.end()) {
        problems.emplace_back("Id matches an SRD monster.");
    }
    for (const MonsterAttack& attack : monster.attacks) {
        if (isBlank(attack.name)) {
            problems.emplace_back("Attack name is required.");
            break;
        }
        if (attack.count < 1) {
            problems.emplace_back("Attack count must be at least 1.");
            break;
        }
        bool badDice = false;
        for (const DamagePart& part : attack.damage) {
            if (!parseDice(part.dice).has_value() || (!part.type.empty() && !isDamageType(part.type))) {
                badDice = true;
            }
        }
        if (badDice) {
            problems.emplace_back("Damage must be dice such as 2d6+3 and a damage type.");
            break;
        }
    }
    return problems;
}

}  // namespace combat
