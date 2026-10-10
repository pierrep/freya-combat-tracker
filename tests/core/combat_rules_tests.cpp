#include <algorithm>
#include "core/combat_rules.h"
#include "core/spell_rules.h"
#include "core/encounter_difficulty.h"
#include "core/stat_block_reader.h"
#include "test_harness.h"

#include <string>
#include <vector>

using namespace combat;

namespace {

Combatant hero()
{
    Combatant combatant;
    combatant.id = "c";
    combatant.source = kCombatantSourceCharacter;
    combatant.sourceId = "sheet";
    combatant.name = "Aria";
    combatant.hp = 10;
    combatant.maxHp = 12;
    combatant.tempHp = 3;
    return combatant;
}

Combatant orc()
{
    Combatant combatant;
    combatant.id = "m";
    combatant.source = kCombatantSourceMonster;
    combatant.sourceId = "orc";
    combatant.name = "Orc";
    combatant.hp = 15;
    combatant.maxHp = 15;
    combatant.ac = 13;
    return combatant;
}

Character wizard()
{
    Character character;
    character.id = "sheet";
    character.name = "Aria";
    character.abilities.dexterity = 16;
    character.abilities.constitution = 14;
    character.abilities.wisdom = 8;
    character.abilities.intelligence = 18;
    character.classes.push_back(ClassLevel{"Wizard", 5, "", 0});
    character.savingThrows.intelligence = true;
    character.hp = {20, 32};
    character.spellSlots.push_back(SpellSlot{1, 2, 4, false});
    character.spellSlots.push_back(SpellSlot{2, 0, 3, false});
    return character;
}

Monster dragon()
{
    Monster monster;
    monster.id = "red";
    monster.name = "Red Dragon";
    monster.hp = 200;
    monster.ac = 19;
    monster.legendaryActionUses = 3;
    MonsterAttack multi;
    multi.name = "Multiattack";
    multi.count = 3;
    MonsterAttack rend;
    rend.name = "Rend";
    rend.effect = "Melee Attack Roll: +14, reach 10 ft. Hit: 13 (1d10 + 8) Slashing damage.";
    rend.attackBonus = 14;
    rend.damage = {DamagePart{"1d10+8", "slashing", DamageWhen::Always}};
    rend.inMultiattack = true;
    MonsterAttack breath;
    breath.name = "Fire Breath (Recharge 5–6)";
    breath.save = SaveSpec{Ability::Dexterity, 21, true};
    breath.damage = {DamagePart{"17d6", "fire", DamageWhen::Always}};
    breath.recharge = 5;
    breath.area = true;
    MonsterAttack fireball;
    fireball.name = "Fireball (1/Day)";
    fireball.perDay = 1;
    monster.attacks = {multi, rend, breath, fireball};
    monster.legendaryActions = {MonsterFeature{"Pounce", "The dragon moves up to half its Speed, and it makes one Rend attack."}};
    monster.reactions = {MonsterFeature{"Tail", "Swipe."}};
    monster.bonusActions = {MonsterFeature{"Roar", "Roar."}};
    return monster;
}

RollDie fixed(int face)
{
    return [face](int) { return face; };
}

}  // namespace

TEST_CASE("damage spends temporary HP first and a monster at zero leaves the fight")
{
    Combatant monster = orc();
    monster.tempHp = 4;
    const DamageResult first = applyDamage(monster, 6);
    CHECK_EQ(first.tempSpent, 4);
    CHECK_EQ(first.hpLost, 2);
    CHECK_EQ(monster.hp, 13);
    const DamageResult second = applyDamage(monster, 100);
    CHECK(second.droppedToZero);
    CHECK(second.died);
    CHECK_EQ(monster.hp, 0);
    CHECK(!isInInitiative(monster));
    const DamageResult none = applyDamage(monster, -3);
    CHECK_EQ(none.taken, 0);
}

TEST_CASE("a character at zero is dying, stays in the turn order, and takes failures from damage")
{
    Combatant combatant = hero();
    combatant.concentration = "bless";
    const DamageResult down = applyDamage(combatant, 13);
    CHECK(down.droppedToZero);
    CHECK(!down.died);
    CHECK(down.concentrationEnded);
    CHECK(combatant.concentration.empty());
    CHECK(isDying(combatant));
    CHECK(isInInitiative(combatant));
    CHECK(hasCondition(combatant, "unconscious"));

    const DamageResult hit = applyDamage(combatant, 2);
    CHECK_EQ(hit.deathSaveFailures, 1);
    CHECK_EQ(combatant.deathSaves.failures, 1);
    const DamageResult crit = applyDamage(combatant, 2, {}, true);
    CHECK_EQ(crit.deathSaveFailures, 2);
    CHECK(crit.died);
    CHECK(combatant.dead);
    CHECK(!isInInitiative(combatant));
    CHECK_EQ(applyHealing(combatant, 10).healed, 0);
}

TEST_CASE("massive damage kills outright, at zero or from above it")
{
    Combatant combatant = hero();
    combatant.tempHp = 0;
    const DamageResult result = applyDamage(combatant, 10 + 12);
    CHECK(result.instantDeath);
    CHECK(combatant.dead);

    Combatant dying = hero();
    dying.tempHp = 0;
    applyDamage(dying, 10);
    CHECK(isDying(dying));
    CHECK(applyDamage(dying, 12).instantDeath);
}

TEST_CASE("healing a dying character brings it back and resets death saves")
{
    Combatant combatant = hero();
    combatant.tempHp = 0;
    applyDamage(combatant, 10);
    adjustDeathSave(combatant, false, 2);
    adjustDeathSave(combatant, true, 1);
    const HealingResult healed = applyHealing(combatant, 5);
    CHECK(healed.revived);
    CHECK_EQ(combatant.hp, 5);
    CHECK_EQ(combatant.deathSaves, (DeathSaves{0, 0}));
    CHECK(!hasCondition(combatant, "unconscious"));
    CHECK_EQ(applyHealing(combatant, 50).healed, 7);
    CHECK_EQ(combatant.hp, 12);
}

TEST_CASE("death saving throws: 1 is two failures, 20 revives, three successes stabilize")
{
    Combatant combatant = hero();
    combatant.tempHp = 0;
    applyDamage(combatant, 10);
    CHECK(rollDeathSave(combatant, 1) == DeathSaveResult::DoubleFailure);
    CHECK_EQ(combatant.deathSaves.failures, 2);
    CHECK(rollDeathSave(combatant, 20) == DeathSaveResult::Revived);
    CHECK_EQ(combatant.hp, 1);
    CHECK_EQ(combatant.deathSaves.failures, 0);

    applyDamage(combatant, 1);
    CHECK(rollDeathSave(combatant, 10) == DeathSaveResult::Success);
    CHECK(rollDeathSave(combatant, 15) == DeathSaveResult::Success);
    CHECK(rollDeathSave(combatant, 9) == DeathSaveResult::Failure);
    CHECK(rollDeathSave(combatant, 12) == DeathSaveResult::Stabilized);
    CHECK(combatant.stable);
    CHECK(!isDying(combatant));
    CHECK(isInInitiative(combatant));
    CHECK(rollDeathSave(combatant, 12) == DeathSaveResult::NotDying);
    applyDamage(combatant, 1);
    CHECK(isDying(combatant));

    combatant.exhaustion = 1;
    CHECK(rollDeathSave(combatant, 11) == DeathSaveResult::Failure);
    adjustDeathSave(combatant, false, 5);
    CHECK_EQ(combatant.deathSaves.failures, 3);
    CHECK(combatant.dead);
}

TEST_CASE("resistance halves, immunity removes, vulnerability doubles, per damage type")
{
    Combatant monster = orc();
    monster.hp = 100;
    monster.maxHp = 100;
    monster.defenses.resistances = {"fire"};
    monster.defenses.immunities = {"poison"};
    monster.defenses.vulnerabilities = {"cold", "fire"};
    CHECK_EQ(damageAfterDefenses(9, "fire", monster.defenses), 8);
    CHECK_EQ(damageAfterDefenses(9, "poison", monster.defenses), 0);
    CHECK_EQ(damageAfterDefenses(9, "cold", monster.defenses), 18);
    CHECK_EQ(damageAfterDefenses(9, "", monster.defenses), 9);
    const DamageResult result =
        applyDamage(monster, {TypedDamage{7, "slashing"}, TypedDamage{5, "poison"}, TypedDamage{3, "cold"}});
    CHECK_EQ(result.rolled, 15);
    CHECK_EQ(result.taken, 13);
    CHECK_EQ(monster.hp, 87);
    CHECK_EQ(result.notes.size(), std::size_t{2});
}

TEST_CASE("concentration checks use half the damage between 10 and 30")
{
    CHECK_EQ(concentrationDc(4), 10);
    CHECK_EQ(concentrationDc(25), 12);
    CHECK_EQ(concentrationDc(99), 30);
    Combatant combatant = hero();
    combatant.concentration = "bless";
    const DamageResult result = applyDamage(combatant, 4);
    CHECK(result.concentrationDc.has_value());
    CHECK_EQ(*result.concentrationDc, 10);
    CHECK_EQ(addCondition(combatant, "stunned"), AddConditionResult::Added);
    CHECK(combatant.concentration.empty());
}

TEST_CASE("conditions refuse duplicates and immunities, and exhaustion is a level")
{
    Combatant combatant = orc();
    combatant.conditionImmunities = {"poisoned"};
    CHECK_EQ(addCondition(combatant, "blinded"), AddConditionResult::Added);
    CHECK_EQ(addCondition(combatant, "blinded"), AddConditionResult::Duplicate);
    CHECK_EQ(addCondition(combatant, "poisoned"), AddConditionResult::Immune);
    CHECK_EQ(addCondition(combatant, ""), AddConditionResult::Empty);
    CHECK(removeCondition(combatant, "blinded"));
    CHECK(!removeCondition(combatant, "blinded"));

    CHECK_EQ(addCondition(combatant, "exhaustion"), AddConditionResult::Added);
    CHECK_EQ(addCondition(combatant, "exhaustion"), AddConditionResult::Added);
    CHECK_EQ(combatant.exhaustion, 2);
    CHECK_EQ(d20Penalty(combatant), 4);
    CHECK_EQ(exhaustionSpeedPenalty(2), 10);
    CHECK(removeCondition(combatant, "exhaustion"));
    CHECK_EQ(combatant.exhaustion, 1);
    setExhaustion(combatant, 6);
    CHECK(combatant.dead);
    CHECK(!isInInitiative(combatant));
}

TEST_CASE("Paralyzed also gives the Incapacitated condition")
{
    Combatant aria = hero();
    aria.concentration = "bless";
    ActiveCondition paralyzed;
    paralyzed.id = "paralyzed";
    paralyzed.source = "Hold Person";
    paralyzed.byId = "wizard";
    paralyzed.duration = ConditionDuration{"wizard", TurnBoundary::End, 2, false};
    CHECK_EQ(addCondition(aria, paralyzed), AddConditionResult::Added);
    CHECK(aria.concentration.empty());
    CHECK(hasCondition(aria, "paralyzed"));
    CHECK(hasCondition(aria, "incapacitated"));
    CHECK(isIncapacitated(aria));
    const auto included = std::find_if(aria.conditions.begin(), aria.conditions.end(),
                                       [](const ActiveCondition& row) { return row.id == "incapacitated"; });
    CHECK(included != aria.conditions.end());
    CHECK_EQ(included->tiedTo, std::string("paralyzed"));
    CHECK_EQ(included->source, std::string("Hold Person"));
    CHECK_EQ(included->byId, std::string("wizard"));
    CHECK(included->duration == paralyzed.duration);

    CHECK(removeCondition(aria, "paralyzed"));
    Encounter encounter;
    encounter.combatants.push_back(aria);
    const std::vector<ReleasedCondition> released = releaseConditions(encounter);
    CHECK_EQ(released.size(), std::size_t{1});
    CHECK_EQ(released[0].condition.id, std::string("incapacitated"));
    CHECK(!hasCondition(encounter.combatants[0], "paralyzed"));
    CHECK(!hasCondition(encounter.combatants[0], "incapacitated"));
    aria = encounter.combatants[0];

    // An Incapacitated condition from another effect stays when Paralyzed ends.
    ActiveCondition already;
    already.id = "incapacitated";
    already.source = "Banishment";
    CHECK_EQ(addCondition(aria, already), AddConditionResult::Added);
    CHECK_EQ(addCondition(aria, "paralyzed"), AddConditionResult::Added);
    CHECK_EQ(std::count_if(aria.conditions.begin(), aria.conditions.end(),
                           [](const ActiveCondition& row) { return row.id == "incapacitated"; }),
             1);
    const auto kept = std::find_if(aria.conditions.begin(), aria.conditions.end(),
                                   [](const ActiveCondition& row) { return row.id == "incapacitated"; });
    CHECK(kept->tiedTo.empty());
    CHECK_EQ(kept->source, std::string("Banishment"));
    CHECK(removeCondition(aria, "paralyzed"));
    Encounter still;
    still.combatants.push_back(aria);
    CHECK(releaseConditions(still).empty());
    CHECK(hasCondition(still.combatants[0], "incapacitated"));

    Combatant immune = hero();
    immune.conditionImmunities = {"incapacitated"};
    CHECK_EQ(addCondition(immune, "paralyzed"), AddConditionResult::Added);
    CHECK(hasCondition(immune, "paralyzed"));
    CHECK(!hasCondition(immune, "incapacitated"));
    CHECK(isIncapacitated(immune));
}

TEST_CASE("saves use the stored bonus and the exhaustion penalty, and paralysis fails Dex saves")
{
    Combatant combatant = orc();
    combatant.saveBonuses[static_cast<std::size_t>(Ability::Dexterity)] = 3;
    CHECK(rollSave(combatant, Ability::Dexterity, 15, 12).success);
    combatant.exhaustion = 1;
    CHECK(!rollSave(combatant, Ability::Dexterity, 15, 12).success);
    combatant.exhaustion = 0;
    addCondition(combatant, "paralyzed");
    const SaveRoll paralyzed = rollSave(combatant, Ability::Dexterity, 5, 20);
    CHECK(paralyzed.automaticFailure);
    CHECK(!paralyzed.success);
    CHECK(rollSave(combatant, Ability::Wisdom, 5, 20).success);
}

TEST_CASE("attack rolls: natural 20 is a critical hit, natural 1 misses")
{
    CHECK(resolveAttackRoll(4, 30, 0, 20).critical);
    CHECK(resolveAttackRoll(4, 30, 0, 20).hit);
    CHECK(!resolveAttackRoll(40, 5, 0, 1).hit);
    CHECK(resolveAttackRoll(4, 15, 0, 11).hit);
    CHECK(!resolveAttackRoll(4, 15, 2, 11).hit);
    CHECK_EQ(pickD20(RollMode::Advantage, 3, 17), 17);
    CHECK_EQ(pickD20(RollMode::Disadvantage, 3, 17), 3);
    CHECK_EQ(pickD20(RollMode::Normal, 3, 17), 3);
}

