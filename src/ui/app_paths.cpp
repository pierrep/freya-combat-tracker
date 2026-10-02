#include "ui/app_paths.h"

#include <QStandardPaths>
#include <QString>

namespace combat::ui {

std::filesystem::path appDataFolder()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return std::filesystem::path(folder.toStdU16String());
}

std::filesystem::path charactersFilePath()
{
    return appDataFolder() / "characters.json";
}

std::filesystem::path customMonstersFilePath()
{
    return appDataFolder() / "custom-monsters.json";
}

std::filesystem::path srdDirectory()
{
#ifdef COMBAT_TRACKER_SRD_DIR
    return std::filesystem::path{COMBAT_TRACKER_SRD_DIR};
#else
    return {};
#endif
}

std::filesystem::path srdMonstersFilePath()
{
    return srdDirectory() / "monsters.json";
}

std::filesystem::path srdAttributionFilePath()
{
    return srdDirectory() / "ATTRIBUTION.txt";
}

}  // namespace combat::ui
