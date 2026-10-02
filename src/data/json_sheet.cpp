#include "data/json_sheet.h"

#include "core/monster.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace combat {

namespace {

using json = nlohmann::ordered_json;

const json& requireField(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        throw CatalogError(context + ": missing field \"" + key + "\".");
    }
    return *it;
}

int readInt(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField(object, key, context);
    if (!value.is_number_integer()) {
        throw CatalogError(context + ": field \"" + key + "\" must be an integer.");
    }
    if (value.is_number_unsigned()) {
        const auto raw = value.get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            throw CatalogError(context + ": field \"" + key + "\" is out of range.");
        }
        return static_cast<int>(raw);
    }
    const auto raw = value.get<std::int64_t>();
    if (raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) {
        throw CatalogError(context + ": field \"" + key + "\" is out of range.");
    }
    return static_cast<int>(raw);
}

std::string readString(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField(object, key, context);
    if (!value.is_string()) {
        throw CatalogError(context + ": field \"" + key + "\" must be a string.");
    }
    return value.get<std::string>();
}

bool readBool(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField(object, key, context);
    if (!value.is_boolean()) {
        throw CatalogError(context + ": field \"" + key + "\" must be a boolean.");
    }
    return value.get<bool>();
}

bool isBlank(const std::string& text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

json parseObject(const std::string& text, const char* what)
{
    json document;
    try {
        document = json::parse(text);
    } catch (const json::parse_error& error) {
        throw CatalogError(std::string("The ") + what + " file is not valid JSON: " + error.what());
    }
    if (!document.is_object()) {
        throw CatalogError(std::string("The ") + what + " file must contain a JSON object.");
    }
    return document;
}

void readCatalogHeader(const json& document, const char* what)
{
    const std::string context = std::string(what) + " file";
    const int version = readInt(document, "schemaVersion", context);
    if (version != kCatalogSchemaVersion) {
        throw CatalogError(context + " has schemaVersion " + std::to_string(version) +
                            ", which this version of the app cannot read.");
    }
    const std::string source = readString(document, "source", context);
    if (source != kSrdMonsterSource) {
        throw CatalogError(context + ": source must be \"" + std::string(kSrdMonsterSource) + "\".");
    }
}

const json& requireArray(const json& document, const char* key, const char* what)
{
    const std::string context = std::string(what) + " file";
    const json& list = requireField(document, key, context);
    if (!list.is_array()) {
        throw CatalogError(context + ": field \"" + std::string(key) + "\" must be an array.");
    }
    return list;
}

std::string readFile(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) {
            throw CatalogError("Could not check " + path.string() + ": " + ec.message());
        }
        throw CatalogError("Could not open " + path.string() + " for reading.");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw CatalogError("Could not open " + path.string() + " for reading.");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw CatalogError("Could not read " + path.string() + ".");
    }
    return buffer.str();
}

}  // namespace

std::vector<Spell> parseSpellCatalog(const std::string& text)
{
    const json document = parseObject(text, "spell catalog");
    readCatalogHeader(document, "Spell catalog");
    const json& list = requireArray(document, "spells", "Spell catalog");

    std::vector<Spell> spells;
    spells.reserve(list.size());
    std::unordered_set<std::string> seenIds;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string context = "Spell " + std::to_string(i + 1);
        const json& value = list[i];
        if (!value.is_object()) {
            throw CatalogError(context + ": must be an object.");
        }
        Spell spell;
        spell.id = readString(value, "id", context);
        spell.name = readString(value, "name", context);
        spell.level = readInt(value, "level", context);
        spell.school = readString(value, "school", context);
        spell.castingTime = readString(value, "castingTime", context);
        spell.range = readString(value, "range", context);
        spell.duration = readString(value, "duration", context);
        spell.concentration = readBool(value, "concentration", context);
        spell.description = readString(value, "description", context);
        if (spell.id.empty() || isBlank(spell.name) || isBlank(spell.description)) {
            throw CatalogError(context + ": id, name, and description are required.");
        }
        if (spell.level < 0) {
            throw CatalogError(context + ": level must be zero or greater.");
        }
        if (!seenIds.insert(spell.id).second) {
            throw CatalogError(context + ": duplicate id \"" + spell.id + "\".");
        }
        spells.push_back(std::move(spell));
    }
    return spells;
}

std::vector<Spell> loadSpellCatalog(const std::filesystem::path& path)
{
    return parseSpellCatalog(readFile(path));
}

std::vector<Condition> parseConditionCatalog(const std::string& text)
{
    const json document = parseObject(text, "condition catalog");
    readCatalogHeader(document, "Condition catalog");
    const json& list = requireArray(document, "conditions", "Condition catalog");

    std::vector<Condition> conditions;
    conditions.reserve(list.size());
    std::unordered_set<std::string> seenIds;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string context = "Condition " + std::to_string(i + 1);
        const json& value = list[i];
        if (!value.is_object()) {
            throw CatalogError(context + ": must be an object.");
        }
        Condition condition;
        condition.id = readString(value, "id", context);
        condition.name = readString(value, "name", context);
        condition.description = readString(value, "description", context);
        if (condition.id.empty() || isBlank(condition.name) || isBlank(condition.description)) {
            throw CatalogError(context + ": id, name, and description are required.");
        }
        if (!seenIds.insert(condition.id).second) {
            throw CatalogError(context + ": duplicate id \"" + condition.id + "\".");
        }
        conditions.push_back(std::move(condition));
    }
    return conditions;
}

std::vector<Condition> loadConditionCatalog(const std::filesystem::path& path)
{
    return parseConditionCatalog(readFile(path));
}

std::vector<std::string> parseSpeciesCatalog(const std::string& text)
{
    const json document = parseObject(text, "species catalog");
    readCatalogHeader(document, "Species catalog");
    const json& list = requireArray(document, "species", "Species catalog");

    std::vector<std::string> species;
    species.reserve(list.size());
    std::unordered_set<std::string> seen;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string context = "Species " + std::to_string(i + 1);
        if (!list[i].is_string()) {
            throw CatalogError(context + ": must be a string.");
        }
        const std::string name = list[i].get<std::string>();
        if (isBlank(name)) {
            throw CatalogError(context + ": name is required.");
        }
        if (!seen.insert(name).second) {
            throw CatalogError(context + ": duplicate name \"" + name + "\".");
        }
        species.push_back(name);
    }
    return species;
}

std::vector<std::string> loadSpeciesCatalog(const std::filesystem::path& path)
{
    return parseSpeciesCatalog(readFile(path));
}

}  // namespace combat
