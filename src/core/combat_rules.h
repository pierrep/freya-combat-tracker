#pragma once

#include "core/character.h"
#include "core/encounter.h"

namespace combat {

// Damage spends temporary HP first, then current HP. Current HP does not go
// below 0; damage past that is not applied. Returns false, and changes
// nothing, when amount is negative.
bool applyDamage(Combatant& combatant, int amount);

// Healing raises current HP and does not change temporary HP. When a maximum
// is stored, current HP is brought down to it if it is already higher, and is
// not raised above it. No maximum is invented. Returns false, and changes
// nothing, when amount is negative.
bool applyHealing(Combatant& combatant, int amount);

// A typed current HP. When maximum is empty, current is returned unchanged.
int cappedHitPoints(int current, std::optional<int> maximum);

// Conditions are ids. An empty id or a duplicate is refused.
bool addCondition(Combatant& combatant, const std::string& conditionId);
bool removeCondition(Combatant& combatant, const std::string& conditionId);

// An empty spell id means the combatant is not concentrating. The app does
// not roll a Constitution save and does not clear this on its own.
void setConcentration(Combatant& combatant, const std::string& spellId);

// Adds delta to the success or failure counter. The result is clamped to the
// int range. There is no automatic stop at 3.
void adjustDeathSave(Combatant& combatant, bool success, int delta);

// Decrements the current count of the slot at this level by one. Returns
// false when that level is missing or its current count is already below 1.
bool spendSpellSlot(Character& character, int level);

// Sets every slot's current count to its stored maximum.
void finishRest(Character& character);

// Sum of class levels. The SRD proficiency table uses that total.
int totalClassLevel(const Character& character);

// SRD 5.2.1 Proficiency Bonus table (level or CR): up to 4 is +2, then +1
// for each further band of 4, through 29-30 at +9. A total above 30 stays
// at +9 because the table ends there.
int proficiencyBonusForLevel(int totalLevel);

// Ability modifier, plus the proficiency bonus when the character is
// proficient in that save.
int saveBonus(int abilityScore, bool proficient, int proficiencyBonus);

// The Dexterity modifier. It is not added to the combatant's initiative
// total; a typed total on the combatant wins.
int dexterityInitiativeModifier(const Character& character);

}  // namespace combat
