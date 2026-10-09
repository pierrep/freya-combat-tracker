#include "core/encounter.h"

#include "core/combat_rules.h"
#include "core/text.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace combat {

int timedAcBonus(const std::string& effectName)
{
    // "ac:2:Shimmering Shield"
    if (effectName.rfind("ac:", 0) != 0) {
        return 0;
    }
    const std::size_t end = effectName.find(':', 3);
    try {
        return std::stoi(effectName.substr(3, end == std::string::npos ? std::string::npos : end - 3));
    } catch (const std::exception&) {
        return 0;
    }
}


namespace {

int combatantCount(const std::vector<Combatant>& combatants)
{
    return static_cast<int>(combatants.size());
}

int addRoll(int roll, int bonus)
{
    const long long total = static_cast<long long>(roll) + static_cast<long long>(bonus);
    return static_cast<int>(std::clamp<long long>(total, std::numeric_limits<int>::min(),
                                                  std::numeric_limits<int>::max()));
}

int copyNumber(const std::string& name, const std::string& monsterName)
{
    const std::string prefix = monsterName + " ";
    if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) {
        return 0;
    }
    long long value = 0;
    for (std::size_t i = prefix.size(); i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < '0' || c > '9') {
            return 0;
        }
        value = value * 10 + (c - '0');
        if (value > std::numeric_limits<int>::max()) {
            return std::numeric_limits<int>::max();
        }
    }
    return static_cast<int>(value);
}

std::string copyBaseName(const std::string& name)
{
    const auto space = name.rfind(' ');
    if (space == std::string::npos || space == 0 || space + 1 >= name.size()) {
        return name;
    }
    for (std::size_t i = space + 1; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < '0' || c > '9') {
            return name;
        }
    }
    return name.substr(0, space);
}

std::array<int, 6> characterSaves(const Character& character)
{
    std::array<int, 6> saves{};
    for (const Ability ability : kAbilityOrder) {
        saves[static_cast<std::size_t>(ability)] = characterSaveBonus(character, ability);
    }
    return saves;
}

std::array<int, 6> monsterSaves(const Monster& monster)
{
    std::array<int, 6> saves{};
    for (const Ability ability : kAbilityOrder) {
        saves[static_cast<std::size_t>(ability)] = monsterSaveBonus(monster, ability);
    }
    return saves;
}

int livingPosition(const std::vector<int>& order, int turnIndex)
{
    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        if (order[static_cast<std::size_t>(i)] == turnIndex) {
            return i;
        }
    }
    return -1;
}

// Counts down durations anchored to this boundary of the anchor's turn.
void tickDurations(Encounter& encounter, const std::string& anchorId, TurnBoundary boundary,
                   std::vector<TurnEvent>& events)
{
    for (Combatant& combatant : encounter.combatants) {
        std::vector<ActiveCondition> kept;
        for (ActiveCondition& condition : combatant.conditions) {
            if (condition.duration.has_value() && condition.duration->anchorId == anchorId &&
                condition.duration->boundary == boundary) {
                ConditionDuration& duration = *condition.duration;
                if (duration.skipNext) {
                    duration.skipNext = false;
                } else {
                    duration.turnsRemaining -= 1;
                }
                if (duration.turnsRemaining <= 0) {
                    TurnEvent event;
                    event.kind = TurnEvent::Kind::ConditionEnded;
                    event.combatantId = combatant.id;
                    event.conditionId = condition.id;
                    events.push_back(event);
                    continue;
                }
            }
            kept.push_back(condition);
        }
        combatant.conditions = std::move(kept);
        // Timed effects (Aversion to Fire, War Cry's Advantage, Shimmering
        // Shield's AC) end the same way; more AC is taken off again.
        std::erase_if(combatant.timedEffects, [&anchorId, boundary, &combatant](auto& effect) {
            ConditionDuration& duration = effect.second;
            if (duration.anchorId != anchorId || duration.boundary != boundary) {
                return false;
            }
            if (duration.skipNext) {
                duration.skipNext = false;
                return false;
            }
            duration.turnsRemaining -= 1;
            if (duration.turnsRemaining > 0) {
                return false;
            }
            combatant.ac -= timedAcBonus(effect.first);
            return true;
        });
    }
}

