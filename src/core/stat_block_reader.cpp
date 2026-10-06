#include "core/stat_block_reader.h"

#include "core/text.h"

#include <algorithm>
#include <regex>
#include <tuple>

namespace combat {

namespace {

const char* const kTypes =
    "(Acid|Bludgeoning|Cold|Fire|Force|Lightning|Necrotic|Piercing|Poison|Psychic|Radiant|Slashing|Thunder)";
const char* const kConditionWord =
    "(?:Blinded|Charmed|Deafened|Frightened|Grappled|Incapacitated|Invisible|Paralyzed|Petrified|Poisoned|Prone|"
    "Restrained|Stunned|Unconscious)";
const char* const kSizes = "(Tiny|Small|Medium|Large|Huge|Gargantuan)";
const char* const kAbilities = "(Strength|Dexterity|Constitution|Intelligence|Wisdom|Charisma)";

std::optional<Ability> abilityWord(const std::string& word)
{
    return abilityFromKey(asciiLower(word));
}

std::vector<std::string> sentences(const std::string& text)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '.' && i + 1 < text.size() && (text[i + 1] == ' ' || text[i + 1] == '\n')) {
            out.push_back(text.substr(start, i + 1 - start));
            start = i + 1;
            while (start < text.size() && (text[start] == ' ' || text[start] == '\n')) {
                ++start;
            }
            i = start > 0 ? start - 1 : 0;
        }
    }
    if (start < text.size()) {
        out.push_back(text.substr(start));
    }
    return out;
}

std::vector<std::string> conditionIds(const std::string& words)
{
    static const std::regex word(kConditionWord);
    std::vector<std::string> ids;
    for (auto it = std::sregex_iterator(words.begin(), words.end(), word); it != std::sregex_iterator(); ++it) {
        ids.push_back(asciiLower(it->str()));
    }
    return ids;
}

// --- Damage --------------------------------------------------------------------

DamageWhen classify(std::string after, std::string before)
{
    after = asciiLower(after);
    before = asciiLower(before);
    const std::string trimmedBefore = trim(before);
    const auto endsWith = [&trimmedBefore](const std::string& tail) {
        return trimmedBefore.size() >= tail.size() &&
               trimmedBefore.compare(trimmedBefore.size() - tail.size(), tail.size(), tail) == 0;
    };
    if (endsWith(", or") || endsWith("\xE2\x80\x94or")) {
        return after.find("advantage") != std::string::npos ? DamageWhen::AdvantageAlt : DamageWhen::Alternative;
    }
    if (after.find("if the attack roll had advantage") != std::string::npos) {
        return DamageWhen::Advantage;
    }
    static const std::regex eachOf("(start|end) of each of");
    if (std::regex_search(after + " " + before, eachOf)) {
        return DamageWhen::Ongoing;
    }
    std::string clause = after.substr(0, after.find_first_of(",."));
    const std::size_t dash = clause.find("\xE2\x80\x94");
    if (dash != std::string::npos) {
        clause = clause.substr(0, dash);
    }
    std::string tail = before;
    const std::size_t comma = tail.find_last_of(',');
    const std::size_t emDash = tail.rfind("\xE2\x80\x94");
    if (comma != std::string::npos && (emDash == std::string::npos || comma > emDash)) {
        tail = tail.substr(comma + 1);
    } else if (emDash != std::string::npos) {
        tail = tail.substr(emDash + 3);
    }
    static const std::regex ifWord("\\bif\\b");
    if (std::regex_search(clause, ifWord) || before.find("extra") != std::string::npos ||
        trim(tail).rfind("if ", 0) == 0) {
        return DamageWhen::Conditional;
    }
    return DamageWhen::Always;
}

