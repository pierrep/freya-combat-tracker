#pragma once

#include "core/character.h"
#include "core/sheet.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace combat {

inline constexpr char kPdfImportSource[] = "dndbeyond-pdf";

// The file has no AcroForm, or the named fields are empty. The message asks
// for a fresh Export to PDF. Page text is not read.
class PdfImportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Which imported fields were present. Replace copies only these onto the
// existing character. Skills, saves, conditions, and death saves are never
// set from a PDF.
struct PdfImportPresence {
    bool name = false;
    bool strength = false;
    bool dexterity = false;
    bool constitution = false;
    bool intelligence = false;
    bool wisdom = false;
    bool charisma = false;
    bool ac = false;
    bool hpCurrent = false;
    bool hpMax = false;
    bool tempHp = false;
    bool passivePerception = false;
    bool initiative = false;
    bool speed = false;
    bool proficiency = false;
    bool species = false;
    bool classes = false;
    bool notes = false;
    bool gear = false;
    bool spells = false;
    bool spellSlots = false;
};

struct PdfImportReport {
    std::vector<std::string> fieldsUsed;
    std::vector<std::string> unmatchedSpells;
    std::vector<std::string> fieldsDropped;
    std::vector<std::string> parseFailures;
    std::vector<std::string> fieldsUnused;
    int skippedButtons = 0;
};

struct PdfImportResult {
    // id is empty. The caller assigns one for a new character. external is set.
    Character character;
    PdfImportReport report;
    PdfImportPresence presence;
};

// Reads AcroForm values and maps the classic fillable field names. Does not
// log in, call a network service, or keep the PDF.
PdfImportResult importCharacterPdf(const std::filesystem::path& path, const std::vector<Spell>& catalog);

// Copies the fields the PDF actually contained. target.id is unchanged.
void applyImportedCharacter(Character& target, const PdfImportResult& imported);

// Review text. Dropped fields are named only; their prose is not included.
std::string formatPdfImportReport(const PdfImportReport& report);

}  // namespace combat
