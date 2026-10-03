#include "data/json_monsters.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace combat {

namespace {

using json = nlohmann::ordered_json;

const json& requireField(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        throw MonsterDataError(context + ": missing field \"" + key + "\".");
    }
    return *it;
}

int readInt(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField(object, key, context);
    if (!value.is_number_integer()) {
        throw MonsterDataError(context + ": field \"" + key + "\" must be an integer.");
    }
    if (value.is_number_unsigned()) {
        const auto raw = value.get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            throw MonsterDataError(context + ": field \"" + key + "\" is out of range.");
        }
        return static_cast<int>(raw);
    }
    const auto raw = value.get<std::int64_t>();
    if (raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) {
        throw MonsterDataError(context + ": field \"" + key + "\" is out of range.");
    }
    return static_cast<int>(raw);
}

std::string readString(const json& object, const char* key, const std::string& context)
{
    const json& value = requireField(object, key, context);
    if (!value.is_string()) {
        throw MonsterDataError(context + ": field \"" + key + "\" must be a string.");
    }
    return value.get<std::string>();
}

bool isBlank(const std::string& text);
std::vector<MonsterAttack> readAttacks(const json& object, const std::string& context);
std::vector<MonsterFeature> readFeatures(const json& object, const char* key, const std::string& context);

Monster monsterFromJson(const json& value, std::size_t index)
{
    const std::string context = "Monster " + std::to_string(index + 1);
    if (!value.is_object()) {
        throw MonsterDataError(context + ": must be an object.");
    }

    Monster monster;
    monster.id = readString(value, "id", context);
    monster.name = readString(value, "name", context);
    monster.size = readString(value, "size", context);
    monster.creatureType = readString(value, "creatureType", context);
    monster.ac = readInt(value, "ac", context);
    monster.hp = readInt(value, "hp", context);
    monster.hitDice = readString(value, "hitDice", context);
    monster.speed = readString(value, "speed", context);
    monster.initiativeBonus = readInt(value, "initiativeBonus", context);
    monster.passivePerception = readInt(value, "passivePerception", context);
    monster.challengeRating = readString(value, "challengeRating", context);
    monster.source = readString(value, "source", context);

    const json& abilities = requireField(value, "abilities", context);
    if (!abilities.is_object()) {
        throw MonsterDataError(context + ": field \"abilities\" must be an object.");
    }
    const std::string abilityContext = context + " abilities";
    monster.abilities.strength = readInt(abilities, "strength", abilityContext);
    monster.abilities.dexterity = readInt(abilities, "dexterity", abilityContext);
    monster.abilities.constitution = readInt(abilities, "constitution", abilityContext);
    monster.abilities.intelligence = readInt(abilities, "intelligence", abilityContext);
    monster.abilities.wisdom = readInt(abilities, "wisdom", abilityContext);
    monster.abilities.charisma = readInt(abilities, "charisma", abilityContext);
    monster.attacks = readAttacks(value, context);
    monster.traits = readFeatures(value, "traits", context);
    monster.bonusActions = readFeatures(value, "bonusActions", context);
    monster.reactions = readFeatures(value, "reactions", context);
    monster.legendaryActions = readFeatures(value, "legendaryActions", context);
    return monster;
}

std::vector<MonsterAttack> readAttacks(const json& object, const std::string& context)
{
    const auto it = object.find("attacks");
    if (it == object.end()) {
        return {};
    }
    if (!it->is_array()) {
        throw MonsterDataError(context + ": field \"attacks\" must be an array.");
    }
    std::vector<MonsterAttack> attacks;
    attacks.reserve(it->size());
    for (std::size_t i = 0; i < it->size(); ++i) {
        const std::string attackContext = context + " attack " + std::to_string(i + 1);
        const json& value = (*it)[i];
        if (!value.is_object()) {
            throw MonsterDataError(attackContext + ": must be an object.");
        }
        MonsterAttack attack;
        attack.name = readString(value, "name", attackContext);
        attack.effect = readString(value, "effect", attackContext);
        attack.count = readInt(value, "count", attackContext);
        if (isBlank(attack.name)) {
            throw MonsterDataError(attackContext + ": name is required.");
        }
        if (attack.count < 1) {
            throw MonsterDataError(attackContext + ": count must be at least 1.");
        }
        attacks.push_back(std::move(attack));
    }
    return attacks;
}

