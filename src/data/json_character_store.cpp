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

bool readBool(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField(object, key, context);
    if (!value.is_boolean()) {
        throw CharacterStoreError(context + ": field \"" + key + "\" must be a boolean.");
    }
    return value.get<bool>();
}

std::string readOptionalString(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return {};
    }
    if (!it->is_string()) {
        throw CharacterStoreError(context + ": field \"" + key + "\" must be a string.");
    }
    return it->get<std::string>();
}

void readAbilities(Character& character, const json& value, const std::string& context)
{
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
}

void requireObject(const json& value, const std::string& context)
{
    if (!value.is_object()) {
        throw CharacterStoreError(context + ": must be an object.");
    }
}

Character characterFromJsonV1(const json& value, std::size_t index)
{
    const std::string context = "Character " + std::to_string(index + 1);
    requireObject(value, context);

    Character character;
    character.id = readString(value, "id", context);
    character.name = readString(value, "name", context);
    const int hp = readInt(value, "hp", context);
    character.hp.current = hp;
    character.hp.max = hp;
    character.ac = readInt(value, "ac", context);
    character.passivePerception = readInt(value, "passivePerception", context);
    readAbilities(character, value, context);

    const auto problems = validateCharacter(character);
    if (!problems.empty()) {
        throw CharacterStoreError(context + ": " + problems.front());
    }
    return character;
}