TEST_CASE("conditions suggest advantage and disadvantage, which cancel")
{
    Combatant attacker = orc();
    Combatant target = hero();
    CHECK(suggestedAttackMode(attacker, target, true) == RollMode::Normal);
    addCondition(target, "prone");
    CHECK(suggestedAttackMode(attacker, target, true) == RollMode::Advantage);
    CHECK(suggestedAttackMode(attacker, target, false) == RollMode::Disadvantage);
    addCondition(attacker, "poisoned");
    CHECK(suggestedAttackMode(attacker, target, true) == RollMode::Normal);
    addCondition(target, "unconscious");
    CHECK(meleeHitIsCritical(target));
}

TEST_CASE("the damage formula is what is rolled: chosen parts, a critical's doubled dice")
{
    const std::vector<DamagePart> axe{{"2d12+6", "slashing", DamageWhen::Always},
                                      {"2d8", "cold", DamageWhen::Always},
                                      {"1d4", "slashing", DamageWhen::Advantage},
                                      {"2d6", "acid", DamageWhen::Ongoing}};
    CHECK_EQ(damageFormula(axe, {}), std::string("2d12+6 slashing + 2d8 cold"));
    DamageOptions critical;
    critical.critical = true;
    CHECK_EQ(damageFormula(axe, critical), std::string("4d12+6 slashing + 4d8 cold"));
    DamageOptions advantage;
    advantage.advantage = true;
    CHECK_EQ(damageFormula(axe, advantage), std::string("2d12+6 slashing + 2d8 cold + 1d4 slashing"));
    // An alternative replaces the part of its type.
    const std::vector<DamagePart> swarm{{"2d4", "piercing", DamageWhen::Always},
                                        {"1d4", "piercing", DamageWhen::Alternative}};
    DamageOptions alternative;
    alternative.alternative = true;
    CHECK_EQ(damageFormula(swarm, alternative), std::string("1d4 piercing"));
    CHECK(damageFormula({}, {}).empty());
}

TEST_CASE("damage parts: advantage dice, alternatives, and ongoing damage")
{
    const std::vector<DamagePart> scimitar{{"1d6+2", "slashing", DamageWhen::Always},
                                           {"1d4", "slashing", DamageWhen::Advantage}};
    CHECK_EQ(totalDamage(rollDamageParts(scimitar, {}, fixed(3))), 5);
    DamageOptions advantage;
    advantage.advantage = true;
    CHECK_EQ(totalDamage(rollDamageParts(scimitar, advantage, fixed(3))), 8);
    DamageOptions critical;
    critical.critical = true;
    CHECK_EQ(totalDamage(rollDamageParts(scimitar, critical, fixed(3))), 8);

    const std::vector<DamagePart> swarm{{"2d4", "piercing", DamageWhen::Always},
                                        {"1d4", "piercing", DamageWhen::Alternative},
                                        {"2d6", "acid", DamageWhen::Ongoing}};
    CHECK_EQ(totalDamage(rollDamageParts(swarm, {}, fixed(4))), 8);
    DamageOptions bloodied;
    bloodied.alternative = true;
    const auto alternative = rollDamageParts(swarm, bloodied, fixed(4));
    CHECK_EQ(alternative.size(), std::size_t{1});
    CHECK_EQ(totalDamage(alternative), 4);
    CHECK_EQ(totalDamage(halveDamage({TypedDamage{9, "fire"}})), 4);
    CHECK_EQ(describeDamage({TypedDamage{6, "slashing"}, TypedDamage{3, "fire"}}), std::string("6 slashing + 3 fire"));
}

TEST_CASE("Multiattack is the action, a recharge action cannot join it, and uses are spent")
{
    const Monster monster = dragon();
    Combatant combatant = makeMonsterCombatant(monster, "dragon-row");
    const MonsterAttack& multi = monster.attacks[0];
    const MonsterAttack& rend = monster.attacks[1];
    const MonsterAttack& breath = monster.attacks[2];
    const MonsterAttack& fireball = monster.attacks[3];
    CHECK(!actionAvailability(combatant, rend, false).available);
    CHECK(useAction(combatant, multi, true));
    CHECK_EQ(combatant.economy.attacksRemaining, 3);
    CHECK(!actionAvailability(combatant, breath, true).available);
    CHECK(useAction(combatant, rend, true));
    CHECK(useAction(combatant, rend, true));
    CHECK(useAction(combatant, rend, true));
    CHECK(!useAction(combatant, rend, true));
    CHECK(monsterActionSpent(combatant));

    startTurnEconomy(combatant);
    CHECK(useAction(combatant, breath, true));
    CHECK(!actionAvailability(combatant, rend, true).available);
    startTurnEconomy(combatant);
    CHECK_EQ(actionAvailability(combatant, breath, true).reason, std::string("Recharging"));
    CHECK_EQ(combatant.usesRemaining.at(fireball.name), 1);
    CHECK(useAction(combatant, fireball, true));
    startTurnEconomy(combatant);
    CHECK_EQ(actionAvailability(combatant, fireball, true).reason, std::string("No uses left today"));
}

TEST_CASE("legendary actions are off-turn, reset each turn, and can allow one attack")
{
    const Monster monster = dragon();
    Combatant combatant = makeMonsterCombatant(monster, "dragon-row");
    CHECK_EQ(combatant.economy.legendaryRemaining, 3);
    CHECK(!featureAvailability(combatant, FeatureKind::Legendary, true).available);
    CHECK(useFeature(combatant, FeatureKind::Legendary, monster.legendaryActions[0], false));
    CHECK_EQ(combatant.economy.legendaryRemaining, 2);
    CHECK_EQ(combatant.economy.grantedAttack, std::string("Rend"));
    CHECK(actionAvailability(combatant, monster.attacks[1], false).available);
    CHECK(useAction(combatant, monster.attacks[1], false));
    CHECK(!actionAvailability(combatant, monster.attacks[1], false).available);

    CHECK(useFeature(combatant, FeatureKind::Reaction, monster.reactions[0], false));
    CHECK(!useFeature(combatant, FeatureKind::Reaction, monster.reactions[0], false));
    CHECK(!useFeature(combatant, FeatureKind::BonusAction, monster.bonusActions[0], false));
    CHECK(useFeature(combatant, FeatureKind::BonusAction, monster.bonusActions[0], true));
    startTurnEconomy(combatant);
    CHECK_EQ(combatant.economy.legendaryRemaining, 3);
    CHECK(!combatant.economy.reactionUsed);

    addCondition(combatant, "stunned");
    CHECK_EQ(actionAvailability(combatant, monster.attacks[0], true).reason, std::string("Incapacitated"));
}

TEST_CASE("turn start rolls recharges, and a dying character's turn asks for a death save")
{
    const Monster monster = dragon();
    Combatant beast = makeMonsterCombatant(monster, "dragon");
    beast.initiative = 20;
    Combatant aria = hero();
    aria.initiative = 10;
    aria.hp = 0;
    aria.tempHp = 0;
    Encounter encounter;
    encounter.id = "e";
    encounter.name = "Lair";
    encounter.combatants = {beast, aria};
    encounter.turnIndex = 0;
    useAction(encounter.combatants[0], monster.attacks[2], true);

    std::vector<TurnEvent> events = advanceTurn(encounter, fixed(5));
    CHECK_EQ(encounter.turnIndex, 1);
    CHECK_EQ(events.size(), std::size_t{1});
    CHECK(events[0].kind == TurnEvent::Kind::DeathSave);

    events = advanceTurn(encounter, fixed(4));
    CHECK_EQ(encounter.turnIndex, 0);
    CHECK_EQ(encounter.round, 2);
    CHECK_EQ(events.size(), std::size_t{1});
    CHECK(events[0].kind == TurnEvent::Kind::NotRecharged);
    events = advanceTurn(encounter, fixed(5));
    events = advanceTurn(encounter, fixed(5));
    CHECK(events[0].kind == TurnEvent::Kind::Recharged);
    CHECK(encounter.combatants[0].expended.empty());
}

TEST_CASE("durations end at the anchor's turn boundary, and save-ends asks at the end of its turn")
{
    Combatant goblin = orc();
    goblin.id = "goblin";
    Combatant aria = hero();
    aria.id = "aria";
    Encounter encounter;
    encounter.id = "e";
    encounter.name = "Fight";
    encounter.combatants = {goblin, aria};
    encounter.turnIndex = 0;

    ActiveCondition frightened{"frightened", makeDuration(encounter, "goblin", TurnBoundary::End, 1), std::nullopt};
    CHECK(frightened.duration->skipNext);
    addCondition(encounter.combatants[1], frightened);
    ActiveCondition poisoned{"poisoned", std::nullopt, SaveEnds{Ability::Constitution, 13}};
    addCondition(encounter.combatants[1], poisoned);
    CHECK_EQ(describeCondition(encounter.combatants[1].conditions[0], "Frightened", encounter),
             std::string("Frightened (until the end of Orc's next turn)"));

    advanceTurn(encounter);  // goblin's turn ends: skipped
    CHECK(hasCondition(encounter.combatants[1], "frightened"));
    std::vector<TurnEvent> events = advanceTurn(encounter);  // aria's turn ends
    CHECK_EQ(events.size(), std::size_t{1});
    CHECK(events[0].kind == TurnEvent::Kind::SaveToEnd);
    CHECK_EQ(events[0].dc, 13);
    events = advanceTurn(encounter);  // goblin's turn ends again
    CHECK(events[0].kind == TurnEvent::Kind::ConditionEnded);
    CHECK(!hasCondition(encounter.combatants[1], "frightened"));
}

TEST_CASE("a save the GM calls for is never asked at the end of the turn")
{
    const auto fear = spellAsAttack("Fear", 3, 15, std::nullopt);
    CHECK(fear.has_value()); if (!(fear.has_value())) { return; }
    CHECK(!fear->riders.empty()); if (!(!fear->riders.empty())) { return; }
    CHECK(fear->riders[0].saveOnDemand);
    CHECK(!fear->riders[0].saveEnds);
    // Hideous Laughter saves every turn on its own.
    const auto laughter = spellAsAttack("Hideous Laughter", 1, 15, std::nullopt);
    CHECK(laughter.has_value()); if (!(laughter.has_value())) { return; }
    CHECK(!laughter->riders[0].saveOnDemand);

    Combatant goblin = orc();
    goblin.id = "goblin";
    Combatant aria = hero();
    aria.id = "aria";
    Encounter encounter;
    encounter.id = "e";
    encounter.name = "Fight";
    encounter.combatants = {goblin, aria};
    encounter.turnIndex = 1;
    ActiveCondition scared{"frightened", std::nullopt, SaveEnds{Ability::Wisdom, 15}};
    scared.saveEnds->manual = true;
    addCondition(encounter.combatants[1], scared);
    CHECK(advanceTurn(encounter).empty());  // aria's turn ends: no automatic save
    CHECK(hasCondition(encounter.combatants[1], "frightened"));
}

TEST_CASE("long and short rests restore what the 2024 rules restore")
{
    Character character = wizard();
    character.tempHp = 5;
    character.exhaustion = 2;
    character.spellSlots.push_back(SpellSlot{3, 0, 2, true});
    character.classes[0].hitDiceSpent = 3;
    const ShortRestResult rest = shortRest(character, {5}, fixed(4));
    CHECK_EQ(rest.diceSpent, 2);
    CHECK_EQ(rest.healed, 12);
    CHECK_EQ(character.hp.current, 32);
    CHECK_EQ(character.spellSlots[2].current, 2);
    CHECK_EQ(character.spellSlots[1].current, 0);
    CHECK_EQ(hitDiceRemaining(character.classes[0]), 0);

    longRest(character);
    CHECK_EQ(character.tempHp, 0);
    CHECK_EQ(character.exhaustion, 1);
    CHECK_EQ(character.spellSlots[1].current, 3);
    CHECK_EQ(hitDiceRemaining(character.classes[0]), 5);
    CHECK(spendSpellSlot(character, 1));
    CHECK(!spendSpellSlot(character, 9));
}

TEST_CASE("proficiency follows the table unless overridden, and initiative uses Dexterity unless overridden")
{
    CHECK_EQ(proficiencyBonusForLevel(1), 2);
    CHECK_EQ(proficiencyBonusForLevel(4), 2);
    CHECK_EQ(proficiencyBonusForLevel(5), 3);
    CHECK_EQ(proficiencyBonusForLevel(17), 6);
    CHECK_EQ(proficiencyBonusForLevel(28), 8);
    CHECK_EQ(proficiencyBonusForLevel(40), 9);
    Character character = wizard();
    CHECK_EQ(proficiencyBonus(character), 3);
    CHECK_EQ(characterSaveBonus(character, Ability::Intelligence), 7);
    CHECK_EQ(characterSaveBonus(character, Ability::Wisdom), -1);
    character.proficiencyOverride = 4;
    CHECK_EQ(characterSaveBonus(character, Ability::Intelligence), 8);
    CHECK_EQ(initiativeModifier(character), 3);
    character.initiativeOverride = 8;
    CHECK_EQ(initiativeModifier(character), 8);
    CHECK_EQ(hitDieForClass("Barbarian"), 12);
    CHECK_EQ(hitDieForClass("wizard"), 6);
    CHECK_EQ(hitDieForClass("Artificer"), 8);
}

TEST_CASE("encounter difficulty uses the 2024 XP budget")
{
    CHECK_EQ(xpBudgetForLevel(1), (XpBudget{50, 75, 100}));
    CHECK_EQ(xpBudgetForLevel(20), (XpBudget{6400, 13200, 22000}));
    CHECK_EQ(partyXpBudget({3, 3, 3, 3}), (XpBudget{600, 900, 1600}));
    CHECK_EQ(xpForChallengeRating("1/4"), 50);
    CHECK_EQ(xpForChallengeRating("17"), 18000);
    CHECK(rateEncounter(1000, partyXpBudget({3, 3, 3, 3})) == Difficulty::High);
    CHECK(rateEncounter(2000, partyXpBudget({3, 3, 3, 3})) == Difficulty::AboveHigh);
    CHECK(rateEncounter(700, partyXpBudget({3, 3, 3, 3})) == Difficulty::Moderate);
    CHECK(rateEncounter(100, partyXpBudget({3, 3, 3, 3})) == Difficulty::BelowLow);

    Character a = wizard();
    Monster ogre;
    ogre.id = "ogre";
    ogre.name = "Ogre";
    ogre.challengeRating = "2";
    Encounter encounter;
    encounter.combatants = {makeCharacterCombatant(a, "a"), makeMonsterCombatant(ogre, "o1"),
                            makeMonsterCombatant(ogre, "o2")};
    const EncounterRating rating = rateEncounter(encounter, {a});
    CHECK_EQ(rating.monsterXp, 900);
    CHECK_EQ(rating.budget, xpBudgetForLevel(5));
    CHECK(rating.difficulty == Difficulty::High);
}

