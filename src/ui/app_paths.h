#pragma once

#include <filesystem>

namespace combat::ui {

// The per-user folder that holds characters.json, custom-monsters.json, and
// encounters.json:
//   macOS   ~/Library/Application Support/CombatTracker
//   Linux   $XDG_DATA_HOME/combat-tracker (default ~/.local/share/combat-tracker)
//   Windows %APPDATA%\CombatTracker
// Requires QCoreApplication's application name to be set by main().
std::filesystem::path appDataFolder();

std::filesystem::path charactersFilePath();
std::filesystem::path customMonstersFilePath();
std::filesystem::path encountersFilePath();

// The packaged SRD folder, the first of these that has monsters.json:
//   $FREYA_SRD_DIR
//   the source tree's data/srd                              (this build; wins over a
//                                                            stale copy under build/share)
//   <executable folder>/../share/freya-combat-tracker/srd   (cmake --install)
//   <executable folder>/srd                                  (a copied folder)
std::filesystem::path srdDirectory();

// Packaged SRD files inside srdDirectory().
std::filesystem::path srdMonstersFilePath();
std::filesystem::path srdSpellsFilePath();
std::filesystem::path srdConditionsFilePath();
std::filesystem::path srdSpeciesFilePath();
std::filesystem::path srdAttributionFilePath();

}  // namespace combat::ui
