# Freya Combat Tracker

A local desktop combat tracker for fifth edition (2024 rules), built with C++20 and Qt 6 Widgets. 5E compatible.

Phase 6 fills a character from a PDF you exported on this machine. The app does not log in and does not call D&D Beyond. On the Characters page, Import PDF reads named form fields and shows a short report. Create new character adds a character. Replace, after you confirm, overwrites the imported fields on a character you pick and keeps that character's id. Encounters are not changed. There is no match on name.

The reader is qpdf (Apache-2.0) in the data library. It reads an AcroForm when the catalog has one. The current D&D Beyond export leaves the fields as widget annotations and omits that dictionary; those annotations are read too. It maps the name, the six scores, AC, current / maximum / temporary HP, passive Perception, the typed initiative total, speed, proficiency bonus, class and level, species, gear, spell names, and spell slots. Species comes from `Race `, `Race`, or `RACE`. Class and level come from `ClassLevel` or `CLASS  LEVEL`. Hit points come from `HPCurrent` / `HPMax` / `HPTemp` or `CurrentHP` / `MaxHP` / `TempHP`. A blank current HP is set to the maximum. A temporary HP of `--` is left at 0. Initiative comes from `Initiative` or `Init`. Passive Perception comes from `Passive` or `Passive1`. Gear comes from weapon names, an `Equipment` list, and `Eq Name` / `Eq Qty` rows. Spell names come from `Spells N` or `SpellNameN`. If a spell name is in the SRD catalog, the sheet stores that id. Otherwise it stores the name and no description. Feature essays, personality, ideals, bonds, flaws, backstory, and appearance are dropped and are not shown. Skill and save checkboxes are skipped. If the file has no filled form fields, the import stops and asks for a fresh browser Export to PDF. Page text is not read, and the PDF is not kept.

The Encounter Builder page creates, renames, and deletes encounters, and adds characters and monsters that already exist. Characters are created on the Characters page. Custom monsters are created on the Monsters page.

The Dashboard runs the fight. A dropdown at the top selects an encounter. You roll initiative for monsters, type a character's initiative, move through the turn order, and apply damage and healing to the active combatant. You add or remove SRD conditions, set or clear concentration, and count death saves. Temporary HP is spent before current HP. A character's spell slots can be spent, and Finish rest puts them back to the maximum stored on the sheet. The page also shows ability modifiers, the proficiency bonus from total level, save bonuses, and the Dexterity initiative modifier. The initiative number on the combatant is still the one turn order uses.

## Layout

| Path | Target | Depends on |
| --- | --- | --- |
| `src/core` | `combat_core` (static library) | C++ standard library only |
| `src/data` | `combat_data` (static library) | `combat_core`, nlohmann/json, qpdf |
| `src/ui` | `combat_app` (executable `freya-combat-tracker`) | `combat_core`, `combat_data`, Qt 6 Widgets |
| `tests/core`, `tests/data` | `core_tests`, `data_tests` | No Qt; run without a display |

## Build

Requirements: CMake 3.25+, Ninja, a C++20 compiler (GCC 13+, Clang 17+, Apple Clang, or MSVC 2022), Qt 6.4+ (6.8+ recommended; distro packages such as Ubuntu 24.04's `qt6-base-dev` work, or use the official installer or [aqtinstall](https://github.com/miurahr/aqtinstall)), and qpdf (Apache-2.0). CMake looks for qpdf in `$HOME/opt/qpdf` and then on the default search path. nlohmann/json is used from the system if CMake finds it, otherwise fetched at configure time.

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/ui/freya-combat-tracker
```

To build only the Qt-free libraries and tests, pass `-DCOMBAT_TRACKER_BUILD_APP=OFF`. Warnings are errors by default; pass `-DCOMBAT_TRACKER_WARNINGS_AS_ERRORS=OFF` to relax that.

## Saved data

Characters are saved to `characters.json` (`schemaVersion` 3) in the per-user app data folder:

- macOS: `~/Library/Application Support/CombatTracker/`
- Linux: `$XDG_DATA_HOME/combat-tracker/` (or `~/.local/share/combat-tracker/`)
- Windows: `%APPDATA%\CombatTracker\`

Every edit is saved right away by writing a temporary file and renaming it over the old one. If a file cannot be read, the app shows the error and leaves that file untouched.

A `schemaVersion` 1 roster still opens. The old `hp` number becomes both `hp.current` and `hp.max`, temporary HP starts at 0, and the new lists start empty. Opening it does not rewrite the file. The first save copies those original bytes to `characters.v1.json` beside it (and does not replace that copy later), then writes version 3. A version 2 file still opens, with `external` empty. An older build refuses a version 3 file.

An imported character may include `external`: `source` (`dndbeyond-pdf`), `importedAt` (UTC), and `fileName` (the file name only, not a remote id). Characters typed in omit that object.

The sheet stores spell ids (or a name, when the spell is not in the catalog) and a prepared flag. It does not store spell or condition descriptions. Spell slots are typed per level as a current count and a maximum. Species may be any name; the picker lists the SRD species. Gear is a name, a quantity, and an equipped flag.

Monsters come from two files with the same stat-block shape:

- `data/srd/monsters.json` is the packaged SRD 5.2.1 catalog (`source` `srd-5.2.1`), opened read-only, with `data/srd/ATTRIBUTION.txt`. The Monsters page and the character sheet show that attribution. `data/srd/spells.json` and `data/srd/species.json` are schemaVersion 1. `data/srd/conditions.json` is schemaVersion 2 and may include a `tags` list of short mechanical notes taken from that condition's SRD text. Spell and condition search is a case-insensitive name substring; a blank search lists every row by name.
- `custom-monsters.json` sits next to `characters.json`. A missing file means no custom monsters. Custom ids are UUIDs. A row whose id matches an SRD slug, or whose source is `srd-5.2.1`, is skipped and reported instead of replacing the SRD monster.

Search is offline: a case-insensitive substring of the name, plus optional exact creature-type and challenge-rating filters. A blank search lists every monster by name. When two rows share a name, the SRD row comes first. Custom rows are badged Custom.

Encounters are saved to `encounters.json` (`schemaVersion` 2) in that same app-data folder. A missing file means no encounters. A schemaVersion 1 file still opens: each combatant gets temporary HP 0, no stored maximum, no conditions, no concentration, and death saves at 0. Opening it does not rewrite the file. The next save writes version 2.

Each combatant keeps a copy of the name, AC, current HP, maximum HP, temporary HP, and, for a monster, the initiative bonus from the moment they were added. Damage and healing change that copy. Healing does not raise current HP above the stored maximum, and it does not change temporary HP. Conditions, concentration (an SRD spell id, or empty), and death-save counters also belong to the fight. Spending a spell slot or finishing a rest writes `characters.json`. Monster initiative is `d20 +` the stored bonus. Characters are typed, not rolled. Equal initiatives keep their order, including after a save. The typed initiative total on the combatant is the one the turn order uses.