void endTurn(Encounter& encounter, int index, std::vector<TurnEvent>& events)
{
    if (index < 0 || index >= combatantCount(encounter.combatants)) {
        return;
    }
    const std::string id = encounter.combatants[static_cast<std::size_t>(index)].id;
    tickDurations(encounter, id, TurnBoundary::End, events);
    for (Combatant& combatant : encounter.combatants) {
        if (combatant.id != id) {
            continue;
        }
        for (const ActiveCondition& condition : combatant.conditions) {
            if (condition.saveEnds.has_value() && !condition.saveEnds->manual) {
                TurnEvent event;
                event.kind = TurnEvent::Kind::SaveToEnd;
                event.combatantId = id;
                event.conditionId = condition.id;
                event.ability = condition.saveEnds->ability;
                event.dc = condition.saveEnds->dc;
                events.push_back(event);
            }
        }
    }
}

void startTurn(Encounter& encounter, int index, const RollDie& rollDie, std::vector<TurnEvent>& events)
{
    if (index < 0 || index >= combatantCount(encounter.combatants)) {
        return;
    }
    Combatant& combatant = encounter.combatants[static_cast<std::size_t>(index)];
    const std::string id = combatant.id;
    startTurnEconomy(combatant);
    if (rollDie && !combatant.expended.empty()) {
        std::vector<std::string> stillExpended;
        for (const std::string& name : combatant.expended) {
            int lowest = 6;
            if (combatant.statBlock.has_value()) {
                for (const MonsterAttack& attack : combatant.statBlock->attacks) {
                    if (attack.name == name && attack.recharge.has_value()) {
                        lowest = *attack.recharge;
                    }
                }
                for (const auto* list : {&combatant.statBlock->traits, &combatant.statBlock->bonusActions,
                                         &combatant.statBlock->reactions, &combatant.statBlock->legendaryActions}) {
                    for (const MonsterFeature& feature : *list) {
                        if (feature.name == name && feature.recharge.has_value()) {
                            lowest = *feature.recharge;
                        }
                    }
                }
            }
            const int face = rollDie(6);
            TurnEvent event;
            event.combatantId = id;
            event.actionName = name;
            event.roll = face;
            if (face >= lowest) {
                event.kind = TurnEvent::Kind::Recharged;
            } else {
                event.kind = TurnEvent::Kind::NotRecharged;
                stillExpended.push_back(name);
            }
            events.push_back(event);
        }
        combatant.expended = std::move(stillExpended);
    }
    const bool dying = isDying(combatant);
    // Ongoing damage: the creature's own conditions that hurt at the start of
    // its turns, and conditions it causes that hurt at the start of its turns
    // (a creature it has swallowed).
    for (const Combatant& other : encounter.combatants) {
        for (const ActiveCondition& condition : other.conditions) {
            if (condition.ongoing.empty()) {
                continue;
            }
            const bool atTarget = condition.ongoingAt != kOngoingAtSource && other.id == id;
            const bool atSource = condition.ongoingAt == kOngoingAtSource && condition.byId == id;
            if (!atTarget && !atSource) {
                continue;
            }
            TurnEvent event;
            event.kind = TurnEvent::Kind::OngoingDamage;
            event.combatantId = other.id;
            event.conditionId = condition.id;
            event.damage = condition.ongoing;
            event.sourceId = condition.byId;
            event.source = condition.source;
            events.push_back(event);
        }
    }
    tickDurations(encounter, id, TurnBoundary::Start, events);
    if (dying) {
        TurnEvent event;
        event.kind = TurnEvent::Kind::DeathSave;
        event.combatantId = id;
        events.push_back(event);
    }
}

}  // namespace

std::vector<std::string> validateCombatant(const Combatant& combatant)
{
    std::vector<std::string> problems;
    if (combatant.id.empty()) {
        problems.emplace_back("Id is required.");
    }
    if (combatant.source != kCombatantSourceCharacter && combatant.source != kCombatantSourceMonster) {
        problems.emplace_back("Source must be \"character\" or \"monster\".");
    }
    if (combatant.sourceId.empty()) {
        problems.emplace_back("Source id is required.");
    }
    if (isBlank(combatant.name)) {
        problems.emplace_back("Name is required.");
    }
    if (combatant.source == kCombatantSourceCharacter && combatant.initiativeBonus.has_value()) {
        problems.emplace_back("Characters do not store an initiative bonus.");
    }
    if (combatant.hp < 0 || combatant.tempHp < 0) {
        problems.emplace_back("Hit points cannot be negative.");
    }
    if (combatant.exhaustion < 0 || combatant.exhaustion > 6) {
        problems.emplace_back("Exhaustion must be 0 through 6.");
    }
    if (combatant.deathSaves.successes < 0 || combatant.deathSaves.successes > 3 || combatant.deathSaves.failures < 0 ||
        combatant.deathSaves.failures > 3) {
        problems.emplace_back("Death saves must be 0 through 3.");
    }
    std::vector<std::string> conditionIds;
    for (const ActiveCondition& condition : combatant.conditions) {
        if (condition.id.empty()) {
            problems.emplace_back("Condition id is required.");
            break;
        }
        if (std::find(conditionIds.begin(), conditionIds.end(), condition.id) != conditionIds.end()) {
            problems.emplace_back("Duplicate condition id.");
            break;
        }
        conditionIds.push_back(condition.id);
    }
    return problems;
}

