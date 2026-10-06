#pragma once

#include "core/sheet.h"

#include <filesystem>
#include <string>
#include <vector>

namespace combat {

// Read-only SRD catalogs. A missing file, a parse error, or an unknown
// schemaVersion throws CatalogError. These files are never written.
std::vector<Spell> parseSpellCatalog(const std::string& text);
std::vector<Spell> loadSpellCatalog(const std::filesystem::path& path);

std::vector<Condition> parseConditionCatalog(const std::string& text);
std::vector<Condition> loadConditionCatalog(const std::filesystem::path& path);

std::vector<std::string> parseSpeciesCatalog(const std::string& text);
std::vector<std::string> loadSpeciesCatalog(const std::filesystem::path& path);

}  // namespace combat
