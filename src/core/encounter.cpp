#include "core/encounter.h"

#include "core/combat_rules.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace combat {

namespace {

bool isBlank(const std::string& text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

int combatantCount(const std::vector<Combatant>& combatants)
{
    return static_cast<int>(combatants.size());
}

int addRoll(int roll, int bonus)
{
    const long long total = static_cast<long long>(roll) + static_cast<long long>(bonus);
    if (total > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    if (total < std::numeric_limits<int>::min()) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(total);
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
    std::vector<std::string> conditionIds;
    for (const std::string& id : combatant.conditions) {
        if (id.empty()) {
            problems.emplace_back("Condition id is required.");
            break;
        }
        if (std::find(conditionIds.begin(), conditionIds.end(), id) != conditionIds.end()) {
            problems.emplace_back("Duplicate condition id.");
            break;
        }
        conditionIds.push_back(id);
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
        const std::string base =
            copyBaseName(combatants[static_cast<std::size_t>(indexes.front())].name);
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
    combatant.maxHp = character.hp.max;
    combatant.hp = cappedHitPoints(character.hp.current, combatant.maxHp);
    combatant.tempHp = character.tempHp;
    combatant.ac = character.ac;
    return combatant;
}

void resetMonsterHitPoints(Encounter& encounter)
{
    for (Combatant& combatant : encounter.combatants) {
        if (!isMonsterCombatant(combatant) || !combatant.maxHp.has_value()) {
            continue;
        }
        combatant.hp = *combatant.maxHp;
    }
}

bool carryCharacterHitPoints(std::vector<Character>& characters, const Combatant& combatant)
{
    if (combatant.source != kCombatantSourceCharacter) {
        return false;
    }
    for (Character& character : characters) {
        if (character.id != combatant.sourceId) {
            continue;
        }
        character.hp.current = cappedHitPoints(combatant.hp, character.hp.max);
        return true;
    }
    return false;
}

Combatant makeMonsterCombatant(const Monster& monster, const std::vector<Combatant>& /*existing*/,
                               const std::string& combatantId)
{
    Combatant combatant;
    combatant.id = combatantId;
    combatant.source = kCombatantSourceMonster;
    combatant.sourceId = monster.id;
    combatant.name = monster.name;
    combatant.initiative = 0;
    combatant.initiativeBonus = monster.initiativeBonus;
    combatant.hp = monster.hp;
    combatant.maxHp = monster.hp;
    combatant.ac = monster.ac;
    return combatant;
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

int rollAllMonsterInitiatives(Encounter& encounter, const RollD20& rollD20)
{
    int missing = 0;
    for (Combatant& combatant : encounter.combatants) {
        if (!isMonsterCombatant(combatant)) {
            continue;
        }
        if (!combatant.initiativeBonus.has_value()) {
            ++missing;
        }
        const int bonus = combatant.initiativeBonus.value_or(0);
        combatant.initiative = addRoll(rollD20(), bonus);
    }
    encounter.turnIndex = sortByInitiative(encounter.combatants, encounter.turnIndex);
    return missing;
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
    target->initiative = addRoll(rollD20(), target->initiativeBonus.value_or(0));
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

namespace {

int livingPosition(const std::vector<int>& order, int turnIndex)
{
    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        if (order[static_cast<std::size_t>(i)] == turnIndex) {
            return i;
        }
    }
    return -1;
}

}  // namespace

void advanceTurn(Encounter& encounter)
{
    const std::vector<int> order = initiativeOrder(encounter.combatants);
    if (order.empty()) {
        return;
    }
    const int count = combatantCount(encounter.combatants);
    if (encounter.turnIndex < 0 || encounter.turnIndex >= count) {
        encounter.turnIndex = order.front();
        return;
    }
    const int position = livingPosition(order, encounter.turnIndex);
    if (position >= 0 && position + 1 < static_cast<int>(order.size())) {
        encounter.turnIndex = order[static_cast<std::size_t>(position + 1)];
        return;
    }
    if (position < 0) {
        for (const int index : order) {
            if (index > encounter.turnIndex) {
                encounter.turnIndex = index;
                return;
            }
        }
    }
    encounter.turnIndex = order.front();
    if (encounter.round < std::numeric_limits<int>::max()) {
        ++encounter.round;
    }
}

void retreatTurn(Encounter& encounter)
{
    const std::vector<int> order = initiativeOrder(encounter.combatants);
    if (order.empty()) {
        return;
    }
    const int count = combatantCount(encounter.combatants);
    if (encounter.turnIndex < 0 || encounter.turnIndex >= count) {
        encounter.turnIndex = order.front();
        return;
    }
    const int position = livingPosition(order, encounter.turnIndex);
    if (position > 0) {
        encounter.turnIndex = order[static_cast<std::size_t>(position - 1)];
        return;
    }
    if (position < 0) {
        for (int i = static_cast<int>(order.size()) - 1; i >= 0; --i) {
            if (order[static_cast<std::size_t>(i)] < encounter.turnIndex) {
                encounter.turnIndex = order[static_cast<std::size_t>(i)];
                return;
            }
        }
    }
    encounter.turnIndex = order.back();
    if (encounter.round > 1) {
        --encounter.round;
    }
}

int attackAllotment(const std::vector<MonsterAttack>& attacks)
{
    for (const MonsterAttack& attack : attacks) {
        if (attack.name == "Multiattack") {
            return attack.count < 1 ? 1 : attack.count;
        }
    }
    return 1;
}

std::optional<int> completeMonsterAttack(Encounter& encounter, int targetIndex, int amount,
                                        std::vector<Character>& characters, int attackerIndex, int attacksUsed,
                                        int allotment)
{
    const int count = combatantCount(encounter.combatants);
    if (targetIndex < 0 || targetIndex >= count || amount < 0) {
        return std::nullopt;
    }
    if (attacksUsed < 0) {
        attacksUsed = 0;
    }
    if (allotment < 1) {
        allotment = 1;
    }

    Combatant& target = encounter.combatants[static_cast<std::size_t>(targetIndex)];
    if (!applyDamage(target, amount)) {
        return std::nullopt;
    }
    target.hp = cappedHitPoints(target.hp, target.maxHp);
    if (!isMonsterCombatant(target) && carryCharacterHitPoints(characters, target)) {
        for (const Character& character : characters) {
            if (character.id == target.sourceId) {
                target.hp = character.hp.current;
                break;
            }
        }
    }
    keepTurnInInitiative(encounter);
    if (attackerIndex < 0 || attackerIndex >= count || encounter.turnIndex != attackerIndex) {
        return 0;
    }
    ++attacksUsed;
    if (attacksUsed >= allotment) {
        advanceTurn(encounter);
        return 0;
    }
    return attacksUsed;
}

bool restoreFightUndo(Encounter& encounter, std::vector<Character>& characters, const FightUndo& undo)
{
    encounter = undo.encounter;
    if (!undo.sheet.has_value()) {
        return true;
    }
    for (Character& character : characters) {
        if (character.id == undo.sheet->id) {
            character = *undo.sheet;
            return true;
        }
    }
    return false;
}

void advanceRound(Encounter& encounter)
{
    if (encounter.combatants.empty()) {
        return;
    }
    if (encounter.round < std::numeric_limits<int>::max()) {
        ++encounter.round;
    }
    const std::vector<int> order = initiativeOrder(encounter.combatants);
    encounter.turnIndex = order.empty() ? 0 : order.front();
}

}  // namespace combat