std::vector<std::string> validateEncounter(const Encounter& encounter)
{
    std::vector<std::string> problems;
    if (encounter.id.empty()) {
        problems.emplace_back("Id is required.");
    }
    if (isBlank(encounter.name)) {
        problems.emplace_back("Name is required.");
    }
    if (encounter.round < 1) {
        problems.emplace_back("Round must be at least 1.");
    }

    const int count = combatantCount(encounter.combatants);
    if (encounter.turnIndex < 0 || (count == 0 && encounter.turnIndex != 0) ||
        (count > 0 && encounter.turnIndex >= count)) {
        problems.emplace_back("Turn index is outside the combatant list.");
    }

    std::vector<std::string> ids;
    ids.reserve(encounter.combatants.size());
    for (const Combatant& combatant : encounter.combatants) {
        const auto combatantProblems = validateCombatant(combatant);
        problems.insert(problems.end(), combatantProblems.begin(), combatantProblems.end());
        if (!combatant.id.empty()) {
            if (std::find(ids.begin(), ids.end(), combatant.id) != ids.end()) {
                problems.emplace_back("Duplicate combatant id.");
            }
            ids.push_back(combatant.id);
        }
    }
    return problems;
}

bool isMonsterCombatant(const Combatant& combatant)
{
    return combatant.source == kCombatantSourceMonster;
}

bool isCharacterCombatant(const Combatant& combatant)
{
    return combatant.source == kCombatantSourceCharacter;
}

bool hasCondition(const Combatant& combatant, const std::string& conditionId)
{
    return std::any_of(combatant.conditions.begin(), combatant.conditions.end(),
                       [&conditionId](const ActiveCondition& row) { return row.id == conditionId; });
}

bool isDying(const Combatant& combatant)
{
    return isCharacterCombatant(combatant) && combatant.hp == 0 && !combatant.stable && !combatant.dead;
}

bool isInInitiative(const Combatant& combatant)
{
    if (combatant.dead) {
        return false;
    }
    if (isCharacterCombatant(combatant)) {
        return true;
    }
    return combatant.hp > 0 || combatant.tempHp > 0;
}

bool assignMonsterCopyNames(std::vector<Combatant>& combatants)
{
    std::vector<std::string> sourceIds;
    for (const Combatant& combatant : combatants) {
        if (!isMonsterCombatant(combatant)) {
            continue;
        }
        if (std::find(sourceIds.begin(), sourceIds.end(), combatant.sourceId) == sourceIds.end()) {
            sourceIds.push_back(combatant.sourceId);
        }
    }
    bool changed = false;
    for (const std::string& sourceId : sourceIds) {
        std::vector<int> indexes;
        for (int i = 0; i < combatantCount(combatants); ++i) {
            const Combatant& combatant = combatants[static_cast<std::size_t>(i)];
            if (isMonsterCombatant(combatant) && combatant.sourceId == sourceId) {
                indexes.push_back(i);
            }
        }
        if (indexes.empty()) {
            continue;
        }
        const std::string base = copyBaseName(combatants[static_cast<std::size_t>(indexes.front())].name);
        if (indexes.size() == 1) {
            Combatant& only = combatants[static_cast<std::size_t>(indexes.front())];
            if (only.name != base) {
                only.name = base;
                changed = true;
            }
            continue;
        }
        bool unique = true;
        std::vector<int> seen;
        for (const int index : indexes) {
            const int number = copyNumber(combatants[static_cast<std::size_t>(index)].name, base);
            if (number <= 0 || std::find(seen.begin(), seen.end(), number) != seen.end()) {
                unique = false;
                break;
            }
            seen.push_back(number);
        }
        if (unique) {
            continue;
        }
        int number = 1;
        for (const int index : indexes) {
            const std::string numbered = base + " " + std::to_string(number);
            ++number;
            Combatant& combatant = combatants[static_cast<std::size_t>(index)];
            if (combatant.name != numbered) {
                combatant.name = numbered;
                changed = true;
            }
        }
    }
    return changed;
}

