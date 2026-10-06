#include "data/json_encounters.h"

#include "core/combat_rules.h"

#include "data/json_monster_codec.h"
#include "data/json_util.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace combat {

namespace {

using json_util::json;
using Error = EncounterStoreError;

void throwProblems(const std::vector<std::string>& problems, const std::string& context)
{
    if (!problems.empty()) {
        throw Error(context + ": " + problems.front());
    }
}

Ability readAbility(const json& object, const char* key, const std::string& context)
{
    const std::string name = json_util::readString<Error>(object, key, context);
    const auto ability = abilityFromKey(name);
    if (!ability.has_value()) {
        throw Error(context + ": unknown ability \"" + name + "\".");
    }
    return *ability;
}

// Files before version 4 did not say what ends a condition. Invisible from
// then is treated as the Hide action's.
void fillOldCondition(ActiveCondition& condition, int schemaVersion)
{
    if (schemaVersion < 4 && condition.id == "invisible" && condition.endsOn.empty()) {
        condition.source = "Hide";
        condition.endsOn = hideEndsOn();
    }
}

ActiveCondition conditionFromJson(const json& value, const std::string& context, int schemaVersion)
{
    ActiveCondition condition;
    if (value.is_string()) {
        condition.id = value.get<std::string>();
        fillOldCondition(condition, schemaVersion);
        return condition;
    }
    json_util::requireObject<Error>(value, context);
    condition.id = json_util::readString<Error>(value, "id", context);
    const auto duration = value.find("duration");
    if (duration != value.end() && !duration->is_null()) {
        const std::string durationContext = context + " duration";
        json_util::requireObject<Error>(*duration, durationContext);
        ConditionDuration row;
        row.anchorId = json_util::readString<Error>(*duration, "anchorId", durationContext);
        const std::string boundary = json_util::readString<Error>(*duration, "boundary", durationContext);
        if (boundary != "start" && boundary != "end") {
            throw Error(durationContext + ": boundary must be \"start\" or \"end\".");
        }
        row.boundary = boundary == "start" ? TurnBoundary::Start : TurnBoundary::End;
        row.turnsRemaining = std::max(1, json_util::readInt<Error>(*duration, "turns", durationContext));
        row.skipNext = json_util::readBoolOr<Error>(*duration, "skipNext", false, durationContext);
        condition.duration = row;
    }
    const auto saveEnds = value.find("saveEnds");
    if (saveEnds != value.end() && !saveEnds->is_null()) {
        const std::string saveContext = context + " saveEnds";
        json_util::requireObject<Error>(*saveEnds, saveContext);
        SaveEnds row{readAbility(*saveEnds, "ability", saveContext),
                     json_util::readInt<Error>(*saveEnds, "dc", saveContext)};
        row.worsensTo = json_util::readStringList<Error>(*saveEnds, "worsensTo", saveContext);
        row.worseSaveEnds = json_util::readBoolOr<Error>(*saveEnds, "worseSaveEnds", false, saveContext);
        row.worseEndsOn = json_util::readStringList<Error>(*saveEnds, "worseEndsOn", saveContext);
        condition.saveEnds = std::move(row);
    }
    condition.byId = json_util::readOptionalString<Error>(value, "byId", context);
    condition.tiedTo = json_util::readOptionalString<Error>(value, "tiedTo", context);
    condition.ongoingAt = json_util::readOptionalString<Error>(value, "ongoingAt", context);
    if (const auto ongoing = value.find("ongoing"); ongoing != value.end() && ongoing->is_array()) {
        for (const json& part : *ongoing) {
            const std::string partContext = context + " ongoing";
            json_util::requireObject<Error>(part, partContext);
            DamagePart row;
            row.dice = json_util::readString<Error>(part, "dice", partContext);
            row.type = json_util::readOptionalString<Error>(part, "type", partContext);
            if (!parseDice(row.dice).has_value()) {
                throw Error(partContext + ": \"" + row.dice + "\" is not a dice expression.");
            }
            condition.ongoing.push_back(std::move(row));
        }
    }
    condition.source = json_util::readOptionalString<Error>(value, "source", context);
    condition.endsOn = json_util::readStringList<Error>(value, "endsOn", context);
    condition.concentration = json_util::readOptionalString<Error>(value, "concentration", context);
    fillOldCondition(condition, schemaVersion);
    return condition;
}

json conditionToJson(const ActiveCondition& condition)
{
    json value{{"id", condition.id}};
    if (condition.duration.has_value()) {
        value["duration"] = json{{"anchorId", condition.duration->anchorId},
                                 {"boundary", condition.duration->boundary == TurnBoundary::Start ? "start" : "end"},
                                 {"turns", condition.duration->turnsRemaining},
                                 {"skipNext", condition.duration->skipNext}};
    }
    if (condition.saveEnds.has_value()) {
        json saveEnds{{"ability", abilityKey(condition.saveEnds->ability)}, {"dc", condition.saveEnds->dc}};
        if (!condition.saveEnds->worsensTo.empty()) {
            saveEnds["worsensTo"] = condition.saveEnds->worsensTo;
        }
        if (condition.saveEnds->worseSaveEnds) {
            saveEnds["worseSaveEnds"] = true;
        }
        if (!condition.saveEnds->worseEndsOn.empty()) {
            saveEnds["worseEndsOn"] = condition.saveEnds->worseEndsOn;
        }
        value["saveEnds"] = std::move(saveEnds);
    }
    if (!condition.byId.empty()) {
        value["byId"] = condition.byId;
    }
    if (!condition.tiedTo.empty()) {
        value["tiedTo"] = condition.tiedTo;
    }
    if (!condition.ongoing.empty()) {
        json parts = json::array();
        for (const DamagePart& part : condition.ongoing) {
            parts.push_back(json{{"dice", part.dice}, {"type", part.type}});
        }
        value["ongoing"] = std::move(parts);
    }
    if (!condition.ongoingAt.empty()) {
        value["ongoingAt"] = condition.ongoingAt;
    }
    if (!condition.source.empty()) {
        value["source"] = condition.source;
    }
    if (!condition.endsOn.empty()) {
        value["endsOn"] = condition.endsOn;
    }
    if (!condition.concentration.empty()) {
        value["concentration"] = condition.concentration;
    }
    return value;
}

void readVersion3(Combatant& combatant, const json& value, const std::string& context)
{
    combatant.stable = json_util::readBoolOr<Error>(value, "stable", false, context);
    combatant.dead = json_util::readBoolOr<Error>(value, "dead", false, context);
    combatant.exhaustion = std::clamp(json_util::readIntOr<Error>(value, "exhaustion", 0, context), 0, 6);
    const auto defenses = value.find("defenses");
    if (defenses != value.end() && !defenses->is_null()) {
        const std::string defenseContext = context + " defenses";
        json_util::requireObject<Error>(*defenses, defenseContext);
        combatant.defenses.resistances =
            json_util::readStringList<Error>(*defenses, "resistances", defenseContext);
        combatant.defenses.immunities =
            json_util::readStringList<Error>(*defenses, "immunities", defenseContext);
        combatant.defenses.vulnerabilities =
            json_util::readStringList<Error>(*defenses, "vulnerabilities", defenseContext);
    }
    combatant.conditionImmunities = json_util::readStringList<Error>(value, "conditionImmunities", context);
    const auto saves = value.find("saveBonuses");
    if (saves != value.end() && !saves->is_null()) {
        const std::string saveContext = context + " saveBonuses";
        json_util::requireObject<Error>(*saves, saveContext);
        for (const Ability ability : kAbilityOrder) {
            combatant.saveBonuses[static_cast<std::size_t>(ability)] =
                json_util::readIntOr<Error>(*saves, abilityKey(ability), 0, saveContext);
        }
    }
    const auto economy = value.find("economy");
    if (economy != value.end() && !economy->is_null()) {
        const std::string economyContext = context + " economy";
        json_util::requireObject<Error>(*economy, economyContext);
        TurnEconomy& row = combatant.economy;
        row.actionUsed = json_util::readBoolOr<Error>(*economy, "actionUsed", false, economyContext);
        row.bonusActionUsed = json_util::readBoolOr<Error>(*economy, "bonusActionUsed", false, economyContext);
        row.reactionUsed = json_util::readBoolOr<Error>(*economy, "reactionUsed", false, economyContext);
        row.attacksRemaining =
            std::max(0, json_util::readIntOr<Error>(*economy, "attacksRemaining", 0, economyContext));
        row.legendaryRemaining =
            std::max(0, json_util::readIntOr<Error>(*economy, "legendaryRemaining", 0, economyContext));
        row.grantedAttack = json_util::readOptionalString<Error>(*economy, "grantedAttack", economyContext);
        row.triggered = json_util::readStringList<Error>(*economy, "triggered", economyContext);
        if (const auto left = economy->find("multiattackLeft"); left != economy->end() && left->is_object()) {
            for (auto it = left->begin(); it != left->end(); ++it) {
                row.multiattackLeft[it.key()] =
                    std::max(0, json_util::readInt<Error>(*left, it.key().c_str(), economyContext));
            }
        }
    }
    combatant.expended = json_util::readStringList<Error>(value, "expended", context);
    const auto uses = value.find("usesRemaining");
    if (uses != value.end() && !uses->is_null()) {
        const std::string usesContext = context + " usesRemaining";
        json_util::requireObject<Error>(*uses, usesContext);
        for (auto it = uses->begin(); it != uses->end(); ++it) {
            combatant.usesRemaining[it.key()] =
                std::max(0, json_util::integerValue<Error>(it.value(), it.key().c_str(), usesContext));
        }
    }
    combatant.aurasOff = json_util::readStringList<Error>(value, "aurasOff", context);
    combatant.auraImmunities = json_util::readStringList<Error>(value, "auraImmunities", context);
    combatant.maxHpReduction = std::max(0, json_util::readIntOr<Error>(value, "maxHpReduction", 0, context));
    if (const auto timed = value.find("timedEffects"); timed != value.end() && timed->is_array()) {
        for (const json& row : *timed) {
            const std::string rowContext = context + " timedEffects";
            json_util::requireObject<Error>(row, rowContext);
            ConditionDuration duration;
            duration.anchorId = json_util::readString<Error>(row, "anchorId", rowContext);
            duration.boundary = json_util::readString<Error>(row, "boundary", rowContext) == "start"
                                    ? TurnBoundary::Start
                                    : TurnBoundary::End;
            duration.turnsRemaining = std::max(1, json_util::readIntOr<Error>(row, "turns", 1, rowContext));
            duration.skipNext = json_util::readBoolOr<Error>(row, "skipNext", false, rowContext);
            combatant.timedEffects.emplace_back(json_util::readString<Error>(row, "name", rowContext), duration);
        }
    }
    const auto block = value.find("statBlock");
    if (block != value.end() && !block->is_null()) {
        try {
            combatant.statBlock = json_codec::monsterFromJson(*block, context + " statBlock");
        } catch (const MonsterDataError& error) {
            throw Error(error.what());
        }
    }
}

void readBookkeeping(Combatant& combatant, const json& value, const std::string& context, int schemaVersion)
{
    if (schemaVersion == 1) {
        return;
    }
    combatant.tempHp = std::max(0, json_util::readInt<Error>(value, "tempHp", context));
    const json& maxHp = json_util::requireField<Error>(value, "maxHp", context);
    if (maxHp.is_null()) {
        combatant.maxHp = std::nullopt;
    } else {
        combatant.maxHp = std::max(0, json_util::integerValue<Error>(maxHp, "maxHp", context));
    }

    const json& conditions = json_util::requireArray<Error>(value, "conditions", context);
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        ActiveCondition condition =
            conditionFromJson(conditions[i], context + " condition " + std::to_string(i + 1), schemaVersion);
        if (condition.id == "exhaustion") {
            combatant.exhaustion = std::min(6, combatant.exhaustion + 1);
            continue;
        }
        combatant.conditions.push_back(std::move(condition));
    }
    combatant.concentration = json_util::readString<Error>(value, "concentration", context);
    const json& deathSaves = json_util::requireField<Error>(value, "deathSaves", context);
    json_util::requireObject<Error>(deathSaves, context + " deathSaves");
    const std::string deathContext = context + " deathSaves";
    combatant.deathSaves.successes =
        std::clamp(json_util::readInt<Error>(deathSaves, "successes", deathContext), 0, 3);
    combatant.deathSaves.failures = std::clamp(json_util::readInt<Error>(deathSaves, "failures", deathContext), 0, 3);
    if (schemaVersion >= 3) {
        readVersion3(combatant, value, context);
    }
}