TEST_CASE("a character combatant follows the sheet and writes hit points back")
{
    Character character = wizard();
    character.defenses.resistances = {"fire"};
    character.tempHp = 4;
    Combatant combatant = makeCharacterCombatant(character, "row");
    CHECK_EQ(combatant.hp, 20);
    CHECK_EQ(combatant.tempHp, 4);
    CHECK_EQ(combatant.saveBonuses[static_cast<std::size_t>(Ability::Intelligence)], 7);
    CHECK_EQ(combatant.defenses.resistances.size(), std::size_t{1});
    applyDamage(combatant, 6);
    std::vector<Character> roster{character};
    CHECK(carryCharacterHitPoints(roster, combatant));
    CHECK_EQ(roster[0].tempHp, 0);
    CHECK_EQ(roster[0].hp.current, 18);
    roster[0].ac = 17;
    CHECK(refreshCharacterCombatant(combatant, roster[0]));
    CHECK_EQ(combatant.ac, 17);
    CHECK(!refreshCharacterCombatant(combatant, roster[0]));
}

TEST_CASE("an action's events: attack roll, saving-throw effect, damage, spells, and its name")
{
    auto has = [](const std::vector<std::string>& events, const std::string& event) {
        return std::find(events.begin(), events.end(), event) != events.end();
    };
    MonsterAttack claw;
    claw.name = "Claw";
    claw.attackBonus = 5;
    std::vector<std::string> events = actionEvents(claw.name, claw.effect, &claw, true);
    CHECK(has(events, kEndsOnAttackRoll));
    CHECK(has(events, kEndsOnDealsDamage));
    CHECK(has(events, "action:Claw"));
    CHECK(!has(events, kEndsOnAnySpell));

    MonsterAttack breath;
    breath.name = "Fire Breath (Recharge 5-6)";
    breath.save = SaveSpec{Ability::Dexterity, 15, true};
    events = actionEvents(breath.name, breath.effect, &breath, false);
    CHECK(has(events, kEndsOnSaveEffect));
    CHECK(has(events, "action:Fire Breath"));

    MonsterAttack spellcasting;
    spellcasting.name = "Spellcasting";
    spellcasting.effect = "The dragon casts one of the following spells, requiring no Material components.";
    events = actionEvents(spellcasting.name, spellcasting.effect, &spellcasting, false);
    CHECK(has(events, kEndsOnAnySpell));
    CHECK(has(events, kEndsOnVerbalSpell));
    // The Imp's Invisibility is a spell with no components.
    events = actionEvents("Invisibility", "The imp casts Invisibility on itself, requiring no spell components.",
                          nullptr, false);
    CHECK(has(events, kEndsOnAnySpell));
    CHECK(!has(events, kEndsOnVerbalSpell));

    Spell silent;
    silent.components = {"S"};
    CHECK(!spellHasVerbalComponent(silent));
    CHECK(spellHasVerbalComponent(Spell{}));  // unknown counts as Verbal
    CHECK(!has(spellEvents(silent), kEndsOnVerbalSpell));
    CHECK(has(spellEvents(silent), kEndsOnAnySpell));
}

TEST_CASE("what ends Invisible depends on what caused it")
{
    Combatant hidden;
    ActiveCondition hide;
    hide.id = "invisible";
    hide.source = "Hide";
    hide.endsOn = hideEndsOn();
    hidden.conditions.push_back(hide);
    CHECK(endConditionsOn(hidden, {kEndsOnDealsDamage, kEndsOnAnySpell}).empty());  // a silent spell
    CHECK_EQ(endConditionsOn(hidden, {kEndsOnSaveEffect}).size(), std::size_t{1});  // a breath weapon
    CHECK(!hasCondition(hidden, "invisible"));

    // The Invisibility spell: concentration, and any spell or damage ends it.
    Combatant imp;
    const SelfEffect spell{"invisible", "Invisibility", "invisibility", invisibilitySpellEndsOn()};
    CHECK(applySelfEffect(imp, spell) == AddConditionResult::Added);
    CHECK_EQ(imp.concentration, std::string("invisibility"));
    CHECK(endConditionsOn(imp, {kEndsOnSaveEffect}).empty());
    CHECK_EQ(endConditionsOn(imp, {kEndsOnDealsDamage}).size(), std::size_t{1});
    CHECK(imp.concentration.empty());  // ending the spell ends its concentration

    // Losing concentration ends the effect.
    CHECK(applySelfEffect(imp, spell) == AddConditionResult::Added);
    CHECK_EQ(endConcentration(imp).size(), std::size_t{1});
    CHECK(!hasCondition(imp, "invisible"));
    // So does concentrating on something else.
    applySelfEffect(imp, spell);
    setConcentration(imp, "bless");
    CHECK(!hasCondition(imp, "invisible"));
    // And an incapacitating condition.
    applySelfEffect(imp, spell);
    addCondition(imp, "stunned");
    CHECK(!hasCondition(imp, "invisible"));

    // Vanish ends on an attack roll or Consume Life, not on a breath weapon.
    Combatant wisp;
    applySelfEffect(wisp, SelfEffect{"invisible", "Vanish", "Vanish", {kEndsOnAttackRoll, "action:Consume Life"}});
    CHECK(endConditionsOn(wisp, {kEndsOnSaveEffect, kEndsOnVerbalSpell}).empty());
    CHECK_EQ(endConditionsOn(wisp, actionEvents("Consume Life", "", nullptr, false)).size(), std::size_t{1});
    CHECK(wisp.concentration.empty());

    // Removing it by hand ends the concentration it needed.
    applySelfEffect(wisp, SelfEffect{"invisible", "Vanish", "Vanish", {kEndsOnAttackRoll}});
    CHECK(removeCondition(wisp, "invisible"));
    CHECK(wisp.concentration.empty());
}

TEST_CASE("a trait's condition is there from the start and nothing ends it")
{
    Monster stalker;
    stalker.id = "invisible-stalker";
    stalker.name = "Invisible Stalker";
    MonsterFeature trait;
    trait.name = "Invisibility";
    trait.effect = "The stalker has the Invisible condition.";
    trait.selfEffect = SelfEffect{"invisible", "Invisibility trait", "", {}};
    stalker.traits.push_back(trait);
    Combatant row = makeMonsterCombatant(stalker, "s");
    CHECK(hasCondition(row, "invisible"));
    MonsterAttack slam;
    slam.name = "Slam";
    slam.attackBonus = 6;
    CHECK(endConditionsOn(row, actionEvents(slam.name, slam.effect, &slam, true)).empty());
    CHECK(hasCondition(row, "invisible"));

    Encounter encounter;
    encounter.combatants.push_back(row);
    encounter.combatants[0].conditions.clear();
    resetMonsters(encounter);
    CHECK(hasCondition(encounter.combatants[0], "invisible"));
}

TEST_CASE("bonus actions and reactions can recharge or be limited per day")
{
    Monster basilisk;
    basilisk.id = "basilisk";
    basilisk.name = "Basilisk";
    MonsterFeature gaze;
    gaze.name = "Petrifying Gaze (Recharge 4-6)";
    gaze.recharge = 4;
    basilisk.bonusActions.push_back(gaze);
    MonsterFeature shield;
    shield.name = "Protective Magic (3/Day)";
    shield.perDay = 3;
    basilisk.reactions.push_back(shield);

    Encounter encounter;
    encounter.combatants.push_back(makeMonsterCombatant(basilisk, "b"));
    Combatant hero;
    hero.id = "h";
    hero.source = kCombatantSourceCharacter;
    hero.name = "Hero";
    hero.hp = 10;
    hero.maxHp = 10;
    encounter.combatants.push_back(hero);
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    encounter.turnIndex = sortByInitiative(encounter.combatants, 0);
    Combatant& row = encounter.combatants[0];
    CHECK_EQ(row.usesRemaining.at(shield.name), 3);

    CHECK(useFeature(row, FeatureKind::BonusAction, gaze, true));
    startTurnEconomy(row);
    CHECK_EQ(featureAvailability(row, FeatureKind::BonusAction, gaze, true).reason, std::string("Recharging"));

    for (int i = 0; i < 3; ++i) {
        CHECK(useFeature(row, FeatureKind::Reaction, shield, false));
        startTurnEconomy(row);
    }
    CHECK_EQ(featureAvailability(row, FeatureKind::Reaction, shield, false).reason,
             std::string("No uses left today"));

    // The gaze rolls to recharge at the start of the basilisk's next turn.
    advanceTurn(encounter, [](int) { return 5; });  // to the hero
    const std::vector<TurnEvent> events = advanceTurn(encounter, [](int) { return 5; });  // back to the basilisk
    CHECK(std::any_of(events.begin(), events.end(), [](const TurnEvent& event) {
        return event.kind == TurnEvent::Kind::Recharged;
    }));
    CHECK(featureAvailability(encounter.combatants[0], FeatureKind::BonusAction, gaze, true).available);
}

TEST_CASE("an aura asks every other creature, until it is immune or the aura is off")
{
    Monster hag;
    hag.id = "sea-hag";
    hag.name = "Sea Hag";
    MonsterFeature vile;
    vile.name = "Vile Appearance";
    AuraSave aura;
    aura.ability = Ability::Wisdom;
    aura.dc = 11;
    aura.condition = "frightened";
    aura.immuneOnSuccess = true;
    aura.suppressedBy = {"Illusory Appearance"};
    vile.aura = aura;
    hag.traits.push_back(vile);

    Encounter encounter;
    encounter.combatants.push_back(makeMonsterCombatant(hag, "hag"));
    encounter.combatants.push_back(makeMonsterCombatant(hag, "hag2"));
    Combatant hero;
    hero.id = "hero";
    hero.source = kCombatantSourceCharacter;
    hero.name = "Hero";
    hero.hp = 10;
    hero.maxHp = 10;
    encounter.combatants.push_back(hero);
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 15;
    encounter.combatants[2].initiative = 10;
    encounter.turnIndex = sortByInitiative(encounter.combatants, 2);

    CHECK_EQ(aurasAtTurnStart(encounter, "hero").size(), std::size_t{2});
    // Allies are asked too: the second hag's aura reaches the first.
    CHECK_EQ(aurasAtTurnStart(encounter, "hag").size(), std::size_t{1});
    // An "enemies only" aura skips the other monsters.
    encounter.combatants[1].statBlock->traits[0].aura->enemiesOnly = true;
    CHECK(aurasAtTurnStart(encounter, "hag").empty());
    encounter.combatants[1].statBlock->traits[0].aura->enemiesOnly = false;

    // A failure: Frightened until the start of the hero's next turn.
    Combatant& target = encounter.combatants[2];
    const AuraCheck first = aurasAtTurnStart(encounter, "hero").at(0);
    CHECK(applyAuraFailure(encounter, target, first) == AddConditionResult::Added);
    CHECK(hasCondition(target, "frightened"));
    CHECK(target.conditions.back().duration->boundary == TurnBoundary::Start);
    CHECK_EQ(target.conditions.back().duration->anchorId, std::string("hero"));

    // A success: immune to that hag's aura, not the other's.
    markAuraImmune(target, first);
    CHECK_EQ(aurasAtTurnStart(encounter, "hero").size(), std::size_t{1});

    // The illusion switches the other hag's aura off.
    CHECK_EQ(suppressAuras(encounter.combatants[1], "Illusory Appearance").size(), std::size_t{1});
    CHECK(aurasAtTurnStart(encounter, "hero").empty());

    resetMonsters(encounter);
    CHECK_EQ(aurasAtTurnStart(encounter, "hero").size(), std::size_t{2});

    AuraSave table;
    table.failureDie = 8;
    table.failureTable = {{4, "does nothing"}, {6, "moves at random"}, {8, "attacks at random"}};
    CHECK_EQ(auraFailureOutcome(table, 5), std::string("moves at random"));
}

TEST_CASE("an encounter starts in the initiative phase and Start combat runs the first turn's start")
{
    Monster hag;
    hag.id = "sea-hag";
    hag.name = "Sea Hag";
    hag.legendaryActionUses = 0;
    Encounter encounter;
    encounter.started = false;
    Combatant hero;
    hero.id = "hero";
    hero.source = kCombatantSourceCharacter;
    hero.name = "Hero";
    hero.hp = 10;
    hero.maxHp = 10;
    hero.initiative = 18;
    ActiveCondition held;
    held.id = "restrained";
    held.duration = ConditionDuration{"hero", TurnBoundary::Start, 1, false};
    hero.conditions.push_back(held);
    encounter.combatants.push_back(makeMonsterCombatant(hag, "hag"));
    encounter.combatants[0].initiative = 12;
    encounter.combatants.push_back(hero);
    sortByInitiative(encounter.combatants, 0);

    const std::vector<TurnEvent> events = startCombat(encounter);
    CHECK(encounter.started);
    CHECK_EQ(encounter.round, 1);
    CHECK_EQ(encounter.combatants[static_cast<std::size_t>(encounter.turnIndex)].id, std::string("hero"));
    // The first creature's start of turn ran: its "until the start of its next turn" ended.
    CHECK(!hasCondition(encounter.combatants[static_cast<std::size_t>(encounter.turnIndex)], "restrained"));
    (void)events;

    resetMonsters(encounter);
    CHECK(!encounter.started);
}

TEST_CASE("an aura skips creatures of the wrong type and creatures immune to its condition")
{
    Monster hag;
    hag.id = "sea-hag";
    hag.name = "Sea Hag";
    hag.creatureType = "Fey";
    MonsterFeature vile;
    vile.name = "Vile Appearance";
    AuraSave aura;
    aura.condition = "frightened";
    aura.creatureTypes = {"beast", "humanoid"};
    vile.aura = aura;
    hag.traits.push_back(vile);
    Monster wisp;
    wisp.id = "will-o-wisp";
    wisp.name = "Will-o'-Wisp";
    wisp.creatureType = "Undead";
    Monster rats;
    rats.id = "swarm-of-rats";
    rats.name = "Swarm of Rats";
    rats.creatureType = "Swarm of Tiny Beasts";
    Monster bandit;
    bandit.id = "bandit";
    bandit.name = "Bandit";
    bandit.creatureType = "Humanoid";
    bandit.conditionImmunities = {"frightened"};

    Encounter encounter;
    encounter.combatants.push_back(makeMonsterCombatant(hag, "hag"));
    encounter.combatants.push_back(makeMonsterCombatant(wisp, "wisp"));
    encounter.combatants.push_back(makeMonsterCombatant(rats, "rats"));
    encounter.combatants.push_back(makeMonsterCombatant(bandit, "bandit"));
    Combatant hero;
    hero.id = "hero";
    hero.source = kCombatantSourceCharacter;
    hero.name = "Hero";
    hero.hp = 10;
    encounter.combatants.push_back(hero);
    for (Combatant& combatant : encounter.combatants) {
        combatant.initiative = 10;
    }

    CHECK(aurasAtTurnStart(encounter, "wisp").empty());        // Undead
    CHECK_EQ(aurasAtTurnStart(encounter, "rats").size(), std::size_t{1});  // a swarm of Beasts
    CHECK(aurasAtTurnStart(encounter, "bandit").empty());      // cannot be Frightened
    CHECK_EQ(aurasAtTurnStart(encounter, "hero").size(), std::size_t{1});  // characters are Humanoids
    CHECK_EQ(creatureTypeKey(encounter.combatants[2]), std::string("beast"));
}

