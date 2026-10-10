#include "core/spell_rules.h"

#include "core/character.h"
#include "core/text.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace combat {

namespace {

#include "spell_rules_table.inc"

struct Mention {
    const SpellCombatRule* rule = nullptr;
    int slot = 0;
    bool selfOnly = false;
    std::optional<int> perDay;
};

bool isNameChar(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\'' || c == '/';
}

const std::vector<const SpellCombatRule*>& rulesByLength()
{
    static const std::vector<const SpellCombatRule*> sorted = [] {
        std::vector<const SpellCombatRule*> rules;
        rules.reserve(std::size(kSpellRules));
        for (const SpellCombatRule& rule : kSpellRules) {
            rules.push_back(&rule);
        }
        std::sort(rules.begin(), rules.end(), [](const SpellCombatRule* left, const SpellCombatRule* right) {
            return std::strlen(left->name) > std::strlen(right->name);
        });
        return rules;
    }();
    return sorted;
}

const SpellCombatRule* findRule(const std::string& name)
{
    for (const SpellCombatRule& rule : kSpellRules) {
        if (name == rule.name) {
            return &rule;
        }
    }
    return nullptr;
}

// Spells whose repeat save has its own trigger rather than the end of every
// turn (Fear: "ends its turn without line of sight to you"). The GM rolls it.
bool hasManualRepeatSave(const char* spellId)
{
    return std::strcmp(spellId, "fear") == 0;
}

// Spells whose conditions come through the target seeing or hearing the caster.
bool needsSightOrHearing(const char* spellId)
{
    for (const char* id : {"fear", "command", "hypnotic-pattern", "color-spray", "dissonant-whispers", "vicious-mockery"}) {
        if (std::strcmp(spellId, id) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> splitWords(const char* text)
{
    std::vector<std::string> words;
    std::string word;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word.push_back(*cursor);
        }
    }
    if (!word.empty()) {
        words.push_back(std::move(word));
    }
    return words;
}

bool listed(const char* words, const std::string& wanted)
{
    for (const std::string& word : splitWords(words)) {
        if (word == wanted) {
            return true;
        }
    }
    return false;
}

std::optional<int> leadingNumber(const std::string& text, std::size_t at)
{
    while (at < text.size() && text[at] == ' ') {
        ++at;
    }
    if (at >= text.size() || text[at] < '0' || text[at] > '9') {
        return std::nullopt;
    }
    int value = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        value = value * 10 + (text[at] - '0');
        ++at;
    }
    return value;
}

std::optional<int> spellSaveDc(const std::string& text)
{
    const std::string marker = "spell save DC ";
    const auto at = text.find(marker);
    if (at == std::string::npos) {
        return std::nullopt;
    }
    return leadingNumber(text, at + marker.size());
}

std::optional<int> spellAttackBonus(const std::string& text)
{
    const std::string marker = " to hit with spell attacks";
    const auto at = text.find(marker);
    if (at == std::string::npos || at == 0) {
        return std::nullopt;
    }
    std::size_t digits = at;
    while (digits > 0 && text[digits - 1] >= '0' && text[digits - 1] <= '9') {
        --digits;
    }
    if (digits == at || digits == 0 || text[digits - 1] != '+') {
        return std::nullopt;
    }
    return leadingNumber(text, digits);
}

// "At Will:" and "1/Day Each:" regions. A spell in one takes that limit.
struct Section {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::optional<int> perDay;
};

std::vector<Section> frequencySections(const std::string& text)
{
    struct Header {
        std::size_t pos = 0;
        std::size_t content = 0;
        std::optional<int> perDay;
    };
    std::vector<Header> headers;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.compare(i, 8, "At Will:") == 0) {
            headers.push_back(Header{i, i + 8, std::nullopt});
            continue;
        }
        if (text[i] < '0' || text[i] > '9') {
            continue;
        }
        const std::optional<int> number = leadingNumber(text, i);
        if (!number.has_value()) {
            continue;
        }
        std::size_t cursor = i;
        while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
            ++cursor;
        }
        if (text.compare(cursor, 4, "/Day") != 0) {
            continue;
        }
        cursor += 4;
        if (text.compare(cursor, 5, " Each") == 0) {
            cursor += 5;
        }
        while (cursor < text.size() && text[cursor] == ' ') {
            ++cursor;
        }
        if (cursor < text.size() && text[cursor] == ':') {
            headers.push_back(Header{i, cursor + 1, number});
        }
    }
    std::vector<Section> sections;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        const std::size_t end = i + 1 < headers.size() ? headers[i + 1].pos : text.size();
        sections.push_back(Section{headers[i].content, end, headers[i].perDay});
    }
    return sections;
}

