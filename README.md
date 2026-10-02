# Freya Combat Tracker

A local desktop combat tracker for fifth edition (2024 rules), built with C++20 and Qt 6 Widgets. 5E compatible.

Phase 4 records the character sheet: species, classes, spells, gear, conditions, and the supporting numbers (current and maximum HP, temporary HP, speed, typed proficiency and initiative bonuses, saves, skills, death saves, and notes). Spell and condition text comes from the packaged SRD 5.2.1 catalogs. A fight still keeps its own single HP value, copied from the character's current HP when they are added.

## Layout

| Path | Target | Depends on |
| --- | --- | --- |
| `src/core` | `combat_core` (static library) | C++ standard library only |
| `src/data` | `combat_data` (static library) | `combat_core`, nlohmann/json |
| `src/ui` | `combat_app` (executable `freya-combat-tracker`) | `combat_core`, `combat_data`, Qt 6 Widgets |
| `tests/core`, `tests/data` | `core_tests`, `data_tests` | No Qt; run without a display |

## Build

Requirements: CMake 3.25+, Ninja, a C++20 compiler (GCC 13+, Clang 17+, Apple Clang, or MSVC 2022), and Qt 6.4+ (6.8+ recommended; distro packages such as Ubuntu 24.04's `qt6-base-dev` work, or use the official installer or [aqtinstall](https://github.com/miurahr/aqtinstall)). nlohmann/json is used from the system if CMake finds it, otherwise fetched at configure time.

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/ui/freya-combat-tracker
```

To build only the Qt-free libraries and tests, pass `-DCOMBAT_TRACKER_BUILD_APP=OFF`. Warnings are errors by default; pass `-DCOMBAT_TRACKER_WARNINGS_AS_ERRORS=OFF` to relax that.

## Saved data

Characters are saved to `characters.json` (`schemaVersion` 2) in the per-user app data folder:

- macOS: `~/Library/Application Support/CombatTracker/`
- Linux: `$XDG_DATA_HOME/combat-tracker/` (or `~/.local/share/combat-tracker/`)
- Windows: `%APPDATA%\CombatTracker\`

Every edit is saved right away by writing a temporary file and renaming it over the old one. If a file cannot be read, the app shows the error and leaves that file untouched.

A `schemaVersion` 1 roster still opens. The old `hp` number becomes both `hp.current` and `hp.max`, temporary HP starts at 0, and the new lists start empty. Opening it does not rewrite the file. The first save copies those original bytes to `characters.v1.json` beside it (and does not replace that copy later), then writes version 2. An older build refuses a version 2 file.

The sheet stores spell ids (or a name, when the spell is not in the catalog) and a prepared flag. It does not store spell or condition descriptions. Spell slots are typed per level as a current count and a maximum. Species may be any name; the picker lists the SRD species. Gear is a name, a quantity, and an equipped flag.

Monsters come from two files with the same stat-block shape:

- `data/srd/monsters.json` is the packaged SRD 5.2.1 catalog (`source` `srd-5.2.1`), opened read-only, with `data/srd/ATTRIBUTION.txt`. The Monsters page and the character sheet show that attribution. `data/srd/spells.json`, `data/srd/conditions.json`, and `data/srd/species.json` are the same kind of read-only catalog (`schemaVersion` 1, `source` `srd-5.2.1`). Spell and condition search is a case-insensitive name substring; a blank search lists every row by name.
- `custom-monsters.json` sits next to `characters.json`. A missing file means no custom monsters. Custom ids are UUIDs. A row whose id matches an SRD slug, or whose source is `srd-5.2.1`, is skipped and reported instead of replacing the SRD monster.

Search is offline: a case-insensitive substring of the name, plus optional exact creature-type and challenge-rating filters. A blank search lists every monster by name. When two rows share a name, the SRD row comes first. Custom rows are badged Custom.

Encounters are saved to `encounters.json` (`schemaVersion` 1) in that same app-data folder. A missing file means no encounters. Each combatant keeps a copy of the name, AC, HP, and, for a monster, the initiative bonus from the moment they were added. Changing the fight does not change `characters.json` or either monster file. Monster initiative is `d20 +` that stored bonus. Characters are typed, not rolled. Equal initiatives keep their order, including after a save.