Character characterFromJsonV2(const json& value, std::size_t index)
{
    const std::string context = "Character " + std::to_string(index + 1);
    requireObject(value, context);

    Character character;
    character.id = readString(value, "id", context);
    character.name = readString(value, "name", context);

    const json& hp = requireField(value, "hp", context);
    if (!hp.is_object()) {
        throw CharacterStoreError(context + ": field \"hp\" must be an object.");
    }
    const std::string hpContext = context + " hp";
    character.hp.current = readInt(hp, "current", hpContext);
    character.hp.max = readInt(hp, "max", hpContext);
    character.tempHp = readInt(value, "tempHp", context);
    character.ac = readInt(value, "ac", context);
    character.speed = readString(value, "speed", context);
    character.initiativeBonus = readInt(value, "initiativeBonus", context);
    character.proficiencyBonus = readInt(value, "proficiencyBonus", context);
    readAbilities(character, value, context);
    character.passivePerception = readInt(value, "passivePerception", context);
    character.species = readString(value, "species", context);

    const json& classes = requireField(value, "classes", context);
    if (!classes.is_array()) {
        throw CharacterStoreError(context + ": field \"classes\" must be an array.");
    }
    for (std::size_t i = 0; i < classes.size(); ++i) {
        const std::string classContext = context + " class " + std::to_string(i + 1);
        requireObject(classes[i], classContext);
        ClassLevel row;
        row.name = readString(classes[i], "name", classContext);
        row.level = readInt(classes[i], "level", classContext);
        row.subclass = readString(classes[i], "subclass", classContext);
        character.classes.push_back(std::move(row));
    }

    const json& saves = requireField(value, "savingThrows", context);
    if (!saves.is_object()) {
        throw CharacterStoreError(context + ": field \"savingThrows\" must be an object.");
    }
    const std::string saveContext = context + " savingThrows";
    for (const SaveField& field : kSavingThrows) {
        character.savingThrows.*field.member = readBool(saves, field.key, saveContext);
    }

    const json& skills = requireField(value, "skills", context);
    if (!skills.is_object()) {
        throw CharacterStoreError(context + ": field \"skills\" must be an object.");
    }
    const std::string skillContext = context + " skills";
    for (const SkillField& field : kSkills) {
        character.skills.*field.member = readBool(skills, field.key, skillContext);
    }

    const json& spells = requireField(value, "spells", context);
    if (!spells.is_array()) {
        throw CharacterStoreError(context + ": field \"spells\" must be an array.");
    }
    for (std::size_t i = 0; i < spells.size(); ++i) {
        const std::string spellContext = context + " spell " + std::to_string(i + 1);
        requireObject(spells[i], spellContext);
        CharacterSpell spell;
        spell.id = readOptionalString(spells[i], "id", spellContext);
        spell.name = readOptionalString(spells[i], "name", spellContext);
        spell.prepared = readBool(spells[i], "prepared", spellContext);
        character.spells.push_back(std::move(spell));
    }

    const json& slots = requireField(value, "spellSlots", context);
    if (!slots.is_array()) {
        throw CharacterStoreError(context + ": field \"spellSlots\" must be an array.");
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const std::string slotContext = context + " spell slot " + std::to_string(i + 1);
        requireObject(slots[i], slotContext);
        SpellSlot slot;
        slot.level = readInt(slots[i], "level", slotContext);
        slot.current = readInt(slots[i], "current", slotContext);
        slot.max = readInt(slots[i], "max", slotContext);
        character.spellSlots.push_back(slot);
    }

    const json& gear = requireField(value, "gear", context);
    if (!gear.is_array()) {
        throw CharacterStoreError(context + ": field \"gear\" must be an array.");
    }
    for (std::size_t i = 0; i < gear.size(); ++i) {
        const std::string gearContext = context + " gear " + std::to_string(i + 1);
        requireObject(gear[i], gearContext);
        GearItem item;
        item.name = readString(gear[i], "name", gearContext);
        item.quantity = readInt(gear[i], "quantity", gearContext);
        item.equipped = readBool(gear[i], "equipped", gearContext);
        character.gear.push_back(std::move(item));
    }

    const json& conditions = requireField(value, "conditions", context);
    if (!conditions.is_array()) {
        throw CharacterStoreError(context + ": field \"conditions\" must be an array.");
    }
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        if (!conditions[i].is_string()) {
            throw CharacterStoreError(context + " condition " + std::to_string(i + 1) + ": must be a string.");
        }
        character.conditions.push_back(conditions[i].get<std::string>());
    }

    const json& deathSaves = requireField(value, "deathSaves", context);
    if (!deathSaves.is_object()) {
        throw CharacterStoreError(context + ": field \"deathSaves\" must be an object.");
    }
    const std::string deathContext = context + " deathSaves";
    character.deathSaves.successes = readInt(deathSaves, "successes", deathContext);
    character.deathSaves.failures = readInt(deathSaves, "failures", deathContext);
    character.notes = readString(value, "notes", context);

    const auto external = value.find("external");
    if (external != value.end() && !external->is_null()) {
        const std::string externalContext = context + " external";
        requireObject(*external, externalContext);
        CharacterImport info;
        info.source = readOptionalString(*external, "source", externalContext);
        info.importedAt = readOptionalString(*external, "importedAt", externalContext);
        info.fileName = readOptionalString(*external, "fileName", externalContext);
        if (!info.source.empty() || !info.importedAt.empty() || !info.fileName.empty()) {
            character.external = std::move(info);
        }
    }

    const auto problems = validateCharacter(character);
    if (!problems.empty()) {
        throw CharacterStoreError(context + ": " + problems.front());
    }
    return character;
}

json abilitiesToJson(const AbilityScores& abilities)
{
    return json{
        {"strength", abilities.strength},
        {"dexterity", abilities.dexterity},
        {"constitution", abilities.constitution},
        {"intelligence", abilities.intelligence},
        {"wisdom", abilities.wisdom},
        {"charisma", abilities.charisma},
    };
}