Combatant makeCharacterCombatant(const Character& character, const std::string& combatantId)
{
    Combatant combatant;
    combatant.id = combatantId;
    combatant.source = kCombatantSourceCharacter;
    combatant.sourceId = character.id;
    combatant.name = character.name;
    combatant.initiative = 0;
    refreshCharacterCombatant(combatant, character);
    return combatant;
}

bool refreshCharacterCombatant(Combatant& combatant, const Character& character)
{
    const Combatant before = combatant;
    combatant.name = character.name;
    combatant.ac = character.ac;
    combatant.maxHp = std::max(0, character.hp.max - combatant.maxHpReduction);
    for (const auto& effect : combatant.timedEffects) {
        combatant.ac += timedAcBonus(effect.first);  // a Shimmering Shield still up
    }
    const int sheetHp = cappedHitPoints(std::max(0, character.hp.current), combatant.maxHp);
    if (sheetHp > 0 && combatant.hp == 0) {
        // Healed on the sheet between fights.
        combatant.dead = false;
        combatant.stable = false;
        combatant.deathSaves = {};
        combatant.conditions.erase(std::remove_if(combatant.conditions.begin(), combatant.conditions.end(),
                                                  [](const ActiveCondition& row) { return row.id == "unconscious"; }),
                                   combatant.conditions.end());
    }
    combatant.hp = sheetHp;
    combatant.tempHp = std::max(0, character.tempHp);
    combatant.exhaustion = std::clamp(character.exhaustion, 0, 6);
    combatant.defenses = character.defenses;
    combatant.saveBonuses = characterSaves(character);
    return !(before == combatant);
}

void applyTraitEffects(Combatant& combatant, const Monster& monster)
{
    for (const MonsterFeature& trait : monster.traits) {
        if (!trait.selfEffect.has_value() || trait.selfEffect->condition.empty() ||
            hasCondition(combatant, trait.selfEffect->condition)) {
            continue;
        }
        ActiveCondition condition;
        condition.id = trait.selfEffect->condition;
        condition.source = trait.selfEffect->source;
        condition.endsOn = trait.selfEffect->endsOn;
        combatant.conditions.push_back(std::move(condition));
    }
}

Combatant makeMonsterCombatant(const Monster& monster, const std::string& combatantId)
{
    Combatant combatant;
    combatant.id = combatantId;
    combatant.source = kCombatantSourceMonster;
    combatant.sourceId = monster.id;
    combatant.name = monster.name;
    combatant.initiative = 0;
    combatant.initiativeBonus = monster.initiativeBonus;
    combatant.hp = std::max(0, monster.hp);
    combatant.maxHp = std::max(0, monster.hp);
    combatant.ac = monster.ac;
    fillMonsterSnapshot(combatant, monster);
    applyTraitEffects(combatant, monster);
    startTurnEconomy(combatant);
    return combatant;
}

// Every X/Day entry at its full count.
void fillDailyUses(Combatant& combatant, const Monster& monster)
{
    for (const MonsterAttack& attack : monster.attacks) {
        if (attack.perDay.has_value() && combatant.usesRemaining.find(attack.name) == combatant.usesRemaining.end()) {
            combatant.usesRemaining[attack.name] = *attack.perDay;
        }
    }
    for (const auto* list : {&monster.traits, &monster.bonusActions, &monster.reactions, &monster.legendaryActions}) {
        for (const MonsterFeature& feature : *list) {
            if (feature.perDay.has_value() &&
                combatant.usesRemaining.find(feature.name) == combatant.usesRemaining.end()) {
                combatant.usesRemaining[feature.name] = *feature.perDay;
            }
        }
    }
}