// When an "or" or extra part applies: a kDamageIf... the app can check, or
// the stat block's words for the GM to tick.
std::string damageCondition(const std::string& after, const std::string& before)
{
    const std::string text = before + " " + after;
    std::smatch m;
    if (std::regex_search(text, m,
                          std::regex("the ([a-z][a-z \\-]*?) moved (\\d+)\\+ feet straight toward (?:it|the target) "
                                     "immediately before the hit"))) {
        return "the " + m[1].str() + " moved " + m[2].str() + "+ feet straight toward it immediately before the hit";
    }
    std::string words;
    if (std::regex_search(after, m, std::regex("\\bif (.+?)(?:\xE2\x80\x94|\\.|,|$)"))) {
        words = trim(m[1].str());
    } else if (std::regex_search(before, m, std::regex("\\b[Ii]f (.+?),"))) {
        words = trim(m[1].str());
    }
    if (words.empty() || words.find("had Advantage") != std::string::npos) {
        return {};
    }
    if (words == "the target is Bloodied") {
        return kDamageIfTargetBloodied;
    }
    if (std::regex_match(words, std::regex("(?:the|this) [a-z ]+ is Bloodied|it is Bloodied"))) {
        return kDamageIfSelfBloodied;
    }
    if (words.rfind("the target is Grappled by", 0) == 0) {
        return kDamageIfGrappledBySelf;
    }
    return words;
}

std::vector<DamagePart> damageParts(const std::string& segment)
{
    static const std::regex rolled(std::string("\\b\\d+ \\((\\d+)d(\\d+)(?:\\s*(\\+|-|\xE2\x88\x92|\xE2\x80\x93)\\s*(\\d+))?\\) (?:") +
                                   kTypes + " )?damage");
    static const std::regex flat(std::string("(^|[^\\d(])(\\d+) ") + kTypes + " damage");
    struct Found {
        std::size_t start;
        std::size_t end;
        std::string dice;
        std::string type;
    };
    std::vector<Found> found;
    for (auto it = std::sregex_iterator(segment.begin(), segment.end(), rolled); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        std::string dice = m[1].str() + "d" + m[2].str();
        if (m[4].matched) {
            dice += (m[3].str() == "+" ? "+" : "-") + m[4].str();
        }
        found.push_back(Found{static_cast<std::size_t>(m.position(0)),
                              static_cast<std::size_t>(m.position(0) + m.length(0)), dice, asciiLower(m[5].str())});
    }
    for (auto it = std::sregex_iterator(segment.begin(), segment.end(), flat); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        const auto start = static_cast<std::size_t>(m.position(2));
        const bool inside = std::any_of(found.begin(), found.end(),
                                        [start](const Found& f) { return f.start <= start && start < f.end; });
        if (!inside) {
            found.push_back(Found{start, static_cast<std::size_t>(m.position(0) + m.length(0)), m[2].str(),
                                  asciiLower(m[3].str())});
        }
    }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.start < b.start; });
    std::vector<DamagePart> parts;
    for (std::size_t i = 0; i < found.size(); ++i) {
        const Found& f = found[i];
        std::size_t left = 0;
        const std::size_t dot = segment.rfind(". ", f.start);
        const std::size_t colon = segment.rfind(": ", f.start);
        const std::size_t stop = std::max(dot == std::string::npos ? 0 : dot + 2, colon == std::string::npos ? 0 : colon + 2);
        left = stop;
        if (i > 0) {
            left = std::max(left, found[i - 1].end);
        }
        std::size_t right = segment.find(". ", f.end);
        right = right == std::string::npos ? segment.size() : right;
        if (i + 1 < found.size()) {
            right = std::min(right, found[i + 1].start);
        }
        const std::string after = segment.substr(f.end, right > f.end ? right - f.end : 0);
        const std::string before = segment.substr(left, f.start > left ? f.start - left : 0);
        DamagePart part{f.dice, f.type, classify(after, before)};
        if (part.when == DamageWhen::Alternative || part.when == DamageWhen::Conditional) {
            part.condition = damageCondition(after, before);
        }
        parts.push_back(std::move(part));
    }
    return parts;
}

// --- Riders ----------------------------------------------------------------------