json characterToJson(const Character& character)
{
    json classes = json::array();
    for (const ClassLevel& row : character.classes) {
        classes.push_back(json{
            {"name", row.name},
            {"level", row.level},
            {"subclass", row.subclass},
        });
    }

    json saves = json::object();
    for (const SaveField& field : kSavingThrows) {
        saves[field.key] = character.savingThrows.*field.member;
    }
    json skills = json::object();
    for (const SkillField& field : kSkills) {
        skills[field.key] = character.skills.*field.member;
    }

    json spells = json::array();
    for (const CharacterSpell& spell : character.spells) {
        json object = json::object();
        if (!spell.id.empty()) {
            object["id"] = spell.id;
            if (!spell.name.empty()) {
                object["name"] = spell.name;
            }
        } else {
            object["name"] = spell.name;
        }
        object["prepared"] = spell.prepared;
        spells.push_back(std::move(object));
    }

    json slots = json::array();
    for (const SpellSlot& slot : character.spellSlots) {
        slots.push_back(json{
            {"level", slot.level},
            {"current", slot.current},
            {"max", slot.max},
        });
    }

    json gear = json::array();
    for (const GearItem& item : character.gear) {
        gear.push_back(json{
            {"name", item.name},
            {"quantity", item.quantity},
            {"equipped", item.equipped},
        });
    }

    json conditions = json::array();
    for (const std::string& id : character.conditions) {
        conditions.push_back(id);
    }

    json object = json{
        {"id", character.id},
        {"name", character.name},
        {"hp", {{"current", character.hp.current}, {"max", character.hp.max}}},
        {"tempHp", character.tempHp},
        {"ac", character.ac},
        {"speed", character.speed},
        {"initiativeBonus", character.initiativeBonus},
        {"proficiencyBonus", character.proficiencyBonus},
        {"abilities", abilitiesToJson(character.abilities)},
        {"passivePerception", character.passivePerception},
        {"species", character.species},
        {"classes", std::move(classes)},
        {"savingThrows", std::move(saves)},
        {"skills", std::move(skills)},
        {"spells", std::move(spells)},
        {"spellSlots", std::move(slots)},
        {"gear", std::move(gear)},
        {"conditions", std::move(conditions)},
        {"deathSaves", {{"successes", character.deathSaves.successes}, {"failures", character.deathSaves.failures}}},
        {"notes", character.notes},
    };
    if (character.external.has_value()) {
        object["external"] = json{
            {"source", character.external->source},
            {"importedAt", character.external->importedAt},
            {"fileName", character.external->fileName},
        };
    }
    return object;
}

std::filesystem::path version1BackupPath(const std::filesystem::path& path)
{
    return path.parent_path() / (path.stem().string() + ".v1" + path.extension().string());
}

// Copies the original bytes of a schemaVersion 1 file beside it. A later save
// does not overwrite that copy. Anything that is not a version 1 document is
// left alone.
void preserveVersion1Copy(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) {
            throw CharacterStoreError("Could not check " + path.string() + ": " + ec.message());
        }
        return;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw CharacterStoreError("Could not open " + path.string() + " for reading.");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw CharacterStoreError("Could not read " + path.string() + ".");
    }
    const std::string text = buffer.str();

    int version = -1;
    try {
        const json document = json::parse(text);
        if (document.is_object()) {
            const auto it = document.find("schemaVersion");
            if (it != document.end() && it->is_number_integer()) {
                version = it->get<int>();
            }
        }
    } catch (const json::exception&) {
        return;
    }
    if (version != 1) {
        return;
    }

    const std::filesystem::path backup = version1BackupPath(path);
    if (std::filesystem::exists(backup, ec)) {
        if (ec) {
            throw CharacterStoreError("Could not check " + backup.string() + ": " + ec.message());
        }
        return;
    }

    std::ofstream out(backup, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw CharacterStoreError("Could not keep a copy of the version 1 file at " + backup.string() + ".");
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
        throw CharacterStoreError("Could not keep a copy of the version 1 file at " + backup.string() + ".");
    }
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
    if (version != 1 && version != 2 && version != kCharactersSchemaVersion) {
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
        if (version == 1) {
            characters.push_back(characterFromJsonV1(list[i], i));
        } else {
            characters.push_back(characterFromJsonV2(list[i], i));
        }
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

    preserveVersion1Copy(m_path);

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
