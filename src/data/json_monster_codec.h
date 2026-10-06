#pragma once

// Monster stat block <-> JSON, shared by the catalog files and by encounters,
// which keep a copy of each monster's stat block. Internal to combat_data.

#include "core/monster.h"
#include "data/json_util.h"

#include <string>

namespace combat::json_codec {

// Reads a stat block in the catalog shape. Fields added in schemaVersion 2
// (structured attacks, defenses, saves, XP) are optional. Throws
// MonsterDataError.
Monster monsterFromJson(const json_util::json& value, const std::string& context);
json_util::json monsterToJson(const Monster& monster);

}  // namespace combat::json_codec
