#include "data/json_character_store.h"

#include "core/text.h"
#include "data/json_util.h"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

namespace combat {

namespace {

using json_util::json;
using Error = CharacterStoreError;

int readInt(const json& object, const char* key, const std::string& context)
{
    return json_util::readInt<Error>(object, key, context);
}

std::string readString(const json& object, const char* key, const std::string& context)
{
    return json_util::readString<Error>(object, key, context);
}

bool readBool(const json& object, const char* key, const std::string& context)
{
    return json_util::readBool<Error>(object, key, context);
}

void readAbilities(Character& character, const json& value, const std::string& context)
{
    const json& abilities = json_util::requireField<Error>(value, "abilities", context);
    if (!abilities.is_object()) {
        throw Error(context + ": field \"abilities\" must be an object.");
    }
    const std::string abilityContext = context + " abilities";
    for (const Ability ability : kAbilityOrder) {
        abilityScoreRef(character.abilities, ability) = readInt(abilities, abilityKey(ability), abilityContext);
    }
}

void clampHitPoints(Character& character)
{
    character.hp.max = std::max(0, character.hp.max);
    character.hp.current = std::max(0, character.hp.current);
    character.tempHp = std::max(0, character.tempHp);
    character.exhaustion = std::clamp(character.exhaustion, 0, 6);
}

void throwProblems(const Character& character, const std::string& context)
{
    const auto problems = validateCharacter(character);
    if (!problems.empty()) {
        throw Error(context + ": " + problems.front());
    }
}

Character characterFromJsonV1(const json& value, std::size_t index)
{
    const std::string context = "Character " + std::to_string(index + 1);
    json_util::requireObject<Error>(value, context);

    Character character;
    character.id = readString(value, "id", context);
    character.name = readString(value, "name", context);
    const int hp = readInt(value, "hp", context);
    character.hp.current = hp;
    character.hp.max = hp;
    character.ac = readInt(value, "ac", context);
    character.passivePerception = readInt(value, "passivePerception", context);
    readAbilities(character, value, context);
    clampHitPoints(character);
    throwProblems(character, context);
    return character;
}

std::vector<std::string> readTypes(const json& object, const char* key, const std::string& context)
{
    std::vector<std::string> types = json_util::readStringList<Error>(object, key, context);
    for (std::string& type : types) {
        type = asciiLower(trim(type));
    }
    return types;
}

Character characterFromJson(const json& value, std::size_t index, int version)
{
    const std::string context = "Character " + std::to_string(index + 1);
    json_util::requireObject<Error>(value, context);

    Character character;
    character.id = readString(value, "id", context);
    character.name = readString(value, "name", context);

    const json& hp = json_util::requireField<Error>(value, "hp", context);
    json_util::requireObject<Error>(hp, context + " hp");
    const std::string hpContext = context + " hp";
    character.hp.current = readInt(hp, "current", hpContext);
    character.hp.max = readInt(hp, "max", hpContext);
    character.tempHp = readInt(value, "tempHp", context);
    character.ac = readInt(value, "ac", context);
    character.speed = readString(value, "speed", context);
    readAbilities(character, value, context);
    character.passivePerception = readInt(value, "passivePerception", context);
    character.species = readString(value, "species", context);

    const json& classes = json_util::requireArray<Error>(value, "classes", context);
    for (std::size_t i = 0; i < classes.size(); ++i) {
        const std::string classContext = context + " class " + std::to_string(i + 1);
        json_util::requireObject<Error>(classes[i], classContext);
        ClassLevel row;
        row.name = readString(classes[i], "name", classContext);
        row.level = readInt(classes[i], "level", classContext);
        row.subclass = readString(classes[i], "subclass", classContext);
        row.hitDiceSpent = std::clamp(json_util::readIntOr<Error>(classes[i], "hitDiceSpent", 0, classContext), 0,
                                      std::max(0, row.level));
        character.classes.push_back(std::move(row));
    }

    if (version >= 4) {
        character.initiativeOverride = json_util::readOptionalInt<Error>(value, "initiativeOverride", context);
        character.proficiencyOverride = json_util::readOptionalInt<Error>(value, "proficiencyOverride", context);
    } else {
        const int typedInitiative = readInt(value, "initiativeBonus", context);
        const int typedProficiency = readInt(value, "proficiencyBonus", context);
        if (typedInitiative != 0 && typedInitiative != abilityModifier(character.abilities.dexterity)) {
            character.initiativeOverride = typedInitiative;
        }
        if (typedProficiency != 0 && typedProficiency != proficiencyBonusForLevel(totalClassLevel(character))) {
            character.proficiencyOverride = typedProficiency;
        }
    }

    const json& saves = json_util::requireField<Error>(value, "savingThrows", context);
    json_util::requireObject<Error>(saves, context + " savingThrows");
    const std::string saveContext = context + " savingThrows";
    for (const SaveField& field : kSavingThrows) {
        character.savingThrows.*field.member = readBool(saves, field.key, saveContext);
    }

    const json& skills = json_util::requireField<Error>(value, "skills", context);
    json_util::requireObject<Error>(skills, context + " skills");
    const std::string skillContext = context + " skills";
    for (const SkillField& field : kSkills) {
        character.skills.*field.member = readBool(skills, field.key, skillContext);
    }

    const json& spells = json_util::requireArray<Error>(value, "spells", context);
    for (std::size_t i = 0; i < spells.size(); ++i) {
        const std::string spellContext = context + " spell " + std::to_string(i + 1);
        json_util::requireObject<Error>(spells[i], spellContext);
        CharacterSpell spell;
        spell.id = json_util::readOptionalString<Error>(spells[i], "id", spellContext);
        spell.name = json_util::readOptionalString<Error>(spells[i], "name", spellContext);
        spell.prepared = readBool(spells[i], "prepared", spellContext);
        character.spells.push_back(std::move(spell));
    }

    const json& slots = json_util::requireArray<Error>(value, "spellSlots", context);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const std::string slotContext = context + " spell slot " + std::to_string(i + 1);
        json_util::requireObject<Error>(slots[i], slotContext);
        SpellSlot slot;
        slot.level = readInt(slots[i], "level", slotContext);
        slot.current = readInt(slots[i], "current", slotContext);
        slot.max = readInt(slots[i], "max", slotContext);
        slot.shortRest = json_util::readBoolOr<Error>(slots[i], "shortRest", false, slotContext);
        character.spellSlots.push_back(slot);
    }