TEST_CASE("an action can require a condition or a size of its target")
{
    MonsterAttack glare;
    glare.name = "Death Glare (Recharge 5-6)";
    glare.targetCondition = "frightened";
    Combatant hero;
    hero.id = "hero";
    hero.source = kCombatantSourceCharacter;
    hero.name = "Hero";
    hero.hp = 15;
    CHECK(!targetRequirementProblem(glare, hero).empty());
    addCondition(hero, "frightened");
    CHECK(targetRequirementProblem(glare, hero).empty());

    MonsterAttack engulf;
    engulf.name = "Engulf";
    engulf.targetMaxSize = "Medium";
    CHECK(targetRequirementProblem(engulf, hero, "Gnome").empty());
    Monster ogre;
    ogre.id = "ogre";
    ogre.name = "Ogre";
    ogre.size = "Large";
    const Combatant big = makeMonsterCombatant(ogre, "ogre");
    CHECK(!targetRequirementProblem(engulf, big).empty());
    CHECK_EQ(creatureSize(hero, "Halfling"), std::string("Small"));

    killOutright(hero);
    CHECK(hero.dead);
}

TEST_CASE("a grapple rider records who grapples, ties Restrained to it, and ends when the grappler falls")
{
    Encounter encounter;
    Monster devil;
    devil.id = "chain-devil";
    devil.name = "Chain Devil";
    devil.size = "Medium";
    encounter.combatants.push_back(makeMonsterCombatant(devil, "devil"));
    encounter.combatants.push_back(hero());
    encounter.started = true;
    MonsterAttack chain;
    chain.name = "Chain";
    chain.attackBonus = 7;
    ConditionRider grab;
    grab.conditions = {"grappled"};
    grab.targetMaxSize = "Large";
    grab.escapeDc = 14;
    ConditionRider hold;
    hold.conditions = {"restrained"};
    hold.tiedTo = "grappled";
    chain.riders = {grab, hold};
    Combatant& devilRow = encounter.combatants[0];
    Combatant& aria = encounter.combatants[1];

    CHECK(riderBlocked(grab, aria).empty());
    RiderOutcome outcome = applyRider(encounter, devilRow, aria, chain, grab);
    CHECK_EQ(outcome.added, std::vector<std::string>{"grappled"});
    applyRider(encounter, devilRow, aria, chain, hold);
    CHECK(hasCondition(aria, "restrained"));
    CHECK(isGrappledBy(aria, "devil"));
    CHECK(!isGrappledBy(aria, "someone-else"));
    CHECK_EQ(aria.conditions[0].source, std::string("Chain Devil's Chain, escape DC 14"));

    // Escaping the grapple ends Restrained too.
    removeCondition(aria, "grappled");
    std::vector<ReleasedCondition> released = releaseConditions(encounter);
    CHECK_EQ(released.size(), std::size_t{1});
    CHECK(!hasCondition(aria, "restrained"));

    // A grapple ends when the grappler is Incapacitated or drops.
    applyRider(encounter, devilRow, aria, chain, grab);
    applyRider(encounter, devilRow, aria, chain, hold);
    CHECK(releaseConditions(encounter).empty());
    addCondition(devilRow, "stunned");
    released = releaseConditions(encounter);
    CHECK_EQ(released.size(), std::size_t{2});
    CHECK(aria.conditions.empty());
}

TEST_CASE("riders skip creatures too big, of the wrong type, or with too many Hit Points")
{
    ConditionRider prone;
    prone.conditions = {"prone"};
    prone.targetMaxSize = "Medium";
    Monster ogre;
    ogre.id = "ogre";
    ogre.name = "Ogre";
    ogre.size = "Large";
    ogre.creatureType = "Giant";
    const Combatant big = makeMonsterCombatant(ogre, "ogre");
    CHECK(!riderBlocked(prone, big).empty());
    CHECK(riderBlocked(prone, hero(), "Human").empty());

    ConditionRider paralysis;
    paralysis.conditions = {"paralyzed"};
    paralysis.exceptTypes = {"undead", "elf"};
    CHECK(!riderBlocked(paralysis, hero(), "High Elf").empty());
    CHECK(riderBlocked(paralysis, hero(), "Dwarf").empty());
    Monster zombie;
    zombie.id = "zombie";
    zombie.name = "Zombie";
    zombie.creatureType = "Undead";
    CHECK(!riderBlocked(paralysis, makeMonsterCombatant(zombie, "z")).empty());

    ConditionRider nightmare;
    nightmare.conditions = {"unconscious"};
    nightmare.targetMaxHp = 20;
    Combatant aria = hero();
    CHECK(riderBlocked(nightmare, aria).empty());
    aria.hp = 21;
    CHECK(!riderBlocked(nightmare, aria).empty());
}

TEST_CASE("a rider's condition can be timed, saved against, and worsen on a second failure")
{
    Encounter encounter;
    Monster basilisk;
    basilisk.id = "basilisk";
    basilisk.name = "Basilisk";
    encounter.combatants.push_back(makeMonsterCombatant(basilisk, "b"));
    encounter.combatants.push_back(hero());
    encounter.started = true;
    MonsterAttack gaze;
    gaze.name = "Petrifying Gaze (Recharge 4–6)";
    gaze.save = SaveSpec{Ability::Constitution, 12, false};
    ConditionRider stone;
    stone.conditions = {"restrained"};
    stone.on = kRiderOnFailure;
    stone.saveEnds = true;
    stone.worsensTo = {"petrified"};
    applyRider(encounter, encounter.combatants[0], encounter.combatants[1], gaze, stone);
    Combatant& aria = encounter.combatants[1];
    CHECK(aria.conditions[0].saveEnds.has_value());
    CHECK_EQ(aria.conditions[0].saveEnds->dc, 12);
    CHECK_EQ(worsenCondition(aria, "restrained"), std::vector<std::string>{"petrified"});
    CHECK(!hasCondition(aria, "restrained"));
    CHECK(hasCondition(aria, "petrified"));
    CHECK(!aria.conditions[0].saveEnds.has_value());

    // "until the start of the assassin's next turn"
    ConditionRider poison;
    poison.conditions = {"poisoned"};
    poison.until = kUntilSourceStart;
    applyRider(encounter, encounter.combatants[0], aria, gaze, poison);
    const ActiveCondition& row = aria.conditions.back();
    CHECK(row.duration.has_value());
    CHECK_EQ(row.duration->anchorId, std::string("b"));
    CHECK(row.duration->boundary == TurnBoundary::Start);

    // "which ends early if the target takes any damage"
    ConditionRider sleep;
    sleep.conditions = {"unconscious"};
    sleep.endsOn = {kEndsOnTakesDamage};
    applyRider(encounter, encounter.combatants[0], aria, gaze, sleep);
    CHECK(hasCondition(aria, "unconscious"));
    endConditionsOn(aria, {kEndsOnTakesDamage});
    CHECK(!hasCondition(aria, "unconscious"));
}

TEST_CASE("a d20 roll is written out for its dice card")
{
    D20Calculation attack;
    attack.face = 14;
    attack.bonus = 5;
    attack.total = 19;
    attack.against = "AC 15";
    CHECK_EQ(describeD20(attack), std::string("14 + 5 = 19 vs AC 15"));

    D20Calculation save;
    save.face = 14;
    save.otherFace = 7;
    save.mode = RollMode::Advantage;
    save.bonus = -1;
    save.penalty = 2;
    save.total = 11;
    save.against = "DC 13";
    CHECK_EQ(describeD20(save), std::string("14 (higher of 14 and 7) \u2212 1 \u2212 2 Exhaustion = 11 vs DC 13"));

    D20Calculation stealth;
    stealth.face = 12;
    stealth.bonus = 4;
    stealth.bonusLabel = "Stealth";
    stealth.total = 16;
    CHECK_EQ(describeD20(stealth), std::string("12 + 4 Stealth = 16"));

    D20Calculation death;
    death.face = 8;
    death.against = "10";
    CHECK_EQ(describeD20(death), std::string("8 vs 10"));
}

TEST_CASE("an unconscious creature is not affected by a condition that needs sight or hearing")
{
    Encounter encounter;
    Monster caster;
    caster.id = "caster";
    caster.name = "Caster";
    encounter.combatants.push_back(makeMonsterCombatant(caster, "c"));
    encounter.combatants.push_back(hero());
    encounter.started = true;
    Combatant& aria = encounter.combatants[1];

    const auto fear = spellAsAttack("Fear", 3, 15, std::nullopt);
    CHECK(fear.has_value());
    if (!fear.has_value() || fear->riders.empty()) {
        return;
    }
    CHECK(fear->riders[0].requiresSenses);
    {
        ActiveCondition sleeping;
        sleeping.id = "unconscious";
        addCondition(aria, std::move(sleeping));
    }
    const RiderOutcome asleep = applyRider(encounter, encounter.combatants[0], aria, *fear, fear->riders[0]);
    CHECK(asleep.added.empty());
    CHECK(!asleep.unaware.empty());
    CHECK(!hasCondition(aria, "frightened"));

    // A stat block says it in words.
    MonsterAttack gaze;
    gaze.name = "Gaze";
    gaze.effect = "Each creature that can see the monster is frightened.";
    ConditionRider scare;
    scare.conditions = {"frightened"};
    CHECK(applyRider(encounter, encounter.combatants[0], aria, gaze, scare).added.empty());

    // An awake target is affected; a Blinded one is not affected by sight.
    Combatant bob = hero();
    bob.id = "bob";
    CHECK(!applyRider(encounter, encounter.combatants[0], bob, gaze, scare).added.empty());
    Combatant blind = hero();
    blind.id = "blind";
    {
        ActiveCondition sightless;
        sightless.id = "blinded";
        addCondition(blind, std::move(sightless));
    }
    CHECK(applyRider(encounter, encounter.combatants[0], blind, gaze, scare).added.empty());

    // Knocked out by the same action's damage: it saw the action coming.
    CHECK(!applyRider(encounter, encounter.combatants[0], aria, gaze, scare, false).added.empty());
}

TEST_CASE("ongoing damage comes at the start of the target's or the monster's turn")
{
    Encounter encounter;
    Monster worm;
    worm.id = "worm";
    worm.name = "Purple Worm";
    encounter.combatants.push_back(makeMonsterCombatant(worm, "worm"));
    encounter.combatants.push_back(hero());
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    encounter.started = true;
    encounter.turnIndex = 0;
    MonsterAttack swallow;
    swallow.name = "Swallow";
    ConditionRider inside;
    inside.conditions = {"blinded", "restrained"};
    inside.ongoing = {DamagePart{"5d6", "acid", DamageWhen::Always}};
    inside.ongoingAt = kOngoingAtSource;
    applyRider(encounter, encounter.combatants[0], encounter.combatants[1], swallow, inside);

    std::vector<TurnEvent> events = advanceTurn(encounter, fixed(1));  // worm -> Aria: nothing
    CHECK(std::none_of(events.begin(), events.end(),
                       [](const TurnEvent& e) { return e.kind == TurnEvent::Kind::OngoingDamage; }));
    events = advanceTurn(encounter, fixed(1));  // Aria -> worm: once, not once per condition
    const auto count = std::count_if(events.begin(), events.end(),
                                     [](const TurnEvent& e) { return e.kind == TurnEvent::Kind::OngoingDamage; });
    CHECK_EQ(count, 1);
    const auto found = std::find_if(events.begin(), events.end(),
                                    [](const TurnEvent& e) { return e.kind == TurnEvent::Kind::OngoingDamage; });
    CHECK_EQ(found->combatantId, std::string("c"));
    CHECK_EQ(found->damage.front().dice, std::string("5d6"));
}

TEST_CASE("a drain lowers the Hit Point maximum, kills at 0, and can heal the monster")
{
    Combatant aria = hero();
    aria.tempHp = 0;
    Combatant spawn = orc();
    spawn.hp = 5;
    HpDrain bite{"necrotic", true};
    const std::vector<TypedDamage> damage{{3, "piercing"}, {4, "necrotic"}};
    const DamageResult result = applyDamage(aria, damage);
    const DrainResult drained = applyDrain(aria, spawn, bite, damage, result);
    CHECK_EQ(drained.reduced, 4);
    CHECK_EQ(*aria.maxHp, 8);
    CHECK_EQ(aria.maxHpReduction, 4);
    CHECK_EQ(aria.hp, 3);
    CHECK_EQ(spawn.hp, 9);

    HpDrain all{"", false};
    Combatant frail = hero();
    frail.tempHp = 0;
    frail.maxHp = 5;
    frail.hp = 5;
    const std::vector<TypedDamage> big{{5, "necrotic"}};
    const DamageResult hit = applyDamage(frail, big);
    CHECK(applyDrain(frail, spawn, all, big, hit).died);
    CHECK(frail.dead);
}

TEST_CASE("a Charmed creature can't target its charmer, only other creatures")
{
    Combatant aria = hero();
    Combatant vampire;
    vampire.id = "vampire";
    vampire.name = "Vampire";
    Combatant spawn;
    spawn.id = "spawn";
    spawn.name = "Vampire Spawn";
    CHECK(charmedProblem(aria, vampire).empty());
    ActiveCondition charmed;
    charmed.id = "charmed";
    charmed.byId = "vampire";
    aria.conditions.push_back(charmed);
    CHECK(charmedProblem(aria, vampire).find("Charmed by Vampire") != std::string::npos);
    CHECK(charmedProblem(aria, spawn).empty());
    // Charmed added by hand, with no known charmer, blocks nothing.
    Combatant bryn = hero();
    addCondition(bryn, "charmed");
    CHECK(charmedProblem(bryn, vampire).empty());
}

TEST_CASE("a grapple keeps its escape DC, and the better of Athletics and Acrobatics is used to escape")
{
    ActiveCondition old;
    old.id = "grappled";
    old.source = "Vampire's Grave Strike, escape DC 14";
    CHECK(grappleEscapeDc(old) == std::optional<int>{14});  // saved before the DC had its own field
    ActiveCondition plainGrapple;
    plainGrapple.id = "grappled";
    CHECK(!grappleEscapeDc(plainGrapple).has_value());

    Character sheet;
    sheet.abilities.strength = 16;   // +3
    sheet.abilities.dexterity = 12;  // +1
    sheet.skills.acrobatics = true;  // +1 + 2
    Combatant aria = hero();
    EscapeCheck check = escapeCheck(aria, &sheet);
    CHECK_EQ(check.skill, std::string("Athletics"));
    CHECK_EQ(check.bonus, 3);
    sheet.abilities.dexterity = 18;  // +4 + 2
    check = escapeCheck(aria, &sheet);
    CHECK_EQ(check.skill, std::string("Acrobatics"));
    CHECK_EQ(check.bonus, 6);

    ActiveCondition grappled;
    grappled.id = "grappled";
    grappled.byId = "vampire";
    aria.conditions.push_back(grappled);
    CHECK(!escapeGrapple(aria, "spawn"));
    CHECK(escapeGrapple(aria, "vampire"));
    CHECK(!hasCondition(aria, "grappled"));
}

