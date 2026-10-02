#include "data/pdf_import.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
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

std::string trim(std::string text)
{
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

bool equalFold(const std::string& left, const std::string& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

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
    gmtime_r(&seconds, &calendar);
    char buffer[32];
    if (std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &calendar) == 0) {
        return {};
    }
    return buffer;
}

bool isDroppedField(const std::string& name)
{
    return std::find(std::begin(kDroppedFields), std::end(kDroppedFields), name) != std::end(kDroppedFields);
}

bool isSpellNameField(const std::string& name)
{
    constexpr std::string_view prefix = "Spells ";
    return name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0;
}

std::optional<int> spellFieldOrder(const std::string& name)
{
    constexpr std::string_view prefix = "Spells ";
    return parseInteger(name.substr(prefix.size()));
}

const Spell* findSpellByName(const std::vector<Spell>& catalog, const std::string& name)
{
    for (const Spell& spell : catalog) {
        if (equalFold(spell.name, name)) {
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

std::map<std::string, std::string> readTextFields(const std::filesystem::path& path, PdfImportReport& report)
{
    QPDF pdf;
    try {
        pdf.processFile(path.string().c_str());
    } catch (const std::exception& error) {
        throw PdfImportError("Could not read " + path.string() + ": " + error.what());
    }

    QPDFAcroFormDocumentHelper forms(pdf);
    if (!forms.hasAcroForm()) {
        throw PdfImportError(kFlattenedMessage);
    }
    const std::vector<QPDFFormFieldObjectHelper> fields = forms.getFormFields();
    if (fields.empty()) {
        throw PdfImportError(kFlattenedMessage);
    }

    std::map<std::string, std::string> values;
    for (QPDFFormFieldObjectHelper field : fields) {
        const std::string name = field.getFullyQualifiedName();
        if (name.empty()) {
            continue;
        }
        if (field.isCheckbox() || field.isRadioButton() || field.isPushbutton()) {
            ++report.skippedButtons;
            continue;
        }
        if (!field.isText()) {
            report.fieldsUnused.push_back(name);
            continue;
        }
        const std::string value = trim(field.getValueAsString());
        if (value.empty()) {
            continue;
        }
        values.emplace(name, value);
    }
    return values;
}

bool hasMappedValue(const std::map<std::string, std::string>& values)
{
    constexpr const char* names[] = {
        "CharacterName", "STR",        "DEX",        "CON",         "INT",        "WIS",
        "CHA",           "AC",         "HPCurrent",  "HPMax",       "HPTemp",     "Passive",
        "Initiative",    "Speed",      "ProfBonus",  "ClassLevel",  "Race ",      "Race",
        "Wpn Name",      "Wpn Name 2", "Wpn Name 3", "Equipment",
    };
    for (const char* name : names) {
        if (values.find(name) != values.end()) {
            return true;
        }
    }
    for (const auto& [name, value] : values) {
        (void)value;
        if (isSpellNameField(name)) {
            return true;
        }
        if (name.rfind("SlotsTotal ", 0) == 0 || name.rfind("SlotsRemaining ", 0) == 0) {
            return true;
        }
    }
    return false;
}

void addGearLines(std::vector<GearItem>& gear, const std::string& text)
{
    for (const std::string& line : splitLines(text)) {
        gear.push_back(gearFromLine(line));
    }
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

    takeInteger(values, "AC", result.character.ac, result.presence.ac, result.report);
    takeInteger(values, "HPCurrent", result.character.hp.current, result.presence.hpCurrent, result.report);
    takeInteger(values, "HPMax", result.character.hp.max, result.presence.hpMax, result.report);
    takeInteger(values, "HPTemp", result.character.tempHp, result.presence.tempHp, result.report);
    takeInteger(values, "Passive", result.character.passivePerception, result.presence.passivePerception, result.report);
    takeInteger(values, "Initiative", result.character.initiativeBonus, result.presence.initiative, result.report);
    takeInteger(values, "ProfBonus", result.character.proficiencyBonus, result.presence.proficiency, result.report);
    for (const char* field : {"AC", "HPCurrent", "HPMax", "HPTemp", "Passive", "Initiative", "ProfBonus"}) {
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

    if (const std::string* classes = findValue(values, "ClassLevel")) {
        consume("ClassLevel");
        result.report.fieldsUsed.emplace_back("ClassLevel");
        result.presence.classes = true;
        if (!parseClasses(*classes, result.character.classes)) {
            result.character.classes.clear();
            result.character.notes = *classes;
            result.presence.notes = true;
            result.report.parseFailures.push_back("Class and level could not be read. The text was kept in notes.");
        }
    }

    if (const std::string* spacedSpecies = findValue(values, "Race ")) {
        result.character.species = *spacedSpecies;
        result.presence.species = true;
        result.report.fieldsUsed.emplace_back("Race ");
        consume("Race ");
    } else if (const std::string* plainSpecies = findValue(values, "Race")) {
        result.character.species = *plainSpecies;
        result.presence.species = true;
        result.report.fieldsUsed.emplace_back("Race");
        consume("Race");
    }

    for (const char* field : {"Wpn Name", "Wpn Name 2", "Wpn Name 3", "Equipment"}) {
        if (const std::string* text = findValue(values, field)) {
            addGearLines(result.character.gear, *text);
            result.presence.gear = true;
            result.report.fieldsUsed.emplace_back(field);
            consume(field);
        }
    }

    std::vector<SpellField> spellFields;
    for (const auto& [name, value] : values) {
        if (!isSpellNameField(name)) {
            continue;
        }
        SpellField field;
        const std::optional<int> order = spellFieldOrder(name);
        field.order = order.has_value() ? *order : 1000000;
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
        target.initiativeBonus = source.initiativeBonus;
    }
    if (presence.speed) {
        target.speed = source.speed;
    }
    if (presence.proficiency) {
        target.proficiencyBonus = source.proficiencyBonus;
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
