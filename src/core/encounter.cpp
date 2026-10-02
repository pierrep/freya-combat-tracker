#include "core/encounter.h"

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

std::string nextMonsterCopyName(const std::string& monsterName, const std::string& sourceId,
                                const std::vector<Combatant>& existing)
{
    int highest = 0;
    for (const Combatant& combatant : existing) {
        if (combatant.source != kCombatantSourceMonster || combatant.sourceId != sourceId) {
            continue;
        }
        highest = std::max(highest, copyNumber(combatant.name, monsterName));
    }
    if (highest == std::numeric_limits<int>::max()) {
        return monsterName + " " + std::to_string(highest);
    }
    return monsterName + " " + std::to_string(highest + 1);
}

Combatant makeCharacterCombatant(const Character& character, const std::string& combatantId)
{
    Combatant combatant;
    combatant.id = combatantId;
    combatant.source = kCombatantSourceCharacter;
    combatant.sourceId = character.id;
    combatant.name = character.name;
    combatant.initiative = 0;
    combatant.hp = character.hp;
    combatant.ac = character.ac;
    return combatant;
}

Combatant makeMonsterCombatant(const Monster& monster, const std::vector<Combatant>& existing,
                               const std::string& combatantId)
{
    Combatant combatant;
    combatant.id = combatantId;
    combatant.source = kCombatantSourceMonster;
    combatant.sourceId = monster.id;
    combatant.name = nextMonsterCopyName(monster.name, monster.id, existing);
    combatant.initiative = 0;
    combatant.initiativeBonus = monster.initiativeBonus;
    combatant.hp = monster.hp;
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
    const int destination = index + direction;
    if (destination < 0 || destination >= count) {
        return result;
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

void advanceTurn(Encounter& encounter)
{
    const int count = combatantCount(encounter.combatants);
    if (count <= 0) {
        return;
    }
    if (encounter.turnIndex < 0 || encounter.turnIndex >= count) {
        encounter.turnIndex = 0;
        return;
    }
    if (encounter.turnIndex + 1 >= count) {
        encounter.turnIndex = 0;
        if (encounter.round < std::numeric_limits<int>::max()) {
            ++encounter.round;
        }
        return;
    }
    ++encounter.turnIndex;
}

void retreatTurn(Encounter& encounter)
{
    const int count = combatantCount(encounter.combatants);
    if (count <= 0) {
        return;
    }
    if (encounter.turnIndex < 0 || encounter.turnIndex >= count) {
        encounter.turnIndex = 0;
        return;
    }
    if (encounter.turnIndex == 0) {
        encounter.turnIndex = count - 1;
        if (encounter.round > 1) {
            --encounter.round;
        }
        return;
    }
    --encounter.turnIndex;
}

void advanceRound(Encounter& encounter)
{
    if (encounter.combatants.empty()) {
        return;
    }
    if (encounter.round < std::numeric_limits<int>::max()) {
        ++encounter.round;
    }
    encounter.turnIndex = 0;
}

}  // namespace combat
