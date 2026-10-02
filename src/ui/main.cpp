#include "core/monster_catalog.h"
#include "data/json_character_store.h"
#include "data/json_encounters.h"
#include "data/json_monsters.h"
#include "ui/app_paths.h"
#include "ui/main_window.h"

#include <QApplication>

#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationDisplayName(QStringLiteral("Freya Combat Tracker"));

    // No organization name, so AppDataLocation is <data root>/<application name>,
    // matching the folder names in the plan.
#if defined(Q_OS_LINUX)
    QApplication::setApplicationName(QStringLiteral("combat-tracker"));
#else
    QApplication::setApplicationName(QStringLiteral("CombatTracker"));
#endif

    combat::JsonCharacterStore store(combat::ui::charactersFilePath());

    std::vector<combat::Monster> srdMonsters;
    QString catalogError;
    try {
        srdMonsters = combat::loadSrdMonsters(combat::ui::srdMonstersFilePath());
    } catch (const combat::MonsterDataError& error) {
        catalogError = QString::fromStdString(error.what());
    }
    combat::MergedMonsterCatalog catalog(std::move(srdMonsters));
    combat::JsonCustomMonsterStore customStore(combat::ui::customMonstersFilePath(), catalog.srdIds());

    QString attribution;
    {
        std::ifstream in(combat::ui::srdAttributionFilePath(), std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        if (in) {
            attribution = QString::fromStdString(buffer.str()).trimmed();
        }
    }

    combat::JsonEncounterStore encounters(combat::ui::encountersFilePath());

    combat::ui::MainWindow window(store, catalog, customStore, encounters, attribution, catalogError);
    window.resize(960, 640);
    window.show();

    return QApplication::exec();
}
