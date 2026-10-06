#pragma once

// Reads the rules out of a stat-block entry's wording, so a custom monster
// typed or pasted in plays like an SRD one: "Melee Attack Roll: +4 ... Hit: 9
// (2d6 + 2) Bludgeoning damage. If the target is a Medium or smaller creature,
// it has the Grappled condition (escape DC 12)." The SRD catalog is built by
// tools/build_srd_monsters.py with the same reading plus hand-written
// exceptions; this is the general part of it.

#include "core/monster.h"

#include <optional>
#include <string>
#include <vector>

namespace combat {

struct EntryReading {
    // What the entry does.
    std::optional<int> attackBonus;
    std::optional<SaveSpec> save;
    std::vector<DamagePart> damage;
    bool area = false;
    // From the name: "(Recharge 5–6)", "(3/Day)".
    std::optional<int> recharge;
    std::optional<int> perDay;
    // Its effects: who it can target and what it does to them.
    std::string targetCondition;
    std::string targetMaxSize;
    std::optional<int> failureHpThreshold;
    std::string failureHpEffect;
    std::vector<ConditionRider> riders;
    std::optional<SaveSpec> riderSave;
    std::optional<HpDrain> drain;
    bool advantageIfGrappled = false;
    // A helpful action for a creature it picks (War Cry).
    std::optional<Benefit> benefit;

    bool operator==(const EntryReading&) const = default;
};

EntryReading readEntry(const std::string& name, const std::string& effect);

// Copies only the effects (target rules, riders, rider save, drain).
void applyEffects(MonsterAttack& attack, const EntryReading& reading);
// Copies everything the reading found: roll, save, damage, area, limits, and
// effects.
void applyReading(MonsterAttack& attack, const EntryReading& reading);
// The attack's effects are exactly what this reading gives: nobody has edited
// them by hand since they were read.
bool effectsMatch(const MonsterAttack& attack, const EntryReading& reading);

// A bonus action, reaction, or legendary action with a save, an attack roll,
// or help for a creature it picks, as an action to aim (empty when it has
// none of them).
std::optional<MonsterAttack> targetedFromText(const std::string& name, const std::string& effect);

// A trait that changes the monster's attack rolls: Pack Tactics, "While
// Bloodied, ... Advantage on attack rolls", Sunlight Sensitivity, "If it takes
// Fire damage, it has Disadvantage on attack rolls ...".
std::optional<AttackModifier> readAttackModifier(const std::string& name, const std::string& effect);

// "Immediately after dealing damage to a creature that was already Bloodied"
// (Rampage).
bool readsAfterDamagingBloodied(const std::string& effect);

}  // namespace combat
