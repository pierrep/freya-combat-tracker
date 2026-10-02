#include "data/json_character_store.h"
#include "ui/app_paths.h"
#include "ui/main_window.h"

#include <QApplication>

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

    combat::ui::MainWindow window(store);
    window.resize(960, 640);
    window.show();

    return QApplication::exec();
}