TEST_CASE("the dead lose all their conditions")
{
    Combatant aria = hero();
    addCondition(aria, "poisoned");
    addCondition(aria, "prone");
    killOutright(aria);
    CHECK(aria.conditions.empty());

    Monster goblin;
    goblin.id = "goblin";
    goblin.name = "Goblin";
    goblin.hp = 7;
    Combatant monster = makeMonsterCombatant(goblin, "goblin");
    addCondition(monster, "frightened");
    applyDamage(monster, 20, "slashing");
    CHECK(monster.conditions.empty());

    Combatant other = makeMonsterCombatant(goblin, "goblin-2");
    addCondition(other, "restrained");
    setHitPoints(other, 0);
    CHECK(other.conditions.empty());
}

TEST_CASE("Consume Life needs a living creature at 0 Hit Points")
{
    MonsterAttack consume;
    consume.name = "Consume Life";
    consume.targetAtZeroHp = true;
    consume.targetExceptTypes = {"undead", "construct"};
    Combatant aria = hero();
    CHECK(!targetRequirementProblem(consume, aria).empty());  // still standing
    aria.hp = 0;
    aria.stable = true;
    addCondition(aria, "paralyzed");
    CHECK(targetRequirementProblem(consume, aria).empty());
    Monster zombie;
    zombie.id = "zombie";
    zombie.name = "Zombie";
    zombie.creatureType = "Undead";
    Combatant shambler = makeMonsterCombatant(zombie, "zombie");
    shambler.hp = 0;
    CHECK(targetRequirementProblem(consume, shambler).find("doesn't affect Undead") != std::string::npos);
}

TEST_CASE("Grappled by this monster means this one, and a list of conditions means any of them")
{
    MonsterAttack consume;
    consume.name = "Consume Memories";
    consume.targetCondition = "charmed,grappled";
    Combatant aria = hero();
    CHECK(!targetRequirementProblem(consume, aria, {}, "aboleth").empty());
    addCondition(aria, "charmed");
    CHECK(targetRequirementProblem(consume, aria, {}, "aboleth").empty());

    MonsterAttack pummel;
    pummel.name = "Pummel";
    pummel.targetCondition = "grappled";
    ActiveCondition grappled;
    grappled.id = "grappled";
    grappled.byId = "other";
    addCondition(aria, grappled);
    CHECK(!targetRequirementProblem(pummel, aria, {}, "glabrezu").empty());
    CHECK(targetRequirementProblem(pummel, aria, {}, "other").empty());
}

TEST_CASE("the stat-block reader finds rolls, conditions, and how they end in a custom entry's text")
{
    const EntryReading breath = readEntry(
        "Sleep Breath (Recharge 5\xE2\x80\x93" "6)",
        "Constitution Saving Throw: DC 18, each creature in a 60-foot Cone. Failure: The target has the Incapacitated "
        "condition until the end of its next turn, at which point it repeats the save. Second Failure: The target has "
        "the Unconscious condition for 10 minutes. This effect ends for the target if it takes damage.");
    CHECK_EQ(breath.recharge.value_or(0), 5);
    CHECK(breath.area);
    CHECK_EQ(breath.save->dc, 18);
    CHECK_EQ(breath.riders.size(), std::size_t{1});
    CHECK(breath.riders[0].saveEnds);
    CHECK_EQ(breath.riders[0].worsensTo, std::vector<std::string>{"unconscious"});
    CHECK_EQ(breath.riders[0].worseEndsOn, std::vector<std::string>{kEndsOnTakesDamage});

    const EntryReading bite = readEntry(
        "Bite", "Melee Attack Roll: +14, reach 10 ft. Hit: 18 (3d6 + 8) Piercing damage. If the target is a creature, "
                "it must make the following saving throw. Constitution Saving Throw: DC 21. Failure: The target has "
                "the Poisoned condition. While Poisoned, the target can't regain Hit Points and takes 21 (6d6) Poison "
                "damage at the start of each of its turns, and it repeats the save at the end of each of its turns, "
                "ending the effect on itself on a success.");
    CHECK_EQ(bite.attackBonus.value_or(0), 14);
    CHECK_EQ(bite.riderSave->dc, 21);
    CHECK_EQ(bite.riders.at(0).on, std::string(kRiderOnFailure));
    CHECK(bite.riders.at(0).saveEnds);
    CHECK_EQ(bite.riders.at(0).ongoing.at(0).dice, std::string("6d6"));
    CHECK_EQ(bite.riders.at(0).ongoingAt, std::string(kOngoingAtTarget));

    const EntryReading sting = readEntry(
        "Sting", "Constitution Saving Throw: DC 12, one creature within 5 feet. Failure: 5 (2d4) Poison damage, and "
                 "the target has the Poisoned condition for 1 hour. Failure by 5 or More: While Poisoned, the target "
                 "also has the Unconscious condition, which ends early if the target takes damage.");
    CHECK_EQ(sting.damage.at(0).type, std::string("poison"));
    MonsterAttack stingAttack;
    stingAttack.riders = sting.riders;
    CHECK_EQ(ridersFor(stingAttack, kRiderOnFailureBy5).size(), std::size_t{2});

    const EntryReading glare = readEntry(
        "Death Glare", "Wisdom Saving Throw: DC 11, one Frightened creature the hag can see within 30 feet. Failure: "
                       "If the target has 20 Hit Points or fewer, it drops to 0 Hit Points. Otherwise, the target "
                       "takes 13 (3d8) Psychic damage.");
    CHECK_EQ(glare.targetCondition, std::string("frightened"));
    CHECK_EQ(glare.failureHpThreshold.value_or(0), 20);

    const EntryReading drain = readEntry(
        "Life Drain", "Constitution Saving Throw: DC 13, one creature within 5 feet. Failure: 6 (1d8 + 2) Necrotic "
                      "damage, and the target's Hit Point maximum decreases by an amount equal to the damage taken.");
    CHECK(drain.drain.has_value());
    CHECK(drain.drain->type.empty());

    // Effects follow the text only until they are edited.
    MonsterAttack attack;
    attack.name = "Bite";
    applyReading(attack, bite);
    CHECK(effectsMatch(attack, bite));
    attack.riders.pop_back();
    CHECK(!effectsMatch(attack, bite));
}

TEST_CASE("Cloud of Insects gives Disadvantage on concentration saves until the end of the target's next turn")
{
    const EntryReading cloud = readEntry(
        "Cloud of Insects",
        "Dexterity Saving Throw: DC 17, one creature the dragon can see within 120 feet. Failure: 22 (4d10) Poison "
        "damage, and the target has Disadvantage on saving throws to maintain Concentration until the end of its next "
        "turn. Failure or Success: The dragon can't take this action again until the start of its next turn.");
    CHECK_EQ(cloud.save->dc, 17);
    CHECK_EQ(cloud.damage.at(0).dice, std::string("4d10"));
    CHECK_EQ(cloud.riders.size(), std::size_t{1});
    CHECK(cloud.riders[0].concentrationDisadvantage);
    CHECK(cloud.riders[0].conditions.empty());
    CHECK_EQ(cloud.riders[0].on, std::string(kRiderOnFailure));
    CHECK_EQ(cloud.riders[0].until, std::string(kUntilTargetEnd));
    CHECK(describeRider(cloud.riders[0]).find("Disadvantage on saving throws to maintain Concentration") !=
          std::string::npos);

    Encounter encounter;
    encounter.started = true;
    Combatant dragon = orc();
    dragon.id = "dragon";
    dragon.name = "Adult Black Dragon";
    Combatant aria = hero();
    aria.id = "aria";
    aria.concentration = "bless";
    encounter.combatants = {dragon, aria};
    encounter.turnIndex = 0;
    MonsterAttack attack;
    attack.name = "Cloud of Insects";
    attack.riders = cloud.riders;
    const RiderOutcome outcome =
        applyRider(encounter, encounter.combatants[0], encounter.combatants[1], attack, cloud.riders[0]);
    const Combatant& affected = encounter.combatants[1];
    CHECK(outcome.concentrationDisadvantage);
    CHECK(hasConcentrationDisadvantage(affected));
    CHECK_EQ(affected.timedEffects.at(0).first, std::string("concentrationDisadvantage:Cloud of Insects"));
    CHECK_EQ(affected.timedEffects.at(0).second.anchorId, std::string("aria"));
    CHECK(affected.timedEffects.at(0).second.boundary == TurnBoundary::End);
    CHECK_EQ(affected.timedEffects.at(0).second.turnsRemaining, 1);
    CHECK(!affected.timedEffects.at(0).second.skipNext);

    advanceTurn(encounter);  // the dragon's turn ends; Aria's is next
    CHECK(hasConcentrationDisadvantage(encounter.combatants[1]));
    advanceTurn(encounter);  // the end of Aria's turn
    CHECK(!hasConcentrationDisadvantage(encounter.combatants[1]));
}

TEST_CASE("an SRD stat block stored by an older version gets the rules it lacks, and nothing else changes")
{
    Monster fresh;
    fresh.id = "wolf";
    fresh.name = "Wolf";
    fresh.source = kSrdMonsterSource;
    MonsterAttack bite;
    bite.name = "Bite";
    bite.attackBonus = 4;
    ConditionRider prone;
    prone.conditions = {"prone"};
    prone.targetMaxSize = "Medium";
    bite.riders = {prone};
    fresh.attacks = {bite};
    MonsterFeature howl;
    howl.name = "Howl";
    MonsterAttack aimed;
    aimed.name = "Howl";
    aimed.save = SaveSpec{Ability::Wisdom, 11, false};
    howl.targeted = aimed;
    fresh.bonusActions = {howl};
    MonsterFeature cloud;
    cloud.name = "Cloud of Insects";
    MonsterAttack aimedCloud;
    aimedCloud.name = "Cloud of Insects";
    aimedCloud.save = SaveSpec{Ability::Dexterity, 17, false};
    ConditionRider insects;
    insects.on = kRiderOnFailure;
    insects.until = kUntilTargetEnd;
    insects.concentrationDisadvantage = true;
    aimedCloud.riders = {insects};
    cloud.targeted = aimedCloud;
    fresh.legendaryActions = {cloud};

    Monster old = fresh;
    old.attacks[0].riders.clear();
    old.attacks[0].attackBonus = 5;  // stored copy differs: kept
    old.bonusActions[0].targeted.reset();
    old.legendaryActions[0].targeted->riders.clear();
    Combatant row = makeMonsterCombatant(old, "w");
    CHECK(fillMonsterSnapshot(row, fresh));
    CHECK_EQ(row.statBlock->attacks[0].riders.size(), std::size_t{1});
    CHECK_EQ(row.statBlock->attacks[0].attackBonus.value_or(0), 5);
    CHECK(row.statBlock->bonusActions[0].targeted.has_value());
    CHECK(row.statBlock->legendaryActions[0].targeted->riders.at(0).concentrationDisadvantage);
    CHECK(!fillMonsterSnapshot(row, fresh));

    // A custom monster's stored copy is never touched.
    Monster custom = old;
    custom.source = kCustomMonsterSource;
    Combatant mine = makeMonsterCombatant(custom, "c");
    Monster customFresh = fresh;
    customFresh.source = kCustomMonsterSource;
    CHECK(!fillMonsterSnapshot(mine, customFresh));
}

TEST_CASE("\"or\" and extra damage apply when their condition holds, and the app sees Bloodied and grapples")
{
    Combatant swarm = orc();
    swarm.id = "swarm";
    Combatant aria = hero();
    aria.tempHp = 0;
    const std::vector<DamagePart> bites{DamagePart{"2d4", "piercing", DamageWhen::Always},
                                        DamagePart{"1d4", "piercing", DamageWhen::Alternative, kDamageIfSelfBloodied}};
    DamageOptions options;
    options.conditionsMet = damageConditionsMet(swarm, &aria);
    CHECK(options.conditionsMet.empty());
    CHECK_EQ(totalDamage(rollDamageParts(bites, options, fixed(4))), 8);
    swarm.hp = 7;  // Bloodied
    options.conditionsMet = damageConditionsMet(swarm, &aria);
    const std::vector<std::string> met = options.conditionsMet;
    CHECK_EQ(met, std::vector<std::string>{kDamageIfSelfBloodied});
    CHECK_EQ(totalDamage(rollDamageParts(bites, options, fixed(4))), 4);

    ActiveCondition held;
    held.id = "grappled";
    held.byId = "swarm";
    addCondition(aria, held);
    options.conditionsMet = damageConditionsMet(swarm, &aria);
    CHECK(std::find(options.conditionsMet.begin(), options.conditionsMet.end(), kDamageIfGrappledBySelf) !=
          options.conditionsMet.end());

    // The GM's words round-trip through the editor's damage line.
    const std::string charge = "the boar moved 20+ feet straight toward it immediately before the hit";
    const std::vector<DamagePart> gore{DamagePart{"1d6+1", "piercing", DamageWhen::Always},
                                       DamagePart{"1d6", "piercing", DamageWhen::Conditional, charge}};
    const std::string line = formatDamageParts(gore);
    const std::vector<DamagePart> parsed = parseDamageParts(line).value();
    CHECK_EQ(parsed, gore);
    DamageOptions none;
    CHECK_EQ(rollDamageParts(gore, none, fixed(3)).size(), std::size_t{1});
    DamageOptions charged;
    charged.conditionsMet = {charge};
    CHECK_EQ(rollDamageParts(gore, charged, fixed(3)).size(), std::size_t{2});
    const std::vector<DamagePart> grip = parseDamageParts("2d8+3 piercing alternative if the target is Grappled by it").value();
    CHECK_EQ(grip[0].condition, std::string(kDamageIfGrappledBySelf));

    const EntryReading mimic = readEntry(
        "Bite", "Melee Attack Roll: +5, reach 5 ft. Hit: 7 (1d8 + 3) Piercing damage\xE2\x80\x94or 12 (2d8 + 3) "
                "Piercing damage if the target is Grappled by the mimic\xE2\x80\x94plus 4 (1d8) Acid damage.");
    const std::string mimicCondition = mimic.damage.at(1).condition;
    CHECK_EQ(mimicCondition, std::string(kDamageIfGrappledBySelf));
}

TEST_CASE("automatic attack rolls say why they have Advantage or Disadvantage")
{
    Combatant goblin = orc();
    Combatant aria = hero();
    MonsterAttack bite;
    bite.name = "Bite";
    bite.effect = "Melee Attack Roll: +4, reach 5 ft.";
    CHECK(attackModeReasons(goblin, aria, bite).empty());
    addCondition(aria, "prone");
    const std::vector<std::string> reasons = attackModeReasons(goblin, aria, bite);
    CHECK_EQ(reasons, std::vector<std::string>{"Aria is Prone"});
}

