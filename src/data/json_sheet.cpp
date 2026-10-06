#include "data/json_sheet.h"

#include "core/text.h"
#include "data/json_util.h"

#include <unordered_set>

namespace combat {

namespace {

using json = json_util::json;
using Error = CatalogError;

std::string readString(const json& object, const char* key, const std::string& context)
{
    return json_util::readString<Error>(object, key, context);
}

int readInt(const json& object, const char* key, const std::string& context)
{
    return json_util::readInt<Error>(object, key, context);
}

bool readBool(const json& object, const char* key, const std::string& context)
{
    return json_util::readBool<Error>(object, key, context);
}

json parseObject(const std::string& text, const char* what)
{
    return json_util::parseDocument<Error>(text, what);
}

void readCatalogHeader(const json& document, const char* what, int expectedVersion)
{
    const std::string context = std::string(what) + " file";
    const int version = readInt(document, "schemaVersion", context);
    if (version != expectedVersion) {
        throw CatalogError(context + " has schemaVersion " + std::to_string(version) +
                           ", which this version of the app cannot read.");
    }
    const std::string source = readString(document, "source", context);
    if (source != kSrdSource) {
        throw CatalogError(context + ": source must be \"" + std::string(kSrdSource) + "\".");
    }
}

const json& requireArray(const json& document, const char* key, const char* what)
{
    return json_util::requireArray<Error>(document, key, std::string(what) + " file");
}

std::string readFile(const std::filesystem::path& path)
{
    return *json_util::readTextFile<Error>(path, false);
}

}  // namespace

std::vector<Spell> parseSpellCatalog(const std::string& text)
{
    const json document = parseObject(text, "spell catalog");
    readCatalogHeader(document, "Spell catalog", kCatalogSchemaVersion);
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
        const auto components = value.find("components");
        if (components != value.end()) {
            if (!components->is_array()) {
                throw CatalogError(context + ": field \"components\" must be an array.");
            }
            for (const json& component : *components) {
                if (!component.is_string()) {
                    throw CatalogError(context + ": each component must be a string.");
                }
                spell.components.push_back(component.get<std::string>());
            }
        }
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
    readCatalogHeader(document, "Condition catalog", kConditionCatalogSchemaVersion);
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
        const auto tags = value.find("tags");
        if (tags != value.end()) {
            if (!tags->is_array()) {
                throw CatalogError(context + ": field \"tags\" must be an array.");
            }
            for (std::size_t tagIndex = 0; tagIndex < tags->size(); ++tagIndex) {
                if (!(*tags)[tagIndex].is_string()) {
                    throw CatalogError(context + " tag " + std::to_string(tagIndex + 1) + ": must be a string.");
                }
                const std::string tag = (*tags)[tagIndex].get<std::string>();
                if (isBlank(tag)) {
                    throw CatalogError(context + " tag " + std::to_string(tagIndex + 1) + ": text is required.");
                }
                condition.tags.push_back(tag);
            }
        }
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
    readCatalogHeader(document, "Species catalog", kCatalogSchemaVersion);
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