Combatant combatantFromJson(const json& value, std::size_t index, int schemaVersion)
{
    const std::string context = "Combatant " + std::to_string(index + 1);
    json_util::requireObject<Error>(value, context);

    Combatant combatant;
    combatant.id = json_util::readString<Error>(value, "id", context);
    combatant.source = json_util::readString<Error>(value, "source", context);
    combatant.sourceId = json_util::readString<Error>(value, "sourceId", context);
    combatant.name = json_util::readString<Error>(value, "name", context);
    combatant.initiative = json_util::readInt<Error>(value, "initiative", context);
    if (combatant.source == kCombatantSourceCharacter && value.contains("initiativeBonus")) {
        throw Error(context + ": field \"initiativeBonus\" is only stored on monsters.");
    }
    if (combatant.source == kCombatantSourceMonster) {
        combatant.initiativeBonus = json_util::readOptionalInt<Error>(value, "initiativeBonus", context);
    }
    combatant.hp = std::max(0, json_util::readInt<Error>(value, "hp", context));
    combatant.ac = json_util::readInt<Error>(value, "ac", context);
    readBookkeeping(combatant, value, context, schemaVersion);
    throwProblems(validateCombatant(combatant), context);
    return combatant;
}

json combatantToJson(const Combatant& combatant)
{
    json value{
        {"id", combatant.id},
        {"source", combatant.source},
        {"sourceId", combatant.sourceId},
        {"name", combatant.name},
        {"initiative", combatant.initiative},
    };
    if (combatant.source == kCombatantSourceMonster && combatant.initiativeBonus.has_value()) {
        value["initiativeBonus"] = *combatant.initiativeBonus;
    }
    value["hp"] = combatant.hp;
    if (combatant.maxHp.has_value()) {
        value["maxHp"] = *combatant.maxHp;
    } else {
        value["maxHp"] = nullptr;
    }
    value["tempHp"] = combatant.tempHp;
    value["ac"] = combatant.ac;
    json conditions = json::array();
    for (const ActiveCondition& condition : combatant.conditions) {
        conditions.push_back(conditionToJson(condition));
    }
    value["conditions"] = std::move(conditions);
    value["concentration"] = combatant.concentration;
    value["deathSaves"] = {
        {"successes", combatant.deathSaves.successes},
        {"failures", combatant.deathSaves.failures},
    };
    value["stable"] = combatant.stable;
    value["dead"] = combatant.dead;
    value["exhaustion"] = combatant.exhaustion;
    value["defenses"] = json{{"resistances", combatant.defenses.resistances},
                             {"immunities", combatant.defenses.immunities},
                             {"vulnerabilities", combatant.defenses.vulnerabilities}};
    value["conditionImmunities"] = combatant.conditionImmunities;
    json saves = json::object();
    for (const Ability ability : kAbilityOrder) {
        saves[abilityKey(ability)] = combatant.saveBonuses[static_cast<std::size_t>(ability)];
    }
    value["saveBonuses"] = std::move(saves);
    const TurnEconomy& economy = combatant.economy;
    value["economy"] = json{{"actionUsed", economy.actionUsed},
                            {"bonusActionUsed", economy.bonusActionUsed},
                            {"reactionUsed", economy.reactionUsed},
                            {"attacksRemaining", economy.attacksRemaining},
                            {"legendaryRemaining", economy.legendaryRemaining},
                            {"grantedAttack", economy.grantedAttack},
                            {"triggered", economy.triggered}};
    if (!economy.multiattackLeft.empty()) {
        json left = json::object();
        for (const auto& [name, count] : economy.multiattackLeft) {
            left[name] = count;
        }
        value["economy"]["multiattackLeft"] = std::move(left);
    }
    value["expended"] = combatant.expended;
    json uses = json::object();
    for (const auto& [name, left] : combatant.usesRemaining) {
        uses[name] = left;
    }
    value["usesRemaining"] = std::move(uses);
    if (!combatant.aurasOff.empty()) {
        value["aurasOff"] = combatant.aurasOff;
    }
    if (combatant.maxHpReduction > 0) {
        value["maxHpReduction"] = combatant.maxHpReduction;
    }
    if (!combatant.timedEffects.empty()) {
        json timed = json::array();
        for (const auto& [name, duration] : combatant.timedEffects) {
            timed.push_back(json{{"name", name},
                                 {"anchorId", duration.anchorId},
                                 {"boundary", duration.boundary == TurnBoundary::Start ? "start" : "end"},
                                 {"turns", duration.turnsRemaining},
                                 {"skipNext", duration.skipNext}});
        }
        value["timedEffects"] = std::move(timed);
    }
    if (!combatant.auraImmunities.empty()) {
        value["auraImmunities"] = combatant.auraImmunities;
    }
    if (combatant.statBlock.has_value()) {
        value["statBlock"] = json_codec::monsterToJson(*combatant.statBlock);
    }
    return value;
}

