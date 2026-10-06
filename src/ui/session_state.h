#pragma once

// What the app remembers between runs about where you were: the encounter
// last open on the Dashboard or in Encounter Builder. Kept in the options
// file's [session] group; with no file, nothing is remembered.

#include <QSettings>
#include <QString>

namespace combat::ui {

inline QString readLastEncounter(const QString& file)
{
    if (file.isEmpty()) {
        return {};
    }
    const QSettings settings(file, QSettings::IniFormat);
    return settings.value(QStringLiteral("session/lastEncounter")).toString();
}

inline void writeLastEncounter(const QString& file, const QString& encounterId)
{
    if (file.isEmpty() || encounterId.isEmpty()) {
        return;
    }
    QSettings settings(file, QSettings::IniFormat);
    if (settings.value(QStringLiteral("session/lastEncounter")).toString() != encounterId) {
        settings.setValue(QStringLiteral("session/lastEncounter"), encounterId);
    }
}

}  // namespace combat::ui
