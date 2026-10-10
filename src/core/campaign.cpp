#include "core/campaign.h"

#include <algorithm>

namespace combat {

const Party* findParty(const Campaign& campaign, const std::string& id)
{
    for (const Party& party : campaign.parties) {
        if (!id.empty() && party.id == id) {
            return &party;
        }
    }
    return nullptr;
}

Party* findParty(Campaign& campaign, const std::string& id)
{
    return const_cast<Party*>(findParty(static_cast<const Campaign&>(campaign), id));
}

const Adventure* findAdventure(const Campaign& campaign, const std::string& id)
{
    for (const Adventure& adventure : campaign.adventures) {
        if (!id.empty() && adventure.id == id) {
            return &adventure;
        }
    }
    return nullptr;
}

Adventure* findAdventure(Campaign& campaign, const std::string& id)
{
    return const_cast<Adventure*>(findAdventure(static_cast<const Campaign&>(campaign), id));
}

std::vector<const Character*> partyMembers(const Party& party, const std::vector<Character>& characters)
{
    std::vector<const Character*> members;
    for (const std::string& id : party.characterIds) {
        for (const Character& character : characters) {
            if (character.id == id) {
                members.push_back(&character);
                break;
            }
        }
    }
    return members;
}

bool applyParty(Encounter& encounter, const Party& party, const std::vector<Character>& characters,
                const std::function<std::string()>& newId)
{
    const auto inParty = [&party](const std::string& characterId) {
        return std::find(party.characterIds.begin(), party.characterIds.end(), characterId) != party.characterIds.end();
    };
    bool changed = encounter.partyId != party.id;
    if (changed) {
        encounter.partyLeftOut.clear();  // a different party: none of its members left out yet
    }
    encounter.partyId = party.id;
    const auto leftOut = [&encounter](const std::string& characterId) {
        return std::find(encounter.partyLeftOut.begin(), encounter.partyLeftOut.end(), characterId) !=
               encounter.partyLeftOut.end();
    };
    // Left the party (or the rows of a party it had before): the row goes. A
    // guest who joined it is a party row now.
    for (int i = static_cast<int>(encounter.combatants.size()) - 1; i >= 0; --i) {
        Combatant& row = encounter.combatants[static_cast<std::size_t>(i)];
        if (!isCharacterCombatant(row)) {
            continue;
        }
        if (row.partyMember && !inParty(row.sourceId)) {
            encounter.turnIndex = removeCombatant(encounter.combatants, i, encounter.turnIndex);
            changed = true;
        } else if (!row.partyMember && inParty(row.sourceId)) {
            row.partyMember = true;
            changed = true;
        }
    }
    // Joined it: a row, at the sheet's values.
    for (const Character* member : partyMembers(party, characters)) {
        if (encounterHasCharacter(encounter, member->id) || leftOut(member->id)) {
            continue;
        }
        Combatant row = makeCharacterCombatant(*member, newId());
        row.partyMember = true;
        encounter.combatants.push_back(std::move(row));
        changed = true;
    }
    if (changed) {
        encounter.turnIndex = sortByInitiative(encounter.combatants, encounter.turnIndex);
    }
    return changed;
}

bool syncPartyRows(Encounter& encounter, const Campaign& campaign, const std::vector<Character>& characters,
                   const std::function<std::string()>& newId)
{
    if (encounter.started || encounter.partyId.empty()) {
        return false;
    }
    const Party* party = findParty(campaign, encounter.partyId);
    if (party == nullptr) {
        return false;  // forgetParty clears the link when a party is deleted
    }
    return applyParty(encounter, *party, characters, newId);
}

bool syncPartyRows(std::vector<Encounter>& encounters, const Campaign& campaign,
                   const std::vector<Character>& characters, const std::function<std::string()>& newId)
{
    bool changed = false;
    for (Encounter& encounter : encounters) {
        changed = syncPartyRows(encounter, campaign, characters, newId) || changed;
    }
    return changed;
}

void removePartyRows(Encounter& encounter)
{
    for (int i = static_cast<int>(encounter.combatants.size()) - 1; i >= 0; --i) {
        if (encounter.combatants[static_cast<std::size_t>(i)].partyMember) {
            encounter.turnIndex = removeCombatant(encounter.combatants, i, encounter.turnIndex);
        }
    }
    encounter.partyId.clear();
    encounter.partyLeftOut.clear();
}

int forgetParty(std::vector<Encounter>& encounters, const std::string& partyId)
{
    int changed = 0;
    for (Encounter& encounter : encounters) {
        if (partyId.empty() || encounter.partyId != partyId) {
            continue;
        }
        encounter.partyId.clear();
        encounter.partyLeftOut.clear();
        for (Combatant& row : encounter.combatants) {
            row.partyMember = false;
        }
        ++changed;
    }
    return changed;
}

int forgetAdventure(std::vector<Encounter>& encounters, const std::string& adventureId)
{
    int changed = 0;
    for (Encounter& encounter : encounters) {
        if (!adventureId.empty() && encounter.adventureId == adventureId) {
            encounter.adventureId.clear();
            ++changed;
        }
    }
    return changed;
}

bool inAdventureChoice(const Encounter& encounter, const std::string& choice)
{
    if (choice == kAllAdventures) {
        return true;
    }
    if (choice == kNoAdventure) {
        return encounter.adventureId.empty();
    }
    return encounter.adventureId == choice;
}

}  // namespace combat
