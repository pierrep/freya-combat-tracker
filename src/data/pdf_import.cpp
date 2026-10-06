#include "data/pdf_import.h"

#include "core/combat_rules.h"
#include "core/text.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string_view>
#include <utility>

namespace combat {

namespace {

constexpr char kFlattenedMessage[] =
    "This PDF has no filled form fields. It looks flattened, or it is a sheet without a form. "
    "Save a new Export to PDF from the browser. Do not use Print or Microsoft Print to PDF.";

constexpr const char* kDroppedFields[] = {
    "Features and Traits", "Feat+Traits",     "AttacksSpellcasting", "PersonalityTraits", "Ideals",
    "Bonds",               "Flaws",           "Backstory",           "CharacterAppearance",
};

struct SpellField {
    int order = 0;
    std::string fieldName;
    std::string value;
};

std::optional<int> parseInteger(const std::string& text)
{
    const std::string trimmed = trim(text);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    std::size_t index = 0;
    int sign = 1;
    if (trimmed[index] == '+') {
        ++index;
    } else if (trimmed[index] == '-') {
        sign = -1;
        ++index;
    }
    if (index >= trimmed.size()) {
        return std::nullopt;
    }
    long long value = 0;
    for (; index < trimmed.size(); ++index) {
        const auto digit = static_cast<unsigned char>(trimmed[index]);
        if (std::isdigit(digit) == 0) {
            return std::nullopt;
        }
        value = value * 10 + (digit - '0');
        if (value > 1000000000LL) {
            return std::nullopt;
        }
    }
    value *= sign;
    if (value > std::numeric_limits<int>::max() || value < std::numeric_limits<int>::min()) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::string utcNow()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm calendar{};
#if defined(_WIN32)
    gmtime_s(&calendar, &seconds);
#else
    gmtime_r(&seconds, &calendar);
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &calendar) == 0) {
        return {};
    }
    return buffer;
}

bool isDroppedField(const std::string& name)
{
    const std::string key = trim(name);
    if (std::find(std::begin(kDroppedFields), std::end(kDroppedFields), key) != std::end(kDroppedFields)) {
        return true;
    }
    if (key.rfind("FeaturesTraits", 0) == 0 || key.rfind("AdditionalNotes", 0) == 0) {
        return true;
    }
    return key == "AlliesOrganizations" || key == "Appearance";
}

bool fieldIndex(const std::string& name, std::string_view prefix, int& index)
{
    if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const std::optional<int> parsed = parseInteger(name.substr(prefix.size()));
    if (!parsed.has_value()) {
        return false;
    }
    index = *parsed;
    return true;
}

bool spellFieldOrder(const std::string& name, int& order)
{
    if (fieldIndex(name, "Spells ", order) || fieldIndex(name, "SpellName", order)) {
        return true;
    }
    return false;
}

bool isPlaceholder(const std::string& value)
{
    return !value.empty() && value.find_first_not_of('-') == std::string::npos;
}

const Spell* findSpellByName(const std::vector<Spell>& catalog, const std::string& name)
{
    for (const Spell& spell : catalog) {
        if (equalsInsensitive(spell.name, name)) {
            return &spell;
        }
    }
    return nullptr;
}

