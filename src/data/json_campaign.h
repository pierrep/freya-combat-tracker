#pragma once

#include "core/campaign.h"

#include <filesystem>
#include <string>

namespace combat {

inline constexpr int kCampaignSchemaVersion = 1;

// Reads and writes campaign.json: the parties and the adventures. A missing
// file loads as none of either. A file that cannot be parsed throws
// CampaignStoreError and is left as it is. Saving writes a temporary file and
// renames it over the real one.
class JsonCampaignStore final : public CampaignStore {
public:
    explicit JsonCampaignStore(std::filesystem::path path);

    Campaign load() override;
    void save(const Campaign& campaign) override;

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

Campaign parseCampaignDocument(const std::string& text);
std::string serializeCampaignDocument(const Campaign& campaign);

}  // namespace combat