// "casts Fireball" and "to cast Fear", through the end of the sentence.
std::vector<std::pair<std::size_t, std::size_t>> castSpans(const std::string& text)
{
    const std::string lower = asciiLower(text);
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    for (const char* phrase : {"casts ", "to cast "}) {
        const std::string needle = phrase;
        std::size_t from = 0;
        while (true) {
            const auto at = lower.find(needle, from);
            if (at == std::string::npos) {
                break;
            }
            const std::size_t begin = at + needle.size();
            const auto period = text.find('.', begin);
            spans.emplace_back(begin, period == std::string::npos ? text.size() : period);
            from = begin;
        }
    }
    return spans;
}

bool inSpan(std::size_t pos, const std::vector<Section>& sections,
            const std::vector<std::pair<std::size_t, std::size_t>>& casts)
{
    for (const Section& section : sections) {
        if (pos >= section.begin && pos < section.end) {
            return true;
        }
    }
    for (const auto& span : casts) {
        if (pos >= span.first && pos < span.second) {
            return true;
        }
    }
    return false;
}

int slotAfter(const std::string& text, std::size_t after, int baseLevel)
{
    std::size_t cursor = after;
    while (cursor < text.size() && text[cursor] == ' ') {
        ++cursor;
    }
    const std::string marker = "(level ";
    if (text.compare(cursor, marker.size(), marker) != 0) {
        return baseLevel;
    }
    cursor += marker.size();
    const std::optional<int> number = leadingNumber(text, cursor);
    if (!number.has_value()) {
        return baseLevel;
    }
    while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
        ++cursor;
    }
    if (text.compare(cursor, 9, " version)") != 0) {
        return baseLevel;
    }
    return *number;
}

std::optional<int> perDayAt(const std::vector<Section>& sections, std::size_t pos)
{
    for (const Section& section : sections) {
        if (pos >= section.begin && pos < section.end) {
            return section.perDay;
        }
    }
    return std::nullopt;
}

std::vector<Mention> mentionsIn(const std::string& text)
{
    const std::vector<Section> sections = frequencySections(text);
    const std::vector<std::pair<std::size_t, std::size_t>> casts = castSpans(text);
    std::vector<Mention> found;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i > 0 && isNameChar(static_cast<unsigned char>(text[i - 1]))) {
            continue;
        }
        if (!inSpan(i, sections, casts)) {
            continue;
        }
        for (const SpellCombatRule* rule : rulesByLength()) {
            const std::size_t length = std::strlen(rule->name);
            if (i + length > text.size() || text.compare(i, length, rule->name) != 0) {
                continue;
            }
            if (i + length < text.size() && isNameChar(static_cast<unsigned char>(text[i + length]))) {
                continue;
            }
            Mention mention;
            mention.rule = rule;
            mention.slot = slotAfter(text, i + length, rule->level);
            const std::string after = text.substr(i, 80);
            mention.selfOnly = containsInsensitive(after, "on itself") || containsInsensitive(after, "self only");
            mention.perDay = perDayAt(sections, i);
            const bool already = std::any_of(found.begin(), found.end(), [&mention](const Mention& other) {
                return other.rule == mention.rule && other.slot == mention.slot;
            });
            if (!already) {
                found.push_back(mention);
            } else if (mention.selfOnly) {
                for (Mention& other : found) {
                    if (other.rule == mention.rule && other.slot == mention.slot) {
                        other.selfOnly = true;
                    }
                }
            }
            i += length - 1;
            break;
        }
    }
    return found;
}

std::string diceText(int count, int sides, int flat)
{
    std::string text = std::to_string(count) + "d" + std::to_string(sides);
    if (flat > 0) {
        text += "+" + std::to_string(flat);
    }
    return text;
}