void readDuration(const std::string& after, ConditionRider& rider)
{
    std::string clause = after;
    for (std::size_t i = 0; i < clause.size(); ++i) {
        if (clause[i] == '.' && (i + 1 == clause.size() || clause[i + 1] == ' ')) {
            clause = clause.substr(0, i);
            break;
        }
    }
    static const std::regex escape("^\\s*\\(escape DC (\\d+)\\)");
    std::smatch m;
    if (std::regex_search(clause, m, escape)) {
        rider.escapeDc = std::stoi(m[1].str());
    }
    const std::string lowered = asciiLower(clause);
    static const std::regex edge("until the (start|end) of (?:the )?([a-z\xE2\x80\x99' \\-]+?)(?:\xE2\x80\x99s|'s)? next turn");
    if (std::regex_search(lowered, m, edge)) {
        const std::string whose = trim(m[2].str());
        const bool target = whose == "its" || whose == "target" || whose == "the target";
        rider.until = std::string(target ? "target" : "source") + (m[1].str() == "start" ? "Start" : "End");
    } else if (lowered.find("until the end of its turn") != std::string::npos) {
        rider.until = kUntilTargetThisTurn;
    } else if (std::regex_search(lowered, std::regex("\\bfor 1 minute\\b"))) {
        rider.until = kUntilMinute;
    }
    if (lowered.find("until the grapple ends") != std::string::npos) {
        rider.tiedTo = "grappled";
    }
    if (lowered.find("repeats the save at the end of each of its turns") != std::string::npos) {
        rider.saveEnds = true;
    }
    if (std::regex_search(lowered, std::regex("until the [a-z ]+ dies"))) {
        rider.endsOn.push_back(kEndsOnSourceGone);
    }
    if (std::regex_search(lowered, std::regex("until it takes damage|ends early if the target takes (?:any )?damage"))) {
        rider.endsOn.push_back(kEndsOnTakesDamage);
    }
}