std::string spellLookupName(const std::string& value)
{
    const auto split = value.find(" - ");
    if (split == std::string::npos) {
        return value;
    }
    const std::string head = trim(value.substr(0, split));
    return head.empty() ? value : head;
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char character : text) {
        if (character == '\n' || character == '\r') {
            const std::string line = trim(current);
            if (!line.empty()) {
                lines.push_back(line);
            }
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    const std::string line = trim(current);
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

GearItem gearFromLine(const std::string& line)
{
    GearItem item;
    item.quantity = 1;
    item.name = line;
    std::size_t index = 0;
    while (index < line.size() && std::isdigit(static_cast<unsigned char>(line[index])) != 0) {
        ++index;
    }
    if (index == 0 || index >= line.size()) {
        return item;
    }
    std::size_t rest = index;
    if (line[rest] == 'x' || line[rest] == 'X') {
        ++rest;
    }
    if (rest >= line.size() || std::isspace(static_cast<unsigned char>(line[rest])) == 0) {
        return item;
    }
    while (rest < line.size() && std::isspace(static_cast<unsigned char>(line[rest])) != 0) {
        ++rest;
    }
    if (rest >= line.size()) {
        return item;
    }
    const std::optional<int> quantity = parseInteger(line.substr(0, index));
    if (!quantity.has_value() || *quantity < 1) {
        return item;
    }
    item.quantity = *quantity;
    item.name = line.substr(rest);
    return item;
}

bool parseClasses(const std::string& raw, std::vector<ClassLevel>& classes)
{
    classes.clear();
    std::size_t start = 0;
    const std::string text = trim(raw);
    if (text.empty()) {
        return false;
    }
    while (start <= text.size()) {
        const auto slash = text.find('/', start);
        const std::string piece = trim(text.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (piece.empty()) {
            classes.clear();
            return false;
        }
        const auto gap = piece.find_last_of(" \t");
        if (gap == std::string::npos) {
            classes.clear();
            return false;
        }
        const std::optional<int> level = parseInteger(piece.substr(gap + 1));
        const std::string name = trim(piece.substr(0, gap));
        if (!level.has_value() || name.empty()) {
            classes.clear();
            return false;
        }
        classes.push_back(ClassLevel{name, *level, {}});
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return !classes.empty();
}

const std::string* findValue(const std::map<std::string, std::string>& values, const std::string& name)
{
    const auto it = values.find(name);
    if (it == values.end()) {
        return nullptr;
    }
    return &it->second;
}

bool takeInteger(const std::map<std::string, std::string>& values, const char* field, int& destination,
                 bool& present, PdfImportReport& report)
{
    const std::string* text = findValue(values, field);
    if (text == nullptr) {
        return false;
    }
    const std::optional<int> parsed = parseInteger(*text);
    if (!parsed.has_value()) {
        report.parseFailures.push_back(std::string(field) + " is not a whole number.");
        return false;
    }
    destination = *parsed;
    present = true;
    report.fieldsUsed.emplace_back(field);
    return true;
}

void collectTextField(QPDFFormFieldObjectHelper field, std::map<std::string, std::string>& values, PdfImportReport& report)
{
    const std::string name = field.getFullyQualifiedName();
    if (name.empty()) {
        return;
    }
    if (field.isCheckbox() || field.isRadioButton() || field.isPushbutton()) {
        ++report.skippedButtons;
        return;
    }
    if (!field.isText()) {
        report.fieldsUnused.push_back(name);
        return;
    }
    const std::string value = trim(field.getValueAsString());
    if (value.empty() || isPlaceholder(value)) {
        return;
    }
    values.emplace(name, value);
}

std::vector<QPDFFormFieldObjectHelper> fieldsFromWidgetAnnotations(QPDF& pdf)
{
    std::vector<QPDFFormFieldObjectHelper> fields;
    QPDFPageDocumentHelper pages(pdf);
    for (QPDFPageObjectHelper page : pages.getAllPages()) {
        for (QPDFAnnotationObjectHelper annot : page.getAnnotations("/Widget")) {
            QPDFFormFieldObjectHelper field(annot.getObjectHandle());
            if (!field.getObjectHandle().isDictionary()) {
                continue;
            }
            fields.push_back(field);
        }
    }
    return fields;
}

std::map<std::string, std::string> readTextFields(const std::filesystem::path& path, PdfImportReport& report)
{
    QPDF pdf;
    try {
        pdf.processFile(path.string().c_str());
    } catch (const std::exception& error) {
        throw PdfImportError("Could not read " + path.string() + ": " + error.what());
    }

    QPDFAcroFormDocumentHelper forms(pdf);
    std::vector<QPDFFormFieldObjectHelper> fields;
    if (forms.hasAcroForm()) {
        fields = forms.getFormFields();
    }
    if (fields.empty()) {
        fields = fieldsFromWidgetAnnotations(pdf);
    }
    if (fields.empty()) {
        throw PdfImportError(kFlattenedMessage);
    }

    std::map<std::string, std::string> values;
    for (QPDFFormFieldObjectHelper field : fields) {
        collectTextField(field, values, report);
    }
    return values;
}

bool hasMappedValue(const std::map<std::string, std::string>& values)
{
    constexpr const char* names[] = {
        "CharacterName", "STR",         "DEX",        "CON",        "INT",         "WIS",
        "CHA",           "AC",          "HPCurrent",  "HPMax",      "HPTemp",      "CurrentHP",
        "MaxHP",         "TempHP",      "Passive",    "Passive1",   "Initiative",  "Init",
        "Speed",         "ProfBonus",   "ClassLevel", "CLASS  LEVEL", "Race ",    "Race",
        "RACE",          "Wpn Name",    "Equipment",
    };
    for (const char* name : names) {
        if (values.find(name) != values.end()) {
            return true;
        }
    }
    for (const auto& [name, value] : values) {
        (void)value;
        int index = 0;
        if (spellFieldOrder(name, index) || fieldIndex(name, "Wpn Name ", index) || fieldIndex(name, "Eq Name", index)) {
            return true;
        }
        if (name.rfind("SlotsTotal ", 0) == 0 || name.rfind("SlotsRemaining ", 0) == 0) {
            return true;
        }
    }
    return false;
}

// "Resistances: Fire, Poison; Immunities: Poison" or the export's lines
// "Resistance - Fire". Each damage type named after a Resist / Immun /
// Vulnerab word goes in that list.
Defenses parseDefenses(const std::string& text)
{
    Defenses defenses;
    std::vector<std::string>* current = nullptr;
    std::string word;
    auto flush = [&]() {
        const std::string lowered = asciiLower(word);
        word.clear();
        if (lowered.empty()) {
            return;
        }
        if (lowered.rfind("resist", 0) == 0) {
            current = &defenses.resistances;
        } else if (lowered.rfind("immun", 0) == 0) {
            current = &defenses.immunities;
        } else if (lowered.rfind("vulnerab", 0) == 0) {
            current = &defenses.vulnerabilities;
        } else if (current != nullptr && isDamageType(lowered) &&
                   std::find(current->begin(), current->end(), lowered) == current->end()) {
            current->push_back(lowered);
        }
    };
    for (const char c : text) {
        if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
            word.push_back(c);
        } else {
            flush();
        }
    }
    flush();
    return defenses;
}

}  // namespace

PdfImportResult importCharacterPdf(const std::filesystem::path& path, const std::vector<Spell>& catalog)
{
    PdfImportResult result;
    const std::map<std::string, std::string> values = readTextFields(path, result.report);
    if (!hasMappedValue(values)) {
        throw PdfImportError(kFlattenedMessage);
    }

    std::vector<std::string> consumed;
    auto consume = [&](const std::string& name) { consumed.push_back(name); };

    if (const std::string* name = findValue(values, "CharacterName")) {
        result.character.name = *name;
        result.presence.name = true;
        result.report.fieldsUsed.emplace_back("CharacterName");
        consume("CharacterName");
    }

    takeInteger(values, "STR", result.character.abilities.strength, result.presence.strength, result.report);
    takeInteger(values, "DEX", result.character.abilities.dexterity, result.presence.dexterity, result.report);
    takeInteger(values, "CON", result.character.abilities.constitution, result.presence.constitution, result.report);
    takeInteger(values, "INT", result.character.abilities.intelligence, result.presence.intelligence, result.report);
    takeInteger(values, "WIS", result.character.abilities.wisdom, result.presence.wisdom, result.report);
    takeInteger(values, "CHA", result.character.abilities.charisma, result.presence.charisma, result.report);
    for (const char* field : {"STR", "DEX", "CON", "INT", "WIS", "CHA"}) {
        if (values.find(field) != values.end()) {
            consume(field);
        }
    }

    auto takeFirstInteger = [&](std::initializer_list<const char*> fields, int& destination, bool& present) {
        for (const char* field : fields) {
            if (findValue(values, field) == nullptr) {
                continue;
            }
            takeInteger(values, field, destination, present, result.report);
            return;
        }
    };
    takeFirstInteger({"AC"}, result.character.ac, result.presence.ac);
    takeFirstInteger({"HPCurrent", "CurrentHP"}, result.character.hp.current, result.presence.hpCurrent);
    takeFirstInteger({"HPMax", "MaxHP"}, result.character.hp.max, result.presence.hpMax);
    takeFirstInteger({"HPTemp", "TempHP"}, result.character.tempHp, result.presence.tempHp);
    takeFirstInteger({"Passive", "Passive1"}, result.character.passivePerception, result.presence.passivePerception);
    int typedInitiative = 0;
    int typedProficiency = 0;
    takeFirstInteger({"Initiative", "Init"}, typedInitiative, result.presence.initiative);
    takeFirstInteger({"ProfBonus"}, typedProficiency, result.presence.proficiency);
    if (result.presence.hpMax && !result.presence.hpCurrent) {
        result.character.hp.current = result.character.hp.max;
        result.presence.hpCurrent = true;
    }
    for (const char* field : {"AC", "HPCurrent", "CurrentHP", "HPMax", "MaxHP", "HPTemp", "TempHP", "Passive",
                              "Passive1", "Initiative", "Init", "ProfBonus"}) {
        if (values.find(field) != values.end()) {
            consume(field);
        }
    }

    if (const std::string* speed = findValue(values, "Speed")) {
        result.character.speed = *speed;
        result.presence.speed = true;
        result.report.fieldsUsed.emplace_back("Speed");
        consume("Speed");
    }

    const char* classField = nullptr;
    if (findValue(values, "ClassLevel") != nullptr) {
        classField = "ClassLevel";
    } else if (findValue(values, "CLASS  LEVEL") != nullptr) {
        classField = "CLASS  LEVEL";
    }
    if (classField != nullptr) {
        const std::string* classes = findValue(values, classField);
        consume(classField);
        result.report.fieldsUsed.emplace_back(classField);
        if (parseClasses(*classes, result.character.classes)) {
            result.presence.classes = true;
        } else {
            // Nothing is copied: a Replace keeps the character's classes and notes.
            result.character.classes.clear();
            result.report.parseFailures.push_back("Class and level could not be read from \"" + *classes +
                                                  "\". Classes were left as they were.");
        }
    }

    const char* speciesField = nullptr;
    if (findValue(values, "Race ") != nullptr) {
        speciesField = "Race ";
    } else if (findValue(values, "Race") != nullptr) {
        speciesField = "Race";
    } else if (findValue(values, "RACE") != nullptr) {
        speciesField = "RACE";
    }
    if (speciesField != nullptr) {
        result.character.species = *findValue(values, speciesField);
        result.presence.species = true;
        result.report.fieldsUsed.emplace_back(speciesField);
        consume(speciesField);
    }

    std::vector<SpellField> weaponFields;
    if (const std::string* firstWeapon = findValue(values, "Wpn Name")) {
        weaponFields.push_back(SpellField{1, "Wpn Name", *firstWeapon});
    }
    std::map<int, SpellField> equipmentRows;
    std::map<int, int> equipmentQuantities;
    for (const auto& [name, value] : values) {
        int index = 0;
        if (fieldIndex(name, "Wpn Name ", index)) {
            weaponFields.push_back(SpellField{index, name, value});
        } else if (fieldIndex(name, "Eq Name", index)) {
            equipmentRows.emplace(index, SpellField{index, name, value});
        } else if (fieldIndex(name, "Eq Qty", index)) {
            const std::optional<int> quantity = parseInteger(value);
            consume(name);
            if (!quantity.has_value() || *quantity < 1) {
                result.report.parseFailures.push_back(name + " is not a whole number.");
            } else {
                equipmentQuantities.emplace(index, *quantity);
                result.report.fieldsUsed.push_back(name);
            }
        }
    }
    std::sort(weaponFields.begin(), weaponFields.end(), [](const SpellField& left, const SpellField& right) {
        if (left.order != right.order) {
            return left.order < right.order;
        }
        return left.fieldName < right.fieldName;
    });

    auto gearNameTaken = [&](const std::string& name) {
        return std::any_of(result.character.gear.begin(), result.character.gear.end(),
                           [&](const GearItem& item) { return equalsInsensitive(item.name, name); });
    };
    auto appendGearLine = [&](const std::string& line) {
        const GearItem item = gearFromLine(line);
        if (item.name.empty() || gearNameTaken(item.name)) {
            return;
        }
        result.character.gear.push_back(item);
        result.presence.gear = true;
    };

    if (equipmentRows.empty()) {
        for (const SpellField& field : weaponFields) {
            for (const std::string& line : splitLines(field.value)) {
                appendGearLine(line);
            }
            result.report.fieldsUsed.push_back(field.fieldName);
            consume(field.fieldName);
        }
        if (const std::string* text = findValue(values, "Equipment")) {
            for (const std::string& line : splitLines(*text)) {
                appendGearLine(line);
            }
            result.report.fieldsUsed.emplace_back("Equipment");
            consume("Equipment");
        }
    } else {
        for (const auto& [index, field] : equipmentRows) {
            GearItem item = gearFromLine(field.value);
            const auto quantity = equipmentQuantities.find(index);
            if (quantity != equipmentQuantities.end()) {
                item.quantity = quantity->second;
            }
            if (!item.name.empty()) {
                result.character.gear.push_back(item);
                result.presence.gear = true;
            }
            result.report.fieldsUsed.push_back(field.fieldName);
            consume(field.fieldName);
        }
        for (const SpellField& field : weaponFields) {
            result.report.fieldsUsed.push_back(field.fieldName);
            consume(field.fieldName);
            appendGearLine(field.value);
        }
        if (const std::string* text = findValue(values, "Equipment")) {
            for (const std::string& line : splitLines(*text)) {
                appendGearLine(line);
            }
            result.report.fieldsUsed.emplace_back("Equipment");
            consume("Equipment");
        }
    }

    std::vector<SpellField> spellFields;
    for (const auto& [name, value] : values) {
        int order = 0;
        if (!spellFieldOrder(name, order)) {
            continue;
        }
        SpellField field;
        field.order = order;
        field.fieldName = name;
        field.value = value;
        spellFields.push_back(std::move(field));
    }
    std::sort(spellFields.begin(), spellFields.end(), [](const SpellField& left, const SpellField& right) {
        if (left.order != right.order) {
            return left.order < right.order;
        }
        return left.fieldName < right.fieldName;
    });
    for (const SpellField& field : spellFields) {
        consume(field.fieldName);
        result.report.fieldsUsed.push_back(field.fieldName);
        result.presence.spells = true;
        const std::string lookup = spellLookupName(field.value);
        const Spell* catalogSpell = findSpellByName(catalog, lookup);
        if (catalogSpell == nullptr) {
            catalogSpell = findSpellByName(catalog, field.value);
        }
        CharacterSpell spell;
        spell.prepared = false;
        if (catalogSpell != nullptr) {
            const bool duplicate =
                std::any_of(result.character.spells.begin(), result.character.spells.end(),
                            [&](const CharacterSpell& existing) { return existing.id == catalogSpell->id; });
            if (duplicate) {
                result.report.parseFailures.push_back(catalogSpell->name + " was already imported.");
                continue;
            }
            spell.id = catalogSpell->id;
            spell.name = catalogSpell->name;
        } else {
            spell.name = lookup;
            result.report.unmatchedSpells.push_back(lookup);
        }
        result.character.spells.push_back(std::move(spell));
    }

    bool anySlot = false;
    for (int number = 19; number <= 27; ++number) {
        const int level = number - 18;
        const std::string totalKey = "SlotsTotal " + std::to_string(number);
        const std::string remainingKey = "SlotsRemaining " + std::to_string(number);
        const std::string* totalText = findValue(values, totalKey);
        const std::string* remainingText = findValue(values, remainingKey);
        if (totalText != nullptr) {
            consume(totalKey);
        }
        if (remainingText != nullptr) {
            consume(remainingKey);
        }
        std::optional<int> total;
        std::optional<int> remaining;
        if (totalText != nullptr) {
            total = parseInteger(*totalText);
            if (!total.has_value()) {
                result.report.parseFailures.push_back(totalKey + " is not a whole number.");
            } else {
                result.report.fieldsUsed.push_back(totalKey);
            }
        }
        if (remainingText != nullptr) {
            remaining = parseInteger(*remainingText);
            if (!remaining.has_value()) {
                result.report.parseFailures.push_back(remainingKey + " is not a whole number.");
            } else {
                result.report.fieldsUsed.push_back(remainingKey);
            }
        }
        if (!total.has_value() && !remaining.has_value()) {
            continue;
        }
        const int max = total.has_value() ? *total : *remaining;
        const int current = remaining.has_value() ? *remaining : *total;
        if (max == 0 && current == 0) {
            continue;
        }
        result.character.spellSlots.push_back(SpellSlot{level, current, max});
        anySlot = true;
    }
    result.presence.spellSlots = anySlot;

    // Proficiency marks. The D&D Beyond export fills "StrProf" with a dot and
    // "AthleticsProf" with P (proficient) or E (expertise, read as proficient).
    static constexpr std::pair<const char*, Ability> saveMarks[] = {
        {"StrProf", Ability::Strength},     {"DexProf", Ability::Dexterity}, {"ConProf", Ability::Constitution},
        {"IntProf", Ability::Intelligence}, {"WisProf", Ability::Wisdom},    {"ChaProf", Ability::Charisma},
    };
    bool sawSaveMark = false;
    for (const auto& [field, ability] : saveMarks) {
        const bool present = values.find(field) != values.end();
        result.character.savingThrows.*kSavingThrows[static_cast<std::size_t>(ability)].member = present;
        if (present) {
            sawSaveMark = true;
            consume(field);
            result.report.fieldsUsed.emplace_back(field);
        }
    }
    result.presence.saves = sawSaveMark;
    bool sawSkillMark = false;
    for (const SkillField& skill : kSkills) {
        std::string field = skill.key;
        field[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(field[0])));
        field += "Prof";
        const bool present = values.find(field) != values.end();
        result.character.skills.*skill.member = present;
        if (present) {
            sawSkillMark = true;
            consume(field);
            result.report.fieldsUsed.push_back(field);
        }
    }
    result.presence.skills = sawSkillMark;

    if (const std::string* defenses = findValue(values, "Defenses")) {
        consume("Defenses");
        result.report.fieldsUsed.emplace_back("Defenses");
        result.character.defenses = parseDefenses(*defenses);
        result.presence.defenses = true;
    }

    if (result.presence.initiative &&
        typedInitiative != abilityModifier(result.character.abilities.dexterity)) {
        result.character.initiativeOverride = typedInitiative;
    }
    if (result.presence.proficiency &&
        typedProficiency != proficiencyBonusForLevel(totalClassLevel(result.character))) {
        result.character.proficiencyOverride = typedProficiency;
    }

    for (const auto& [name, value] : values) {
        (void)value;
        if (isDroppedField(name)) {
            result.report.fieldsDropped.push_back(name);
            consume(name);
        }
    }
    std::sort(result.report.fieldsDropped.begin(), result.report.fieldsDropped.end());

    for (const auto& [name, value] : values) {
        (void)value;
        if (std::find(consumed.begin(), consumed.end(), name) == consumed.end()) {
            result.report.fieldsUnused.push_back(name);
        }
    }
    std::sort(result.report.fieldsUnused.begin(), result.report.fieldsUnused.end());

    CharacterImport info;
    info.source = kPdfImportSource;
    info.importedAt = utcNow();
    info.fileName = path.filename().string();
    result.character.external = std::move(info);
    return result;
}

void applyImportedCharacter(Character& target, const PdfImportResult& imported)
{
    const Character& source = imported.character;
    const PdfImportPresence& presence = imported.presence;
    if (presence.name) {
        target.name = source.name;
    }
    if (presence.strength) {
        target.abilities.strength = source.abilities.strength;
    }
    if (presence.dexterity) {
        target.abilities.dexterity = source.abilities.dexterity;
    }
    if (presence.constitution) {
        target.abilities.constitution = source.abilities.constitution;
    }
    if (presence.intelligence) {
        target.abilities.intelligence = source.abilities.intelligence;
    }
    if (presence.wisdom) {
        target.abilities.wisdom = source.abilities.wisdom;
    }
    if (presence.charisma) {
        target.abilities.charisma = source.abilities.charisma;
    }
    if (presence.ac) {
        target.ac = source.ac;
    }
    if (presence.hpCurrent) {
        target.hp.current = source.hp.current;
    }
    if (presence.hpMax) {
        target.hp.max = source.hp.max;
    }
    if (presence.tempHp) {
        target.tempHp = source.tempHp;
    }
    if (presence.passivePerception) {
        target.passivePerception = source.passivePerception;
    }
    if (presence.initiative) {
        target.initiativeOverride = source.initiativeOverride;
    }
    if (presence.speed) {
        target.speed = source.speed;
    }
    if (presence.proficiency) {
        target.proficiencyOverride = source.proficiencyOverride;
    }
    if (presence.species) {
        target.species = source.species;
    }
    if (presence.classes) {
        target.classes = source.classes;
    }
    if (presence.notes) {
        target.notes = source.notes;
    }
    if (presence.gear) {
        target.gear = source.gear;
    }
    if (presence.spells) {
        target.spells = source.spells;
    }
    if (presence.spellSlots) {
        target.spellSlots = source.spellSlots;
    }
    if (presence.saves) {
        target.savingThrows = source.savingThrows;
    }
    if (presence.skills) {
        target.skills = source.skills;
    }
    if (presence.defenses) {
        target.defenses = source.defenses;
    }
    target.external = source.external;
}

std::string formatPdfImportReport(const PdfImportReport& report)
{
    std::string text;
    auto addList = [&](const std::string& heading, const std::vector<std::string>& items) {
        if (items.empty()) {
            return;
        }
        text += heading;
        text += "\n";
        for (const std::string& item : items) {
            text += "  ";
            text += item;
            text += "\n";
        }
    };
    addList("Fields read:", report.fieldsUsed);
    addList("Spells with no SRD entry (the name is kept, with no description):", report.unmatchedSpells);
    addList("Dropped and not stored:", report.fieldsDropped);
    addList("Not used:", report.fieldsUnused);
    addList("Could not read:", report.parseFailures);
    if (report.skippedButtons > 0) {
        text += "Skipped ";
        text += std::to_string(report.skippedButtons);
        text += report.skippedButtons == 1 ? " button." : " buttons.";
        text += " Skill and save checkboxes are not mapped.\n";
    }
    text += "Create a new character, or replace one you pick. Encounters are not changed.\n";
    return text;
}

}  // namespace combat