Encounter encounterFromJson(const json& value, std::size_t index, int schemaVersion)
{
    const std::string context = "Encounter " + std::to_string(index + 1);
    json_util::requireObject<Error>(value, context);

    Encounter encounter;
    encounter.id = json_util::readString<Error>(value, "id", context);
    encounter.name = json_util::readString<Error>(value, "name", context);
    encounter.round = json_util::readInt<Error>(value, "round", context);
    encounter.turnIndex = json_util::readInt<Error>(value, "turnIndex", context);
    encounter.started = json_util::readBoolOr<Error>(value, "started", true, context);

    const json& list = json_util::requireArray<Error>(value, "combatants", context);
    encounter.combatants.reserve(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        encounter.combatants.push_back(combatantFromJson(list[i], i, schemaVersion));
    }
    throwProblems(validateEncounter(encounter), context);
    return encounter;
}

json encounterToJson(const Encounter& encounter)
{
    json list = json::array();
    for (const Combatant& combatant : encounter.combatants) {
        list.push_back(combatantToJson(combatant));
    }
    return json{
        {"id", encounter.id},
        {"name", encounter.name},
        {"round", encounter.round},
        {"turnIndex", encounter.turnIndex},
        {"started", encounter.started},
        {"combatants", std::move(list)},
    };
}