std::vector<ConditionRider> segmentRiders(const std::string& segment, const std::string& on)
{
    static const std::regex has(std::string("has the (") + kConditionWord + "(?:(?:,? and |, )" + kConditionWord +
                                ")*) conditions?");
    static const std::regex size(std::string("[Ii]f (?:the )?target is (?:a |an )?") + kSizes + " or smaller");
    static const std::regex moved("and the ([a-z][a-z \\-]*?) moved (\\d+)\\+ feet straight toward it immediately "
                                  "before the hit");
    static const std::regex fewHp("[Ii]f the (?:target|creature) has (\\d+) Hit Points or fewer");
    static const std::vector<std::pair<std::string, std::string>> tiedPrefixes{
        {"While Grappled", "grappled"},
        {"Until the grapple ends", "grappled"},
        {"While Poisoned", "poisoned"},
        {"While Charmed", "charmed"},
        {"While it has the Poisoned condition", "poisoned"},
        {"While Restrained", "restrained"}};
    static const std::regex advantage("and the [a-z][a-z \\-]*? had Advantage on the attack roll");
    std::vector<ConditionRider> riders;
    for (std::string sentence : sentences(segment)) {
        sentence = trim(sentence);
        // What happens if it dies or lets go is not part of the hit.
        if (sentence.find("regurgitate") != std::string::npos || sentence.find(" dies, ") != std::string::npos) {
            continue;
        }
        std::string tied;
        std::size_t prefixLength = 0;
        for (const auto& [prefix, condition] : tiedPrefixes) {
            if (sentence.rfind(prefix, 0) == 0) {
                tied = condition;
                prefixLength = prefix.size();
            }
        }
        std::smatch sizeMatch;
        const bool hasSize = std::regex_search(sentence, sizeMatch, size);
        std::smatch movedMatch;
        const bool hasMoved = std::regex_search(sentence, movedMatch, moved);
        std::smatch hpMatch;
        const bool hasHp = std::regex_search(sentence, hpMatch, fewHp);
        const bool zero = sentence.find("reduces the target to 0 Hit Points") != std::string::npos ||
                          sentence.find("reduced to 0 Hit Points by this") != std::string::npos;
        std::vector<std::smatch> matches;
        for (auto it = std::sregex_iterator(sentence.begin(), sentence.end(), has); it != std::sregex_iterator(); ++it) {
            const auto at = static_cast<std::size_t>(it->position(0));
            const bool noLonger = at >= 10 && sentence.compare(at - 10, 10, "no longer ") == 0;
            if (at >= prefixLength && !noLonger) {
                matches.push_back(*it);
            }
        }
        // The rider the sentence's other words belong to: this sentence's
        // first, or the one its "While Poisoned" names, or the last one.
        ConditionRider* anchor = nullptr;
        const std::size_t before = riders.size();
        for (std::size_t i = 0; i < matches.size(); ++i) {
            const std::smatch& match = matches[i];
            ConditionRider rider;
            rider.conditions = conditionIds(match[1].str());
            rider.on = on;
            if (hasSize && sizeMatch.position(0) < match.position(0)) {
                rider.targetMaxSize = sizeMatch[1].str();
            }
            if (hasMoved && movedMatch.position(0) < match.position(0)) {
                rider.ask = "the " + movedMatch[1].str() + " moved " + movedMatch[2].str() +
                            "+ feet straight toward it immediately before the hit";
            }
            if (std::regex_search(sentence, advantage)) {
                rider.ask = "@advantage";
            }
            if (hasHp && hpMatch.position(0) < match.position(0)) {
                rider.targetMaxHp = std::stoi(hpMatch[1].str());
            }
            if (!tied.empty()) {
                rider.tiedTo = tied;
            }
            // Up to the next condition: "it has the Grappled condition (escape
            // DC 14), and it has the Restrained condition until the grapple ends."
            const auto from = static_cast<std::size_t>(match.position(0) + match.length(0));
            const std::size_t to =
                i + 1 < matches.size() ? static_cast<std::size_t>(matches[i + 1].position(0)) : sentence.size();
            readDuration(sentence.substr(from, to - from), rider);
            riders.push_back(std::move(rider));
        }
        if (riders.size() > before) {
            anchor = &riders[before];
        } else if (!riders.empty()) {
            anchor = &riders.back();
            for (ConditionRider& rider : riders) {
                if (!tied.empty() &&
                    std::find(rider.conditions.begin(), rider.conditions.end(), tied) != rider.conditions.end()) {
                    anchor = &rider;
                }
            }
        }
        if (anchor == nullptr) {
            continue;
        }
        // A rider tied to another starts when that one does ("While Poisoned").
        for (std::size_t i = before; i < riders.size(); ++i) {
            for (const ConditionRider& other : riders) {
                if (!riders[i].tiedTo.empty() &&
                    std::find(other.conditions.begin(), other.conditions.end(), riders[i].tiedTo) !=
                        other.conditions.end()) {
                    riders[i].on = other.on;
                    if (riders[i].targetMaxSize.empty()) {
                        riders[i].targetMaxSize = other.targetMaxSize;
                    }
                }
            }
        }
        if (zero && riders.size() > before) {
            anchor->on = kRiderOnZeroHp;
            if (sentence.find("becomes Stable") != std::string::npos) {
                anchor->stabilize = true;
            }
        }
        // "... and takes 10 (3d6) Acid damage at the start of each of the
        // cube's turns", "it repeats the save at the end of each of its turns".
        std::vector<DamagePart> ongoing;
        const std::string lowered = asciiLower(sentence);
        if (lowered.find("at the start of each of") != std::string::npos) {
            for (DamagePart part : damageParts(sentence)) {
                part.when = DamageWhen::Always;
                ongoing.push_back(part);
            }
        }
        if (!ongoing.empty() && anchor->ongoing.empty()) {
            anchor->ongoing = ongoing;
            anchor->ongoingAt = lowered.find("start of each of its turns") != std::string::npos
                                    ? kOngoingAtTarget
                                    : kOngoingAtSource;
        }
        if (matches.empty() && sentence.find("repeats the save at the end of each of its turns") != std::string::npos) {
            anchor->saveEnds = true;
        }
    }
    // "If the behir dies, a swallowed creature is no longer Restrained."
    if (std::regex_search(segment, std::regex(" dies, [^.]*no longer"))) {
        for (ConditionRider& rider : riders) {
            rider.endsOn.push_back(kEndsOnSourceGone);
        }
    }
    // "It is swallowed and no longer Grappled."
    if (!riders.empty() && (segment.find("no longer Grappled") != std::string::npos ||
                            segment.find("the Grappled condition ends") != std::string::npos)) {
        riders.front().removes = {"grappled"};
    }
    return riders;
}

