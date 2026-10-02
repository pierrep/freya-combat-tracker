#include "data/json_encounters.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace combat {

namespace {

using json = nlohmann::ordered_json;

const json& requireField(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        throw EncounterStoreError(context + ": missing field \"" + key + "\".");
    }
    return *it;
}

int integerFromJson(const json& value, const char* key, const std::string& context)
{
    if (!value.is_number_integer()) {
        throw EncounterStoreError(context + ": field \"" + key + "\" must be an integer.");
    }
    if (value.is_number_unsigned()) {
        const auto raw = value.get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            throw EncounterStoreError(context + ": field \"" + key + "\" is out of range.");
        }
        return static_cast<int>(raw);
    }
    const auto raw = value.get<std::int64_t>();
    if (raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) {
        throw EncounterStoreError(context + ": field \"" + key + "\" is out of range.");
    }
    return static_cast<int>(raw);
}

int readInt(const json& object, const char* key, const std::string& context)
{
    return integerFromJson(requireField(object, key, context), key, context);
}

std::optional<int> readOptionalInt(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    return integerFromJson(*it, key, context);
}

std::string readString(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField(object, key, context);
    if (!value.is_string()) {
        throw EncounterStoreError(context + ": field \"" + key + "\" must be a string.");
    }
    return value.get<std::string>();
}

void throwProblems(const std::vector<std::string>& problems, const std::string& context)
{
    if (!problems.empty()) {
        throw EncounterStoreError(context + ": " + problems.front());
    }
}

void readBookkeeping(Combatant& combatant, const json& value, const std::string& context, int schemaVersion)
{
    if (schemaVersion == 1) {
        return;
    }
    combatant.tempHp = readInt(value, "tempHp", context);
    const json& maxHp = requireField(value, "maxHp", context);
    if (maxHp.is_null()) {
        combatant.maxHp = std::nullopt;
    } else {
        combatant.maxHp = integerFromJson(maxHp, "maxHp", context);
    }

    const json& conditions = requireField(value, "conditions", context);
    if (!conditions.is_array()) {
        throw EncounterStoreError(context + ": field \"conditions\" must be an array.");
    }
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        if (!conditions[i].is_string()) {
            throw EncounterStoreError(context + " condition " + std::to_string(i + 1) + ": must be a string.");
        }
        combatant.conditions.push_back(conditions[i].get<std::string>());
    }
    combatant.concentration = readString(value, "concentration", context);
    const json& deathSaves = requireField(value, "deathSaves", context);
    if (!deathSaves.is_object()) {
        throw EncounterStoreError(context + ": field \"deathSaves\" must be an object.");
    }
    const std::string deathContext = context + " deathSaves";
    combatant.deathSaves.successes = readInt(deathSaves, "successes", deathContext);
    combatant.deathSaves.failures = readInt(deathSaves, "failures", deathContext);
}

Combatant combatantFromJson(const json& value, std::size_t index, int schemaVersion)
{
    const std::string context = "Combatant " + std::to_string(index + 1);
    if (!value.is_object()) {
        throw EncounterStoreError(context + ": must be an object.");
    }

    Combatant combatant;
    combatant.id = readString(value, "id", context);
    combatant.source = readString(value, "source", context);
    combatant.sourceId = readString(value, "sourceId", context);
    combatant.name = readString(value, "name", context);
    combatant.initiative = readInt(value, "initiative", context);
    if (combatant.source == kCombatantSourceCharacter && value.contains("initiativeBonus")) {
        throw EncounterStoreError(context + ": field \"initiativeBonus\" is only stored on monsters.");
    }
    if (combatant.source == kCombatantSourceMonster) {
        combatant.initiativeBonus = readOptionalInt(value, "initiativeBonus", context);
    }
    combatant.hp = readInt(value, "hp", context);
    combatant.ac = readInt(value, "ac", context);
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
    for (const std::string& id : combatant.conditions) {
        conditions.push_back(id);
    }
    value["conditions"] = std::move(conditions);
    value["concentration"] = combatant.concentration;
    value["deathSaves"] = {
        {"successes", combatant.deathSaves.successes},
        {"failures", combatant.deathSaves.failures},
    };
    return value;
}