TEST_CASE("Automatic attack rolls use traits: Bloodied Fury, Blood Frenzy, Pack Tactics, auras, Aversion to Fire")
{
    Encounter encounter;
    Monster boar;
    boar.id = "boar";
    boar.name = "Boar";
    boar.hp = 12;
    MonsterFeature fury;
    fury.name = "Bloodied Fury";
    fury.attackModifier = AttackModifier{true, kModifierWhileBloodied};
    MonsterFeature pack;
    pack.name = "Pack Tactics";
    AttackModifier packRule;
    packRule.ask = "an ally is within 5 feet of the target";
    pack.attackModifier = packRule;
    boar.traits = {fury, pack};
    Monster captain;
    captain.id = "captain";
    captain.name = "Hobgoblin Captain";
    captain.hp = 50;
    MonsterFeature aura;
    aura.name = "Aura of Authority";
    AttackModifier auraRule;
    auraRule.ask = "the attacker is within 10 feet of the hobgoblin";
    auraRule.alliesToo = true;
    auraRule.whileActive = true;
    aura.attackModifier = auraRule;
    captain.traits = {aura};
    Monster golem;
    golem.id = "golem";
    golem.name = "Flesh Golem";
    golem.hp = 100;
    MonsterFeature fire;
    fire.name = "Aversion to Fire";
    AttackModifier fireRule;
    fireRule.advantage = false;
    fireRule.when = kModifierAfterDamage;
    fireRule.damageType = "fire";
    fire.attackModifier = fireRule;
    golem.traits = {fire};
    encounter.combatants.push_back(makeMonsterCombatant(boar, "b"));
    encounter.combatants.push_back(makeMonsterCombatant(captain, "c"));
    encounter.combatants.push_back(makeMonsterCombatant(golem, "g"));
    encounter.combatants.push_back(hero());
    encounter.started = true;
    MonsterAttack gore;
    gore.name = "Gore";
    gore.effect = "Melee Attack Roll: +3, reach 5 ft.";
    gore.attackBonus = 3;
    const Combatant& aria = encounter.combatants[3];

    CHECK(decideAttackMode(encounter, encounter.combatants[0], aria, gore).mode == RollMode::Normal);
    encounter.combatants[0].hp = 6;  // Bloodied
    AttackModeChoice choice = decideAttackMode(encounter, encounter.combatants[0], aria, gore);
    CHECK(choice.mode == RollMode::Advantage);
    CHECK_EQ(choice.advantages.at(0), std::string("Bloodied Fury: Boar is Bloodied"));
    encounter.combatants[0].hp = 12;

    // Questions: its own Pack Tactics and the captain's aura.
    const std::vector<RollQuestion> questions = attackRollQuestions(encounter, encounter.combatants[0]);
    CHECK_EQ(questions.size(), std::size_t{2});
    choice = decideAttackMode(encounter, encounter.combatants[0], aria, gore, {"c/Aura of Authority"});
    CHECK(choice.mode == RollMode::Advantage);
    addCondition(encounter.combatants[1], "stunned");  // the aura stops
    CHECK_EQ(attackRollQuestions(encounter, encounter.combatants[0]).size(), std::size_t{1});
    CHECK(decideAttackMode(encounter, encounter.combatants[0], aria, gore, {"c/Aura of Authority"}).mode ==
          RollMode::Normal);
    choice = decideAttackMode(encounter, encounter.combatants[0], aria, gore, {"b/Pack Tactics"});
    CHECK(choice.mode == RollMode::Advantage);

    // Aversion to Fire: Disadvantage until the end of the golem's next turn.
    Combatant& golemRow = encounter.combatants[2];
    const DamageResult burnt = applyDamage(golemRow, {TypedDamage{5, "fire"}});
    CHECK_EQ(noteDamageTaken(encounter, golemRow, burnt), std::vector<std::string>{"Aversion to Fire"});
    CHECK(decideAttackMode(encounter, golemRow, aria, gore).mode == RollMode::Disadvantage);
    encounter.turnIndex = 2;  // the golem's turn
    advanceTurn(encounter);   // its turn ends: the effect ends
    CHECK(golemRow.timedEffects.empty());
    CHECK(decideAttackMode(encounter, golemRow, aria, gore).mode == RollMode::Normal);

    // Advantage and Disadvantage cancel.
    encounter.combatants[0].hp = 6;
    addCondition(encounter.combatants[0], "poisoned");
    choice = decideAttackMode(encounter, encounter.combatants[0], aria, gore);
    CHECK(choice.mode == RollMode::Normal);
    CHECK(!choice.advantages.empty());
    CHECK(!choice.disadvantages.empty());

    const std::optional<AttackModifier> read = readAttackModifier(
        "Sunlight Sensitivity", "While in sunlight, the kobold has Disadvantage on ability checks and attack rolls.");
    CHECK(read.has_value());
    CHECK(!read->advantage);
    CHECK(read->sticky);
}

TEST_CASE("help for a picked creature: Temporary Hit Points, Advantage, and AC until the monster's turn")
{
    Encounter encounter;
    Monster giant;
    giant.id = "giant";
    giant.name = "Frost Giant";
    encounter.combatants.push_back(makeMonsterCombatant(giant, "g"));
    encounter.combatants.push_back(hero());
    encounter.combatants[1].tempHp = 0;
    encounter.combatants[1].ac = 15;
    encounter.started = true;
    encounter.turnIndex = 0;
    MonsterAttack cry;
    cry.name = "War Cry (Recharge 5–6)";
    cry.benefit = Benefit{"2d10+5", "", true, 0, kUntilSourceStart};
    Combatant& aria = encounter.combatants[1];
    BenefitResult result = applyBenefit(encounter, encounter.combatants[0], aria, cry, fixed(5));
    CHECK_EQ(result.tempHp, 15);
    CHECK_EQ(aria.tempHp, 15);
    CHECK(result.advantage);
    MonsterAttack claw;
    claw.name = "Claw";
    claw.effect = "Melee Attack Roll: +4";
    const Combatant orcRow = orc();
    AttackModeChoice choice = decideAttackMode(encounter, aria, orcRow, claw);
    CHECK(choice.mode == RollMode::Advantage);
    CHECK_EQ(choice.advantages.at(0), std::string("War Cry"));

    // Lower Temporary Hit Points do not replace higher ones.
    result = applyBenefit(encounter, encounter.combatants[0], aria, cry, fixed(1));
    CHECK(result.tempHpKept);
    CHECK_EQ(aria.tempHp, 15);

    MonsterAttack shield;
    shield.name = "Shimmering Shield";
    shield.benefit = Benefit{"3d6", "", false, 2, kUntilSourceEnd};
    applyBenefit(encounter, encounter.combatants[0], aria, shield, fixed(3));
    CHECK_EQ(aria.ac, 17);
    applyBenefit(encounter, encounter.combatants[0], aria, shield, fixed(3));  // not twice
    CHECK_EQ(aria.ac, 17);

    advanceTurn(encounter);  // giant's turn ends (skipped: it was its own turn), Aria's starts
    advanceTurn(encounter);  // Aria's ends, the giant's starts: War Cry ends
    CHECK(decideAttackMode(encounter, aria, orcRow, claw).mode == RollMode::Normal);
    CHECK_EQ(aria.ac, 17);
    advanceTurn(encounter);  // the giant's turn ends: the shield ends
    CHECK_EQ(aria.ac, 15);
    CHECK(aria.timedEffects.empty());

    const EntryReading read = readEntry(
        "War Cry (Recharge 5\xE2\x80\x93" "6)",
        "The giant or one creature of its choice that can see or hear it gains 16 (2d10 + 5) Temporary Hit Points "
        "and has Advantage on attack rolls until the start of the giant\xE2\x80\x99s next turn.");
    CHECK(read.benefit.has_value());
    CHECK_EQ(read.benefit->tempHp, std::string("2d10+5"));
    CHECK(read.benefit->advantageOnAttacks);
    CHECK(targetedFromText("War Cry", "The giant or one creature of its choice gains 16 (2d10 + 5) Temporary Hit Points.")
              .has_value());
}

TEST_CASE("Rampage waits for damage to a Bloodied creature, then gives one more Bite before the turn ends")
{
    Monster hyena;
    hyena.id = "giant-hyena";
    hyena.name = "Giant Hyena";
    MonsterAttack bite;
    bite.name = "Bite";
    bite.effect = "Melee Attack Roll: +5, reach 5 ft.";
    bite.attackBonus = 5;
    hyena.attacks = {bite};
    MonsterFeature rampage;
    rampage.name = "Rampage (1/Day)";
    rampage.effect = "Immediately after dealing damage to a creature that was already Bloodied, the hyena can move up "
                     "to half its Speed, and it makes one Bite attack.";
    rampage.perDay = 1;
    rampage.afterDamagingBloodied = true;
    hyena.bonusActions = {rampage};
    Combatant row = makeMonsterCombatant(hyena, "h");
    CHECK(!featureAvailability(row, FeatureKind::BonusAction, rampage, true).available);
    CHECK(useAction(row, bite, true));  // the first Bite, its action
    CHECK(monsterActionSpent(row));
    CHECK_EQ(noteDamagedBloodied(row), std::vector<std::string>{"Rampage (1/Day)"});
    CHECK(featureAvailability(row, FeatureKind::BonusAction, rampage, true).available);
    CHECK(useFeature(row, FeatureKind::BonusAction, rampage, true));
    CHECK_EQ(row.economy.grantedAttack, std::string("Bite"));
    CHECK(!monsterActionSpent(row));  // the turn waits for the extra Bite
    CHECK(actionAvailability(row, bite, true).available);
    CHECK(useAction(row, bite, true));
    CHECK(monsterActionSpent(row));
    CHECK(!featureAvailability(row, FeatureKind::BonusAction, rampage, true).available);  // used today
    CHECK(readsAfterDamagingBloodied(rampage.effect));
}

TEST_CASE("a feature that takes standard actions offers each one, and Disengage lasts to the end of the turn")
{
    using Choices = std::vector<std::vector<std::string>>;
    CHECK(standardActionChoices("The goblin takes the Disengage or Hide action.") == Choices({{"Disengage"}, {"Hide"}}));
    CHECK(standardActionChoices("The spy takes the Dash, Disengage, or Hide action.") ==
          Choices({{"Dash"}, {"Disengage"}, {"Hide"}}));
    CHECK(standardActionChoices("The golem takes the Dash and Disengage actions.") ==
          Choices({{"Dash", "Disengage"}}));
    CHECK(standardActionChoices("While in Dim Light or Darkness, the shadow takes the Hide action.") ==
          Choices({{"Hide"}}));
    CHECK(standardActionChoices("The giant makes two attacks.").empty());
    CHECK(!standardActionRules("Disengage").empty());

    Monster goblin;
    goblin.id = "goblin";
    goblin.name = "Goblin";
    goblin.hp = 7;
    goblin.abilities.dexterity = 14;
    Encounter fight;
    fight.combatants = {makeMonsterCombatant(goblin, "g"), makeMonsterCombatant(goblin, "h")};
    fight.turnIndex = 0;
    CHECK(takeStandardAction(fight.combatants[0], "Disengage"));
    CHECK(!takeStandardAction(fight.combatants[0], "Hide"));
    CHECK_EQ(fight.combatants[0].timedEffects.size(), std::size_t{1});
    fight.combatants[0].initiative = 15;
    fight.combatants[1].initiative = 10;
    advanceTurn(fight);
    CHECK(fight.combatants[0].timedEffects.empty());  // gone at the end of its turn

    CHECK_EQ(hideCheckBonus(fight.combatants[0]), 2);  // Dexterity, with no Stealth listed
    fight.combatants[0].statBlock->skills["stealth"] = 6;
    CHECK_EQ(hideCheckBonus(fight.combatants[0]), 6);
}

TEST_CASE("Multiattack allows each attack only as often as it names it")
{
    Monster spawn;
    spawn.id = "vampire-spawn";
    spawn.name = "Vampire Spawn";
    spawn.hp = 90;
    MonsterAttack multi;
    multi.name = "Multiattack";
    multi.count = 3;
    MonsterAttack claw;
    claw.name = "Claw";
    claw.count = 2;
    claw.inMultiattack = true;
    claw.attackBonus = 6;
    MonsterAttack bite;
    bite.name = "Bite";
    bite.count = 1;
    bite.inMultiattack = true;
    bite.attackBonus = 6;
    spawn.attacks = {multi, claw, bite};
    Combatant vampire = makeMonsterCombatant(spawn, "v");

    CHECK(useAction(vampire, multi, true));
    CHECK(useAction(vampire, bite, true));
    CHECK(!actionAvailability(vampire, bite, true).available);  // one Bite
    CHECK(useAction(vampire, claw, true));
    CHECK(actionAvailability(vampire, claw, true).available);
    CHECK(useAction(vampire, claw, true));
    CHECK(!actionAvailability(vampire, claw, true).available);  // the Multiattack is spent
    CHECK_EQ(vampire.economy.attacksRemaining, 0);

    // "Two attacks, using Scimitar or Shortbow in any combination": both
    // count 2, so two of either.
    Monster boss = spawn;
    boss.attacks[0].count = 2;
    boss.attacks[1].name = "Scimitar";
    boss.attacks[2].name = "Shortbow";
    boss.attacks[2].count = 2;
    Combatant goblin = makeMonsterCombatant(boss, "g");
    CHECK(useAction(goblin, boss.attacks[0], true));
    CHECK(useAction(goblin, boss.attacks[2], true));
    CHECK(useAction(goblin, boss.attacks[2], true));
    CHECK_EQ(goblin.economy.attacksRemaining, 0);

    // A Multiattack whose counts fall short (a Bite per head) is limited by
    // its total only.
    Monster hydra = spawn;
    hydra.attacks = {multi, bite};
    hydra.attacks[0].count = 5;
    Combatant heads = makeMonsterCombatant(hydra, "h");
    CHECK(useAction(heads, hydra.attacks[0], true));
    for (int i = 0; i < 5; ++i) {
        CHECK(useAction(heads, bite, true));
    }
    CHECK_EQ(heads.economy.attacksRemaining, 0);
}

TEST_CASE("an aura that needs sight skips a Blinded or Unconscious creature")
{
    Monster hag;
    hag.id = "sea-hag";
    hag.name = "Sea Hag";
    MonsterFeature vile;
    vile.name = "Vile Appearance";
    AuraSave aura;
    aura.ability = Ability::Wisdom;
    aura.dc = 11;
    aura.who = "a Beast or Humanoid that can see the hag's true form";
    aura.condition = "frightened";
    vile.aura = aura;
    hag.traits.push_back(vile);
    Encounter encounter;
    encounter.combatants.push_back(makeMonsterCombatant(hag, "hag"));
    Combatant hero;
    hero.id = "hero";
    hero.source = kCombatantSourceCharacter;
    hero.name = "Hero";
    hero.hp = 0;
    hero.maxHp = 10;
    encounter.combatants.push_back(hero);
    CHECK_EQ(aurasAtTurnStart(encounter, "hero").size(), std::size_t{1});
    ActiveCondition out;
    out.id = "unconscious";
    encounter.combatants[1].conditions = {out};
    CHECK(aurasAtTurnStart(encounter, "hero").empty());
    out.id = "blinded";
    encounter.combatants[1].conditions = {out};
    CHECK(aurasAtTurnStart(encounter, "hero").empty());
}