    const json& gear = json_util::requireArray<Error>(value, "gear", context);
    for (std::size_t i = 0; i < gear.size(); ++i) {
        const std::string gearContext = context + " gear " + std::to_string(i + 1);
        json_util::requireObject<Error>(gear[i], gearContext);
        GearItem item;
        item.name = readString(gear[i], "name", gearContext);
        item.quantity = readInt(gear[i], "quantity", gearContext);
        item.equipped = readBool(gear[i], "equipped", gearContext);
        character.gear.push_back(std::move(item));
    }

    if (version >= 4) {
        const auto defenses = value.find("defenses");
        if (defenses != value.end() && !defenses->is_null()) {
            const std::string defenseContext = context + " defenses";
            json_util::requireObject<Error>(*defenses, defenseContext);
            character.defenses.resistances = readTypes(*defenses, "resistances", defenseContext);
            character.defenses.immunities = readTypes(*defenses, "immunities", defenseContext);
            character.defenses.vulnerabilities = readTypes(*defenses, "vulnerabilities", defenseContext);
        }
        character.exhaustion = json_util::readIntOr<Error>(value, "exhaustion", 0, context);
    } else {
        // Versions 2 and 3 kept conditions and death saves on the sheet. They
        // are read for shape and dropped; the fight owns them now.
        json_util::requireArray<Error>(value, "conditions", context);
        json_util::requireField<Error>(value, "deathSaves", context);
    }
    character.notes = readString(value, "notes", context);

    const auto external = value.find("external");
    if (external != value.end() && !external->is_null()) {
        const std::string externalContext = context + " external";
        json_util::requireObject<Error>(*external, externalContext);
        CharacterImport info;
        info.source = json_util::readOptionalString<Error>(*external, "source", externalContext);
        info.importedAt = json_util::readOptionalString<Error>(*external, "importedAt", externalContext);
        info.fileName = json_util::readOptionalString<Error>(*external, "fileName", externalContext);
        if (!info.source.empty() || !info.importedAt.empty() || !info.fileName.empty()) {
            character.external = std::move(info);
        }
    }