int scaledCount(const SpellDice& die, const SpellCombatRule& rule, int extra)
{
    if (extra <= 0 || rule.upCount <= 0) {
        return die.count;
    }
    const bool opening = die.when[0] == '\0';
    if (std::strcmp(rule.upMode, "initial") == 0) {
        return opening ? die.count + rule.upCount * extra : die.count;
    }
    if (std::strcmp(rule.upMode, "types") == 0 && !listed(rule.upTypes, die.type)) {
        return die.count;
    }
    return die.count + rule.upCount * extra;
}

std::string damageClause(const SpellDice& die, int count)
{
    std::string text = diceText(count, die.sides, die.flat);
    if (die.type[0] != '\0') {
        text += " ";
        text += die.type;
    }
    return text + " damage";
}

std::optional<MonsterAttack> buildAttack(const Mention& mention, std::optional<int> saveDc,
                                         std::optional<int> attackBonus, bool noComponents)
{
    const SpellCombatRule& rule = *mention.rule;
    const bool needsSave = std::strcmp(rule.delivery, "save") == 0;
    const bool needsAttack = std::strcmp(rule.delivery, "attack") == 0;
    if (needsSave && !saveDc.has_value()) {
        return std::nullopt;
    }
    if (needsAttack && !attackBonus.has_value()) {
        return std::nullopt;
    }
    const int extra = std::max(0, mention.slot - rule.level);
    MonsterAttack attack;
    attack.name = rule.name;
    if (mention.slot != rule.level) {
        attack.name += " (level " + std::to_string(mention.slot) + ")";
    }
    attack.effect = rule.melee ? "Melee spell attack. " : "";
    attack.effect += "The creature casts ";
    attack.effect += rule.name;
    attack.effect += ".";
    if (noComponents) {
        attack.effect += " It uses no spell components.";
    }
    for (int i = 0; i < rule.damageCount; ++i) {
        const SpellDice& die = rule.damage[i];
        const int count = scaledCount(die, rule, extra);
        if (count <= 0) {
            continue;
        }
        if (die.when[0] == '\0') {
            DamagePart part;
            part.dice = diceText(count, die.sides, die.flat);
            part.type = die.type;
            attack.damage.push_back(std::move(part));
        } else if (std::strcmp(die.when, "later") == 0) {
            attack.effect += " At the end of its next turn it takes " + damageClause(die, count) + ".";
            DamagePart part;
            part.dice = diceText(count, die.sides, die.flat);
            part.type = die.type;
            attack.laterDamage.push_back(std::move(part));
        } else if (std::strcmp(die.when, "aside") == 0) {
            attack.effect += " Hit or miss, a saving throw or " + damageClause(die, count) + ".";
        } else if (std::strcmp(die.when, "zone") == 0) {
            attack.effect += " While the spell lasts, a creature that ends its turn in the area or enters it takes " +
                             damageClause(die, count) + ".";
        } else if (std::strcmp(die.when, "repeat") == 0) {
            attack.effect += " On a later failed save it takes " + damageClause(die, count) + ".";
        }
    }
    if (rule.note[0] != '\0') {
        attack.effect += " ";
        attack.effect += rule.note;
    }
    if (needsAttack) {
        attack.attackBonus = *attackBonus;
    }
    if (needsSave) {
        const std::optional<Ability> ability = abilityFromKey(rule.saveAbility);
        if (!ability.has_value()) {
            return std::nullopt;
        }
        SaveSpec save;
        save.ability = *ability;
        save.dc = *saveDc;
        save.halfOnSuccess = rule.halfOnSuccess;
        save.advantageIf = rule.advantageIf;
        attack.save = save;
    }
    if (rule.repeatSave[0] != '\0' && saveDc.has_value()) {
        if (const std::optional<Ability> ability = abilityFromKey(rule.repeatSave)) {
            SaveSpec save;
            save.ability = *ability;
            save.dc = *saveDc;
            attack.repeatSave = save;
        }
    }
    attack.halfDamageOnMiss = rule.halfOnMiss;
    attack.area = rule.area;
    attack.allowSelf = rule.allowSelf;
    attack.selfOnly = mention.selfOnly;
    attack.repeatSameTarget = rule.repeatSameTarget;
    attack.strikes = std::max(1, rule.strikes + rule.strikesPerSlot * extra);
    attack.targetTypes = splitWords(rule.targetTypes);
    if (rule.hpThreshold > 0) {
        attack.failureHpThreshold = rule.hpThreshold;
        if (std::strcmp(rule.below, "dies") == 0) {
            attack.failureHpEffect = "dies";
        }
    }
    if (rule.concentration) {
        attack.concentration = rule.id;
    }
    attack.perDay = mention.perDay;
    for (int i = 0; i < rule.grantCount; ++i) {
        const SpellGrant& grant = rule.grants[i];
        ConditionRider rider;
        rider.conditions = splitWords(grant.ids);
        if (rider.conditions.empty()) {
            continue;
        }
        rider.on = grant.on;
        rider.until = grant.until;
        rider.saveEnds = grant.saveEnds;
        rider.saveOnDemand = !grant.saveEnds && hasManualRepeatSave(rule.id);
        rider.requiresSenses = needsSightOrHearing(rule.id);
        rider.worsensTo = splitWords(grant.worsens);
        rider.endsOn = splitWords(grant.endsOn);
        rider.worseEndsOn = rider.worsensTo.empty() ? std::vector<std::string>{} : rider.endsOn;
        rider.ask = grant.ask;
        rider.tiedTo = grant.tiedTo;
        if (rule.concentration) {
            rider.concentration = rule.id;
        }
        attack.riders.push_back(std::move(rider));
    }
    if (std::strcmp(rule.id, "heat-metal") == 0) {
        // The burn is automatic; the save is only to keep hold of the object.
        attack.save.reset();
    }
    if (std::strcmp(rule.id, "searing-smite") == 0 && saveDc.has_value()) {
        // The extra damage comes with the hit (no save for it). Then, at the
        // start of each of its turns until the spell ends: the same damage,
        // and a Constitution save that ends it.
        SaveSpec repeat;
        repeat.ability = Ability::Constitution;
        repeat.dc = *saveDc;
        attack.save.reset();
        attack.repeatSave = repeat;
        ConditionRider burn;
        burn.conditions = {kBurning};
        burn.on = kRiderOnCast;
        burn.until = kUntilMinute;
        burn.saveEnds = true;
        burn.saveAtStart = true;
        burn.ongoing = attack.damage;
        burn.ongoingAt = kOngoingAtTarget;
        attack.riders.push_back(std::move(burn));
        attack.effect += " At the start of each of its turns until the spell ends, the target takes the damage again "
                         "and makes a Constitution save: on a success the spell ends.";
    }
    if (std::strcmp(rule.id, "phantasmal-killer") == 0 && saveDc.has_value()) {
        // Failed: Disadvantage on ability checks and attack rolls, and at the
        // end of each of its turns a save: a failure takes the damage again, a
        // success ends the spell.
        ConditionRider fear;
        fear.conditions = {kPhantasmalFear};
        fear.on = kRiderOnFailure;
        fear.until = kUntilMinute;
        fear.saveEnds = true;
        fear.saveFailDamage = attack.damage;
        fear.concentration = rule.id;
        attack.riders.push_back(std::move(fear));
        attack.effect += " On a failed save it has Disadvantage on ability checks and attack rolls. At the end of each "
                         "of its turns it saves again: a failure takes the damage again, a success ends the spell.";
    }
    if (std::strcmp(rule.id, "ensnaring-strike") == 0 && !attack.riders.empty()) {
        // The 1d6 Piercing comes at the start of each of its turns while it is
        // Restrained, not with the hit.
        attack.riders.front().ongoing = attack.damage;
        attack.riders.front().ongoingAt = kOngoingAtTarget;
        attack.damage.clear();
    }
    return attack;
}