namespace {

// A fight saved before the SRD catalog carried an ability's rules gets them
// from the catalog: what it does to targets, who it can target, what it does
// to its user, auras, and whether a bonus action is aimed. Only rules the
// stored copy lacks are added, so nothing it already has changes.
bool upgradeSrdSnapshot(Monster& stored, const Monster& catalog)
{
    if (stored.source != kSrdMonsterSource || catalog.source != kSrdMonsterSource || stored.id != catalog.id) {
        return false;
    }
    bool changed = false;
    if (stored.legendaryActionUses == 0 && catalog.legendaryActionUses > 0) {
        stored.legendaryActionUses = catalog.legendaryActionUses;
        changed = true;
    }
    if (stored.skills.empty() && !catalog.skills.empty()) {
        stored.skills = catalog.skills;
        changed = true;
    }
    for (MonsterAttack& attack : stored.attacks) {
        for (const MonsterAttack& fresh : catalog.attacks) {
            if (fresh.name != attack.name) {
                continue;
            }
            if (attack.riders.empty() && !fresh.riders.empty()) {
                attack.riders = fresh.riders;
                attack.riderSave = fresh.riderSave;
                changed = true;
            }
            if (!attack.drain.has_value() && fresh.drain.has_value()) {
                attack.drain = fresh.drain;
                changed = true;
            }
            if (!attack.advantageIfGrappled && fresh.advantageIfGrappled) {
                attack.advantageIfGrappled = true;
                changed = true;
            }
            if (attack.targetCondition.empty() && !fresh.targetCondition.empty()) {
                attack.targetCondition = fresh.targetCondition;
                changed = true;
            }
            if (attack.targetMaxSize.empty() && !fresh.targetMaxSize.empty()) {
                attack.targetMaxSize = fresh.targetMaxSize;
                changed = true;
            }
            if (!attack.failureHpThreshold.has_value() && fresh.failureHpThreshold.has_value()) {
                attack.failureHpThreshold = fresh.failureHpThreshold;
                attack.failureHpEffect = fresh.failureHpEffect;
                changed = true;
            }
            if (attack.failureSelfHealing.empty() && !fresh.failureSelfHealing.empty()) {
                attack.failureSelfHealing = fresh.failureSelfHealing;
                changed = true;
            }
            // Whom it can affect by creature type (Horrific Visage skips Undead).
            if (attack.targetTypes.empty() && !fresh.targetTypes.empty()) {
                attack.targetTypes = fresh.targetTypes;
                changed = true;
            }
            if (!attack.targetAtZeroHp && fresh.targetAtZeroHp) {
                attack.targetAtZeroHp = true;
                changed = true;
            }
            if (attack.targetExceptTypes.empty() && !fresh.targetExceptTypes.empty()) {
                attack.targetExceptTypes = fresh.targetExceptTypes;
                changed = true;
            }
            if (!attack.selfEffect.has_value() && fresh.selfEffect.has_value()) {
                attack.selfEffect = fresh.selfEffect;
                changed = true;
            }
            // Multiattack read wrongly before (the Vampire's, under a
            // form-limited name, allowed one attack and named none).
            if (!attack.inMultiattack && fresh.inMultiattack) {
                attack.inMultiattack = true;
                changed = true;
            }
            if (attack.count < fresh.count) {
                attack.count = fresh.count;
                changed = true;
            }
        }
    }
    const std::vector<MonsterFeature>* freshLists[] = {&catalog.traits, &catalog.bonusActions, &catalog.reactions,
                                                       &catalog.legendaryActions};
    std::vector<MonsterFeature>* storedLists[] = {&stored.traits, &stored.bonusActions, &stored.reactions,
                                                  &stored.legendaryActions};
    for (std::size_t list = 0; list < 4; ++list) {
        for (MonsterFeature& feature : *storedLists[list]) {
            for (const MonsterFeature& fresh : *freshLists[list]) {
                if (fresh.name != feature.name) {
                    continue;
                }
                if (!feature.targeted.has_value() && fresh.targeted.has_value()) {
                    feature.targeted = fresh.targeted;
                    changed = true;
                }
                // Consume Life was aimed before it knew whom it can take and
                // what a failure does.
                if (feature.targeted.has_value() && fresh.targeted.has_value()) {
                    MonsterAttack& aimed = *feature.targeted;
                    const MonsterAttack& wanted = *fresh.targeted;
                    if (!aimed.failureHpThreshold.has_value() && wanted.failureHpThreshold.has_value()) {
                        aimed.failureHpThreshold = wanted.failureHpThreshold;
                        aimed.failureHpEffect = wanted.failureHpEffect;
                        changed = true;
                    }
                    if (aimed.failureSelfHealing.empty() && !wanted.failureSelfHealing.empty()) {
                        aimed.failureSelfHealing = wanted.failureSelfHealing;
                        changed = true;
                    }
                    if (!aimed.targetAtZeroHp && wanted.targetAtZeroHp) {
                        aimed.targetAtZeroHp = true;
                        changed = true;
                    }
                    if (aimed.targetExceptTypes.empty() && !wanted.targetExceptTypes.empty()) {
                        aimed.targetExceptTypes = wanted.targetExceptTypes;
                        changed = true;
                    }
                    if (aimed.targetTypes.empty() && !wanted.targetTypes.empty()) {
                        aimed.targetTypes = wanted.targetTypes;
                        changed = true;
                    }
                    if (aimed.riders.empty() && !wanted.riders.empty()) {
                        aimed.riders = wanted.riders;
                        aimed.riderSave = wanted.riderSave;
                        changed = true;
                    }
                }
                if (!feature.aura.has_value() && fresh.aura.has_value()) {
                    feature.aura = fresh.aura;
                    changed = true;
                }
                if (!feature.afterDamagingBloodied && fresh.afterDamagingBloodied) {
                    feature.afterDamagingBloodied = true;
                    changed = true;
                }
                if (!feature.attackModifier.has_value() && fresh.attackModifier.has_value()) {
                    feature.attackModifier = fresh.attackModifier;
                    changed = true;
                }
                if (!feature.selfEffect.has_value() && fresh.selfEffect.has_value()) {
                    feature.selfEffect = fresh.selfEffect;
                    changed = true;
                }
                if (!feature.recharge.has_value() && !feature.perDay.has_value() &&
                    (fresh.recharge.has_value() || fresh.perDay.has_value())) {
                    feature.recharge = fresh.recharge;
                    feature.perDay = fresh.perDay;
                    changed = true;
                }
            }
        }
    }
    return changed;
}

}  // namespace