Encounter encounterFromJson(const json& value, std::size_t index, int schemaVersion)
{
    const std::string context = "Encounter " + std::to_string(index + 1);
    if (!value.is_object()) {
        throw EncounterStoreError(context + ": must be an object.");
    }

    Encounter encounter;
    encounter.id = readString(value, "id", context);
    encounter.name = readString(value, "name", context);
    encounter.round = readInt(value, "round", context);
    encounter.turnIndex = readInt(value, "turnIndex", context);

    const json& list = requireField(value, "combatants", context);
    if (!list.is_array()) {
        throw EncounterStoreError(context + ": field \"combatants\" must be an array.");
    }
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
        {"combatants", std::move(list)},
    };
}

void rejectDuplicateEncounterIds(const std::vector<Encounter>& encounters)
{
    std::unordered_set<std::string> ids;
    for (const Encounter& encounter : encounters) {
        if (!ids.insert(encounter.id).second) {
            throw EncounterStoreError("Two encounters share the id \"" + encounter.id + "\".");
        }
    }
}

}  // namespace

std::vector<Encounter> parseEncountersDocument(const std::string& text)
{
    json document;
    try {
        document = json::parse(text);
    } catch (const json::parse_error& error) {
        throw EncounterStoreError(std::string("The encounters file is not valid JSON: ") + error.what());
    }

    if (!document.is_object()) {
        throw EncounterStoreError("The encounters file must contain a JSON object.");
    }
    const std::string context = "Encounters file";
    const int version = readInt(document, "schemaVersion", context);
    if (version != 1 && version != kEncounterSchemaVersion) {
        throw EncounterStoreError("The encounters file has schemaVersion " + std::to_string(version) +
                                  ", which this version of the app cannot read.");
    }

    const json& list = requireField(document, "encounters", context);
    if (!list.is_array()) {
        throw EncounterStoreError("Encounters file: field \"encounters\" must be an array.");
    }

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
    std::error_code ec;
    if (!std::filesystem::exists(m_path, ec)) {
        if (ec) {
            throw EncounterStoreError("Could not check " + m_path.string() + ": " + ec.message());
        }
        return {};
    }

    std::ifstream in(m_path, std::ios::binary);
    if (!in) {
        throw EncounterStoreError("Could not open " + m_path.string() + " for reading.");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw EncounterStoreError("Could not read " + m_path.string() + ".");
    }
    return parseEncountersDocument(buffer.str());
}

void JsonEncounterStore::saveAll(const std::vector<Encounter>& encounters)
{
    for (std::size_t i = 0; i < encounters.size(); ++i) {
        const auto problems = validateEncounter(encounters[i]);
        if (!problems.empty()) {
            throw EncounterStoreError("Encounter " + std::to_string(i + 1) + ": " + problems.front());
        }
    }
    rejectDuplicateEncounterIds(encounters);

    const std::string text = serializeEncountersDocument(encounters);

    std::error_code ec;
    const auto folder = m_path.parent_path();
    if (!folder.empty()) {
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            throw EncounterStoreError("Could not create " + folder.string() + ": " + ec.message());
        }
    }

    auto tempPath = m_path;
    tempPath += ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw EncounterStoreError("Could not open " + tempPath.string() + " for writing.");
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tempPath, ec);
            throw EncounterStoreError("Could not write " + tempPath.string() + ".");
        }
    }

    std::filesystem::rename(tempPath, m_path, ec);
    if (ec) {
        const std::string message = ec.message();
        std::filesystem::remove(tempPath, ec);
        throw EncounterStoreError("Could not replace " + m_path.string() + ": " + message);
    }
}

}  // namespace combat
