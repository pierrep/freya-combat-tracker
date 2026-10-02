#include "data/json_character_store.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace combat {

namespace {

// ordered_json keeps keys in the order written, so the file reads like the plan.
using json = nlohmann::ordered_json;

const json& requireField(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        throw CharacterStoreError(context + ": missing field \"" + key + "\".");
    }
    return *it;
}

int readInt(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField(object, key, context);
    if (!value.is_number_integer()) {
        throw CharacterStoreError(context + ": field \"" + key + "\" must be an integer.");
    }
    if (value.is_number_unsigned()) {
        const auto raw = value.get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            throw CharacterStoreError(context + ": field \"" + key + "\" is out of range.");
        }
        return static_cast<int>(raw);
    }
    const auto raw = value.get<std::int64_t>();
    if (raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) {
        throw CharacterStoreError(context + ": field \"" + key + "\" is out of range.");
    }
    return static_cast<int>(raw);
}

std::string readString(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField(object, key, context);
    if (!value.is_string()) {
        throw CharacterStoreError(context + ": field \"" + key + "\" must be a string.");
    }
    return value.get<std::string>();
}

Character characterFromJson(const json& value, std::size_t index)
{
    const std::string context = "Character " + std::to_string(index + 1);
    if (!value.is_object()) {
        throw CharacterStoreError(context + ": must be an object.");
    }

    Character character;
    character.id = readString(value, "id", context);
    character.name = readString(value, "name", context);
    character.hp = readInt(value, "hp", context);
    character.ac = readInt(value, "ac", context);
    character.passivePerception = readInt(value, "passivePerception", context);

    const json& abilities = requireField(value, "abilities", context);
    if (!abilities.is_object()) {
        throw CharacterStoreError(context + ": field \"abilities\" must be an object.");
    }
    const std::string abilityContext = context + " abilities";
    character.abilities.strength = readInt(abilities, "strength", abilityContext);
    character.abilities.dexterity = readInt(abilities, "dexterity", abilityContext);
    character.abilities.constitution = readInt(abilities, "constitution", abilityContext);
    character.abilities.intelligence = readInt(abilities, "intelligence", abilityContext);
    character.abilities.wisdom = readInt(abilities, "wisdom", abilityContext);
    character.abilities.charisma = readInt(abilities, "charisma", abilityContext);

    const auto problems = validateCharacter(character);
    if (!problems.empty()) {
        throw CharacterStoreError(context + ": " + problems.front());
    }
    return character;
}

json characterToJson(const Character& character)
{
    return json{
        {"id", character.id},
        {"name", character.name},
        {"hp", character.hp},
        {"ac", character.ac},
        {"abilities",
         {
             {"strength", character.abilities.strength},
             {"dexterity", character.abilities.dexterity},
             {"constitution", character.abilities.constitution},
             {"intelligence", character.abilities.intelligence},
             {"wisdom", character.abilities.wisdom},
             {"charisma", character.abilities.charisma},
         }},
        {"passivePerception", character.passivePerception},
    };
}

}  // namespace

std::vector<Character> parseCharactersDocument(const std::string& text)
{
    json document;
    try {
        document = json::parse(text);
    } catch (const json::parse_error& error) {
        throw CharacterStoreError(std::string("The characters file is not valid JSON: ") + error.what());
    }

    if (!document.is_object()) {
        throw CharacterStoreError("The characters file must contain a JSON object.");
    }
    const std::string context = "Characters file";
    const int version = readInt(document, "schemaVersion", context);
    if (version != kCharactersSchemaVersion) {
        throw CharacterStoreError("The characters file has schemaVersion " + std::to_string(version) +
                                  ", which this version of the app cannot read.");
    }

    const auto listIt = document.find("characters");
    if (listIt == document.end()) {
        throw CharacterStoreError("Characters file: missing field \"characters\".");
    }
    const json& list = *listIt;
    if (!list.is_array()) {
        throw CharacterStoreError("Characters file: field \"characters\" must be an array.");
    }

    std::vector<Character> characters;
    characters.reserve(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        characters.push_back(characterFromJson(list[i], i));
    }
    return characters;
}

std::string serializeCharactersDocument(const std::vector<Character>& characters)
{
    json list = json::array();
    for (const Character& character : characters) {
        list.push_back(characterToJson(character));
    }
    const json document{
        {"schemaVersion", kCharactersSchemaVersion},
        {"characters", std::move(list)},
    };
    return document.dump(2) + "\n";
}

JsonCharacterStore::JsonCharacterStore(std::filesystem::path path)
    : m_path(std::move(path))
{
}

std::vector<Character> JsonCharacterStore::loadAll()
{
    std::error_code ec;
    if (!std::filesystem::exists(m_path, ec)) {
        if (ec) {
            throw CharacterStoreError("Could not check " + m_path.string() + ": " + ec.message());
        }
        return {};
    }

    std::ifstream in(m_path, std::ios::binary);
    if (!in) {
        throw CharacterStoreError("Could not open " + m_path.string() + " for reading.");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw CharacterStoreError("Could not read " + m_path.string() + ".");
    }
    return parseCharactersDocument(buffer.str());
}

void JsonCharacterStore::saveAll(const std::vector<Character>& characters)
{
    for (std::size_t i = 0; i < characters.size(); ++i) {
        const auto problems = validateCharacter(characters[i]);
        if (!problems.empty()) {
            throw CharacterStoreError("Character " + std::to_string(i + 1) + ": " + problems.front());
        }
    }

    const std::string text = serializeCharactersDocument(characters);

    std::error_code ec;
    const auto folder = m_path.parent_path();
    if (!folder.empty()) {
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            throw CharacterStoreError("Could not create " + folder.string() + ": " + ec.message());
        }
    }

    auto tempPath = m_path;
    tempPath += ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw CharacterStoreError("Could not open " + tempPath.string() + " for writing.");
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tempPath, ec);
            throw CharacterStoreError("Could not write " + tempPath.string() + ".");
        }
    }

    std::filesystem::rename(tempPath, m_path, ec);
    if (ec) {
        const std::string message = ec.message();
        std::filesystem::remove(tempPath, ec);
        throw CharacterStoreError("Could not replace " + m_path.string() + ": " + message);
    }
}

}  // namespace combat
