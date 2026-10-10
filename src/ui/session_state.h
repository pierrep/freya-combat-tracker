#pragma once

// What the app remembers between runs about where you were: the encounter
// last open on the Dashboard or in Encounter Builder, and the adventure chosen. Kept in the options
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

// The adventure chosen on the Dashboard or in Encounter Builder (the same
// choice on both): kAllAdventures, kNoAdventure or an adventure's id. All
// when nothing was chosen.
inline QString readLastAdventure(const QString& file)
{
    if (file.isEmpty()) {
        return {};
    }
    const QSettings settings(file, QSettings::IniFormat);
    const QString value = settings.value(QStringLiteral("session/lastAdventure")).toString();
    return value == QStringLiteral("*") ? QString() : value;
}

inline void writeLastAdventure(const QString& file, const QString& choice)
{
    if (file.isEmpty()) {
        return;
    }
    QSettings settings(file, QSettings::IniFormat);
    const QString value = choice.isEmpty() ? QStringLiteral("*") : choice;
    if (settings.value(QStringLiteral("session/lastAdventure")).toString() != value) {
        settings.setValue(QStringLiteral("session/lastAdventure"), value);
    }
}

}  // namespace combat::ui
