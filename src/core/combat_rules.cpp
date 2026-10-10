#include "core/combat_rules.h"

#include "core/text.h"

#include <array>
#include <cctype>
#include <algorithm>
#include <regex>
#include <optional>
#include <limits>

namespace combat {

namespace {

int clampToInt(long long value)
{
    if (value > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    if (value < std::numeric_limits<int>::min()) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(value);
}

bool contains(const std::vector<std::string>& list, const std::string& value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

constexpr const char* kUnconscious = "unconscious";

void eraseCondition(Combatant& combatant, const std::string& id)
{
    combatant.conditions.erase(std::remove_if(combatant.conditions.begin(), combatant.conditions.end(),
                                              [&id](const ActiveCondition& row) { return row.id == id; }),
                               combatant.conditions.end());
}

// Paralyzed: "You have the Incapacitated condition." Same cause and duration,
// and tied to Paralyzed, so releaseConditions ends it when Paralyzed does.
// An Incapacitated condition already on the creature is left alone.
void grantParalyzedIncapacitated(Combatant& combatant, const ActiveCondition& paralyzed)
{
    if (paralyzed.id != "paralyzed" || hasCondition(combatant, "incapacitated") ||
        contains(combatant.conditionImmunities, "incapacitated")) {
        return;
    }
    ActiveCondition included;
    included.id = "incapacitated";
    included.duration = paralyzed.duration;
    included.source = paralyzed.source;
    included.byId = paralyzed.byId;
    included.tiedTo = paralyzed.id;
    combatant.conditions.push_back(std::move(included));
}

void becomeDying(Combatant& combatant)
{
    combatant.stable = false;
    combatant.deathSaves = {};
    if (!hasCondition(combatant, kUnconscious)) {
        combatant.conditions.push_back(ActiveCondition{kUnconscious, std::nullopt, std::nullopt});
    }
}

void comeBack(Combatant& combatant)
{
    combatant.dead = false;
    combatant.stable = false;
    combatant.deathSaves = {};
    eraseCondition(combatant, kUnconscious);
}

void die(Combatant& combatant)
{
    combatant.dead = true;
    combatant.stable = false;
    combatant.hp = 0;
    endConcentration(combatant);
    // The dead have no conditions.
    combatant.conditions.clear();
}

std::string baseActionName(const std::string& name)
{
    const auto paren = name.find(" (");
    return paren == std::string::npos ? name : name.substr(0, paren);
}

bool sameAction(const std::string& left, const std::string& right)
{
    return equalsInsensitive(baseActionName(left), baseActionName(right));
}

int usesLeft(const Combatant& combatant, const std::string& name, std::optional<int> perDay)
{
    const auto found = combatant.usesRemaining.find(name);
    if (found != combatant.usesRemaining.end()) {
        return found->second;
    }
    return perDay.value_or(0);
}

int usesLeft(const Combatant& combatant, const MonsterAttack& attack)
{
    return usesLeft(combatant, attack.name, attack.perDay);
}

}  // namespace

// --- Damage and healing -----------------------------------------------------

int damageAfterDefenses(int amount, const std::string& type, const Defenses& defenses)
{
    if (amount <= 0) {
        return 0;
    }
    if (type.empty()) {
        return amount;
    }
    if (contains(defenses.immunities, type)) {
        return 0;
    }
    long long result = amount;
    if (contains(defenses.resistances, type)) {
        result /= 2;
    }
    if (contains(defenses.vulnerabilities, type)) {
        result *= 2;
    }
    return clampToInt(result);
}

DamageResult applyDamage(Combatant& combatant, const std::vector<TypedDamage>& parts, bool critical)
{
    DamageResult result;
    if (combatant.dead) {
        return result;
    }
    long long rolled = 0;
    long long taken = 0;
    for (const TypedDamage& part : parts) {
        const int amount = std::max(0, part.amount);
        const int adjusted = damageAfterDefenses(amount, part.type, combatant.defenses);
        rolled += amount;
        taken += adjusted;
        if (adjusted > 0 && !part.type.empty() && !contains(result.typesTaken, part.type)) {
            result.typesTaken.push_back(part.type);
        }
        if (amount > 0 && adjusted != amount) {
            if (adjusted == 0) {
                result.notes.push_back(part.type + " immune");
            } else if (adjusted < amount) {
                result.notes.push_back(part.type + " resisted");
            } else {
                result.notes.push_back(part.type + " vulnerable");
            }
        }
    }
    result.rolled = clampToInt(rolled);
    result.taken = clampToInt(taken);
    if (result.taken == 0) {
        return result;
    }

    long long remaining = result.taken;
    if (combatant.tempHp > 0) {
        const long long spent = std::min<long long>(combatant.tempHp, remaining);
        combatant.tempHp = clampToInt(combatant.tempHp - spent);
        result.tempSpent = clampToInt(spent);
        remaining -= spent;
    }

    const bool character = isCharacterCombatant(combatant);
    if (combatant.hp > 0) {
        if (remaining > 0) {
            const long long lost = std::min<long long>(combatant.hp, remaining);
            combatant.hp = clampToInt(combatant.hp - lost);
            result.hpLost = clampToInt(lost);
            const long long leftover = remaining - lost;
            if (combatant.hp == 0) {
                result.droppedToZero = true;
                if (!combatant.concentration.empty()) {
                    endConcentration(combatant);
                    result.concentrationEnded = true;
                }
                if (character) {
                    if (combatant.maxHp.has_value() && *combatant.maxHp > 0 && leftover >= *combatant.maxHp) {
                        die(combatant);
                        result.died = true;
                        result.instantDeath = true;
                    } else {
                        becomeDying(combatant);
                    }
                } else {
                    result.died = true;
                    combatant.conditions.clear();  // a monster at 0 HP is dead
                }
            }
        }
    } else if (remaining > 0 && character) {
        if (combatant.maxHp.has_value() && *combatant.maxHp > 0 && remaining >= *combatant.maxHp) {
            die(combatant);
            result.died = true;
            result.instantDeath = true;
        } else {
            const int failures = critical ? 2 : 1;
            combatant.stable = false;
            combatant.deathSaves.failures = std::min(3, combatant.deathSaves.failures + failures);
            result.deathSaveFailures = failures;
            if (combatant.deathSaves.failures >= 3) {
                die(combatant);
                result.died = true;
            }
        }
    }

    if (!combatant.concentration.empty() && !result.droppedToZero && !combatant.dead) {
        result.concentrationDc = concentrationDc(result.taken);
    }
    return result;
}

DamageResult applyDamage(Combatant& combatant, int amount, const std::string& type, bool critical)
{
    return applyDamage(combatant, std::vector<TypedDamage>{TypedDamage{amount, type}}, critical);
}

HealingResult applyHealing(Combatant& combatant, int amount)
{
    HealingResult result;
    if (combatant.dead || amount <= 0) {
        return result;
    }
    const int before = combatant.hp;
    long long next = static_cast<long long>(combatant.hp) + amount;
    if (combatant.maxHp.has_value()) {
        next = std::min<long long>(next, *combatant.maxHp);
    }
    combatant.hp = clampToInt(next);
    result.healed = std::max(0, combatant.hp - before);
    if (before == 0 && combatant.hp > 0) {
        comeBack(combatant);
        result.revived = true;
    }
    return result;
}

void setHitPoints(Combatant& combatant, int current)
{
    const int next = cappedHitPoints(std::max(0, current), combatant.maxHp);
    if (next > 0 && (combatant.hp == 0 || combatant.dead)) {
        comeBack(combatant);
    } else if (next == 0 && combatant.hp > 0 && isCharacterCombatant(combatant) && !combatant.dead) {
        endConcentration(combatant);
        becomeDying(combatant);
    } else if (next == 0 && combatant.hp > 0) {
        endConcentration(combatant);
        combatant.conditions.clear();  // a monster at 0 HP is dead
    }
    combatant.hp = next;
}

void setTemporaryHitPoints(Combatant& combatant, int temporary)
{
    combatant.tempHp = std::max(0, temporary);
}

int cappedHitPoints(int current, std::optional<int> maximum)
{
    if (!maximum.has_value() || current <= *maximum) {
        return current;
    }
    return *maximum;
}

// --- Death saves --------------------------------------------------------------

DeathSaveResult rollDeathSave(Combatant& combatant, int face)
{
    if (!isDying(combatant)) {
        return DeathSaveResult::NotDying;
    }
    if (face >= 20) {
        combatant.hp = cappedHitPoints(1, combatant.maxHp);
        comeBack(combatant);
        return DeathSaveResult::Revived;
    }
    if (face <= 1) {
        combatant.deathSaves.failures = std::min(3, combatant.deathSaves.failures + 2);
        if (combatant.deathSaves.failures >= 3) {
            die(combatant);
            return DeathSaveResult::Died;
        }
        return DeathSaveResult::DoubleFailure;
    }
    if (face - d20Penalty(combatant) >= 10) {
        combatant.deathSaves.successes = std::min(3, combatant.deathSaves.successes + 1);
        if (combatant.deathSaves.successes >= 3) {
            combatant.stable = true;
            combatant.deathSaves = {};
            return DeathSaveResult::Stabilized;
        }
        return DeathSaveResult::Success;
    }
    combatant.deathSaves.failures = std::min(3, combatant.deathSaves.failures + 1);
    if (combatant.deathSaves.failures >= 3) {
        die(combatant);
        return DeathSaveResult::Died;
    }
    return DeathSaveResult::Failure;
}

void adjustDeathSave(Combatant& combatant, bool success, int delta)
{
    int& field = success ? combatant.deathSaves.successes : combatant.deathSaves.failures;
    field = std::clamp(clampToInt(static_cast<long long>(field) + delta), 0, 3);
    if (!isDying(combatant)) {
        return;
    }
    if (combatant.deathSaves.failures >= 3) {
        die(combatant);
    } else if (combatant.deathSaves.successes >= 3) {
        combatant.stable = true;
        combatant.deathSaves = {};
    }
}

bool stabilize(Combatant& combatant)
{
    if (!isDying(combatant)) {
        return false;
    }
    combatant.stable = true;
    combatant.deathSaves = {};
    return true;
}

// --- Exhaustion ---------------------------------------------------------------

int d20Penalty(const Combatant& combatant)
{
    return 2 * std::clamp(combatant.exhaustion, 0, 6);
}

int exhaustionSpeedPenalty(int level)
{
    return 5 * std::clamp(level, 0, 6);
}

void killOutright(Combatant& combatant)
{
    if (!combatant.dead) {
        die(combatant);
    }
}

std::string creatureSize(const Combatant& combatant, const std::string& species)
{
    if (isMonsterCombatant(combatant)) {
        return combatant.statBlock.has_value() ? combatant.statBlock->size : std::string();
    }
    const std::string lower = asciiLower(species);
    return lower.find("gnome") != std::string::npos || lower.find("halfling") != std::string::npos ? "Small"
                                                                                                   : "Medium";
}

namespace {

int sizeRank(const std::string& size)
{
    static const std::array<const char*, 6> sizes{{"tiny", "small", "medium", "large", "huge", "gargantuan"}};
    const std::string lower = asciiLower(trim(size));
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        if (lower.rfind(sizes[i], 0) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

namespace {

std::string capitalized(std::string word)
{
    if (!word.empty()) {
        word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
    }
    return word;
}

}  // namespace

bool isGrappledBy(const Combatant& target, const std::string& grapplerId)
{
    for (const ActiveCondition& condition : target.conditions) {
        if (condition.id == "grappled" && (condition.byId.empty() || condition.byId == grapplerId)) {
            return true;
        }
    }
    return false;
}

std::string charmedProblem(const Combatant& actor, const Combatant& target)
{
    for (const ActiveCondition& condition : actor.conditions) {
        if (condition.id == "charmed" && !condition.byId.empty() && condition.byId == target.id) {
            return actor.name + " is Charmed by " + target.name + " and can't attack it or target it with "
                   "harmful abilities or magic.";
        }
    }
    return {};
}

std::string targetRequirementProblem(const MonsterAttack& attack, const Combatant& target, const std::string& species,
                                     const std::string& attackerId)
{
    const std::string name = baseActionName(attack.name);
    if (!attack.targetCondition.empty()) {
        // "charmed,grappled": either will do. Grappled must be by this monster
        // when the app knows who is grappling.
        std::vector<std::string> wanted;
        std::string words;
        std::size_t from = 0;
        while (from <= attack.targetCondition.size()) {
            const std::size_t comma = attack.targetCondition.find(',', from);
            const std::string id =
                trim(attack.targetCondition.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
            if (!id.empty()) {
                wanted.push_back(id);
                words += (words.empty() ? "" : " or ") + capitalized(id);
            }
            if (comma == std::string::npos) {
                break;
            }
            from = comma + 1;
        }
        bool ok = false;
        bool grappledByOther = false;
        for (const std::string& id : wanted) {
            if (id == "grappled" && !attackerId.empty()) {
                if (isGrappledBy(target, attackerId)) {
                    ok = true;
                } else if (hasCondition(target, id)) {
                    grappledByOther = true;
                }
            } else if (hasCondition(target, id)) {
                ok = true;
            }
        }
        if (!ok) {
            if (grappledByOther) {
                return name + " needs a creature this monster is grappling, and someone else is grappling " +
                       target.name + ".";
            }
            return name + " needs a " + words + " creature, and " + target.name + " is not " + words + ".";
        }
    }
    if (!attack.targetMaxSize.empty()) {
        const int limit = sizeRank(attack.targetMaxSize);
        const int size = sizeRank(creatureSize(target, species));
        if (limit >= 0 && size > limit) {
            return name + " needs a " + attack.targetMaxSize + " or smaller creature, and " + target.name + " is " +
                   creatureSize(target, species) + ".";
        }
    }
    if (!attack.targetTypes.empty()) {
        const std::string type = creatureTypeKey(target);
        if (std::find(attack.targetTypes.begin(), attack.targetTypes.end(), type) == attack.targetTypes.end()) {
            std::string words;
            for (const std::string& wanted : attack.targetTypes) {
                words += (words.empty() ? "" : " or ") + capitalized(wanted);
            }
            return name + " needs a " + words + ", and " + target.name + " is " +
                   (type.empty() ? std::string("not one") : (std::string("aeiou").find(type[0]) != std::string::npos ? "an " : "a ") + capitalized(type)) +
                   ".";
        }
    }
    if (!attack.targetExceptTypes.empty()) {
        const std::string type = creatureTypeKey(target);
        if (std::find(attack.targetExceptTypes.begin(), attack.targetExceptTypes.end(), type) !=
            attack.targetExceptTypes.end()) {
            std::string never;
            for (const std::string& except : attack.targetExceptTypes) {
                never += (never.empty() ? "" : " or ") + capitalized(except);
            }
            return name + " doesn't affect " + never + ", and " + target.name + " is " +
                   (std::string("aeiou").find(type[0]) != std::string::npos ? "an " : "a ") + capitalized(type) + ".";
        }
    }
    if (attack.targetAtZeroHp && (target.hp > 0 || target.dead)) {
        return name + " needs a creature with 0 Hit Points, and " + target.name + " has " + std::to_string(target.hp) +
               ".";
    }
    return {};
}

// --- Conditions an action gives its target ----------------------------------------

std::string riderBlocked(const ConditionRider& rider, const Combatant& target, const std::string& species)
{
    if (!rider.targetMaxSize.empty()) {
        const int limit = sizeRank(rider.targetMaxSize);
        const int size = sizeRank(creatureSize(target, species));
        if (limit >= 0 && size > limit) {
            return target.name + " is " + creatureSize(target, species) + ", larger than " + rider.targetMaxSize;
        }
    }
    if (rider.targetMaxHp.has_value() && target.hp > *rider.targetMaxHp) {
        return target.name + " has more than " + std::to_string(*rider.targetMaxHp) + " Hit Points";
    }
    const std::string type = creatureTypeKey(target);
    const std::string kind = asciiLower(species);
    for (const std::string& except : rider.exceptTypes) {
        if (except == type) {
            return target.name + " is " + (std::string("aeiou").find(except[0]) != std::string::npos ? "an " : "a ") +
                   capitalized(except);
        }
        if (!except.empty() && kind.find(except) != std::string::npos) {
            return target.name + " is " + (std::string("aeiou").find(except[0]) != std::string::npos ? "an " : "a ") +
                   except;
        }
    }
    return {};
}

std::vector<const ConditionRider*> ridersFor(const MonsterAttack& attack, const std::string& on)
{
    std::vector<const ConditionRider*> found;
    for (const ConditionRider& rider : attack.riders) {
        if (rider.on == on) {
            found.push_back(&rider);
        }
    }
    return found;
}

std::string riderSourceName(const Combatant& attacker, const MonsterAttack& attack)
{
    return attacker.name + "'s " + baseActionName(attack.name);
}

RiderOutcome applyRider(Encounter& encounter, const Combatant& attacker, Combatant& target,
                        const MonsterAttack& attack, const ConditionRider& rider, std::optional<bool> wasUnconscious)
{
    RiderOutcome outcome;
    // Nothing more lands on a creature the hit killed.
    if (target.dead || (isMonsterCombatant(target) && target.hp <= 0)) {
        return outcome;
    }
    // "Swallowed, and no longer Grappled."
    for (const std::string& id : rider.removes) {
        for (auto it = target.conditions.begin(); it != target.conditions.end();) {
            if (it->id == id && (it->byId.empty() || it->byId == attacker.id)) {
                outcome.removed.push_back(it->id);
                it = target.conditions.erase(it);
            } else {
                ++it;
            }
        }
    }
    // A condition that comes through sight or hearing does not land on a
    // creature that cannot see or hear (Unconscious, or Blinded / Deafened for
    // the matching sense). Stat blocks say it as "can see" / "can hear".
    {
        const std::string text = asciiLower(attack.effect);
        const auto mentions = [&text](std::initializer_list<const char*> words) {
            return std::any_of(words.begin(), words.end(),
                               [&text](const char* word) { return text.find(word) != std::string::npos; });
        };
        const bool sight = mentions({"can see", "that see", "who see", "looks at", "look at"});
        const bool hearing = mentions({"can hear", "that hear", "who hear"});
        const bool unaware = wasUnconscious.value_or(hasCondition(target, "unconscious")) ||
                             ((sight && !hearing && !rider.requiresSenses) && hasCondition(target, "blinded")) ||
                             ((hearing && !sight && !rider.requiresSenses) && hasCondition(target, "deafened"));
        if ((rider.requiresSenses || sight || hearing) && unaware) {
            outcome.unaware = rider.conditions;
            return outcome;
        }
    }
    const std::optional<SaveSpec> save = attack.attackBonus.has_value() && attack.riderSave.has_value() ? attack.riderSave
                                                                                                    : attack.save.has_value() ? attack.save
                                                                                                                              : attack.repeatSave;
    std::string source = riderSourceName(attacker, attack);
    if (rider.escapeDc.has_value()) {
        source += ", escape DC " + std::to_string(*rider.escapeDc);
    }
    for (std::size_t i = 0; i < rider.conditions.size(); ++i) {
        ActiveCondition condition;
        condition.id = rider.conditions[i];
        condition.source = source;
        condition.byId = attacker.id;
        condition.tiedTo = rider.tiedTo;
        if (condition.id == "grappled") {
            condition.escapeDc = rider.escapeDc;
        }
        condition.endsOn = rider.endsOn;
        condition.concentration = rider.concentration;
        if (rider.until == kUntilSourceStart) {
            condition.duration = makeDuration(encounter, attacker.id, TurnBoundary::Start, 1);
        } else if (rider.until == kUntilSourceEnd) {
            condition.duration = makeDuration(encounter, attacker.id, TurnBoundary::End, 1);
        } else if (rider.until == kUntilTargetStart) {
            condition.duration = makeDuration(encounter, target.id, TurnBoundary::Start, 1);
        } else if (rider.until == kUntilTargetEnd) {
            condition.duration = makeDuration(encounter, target.id, TurnBoundary::End, 1);
        } else if (rider.until == kUntilTargetThisTurn) {
            condition.duration = ConditionDuration{target.id, TurnBoundary::End, 1, false};
        } else if (rider.until == kUntilMinute) {
            condition.duration = makeDuration(encounter, attacker.id, TurnBoundary::Start, 10);
        }
        if ((rider.saveEnds || rider.saveOnDemand) && save.has_value()) {
            SaveEnds ends{save->ability, save->dc};
            ends.manual = !rider.saveEnds;
            ends.atStart = rider.saveAtStart;
            ends.failDamage = rider.saveFailDamage;
            ends.worsensTo = rider.worsensTo;
            ends.worseSaveEnds = rider.worseSaveEnds;
            ends.worseEndsOn = rider.worseEndsOn;
            condition.saveEnds = std::move(ends);
        }
        if (i == 0) {
            // Once per turn, not once per condition it gives.
            condition.ongoing = rider.ongoing;
            condition.ongoingAt = rider.ongoingAt;
        }
        const bool hadIncapacitated = hasCondition(target, "incapacitated");
        switch (addCondition(target, std::move(condition))) {
        case AddConditionResult::Added:
            outcome.added.push_back(rider.conditions[i]);
            if (rider.conditions[i] == "paralyzed" && !hadIncapacitated && hasCondition(target, "incapacitated")) {
                outcome.added.push_back("incapacitated");
            }
            break;
        case AddConditionResult::Immune:
            outcome.immune.push_back(rider.conditions[i]);
            break;
        case AddConditionResult::Duplicate:
            outcome.already.push_back(rider.conditions[i]);
            break;
        case AddConditionResult::Empty:
            break;
        }
    }
    if (rider.stabilize && isDying(target)) {
        outcome.stabilized = stabilize(target);
    }
    if (rider.concentrationDisadvantage) {
        const std::string until = rider.until.empty() ? std::string(kUntilTargetEnd) : rider.until;
        ConditionDuration duration;
        if (until == kUntilSourceStart) {
            duration = makeDuration(encounter, attacker.id, TurnBoundary::Start, 1);
        } else if (until == kUntilSourceEnd) {
            duration = makeDuration(encounter, attacker.id, TurnBoundary::End, 1);
        } else if (until == kUntilTargetStart) {
            duration = makeDuration(encounter, target.id, TurnBoundary::Start, 1);
        } else if (until == kUntilTargetThisTurn) {
            duration = ConditionDuration{target.id, TurnBoundary::End, 1, false};
        } else if (until == kUntilMinute) {
            duration = makeDuration(encounter, attacker.id, TurnBoundary::Start, 10);
        } else {
            duration = makeDuration(encounter, target.id, TurnBoundary::End, 1);
        }
        const std::string key = std::string(kTimedConcentrationDisadvantagePrefix) + baseActionName(attack.name);
        std::erase_if(target.timedEffects, [&key](const auto& effect) { return effect.first == key; });
        target.timedEffects.emplace_back(key, duration);
        outcome.concentrationDisadvantage = true;
    }
    return outcome;
}

std::vector<std::string> worsenCondition(Combatant& combatant, const std::string& conditionId)
{
    std::vector<std::string> added;
    const auto it = std::find_if(combatant.conditions.begin(), combatant.conditions.end(),
                                 [&conditionId](const ActiveCondition& row) { return row.id == conditionId; });
    if (it == combatant.conditions.end() || !it->saveEnds.has_value() || it->saveEnds->worsensTo.empty()) {
        return added;
    }
    const ActiveCondition old = *it;
    combatant.conditions.erase(it);
    for (const std::string& id : old.saveEnds->worsensTo) {
        ActiveCondition worse;
        worse.id = id;
        worse.source = old.source;
        worse.byId = old.byId;
        worse.endsOn = old.saveEnds->worseEndsOn;
        worse.concentration = old.concentration;
        if (old.saveEnds->worseSaveEnds) {
            worse.saveEnds = SaveEnds{old.saveEnds->ability, old.saveEnds->dc};
        }
        if (addCondition(combatant, std::move(worse)) == AddConditionResult::Added) {
            added.push_back(id);
        }
    }
    return added;
}

std::vector<ReleasedCondition> releaseConditions(Encounter& encounter)
{
    std::vector<ReleasedCondition> released;
    const auto sourceGone = [&encounter](const std::string& id, bool incapacitatedCounts) {
        for (const Combatant& other : encounter.combatants) {
            if (other.id == id) {
                if (other.dead || (isMonsterCombatant(other) && !isInInitiative(other))) {
                    return true;
                }
                return incapacitatedCounts && isIncapacitated(other);
            }
        }
        return true;  // removed from the fight
    };
    for (Combatant& combatant : encounter.combatants) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto it = combatant.conditions.begin(); it != combatant.conditions.end(); ++it) {
                bool ends = false;
                if (!it->byId.empty()) {
                    // A grapple ends when the grappler is Incapacitated, dies, or
                    // leaves; "until the aboleth dies" when it dies or leaves.
                    if (it->id == "grappled" && sourceGone(it->byId, true)) {
                        ends = true;
                    } else if (contains(it->endsOn, kEndsOnSourceGone) && sourceGone(it->byId, false)) {
                        ends = true;
                    }
                }
                if (!ends && !it->tiedTo.empty()) {
                    const std::string tiedTo = it->tiedTo;
                    const std::string byId = it->byId;
                    ends = std::none_of(combatant.conditions.begin(), combatant.conditions.end(),
                                        [&tiedTo, &byId](const ActiveCondition& other) {
                                            return other.id == tiedTo && other.byId == byId;
                                        });
                }
                // Invisibility, Hold Person, and the like: the condition lasts
                // while the creature who caused it is concentrating on that spell.
                if (!ends && !it->concentration.empty()) {
                    const std::string spellId = it->concentration;
                    const std::string casterId = it->byId.empty() ? combatant.id : it->byId;
                    const Combatant* caster = nullptr;
                    for (const Combatant& other : encounter.combatants) {
                        if (other.id == casterId) {
                            caster = &other;
                            break;
                        }
                    }
                    if (caster == nullptr || caster->concentration != spellId) {
                        ends = true;
                    }
                }
                if (ends) {
                    released.push_back(ReleasedCondition{combatant.id, *it});
                    combatant.conditions.erase(it);
                    changed = true;
                    break;
                }
            }
        }
    }
    return released;
}

namespace {

std::string joinWords(const std::vector<std::string>& words, const std::string& last = " and ")
{
    std::string out;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i > 0) {
            out += i + 1 == words.size() ? last : ", ";
        }
        out += words[i];
    }
    return out;
}

std::vector<std::string> conditionWords(const std::vector<std::string>& ids)
{
    std::vector<std::string> words;
    for (const std::string& id : ids) {
        words.push_back(capitalized(id));
    }
    return words;
}

}  // namespace

std::string describeRider(const ConditionRider& rider)
{
    std::string when = "On a hit";
    if (rider.on == kRiderOnFailure) {
        when = "On a failed save";
    } else if (rider.on == kRiderOnFailureBy5) {
        when = "Failing by 5 or more";
    } else if (rider.on == kRiderOnZeroHp) {
        when = "If the hit drops it to 0 HP";
    } else if (rider.on == kRiderOnCast) {
        when = "When cast";
    }
    std::vector<std::string> notes;
    if (rider.escapeDc.has_value()) {
        notes.push_back("escape DC " + std::to_string(*rider.escapeDc));
    }
    if (!rider.targetMaxSize.empty()) {
        notes.push_back(rider.targetMaxSize + " or smaller");
    }
    if (rider.targetMaxHp.has_value()) {
        notes.push_back(std::to_string(*rider.targetMaxHp) + " HP or fewer");
    }
    if (!rider.exceptTypes.empty()) {
        notes.push_back("not " + joinWords(rider.exceptTypes, " or "));
    }
    if (rider.ask == "@advantage") {
        notes.push_back("if the attack had Advantage");
    } else if (!rider.ask.empty()) {
        notes.push_back("if " + rider.ask);
    }
    if (rider.until == kUntilSourceStart) {
        notes.push_back("until the start of the monster's next turn");
    } else if (rider.until == kUntilSourceEnd) {
        notes.push_back("until the end of the monster's next turn");
    } else if (rider.until == kUntilTargetStart) {
        notes.push_back("until the start of its next turn");
    } else if (rider.until == kUntilTargetEnd) {
        notes.push_back("until the end of its next turn");
    } else if (rider.until == kUntilTargetThisTurn) {
        notes.push_back("until the end of its turn");
    } else if (rider.until == kUntilMinute) {
        notes.push_back("for 1 minute");
    }
    if (!rider.tiedTo.empty()) {
        notes.push_back("while " + capitalized(rider.tiedTo));
    }
    if (rider.saveEnds) {
        std::string text = rider.worsensTo.empty() ? "a save at the end of each of its turns ends it"
                                                   : "it saves again at the end of its next turn: a success ends it, "
                                                     "a failure makes it " + joinWords(conditionWords(rider.worsensTo));
        notes.push_back(text);
    }
    for (const std::string& event : rider.endsOn) {
        if (event == kEndsOnTakesDamage) {
            notes.push_back("ends if it takes damage");
        } else if (event == kEndsOnSourceGone) {
            notes.push_back("ends if the monster dies");
        }
    }
    if (!rider.ongoing.empty()) {
        std::vector<std::string> parts;
        for (const DamagePart& part : rider.ongoing) {
            parts.push_back(part.dice + (part.type.empty() ? "" : " " + part.type));
        }
        notes.push_back(joinWords(parts) + " damage at the start of each of " +
                        (rider.ongoingAt == kOngoingAtSource ? "the monster's" : "its") + " turns");
    }
    if (!rider.removes.empty()) {
        notes.push_back("no longer " + joinWords(conditionWords(rider.removes), " or "));
    }
    if (rider.stabilize) {
        notes.push_back("and Stable");
    }
    if (rider.refundDamage) {
        notes.push_back("instead of the damage");
    }
    std::string effect = joinWords(conditionWords(rider.conditions));
    if (rider.concentrationDisadvantage) {
        const std::string disadvantage = "Disadvantage on saving throws to maintain Concentration";
        effect = effect.empty() ? disadvantage : effect + " and " + disadvantage;
    }
    std::string text = when + ": " + effect;
    if (!notes.empty()) {
        text += " (" + joinWords(notes, "; ") + ")";
    }
    return text + ".";
}

std::vector<std::string> describeActionRules(const MonsterAttack& attack)
{
    std::vector<std::string> lines;
    if (!attack.targetCondition.empty()) {
        std::vector<std::string> ids;
        std::size_t from = 0;
        while (true) {
            const std::size_t comma = attack.targetCondition.find(',', from);
            ids.push_back(trim(attack.targetCondition.substr(from, comma == std::string::npos ? std::string::npos
                                                                                               : comma - from)));
            if (comma == std::string::npos) {
                break;
            }
            from = comma + 1;
        }
        lines.push_back("Only a " + joinWords(conditionWords(ids), " or ") + " target.");
    }
    if (!attack.targetMaxSize.empty()) {
        lines.push_back("Only a " + attack.targetMaxSize + " or smaller target.");
    }
    if (attack.advantageIfGrappled) {
        lines.push_back("Advantage against a creature it is grappling.");
    }
    if (attack.targetAtZeroHp) {
        lines.push_back("Only a target with 0 Hit Points.");
    }
    if (attack.failureHpThreshold.has_value() && !attack.targetAtZeroHp) {
        const std::string amount = std::to_string(*attack.failureHpThreshold);
        if (attack.failureHpEffect == "dies") {
            lines.push_back(std::string(attack.save.has_value() ? "On a failure, a target with " : "A target with ") +
                            amount + " HP or fewer dies.");
        } else if (attack.failureHpEffect == "dropsToZero") {
            lines.push_back("On a failure, a target with " + amount + " HP or fewer drops to 0.");
        } else if (!attack.failureHpEffect.empty()) {
            lines.push_back("On a failure, a target with " + amount + " HP or fewer drops to 0.");
        } else {
            lines.push_back("Only a target with " + amount + " HP or fewer.");
        }
    } else if (attack.failureHpThreshold.has_value()) {
        lines.push_back(attack.failureHpEffect == "dies" ? "On a failure, the target dies." : "On a failure, the target drops to 0.");
    }
    if (!attack.failureSelfHealing.empty()) {
        lines.push_back("On a failure, the monster regains " + attack.failureSelfHealing + " Hit Points.");
    }
    if (attack.riderSave.has_value()) {
        lines.push_back("A hit makes the target roll a DC " + std::to_string(attack.riderSave->dc) + " " +
                        abilityLabel(attack.riderSave->ability) + " save.");
    }
    if (attack.benefit.has_value()) {
        const Benefit& benefit = *attack.benefit;
        std::vector<std::string> what;
        if (!benefit.tempHp.empty()) {
            what.push_back(benefit.tempHp + " Temporary Hit Points");
        }
        if (!benefit.healing.empty()) {
            what.push_back(benefit.healing + " healing");
        }
        if (benefit.advantageOnAttacks) {
            what.push_back("Advantage on attack rolls");
        }
        if (benefit.acBonus != 0) {
            what.push_back("+" + std::to_string(benefit.acBonus) + " AC");
        }
        std::string line = (attack.selfOnly ? "On itself: " : "Helps the creature you pick (itself too): ") +
                           joinWords(what);
        if (benefit.advantageOnAttacks || benefit.acBonus != 0) {
            line += benefit.until == kUntilSourceEnd ? " until the end of the monster's next turn"
                                                     : " until the start of the monster's next turn";
        }
        lines.push_back(line + ".");
    }
    for (const ConditionRider& rider : attack.riders) {
        lines.push_back(describeRider(rider));
    }
    if (attack.halfDamageOnMiss) {
        lines.push_back("Half damage on a miss.");
    }
    if (attack.strikes > 1) {
        lines.push_back(std::to_string(attack.strikes) + " strikes.");
    }
    if (!attack.concentration.empty()) {
        lines.push_back("Concentration.");
    }
    if (attack.drain.has_value()) {
        lines.push_back(std::string("Its Hit Point maximum drops by the ") +
                        (attack.drain->type.empty() ? "" : attack.drain->type + " ") + "damage taken" +
                        (attack.drain->heals ? ", and the monster regains that many Hit Points." : "."));
    }
    return lines;
}

DrainResult applyDrain(Combatant& target, Combatant& attacker, const HpDrain& drain,
                       const std::vector<TypedDamage>& damage, const DamageResult& result)
{
    DrainResult out;
    int amount = 0;
    if (drain.type.empty()) {
        amount = result.taken;
    } else {
        for (const TypedDamage& part : damage) {
            if (part.type == drain.type) {
                amount += damageAfterDefenses(part.amount, part.type, target.defenses);
            }
        }
        amount = std::min(amount, result.taken);
    }
    if (amount <= 0 || target.dead || !target.maxHp.has_value()) {
        return out;
    }
    const int before = *target.maxHp;
    target.maxHp = std::max(0, before - amount);
    out.reduced = before - *target.maxHp;
    target.maxHpReduction += out.reduced;
    target.hp = std::min(target.hp, *target.maxHp);
    if (*target.maxHp == 0) {
        // "A creature dies if its Hit Point maximum is reduced to 0."
        killOutright(target);
        out.died = true;
    }
    if (drain.heals && !attacker.dead) {
        out.healed = applyHealing(attacker, amount).healed;
    }
    return out;
}

void setExhaustion(Combatant& combatant, int level)
{
    combatant.exhaustion = std::clamp(level, 0, 6);
    if (combatant.exhaustion == 6 && !combatant.dead) {
        die(combatant);
    }
}

// --- Conditions and concentration ---------------------------------------------

bool conditionEndsConcentration(const std::string& id)
{
    return id == "incapacitated" || id == "paralyzed" || id == "petrified" || id == "stunned" || id == kUnconscious;
}

bool isIncapacitated(const Combatant& combatant)
{
    for (const ActiveCondition& condition : combatant.conditions) {
        if (conditionEndsConcentration(condition.id)) {
            return true;
        }
    }
    return false;
}

AddConditionResult addCondition(Combatant& combatant, ActiveCondition condition)
{
    if (condition.id.empty()) {
        return AddConditionResult::Empty;
    }
    if (contains(combatant.conditionImmunities, condition.id)) {
        return AddConditionResult::Immune;
    }
    if (condition.id == "exhaustion") {
        if (combatant.exhaustion >= 6) {
            return AddConditionResult::Duplicate;
        }
        setExhaustion(combatant, combatant.exhaustion + 1);
        return AddConditionResult::Added;
    }
    if (hasCondition(combatant, condition.id)) {
        return AddConditionResult::Duplicate;
    }
    if (conditionEndsConcentration(condition.id)) {
        endConcentration(combatant);
    }
    combatant.conditions.push_back(std::move(condition));
    grantParalyzedIncapacitated(combatant, combatant.conditions.back());
    return AddConditionResult::Added;
}

AddConditionResult addCondition(Combatant& combatant, const std::string& conditionId)
{
    return addCondition(combatant, ActiveCondition{conditionId, std::nullopt, std::nullopt});
}

std::vector<std::string> hideEndsOn()
{
    return {kEndsOnAttackRoll, kEndsOnSaveEffect, kEndsOnVerbalSpell};
}

std::vector<std::string> invisibilitySpellEndsOn()
{
    return {kEndsOnAttackRoll, kEndsOnDealsDamage, kEndsOnAnySpell};
}

std::vector<std::string> actionEvents(const std::string& name, const std::string& effect, const MonsterAttack* attack,
                                      bool dealtDamage)
{
    std::vector<std::string> events;
    if (!name.empty()) {
        events.push_back(kEndsOnActionPrefix + baseActionName(name));
    }
    if (dealtDamage) {
        events.emplace_back(kEndsOnDealsDamage);
    }
    if (attack != nullptr && attack->attackBonus.has_value()) {
        events.emplace_back(kEndsOnAttackRoll);
    }
    if (attack != nullptr && attack->save.has_value()) {
        events.emplace_back(kEndsOnSaveEffect);
    }
    // Casting a spell. Verbal unless the text says it needs no components;
    // "no Material components" still needs a Verbal one.
    const std::string text = asciiLower(effect);
    const bool casts = asciiLower(name).find("spellcasting") != std::string::npos ||
                       text.find(" casts ") != std::string::npos || text.find("uses spellcasting to cast") != std::string::npos;
    if (casts) {
        events.emplace_back(kEndsOnAnySpell);
        if (text.find("no components") == std::string::npos && text.find("no spell components") == std::string::npos &&
            text.find("no verbal") == std::string::npos) {
            events.emplace_back(kEndsOnVerbalSpell);
        }
    }
    return events;
}

std::vector<std::string> spellEvents(const Spell& spell)
{
    std::vector<std::string> events{kEndsOnAnySpell};
    if (spellHasVerbalComponent(spell)) {
        events.emplace_back(kEndsOnVerbalSpell);
    }
    return events;
}

bool spellHasVerbalComponent(const Spell& spell)
{
    return spell.components.empty() ||
           std::find(spell.components.begin(), spell.components.end(), "V") != spell.components.end();
}

std::string describeSelfEffect(const SelfEffect& effect, const std::string& conditionName)
{
    std::string text = "Gives it " + conditionName;
    if (!effect.concentration.empty()) {
        text += ", with Concentration";
    }
    text += ".";
    std::vector<std::string> ends;
    for (const std::string& trigger : effect.endsOn) {
        if (trigger == kEndsOnAttackRoll) {
            ends.emplace_back("an attack roll");
        } else if (trigger == kEndsOnSaveEffect) {
            ends.emplace_back("an effect that forces a saving throw");
        } else if (trigger == kEndsOnVerbalSpell) {
            ends.emplace_back("a spell with a Verbal component");
        } else if (trigger == kEndsOnAnySpell) {
            ends.emplace_back("casting a spell");
        } else if (trigger == kEndsOnDealsDamage) {
            ends.emplace_back("dealing damage");
        } else if (trigger.rfind(kEndsOnActionPrefix, 0) == 0) {
            ends.push_back("using " + trigger.substr(std::string(kEndsOnActionPrefix).size()));
        }
    }
    if (ends.empty()) {
        return text + (effect.concentration.empty() ? " Nothing ends it." : "");
    }
    text += " Ends early on ";
    for (std::size_t i = 0; i < ends.size(); ++i) {
        if (i > 0) {
            text += i + 1 == ends.size() ? (ends.size() > 2 ? ", or " : " or ") : ", ";
        }
        text += ends[i];
    }
    return text + ".";
}

std::vector<ActiveCondition> endConditionsOnDamage(Combatant& combatant, const std::string& attackerId,
                                                   const std::string& actionName)
{
    const std::string action = asciiLower(baseActionName(actionName));
    std::vector<ActiveCondition> spared;
    for (auto it = combatant.conditions.begin(); it != combatant.conditions.end();) {
        const bool spares = !attackerId.empty() && it->byId == attackerId &&
                            std::any_of(it->endsOn.begin(), it->endsOn.end(), [&action](const std::string& trigger) {
                                return trigger.rfind(kEndsOnSparesPrefix, 0) == 0 &&
                                       asciiLower(trigger.substr(std::string(kEndsOnSparesPrefix).size())) == action;
                            });
        if (spares && contains(it->endsOn, kEndsOnTakesDamage)) {
            spared.push_back(std::move(*it));
            it = combatant.conditions.erase(it);
        } else {
            ++it;
        }
    }
    std::vector<ActiveCondition> ended = endConditionsOn(combatant, {kEndsOnTakesDamage});
    for (ActiveCondition& condition : spared) {
        combatant.conditions.push_back(std::move(condition));
    }
    return ended;
}

std::vector<ActiveCondition> endConditionsOn(Combatant& combatant, const std::vector<std::string>& events)
{
    auto endedBy = [&events](const ActiveCondition& condition) {
        return std::any_of(condition.endsOn.begin(), condition.endsOn.end(), [&events](const std::string& trigger) {
            return std::any_of(events.begin(), events.end(),
                               [&trigger](const std::string& event) { return equalsInsensitive(event, trigger); });
        });
    };
    std::vector<ActiveCondition> removed;
    bool endsConcentration = false;
    for (auto it = combatant.conditions.begin(); it != combatant.conditions.end();) {
        if (endedBy(*it)) {
            if (!it->concentration.empty() && it->concentration == combatant.concentration) {
                endsConcentration = true;
            }
            removed.push_back(std::move(*it));
            it = combatant.conditions.erase(it);
        } else {
            ++it;
        }
    }
    if (endsConcentration) {
        for (ActiveCondition& condition : endConcentration(combatant)) {
            removed.push_back(std::move(condition));
        }
    }
    return removed;
}

AddConditionResult applySelfEffect(Combatant& combatant, const SelfEffect& effect)
{
    if (effect.condition.empty()) {
        return AddConditionResult::Empty;
    }
    if (contains(combatant.conditionImmunities, effect.condition)) {
        return AddConditionResult::Immune;
    }
    if (!effect.concentration.empty()) {
        setConcentration(combatant, effect.concentration);
    }
    // The new cause replaces the old one: Hide then Invisibility is Invisibility.
    eraseCondition(combatant, effect.condition);
    ActiveCondition condition;
    condition.id = effect.condition;
    condition.source = effect.source;
    condition.endsOn = effect.endsOn;
    condition.concentration = effect.concentration;
    return addCondition(combatant, std::move(condition));
}

bool removeCondition(Combatant& combatant, const std::string& conditionId)
{
    if (conditionId == "exhaustion" && combatant.exhaustion > 0) {
        combatant.exhaustion -= 1;
        return true;
    }
    const auto found = std::find_if(combatant.conditions.begin(), combatant.conditions.end(),
                                    [&conditionId](const ActiveCondition& condition) { return condition.id == conditionId; });
    if (found == combatant.conditions.end()) {
        return false;
    }
    // Ending the effect ends the concentration it needed (Invisibility, Vanish).
    const bool endsConcentration = !found->concentration.empty() && found->concentration == combatant.concentration;
    combatant.conditions.erase(found);
    if (endsConcentration) {
        endConcentration(combatant);
    }
    return true;
}

bool hasConcentrationDisadvantage(const Combatant& combatant)
{
    return std::any_of(combatant.timedEffects.begin(), combatant.timedEffects.end(), [](const auto& effect) {
        return effect.first.rfind(kTimedConcentrationDisadvantagePrefix, 0) == 0;
    });
}

int concentrationDc(int damage)
{
    return std::clamp(damage / 2, 10, 30);
}

void addDelayedDamage(const Encounter& encounter, Combatant& target, const std::vector<DamagePart>& damage,
                      const std::string& source, const std::string& byId)
{
    if (damage.empty()) {
        return;
    }
    DelayedDamage delayed;
    delayed.damage = damage;
    delayed.source = source;
    delayed.byId = byId;
    delayed.when = makeDuration(encounter, target.id, TurnBoundary::End, 1);
    target.delayedDamage.push_back(std::move(delayed));
}

void setConcentration(Combatant& combatant, const std::string& spellId)
{
    if (combatant.concentration == spellId) {
        return;
    }
    endConcentration(combatant);
    combatant.concentration = spellId;
}

std::vector<ActiveCondition> endConcentration(Combatant& combatant)
{
    std::vector<ActiveCondition> removed;
    if (combatant.concentration.empty()) {
        return removed;
    }
    const std::string ending = combatant.concentration;
    combatant.concentration.clear();
    combatant.sustained.reset();
    for (auto it = combatant.conditions.begin(); it != combatant.conditions.end();) {
        if (!it->concentration.empty() && it->concentration == ending) {
            removed.push_back(std::move(*it));
            it = combatant.conditions.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

ConditionDuration makeDuration(const Encounter& encounter, const std::string& anchorId, TurnBoundary boundary,
                               int turns)
{
    ConditionDuration duration;
    duration.anchorId = anchorId;
    duration.boundary = boundary;
    duration.turnsRemaining = std::max(1, turns);
    const int turn = encounter.turnIndex;
    if (boundary == TurnBoundary::End && turn >= 0 && turn < static_cast<int>(encounter.combatants.size()) &&
        encounter.combatants[static_cast<std::size_t>(turn)].id == anchorId) {
        duration.skipNext = true;
    }
    return duration;
}

std::vector<std::vector<std::string>> standardActionChoices(const std::string& effect)
{
    static const std::regex sentence(
        R"(takes the ((?:Dash|Disengage|Dodge|Hide|Help|Search)(?:(?:, or |, and |, | or | and )(?:Dash|Disengage|Dodge|Hide|Help|Search))*) actions?)");
    static const std::regex word(R"(Dash|Disengage|Dodge|Hide|Help|Search)");
    std::vector<std::vector<std::string>> choices;
    std::smatch found;
    if (!std::regex_search(effect, found, sentence)) {
        return choices;
    }
    const std::string list = found[1].str();
    std::vector<std::string> actions;
    for (auto it = std::sregex_iterator(list.begin(), list.end(), word); it != std::sregex_iterator(); ++it) {
        actions.push_back(it->str());
    }
    // "X and Y": both at once. "X or Y", "X, Y, or Z": one of them.
    if (list.find(" or ") == std::string::npos && list.find(" and ") != std::string::npos) {
        choices.push_back(actions);
    } else {
        for (const std::string& action : actions) {
            choices.push_back({action});
        }
    }
    return choices;
}

std::string standardActionRules(const std::string& action)
{
    if (action == "Dash") {
        return "For the rest of the turn, it gains extra movement equal to its Speed.";
    }
    if (action == "Disengage") {
        return "Its movement doesn't provoke Opportunity Attacks for the rest of the turn.";
    }
    if (action == "Dodge") {
        return "Until the start of its next turn, attack rolls against it have Disadvantage if it can see the "
               "attacker, and it makes Dexterity saving throws with Advantage.";
    }
    if (action == "Hide") {
        return "While Heavily Obscured or behind Three-Quarters or Total Cover, and out of any enemy's line of sight, "
               "it makes a DC 15 Dexterity (Stealth) check. On a success it has the Invisible condition; the total is "
               "the DC to find it with a Wisdom (Perception) check.";
    }
    if (action == "Help") {
        return "It helps an ally with an ability check or distracts an enemy within 5 feet, giving Advantage on the "
               "next attack roll against it.";
    }
    if (action == "Search") {
        return "It makes a Wisdom (Insight, Medicine, Perception, or Survival) check.";
    }
    return {};
}

bool takeStandardAction(Combatant& combatant, const std::string& action)
{
    ConditionDuration duration;
    duration.anchorId = combatant.id;
    if (action == "Dash" || action == "Disengage") {
        duration.boundary = TurnBoundary::End;  // the end of this turn
    } else if (action == "Dodge") {
        duration.boundary = TurnBoundary::Start;  // the start of its next turn
    } else {
        return false;
    }
    duration.turnsRemaining = 1;
    std::erase_if(combatant.timedEffects, [&action](const auto& effect) { return effect.first == action; });
    combatant.timedEffects.emplace_back(action, duration);
    return true;
}

int hideCheckBonus(const Combatant& combatant)
{
    int bonus = 0;
    if (combatant.statBlock.has_value()) {
        const auto stealth = combatant.statBlock->skills.find("stealth");
        bonus = stealth != combatant.statBlock->skills.end()
                    ? stealth->second
                    : abilityModifier(combatant.statBlock->abilities.dexterity);
    }
    return bonus - d20Penalty(combatant);
}

std::optional<int> grappleEscapeDc(const ActiveCondition& condition)
{
    if (condition.escapeDc.has_value()) {
        return condition.escapeDc;
    }
    // Saved before the DC was kept on its own: "Vampire's Grave Strike, escape DC 14".
    const std::string marker = "escape DC ";
    const auto at = condition.source.find(marker);
    if (at == std::string::npos) {
        return std::nullopt;
    }
    int dc = 0;
    bool any = false;
    for (std::size_t i = at + marker.size(); i < condition.source.size() && condition.source[i] >= '0' &&
                                             condition.source[i] <= '9';
         ++i) {
        dc = dc * 10 + (condition.source[i] - '0');
        any = true;
    }
    return any ? std::optional<int>{dc} : std::nullopt;
}

EscapeCheck escapeCheck(const Combatant& combatant, const Character* sheet)
{
    EscapeCheck athletics{0, "Athletics", Ability::Strength};
    EscapeCheck acrobatics{0, "Acrobatics", Ability::Dexterity};
    if (sheet != nullptr) {
        const int proficiency = proficiencyBonus(*sheet);
        athletics.bonus = abilityModifier(sheet->abilities.strength) + (sheet->skills.athletics ? proficiency : 0);
        acrobatics.bonus = abilityModifier(sheet->abilities.dexterity) + (sheet->skills.acrobatics ? proficiency : 0);
    } else if (combatant.statBlock.has_value()) {
        const Monster& block = *combatant.statBlock;
        const auto skill = [&block](const char* name, int score) {
            const auto found = block.skills.find(name);
            return found != block.skills.end() ? found->second : abilityModifier(score);
        };
        athletics.bonus = skill("athletics", block.abilities.strength);
        acrobatics.bonus = skill("acrobatics", block.abilities.dexterity);
    }
    EscapeCheck best = acrobatics.bonus > athletics.bonus ? acrobatics : athletics;
    best.bonus -= d20Penalty(combatant);
    return best;
}

bool escapeGrapple(Combatant& combatant, const std::string& grapplerId)
{
    const auto before = combatant.conditions.size();
    combatant.conditions.erase(std::remove_if(combatant.conditions.begin(), combatant.conditions.end(),
                                              [&grapplerId](const ActiveCondition& condition) {
                                                  return condition.id == "grappled" && condition.byId == grapplerId;
                                              }),
                               combatant.conditions.end());
    return combatant.conditions.size() != before;
}

std::vector<std::string> conditionNotes(const ActiveCondition& condition, const Encounter& encounter)
{
    std::vector<std::string> notes;
    if (!condition.source.empty()) {
        notes.push_back(condition.source);
    }
    if (!condition.concentration.empty()) {
        notes.push_back("while concentrating");
    }
    if (condition.duration.has_value()) {
        const ConditionDuration& duration = *condition.duration;
        std::string anchor = "someone who left the fight";
        for (const Combatant& combatant : encounter.combatants) {
            if (combatant.id == duration.anchorId) {
                anchor = combatant.name;
            }
        }
        const char* edge = duration.boundary == TurnBoundary::Start ? "start" : "end";
        if (duration.turnsRemaining <= 1) {
            notes.push_back(std::string("until the ") + edge + " of " + anchor + "'s next turn");
        } else {
            notes.push_back(std::to_string(duration.turnsRemaining) + " turns, ending at the " + edge + " of " +
                            anchor + "'s turn");
        }
    }
    if (condition.saveEnds.has_value()) {
        std::string save = "DC " + std::to_string(condition.saveEnds->dc) + " " +
                           abilityShort(condition.saveEnds->ability) + " save ends";
        if (condition.saveEnds->manual) {
            save += " (rolled when the spell calls for it)";
        }
        if (!condition.saveEnds->worsensTo.empty()) {
            save += ", a failure makes it " + joinWords(conditionWords(condition.saveEnds->worsensTo));
        }
        notes.push_back(save);
    }
    if (!condition.tiedTo.empty()) {
        notes.push_back("while " + capitalized(condition.tiedTo));
    }
    if (contains(condition.endsOn, kEndsOnTakesDamage)) {
        notes.push_back("ends on taking damage");
    }
    if (!condition.ongoing.empty()) {
        std::vector<std::string> parts;
        for (const DamagePart& part : condition.ongoing) {
            parts.push_back(part.dice + (part.type.empty() ? "" : " " + part.type));
        }
        notes.push_back(joinWords(parts) + " damage each turn");
    }
    return notes;
}

std::string describeCondition(const ActiveCondition& condition, const std::string& displayName,
                              const Encounter& encounter)
{
    std::string text = displayName;
    const std::vector<std::string> notes = conditionNotes(condition, encounter);
    if (!notes.empty()) {
        text += " (";
        for (std::size_t i = 0; i < notes.size(); ++i) {
            if (i > 0) {
                text += "; ";
            }
            text += notes[i];
        }
        text += ")";
    }
    return text;
}

// --- Rolls ----------------------------------------------------------------------

int pickD20(RollMode mode, int first, int second)
{
    switch (mode) {
    case RollMode::Advantage:
        return std::max(first, second);
    case RollMode::Disadvantage:
        return std::min(first, second);
    case RollMode::Normal:
        break;
    }
    return first;
}

SaveRoll rollSave(const Combatant& combatant, Ability ability, int dc, int face)
{
    SaveRoll roll;
    roll.face = face;
    roll.total = clampToInt(static_cast<long long>(face) + combatant.saveBonuses[static_cast<std::size_t>(ability)] -
                            d20Penalty(combatant));
    if (ability == Ability::Strength || ability == Ability::Dexterity) {
        for (const char* id : {"paralyzed", "petrified", "stunned", kUnconscious}) {
            if (hasCondition(combatant, id)) {
                roll.automaticFailure = true;
            }
        }
    }
    roll.success = !roll.automaticFailure && roll.total >= dc;
    return roll;
}

std::string describeD20(const D20Calculation& roll)
{
    std::string text = std::to_string(roll.face);
    if (roll.otherFace.has_value() && roll.mode != RollMode::Normal) {
        // face is the die kept; otherFace the one set aside.
        text += (roll.mode == RollMode::Advantage ? " (higher of " : " (lower of ") + std::to_string(roll.face) +
                " and " + std::to_string(*roll.otherFace) + ")";
    }
    const bool adjusted = roll.bonus != 0 || roll.penalty > 0;
    if (roll.bonus != 0) {
        text += (roll.bonus > 0 ? " + " : " − ") + std::to_string(std::abs(roll.bonus));
        if (!roll.bonusLabel.empty()) {
            text += " " + roll.bonusLabel;
        }
    }
    if (roll.penalty > 0) {
        text += " − " + std::to_string(roll.penalty) + " Exhaustion";
    }
    if (adjusted) {
        text += " = " + std::to_string(roll.total);
    }
    if (!roll.against.empty()) {
        text += " vs " + roll.against;
    }
    return text;
}

AttackRoll resolveAttackRoll(int attackBonus, int targetAc, int attackerPenalty, int face)
{
    AttackRoll roll;
    roll.face = face;
    roll.total = clampToInt(static_cast<long long>(face) + attackBonus - attackerPenalty);
    roll.critical = face >= 20;
    roll.hit = roll.critical || (face > 1 && roll.total >= targetAc);
    return roll;
}

RollMode suggestedAttackMode(const Combatant& attacker, const Combatant& target, bool melee)
{
    bool advantage = false;
    bool disadvantage = false;
    for (const char* id : {"blinded", "paralyzed", "petrified", "restrained", "stunned", kUnconscious}) {
        if (hasCondition(target, id)) {
            advantage = true;
        }
    }
    if (hasCondition(target, "prone")) {
        (melee ? advantage : disadvantage) = true;
    }
    if (hasCondition(target, "invisible")) {
        disadvantage = true;
    }
    if (hasCondition(attacker, "invisible")) {
        advantage = true;
    }
    for (const char* id : {"blinded", "frightened", "poisoned", "prone", "restrained", kPhantasmalFear}) {
        if (hasCondition(attacker, id)) {
            disadvantage = true;
        }
    }
    if (advantage == disadvantage) {
        return RollMode::Normal;
    }
    return advantage ? RollMode::Advantage : RollMode::Disadvantage;
}

RollMode suggestedAttackMode(const Combatant& attacker, const Combatant& target, const MonsterAttack& attack)
{
    const RollMode mode = suggestedAttackMode(attacker, target, isMeleeAttack(attack));
    if (!attack.advantageIfGrappled || !isGrappledBy(target, attacker.id)) {
        return mode;
    }
    // Advantage from the grapple: cancels Disadvantage, otherwise Advantage.
    return mode == RollMode::Disadvantage ? RollMode::Normal : RollMode::Advantage;
}

std::vector<std::string> damageConditionsMet(const Combatant& attacker, const Combatant* target)
{
    std::vector<std::string> met;
    const auto bloodied = [](const Combatant& creature) {
        return creature.maxHp.has_value() && *creature.maxHp > 0 && creature.hp * 2 <= *creature.maxHp;
    };
    if (bloodied(attacker)) {
        met.emplace_back(kDamageIfSelfBloodied);
    }
    if (target != nullptr) {
        if (bloodied(*target)) {
            met.emplace_back(kDamageIfTargetBloodied);
        }
        if (isGrappledBy(*target, attacker.id) && hasCondition(*target, "grappled")) {
            met.emplace_back(kDamageIfGrappledBySelf);
        }
    }
    return met;
}

std::vector<std::string> attackModeReasons(const Combatant& attacker, const Combatant& target,
                                           const MonsterAttack& attack)
{
    std::vector<std::string> reasons;
    const bool melee = isMeleeAttack(attack);
    for (const char* id : {"blinded", "paralyzed", "petrified", "restrained", "stunned", "unconscious"}) {
        if (hasCondition(target, id)) {
            reasons.push_back(target.name + " is " + capitalized(id));
        }
    }
    if (hasCondition(target, "prone")) {
        reasons.push_back(target.name + " is Prone" + (melee ? "" : " (Disadvantage at range)"));
    }
    if (hasCondition(target, "invisible")) {
        reasons.push_back(target.name + " is Invisible");
    }
    if (hasCondition(attacker, "invisible")) {
        reasons.push_back(attacker.name + " is Invisible");
    }
    for (const char* id : {"blinded", "frightened", "poisoned", "prone", "restrained", kPhantasmalFear}) {
        if (hasCondition(attacker, id)) {
            const std::string custom = spellEffectConditionName(id);
            reasons.push_back(attacker.name + " is " + (custom.empty() ? capitalized(id) : custom));
        }
    }
    if (attack.advantageIfGrappled && isGrappledBy(target, attacker.id) && hasCondition(target, "grappled")) {
        reasons.push_back(attacker.name + " is grappling " + target.name);
    }
    return reasons;
}

bool isBloodied(const Combatant& creature)
{
    return creature.maxHp.has_value() && *creature.maxHp > 0 && creature.hp * 2 <= *creature.maxHp;
}

namespace {

// The monsters whose traits can change this attacker's rolls: itself, and
// allies whose trait reaches others (Aura of Authority).
template <class Visit>
void forEachModifier(const Encounter& encounter, const Combatant& attacker, Visit visit)
{
    for (const Combatant& source : encounter.combatants) {
        if (!source.statBlock.has_value()) {
            continue;
        }
        const bool self = source.id == attacker.id;
        if (!self && (!isMonsterCombatant(attacker) || source.dead || !isInInitiative(source))) {
            continue;
        }
        for (const MonsterFeature& trait : source.statBlock->traits) {
            if (!trait.attackModifier.has_value() || (!self && !trait.attackModifier->alliesToo)) {
                continue;
            }
            if (trait.attackModifier->whileActive && isIncapacitated(source)) {
                continue;
            }
            visit(source, trait, *trait.attackModifier);
        }
    }
}

std::string questionKey(const Combatant& source, const MonsterFeature& trait)
{
    return source.id + "/" + trait.name;
}

}  // namespace

std::string describeAttackModifier(const AttackModifier& modifier)
{
    std::string text = std::string(modifier.advantage ? "Advantage" : "Disadvantage") + " on " +
                       (modifier.alliesToo ? "its and its allies' " : "its ") + (modifier.meleeOnly ? "melee " : "") +
                       "attack rolls";
    if (modifier.when == kModifierWhileBloodied) {
        text += " while it is Bloodied (automatic)";
    } else if (modifier.when == kModifierTargetHurt) {
        text += " against a creature missing Hit Points (automatic)";
    } else if (modifier.when == kModifierAfterDamage) {
        text += " until the end of its next turn after it takes " + modifier.damageType + " damage (automatic)";
    } else {
        text += " when " + (modifier.ask.empty() ? std::string("it applies") : modifier.ask) +
                (modifier.sticky ? " (tick it in the fight; it stays ticked)"
                                 : " (tick it in the fight before the attack)");
    }
    if (modifier.whileActive) {
        text += ", unless it is Incapacitated";
    }
    return text + ".";
}

std::vector<RollQuestion> attackRollQuestions(const Encounter& encounter, const Combatant& attacker)
{
    std::vector<RollQuestion> questions;
    forEachModifier(encounter, attacker,
                    [&](const Combatant& source, const MonsterFeature& trait, const AttackModifier& modifier) {
                        if (!modifier.when.empty()) {
                            return;
                        }
                        RollQuestion question;
                        question.key = questionKey(source, trait);
                        question.sourceId = source.id;
                        question.trait = source.id == attacker.id ? trait.name : trait.name + " (" + source.name + ")";
                        question.text = modifier.ask.empty() ? std::string("it applies") : modifier.ask;
                        question.advantage = modifier.advantage;
                        question.sticky = modifier.sticky;
                        questions.push_back(std::move(question));
                    });
    return questions;
}

AttackModeChoice decideAttackMode(const Encounter& encounter, const Combatant& attacker, const Combatant& target,
                                  const MonsterAttack& attack, const std::vector<std::string>& ticked)
{
    AttackModeChoice choice;
    const bool melee = isMeleeAttack(attack);
    for (const char* id : {"blinded", "paralyzed", "petrified", "restrained", "stunned", kUnconscious}) {
        if (hasCondition(target, id)) {
            choice.advantages.push_back(target.name + " is " + capitalized(id));
        }
    }
    if (hasCondition(target, "prone")) {
        (melee ? choice.advantages : choice.disadvantages).push_back(target.name + " is Prone");
    }
    if (hasCondition(target, "invisible")) {
        choice.disadvantages.push_back(target.name + " is Invisible");
    }
    if (hasCondition(attacker, "invisible")) {
        choice.advantages.push_back(attacker.name + " is Invisible");
    }
    for (const char* id : {"blinded", "frightened", "poisoned", "prone", "restrained", kPhantasmalFear}) {
        if (hasCondition(attacker, id)) {
            const std::string custom = spellEffectConditionName(id);
            choice.disadvantages.push_back(attacker.name + " is " + (custom.empty() ? capitalized(id) : custom));
        }
    }
    if (attack.advantageIfGrappled && isGrappledBy(target, attacker.id) && hasCondition(target, "grappled")) {
        choice.advantages.push_back(attacker.name + " is grappling " + target.name);
    }
    for (const auto& effect : attacker.timedEffects) {
        if (effect.first.rfind("advantage:", 0) == 0) {
            choice.advantages.push_back(effect.first.substr(10));  // "War Cry"
        }
    }
    forEachModifier(encounter, attacker,
                    [&](const Combatant& source, const MonsterFeature& trait, const AttackModifier& modifier) {
                        if (modifier.meleeOnly && !melee) {
                            return;
                        }
                        std::string why;
                        if (modifier.when == kModifierWhileBloodied) {
                            if (isBloodied(source)) {
                                why = trait.name + ": " + source.name + " is Bloodied";
                            }
                        } else if (modifier.when == kModifierTargetHurt) {
                            if (target.maxHp.has_value() && target.hp < *target.maxHp) {
                                why = trait.name + ": " + target.name + " is hurt";
                            }
                        } else if (modifier.when == kModifierAfterDamage) {
                            for (const auto& effect : source.timedEffects) {
                                if (effect.first == trait.name) {
                                    why = trait.name + ": " + source.name + " took " + modifier.damageType + " damage";
                                }
                            }
                        } else if (contains(ticked, questionKey(source, trait))) {
                            why = trait.name + (source.id == attacker.id ? "" : " (" + source.name + ")");
                        }
                        if (!why.empty()) {
                            (modifier.advantage ? choice.advantages : choice.disadvantages).push_back(why);
                        }
                    });
    if (!choice.advantages.empty() && choice.disadvantages.empty()) {
        choice.mode = RollMode::Advantage;
    } else if (choice.advantages.empty() && !choice.disadvantages.empty()) {
        choice.mode = RollMode::Disadvantage;
    }
    return choice;
}

BenefitResult applyBenefit(const Encounter& encounter, const Combatant& source, Combatant& target,
                           const MonsterAttack& action, const RollDie& rollDie)
{
    BenefitResult result;
    if (!action.benefit.has_value() || target.dead) {
        return result;
    }
    const Benefit& benefit = *action.benefit;
    if (const std::optional<Dice> dice = parseDice(benefit.tempHp)) {
        result.tempHp = rollDice(*dice, rollDie, false);
        if (result.tempHp > target.tempHp) {
            setTemporaryHitPoints(target, result.tempHp);
        } else {
            result.tempHpKept = true;
        }
    }
    if (const std::optional<Dice> dice = parseDice(benefit.healing)) {
        result.healed = applyHealing(target, rollDice(*dice, rollDie, false)).healed;
    }
    const ConditionDuration duration =
        makeDuration(encounter, source.id, benefit.until == kUntilSourceEnd ? TurnBoundary::End : TurnBoundary::Start, 1);
    const std::string name = baseActionName(action.name);
    if (benefit.advantageOnAttacks) {
        std::erase_if(target.timedEffects, [&name](const auto& effect) { return effect.first == "advantage:" + name; });
        target.timedEffects.emplace_back("advantage:" + name, duration);
        result.advantage = true;
    }
    if (benefit.acBonus != 0) {
        const std::string key = "ac:" + std::to_string(benefit.acBonus) + ":" + name;
        const bool already = std::any_of(target.timedEffects.begin(), target.timedEffects.end(),
                                         [&key](const auto& effect) { return effect.first == key; });
        std::erase_if(target.timedEffects, [&key](const auto& effect) { return effect.first == key; });
        target.timedEffects.emplace_back(key, duration);
        if (!already) {
            target.ac += benefit.acBonus;
        }
        result.acBonus = benefit.acBonus;
    }
    return result;
}

std::vector<std::string> noteDamageTaken(const Encounter& encounter, Combatant& target, const DamageResult& result)
{
    std::vector<std::string> started;
    if (!target.statBlock.has_value() || target.dead) {
        return started;
    }
    for (const MonsterFeature& trait : target.statBlock->traits) {
        if (!trait.attackModifier.has_value() || trait.attackModifier->when != kModifierAfterDamage ||
            !contains(result.typesTaken, trait.attackModifier->damageType)) {
            continue;
        }
        std::erase_if(target.timedEffects, [&trait](const auto& effect) { return effect.first == trait.name; });
        target.timedEffects.emplace_back(trait.name, makeDuration(encounter, target.id, TurnBoundary::End, 1));
        started.push_back(trait.name);
    }
    return started;
}

bool meleeHitIsCritical(const Combatant& target)
{
    return hasCondition(target, "paralyzed") || hasCondition(target, kUnconscious);
}

bool isMeleeAttack(const MonsterAttack& attack)
{
    return attack.effect.rfind("Melee", 0) == 0;
}

std::vector<TypedDamage> rollDamageParts(const std::vector<DamagePart>& parts, const DamageOptions& options,
                                         const RollDie& rollDie)
{
    std::vector<TypedDamage> rolled;
    auto replaceLast = [&rolled](const std::string& type, int amount) {
        for (auto it = rolled.rbegin(); it != rolled.rend(); ++it) {
            if (it->type == type) {
                it->amount = amount;
                return;
            }
        }
        rolled.push_back(TypedDamage{amount, type});
    };
    for (const DamagePart& part : parts) {
        const std::optional<Dice> dice = parseDice(part.dice);
        if (!dice.has_value()) {
            continue;
        }
        switch (part.when) {
        case DamageWhen::Always:
            rolled.push_back(TypedDamage{rollDice(*dice, rollDie, options.critical), part.type});
            break;
        case DamageWhen::Advantage:
            if (options.advantage) {
                rolled.push_back(TypedDamage{rollDice(*dice, rollDie, options.critical), part.type});
            }
            break;
        case DamageWhen::AdvantageAlt:
            if (options.advantage) {
                replaceLast(part.type, rollDice(*dice, rollDie, options.critical));
            }
            break;
        case DamageWhen::Alternative:
            if (part.condition.empty() ? options.alternative : contains(options.conditionsMet, part.condition)) {
                replaceLast(part.type, rollDice(*dice, rollDie, options.critical));
            }
            break;
        case DamageWhen::Conditional:
            if (part.condition.empty() ? options.conditional : contains(options.conditionsMet, part.condition)) {
                rolled.push_back(TypedDamage{rollDice(*dice, rollDie, options.critical), part.type});
            }
            break;
        case DamageWhen::Ongoing:
            break;
        }
    }
    return rolled;
}

std::string damageFormula(const std::vector<DamagePart>& parts, const DamageOptions& options)
{
    // The same choices as rollDamageParts, with text in place of rolls.
    std::vector<std::pair<std::string, std::string>> chosen;  // type, dice
    const auto text = [&options](Dice dice) {
        if (options.critical) {
            dice.count *= 2;
        }
        return formatDice(dice);
    };
    auto replaceLast = [&chosen](const std::string& type, const std::string& dice) {
        for (auto it = chosen.rbegin(); it != chosen.rend(); ++it) {
            if (it->first == type) {
                it->second = dice;
                return;
            }
        }
        chosen.emplace_back(type, dice);
    };
    for (const DamagePart& part : parts) {
        const std::optional<Dice> dice = parseDice(part.dice);
        if (!dice.has_value()) {
            continue;
        }
        switch (part.when) {
        case DamageWhen::Always:
            chosen.emplace_back(part.type, text(*dice));
            break;
        case DamageWhen::Advantage:
            if (options.advantage) {
                chosen.emplace_back(part.type, text(*dice));
            }
            break;
        case DamageWhen::AdvantageAlt:
            if (options.advantage) {
                replaceLast(part.type, text(*dice));
            }
            break;
        case DamageWhen::Alternative:
            if (part.condition.empty() ? options.alternative : contains(options.conditionsMet, part.condition)) {
                replaceLast(part.type, text(*dice));
            }
            break;
        case DamageWhen::Conditional:
            if (part.condition.empty() ? options.conditional : contains(options.conditionsMet, part.condition)) {
                chosen.emplace_back(part.type, text(*dice));
            }
            break;
        case DamageWhen::Ongoing:
            break;
        }
    }
    std::string formula;
    for (const auto& [type, dice] : chosen) {
        if (!formula.empty()) {
            formula += " + ";
        }
        formula += dice;
        if (!type.empty()) {
            formula += " " + type;
        }
    }
    return formula;
}

std::vector<TypedDamage> halveDamage(const std::vector<TypedDamage>& parts)
{
    std::vector<TypedDamage> halved = parts;
    for (TypedDamage& part : halved) {
        part.amount /= 2;
    }
    return halved;
}

int totalDamage(const std::vector<TypedDamage>& parts)
{
    long long total = 0;
    for (const TypedDamage& part : parts) {
        total += std::max(0, part.amount);
    }
    return clampToInt(total);
}

std::string describeDamage(const std::vector<TypedDamage>& parts)
{
    std::string text;
    for (const TypedDamage& part : parts) {
        if (!text.empty()) {
            text += " + ";
        }
        text += std::to_string(part.amount);
        if (!part.type.empty()) {
            text += " " + part.type;
        }
    }
    return text.empty() ? "0" : text;
}

// --- Action economy ---------------------------------------------------------

// A spell cast in place of Spellcasting (or Charm) spends that action's
// Multiattack entry, not one named for the spell.
const std::string& multiattackBucket(const MonsterAttack& attack)
{
    return attack.multiattackAs.empty() ? attack.name : attack.multiattackAs;
}

namespace {

// Each attack Multiattack names, as often as it names it. When the counts do
// not reach the total (an attack per head), the map is empty and the total
// alone decides.
std::map<std::string, int> multiattackPlan(const Monster& block, int total)
{
    std::map<std::string, int> plan;
    int named = 0;
    bool spellcasting = false;
    int spellcastingCount = 1;
    for (const MonsterAttack& part : block.attacks) {
        if (part.inMultiattack && !isMultiattack(part)) {
            plan[part.name] = std::max(1, part.count);
            named += std::max(1, part.count);
            if (part.name == "Spellcasting") {
                spellcasting = true;
                spellcastingCount = std::max(1, part.count);
            }
        }
    }
    // Counts that do not add up (one attack per head) leave only the
    // total. A spell still replaces just one of those attacks.
    if (named < total) {
        plan.clear();
    }
    if (spellcasting && plan.find("Spellcasting") == plan.end()) {
        plan["Spellcasting"] = spellcastingCount;
    }
    return plan;
}

void startMultiattack(Combatant& combatant, const MonsterAttack& multi)
{
    TurnEconomy& economy = combatant.economy;
    economy.actionUsed = true;
    economy.attacksRemaining = std::max(1, multi.count);
    economy.multiattackLeft.clear();
    if (combatant.statBlock.has_value()) {
        economy.multiattackLeft = multiattackPlan(*combatant.statBlock, economy.attacksRemaining);
    }
}

}  // namespace

const MonsterAttack* multiattackEntry(const Combatant& combatant)
{
    if (!combatant.statBlock.has_value()) {
        return nullptr;
    }
    for (const MonsterAttack& attack : combatant.statBlock->attacks) {
        if (isMultiattack(attack)) {
            return &attack;
        }
    }
    return nullptr;
}

bool startsMultiattack(const Combatant& combatant, const MonsterAttack& attack, bool theirTurn)
{
    const TurnEconomy& economy = combatant.economy;
    return theirTurn && attack.inMultiattack && !isMultiattack(attack) && !economy.actionUsed &&
           economy.attacksRemaining == 0 && multiattackEntry(combatant) != nullptr;
}

std::optional<int> multiattackUsesLeft(const Combatant& combatant, const MonsterAttack& attack)
{
    const MonsterAttack* multi = multiattackEntry(combatant);
    if (multi == nullptr || !attack.inMultiattack || isMultiattack(attack)) {
        return std::nullopt;
    }
    const TurnEconomy& economy = combatant.economy;
    const std::string& bucket = multiattackBucket(attack);
    if (economy.attacksRemaining > 0) {
        const auto left = economy.multiattackLeft.find(bucket);
        return left == economy.multiattackLeft.end() ? economy.attacksRemaining
                                                     : std::min(left->second, economy.attacksRemaining);
    }
    if (economy.actionUsed) {
        return 0;
    }
    const int total = std::max(1, multi->count);
    const std::map<std::string, int> plan = multiattackPlan(*combatant.statBlock, total);
    const auto planned = plan.find(bucket);
    return planned == plan.end() ? total : std::min(planned->second, total);
}

Availability actionAvailability(const Combatant& combatant, const MonsterAttack& attack, bool theirTurn)
{
    if (isIncapacitated(combatant)) {
        return {false, "Incapacitated"};
    }
    if (!isInInitiative(combatant)) {
        return {false, "Out of the fight"};
    }
    if (attack.recharge.has_value() && contains(combatant.expended, attack.name)) {
        return {false, "Recharging"};
    }
    if (attack.perDay.has_value() && usesLeft(combatant, attack) <= 0) {
        return {false, "No uses left today"};
    }
    const TurnEconomy& economy = combatant.economy;
    if (isMultiattack(attack)) {
        if (!theirTurn) {
            return {false, "Not its turn"};
        }
        return economy.actionUsed ? Availability{false, "Its action is used"} : Availability{true, {}};
    }
    if (!economy.grantedAttack.empty() && sameAction(economy.grantedAttack, attack.name)) {
        return {true, {}};
    }
    if (!theirTurn) {
        return {false, "Not its turn"};
    }
    if (economy.attacksRemaining > 0) {
        if (!attack.inMultiattack) {
            return {false, "Not part of Multiattack"};
        }
        const auto left = economy.multiattackLeft.find(multiattackBucket(attack));
        if (left != economy.multiattackLeft.end() && left->second <= 0) {
            return {false, "Multiattack allows no more of this attack"};
        }
        return {true, {}};
    }
    return economy.actionUsed ? Availability{false, "Its action is used"} : Availability{true, {}};
}

bool useAction(Combatant& combatant, const MonsterAttack& attack, bool theirTurn)
{
    if (!actionAvailability(combatant, attack, theirTurn).available) {
        return false;
    }
    TurnEconomy& economy = combatant.economy;
    if (isMultiattack(attack)) {
        startMultiattack(combatant, attack);
        return true;
    }
    // An attack Multiattack names, with the action still free, takes the
    // Multiattack action and is its first attack.
    const bool started = startsMultiattack(combatant, attack, theirTurn);
    if (started) {
        startMultiattack(combatant, *multiattackEntry(combatant));
    }
    if (!started && !economy.grantedAttack.empty() && sameAction(economy.grantedAttack, attack.name) &&
        (!theirTurn || economy.actionUsed)) {
        economy.grantedAttack.clear();
    } else if (economy.attacksRemaining > 0 && attack.inMultiattack) {
        economy.attacksRemaining -= 1;
        if (const auto left = economy.multiattackLeft.find(multiattackBucket(attack)); left != economy.multiattackLeft.end()) {
            left->second = std::max(0, left->second - 1);
        }
        if (economy.attacksRemaining == 0) {
            economy.multiattackLeft.clear();
        }
    } else {
        economy.actionUsed = true;
        economy.attacksRemaining = 0;
    }
    if (attack.recharge.has_value()) {
        combatant.expended.push_back(attack.name);
    }
    if (attack.perDay.has_value()) {
        combatant.usesRemaining[attack.name] = std::max(0, usesLeft(combatant, attack) - 1);
    }
    return true;
}

Availability featureAvailability(const Combatant& combatant, FeatureKind kind, const MonsterFeature& feature,
                                 bool theirTurn)
{
    if (feature.recharge.has_value() && contains(combatant.expended, feature.name)) {
        return {false, "Recharging"};
    }
    if (feature.perDay.has_value() && usesLeft(combatant, feature.name, feature.perDay) <= 0) {
        return {false, "No uses left today"};
    }
    if (feature.afterDamagingBloodied && !contains(combatant.economy.triggered, feature.name)) {
        return {false, "Only right after it damages a creature that was already Bloodied"};
    }
    return featureAvailability(combatant, kind, theirTurn);
}

Availability featureAvailability(const Combatant& combatant, FeatureKind kind, bool theirTurn)
{
    if (kind == FeatureKind::Trait) {
        // Legendary Resistance works even while Stunned.
        return isInInitiative(combatant) ? Availability{true, {}} : Availability{false, "Out of the fight"};
    }
    if (isIncapacitated(combatant)) {
        return {false, "Incapacitated"};
    }
    if (!isInInitiative(combatant)) {
        return {false, "Out of the fight"};
    }
    const TurnEconomy& economy = combatant.economy;
    switch (kind) {
    case FeatureKind::BonusAction:
        if (!theirTurn) {
            return {false, "Not its turn"};
        }
        return economy.bonusActionUsed ? Availability{false, "Bonus action used"} : Availability{true, {}};
    case FeatureKind::Reaction:
        return economy.reactionUsed ? Availability{false, "Reaction used"} : Availability{true, {}};
    case FeatureKind::Legendary:
        if (theirTurn) {
            return {false, "Only after another creature's turn"};
        }
        return economy.legendaryRemaining > 0 ? Availability{true, {}}
                                              : Availability{false, "No legendary uses left"};
    case FeatureKind::Trait:
        return {true, {}};
    }
    return {false, {}};
}

bool useFeature(Combatant& combatant, FeatureKind kind, const MonsterFeature& feature, bool theirTurn)
{
    if (!featureAvailability(combatant, kind, feature, theirTurn).available) {
        return false;
    }
    if (feature.recharge.has_value()) {
        combatant.expended.push_back(feature.name);
    }
    if (feature.perDay.has_value()) {
        combatant.usesRemaining[feature.name] = std::max(0, usesLeft(combatant, feature.name, feature.perDay) - 1);
    }
    TurnEconomy& economy = combatant.economy;
    switch (kind) {
    case FeatureKind::BonusAction:
        economy.bonusActionUsed = true;
        break;
    case FeatureKind::Trait:
        break;
    case FeatureKind::Reaction:
        economy.reactionUsed = true;
        break;
    case FeatureKind::Legendary:
        economy.legendaryRemaining -= 1;
        break;
    }
    // "It makes one Rend attack" (Pounce), "and it makes one Bite attack"
    // (Rampage): that attack can be used once more.
    if (kind != FeatureKind::Trait) {
        const std::string marker = "makes one ";
        const auto at = feature.effect.find(marker);
        if (at != std::string::npos) {
            const auto start = at + marker.size();
            const auto end = feature.effect.find(" attack", start);
            if (end != std::string::npos) {
                economy.grantedAttack = feature.effect.substr(start, end - start);
            }
        }
    }
    return true;
}

std::vector<std::string> noteDamagedBloodied(Combatant& monster)
{
    std::vector<std::string> ready;
    if (!monster.statBlock.has_value()) {
        return ready;
    }
    for (const auto* list : {&monster.statBlock->bonusActions, &monster.statBlock->reactions,
                             &monster.statBlock->legendaryActions}) {
        for (const MonsterFeature& feature : *list) {
            if (feature.afterDamagingBloodied && !contains(monster.economy.triggered, feature.name)) {
                monster.economy.triggered.push_back(feature.name);
                ready.push_back(feature.name);
            }
        }
    }
    return ready;
}

bool monsterActionSpent(const Combatant& combatant)
{
    return combatant.economy.actionUsed && combatant.economy.attacksRemaining == 0 &&
           combatant.economy.grantedAttack.empty();
}

void startTurnEconomy(Combatant& combatant)
{
    const int legendary = combatant.statBlock.has_value() ? combatant.statBlock->legendaryActionUses : 0;
    combatant.economy = TurnEconomy{};
    combatant.economy.legendaryRemaining = legendary;
}

// --- Auras -------------------------------------------------------------------

std::string creatureTypeKey(const Combatant& combatant)
{
    if (!isMonsterCombatant(combatant)) {
        return "humanoid";  // every 2024 player species is a Humanoid
    }
    if (!combatant.statBlock.has_value()) {
        return {};
    }
    std::string type = asciiLower(trim(combatant.statBlock->creatureType));
    if (type.rfind("swarm of ", 0) == 0) {
        type = type.substr(type.find_last_of(' ') + 1);  // "beasts"
        if (type.size() > 1 && type.back() == 's') {
            type.pop_back();
        }
        return type;
    }
    const auto end = type.find_first_of(" (");
    return end == std::string::npos ? type : type.substr(0, end);
}

std::vector<AuraCheck> aurasAtTurnStart(const Encounter& encounter, const std::string& combatantId)
{
    std::vector<AuraCheck> checks;
    const Combatant* target = nullptr;
    for (const Combatant& combatant : encounter.combatants) {
        if (combatant.id == combatantId) {
            target = &combatant;
        }
    }
    if (target == nullptr || target->dead || !isInInitiative(*target)) {
        return checks;
    }
    for (const Combatant& source : encounter.combatants) {
        if (source.id == combatantId || !isMonsterCombatant(source) || !source.statBlock.has_value() ||
            source.dead || !isInInitiative(source)) {
            continue;
        }
        for (const MonsterFeature& trait : source.statBlock->traits) {
            if (!trait.aura.has_value() || contains(source.aurasOff, trait.name)) {
                continue;
            }
            if (trait.aura->whileActive && isIncapacitated(source)) {
                continue;
            }
            // Monsters fight on one side, characters on the other.
            if (trait.aura->enemiesOnly && isMonsterCombatant(*target)) {
                continue;
            }
            // Only some creature types ("any Beast or Humanoid"). A creature of
            // unknown type is asked.
            if (!trait.aura->creatureTypes.empty()) {
                const std::string type = creatureTypeKey(*target);
                if (!type.empty() &&
                    std::none_of(trait.aura->creatureTypes.begin(), trait.aura->creatureTypes.end(),
                                 [&type](const std::string& wanted) { return equalsInsensitive(wanted, type); })) {
                    continue;
                }
            }
            // "That can see the hag's true form": not a Blinded or Unconscious
            // creature. "Can hear": not a Deafened or Unconscious one.
            const std::string who = asciiLower(trait.aura->who);
            if (who.find("can see") != std::string::npos &&
                (hasCondition(*target, "blinded") || hasCondition(*target, kUnconscious))) {
                continue;
            }
            if (who.find("can hear") != std::string::npos &&
                (hasCondition(*target, "deafened") || hasCondition(*target, kUnconscious))) {
                continue;
            }
            // Immune to the condition, and nothing else happens on a failure.
            if (!trait.aura->condition.empty() && trait.aura->failureDie == 0 &&
                contains(target->conditionImmunities, trait.aura->condition)) {
                continue;
            }
            if (contains(target->auraImmunities, source.id + "/" + trait.name)) {
                continue;
            }
            checks.push_back(AuraCheck{source.id, trait.name, *trait.aura});
        }
    }
    return checks;
}

AddConditionResult applyAuraFailure(const Encounter& encounter, Combatant& target, const AuraCheck& check)
{
    if (check.aura.condition.empty()) {
        return AddConditionResult::Empty;
    }
    ActiveCondition condition;
    condition.id = check.aura.condition;
    condition.source = check.name;
    condition.duration = makeDuration(encounter, target.id, TurnBoundary::Start, 1);
    // A repeat failure replaces the old one, so its duration starts again.
    eraseCondition(target, condition.id);
    return addCondition(target, std::move(condition));
}

void markAuraImmune(Combatant& target, const AuraCheck& check)
{
    const std::string key = check.sourceId + "/" + check.name;
    if (!contains(target.auraImmunities, key)) {
        target.auraImmunities.push_back(key);
    }
}

std::string auraFailureOutcome(const AuraSave& aura, int roll)
{
    for (const AuraOutcome& row : aura.failureTable) {
        if (roll <= row.upTo) {
            return row.text;
        }
    }
    return {};
}

std::vector<std::string> suppressAuras(Combatant& monster, const std::string& actionName)
{
    std::vector<std::string> off;
    if (!monster.statBlock.has_value()) {
        return off;
    }
    for (const MonsterFeature& trait : monster.statBlock->traits) {
        if (!trait.aura.has_value() || contains(monster.aurasOff, trait.name)) {
            continue;
        }
        for (const std::string& action : trait.aura->suppressedBy) {
            if (sameAction(action, actionName)) {
                monster.aurasOff.push_back(trait.name);
                off.push_back(trait.name);
                break;
            }
        }
    }
    return off;
}

// --- Spell slots and rests --------------------------------------------------

bool spendSpellSlot(Character& character, int level)
{
    for (SpellSlot& slot : character.spellSlots) {
        if (slot.level != level) {
            continue;
        }
        if (slot.current < 1) {
            return false;
        }
        slot.current -= 1;
        return true;
    }
    return false;
}

int hitDiceRemaining(const ClassLevel& row)
{
    return std::max(0, row.level - row.hitDiceSpent);
}

void longRest(Character& character)
{
    character.hp.current = character.hp.max;
    character.tempHp = 0;
    for (SpellSlot& slot : character.spellSlots) {
        slot.current = slot.max;
    }
    for (ClassLevel& row : character.classes) {
        row.hitDiceSpent = 0;
    }
    character.exhaustion = std::max(0, character.exhaustion - 1);
}

ShortRestResult shortRest(Character& character, const std::vector<int>& diceByClass, const RollDie& rollDie)
{
    ShortRestResult result;
    const int constitution = abilityModifier(character.abilities.constitution);
    long long healed = 0;
    for (std::size_t i = 0; i < character.classes.size() && i < diceByClass.size(); ++i) {
        ClassLevel& row = character.classes[i];
        const int spend = std::clamp(diceByClass[i], 0, hitDiceRemaining(row));
        const int die = hitDieForClass(row.name);
        for (int d = 0; d < spend; ++d) {
            const int face = rollDie ? rollDie(die) : (die / 2 + 1);
            healed += std::max(0, face + constitution);
        }
        row.hitDiceSpent += spend;
        result.diceSpent += spend;
    }
    const int before = character.hp.current;
    character.hp.current = clampToInt(std::min<long long>(character.hp.max, before + healed));
    result.healed = std::max(0, character.hp.current - before);
    for (SpellSlot& slot : character.spellSlots) {
        if (slot.shortRest) {
            slot.current = slot.max;
        }
    }
    return result;
}

}  // namespace combat