// "Failure:", "Second Failure:", ... and the text each one covers.
std::vector<std::pair<std::string, std::string>> outcomeSegments(const std::string& text)
{
    static const std::regex mark(
        "(First Failure|Second Failure|Subsequent Failures|Failure by 5 or More|Failure or Success|Failure|Success):");
    std::vector<std::pair<std::string, std::size_t>> marks;
    std::vector<std::size_t> ends;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), mark); it != std::sregex_iterator(); ++it) {
        marks.emplace_back((*it)[1].str(), static_cast<std::size_t>(it->position(0) + it->length(0)));
        ends.push_back(static_cast<std::size_t>(it->position(0)));
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (std::size_t i = 0; i < marks.size(); ++i) {
        const std::size_t end = i + 1 < marks.size() ? ends[i + 1] : text.size();
        out.emplace_back(marks[i].first, text.substr(marks[i].second, end - marks[i].second));
    }
    return out;
}

std::string segmentFor(const std::vector<std::pair<std::string, std::string>>& segments, const std::string& key)
{
    for (const auto& [name, text] : segments) {
        if (name == key) {
            return text;
        }
    }
    return {};
}

std::vector<ConditionRider> failureRiders(const std::string& afterSave)
{
    const auto segments = outcomeSegments(afterSave);
    std::string failure = segmentFor(segments, "Failure");
    if (failure.empty()) {
        failure = segmentFor(segments, "First Failure");
    }
    std::vector<ConditionRider> riders = segmentRiders(failure, kRiderOnFailure);
    // "Second Failure: the target has the Petrified condition instead."
    const std::string second = segmentFor(segments, "Second Failure");
    const std::vector<ConditionRider> worse = segmentRiders(second, kRiderOnFailure);
    if (!worse.empty() && !riders.empty()) {
        ConditionRider& first = riders.front();
        first.saveEnds = true;
        first.worsensTo = worse.front().conditions;
        first.worseSaveEnds = worse.front().saveEnds;
        first.worseEndsOn = worse.front().endsOn;
        if (asciiLower(second).find("takes damage") != std::string::npos &&
            std::find(first.worseEndsOn.begin(), first.worseEndsOn.end(), kEndsOnTakesDamage) ==
                first.worseEndsOn.end()) {
            first.worseEndsOn.push_back(kEndsOnTakesDamage);
        }
        // "Incapacitated until the end of its next turn, at which point it
        // repeats the save": the repeat save decides, not the duration.
        first.until.clear();
    }
    // "Failure by 5 or More": the failure's riders, with these instead of any
    // that give the same conditions.
    const std::vector<ConditionRider> byFive = segmentRiders(segmentFor(segments, "Failure by 5 or More"),
                                                             kRiderOnFailureBy5);
    if (!byFive.empty()) {
        std::vector<ConditionRider> extra;
        for (const ConditionRider& rider : riders) {
            const bool replaced = std::any_of(byFive.begin(), byFive.end(), [&rider](const ConditionRider& other) {
                return other.conditions == rider.conditions;
            });
            if (!replaced) {
                ConditionRider copy = rider;
                copy.on = kRiderOnFailureBy5;
                extra.push_back(std::move(copy));
            }
        }
        riders.insert(riders.end(), extra.begin(), extra.end());
        riders.insert(riders.end(), byFive.begin(), byFive.end());
    }
    return riders;
}

std::vector<std::string> exceptedTypes(const std::string& lead)
{
    std::vector<std::string> types;
    std::smatch m;
    if (std::regex_search(lead, m, std::regex("non-([A-Z][a-z]+)"))) {
        types.push_back(asciiLower(m[1].str()));
    }
    if (std::regex_search(lead, m,
                          std::regex("(?:isn\xE2\x80\x99t|isn't) an? ([A-Za-z]+)(?: or (?:an? )?([A-Za-z]+))?"))) {
        types.push_back(asciiLower(m[1].str()));
        if (m[2].matched) {
            types.push_back(asciiLower(m[2].str()));
        }
    }
    return types;
}

}  // namespace