    clampHitPoints(character);
    throwProblems(character, context);
    return character;
}

json optionalInt(const std::optional<int>& value)
{
    return value.has_value() ? json(*value) : json(nullptr);
}

json characterToJson(const Character& character)
{
    json abilities = json::object();
    for (const Ability ability : kAbilityOrder) {
        abilities[abilityKey(ability)] = abilityScore(character.abilities, ability);
    }
    json classes = json::array();
    for (const ClassLevel& row : character.classes) {
        classes.push_back(json{
            {"name", row.name},
            {"level", row.level},
            {"subclass", row.subclass},
            {"hitDiceSpent", row.hitDiceSpent},
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
            {"shortRest", slot.shortRest},
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

    json object = json{
        {"id", character.id},
        {"name", character.name},
        {"hp", {{"current", character.hp.current}, {"max", character.hp.max}}},
        {"tempHp", character.tempHp},
        {"ac", character.ac},
        {"speed", character.speed},
        {"initiativeOverride", optionalInt(character.initiativeOverride)},
        {"proficiencyOverride", optionalInt(character.proficiencyOverride)},
        {"abilities", std::move(abilities)},
        {"passivePerception", character.passivePerception},
        {"species", character.species},
        {"classes", std::move(classes)},
        {"savingThrows", std::move(saves)},
        {"skills", std::move(skills)},
        {"spells", std::move(spells)},
        {"spellSlots", std::move(slots)},
        {"gear", std::move(gear)},
        {"defenses",
         {{"resistances", character.defenses.resistances},
          {"immunities", character.defenses.immunities},
          {"vulnerabilities", character.defenses.vulnerabilities}}},
        {"exhaustion", character.exhaustion},
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

// Copies the original bytes of an older file beside it before the first save
// in the new version, so an older build can still be pointed at that copy.
void preserveOlderCopy(const std::filesystem::path& path)
{
    const std::optional<std::string> text = json_util::readTextFile<Error>(path, true);
    if (!text.has_value()) {
        return;
    }
    int version = -1;
    try {
        const json document = json::parse(*text);
        if (document.is_object()) {
            const auto it = document.find("schemaVersion");
            if (it != document.end() && it->is_number_integer()) {
                version = it->get<int>();
            }
        }
    } catch (const json::exception&) {
        return;
    }
    if (version < 1 || version >= kCharactersSchemaVersion) {
        return;
    }

    std::error_code ec;
    const std::filesystem::path backup =
        path.parent_path() / (path.stem().string() + ".v" + std::to_string(version) + path.extension().string());
    if (std::filesystem::exists(backup, ec)) {
        if (ec) {
            throw Error("Could not check " + backup.string() + ": " + ec.message());
        }
        return;
    }
    std::ofstream out(backup, std::ios::binary | std::ios::trunc);
    out.write(text->data(), static_cast<std::streamsize>(text->size()));
    out.flush();
    if (!out) {
        throw Error("Could not keep a copy of the version " + std::to_string(version) + " file at " +
                    backup.string() + ".");
    }
}

}  // namespace

std::vector<Character> parseCharactersDocument(const std::string& text)
{
    const json document = json_util::parseDocument<Error>(text, "characters");
    const std::string context = "Characters file";
    const int version = readInt(document, "schemaVersion", context);
    if (version < 1 || version > kCharactersSchemaVersion) {
        throw Error("The characters file has schemaVersion " + std::to_string(version) +
                    ", which this version of the app cannot read.");
    }

    const json& list = json_util::requireArray<Error>(document, "characters", context);
    std::vector<Character> characters;
    characters.reserve(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (version == 1) {
            characters.push_back(characterFromJsonV1(list[i], i));
        } else {
            characters.push_back(characterFromJson(list[i], i, version));
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
    const std::optional<std::string> text = json_util::readTextFile<Error>(m_path, true);
    if (!text.has_value()) {
        return {};
    }
    return parseCharactersDocument(*text);
}

void JsonCharacterStore::saveAll(const std::vector<Character>& characters)
{
    for (std::size_t i = 0; i < characters.size(); ++i) {
        const auto problems = validateCharacter(characters[i]);
        if (!problems.empty()) {
            throw Error("Character " + std::to_string(i + 1) + ": " + problems.front());
        }
    }
    preserveOlderCopy(m_path);
    json_util::writeFileAtomically<Error>(m_path, serializeCharactersDocument(characters));
}

}  // namespace combat
