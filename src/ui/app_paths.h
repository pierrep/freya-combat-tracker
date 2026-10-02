#pragma once

#include <filesystem>

namespace combat::ui {

// The per-user folder that holds characters.json:
//   macOS   ~/Library/Application Support/CombatTracker
//   Linux   $XDG_DATA_HOME/combat-tracker (default ~/.local/share/combat-tracker)
//   Windows %APPDATA%\CombatTracker
// Requires QCoreApplication's application name to be set by main().
std::filesystem::path appDataFolder();

std::filesystem::path charactersFilePath();
std::filesystem::path customMonstersFilePath();

// Packaged SRD files. The build sets COMBAT_TRACKER_SRD_DIR to data/srd.
std::filesystem::path srdMonstersFilePath();
std::filesystem::path srdAttributionFilePath();

}  // namespace combat::ui