std::string blockText(const Monster& monster)
{
    std::string text;
    for (const MonsterAttack& attack : monster.attacks) {
        text.push_back('\n');
        text += attack.effect;
    }
    for (const std::vector<MonsterFeature>* features :
         {&monster.traits, &monster.bonusActions, &monster.reactions, &monster.legendaryActions}) {
        for (const MonsterFeature& feature : *features) {
            text.push_back('\n');
            text += feature.effect;
        }
    }
    return text;
}

std::vector<MonsterAttack> spellsFrom(const std::string& effect, const Monster& monster, bool inMultiattack,
                                      const std::string& multiattackAs, bool dropPerDay)
{
    std::optional<int> dc = spellSaveDc(effect);
    std::optional<int> bonus = spellAttackBonus(effect);
    if (!dc.has_value() || !bonus.has_value()) {
        const std::string block = blockText(monster);
        if (!dc.has_value()) {
            dc = spellSaveDc(block);
        }
        if (!bonus.has_value()) {
            bonus = spellAttackBonus(block);
        }
    }
    if (!bonus.has_value() && dc.has_value()) {
        bonus = *dc - 8;
    }
    const bool silent =
        containsInsensitive(effect, "no spell components") || containsInsensitive(effect, "no components");
    std::vector<MonsterAttack> attacks;
    for (const Mention& mention : mentionsIn(effect)) {
        std::optional<MonsterAttack> attack = buildAttack(mention, dc, bonus, silent);
        if (!attack.has_value()) {
            continue;
        }
        attack->inMultiattack = inMultiattack;
        if (inMultiattack) {
            attack->multiattackAs = multiattackAs;
        }
        if (dropPerDay) {
            attack->perDay.reset();
        }
        attacks.push_back(std::move(*attack));
    }
    return attacks;
}

}  // namespace

