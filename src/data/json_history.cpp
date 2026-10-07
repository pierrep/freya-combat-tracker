#include "data/json_history.h"

#include "data/json_character_store.h"
#include "data/json_encounters.h"
#include "data/json_monster_codec.h"
#include "data/json_util.h"

namespace combat {

namespace {

using json_util::json;
using Error = HistoryError;

constexpr int kHistorySchemaVersion = 1;

// Encounters and characters go through their own documents, so their
// versions and migrations apply here as in their files.
json encountersJson(const std::vector<Encounter>& encounters)
{
    return json::parse(serializeEncountersDocument(encounters));
}

std::vector<Encounter> encountersFrom(const json& value)
{
    try {
        return parseEncountersDocument(value.dump());
    } catch (const std::exception& error) {
        throw Error(std::string("history: ") + error.what());
    }
}

json rosterJson(const std::vector<Character>& roster)
{
    return json::parse(serializeCharactersDocument(roster));
}

std::vector<Character> rosterFrom(const json& value)
{
    try {
        return parseCharactersDocument(value.dump());
    } catch (const std::exception& error) {
        throw Error(std::string("history: ") + error.what());
    }
}

// An action rides inside a one-action monster, the codec's unit.
json attackJson(const MonsterAttack& attack)
{
    Monster holder;
    holder.id = "history";
    holder.name = "history";
    holder.source = "history";
    holder.attacks = {attack};
    return json_codec::monsterToJson(holder);
}

MonsterAttack attackFrom(const json& value)
{
    Monster holder;
    try {
        holder = json_codec::monsterFromJson(value, "history prompt action");
    } catch (const std::exception& error) {
        throw Error(error.what());
    }
    if (holder.attacks.size() != 1) {
        throw Error("history prompt action: expected one action.");
    }
    return holder.attacks.front();
}

json promptJson(const HistoryPrompt& prompt)
{
    json row = json::object();
    row["kind"] = prompt.kind;
    row["combatantId"] = prompt.combatantId;
    row["conditionId"] = prompt.conditionId;
    row["sourceId"] = prompt.sourceId;
    row["auraName"] = prompt.auraName;
    row["ability"] = prompt.ability;
    row["dc"] = prompt.dc;
    if (prompt.attack.has_value()) {
        row["attack"] = attackJson(*prompt.attack);
    }
    json riders = json::array();
    for (const std::size_t index : prompt.riders) {
        riders.push_back(index);
    }
    row["riders"] = riders;
    row["refund"] = prompt.refund;
    if (!prompt.damage.empty()) {
        json damage = json::array();
        for (const auto& [amount, type] : prompt.damage) {
            damage.push_back(json{{"amount", amount}, {"type", type}});
        }
        row["damage"] = std::move(damage);
    }
    if (prompt.advantage) {
        row["advantage"] = true;
    }
    if (prompt.afterHit) {
        row["afterHit"] = true;
    }
    if (prompt.wasBloodied) {
        row["wasBloodied"] = true;
    }
    return row;
}

HistoryPrompt promptFrom(const json& row, const std::string& context)
{
    json_util::requireObject<Error>(row, context);
    HistoryPrompt prompt;
    prompt.kind = json_util::readInt<Error>(row, "kind", context);
    prompt.combatantId = json_util::readString<Error>(row, "combatantId", context);
    prompt.conditionId = json_util::readOptionalString<Error>(row, "conditionId", context);
    prompt.sourceId = json_util::readOptionalString<Error>(row, "sourceId", context);
    prompt.auraName = json_util::readOptionalString<Error>(row, "auraName", context);
    prompt.ability = json_util::readIntOr<Error>(row, "ability", 0, context);
    prompt.dc = json_util::readIntOr<Error>(row, "dc", 10, context);
    if (const auto it = row.find("attack"); it != row.end()) {
        prompt.attack = attackFrom(*it);
    }
    if (const auto it = row.find("riders"); it != row.end() && it->is_array()) {
        for (const json& index : *it) {
            if (!index.is_number_unsigned()) {
                throw Error(context + ": riders must be whole numbers.");
            }
            prompt.riders.push_back(index.get<std::size_t>());
        }
    }
    prompt.refund = json_util::readIntOr<Error>(row, "refund", 0, context);
    if (const auto damage = row.find("damage"); damage != row.end()) {
        if (!damage->is_array()) {
            throw Error(context + ": field \"damage\" must be an array.");
        }
        for (std::size_t i = 0; i < damage->size(); ++i) {
            const std::string partContext = context + " damage " + std::to_string(i + 1);
            json_util::requireObject<Error>((*damage)[i], partContext);
            prompt.damage.emplace_back(json_util::readInt<Error>((*damage)[i], "amount", partContext),
                                       json_util::readOptionalString<Error>((*damage)[i], "type", partContext));
        }
    }
    prompt.advantage = json_util::readBoolOr<Error>(row, "advantage", false, context);
    prompt.afterHit = json_util::readBoolOr<Error>(row, "afterHit", false, context);
    prompt.wasBloodied = json_util::readBoolOr<Error>(row, "wasBloodied", false, context);
    return prompt;
}

json promptsJson(const std::vector<HistoryPrompt>& prompts)
{
    json rows = json::array();
    for (const HistoryPrompt& prompt : prompts) {
        rows.push_back(promptJson(prompt));
    }
    return rows;
}

std::vector<HistoryPrompt> promptsFrom(const json& object, const std::string& context)
{
    std::vector<HistoryPrompt> prompts;
    const auto it = object.find("prompts");
    if (it == object.end()) {
        return prompts;
    }
    if (!it->is_array()) {
        throw Error(context + ": field \"prompts\" must be an array.");
    }
    for (std::size_t i = 0; i < it->size(); ++i) {
        prompts.push_back(promptFrom((*it)[i], context + " prompt " + std::to_string(i + 1)));
    }
    return prompts;
}

json logJson(const std::vector<std::string>& lines)
{
    json rows = json::array();
    for (const std::string& line : lines) {
        rows.push_back(line);
    }
    return rows;
}

}  // namespace

std::string serializeHistory(const FightHistory& history)
{
    json document = json::object();
    document["schemaVersion"] = kHistorySchemaVersion;
    document["roster"] = rosterJson(history.roster);
    document["encounters"] = encountersJson(history.encounters);
    document["shownEncounterId"] = history.shownEncounterId;
    document["log"] = logJson(history.log);
    document["prompts"] = promptsJson(history.prompts);
    json steps = json::array();
    for (const HistoryStep& step : history.steps) {
        json row = json::object();
        row["encounterId"] = step.encounterId;
        row["encounter"] = encountersJson({step.encounter});
        row["roster"] = rosterJson(step.roster);
        row["log"] = logJson(step.log);
        row["prompts"] = promptsJson(step.prompts);
        row["selectionId"] = step.selectionId;
        steps.push_back(row);
    }
    document["steps"] = steps;
    return document.dump(1) + "\n";
}

FightHistory parseHistory(const std::string& text)
{
    const json document = json_util::parseDocument<Error>(text, "history");
    const int version = json_util::readInt<Error>(document, "schemaVersion", "history");
    if (version != kHistorySchemaVersion) {
        throw Error("history: unknown schemaVersion " + std::to_string(version) + ".");
    }
    FightHistory history;
    history.roster = rosterFrom(json_util::requireField<Error>(document, "roster", "history"));
    history.encounters = encountersFrom(json_util::requireField<Error>(document, "encounters", "history"));
    history.shownEncounterId = json_util::readOptionalString<Error>(document, "shownEncounterId", "history");
    history.log = json_util::readStringList<Error>(document, "log", "history");
    history.prompts = promptsFrom(document, "history");
    const auto stepsIt = document.find("steps");
    if (stepsIt == document.end() || !stepsIt->is_array()) {
        throw Error("history: field \"steps\" must be an array.");
    }
    const json& steps = *stepsIt;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const std::string context = "history step " + std::to_string(i + 1);
        const json& row = steps[i];
        json_util::requireObject<Error>(row, context);
        HistoryStep step;
        step.encounterId = json_util::readString<Error>(row, "encounterId", context);
        const std::vector<Encounter> encounter =
            encountersFrom(json_util::requireField<Error>(row, "encounter", context));
        if (encounter.size() != 1) {
            throw Error(context + ": expected one encounter.");
        }
        step.encounter = encounter.front();
        step.roster = rosterFrom(json_util::requireField<Error>(row, "roster", context));
        step.log = json_util::readStringList<Error>(row, "log", context);
        step.prompts = promptsFrom(row, context);
        step.selectionId = json_util::readOptionalString<Error>(row, "selectionId", context);
        history.steps.push_back(std::move(step));
    }
    return history;
}

std::optional<FightHistory> loadHistoryFile(const std::filesystem::path& path)
{
    try {
        const std::optional<std::string> text = json_util::readTextFile<Error>(path, true);
        if (!text.has_value()) {
            return std::nullopt;
        }
        return parseHistory(*text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void saveHistoryFile(const std::filesystem::path& path, const FightHistory& history)
{
    json_util::writeFileAtomically<Error>(path, serializeHistory(history));
}

}  // namespace combat