bool fillMonsterSnapshot(Combatant& combatant, const Monster& monster)
{
    if (!isMonsterCombatant(combatant)) {
        return false;
    }
    if (combatant.statBlock.has_value()) {
        return upgradeSrdSnapshot(*combatant.statBlock, monster);
    }
    combatant.statBlock = monster;
    combatant.defenses = monster.defenses;
    combatant.conditionImmunities = monster.conditionImmunities;
    combatant.saveBonuses = monsterSaves(monster);
    fillDailyUses(combatant, monster);
    return true;
}

bool encounterHasCharacter(const Encounter& encounter, const std::string& characterId)
{
    return std::any_of(encounter.combatants.begin(), encounter.combatants.end(), [&](const Combatant& combatant) {
        return isCharacterCombatant(combatant) && combatant.sourceId == characterId;
    });
}

void resetMonsters(Encounter& encounter)
{
    std::vector<std::string> monsterIds;
    for (const Combatant& combatant : encounter.combatants) {
        if (isMonsterCombatant(combatant)) {
            monsterIds.push_back(combatant.id);
        }
    }
    for (Combatant& combatant : encounter.combatants) {
        combatant.auraImmunities.clear();
        // Everyone rolls initiative again; a character's is typed in fresh, and
        // its conditions and concentration start over. A character still at 0
        // HP stays Unconscious: that comes from its Hit Points, not the fight.
        if (isCharacterCombatant(combatant)) {
            combatant.initiative = 0;
            const bool down = combatant.hp == 0 && !combatant.dead;
            std::erase_if(combatant.conditions, [down](const ActiveCondition& row) {
                return !(down && row.id == "unconscious");
            });
            if (down && !hasCondition(combatant, "unconscious")) {
                combatant.conditions.push_back(ActiveCondition{"unconscious", std::nullopt, std::nullopt});
            }
            combatant.concentration.clear();
            startTurnEconomy(combatant);
            for (const auto& effect : combatant.timedEffects) {
                combatant.ac -= timedAcBonus(effect.first);
            }
            combatant.timedEffects.clear();
        }
        std::erase_if(combatant.conditions, [&monsterIds](const ActiveCondition& row) {
            return std::find(monsterIds.begin(), monsterIds.end(), row.byId) != monsterIds.end();
        });
    }
    for (Combatant& combatant : encounter.combatants) {
        if (!isMonsterCombatant(combatant)) {
            continue;
        }
        // Monsters roll again too, on the Dashboard's initiative list.
        combatant.initiative = 0;
        if (combatant.maxHp.has_value()) {
            combatant.maxHp = *combatant.maxHp + combatant.maxHpReduction;
            combatant.hp = *combatant.maxHp;
        }
        combatant.maxHpReduction = 0;
        combatant.tempHp = 0;
        combatant.dead = false;
        combatant.conditions.clear();
        combatant.concentration.clear();
        combatant.aurasOff.clear();
        for (const auto& effect : combatant.timedEffects) {
            combatant.ac -= timedAcBonus(effect.first);
        }
        combatant.timedEffects.clear();
        if (combatant.statBlock.has_value()) {
            applyTraitEffects(combatant, *combatant.statBlock);
        }
        combatant.exhaustion = 0;
        combatant.expended.clear();
        combatant.usesRemaining.clear();
        if (combatant.statBlock.has_value()) {
            fillDailyUses(combatant, *combatant.statBlock);
        }
        startTurnEconomy(combatant);
    }
    // Back to the initiative phase.
    encounter.started = false;
    encounter.round = 1;
    const std::vector<int> order = initiativeOrder(encounter.combatants);
    encounter.turnIndex = order.empty() ? 0 : order.front();
}

