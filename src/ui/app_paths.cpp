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

}  // namespace combat::ui
