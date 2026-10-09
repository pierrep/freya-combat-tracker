#include "ui/app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

#include <system_error>
#include <vector>

namespace combat::ui {

std::filesystem::path defaultDataFolder()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return std::filesystem::path(folder.toStdU16String());
}

std::filesystem::path optionsFilePath()
{
    return defaultDataFolder() / "options.ini";
}

QString dataFolderOptionKey()
{
    return QStringLiteral("storage/dataFolder");
}

std::filesystem::path appDataFolder()
{
    const QSettings settings(QString::fromStdU16String(optionsFilePath().u16string()), QSettings::IniFormat);
    const QString chosen = settings.value(dataFolderOptionKey()).toString();
    if (!chosen.isEmpty()) {
        const QDir dir(chosen);
        if (dir.exists() || QDir().mkpath(chosen)) {
            return std::filesystem::path(dir.absolutePath().toStdU16String());
        }
    }
    return defaultDataFolder();
}

std::filesystem::path charactersFilePath()
{
    return appDataFolder() / "characters.json";
}

std::filesystem::path customMonstersFilePath()
{
    return appDataFolder() / "custom-monsters.json";
}

std::filesystem::path encountersFilePath()
{
    return appDataFolder() / "encounters.json";
}

std::filesystem::path srdDirectory()
{
    std::vector<std::filesystem::path> candidates;
    const QByteArray fromEnvironment = qgetenv("FREYA_SRD_DIR");
    if (!fromEnvironment.isEmpty()) {
        candidates.emplace_back(QString::fromLocal8Bit(fromEnvironment).toStdU16String());
    }
    // Prefer the catalog this binary was built against. A copy installed beside
    // the executable (build/share) otherwise hides later edits to data/srd, and
    // Cloud of Insects' concentration rider never reaches the fight.
#ifdef COMBAT_TRACKER_SRD_DIR
    candidates.emplace_back(COMBAT_TRACKER_SRD_DIR);
#endif
    const std::filesystem::path executable(QCoreApplication::applicationDirPath().toStdU16String());
    candidates.push_back(executable / ".." / "share" / "freya-combat-tracker" / "srd");
    candidates.push_back(executable / "srd");
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate / "monsters.json", ec)) {
            return candidate.lexically_normal();
        }
    }
    return candidates.empty() ? std::filesystem::path{} : candidates.back();
}

std::filesystem::path srdMonstersFilePath()
{
    return srdDirectory() / "monsters.json";
}

std::filesystem::path srdSpellsFilePath()
{
    return srdDirectory() / "spells.json";
}

std::filesystem::path srdConditionsFilePath()
{
    return srdDirectory() / "conditions.json";
}

std::filesystem::path srdSpeciesFilePath()
{
    return srdDirectory() / "species.json";
}

std::filesystem::path srdAttributionFilePath()
{
    return srdDirectory() / "ATTRIBUTION.txt";
}

}  // namespace combat::ui