std::optional<MonsterAttack> spellAsAttack(const std::string& name, int slotLevel, int saveDc,
                                           std::optional<int> attackBonus)
{
    const SpellCombatRule* rule = findRule(name);
    if (rule == nullptr) {
        return std::nullopt;
    }
    Mention mention;
    mention.rule = rule;
    mention.slot = slotLevel > 0 ? slotLevel : rule->level;
    const std::optional<int> dc = saveDc > 0 ? std::optional<int>(saveDc) : std::nullopt;
    std::optional<int> bonus = attackBonus;
    if (!bonus.has_value() && dc.has_value()) {
        bonus = *dc - 8;
    }
    return buildAttack(mention, dc, bonus, false);
}

std::vector<MonsterAttack> actionableSpells(const Monster& monster, const MonsterAttack& action)
{
    if (isMultiattack(action) || action.attackBonus.has_value() || action.save.has_value() ||
        action.selfEffect.has_value()) {
        return {};
    }
    std::vector<MonsterAttack> spells = spellsFrom(action.effect, monster, action.inMultiattack, action.name, false);
    if (!action.inMultiattack) {
        return spells;
    }
    // Multiattack lets the dragon replace an attack with Spellcasting "to cast
    // Shatter": only the spells it names are part of Multiattack. The rest of
    // the list (Invisibility, Detect Magic) is a separate action.
    std::string multiattackText;
    for (const MonsterAttack& other : monster.attacks) {
        if (isMultiattack(other)) {
            multiattackText += other.effect;
        }
    }
    // "Acid Arrow (level 3)" is named in the text as "Acid Arrow (level 3 version)".
    const auto named = [&multiattackText](const MonsterAttack& spell) {
        return containsInsensitive(multiattackText, spell.name.substr(0, spell.name.find(" (")));
    };
    // When Multiattack names no spell at all (an oni's "uses Spellcasting"),
    // any of them can replace an attack.
    if (std::none_of(spells.begin(), spells.end(), named)) {
        return spells;
    }
    for (MonsterAttack& spell : spells) {
        if (!named(spell)) {
            spell.inMultiattack = false;
            spell.multiattackAs.clear();
        }
    }
    return spells;
}

std::vector<MonsterAttack> actionableSpells(const Monster& monster, const MonsterFeature& feature)
{
    if (feature.targeted.has_value() || feature.selfEffect.has_value()) {
        return {};
    }
    const bool ownBudget = feature.perDay.has_value() || feature.recharge.has_value();
    return spellsFrom(feature.effect, monster, false, {}, ownBudget);
}

std::vector<std::string> combatSpellMentions(const std::string& text)
{
    std::vector<std::string> names;
    for (const Mention& mention : mentionsIn(text)) {
        std::string name = mention.rule->name;
        if (mention.slot != mention.rule->level) {
            name += " (level " + std::to_string(mention.slot) + ")";
        }
        names.push_back(std::move(name));
    }
    return names;
}