EntryReading readEntry(const std::string& name, const std::string& effect)
{
    EntryReading reading;
    std::smatch m;
    if (std::regex_search(name, m, std::regex("\\(Recharge (\\d)(?:(?:\xE2\x80\x93|-)\\d)?\\)"))) {
        reading.recharge = std::stoi(m[1].str());
    }
    if (std::regex_search(name, m, std::regex("\\((\\d+)/Day"))) {
        reading.perDay = std::stoi(m[1].str());
    } else if (name.find("Recharge after a Short or Long Rest") != std::string::npos) {
        reading.perDay = 1;
    }

    static const std::regex roll("(?:Melee|Ranged|Melee or Ranged) Attack Roll: (\\+|-|\xE2\x88\x92)(\\d+)");
    static const std::regex saveLine(std::string(kAbilities) + " Saving Throw: DC (\\d+)");
    if (std::regex_search(effect, m, roll)) {
        const int value = std::stoi(m[2].str());
        reading.attackBonus = m[1].str() == "+" ? value : -value;
        const std::size_t hit = effect.find("Hit:");
        const std::string body = hit == std::string::npos ? std::string() : effect.substr(hit + 4);
        std::smatch saveMatch;
        const bool riderSave = std::regex_search(body, saveMatch, saveLine);
        const std::string lead = riderSave ? body.substr(0, static_cast<std::size_t>(saveMatch.position(0))) : body;
        reading.damage = damageParts(lead);
        reading.riders = segmentRiders(lead, kRiderOnHit);
        if (riderSave) {
            const std::string after = body.substr(static_cast<std::size_t>(saveMatch.position(0)));
            for (DamagePart part : damageParts(after)) {
                if (part.when != DamageWhen::Ongoing) {
                    part.when = DamageWhen::Conditional;
                }
                reading.damage.push_back(part);
            }
            std::vector<ConditionRider> failed = failureRiders(after);
            const std::vector<std::string> except = exceptedTypes(lead);
            for (ConditionRider& rider : failed) {
                rider.exceptTypes = except;
            }
            if (!failed.empty()) {
                const auto ability = abilityWord(saveMatch[1].str());
                if (ability.has_value()) {
                    reading.riderSave = SaveSpec{*ability, std::stoi(saveMatch[2].str()), false};
                }
                reading.riders.insert(reading.riders.end(), failed.begin(), failed.end());
            }
        }
    } else if (std::regex_search(effect, m, saveLine)) {
        const auto ability = abilityWord(m[1].str());
        if (ability.has_value()) {
            reading.save = SaveSpec{*ability, std::stoi(m[2].str()), false};
        }
        const std::string after = effect.substr(static_cast<std::size_t>(m.position(0) + m.length(0)));
        const auto segments = outcomeSegments(after);
        std::string failure = segmentFor(segments, "Failure");
        if (failure.empty()) {
            failure = segmentFor(segments, "First Failure");
        }
        reading.damage = damageParts(failure);
        if (asciiLower(segmentFor(segments, "Success")).rfind(" half", 0) == 0 && reading.save.has_value()) {
            reading.save->halfOnSuccess = true;
        }
        static const std::regex area("each creature|each other creature|creatures? in a|\\bcone\\b|\\bline\\b|"
                                     "emanation|sphere|cube",
                                     std::regex::icase);
        reading.area = std::regex_search(effect, area);
        reading.riders = failureRiders(after);
        // Who it can target: "DC 11, one Frightened creature the hag can see".
        std::smatch clauseMatch;
        if (std::regex_search(after, clauseMatch, std::regex("^,([^.]*?)\\.\\s*(?:Failure|First Failure)"))) {
            const std::string clause = clauseMatch[1].str();
            if (std::regex_search(clause, std::regex("^\\s*one "))) {
                std::smatch c;
                if (std::regex_search(clause, c, std::regex(std::string("that has the (") + kConditionWord +
                                                            ") condition"))) {
                    reading.targetCondition = asciiLower(c[1].str());
                }
                if (std::regex_search(clause, c, std::regex(std::string("^\\s*one (") + kConditionWord + ") creature"))) {
                    reading.targetCondition = asciiLower(c[1].str());
                }
                if (std::regex_search(clause, c,
                                      std::regex("(?:(Grappled|Charmed) or )?(Grappled|Charmed) by (?:the|this|it)"))) {
                    reading.targetCondition =
                        c[1].matched ? asciiLower(c[1].str()) + "," + asciiLower(c[2].str()) : asciiLower(c[2].str());
                }
                if (std::regex_search(clause, c, std::regex(std::string("one ") + kSizes + " or smaller creature"))) {
                    reading.targetMaxSize = c[1].str();
                }
            }
        }
        // "Failure: If the target has 20 Hit Points or fewer, it drops to 0
        // Hit Points." / "..., it dies."
        if (std::regex_search(failure, m,
                              std::regex("[Ii]f the (?:target|creature) has (\\d+) Hit Points or fewer, it "
                                         "(drops to 0 Hit Points|dies)"))) {
            reading.failureHpThreshold = std::stoi(m[1].str());
            reading.failureHpEffect = m[2].str() == "dies" ? "dies" : "dropsToZero";
        }
    }
    if (std::regex_search(effect, m,
                          std::regex(std::string("Hit Point maximum decreases by an amount equal to the (?:") +
                                     kTypes + " )?damage taken"))) {
        HpDrain drain;
        drain.type = asciiLower(m[1].str());
        drain.heals = effect.find("regains Hit Points equal to that amount") != std::string::npos;
        reading.drain = drain;
    }
    reading.advantageIfGrappled = effect.find("with Advantage if the target is Grappled by") != std::string::npos;
    // "The giant or one creature of its choice ... gains 16 (2d10 + 5)
    // Temporary Hit Points and has Advantage on attack rolls until the start of
    // the giant's next turn."
    if (!reading.attackBonus.has_value() && !reading.save.has_value() &&
        std::regex_search(effect, std::regex("\\bor one creature|one creature it can see|another creature|targets itself or"))) {
        Benefit benefit;
        const std::string dice = "\\d+ \\((\\d+d\\d+(?:\\s*[+\\-]\\s*\\d+)?)\\)";
        const auto compact = [](std::string text) {
            std::erase(text, ' ');
            return text;
        };
        if (std::regex_search(effect, m, std::regex("gains? " + dice + " Temporary Hit Points"))) {
            benefit.tempHp = compact(m[1].str());
        }
        if (std::regex_search(effect, m, std::regex("(?:target|creature) (?:magically )?regains " + dice + " Hit Points"))) {
            benefit.healing = compact(m[1].str());
        }
        benefit.advantageOnAttacks = effect.find("has Advantage on attack rolls until") != std::string::npos;
        if (std::regex_search(effect, m, std::regex("AC increases by (\\d+)"))) {
            benefit.acBonus = std::stoi(m[1].str());
        }
        if (std::regex_search(effect, m, std::regex("until the (start|end) of the [a-z ]+?(?:\xE2\x80\x99|')s next turn"))) {
            benefit.until = m[1].str() == "start" ? kUntilSourceStart : kUntilSourceEnd;
        }
        if (!benefit.tempHp.empty() || !benefit.healing.empty() || benefit.advantageOnAttacks || benefit.acBonus != 0) {
            reading.benefit = benefit;
        }
    }
    return reading;
}

