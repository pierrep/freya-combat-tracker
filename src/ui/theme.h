#pragma once

#include <QColor>
#include <QString>

class QApplication;
class QFrame;
class QLabel;
class QLayout;
class QPushButton;
class QWidget;

namespace combat::ui {

// One quiet palette for the whole app. Health colours are the only loud ones.
// setDarkMode switches every colour between the light set and the dark one.
namespace palette {
inline QColor window{0xEE, 0xF1, 0xF4};
inline QColor surface{0xFF, 0xFF, 0xFF};
inline QColor ink{0x1E, 0x25, 0x30};
inline QColor muted{0x6B, 0x74, 0x82};
inline QColor line{0xDD, 0xE2, 0xE8};
inline QColor accent{0x4A, 0x47, 0xA3};
inline QColor accentSoft{0xE6, 0xE5, 0xF5};
inline QColor healthy{0x3B, 0x9B, 0x6B};
inline QColor bloodied{0xD9, 0x8E, 0x2B};
inline QColor critical{0xC2, 0x41, 0x3A};
// Quieter surfaces and states.
inline QColor tile{0xF6, 0xF7, 0xF9};          // a panel inside a card
inline QColor alert{0xFB, 0xED, 0xEC};         // the death-save strip, error banners
inline QColor disabledText{0xA4, 0xAB, 0xB5};
inline QColor disabledField{0xF7, 0xF8, 0xFA};
inline QColor accentHover{0x3E, 0x3B, 0x8E};
inline QColor accentDisabled{0xB8, 0xB7, 0xDA};
inline QColor scrollHandle{0xC9, 0xCE, 0xD6};
inline QColor chevronOff{0xC4, 0xC9, 0xD0};
}  // namespace palette

// Dark or light (the default). Fills the palette; applyTheme then styles the
// app with it.
void setDarkMode(bool on);
bool darkMode();

// Whether the computer is set to dark. Qt 6.5 and later ask the desktop
// (and follow its changes); older Qt goes by the system colours the app
// started with.
bool systemPrefersDark();

// Fusion style plus the app style sheet, in the palette's current colours.
// Called again after setDarkMode to restyle a running app.
void applyTheme(QApplication& app);

// A white rounded panel. Children go in its layout.
QFrame* makeCard(QWidget* parent = nullptr);
// Page heading, section heading, and secondary text.
QLabel* makeTitle(const QString& text);
QLabel* makeHeading(const QString& text);
QLabel* makeMuted(const QString& text);
// The main action on a panel.
void makePrimary(QPushButton* button);
// A button that removes or harms.
void makeDanger(QPushButton* button);
// A small text-only button.
void makeQuiet(QPushButton* button);

// Colour for a health fraction: green, amber at or below half (Bloodied),
// red at or below a quarter.
QColor healthColor(int current, int maximum);

}  // namespace combat::ui
