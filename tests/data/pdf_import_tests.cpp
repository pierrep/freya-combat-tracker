#include "data/json_character_store.h"
#include "data/json_sheet.h"
#include "data/pdf_import.h"
#include "test_harness.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace combat;
namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir()
    {
        std::random_device rd;
        m_path = fs::temp_directory_path() / ("combat-tracker-pdf-" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(m_path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

void writeFile(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// qpdf only returns a field from getFormFields when the dictionary is also a widget.
void markWidget(QPDFObjectHandle& field)
{
    field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    QPDFObjectHandle rect = QPDFObjectHandle::newArray();
    rect.appendItem(QPDFObjectHandle::newInteger(0));
    rect.appendItem(QPDFObjectHandle::newInteger(0));
    rect.appendItem(QPDFObjectHandle::newInteger(72));
    rect.appendItem(QPDFObjectHandle::newInteger(16));
    field.replaceKey("/Rect", rect);
}

void addTextField(QPDF& pdf, QPDFAcroFormDocumentHelper& forms, const std::string& name, const std::string& value)
{
    QPDFObjectHandle field = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
    field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
    field.replaceKey("/T", QPDFObjectHandle::newString(name));
    markWidget(field);
    forms.addFormField(QPDFFormFieldObjectHelper(field));
    QPDFFormFieldObjectHelper(field).setV(value, false);
}

void addCheckbox(QPDF& pdf, QPDFAcroFormDocumentHelper& forms, const std::string& name)
{
    QPDFObjectHandle field = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
    field.replaceKey("/FT", QPDFObjectHandle::newName("/Btn"));
    field.replaceKey("/T", QPDFObjectHandle::newString(name));
    field.replaceKey("/V", QPDFObjectHandle::newName("/Yes"));
    markWidget(field);
    forms.addFormField(QPDFFormFieldObjectHelper(field));
}

void writePdf(QPDF& pdf, const fs::path& path)
{
    QPDFWriter writer(pdf, path.string().c_str());
    writer.setStaticID(true);
    writer.write();
}

bool contains(const std::vector<std::string>& items, const std::string& value)
{
    return std::find(items.begin(), items.end(), value) != items.end();
}

std::vector<Spell> spellCatalog()
{
    return loadSpellCatalog(fs::path(COMBAT_TRACKER_SRD_DIR) / "spells.json");
}

}  // namespace

TEST_CASE("classic fillable fields map onto a character and prose is dropped")
{
    TempDir dir;
    const fs::path pdfPath = dir.path() / "freya-sheet.pdf";
    QPDF pdf;
    pdf.emptyPDF();
    QPDFAcroFormDocumentHelper forms(pdf);
    addTextField(pdf, forms, "CharacterName", "Freya");
    addTextField(pdf, forms, "Race ", "Elf");
    addTextField(pdf, forms, "ClassLevel", "Fighter 5 / Wizard 2");
    addTextField(pdf, forms, "STR", "16");
    addTextField(pdf, forms, "DEX", "14");
    addTextField(pdf, forms, "CON", "13");
    addTextField(pdf, forms, "INT", "12");
    addTextField(pdf, forms, "WIS", "10");
    addTextField(pdf, forms, "CHA", "8");
    addTextField(pdf, forms, "AC", "16");
    addTextField(pdf, forms, "HPCurrent", "12");
    addTextField(pdf, forms, "HPMax", "20");
    addTextField(pdf, forms, "HPTemp", "5");
    addTextField(pdf, forms, "Passive", "13");
    addTextField(pdf, forms, "Initiative", "+3");
    addTextField(pdf, forms, "ProfBonus", "+2");
    addTextField(pdf, forms, "Speed", "30 ft.");
    addTextField(pdf, forms, "Wpn Name", "Longsword");
    addTextField(pdf, forms, "Wpn Name 2", "2 Daggers");
    addTextField(pdf, forms, "Equipment", "Rope\n10 arrows");
    addTextField(pdf, forms, "Spells 1014", "Bless");
    addTextField(pdf, forms, "Spells 1015", "Homebrew Zap");
    addTextField(pdf, forms, "Spells 1016", "Fire Bolt - V, S");
    addTextField(pdf, forms, "SlotsTotal 19", "4");
    addTextField(pdf, forms, "SlotsRemaining 19", "3");
    addTextField(pdf, forms, "SlotsTotal 27", "1");
    addTextField(pdf, forms, "Features and Traits", "Sneak Attack essay");
    addTextField(pdf, forms, "PersonalityTraits", "A secret from the book");
    addTextField(pdf, forms, "STRmod", "+3");
    addCheckbox(pdf, forms, "Check Box 11");
    writePdf(pdf, pdfPath);

    const PdfImportResult imported = importCharacterPdf(pdfPath, spellCatalog());
    const Character& character = imported.character;
    CHECK(character.id.empty());
    CHECK_EQ(character.name, std::string("Freya"));
    CHECK_EQ(character.species, std::string("Elf"));
    CHECK_EQ(character.classes.size(), std::size_t{2});
    CHECK_EQ(character.classes[0].name, std::string("Fighter"));
    CHECK_EQ(character.classes[0].level, 5);
    CHECK(character.classes[0].subclass.empty());
    CHECK_EQ(character.classes[1].name, std::string("Wizard"));
    CHECK_EQ(character.classes[1].level, 2);
    CHECK_EQ(character.abilities.strength, 16);
    CHECK_EQ(character.abilities.dexterity, 14);
    CHECK_EQ(character.abilities.constitution, 13);
    CHECK_EQ(character.abilities.intelligence, 12);
    CHECK_EQ(character.abilities.wisdom, 10);
    CHECK_EQ(character.abilities.charisma, 8);
    CHECK_EQ(character.ac, 16);
    CHECK_EQ(character.hp.current, 12);
    CHECK_EQ(character.hp.max, 20);
    CHECK_EQ(character.tempHp, 5);
    CHECK_EQ(character.passivePerception, 13);
    CHECK_EQ(character.initiativeBonus, 3);
    CHECK_EQ(character.proficiencyBonus, 2);
    CHECK_EQ(character.speed, std::string("30 ft."));
    CHECK_EQ(character.gear.size(), std::size_t{4});
    CHECK_EQ(character.gear[0].name, std::string("Longsword"));
    CHECK_EQ(character.gear[0].quantity, 1);
    CHECK(!character.gear[0].equipped);
    CHECK_EQ(character.gear[1].name, std::string("Daggers"));
    CHECK_EQ(character.gear[1].quantity, 2);
    CHECK_EQ(character.gear[2].name, std::string("Rope"));
    CHECK_EQ(character.gear[2].quantity, 1);
    CHECK_EQ(character.gear[3].name, std::string("arrows"));
    CHECK_EQ(character.gear[3].quantity, 10);
    CHECK_EQ(character.spells.size(), std::size_t{3});
    CHECK_EQ(character.spells[0].id, std::string("bless"));
    CHECK_EQ(character.spells[0].name, std::string("Bless"));
    CHECK(!character.spells[0].prepared);
    CHECK(character.spells[1].id.empty());
    CHECK_EQ(character.spells[1].name, std::string("Homebrew Zap"));
    CHECK(!character.spells[1].prepared);
    CHECK_EQ(character.spells[2].id, std::string("fire-bolt"));
    CHECK_EQ(character.spells[2].name, std::string("Fire Bolt"));
    CHECK_EQ(character.spellSlots.size(), std::size_t{2});
    CHECK_EQ(character.spellSlots[0].level, 1);
    CHECK_EQ(character.spellSlots[0].current, 3);
    CHECK_EQ(character.spellSlots[0].max, 4);
    CHECK_EQ(character.spellSlots[1].level, 9);
    CHECK_EQ(character.spellSlots[1].current, 1);
    CHECK_EQ(character.spellSlots[1].max, 1);
    CHECK(character.notes.empty());
    CHECK(!character.savingThrows.strength);
    CHECK(!character.skills.perception);
    CHECK(character.external.has_value());
    CHECK_EQ(character.external->source, std::string(kPdfImportSource));
    CHECK_EQ(character.external->fileName, std::string("freya-sheet.pdf"));
    CHECK(!character.external->importedAt.empty());
    CHECK(character.external->importedAt.back() == 'Z');

    CHECK(contains(imported.report.fieldsUsed, "Race "));
    CHECK(contains(imported.report.fieldsDropped, "Features and Traits"));
    CHECK(contains(imported.report.fieldsDropped, "PersonalityTraits"));
    CHECK(contains(imported.report.unmatchedSpells, "Homebrew Zap"));
    CHECK(contains(imported.report.fieldsUnused, "STRmod"));
    CHECK(imported.report.skippedButtons >= 1);
    const std::string report = formatPdfImportReport(imported.report);
    CHECK(report.find("Sneak Attack") == std::string::npos);
    CHECK(report.find("A secret from the book") == std::string::npos);
    CHECK(report.find("+3") == std::string::npos);
    CHECK(report.find("STRmod") != std::string::npos);
    CHECK(report.find("Create a new character") != std::string::npos);
}

TEST_CASE("unparsed class text is kept in notes and classes stay empty")
{
    TempDir dir;
    const fs::path pdfPath = dir.path() / "class.pdf";
    QPDF pdf;
    pdf.emptyPDF();
    QPDFAcroFormDocumentHelper forms(pdf);
    addTextField(pdf, forms, "CharacterName", "Freya");
    addTextField(pdf, forms, "ClassLevel", "not a class");
    writePdf(pdf, pdfPath);

    const PdfImportResult imported = importCharacterPdf(pdfPath, {});
    CHECK(imported.character.classes.empty());
    CHECK_EQ(imported.character.notes, std::string("not a class"));
    CHECK(imported.presence.classes);
    CHECK(imported.presence.notes);
    CHECK(!imported.report.parseFailures.empty());
}

TEST_CASE("Race without a trailing space is used only when the spaced name is absent")
{
    TempDir dir;
    const fs::path pdfPath = dir.path() / "race.pdf";
    QPDF pdf;
    pdf.emptyPDF();
    QPDFAcroFormDocumentHelper forms(pdf);
    addTextField(pdf, forms, "CharacterName", "Freya");
    addTextField(pdf, forms, "Race", "Dwarf");
    writePdf(pdf, pdfPath);

    const PdfImportResult imported = importCharacterPdf(pdfPath, {});
    CHECK_EQ(imported.character.species, std::string("Dwarf"));
    CHECK(contains(imported.report.fieldsUsed, "Race"));
}

TEST_CASE("a PDF with no form fields asks for a fresh export")
{
    TempDir dir;
    const fs::path pdfPath = dir.path() / "flat.pdf";
    QPDF pdf;
    pdf.emptyPDF();
    writePdf(pdf, pdfPath);

    bool threw = false;
    try {
        (void)importCharacterPdf(pdfPath, {});
    } catch (const PdfImportError& error) {
        threw = true;
        const std::string message = error.what();
        CHECK(message.find("Export to PDF") != std::string::npos);
        CHECK(message.find("flattened") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("an empty form and dropped prose alone do not import")
{
    TempDir dir;
    const fs::path emptyPath = dir.path() / "empty-name.pdf";
    {
        QPDF pdf;
        pdf.emptyPDF();
        QPDFAcroFormDocumentHelper forms(pdf);
        addTextField(pdf, forms, "CharacterName", "   ");
        writePdf(pdf, emptyPath);
    }
    bool threw = false;
    try {
        (void)importCharacterPdf(emptyPath, {});
    } catch (const PdfImportError& error) {
        threw = true;
        CHECK(std::string(error.what()).find("Export to PDF") != std::string::npos);
    }
    CHECK(threw);

    const fs::path prosePath = dir.path() / "prose-only.pdf";
    {
        QPDF pdf;
        pdf.emptyPDF();
        QPDFAcroFormDocumentHelper forms(pdf);
        addTextField(pdf, forms, "Backstory", "A long non-SRD essay");
        writePdf(pdf, prosePath);
    }
    threw = false;
    try {
        (void)importCharacterPdf(prosePath, {});
    } catch (const PdfImportError& error) {
        threw = true;
        const std::string message = error.what();
        CHECK(message.find("A long non-SRD essay") == std::string::npos);
        CHECK(message.find("Export to PDF") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("replace keeps the id and leaves skills and death saves")
{
    Character target;
    target.id = "keep-me";
    target.name = "Old";
    target.hp = {8, 8};
    target.skills.athletics = true;
    target.deathSaves = {2, 1};
    target.classes.push_back(ClassLevel{"Rogue", 3, "Thief"});

    PdfImportResult imported;
    imported.character.name = "Freya";
    imported.character.hp = {12, 20};
    imported.presence.name = true;
    imported.presence.hpCurrent = true;
    imported.presence.hpMax = true;
    imported.character.external = CharacterImport{kPdfImportSource, "2026-10-02T01:02:03Z", "a.pdf"};

    applyImportedCharacter(target, imported);
    CHECK_EQ(target.id, std::string("keep-me"));
    CHECK_EQ(target.name, std::string("Freya"));
    CHECK_EQ(target.hp.current, 12);
    CHECK_EQ(target.hp.max, 20);
    CHECK(target.skills.athletics);
    CHECK_EQ(target.deathSaves.successes, 2);
    CHECK_EQ(target.deathSaves.failures, 1);
    CHECK_EQ(target.classes.size(), std::size_t{1});
    CHECK_EQ(target.classes[0].name, std::string("Rogue"));
    CHECK(target.external.has_value());
    CHECK_EQ(target.external->fileName, std::string("a.pdf"));
}

TEST_CASE("schema version 2 loads with no external and saves as version 3")
{
    Character character;
    character.id = "11111111-1111-4111-8111-111111111111";
    character.name = "Aria";
    const std::string version3 = serializeCharactersDocument({character});
    const auto versionAt = version3.find("\"schemaVersion\": 3");
    CHECK(versionAt != std::string::npos);
    std::string version2 = version3;
    version2.replace(versionAt, std::string("\"schemaVersion\": 3").size(), "\"schemaVersion\": 2");
    CHECK(version2.find("external") == std::string::npos);

    const auto loaded = parseCharactersDocument(version2);
    CHECK_EQ(loaded.size(), std::size_t{1});
    CHECK(!loaded[0].external.has_value());
    CHECK_EQ(loaded[0].name, std::string("Aria"));

    TempDir dir;
    const fs::path path = dir.path() / "characters.json";
    writeFile(path, version2);
    JsonCharacterStore store(path);
    CHECK(!store.loadAll()[0].external.has_value());
    store.saveAll(store.loadAll());
    const std::string saved = readFile(path);
    CHECK(saved.find("\"schemaVersion\": 3") != std::string::npos);
    CHECK(saved.find("external") == std::string::npos);
    CHECK(!fs::exists(dir.path() / "characters.v1.json"));
    CHECK(!fs::exists(dir.path() / "characters.v2.json"));
}

TEST_CASE("schema version 3 external round-trips and is omitted when empty")
{
    Character character;
    character.id = "22222222-2222-4222-8222-222222222222";
    character.name = "Freya";
    character.external = CharacterImport{"dndbeyond-pdf", "2026-10-02T03:04:05Z", "sheet.pdf"};

    TempDir dir;
    JsonCharacterStore store(dir.path() / "characters.json");
    store.saveAll({character});
    CHECK(store.loadAll() == std::vector<Character>{character});
    const std::string text = readFile(store.path());
    CHECK(text.find("\"schemaVersion\": 3") != std::string::npos);
    CHECK(text.find("dndbeyond-pdf") != std::string::npos);
    CHECK(text.find("sheet.pdf") != std::string::npos);

    character.external.reset();
    store.saveAll({character});
    CHECK(readFile(store.path()).find("external") == std::string::npos);
    CHECK(store.loadAll() == std::vector<Character>{character});
}
