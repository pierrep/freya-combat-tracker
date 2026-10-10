#pragma once

// Parties and adventures. A party is a named group of characters; adding it
// to an encounter adds every member. An adventure is a named group of
// encounters (the Dashboard shows one adventure's at a time), and may say
// which party new encounters in it start with.

#include "core/character.h"
#include "core/encounter.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace combat {

struct Party {
    std::string id;
    std::string name;
    std::vector<std::string> characterIds;

    bool operator==(const Party&) const = default;
};

struct Adventure {
    std::string id;
    std::string name;
    // The party each new encounter in it starts with; empty for none.
    std::string partyId;

    bool operator==(const Adventure&) const = default;
};

struct Campaign {
    std::vector<Party> parties;
    std::vector<Adventure> adventures;

    bool operator==(const Campaign&) const = default;
};

class CampaignStoreError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Loads and saves the parties and adventures. Implementations throw
// CampaignStoreError when the file cannot be read or written.
class CampaignStore {
public:
    virtual ~CampaignStore() = default;
    virtual Campaign load() = 0;
    virtual void save(const Campaign& campaign) = 0;
};

// Kept in memory only (when there is no file to keep it in).
class MemoryCampaignStore final : public CampaignStore {
public:
    Campaign load() override { return m_campaign; }
    void save(const Campaign& campaign) override { m_campaign = campaign; }

private:
    Campaign m_campaign;
};

const Party* findParty(const Campaign& campaign, const std::string& id);
Party* findParty(Campaign& campaign, const std::string& id);
const Adventure* findAdventure(const Campaign& campaign, const std::string& id);
Adventure* findAdventure(Campaign& campaign, const std::string& id);

// The party's members that are on the roster, in the party's order.
std::vector<const Character*> partyMembers(const Party& party, const std::vector<Character>& characters);

// Brings an encounter with a party in line with it, while the encounter has
// not started (once combat starts its characters stay as they are): a row for
// each member it lacks, at the sheet's values; no row for a character that has
// left the party (one added on its own, a guest, stays). A guest who joins the
// party becomes a party row rather than a second one. Returns true when it
// changed anything.
bool syncPartyRows(Encounter& encounter, const Campaign& campaign, const std::vector<Character>& characters,
                   const std::function<std::string()>& newId);
// Puts the party in the encounter now, started or not: partyId set, a row for
// each member it lacks (but those left out of it by hand), guests who are
// members marked as the party's, and the rows of a party it had before taken
// out. Returns true when anything changed.
bool applyParty(Encounter& encounter, const Party& party, const std::vector<Character>& characters,
                const std::function<std::string()>& newId);
// The same for every encounter.
bool syncPartyRows(std::vector<Encounter>& encounters, const Campaign& campaign,
                   const std::vector<Character>& characters, const std::function<std::string()>& newId);

// Takes the party out of the encounter: its rows go, guests stay.
void removePartyRows(Encounter& encounter);

// After a party is deleted: encounters that had it keep their characters, as
// guests. After an adventure is deleted: its encounters have no adventure.
// Return how many encounters changed.
int forgetParty(std::vector<Encounter>& encounters, const std::string& partyId);
int forgetAdventure(std::vector<Encounter>& encounters, const std::string& adventureId);

// What the Dashboard and the Encounter Builder list for an adventure choice:
// kAllAdventures, kNoAdventure, or an adventure's id.
inline constexpr const char* kAllAdventures = "";
inline constexpr const char* kNoAdventure = "-";
bool inAdventureChoice(const Encounter& encounter, const std::string& choice);

}  // namespace combat