SpellStrike resolveSpellStrike(const MonsterAttack& attack, int currentHp)
{
    SpellStrike strike;
    if (!attack.failureHpThreshold.has_value()) {
        return strike;
    }
    const bool below = currentHp <= *attack.failureHpThreshold;
    if (attack.failureHpEffect == "dies") {
        strike.applyDamage = !below;
        strike.applyConditions = false;
        strike.kill = below;
        return strike;
    }
    // A condition with a Hit Point limit (Power Word Stun), not a damage spell.
    strike.applyDamage = false;
    strike.applyConditions = below;
    return strike;
}

namespace {

struct FollowUpRule {
    const char* spell;
    FollowUpCost cost;
    const char* suffix;  // after the spell's name: "Call Lightning (again)"
    const char* text;
    bool zone;    // the area's own damage (a creature entering it), any time
    bool single;  // one creature, though the spell is an area
};

// What a concentration spell lets the caster do on later turns, and areas that
// hurt a creature that enters them or ends its turn there.
constexpr FollowUpRule kFollowUps[] = {
    {"call-lightning", FollowUpCost::Action, "again",
     "Magic action: another bolt from the storm cloud, at the same point or a different one. Each creature within 5 "
     "feet of it saves.",
     false, false},
    {"moonbeam", FollowUpCost::Action, "move",
     "Magic action: move the beam up to 60 feet. Each creature it moves into saves (once per turn).", false, false},
    {"moonbeam", FollowUpCost::Free, "in the beam",
     "No action: a creature that enters the beam or ends its turn there saves, once per turn.", true, false},
    {"heat-metal", FollowUpCost::BonusAction, "again",
     "Bonus action: the metal burns whoever is touching it again, if it is within range.", false, true},
    {"spiritual-weapon", FollowUpCost::BonusAction, "again",
     "Bonus action: move the weapon up to 20 feet and attack a creature within 5 feet of it.", false, false},
    {"flaming-sphere", FollowUpCost::BonusAction, "ram",
     "Bonus action: roll the sphere up to 30 feet into a creature's space; that creature saves, and the sphere stops.",
     false, true},
    {"flaming-sphere", FollowUpCost::Free, "nearby",
     "No action: a creature that ends its turn within 5 feet of the sphere saves.", true, false},
    {"vampiric-touch", FollowUpCost::Action, "again",
     "Magic action: touch the same creature or a different one.", false, false},
    {"sunbeam", FollowUpCost::Action, "again", "Magic action: a new Line of radiance.", false, false},
    {"eyebite", FollowUpCost::Action, "again",
     "Magic action: target another creature, not one that has succeeded on a save against this casting.", false,
     false},
    {"arcane-hand", FollowUpCost::BonusAction, "again",
     "Bonus action: move the hand up to 60 feet and strike with the Clenched Fist.", false, false},
    {"arcane-sword", FollowUpCost::BonusAction, "again",
     "Bonus action: move the sword up to 30 feet and attack the same target or a different one.", false, false},
    {"flame-blade", FollowUpCost::Action, "again", "Magic action: another melee spell attack with the blade.", false,
     false},
    {"spirit-guardians", FollowUpCost::Free, "in the aura",
     "No action: a creature the aura moves into, or that enters it or ends its turn there, saves (once per turn).",
     true, false},
    {"cloudkill", FollowUpCost::Free, "in the cloud",
     "No action: a creature the cloud moves into, or that enters it or ends its turn there, saves (once per turn).",
     true, false},
    {"incendiary-cloud", FollowUpCost::Free, "in the cloud",
     "No action: a creature the cloud moves into, or that enters it or ends its turn there, saves (once per turn).",
     true, false},
    {"insect-plague", FollowUpCost::Free, "in the swarm",
     "No action: a creature that enters the swarm for the first time on a turn or ends its turn there saves.", true,
     false},
    {"wall-of-fire", FollowUpCost::Free, "at the wall",
     "No action: a creature that ends its turn within 10 feet of the hot side or inside the wall, or enters it for "
     "the first time on a turn, saves.",
     true, false},
    {"wall-of-thorns", FollowUpCost::Free, "in the wall",
     "No action: a creature that enters the wall or ends its turn there saves.", true, false},
    {"wall-of-ice", FollowUpCost::Free, "the frigid air",
     "No action: a creature moving through the frigid air where a panel broke, for the first time on a turn, saves.",
     true, false},
    {"blade-barrier", FollowUpCost::Free, "in the wall",
     "No action: a creature that enters the wall for the first time on a turn or ends its turn there saves.", true,
     false},
    {"black-tentacles", FollowUpCost::Free, "in the area",
     "No action: a creature that enters the area or ends its turn there saves (once per turn).", true, false},
};

const SpellCombatRule* ruleById(const std::string& id)
{
    for (const SpellCombatRule& rule : kSpellRules) {
        if (id == rule.id) {
            return &rule;
        }
    }
    return nullptr;
}

bool hasFollowUps(const std::string& spellId)
{
    return std::any_of(std::begin(kFollowUps), std::end(kFollowUps),
                       [&spellId](const FollowUpRule& row) { return spellId == row.spell; });
}

}  // namespace

