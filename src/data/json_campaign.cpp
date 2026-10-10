#include "data/json_campaign.h"

#include "data/json_util.h"

#include <nlohmann/json.hpp>

#include <unordered_set>

namespace combat {

namespace {

using json = nlohmann::json;
using Error = CampaignStoreError;

void rejectDuplicates(const Campaign& campaign)
{
    std::unordered_set<std::string> ids;
    for (const Party& party : campaign.parties) {
        if (party.id.empty() || !ids.insert(party.id).second) {
            throw Error("A party has no id, or shares one: \"" + party.id + "\".");
        }
    }
    for (const Adventure& adventure : campaign.adventures) {
        if (adventure.id.empty() || !ids.insert(adventure.id).second) {
            throw Error("An adventure has no id, or shares one: \"" + adventure.id + "\".");
        }
    }
}

}  // namespace

Campaign parseCampaignDocument(const std::string& text)
{
    const json document = json_util::parseDocument<Error>(text, "campaign");
    const std::string context = "Campaign file";
    json_util::requireObject<Error>(document, context);
    const int version = json_util::readInt<Error>(document, "schemaVersion", context);
    if (version < 1 || version > kCampaignSchemaVersion) {
        throw Error(context + ": unknown schemaVersion " + std::to_string(version) + ".");
    }
    Campaign campaign;
    if (document.contains("parties")) {
        const json& parties = json_util::requireArray<Error>(document, "parties", context);
        for (std::size_t i = 0; i < parties.size(); ++i) {
            const std::string rowContext = "Party " + std::to_string(i + 1);
            json_util::requireObject<Error>(parties[i], rowContext);
            Party party;
            party.id = json_util::readString<Error>(parties[i], "id", rowContext);
            party.name = json_util::readString<Error>(parties[i], "name", rowContext);
            if (parties[i].contains("characterIds")) {
                party.characterIds = json_util::readStringList<Error>(parties[i], "characterIds", rowContext);
            }
            campaign.parties.push_back(std::move(party));
        }
    }
    if (document.contains("adventures")) {
        const json& adventures = json_util::requireArray<Error>(document, "adventures", context);
        for (std::size_t i = 0; i < adventures.size(); ++i) {
            const std::string rowContext = "Adventure " + std::to_string(i + 1);
            json_util::requireObject<Error>(adventures[i], rowContext);
            Adventure adventure;
            adventure.id = json_util::readString<Error>(adventures[i], "id", rowContext);
            adventure.name = json_util::readString<Error>(adventures[i], "name", rowContext);
            adventure.partyId = json_util::readOptionalString<Error>(adventures[i], "partyId", rowContext);
            campaign.adventures.push_back(std::move(adventure));
        }
    }
    rejectDuplicates(campaign);
    return campaign;
}

std::string serializeCampaignDocument(const Campaign& campaign)
{
    json parties = json::array();
    for (const Party& party : campaign.parties) {
        parties.push_back(json{{"id", party.id}, {"name", party.name}, {"characterIds", party.characterIds}});
    }
    json adventures = json::array();
    for (const Adventure& adventure : campaign.adventures) {
        json row{{"id", adventure.id}, {"name", adventure.name}};
        if (!adventure.partyId.empty()) {
            row["partyId"] = adventure.partyId;
        }
        adventures.push_back(std::move(row));
    }
    const json document{
        {"schemaVersion", kCampaignSchemaVersion},
        {"parties", std::move(parties)},
        {"adventures", std::move(adventures)},
    };
    return document.dump(2) + "\n";
}

JsonCampaignStore::JsonCampaignStore(std::filesystem::path path) : m_path(std::move(path)) {}

Campaign JsonCampaignStore::load()
{
    const std::optional<std::string> text = json_util::readTextFile<Error>(m_path, true);
    if (!text.has_value()) {
        return {};
    }
    return parseCampaignDocument(*text);
}

void JsonCampaignStore::save(const Campaign& campaign)
{
    rejectDuplicates(campaign);
    json_util::writeFileAtomically<Error>(m_path, serializeCampaignDocument(campaign));
}

}  // namespace combat