std::vector<MonsterFeature> readFeatures(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return {};
    }
    if (!it->is_array()) {
        throw MonsterDataError(context + ": field \"" + key + "\" must be an array.");
    }
    std::vector<MonsterFeature> features;
    features.reserve(it->size());
    for (std::size_t i = 0; i < it->size(); ++i) {
        const std::string featureContext = context + " " + key + " " + std::to_string(i + 1);
        const json& value = (*it)[i];
        if (!value.is_object()) {
            throw MonsterDataError(featureContext + ": must be an object.");
        }
        MonsterFeature feature;
        feature.name = readString(value, "name", featureContext);
        feature.effect = readString(value, "effect", featureContext);
        if (isBlank(feature.name)) {
            throw MonsterDataError(featureContext + ": name is required.");
        }
        features.push_back(std::move(feature));
    }
    return features;
}

void writeFeatures(json& document, const char* key, const std::vector<MonsterFeature>& features)
{
    if (features.empty()) {
        return;
    }
    json list = json::array();
    for (const MonsterFeature& feature : features) {
        list.push_back(json{
            {"name", feature.name},
            {"effect", feature.effect},
        });
    }
    document[key] = std::move(list);
}

json monsterToJson(const Monster& monster)
{
    json document{
        {"id", monster.id},
        {"name", monster.name},
        {"size", monster.size},
        {"creatureType", monster.creatureType},
        {"ac", monster.ac},
        {"hp", monster.hp},
        {"hitDice", monster.hitDice},
        {"speed", monster.speed},
        {"initiativeBonus", monster.initiativeBonus},
        {"abilities",
         {
             {"strength", monster.abilities.strength},
             {"dexterity", monster.abilities.dexterity},
             {"constitution", monster.abilities.constitution},
             {"intelligence", monster.abilities.intelligence},
             {"wisdom", monster.abilities.wisdom},
             {"charisma", monster.abilities.charisma},
         }},
        {"passivePerception", monster.passivePerception},
        {"challengeRating", monster.challengeRating},
        {"source", monster.source},
    };
    if (!monster.attacks.empty()) {
        json attacks = json::array();
        for (const MonsterAttack& attack : monster.attacks) {
            attacks.push_back(json{
                {"name", attack.name},
                {"effect", attack.effect},
                {"count", attack.count},
            });
        }
        document["attacks"] = std::move(attacks);
    }
    writeFeatures(document, "traits", monster.traits);
    writeFeatures(document, "bonusActions", monster.bonusActions);
    writeFeatures(document, "reactions", monster.reactions);
    writeFeatures(document, "legendaryActions", monster.legendaryActions);
    return document;
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
        throw MonsterDataError(std::string("The ") + what + " file is not valid JSON: " + error.what());
    }
    if (!document.is_object()) {
        throw MonsterDataError(std::string("The ") + what + " file must contain a JSON object.");
    }
    return document;
}

int readSchemaVersion(const json& document, const char* what)
{
    const std::string context = std::string(what) + " file";
    const int version = readInt(document, "schemaVersion", context);
    if (version != kMonsterSchemaVersion) {
        throw MonsterDataError(context + " has schemaVersion " + std::to_string(version) +
                                ", which this version of the app cannot read.");
    }
    return version;
}

const json& monsterArray(const json& document, const char* what)
{
    const std::string context = std::string(what) + " file";
    const json& list = requireField(document, "monsters", context);
    if (!list.is_array()) {
        throw MonsterDataError(context + ": field \"monsters\" must be an array.");
    }
    return list;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw MonsterDataError("Could not open " + path.string() + " for reading.");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw MonsterDataError("Could not read " + path.string() + ".");
    }
    return buffer.str();
}

void writeAtomically(const std::filesystem::path& path, const std::string& text)
{
    std::error_code ec;
    const auto folder = path.parent_path();
    if (!folder.empty()) {
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            throw MonsterDataError("Could not create " + folder.string() + ": " + ec.message());
        }
    }

    auto tempPath = path;
    tempPath += ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw MonsterDataError("Could not open " + tempPath.string() + " for writing.");
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tempPath, ec);
            throw MonsterDataError("Could not write " + tempPath.string() + ".");
        }
    }

    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        const std::string message = ec.message();
        std::filesystem::remove(tempPath, ec);
        throw MonsterDataError("Could not replace " + path.string() + ": " + message);
    }
}

}  // namespace