void rejectDuplicateEncounterIds(const std::vector<Encounter>& encounters)
{
    std::unordered_set<std::string> ids;
    for (const Encounter& encounter : encounters) {
        if (!ids.insert(encounter.id).second) {
            throw Error("Two encounters share the id \"" + encounter.id + "\".");
        }
    }
}

}  // namespace

std::vector<Encounter> parseEncountersDocument(const std::string& text)
{
    const json document = json_util::parseDocument<Error>(text, "encounters");
    const std::string context = "Encounters file";
    const int version = json_util::readInt<Error>(document, "schemaVersion", context);
    if (version < 1 || version > kEncounterSchemaVersion) {
        throw Error("The encounters file has schemaVersion " + std::to_string(version) +
                    ", which this version of the app cannot read.");
    }

    const json& list = json_util::requireArray<Error>(document, "encounters", context);
    std::vector<Encounter> encounters;
    encounters.reserve(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        encounters.push_back(encounterFromJson(list[i], i, version));
    }
    rejectDuplicateEncounterIds(encounters);
    return encounters;
}

std::string serializeEncountersDocument(const std::vector<Encounter>& encounters)
{
    json list = json::array();
    for (const Encounter& encounter : encounters) {
        list.push_back(encounterToJson(encounter));
    }
    const json document{
        {"schemaVersion", kEncounterSchemaVersion},
        {"encounters", std::move(list)},
    };
    return document.dump(2) + "\n";
}

JsonEncounterStore::JsonEncounterStore(std::filesystem::path path)
    : m_path(std::move(path))
{
}

std::vector<Encounter> JsonEncounterStore::loadAll()
{
    const std::optional<std::string> text = json_util::readTextFile<Error>(m_path, true);
    if (!text.has_value()) {
        return {};
    }
    return parseEncountersDocument(*text);
}

void JsonEncounterStore::saveAll(const std::vector<Encounter>& encounters)
{
    for (std::size_t i = 0; i < encounters.size(); ++i) {
        const auto problems = validateEncounter(encounters[i]);
        if (!problems.empty()) {
            throw Error("Encounter " + std::to_string(i + 1) + ": " + problems.front());
        }
    }
    rejectDuplicateEncounterIds(encounters);
    json_util::writeFileAtomically<Error>(m_path, serializeEncountersDocument(encounters));
}

}  // namespace combat
