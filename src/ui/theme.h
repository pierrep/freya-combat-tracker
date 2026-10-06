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
namespace palette {
inline const QColor window{0xEE, 0xF1, 0xF4};
inline const QColor surface{0xFF, 0xFF, 0xFF};
inline const QColor ink{0x1E, 0x25, 0x30};
inline const QColor muted{0x6B, 0x74, 0x82};
inline const QColor line{0xDD, 0xE2, 0xE8};
inline const QColor accent{0x4A, 0x47, 0xA3};
inline const QColor accentSoft{0xE6, 0xE5, 0xF5};
inline const QColor healthy{0x3B, 0x9B, 0x6B};
inline const QColor bloodied{0xD9, 0x8E, 0x2B};
inline const QColor critical{0xC2, 0x41, 0x3A};
}  // namespace palette

// Fusion style plus the app style sheet.
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