TEST_CASE("Charm Person: Humanoids only, and the vampire's Bite does not end the Charmed condition")
{
    Monster vampire;
    vampire.id = "vampire";
    vampire.name = "Vampire";
    MonsterAttack charm;
    charm.name = "Charm (Recharge 5-6)";
    charm.targetTypes = {"humanoid"};
    Monster wolf;
    wolf.id = "wolf";
    wolf.name = "Wolf";
    wolf.creatureType = "Beast";
    Combatant beast = makeMonsterCombatant(wolf, "w");
    CHECK(!targetRequirementProblem(charm, beast, {}, "v").empty());
    Combatant hero;
    hero.id = "hero";
    hero.source = kCombatantSourceCharacter;
    hero.name = "Hero";
    hero.hp = 10;
    hero.maxHp = 10;
    CHECK(targetRequirementProblem(charm, hero, {}, "v").empty());

    ActiveCondition charmed;
    charmed.id = "charmed";
    charmed.byId = "v";
    charmed.endsOn = {kEndsOnTakesDamage, "spares:Bite"};
    hero.conditions = {charmed};
    CHECK(endConditionsOnDamage(hero, "v", "Bite (Bat or Vampire Form Only)").empty());
    CHECK(hasCondition(hero, "charmed"));
    CHECK_EQ(endConditionsOnDamage(hero, "v", "Grave Strike").size(), std::size_t{1});
    CHECK(!hasCondition(hero, "charmed"));
}

TEST_CASE("SRD spells that deal damage or grant a condition become attacks")
{
    CHECK(spellCombatRuleCount() > 100);
    CHECK(!spellAsAttack("Detect Magic", 1, 15, std::nullopt).has_value());

    const auto fireball = spellAsAttack("Fireball", 3, 15, std::nullopt);
    CHECK(fireball.has_value());
    if (fireball.has_value()) {
        CHECK(fireball->save.has_value());
        CHECK(fireball->save->ability == Ability::Dexterity);
        CHECK_EQ(fireball->save->dc, 15);
        CHECK(fireball->save->halfOnSuccess);
        CHECK(fireball->area);
        CHECK_EQ(fireball->damage.at(0).dice, std::string("8d6"));
        CHECK_EQ(fireball->damage.at(0).type, std::string("fire"));
        CHECK(fireball->effect.find(" casts ") != std::string::npos);
    }
    const auto upcast = spellAsAttack("Fireball", 5, 15, std::nullopt);
    CHECK(upcast.has_value());
    if (upcast.has_value()) {
        CHECK_EQ(upcast->name, std::string("Fireball (level 5)"));
        CHECK_EQ(upcast->damage.at(0).dice, std::string("10d6"));
    }

    const auto hold = spellAsAttack("Hold Person", 2, 16, std::nullopt);
    CHECK(hold.has_value());
    if (hold.has_value()) {
        CHECK(hold->save->ability == Ability::Wisdom);
        CHECK_EQ(hold->concentration, std::string("hold-person"));
        CHECK_EQ(hold->targetTypes.at(0), std::string("humanoid"));
        CHECK_EQ(hold->strikes, 1);
        CHECK_EQ(hold->riders.at(0).conditions.at(0), std::string("paralyzed"));
        CHECK(hold->riders.at(0).saveEnds);
        CHECK_EQ(hold->riders.at(0).concentration, std::string("hold-person"));
        CHECK_EQ(hold->riders.at(0).on, std::string(kRiderOnFailure));
    }
    const auto holdMore = spellAsAttack("Hold Person", 3, 16, std::nullopt);
    CHECK(holdMore.has_value());
    if (holdMore.has_value()) {
        CHECK_EQ(holdMore->strikes, 2);
    }

    const auto acid = spellAsAttack("Acid Arrow", 3, 17, 9);
    CHECK(acid.has_value());
    if (acid.has_value()) {
        CHECK_EQ(acid->attackBonus.value_or(0), 9);
        CHECK_EQ(acid->damage.at(0).dice, std::string("5d4"));
        CHECK(acid->halfDamageOnMiss);
        CHECK(acid->effect.find("3d4") != std::string::npos);
    }

    const auto missiles = spellAsAttack("Magic Missile", 1, 0, std::nullopt);
    CHECK(missiles.has_value());
    if (missiles.has_value()) {
        CHECK_EQ(missiles->strikes, 3);
        CHECK(missiles->repeatSameTarget);
        CHECK_EQ(missiles->damage.at(0).dice, std::string("1d4+1"));
        CHECK(!missiles->save.has_value());
        CHECK(!missiles->attackBonus.has_value());
    }
    const auto moreMissiles = spellAsAttack("Magic Missile", 2, 0, std::nullopt);
    CHECK(moreMissiles.has_value());
    if (moreMissiles.has_value()) {
        CHECK_EQ(moreMissiles->strikes, 4);
    }

    const auto command = spellAsAttack("Command", 1, 14, std::nullopt);
    CHECK(command.has_value());
    if (command.has_value()) {
        CHECK_EQ(command->riders.at(0).ask, std::string("the command is Grovel"));
        CHECK_EQ(command->riders.at(0).conditions.at(0), std::string("prone"));
    }

    const auto hidden = spellAsAttack("Invisibility", 2, 0, std::nullopt);
    CHECK(hidden.has_value());
    if (hidden.has_value()) {
        CHECK(hidden->allowSelf);
        CHECK_EQ(hidden->concentration, std::string("invisibility"));
        CHECK_EQ(hidden->riders.at(0).on, std::string(kRiderOnCast));
        CHECK_EQ(hidden->riders.at(0).conditions.at(0), std::string("invisible"));
        CHECK(std::find(hidden->riders.at(0).endsOn.begin(), hidden->riders.at(0).endsOn.end(), kEndsOnAttackRoll) !=
              hidden->riders.at(0).endsOn.end());
    }

    const auto kill = spellAsAttack("Power Word Kill", 9, 0, std::nullopt);
    CHECK(kill.has_value());
    if (kill.has_value()) {
        CHECK_EQ(kill->damage.at(0).dice, std::string("12d12"));
        const SpellStrike low = resolveSpellStrike(*kill, 100);
        CHECK(low.kill);
        CHECK(!low.applyDamage);
        const SpellStrike high = resolveSpellStrike(*kill, 101);
        CHECK(!high.kill);
        CHECK(high.applyDamage);
    }
    const auto stun = spellAsAttack("Power Word Stun", 8, 17, std::nullopt);
    CHECK(stun.has_value());
    if (stun.has_value()) {
        CHECK(stun->repeatSave.has_value());
        CHECK(stun->repeatSave->ability == Ability::Constitution);
        CHECK_EQ(stun->repeatSave->dc, 17);
        CHECK(stun->riders.at(0).saveEnds);
        CHECK(resolveSpellStrike(*stun, 150).applyConditions);
        CHECK(!resolveSpellStrike(*stun, 151).applyConditions);
    }
}

TEST_CASE("a spell cast during Multiattack spends the Spellcasting entry")
{
    Monster monster;
    monster.id = "dragon";
    monster.name = "Dragon";
    monster.hp = 100;
    MonsterAttack multi;
    multi.name = "Multiattack";
    multi.count = 3;
    MonsterAttack rend;
    rend.name = "Rend";
    rend.count = 3;
    rend.inMultiattack = true;
    rend.attackBonus = 5;
    MonsterAttack casting;
    casting.name = "Spellcasting";
    casting.count = 1;
    casting.inMultiattack = true;
    monster.attacks = {multi, rend, casting};
    Combatant combatant = makeMonsterCombatant(monster, "dragon");
    CHECK(useAction(combatant, multi, true));
    CHECK_EQ(combatant.economy.multiattackLeft.at("Spellcasting"), 1);
    CHECK_EQ(combatant.economy.multiattackLeft.at("Rend"), 3);
    auto fireball = spellAsAttack("Fireball", 3, 15, std::nullopt);
    CHECK(fireball.has_value());
    if (!fireball.has_value()) {
        return;
    }
    fireball->inMultiattack = true;
    fireball->multiattackAs = "Spellcasting";
    CHECK(useAction(combatant, *fireball, true));
    CHECK_EQ(combatant.economy.attacksRemaining, 2);
    CHECK_EQ(combatant.economy.multiattackLeft.at("Spellcasting"), 0);
    CHECK_EQ(combatant.economy.multiattackLeft.at("Rend"), 3);
    CHECK(!actionAvailability(combatant, *fireball, true).available);
    CHECK(actionAvailability(combatant, rend, true).available);
}

TEST_CASE("a concentration spell ends on whoever received it when the caster stops")
{
    Encounter encounter;
    Combatant caster;
    caster.id = "mage";
    caster.name = "Mage";
    caster.hp = 20;
    caster.concentration = "invisibility";
    ActiveCondition own;
    own.id = "invisible";
    own.concentration = "invisibility";
    caster.conditions.push_back(own);

    Combatant ally;
    ally.id = "ally";
    ally.name = "Ally";
    ally.hp = 20;
    ActiveCondition shared;
    shared.id = "invisible";
    shared.concentration = "invisibility";
    shared.byId = "mage";
    shared.source = "Mage's Invisibility";
    ally.conditions.push_back(shared);

    Combatant other;
    other.id = "other";
    other.name = "Other";
    other.hp = 20;
    other.concentration = "invisibility";
    ActiveCondition separate;
    separate.id = "invisible";
    separate.concentration = "invisibility";
    separate.byId = "other";
    other.conditions.push_back(separate);

    encounter.combatants = {caster, ally, other};
    CHECK(releaseConditions(encounter).empty());
    CHECK(hasCondition(encounter.combatants[0], "invisible"));
    CHECK(hasCondition(encounter.combatants[1], "invisible"));

    // The spell ends early on a target who attacks. The caster is not that
    // target, so attacking leaves the ally Invisible and concentration up.
    endConditionsOn(encounter.combatants[0], {"attackRoll", "dealsDamage"});
    CHECK(releaseConditions(encounter).empty());
    CHECK(hasCondition(encounter.combatants[1], "invisible"));
    CHECK_EQ(encounter.combatants[0].concentration, std::string("invisibility"));

    endConcentration(encounter.combatants[0]);
    const std::vector<ReleasedCondition> ended = releaseConditions(encounter);
    CHECK_EQ(encounter.combatants[0].concentration, std::string());
    CHECK(!hasCondition(encounter.combatants[0], "invisible"));
    CHECK(!hasCondition(encounter.combatants[1], "invisible"));
    CHECK(hasCondition(encounter.combatants[2], "invisible"));
    CHECK_EQ(ended.size(), std::size_t{1});
    CHECK_EQ(ended[0].combatantId, std::string("ally"));

    // Casting it again does not bring the old beneficiary back.
    setConcentration(encounter.combatants[0], "invisibility");
    CHECK(releaseConditions(encounter).empty());
    CHECK(!hasCondition(encounter.combatants[1], "invisible"));
    CHECK_EQ(encounter.combatants[0].concentration, std::string("invisibility"));
}

TEST_CASE("Hold Person on someone else ends, including Incapacitated, when concentration ends")
{
    Encounter encounter;
    Combatant caster;
    caster.id = "mage";
    caster.name = "Mage";
    caster.hp = 20;
    caster.concentration = "hold-person";
    Combatant ally;
    ally.id = "ally";
    ally.name = "Ally";
    ally.hp = 20;
    ActiveCondition held;
    held.id = "paralyzed";
    held.concentration = "hold-person";
    held.byId = "mage";
    CHECK(addCondition(ally, held) == AddConditionResult::Added);
    CHECK(hasCondition(ally, "incapacitated"));
    encounter.combatants = {caster, ally};

    endConcentration(encounter.combatants[0]);
    const std::vector<ReleasedCondition> ended = releaseConditions(encounter);
    CHECK(!hasCondition(encounter.combatants[1], "paralyzed"));
    CHECK(!hasCondition(encounter.combatants[1], "incapacitated"));
    CHECK_EQ(ended.size(), std::size_t{2});
}

TEST_CASE("the caster's own Invisibility ending also ends it on everyone else")
{
    Encounter encounter;
    Combatant caster;
    caster.id = "mage";
    caster.name = "Mage";
    caster.hp = 20;
    caster.concentration = "invisibility";
    ActiveCondition own;
    own.id = "invisible";
    own.concentration = "invisibility";
    own.byId = "mage";
    own.endsOn = {"attackRoll", "dealsDamage", "anySpell"};
    caster.conditions.push_back(own);

    Combatant ally;
    ally.id = "ally";
    ally.name = "Ally";
    ally.hp = 20;
    ActiveCondition shared = own;
    ally.conditions.push_back(shared);
    encounter.combatants = {caster, ally};

    endConditionsOn(encounter.combatants[0], {"attackRoll"});
    CHECK(encounter.combatants[0].concentration.empty());
    CHECK(!hasCondition(encounter.combatants[0], "invisible"));
    const std::vector<ReleasedCondition> ended = releaseConditions(encounter);
    CHECK(!hasCondition(encounter.combatants[1], "invisible"));
    CHECK_EQ(ended.size(), std::size_t{1});
    CHECK_EQ(ended[0].combatantId, std::string("ally"));
}

namespace {

Monster blueDragon()
{
    Monster dragon;
    dragon.id = "adult-blue-dragon";
    dragon.name = "Adult Blue Dragon";
    dragon.hp = 212;
    MonsterAttack multi;
    multi.name = "Multiattack";
    multi.count = 3;
    multi.effect = "The dragon makes three Rend attacks. It can replace one attack with a use of Spellcasting to cast Shatter.";
    MonsterAttack rend;
    rend.name = "Rend";
    rend.attackBonus = 12;
    rend.inMultiattack = true;
    MonsterAttack casting;
    casting.name = "Spellcasting";
    casting.inMultiattack = true;
    casting.effect = "The dragon casts one of the following spells, requiring no Material components and using "
                     "Charisma as the spellcasting ability (spell save DC 18): At Will: Detect Magic, Invisibility, "
                     "Mage Hand, Shatter";
    dragon.attacks = {multi, rend, casting};
    return dragon;
}

}  // namespace