void applyEffects(MonsterAttack& attack, const EntryReading& reading)
{
    attack.targetCondition = reading.targetCondition;
    attack.targetMaxSize = reading.targetMaxSize;
    attack.failureHpThreshold = reading.failureHpThreshold;
    attack.failureHpEffect = reading.failureHpEffect;
    attack.riders = reading.riders;
    attack.riderSave = reading.riderSave;
    attack.drain = reading.drain;
    attack.advantageIfGrappled = reading.advantageIfGrappled;
    attack.benefit = reading.benefit;
}

void applyReading(MonsterAttack& attack, const EntryReading& reading)
{
    attack.attackBonus = reading.attackBonus;
    attack.save = reading.save;
    attack.damage = reading.damage;
    attack.area = reading.area;
    attack.recharge = reading.recharge;
    attack.perDay = reading.perDay;
    applyEffects(attack, reading);
}

bool effectsMatch(const MonsterAttack& attack, const EntryReading& reading)
{
    return attack.targetCondition == reading.targetCondition && attack.targetMaxSize == reading.targetMaxSize &&
           attack.failureHpThreshold == reading.failureHpThreshold &&
           attack.failureHpEffect == reading.failureHpEffect && attack.riders == reading.riders &&
           attack.riderSave == reading.riderSave && attack.drain == reading.drain &&
           attack.advantageIfGrappled == reading.advantageIfGrappled && attack.benefit == reading.benefit;
}