std::optional<SustainedSpell> sustainedSpellFor(const MonsterAttack& cast)
{
    if (cast.concentration.empty() || !hasFollowUps(cast.concentration)) {
        return std::nullopt;
    }
    const SpellCombatRule* rule = ruleById(cast.concentration);
    if (rule == nullptr) {
        return std::nullopt;
    }
    SustainedSpell sustained;
    sustained.spellId = rule->id;
    sustained.slot = rule->level;
    // "Call Lightning (level 4)"
    const std::string marker = " (level ";
    if (const std::size_t at = cast.name.rfind(marker); at != std::string::npos) {
        sustained.slot = std::max(rule->level, std::atoi(cast.name.c_str() + at + marker.size()));
    }
    if (cast.save.has_value()) {
        sustained.saveDc = cast.save->dc;
    } else if (cast.repeatSave.has_value()) {
        sustained.saveDc = cast.repeatSave->dc;
    }
    sustained.attackBonus = cast.attackBonus;
    return sustained;
}

std::vector<SpellFollowUp> spellFollowUps(const SustainedSpell& sustained)
{
    std::vector<SpellFollowUp> followUps;
    const SpellCombatRule* rule = ruleById(sustained.spellId);
    if (rule == nullptr) {
        return followUps;
    }
    Mention mention;
    mention.rule = rule;
    mention.slot = std::max(rule->level, sustained.slot);
    // A save spell whose save was set aside (Heat Metal) still needs a DC to build.
    std::optional<int> dc = sustained.saveDc;
    if (!dc.has_value() && sustained.attackBonus.has_value()) {
        dc = *sustained.attackBonus + 8;
    }
    if (!dc.has_value() && std::strcmp(rule->id, "heat-metal") == 0) {
        dc = 10;  // set aside again as it is built: the burn has no save
    }
    std::optional<int> bonus = sustained.attackBonus;
    if (!bonus.has_value() && dc.has_value()) {
        bonus = *dc - 8;
    }
    const std::optional<MonsterAttack> base = buildAttack(mention, dc, bonus, false);
    if (!base.has_value()) {
        return followUps;
    }
    const int extra = std::max(0, mention.slot - rule->level);
    for (const FollowUpRule& row : kFollowUps) {
        if (sustained.spellId != row.spell) {
            continue;
        }
        MonsterAttack attack = *base;
        attack.name = std::string(rule->name) + " (" + row.suffix + ")";
        attack.effect = row.text;
        // Using it again is not a new casting: no new concentration, no use spent.
        attack.concentration.clear();
        attack.perDay.reset();
        attack.multiattackAs.clear();
        attack.inMultiattack = false;
        attack.laterDamage.clear();
        if (row.zone) {
            std::vector<DamagePart> zone;
            for (int i = 0; i < rule->damageCount; ++i) {
                const SpellDice& die = rule->damage[i];
                const int count = scaledCount(die, *rule, extra);
                if (std::strcmp(die.when, "zone") == 0 && count > 0) {
                    DamagePart part;
                    part.dice = diceText(count, die.sides, die.flat);
                    part.type = die.type;
                    zone.push_back(std::move(part));
                }
            }
            if (!zone.empty()) {
                attack.damage = std::move(zone);
            }
            attack.area = true;
        }
        if (row.single) {
            attack.area = false;
        }
        followUps.push_back(SpellFollowUp{std::move(attack), row.cost});
    }
    return followUps;
}

int spellCombatRuleCount()
{
    return static_cast<int>(std::size(kSpellRules));
}

}  // namespace combat