bool carryCharacterHitPoints(std::vector<Character>& characters, const Combatant& combatant)
{
    if (!isCharacterCombatant(combatant)) {
        return false;
    }
    for (Character& character : characters) {
        if (character.id != combatant.sourceId) {
            continue;
        }
        character.hp.current = cappedHitPoints(std::max(0, combatant.hp), character.hp.max);
        character.tempHp = std::max(0, combatant.tempHp);
        character.exhaustion = std::clamp(combatant.exhaustion, 0, 6);
        return true;
    }
    return false;
}

int sortByInitiative(std::vector<Combatant>& combatants, int activeIndex)
{
    if (combatants.empty()) {
        return 0;
    }
    const int count = combatantCount(combatants);
    if (activeIndex < 0 || activeIndex >= count) {
        activeIndex = 0;
    }
    const std::string activeId = combatants[static_cast<std::size_t>(activeIndex)].id;
    std::stable_sort(combatants.begin(), combatants.end(), [](const Combatant& left, const Combatant& right) {
        return left.initiative > right.initiative;
    });
    for (int i = 0; i < count; ++i) {
        if (combatants[static_cast<std::size_t>(i)].id == activeId) {
            return i;
        }
    }
    return 0;
}

MoveResult moveCombatant(std::vector<Combatant>& combatants, int index, int direction, int turnIndex)
{
    MoveResult result;
    result.movedTo = index;
    result.turnIndex = turnIndex;
    const int count = combatantCount(combatants);
    if (count == 0 || (direction != -1 && direction != 1)) {
        return result;
    }
    if (index < 0 || index >= count) {
        return result;
    }
    if (!isInInitiative(combatants[static_cast<std::size_t>(index)])) {
        return result;
    }
    int destination = index;
    for (;;) {
        destination += direction;
        if (destination < 0 || destination >= count) {
            return result;
        }
        if (isInInitiative(combatants[static_cast<std::size_t>(destination)])) {
            break;
        }
    }
    std::swap(combatants[static_cast<std::size_t>(index)], combatants[static_cast<std::size_t>(destination)]);
    result.movedTo = destination;
    if (turnIndex == index) {
        result.turnIndex = destination;
    } else if (turnIndex == destination) {
        result.turnIndex = index;
    }
    return result;
}

int removeCombatant(std::vector<Combatant>& combatants, int index, int turnIndex)
{
    const int count = combatantCount(combatants);
    if (index < 0 || index >= count) {
        return count == 0 ? 0 : std::clamp(turnIndex, 0, count - 1);
    }
    combatants.erase(combatants.begin() + index);
    const int remaining = count - 1;
    if (remaining == 0) {
        return 0;
    }
    if (index < turnIndex) {
        return turnIndex - 1;
    }
    if (index == turnIndex && turnIndex >= remaining) {
        return 0;
    }
    if (turnIndex >= remaining) {
        return remaining - 1;
    }
    if (turnIndex < 0) {
        return 0;
    }
    return turnIndex;
}

int rollAllMonsterInitiatives(Encounter& encounter, const RollD20& rollD20, bool group)
{
    int missing = 0;
    std::map<std::string, int> groupRolls;
    for (Combatant& combatant : encounter.combatants) {
        if (!isMonsterCombatant(combatant)) {
            continue;
        }
        if (!combatant.initiativeBonus.has_value()) {
            ++missing;
        }
        const int bonus = combatant.initiativeBonus.value_or(0) - d20Penalty(combatant);
        int face = 0;
        if (group) {
            const auto found = groupRolls.find(combatant.sourceId);
            if (found != groupRolls.end()) {
                face = found->second;
            } else {
                face = rollD20();
                groupRolls[combatant.sourceId] = face;
            }
        } else {
            face = rollD20();
        }
        combatant.initiative = addRoll(face, bonus);
    }
    encounter.turnIndex = sortByInitiative(encounter.combatants, encounter.turnIndex);
    return missing;
}

void rollAllCharacterInitiatives(Encounter& encounter, const RollD20& rollD20,
                                 const std::map<std::string, int>& bonuses)
{
    for (Combatant& combatant : encounter.combatants) {
        if (!isCharacterCombatant(combatant)) {
            continue;
        }
        const auto found = bonuses.find(combatant.id);
        const int bonus = (found == bonuses.end() ? 0 : found->second) - d20Penalty(combatant);
        combatant.initiative = addRoll(rollD20(), bonus);
    }
    encounter.turnIndex = sortByInitiative(encounter.combatants, encounter.turnIndex);
}