std::optional<AttackModifier> readAttackModifier(const std::string& name, const std::string& effect)
{
    std::smatch advantage;
    const bool hasAdvantage =
        std::regex_search(effect, advantage, std::regex("\\bAdvantage on (?:an |the )?(melee )?attack rolls?"));
    std::smatch disadvantage;
    const bool hasDisadvantage = std::regex_search(
        effect, disadvantage, std::regex("\\bDisadvantage on (?:ability checks and )?(melee )?attack rolls?"));
    AttackModifier modifier;
    std::smatch m;
    if (name.rfind("Pack Tactics", 0) == 0 && hasAdvantage) {
        modifier.ask = "an ally is within 5 feet of the target and isn't Incapacitated";
        return modifier;
    }
    if (hasAdvantage && effect.find("Bloodied") != std::string::npos) {
        modifier.when = kModifierWhileBloodied;
        modifier.meleeOnly = advantage[1].matched;
        return modifier;
    }
    if (hasAdvantage && (effect.find("doesn\xE2\x80\x99t have all its Hit Points") != std::string::npos ||
                         effect.find("doesn't have all its Hit Points") != std::string::npos)) {
        modifier.when = kModifierTargetHurt;
        return modifier;
    }
    if (hasAdvantage && effect.find("its allies have Advantage on attack rolls") != std::string::npos) {
        modifier.alliesToo = true;
        modifier.whileActive = effect.find("Incapacitated") != std::string::npos;
        modifier.ask = std::regex_search(effect, m, std::regex("(\\d+)-foot Emanation originating from the ([a-z ]+?)[,.]"))
                           ? "the attacker is within " + m[1].str() + " feet of the " + m[2].str()
                           : std::string("the attacker is within its aura");
        return modifier;
    }
    if (hasDisadvantage) {
        modifier.advantage = false;
        if (std::regex_search(effect, m, std::regex("takes ([A-Z][a-z]+) damage, it has Disadvantage on attack rolls"))) {
            modifier.when = kModifierAfterDamage;
            modifier.damageType = asciiLower(m[1].str());
            return modifier;
        }
        if (std::regex_search(effect, std::regex("in sunlight", std::regex::icase))) {
            modifier.ask = "it is in sunlight";
            modifier.sticky = true;
            return modifier;
        }
        if (effect.find("in Bright Light") != std::string::npos) {
            modifier.ask = "it is in Bright Light";
            modifier.sticky = true;
            return modifier;
        }
    }
    return std::nullopt;
}

bool readsAfterDamagingBloodied(const std::string& effect)
{
    return std::regex_search(effect, std::regex("after dealing damage to a creature that (?:was|is) already Bloodied"));
}

std::optional<MonsterAttack> targetedFromText(const std::string& name, const std::string& effect)
{
    const EntryReading reading = readEntry(name, effect);
    if (!reading.attackBonus.has_value() && !reading.save.has_value() && !reading.benefit.has_value()) {
        return std::nullopt;
    }
    MonsterAttack attack;
    attack.name = name;
    attack.effect = effect;
    applyReading(attack, reading);
    attack.recharge.reset();  // the feature's own
    attack.perDay.reset();
    return attack;
}

}  // namespace combat