std::vector<Monster> parseSrdMonsterDocument(const std::string& text)
{
    const json document = parseObject(text, "monster catalog");
    readSchemaVersion(document, "Monster catalog");
    const std::string context = "Monster catalog file";
    const std::string source = readString(document, "source", context);
    if (source != kSrdMonsterSource) {
        throw MonsterDataError("Monster catalog file: source must be \"" + std::string(kSrdMonsterSource) + "\".");
    }

    const json& list = monsterArray(document, "Monster catalog");
    std::vector<Monster> monsters;
    monsters.reserve(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        Monster monster = monsterFromJson(list[i], i);
        if (monster.source != kSrdMonsterSource) {
            throw MonsterDataError("Monster " + std::to_string(i + 1) + ": source must be \"" +
                                    std::string(kSrdMonsterSource) + "\".");
        }
        if (monster.id.empty() || monster.name.empty()) {
            throw MonsterDataError("Monster " + std::to_string(i + 1) + ": id and name are required.");
        }
        monsters.push_back(std::move(monster));
    }
    return monsters;
}

std::vector<Monster> loadSrdMonsters(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) {
            throw MonsterDataError("Could not check " + path.string() + ": " + ec.message());
        }
        throw MonsterDataError("Could not open " + path.string() + " for reading.");
    }
    return parseSrdMonsterDocument(readFile(path));
}

CustomMonsterLoadResult parseCustomMonsterDocument(const std::string& text,
                                                   const std::unordered_set<std::string>& srdIds)
{
    const json document = parseObject(text, "custom monster");
    readSchemaVersion(document, "Custom monster");
    const json& list = monsterArray(document, "Custom monster");

    CustomMonsterLoadResult result;
    std::unordered_set<std::string> seenIds;
    for (std::size_t i = 0; i < list.size(); ++i) {
        Monster monster = monsterFromJson(list[i], i);
        const std::string label = monster.id.empty() ? ("Monster " + std::to_string(i + 1)) : monster.id;
        if (!monster.id.empty() && srdIds.find(monster.id) != srdIds.end()) {
            result.skipped.push_back("Skipped custom monster \"" + label +
                                      "\": its id matches an SRD monster, so it was not loaded.");
            continue;
        }
        if (monster.source == kSrdMonsterSource) {
            result.skipped.push_back("Skipped custom monster \"" + label +
                                      "\": its source is SRD, so it was not loaded.");
            continue;
        }
        if (monster.source != kCustomMonsterSource) {
            throw MonsterDataError("Monster " + std::to_string(i + 1) + ": source must be \"" +
                                    std::string(kCustomMonsterSource) + "\".");
        }
        if (monster.id.empty() || isBlank(monster.name)) {
            throw MonsterDataError("Monster " + std::to_string(i + 1) + ": id and name are required.");
        }
        if (!seenIds.insert(monster.id).second) {
            result.skipped.push_back("Skipped custom monster \"" + monster.id + "\": duplicate id.");
            continue;
        }
        result.monsters.push_back(std::move(monster));
    }
    return result;
}

std::string serializeCustomMonsterDocument(const std::vector<Monster>& monsters)
{
    json list = json::array();
    for (const Monster& monster : monsters) {
        list.push_back(monsterToJson(monster));
    }
    const json document{
        {"schemaVersion", kMonsterSchemaVersion},
        {"monsters", std::move(list)},
    };
    return document.dump(2) + "\n";
}

JsonCustomMonsterStore::JsonCustomMonsterStore(std::filesystem::path path, std::unordered_set<std::string> srdIds)
    : m_path(std::move(path))
    , m_srdIds(std::move(srdIds))
{
}

CustomMonsterLoadResult JsonCustomMonsterStore::loadAll()
{
    std::error_code ec;
    if (!std::filesystem::exists(m_path, ec)) {
        if (ec) {
            throw MonsterDataError("Could not check " + m_path.string() + ": " + ec.message());
        }
        return {};
    }
    return parseCustomMonsterDocument(readFile(m_path), m_srdIds);
}

void JsonCustomMonsterStore::saveAll(const std::vector<Monster>& monsters)
{
    std::unordered_set<std::string> seenIds;
    for (std::size_t i = 0; i < monsters.size(); ++i) {
        const auto problems = validateCustomMonster(monsters[i], m_srdIds);
        if (!problems.empty()) {
            throw MonsterDataError("Monster " + std::to_string(i + 1) + ": " + problems.front());
        }
        if (!seenIds.insert(monsters[i].id).second) {
            throw MonsterDataError("Monster " + std::to_string(i + 1) + ": duplicate id \"" + monsters[i].id + "\".");
        }
    }
    writeAtomically(m_path, serializeCustomMonsterDocument(monsters));
}

}  // namespace combat