TEST_CASE("only the spell Multiattack names counts as part of it: Shatter, not Invisibility")
{
    const Monster dragon = blueDragon();
    const std::vector<MonsterAttack> spells = actionableSpells(dragon, dragon.attacks[2]);
    const MonsterAttack* shatter = nullptr;
    const MonsterAttack* invisibility = nullptr;
    for (const MonsterAttack& spell : spells) {
        shatter = spell.name == "Shatter" ? &spell : shatter;
        invisibility = spell.name == "Invisibility" ? &spell : invisibility;
    }
    CHECK(shatter != nullptr);
    CHECK(invisibility != nullptr);
    if (shatter == nullptr || invisibility == nullptr) {
        return;
    }
    CHECK(shatter->inMultiattack);
    CHECK(!invisibility->inMultiattack);

    // Mid-Multiattack, Invisibility is refused; Shatter replaces one Rend.
    Combatant combatant = makeMonsterCombatant(dragon, "dragon-row");
    CHECK(useAction(combatant, dragon.attacks[1], true));
    CHECK(!actionAvailability(combatant, *invisibility, true).available);
    CHECK(actionAvailability(combatant, *shatter, true).available);
    CHECK(useAction(combatant, *shatter, true));
    CHECK_EQ(combatant.economy.attacksRemaining, 1);

    // With the action free, Invisibility is its own action, not a Multiattack.
    Combatant fresh = makeMonsterCombatant(dragon, "fresh");
    CHECK(actionAvailability(fresh, *invisibility, true).available);
    CHECK(useAction(fresh, *invisibility, true));
    CHECK_EQ(fresh.economy.attacksRemaining, 0);
    CHECK(fresh.economy.actionUsed);

    // An oni's Multiattack names no spell: any spell can replace an attack.
    Monster oni = blueDragon();
    oni.attacks[0].effect = "The oni makes two Claw attacks. It can replace one attack with a use of Spellcasting.";
    for (const MonsterAttack& spell : actionableSpells(oni, oni.attacks[2])) {
        CHECK(spell.inMultiattack);
    }
}

TEST_CASE("Acid Arrow and Vitriolic Sphere hurt again at the end of the target's next turn")
{
    const auto arrow = spellAsAttack("Acid Arrow", 3, 15, 7);
    CHECK(arrow.has_value());
    if (!arrow.has_value() || arrow->laterDamage.empty()) {
        return;
    }
    CHECK_EQ(arrow->laterDamage[0].dice, std::string("3d4"));  // both parts scale
    CHECK_EQ(arrow->laterDamage[0].type, std::string("acid"));
    const auto sphere = spellAsAttack("Vitriolic Sphere", 5, 15, std::nullopt);
    CHECK(sphere.has_value());
    if (sphere.has_value() && !sphere->laterDamage.empty()) {
        CHECK_EQ(sphere->laterDamage[0].dice, std::string("5d4"));  // only the first part scales
    }

    Encounter encounter;
    Monster dragon;
    dragon.id = "dragon";
    dragon.name = "Dragon";
    encounter.combatants.push_back(makeMonsterCombatant(dragon, "d"));
    encounter.combatants.push_back(hero());
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    encounter.started = true;
    encounter.turnIndex = 0;  // the dragon's turn
    addDelayedDamage(encounter, encounter.combatants[1], arrow->laterDamage, "Dragon's Acid Arrow", "d");
    CHECK(advanceTurn(encounter).empty());  // the dragon's turn ends: nothing
    const std::vector<TurnEvent> events = advanceTurn(encounter);  // Aria's turn ends
    CHECK_EQ(events.size(), std::size_t{1});
    if (!events.empty()) {
        CHECK(events[0].kind == TurnEvent::Kind::OngoingDamage);
        CHECK_EQ(events[0].combatantId, std::string("c"));
        CHECK_EQ(events[0].sourceId, std::string("d"));
        CHECK_EQ(events[0].damage[0].dice, std::string("3d4"));
    }
    CHECK(encounter.combatants[1].delayedDamage.empty());
    advanceTurn(encounter);
    CHECK(advanceTurn(encounter).empty());  // only once

    // Hit on its own turn: the end of its following turn.
    encounter.turnIndex = 1;
    addDelayedDamage(encounter, encounter.combatants[1], arrow->laterDamage, "Dragon's Acid Arrow", "d");
    CHECK(advanceTurn(encounter).empty());
    CHECK(advanceTurn(encounter).empty());
    CHECK_EQ(advanceTurn(encounter).size(), std::size_t{1});
}

TEST_CASE("a concentration spell can be used again on later turns, at the level it was cast")
{
    const auto lightning = spellAsAttack("Call Lightning", 4, 15, std::nullopt);
    CHECK(lightning.has_value());
    if (!lightning.has_value()) {
        return;
    }
    const std::optional<SustainedSpell> sustained = sustainedSpellFor(*lightning);
    CHECK(sustained.has_value());
    if (!sustained.has_value()) {
        return;
    }
    CHECK_EQ(sustained->spellId, std::string("call-lightning"));
    CHECK_EQ(sustained->slot, 4);
    CHECK_EQ(sustained->saveDc.value_or(0), 15);
    const std::vector<SpellFollowUp> again = spellFollowUps(*sustained);
    CHECK_EQ(again.size(), std::size_t{1});
    if (!again.empty()) {
        CHECK(again[0].cost == FollowUpCost::Action);
        CHECK_EQ(again[0].attack.name, std::string("Call Lightning (again)"));
        CHECK(again[0].attack.concentration.empty());  // not a new casting
        CHECK(again[0].attack.area);
        CHECK_EQ(again[0].attack.damage[0].dice, std::string("4d10"));
        CHECK_EQ(again[0].attack.save->dc, 15);
    }

    // A spell with nothing to do later keeps no record.
    const auto fireball = spellAsAttack("Fireball", 3, 15, std::nullopt);
    CHECK(fireball.has_value() && !sustainedSpellFor(*fireball).has_value());

    // Moonbeam: move it (an action), and its area (no action).
    const auto moon = spellAsAttack("Moonbeam", 2, 14, std::nullopt);
    CHECK(moon.has_value());
    if (moon.has_value()) {
        const std::vector<SpellFollowUp> uses = spellFollowUps(*sustainedSpellFor(*moon));
        CHECK_EQ(uses.size(), std::size_t{2});
        if (uses.size() == 2) {
            CHECK(uses[0].cost == FollowUpCost::Action);
            CHECK(uses[1].cost == FollowUpCost::Free);
        }
    }

    // Spiritual Weapon: a bonus action attack. Wall of Fire: its area's own damage.
    const auto weapon = spellAsAttack("Spiritual Weapon", 2, 13, 5);
    CHECK(weapon.has_value());
    if (weapon.has_value()) {
        const std::vector<SpellFollowUp> uses = spellFollowUps(*sustainedSpellFor(*weapon));
        CHECK(!uses.empty() && uses[0].cost == FollowUpCost::BonusAction);
        CHECK(!uses.empty() && uses[0].attack.attackBonus.value_or(0) == 5);
    }
    const auto wall = spellAsAttack("Wall of Fire", 4, 16, std::nullopt);
    CHECK(wall.has_value());
    if (wall.has_value()) {
        const std::vector<SpellFollowUp> uses = spellFollowUps(*sustainedSpellFor(*wall));
        CHECK(!uses.empty() && uses[0].cost == FollowUpCost::Free);
        CHECK(!uses.empty() && uses[0].attack.damage.size() == 1 && uses[0].attack.damage[0].dice == "5d8");
    }

    // The record goes when the concentration does.
    Combatant priest = hero();
    setConcentration(priest, "call-lightning");
    priest.sustained = sustained;
    endConcentration(priest);
    CHECK(!priest.sustained.has_value());
}

TEST_CASE("Heat Metal burns without a save, and Ensnaring Strike hurts only while Restrained")
{
    const auto heat = spellAsAttack("Heat Metal", 2, 13, std::nullopt);
    CHECK(heat.has_value());
    if (heat.has_value()) {
        CHECK(!heat->save.has_value());
        CHECK_EQ(heat->damage.size(), std::size_t{1});
        const std::vector<SpellFollowUp> uses = spellFollowUps(*sustainedSpellFor(*heat));
        CHECK(!uses.empty() && uses[0].cost == FollowUpCost::BonusAction && !uses[0].attack.save.has_value());
    }
    const auto ensnare = spellAsAttack("Ensnaring Strike", 2, 13, std::nullopt);
    CHECK(ensnare.has_value());
    if (ensnare.has_value() && !ensnare->riders.empty()) {
        CHECK(ensnare->damage.empty());
        CHECK_EQ(ensnare->riders[0].ongoing.size(), std::size_t{1});
        CHECK_EQ(ensnare->riders[0].ongoing[0].dice, std::string("2d6"));
        CHECK_EQ(ensnare->riders[0].ongoingAt, std::string(kOngoingAtTarget));
    }
}

TEST_CASE("Searing Smite burns at the start of each turn and saves after; Phantasmal Killer saves at the end")
{
    const auto smite = spellAsAttack("Searing Smite", 2, 14, std::nullopt);
    CHECK(smite.has_value());
    if (!smite.has_value()) {
        return;
    }
    CHECK(!smite->save.has_value());  // the extra damage comes with the hit
    CHECK(smite->repeatSave.has_value());
    CHECK_EQ(smite->damage.size(), std::size_t{1});
    CHECK_EQ(smite->damage[0].dice, std::string("2d6"));  // all the damage scales
    CHECK_EQ(smite->riders.size(), std::size_t{1});
    if (smite->riders.empty()) {
        return;
    }
    CHECK(smite->riders[0].saveAtStart);
    CHECK_EQ(smite->riders[0].ongoing[0].dice, std::string("2d6"));

    Encounter encounter;
    Monster paladin;
    paladin.id = "paladin";
    paladin.name = "Paladin";
    encounter.combatants.push_back(makeMonsterCombatant(paladin, "p"));
    encounter.combatants.push_back(hero());
    encounter.combatants[0].initiative = 20;
    encounter.combatants[1].initiative = 10;
    encounter.started = true;
    encounter.turnIndex = 0;
    applyRider(encounter, encounter.combatants[0], encounter.combatants[1], *smite, smite->riders[0]);
    CHECK(hasCondition(encounter.combatants[1], kBurning));
    std::vector<TurnEvent> events = advanceTurn(encounter);  // the paladin's turn ends; Aria's starts
    CHECK_EQ(events.size(), std::size_t{2});
    if (events.size() == 2) {
        CHECK(events[0].kind == TurnEvent::Kind::OngoingDamage);  // the damage first
        CHECK(events[1].kind == TurnEvent::Kind::SaveToEnd);      // then the save
        CHECK_EQ(events[1].dc, 14);
    }
    events = advanceTurn(encounter);  // Aria's turn ends: no second save
    CHECK(std::none_of(events.begin(), events.end(),
                       [](const TurnEvent& event) { return event.kind == TurnEvent::Kind::SaveToEnd; }));

    const auto killer = spellAsAttack("Phantasmal Killer", 5, 15, std::nullopt);
    CHECK(killer.has_value());
    if (!killer.has_value() || killer->riders.empty()) {
        return;
    }
    const ConditionRider& fear = killer->riders[0];
    CHECK(fear.saveEnds && !fear.saveAtStart);
    CHECK_EQ(fear.saveFailDamage[0].dice, std::string("5d10"));
    Combatant& aria = encounter.combatants[1];
    aria.conditions.clear();
    applyRider(encounter, encounter.combatants[0], aria, *killer, fear);
    CHECK(hasCondition(aria, kPhantasmalFear));
    CHECK(aria.conditions[0].saveEnds.has_value());
    CHECK_EQ(aria.conditions[0].saveEnds->failDamage[0].dice, std::string("5d10"));
    CHECK_EQ(aria.conditions[0].concentration, std::string("phantasmal-killer"));

    // Disadvantage on attack rolls, with the reason named.
    MonsterAttack bite;
    bite.name = "Bite";
    bite.attackBonus = 5;
    CHECK(suggestedAttackMode(aria, encounter.combatants[0], false) == RollMode::Disadvantage);
    const AttackModeChoice choice = decideAttackMode(encounter, aria, encounter.combatants[0], bite);
    CHECK(choice.mode == RollMode::Disadvantage);
    CHECK(!choice.disadvantages.empty() && choice.disadvantages[0] == "Aria is Phantasmal Fear");

    // Its save is asked at the end of Aria's turn.
    encounter.turnIndex = 1;
    events = advanceTurn(encounter);
    CHECK(std::any_of(events.begin(), events.end(), [](const TurnEvent& event) {
        return event.kind == TurnEvent::Kind::SaveToEnd && event.conditionId == kPhantasmalFear;
    }));
}

TEST_CASE("a Sleep (1/Day) action is limited to once a day and ends with the last sleeper")
{
    Monster mephit;
    mephit.name = "Dust Mephit";
    MonsterAttack sleep;
    sleep.name = "Sleep (1/Day)";
    sleep.perDay = 1;
    sleep.effect = "The mephit casts the Sleep spell, requiring no spell components and using Charisma as the "
                   "spellcasting ability (spell save DC 10).";
    mephit.attacks = {sleep};
    const std::vector<MonsterAttack> spells = actionableSpells(mephit, mephit.attacks[0]);
    CHECK(!spells.empty());
    if (spells.empty()) {
        return;
    }
    CHECK_EQ(spells[0].name, std::string("Sleep"));
    CHECK(spells[0].perDay.has_value());
    CHECK_EQ(*spells[0].perDay, 1);

    Combatant caster;
    caster.id = "mephit";
    caster.name = "Dust Mephit";
    caster.concentration = "sleep";
    caster.concentrationLanded = true;
    Combatant victim;
    victim.id = "pc";
    victim.name = "Pc";
    ActiveCondition asleep;
    asleep.id = "incapacitated";
    asleep.byId = "mephit";
    asleep.concentration = "sleep";
    victim.conditions.push_back(asleep);
    Encounter encounter;
    encounter.combatants = {caster, victim};
    releaseConditions(encounter);
    CHECK_EQ(encounter.combatants[0].concentration, std::string("sleep"));
    encounter.combatants[1].conditions.clear();
    releaseConditions(encounter);
    CHECK(encounter.combatants[0].concentration.empty());
}

TEST_CASE("Sleep does not affect a creature immune to Exhaustion")
{
    Combatant caster;
    caster.id = "mephit";
    Combatant zombie;
    zombie.id = "zombie";
    zombie.conditionImmunities = {"exhaustion"};
    Combatant guard;
    guard.id = "guard";
    Encounter encounter;
    encounter.combatants = {caster, zombie, guard};
    MonsterAttack spell;
    spell.name = "Sleep";
    ConditionRider rider;
    rider.conditions = {"incapacitated"};
    rider.on = kRiderOnFailure;
    rider.concentration = "sleep";
    const RiderOutcome safe = applyRider(encounter, encounter.combatants[0], encounter.combatants[1], spell, rider);
    CHECK(safe.added.empty());
    CHECK_EQ(safe.immune.size(), std::size_t{1});
    CHECK(!hasCondition(encounter.combatants[1], "incapacitated"));
    const RiderOutcome hit = applyRider(encounter, encounter.combatants[0], encounter.combatants[2], spell, rider);
    CHECK_EQ(hit.added.size(), std::size_t{1});
}