bool rerollMonsterInitiative(Encounter& encounter, const std::string& combatantId, const RollD20& rollD20,
                             bool* bonusMissing)
{
    Combatant* target = nullptr;
    for (Combatant& combatant : encounter.combatants) {
        if (combatant.id == combatantId) {
            target = &combatant;
            break;
        }
    }
    if (target == nullptr || !isMonsterCombatant(*target)) {
        return false;
    }
    const bool missing = !target->initiativeBonus.has_value();
    if (bonusMissing != nullptr) {
        *bonusMissing = missing;
    }
    target->initiative = addRoll(rollD20(), target->initiativeBonus.value_or(0) - d20Penalty(*target));
    encounter.turnIndex = sortByInitiative(encounter.combatants, encounter.turnIndex);
    return true;
}

std::vector<int> initiativeOrder(const std::vector<Combatant>& combatants)
{
    std::vector<int> order;
    const int count = combatantCount(combatants);
    order.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (isInInitiative(combatants[static_cast<std::size_t>(i)])) {
            order.push_back(i);
        }
    }
    return order;
}

void keepTurnInInitiative(Encounter& encounter)
{
    const std::vector<int> order = initiativeOrder(encounter.combatants);
    if (order.empty()) {
        return;
    }
    const int count = combatantCount(encounter.combatants);
    if (encounter.turnIndex >= 0 && encounter.turnIndex < count &&
        isInInitiative(encounter.combatants[static_cast<std::size_t>(encounter.turnIndex)])) {
        return;
    }
    if (encounter.turnIndex < 0 || encounter.turnIndex >= count) {
        encounter.turnIndex = order.front();
        return;
    }
    for (int step = 1; step <= count; ++step) {
        const int index = (encounter.turnIndex + step) % count;
        if (isInInitiative(encounter.combatants[static_cast<std::size_t>(index)])) {
            encounter.turnIndex = index;
            return;
        }
    }
}

std::vector<TurnEvent> startCombat(Encounter& encounter, const RollDie& rollDie)
{
    std::vector<TurnEvent> events;
    const std::vector<int> order = initiativeOrder(encounter.combatants);
    encounter.started = true;
    encounter.round = 1;
    if (order.empty()) {
        return events;
    }
    encounter.turnIndex = order.front();
    startTurn(encounter, encounter.turnIndex, rollDie, events);
    return events;
}

std::vector<TurnEvent> advanceTurn(Encounter& encounter, const RollDie& rollDie)
{
    if (!encounter.started) {
        return startCombat(encounter, rollDie);
    }
    std::vector<TurnEvent> events;
    const std::vector<int> order = initiativeOrder(encounter.combatants);
    if (order.empty()) {
        return events;
    }
    const int count = combatantCount(encounter.combatants);
    if (encounter.turnIndex < 0 || encounter.turnIndex >= count) {
        encounter.turnIndex = order.front();
        startTurn(encounter, encounter.turnIndex, rollDie, events);
        return events;
    }
    endTurn(encounter, encounter.turnIndex, events);
    const int position = livingPosition(order, encounter.turnIndex);
    int next = -1;
    if (position >= 0 && position + 1 < static_cast<int>(order.size())) {
        next = order[static_cast<std::size_t>(position + 1)];
    } else if (position < 0) {
        for (const int index : order) {
            if (index > encounter.turnIndex) {
                next = index;
                break;
            }
        }
    }
    if (next < 0) {
        next = order.front();
        if (encounter.round < std::numeric_limits<int>::max()) {
            ++encounter.round;
        }
    }
    encounter.turnIndex = next;
    startTurn(encounter, next, rollDie, events);
    return events;
}

std::vector<TurnEvent> advanceRound(Encounter& encounter, const RollDie& rollDie)
{
    std::vector<TurnEvent> events;
    if (encounter.combatants.empty()) {
        return events;
    }
    if (encounter.round < std::numeric_limits<int>::max()) {
        ++encounter.round;
    }
    const std::vector<int> order = initiativeOrder(encounter.combatants);
    encounter.turnIndex = order.empty() ? 0 : order.front();
    if (!order.empty()) {
        startTurn(encounter, encounter.turnIndex, rollDie, events);
    }
    return events;
}

void restoreFightUndo(Encounter& encounter, std::vector<Character>& characters, const FightUndo& undo)
{
    encounter = undo.encounter;
    if (undo.roster.has_value()) {
        characters = *undo.roster;
    }
}

}  // namespace combat
