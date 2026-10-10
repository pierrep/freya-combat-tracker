#include "ui/theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QtGlobal>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QStyle>
#include <QStringList>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QVBoxLayout>

namespace combat::ui {

namespace {

QString hex(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

// Style sheets take images by path, so the drop-down chevron is drawn once at
// start-up into a folder that lives as long as the app.
QString chevronFile(const QColor& color, const QString& name)
{
    static QTemporaryDir folder;
    if (!folder.isValid()) {
        return {};
    }
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawPolyline(QPolygonF({QPointF(8, 15), QPointF(20, 27), QPointF(32, 15)}));
    painter.end();
    const QString path = folder.filePath(name + QStringLiteral(".png"));
    pixmap.save(path);
    return path;
}

// The tick in a checked box, white on the accent.
QString tickFile()
{
    static QTemporaryDir folder;
    if (!folder.isValid()) {
        return {};
    }
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::white, 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolyline(QPolygonF({QPointF(9, 21), QPointF(17, 29), QPointF(31, 12)}));
    painter.end();
    const QString path = folder.filePath(QStringLiteral("tick.png"));
    pixmap.save(path);
    return path;
}

void repolish(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

bool g_dark = false;
// The desktop's own window colour, read before the app's palette replaces it.
QColor g_systemWindow;

}  // namespace

void setDarkMode(bool on)
{
    g_dark = on;
    using namespace palette;
    if (on) {
        window = QColor(0x15, 0x17, 0x1E);
        surface = QColor(0x1E, 0x21, 0x29);
        ink = QColor(0xE6, 0xE8, 0xEE);
        muted = QColor(0x9A, 0xA1, 0xAD);
        line = QColor(0x30, 0x35, 0x40);
        accent = QColor(0x6A, 0x66, 0xD6);
        accentSoft = QColor(0x2E, 0x2C, 0x52);
        healthy = QColor(0x4C, 0xC3, 0x8A);
        bloodied = QColor(0xE5, 0xA2, 0x3C);
        critical = QColor(0xE2, 0x61, 0x5A);
        tile = QColor(0x25, 0x29, 0x32);
        alert = QColor(0x3A, 0x22, 0x24);
        disabledText = QColor(0x5C, 0x63, 0x70);
        disabledField = QColor(0x1A, 0x1D, 0x24);
        accentHover = QColor(0x7C, 0x78, 0xE0);
        accentDisabled = QColor(0x3B, 0x3A, 0x60);
        scrollHandle = QColor(0x3A, 0x40, 0x4B);
        chevronOff = QColor(0x4A, 0x50, 0x5A);
    } else {
        window = QColor(0xEE, 0xF1, 0xF4);
        surface = QColor(0xFF, 0xFF, 0xFF);
        ink = QColor(0x1E, 0x25, 0x30);
        muted = QColor(0x6B, 0x74, 0x82);
        line = QColor(0xDD, 0xE2, 0xE8);
        accent = QColor(0x4A, 0x47, 0xA3);
        accentSoft = QColor(0xE6, 0xE5, 0xF5);
        healthy = QColor(0x3B, 0x9B, 0x6B);
        bloodied = QColor(0xD9, 0x8E, 0x2B);
        critical = QColor(0xC2, 0x41, 0x3A);
        tile = QColor(0xF6, 0xF7, 0xF9);
        alert = QColor(0xFB, 0xED, 0xEC);
        disabledText = QColor(0xA4, 0xAB, 0xB5);
        disabledField = QColor(0xF7, 0xF8, 0xFA);
        accentHover = QColor(0x3E, 0x3B, 0x8E);
        accentDisabled = QColor(0xB8, 0xB7, 0xDA);
        scrollHandle = QColor(0xC9, 0xCE, 0xD6);
        chevronOff = QColor(0xC4, 0xC9, 0xD0);
    }
}

bool darkMode()
{
    return g_dark;
}

bool systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark:
        return true;
    case Qt::ColorScheme::Light:
        return false;
    default:
        break;
    }
#endif
    return g_systemWindow.isValid() && g_systemWindow.lightness() < 128;
}

void applyTheme(QApplication& app)
{
    if (!g_systemWindow.isValid()) {
        g_systemWindow = app.palette().color(QPalette::Window);
    }
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette colors;
    colors.setColor(QPalette::Window, palette::window);
    colors.setColor(QPalette::Base, palette::surface);
    colors.setColor(QPalette::AlternateBase, palette::tile);
    colors.setColor(QPalette::Text, palette::ink);
    colors.setColor(QPalette::WindowText, palette::ink);
    colors.setColor(QPalette::ButtonText, palette::ink);
    colors.setColor(QPalette::Button, palette::surface);
    colors.setColor(QPalette::Highlight, palette::accent);
    colors.setColor(QPalette::HighlightedText, Qt::white);
    colors.setColor(QPalette::Light, palette::surface);
    colors.setColor(QPalette::Midlight, palette::line);
    colors.setColor(QPalette::Shadow, palette::window.darker(150));
    colors.setColor(QPalette::ToolTipBase, palette::ink);
    colors.setColor(QPalette::ToolTipText, palette::surface);
    colors.setColor(QPalette::Link, palette::accent);
    colors.setColor(QPalette::PlaceholderText, palette::muted);
    colors.setColor(QPalette::Mid, palette::line);
    colors.setColor(QPalette::Dark, palette::muted);
    colors.setColor(QPalette::Disabled, QPalette::Text, palette::disabledText);
    colors.setColor(QPalette::Disabled, QPalette::ButtonText, palette::disabledText);
    colors.setColor(QPalette::Disabled, QPalette::WindowText, palette::disabledText);
    app.setPalette(colors);

    QFont font = app.font();
    if (font.pointSizeF() > 0 && font.pointSizeF() < 10.0) {
        font.setPointSizeF(10.0);
    }
    app.setFont(font);

    // Pills: low, moderate, high (background, text).
    const QStringList pill = g_dark
        ? QStringList{QStringLiteral("#1F3A2E"), QStringLiteral("#7FD8A8"), QStringLiteral("#3F3220"),
                      QStringLiteral("#F0C070"), QStringLiteral("#45262A"), QStringLiteral("#F08A84")}
        : QStringList{QStringLiteral("#E3F2EA"), QStringLiteral("#2C7A53"), QStringLiteral("#FBEFD9"),
                      QStringLiteral("#9A6212"), QStringLiteral("#FBE4E2"), QStringLiteral("#A8322C")};
    // A thin rule in the accent, faded into the card.
    const auto mix = [](int a, int b) { return (a * 22 + b * 78) / 100; };
    const QColor sectionRule(mix(palette::accent.red(), palette::surface.red()),
                             mix(palette::accent.green(), palette::surface.green()),
                             mix(palette::accent.blue(), palette::surface.blue()));
    const QString chevronSuffix = g_dark ? QStringLiteral("-dark") : QString();
    const QString sheet = QStringLiteral(R"(
QMainWindow, QStackedWidget#pages > QWidget { background: %1; }
QTabWidget > QStackedWidget, QTabWidget > QStackedWidget > QWidget { background: transparent; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }

QToolButton#pageMenu { border: none; background: transparent; border-radius: 8px; padding: 2px 6px;
                       font-size: 20pt; font-weight: 600; color: %3; }
QToolButton#pageMenu:hover, QToolButton#pageMenu:pressed { background: %7; }
QMenu#pageMenuItems::item { padding: 8px 28px 8px 14px; }
QMenu#pageMenuItems::item:checked { color: %6; font-weight: 600; }
QMenu#pageMenuItems::indicator { width: 0; height: 0; }
QListWidget[compact="true"]::item, QTreeWidget[compact="true"]::item { padding: 2px 4px; }

QFrame[card="true"] { background: %2; border: 1px solid %5; border-radius: 10px; }
QLabel[role="title"] { font-size: 20pt; font-weight: 600; color: %3; }
QLabel[role="heading"] { font-weight: 600; color: %3; padding-top: 2px; }
QLabel[role="muted"] { color: %4; }
QLabel[summary="true"] { font-style: italic; }
QLabel[role="banner-error"] { color: %8; background: %11; border-radius: 8px; padding: 8px 12px; }
QFrame[role="alert"] { background: %11; border-radius: 8px; }
QFrame[role="alert"] QPushButton[variant="quiet"] { color: %8; }
QLabel[role="pill"] { background: %7; color: %6; border-radius: 11px; padding: 3px 12px; font-weight: 600; }
QLabel[role="pill"][level="low"] { background: %18; color: %19; }
QLabel[role="pill"][level="moderate"] { background: %20; color: %21; }
QLabel[role="pill"][level="high"] { background: %22; color: %23; }
QFrame[card="true"] QListWidget[inset="true"], QListWidget[inset="true"] { border: 1px solid %5; border-radius: 8px; }
QFrame[role="tile"] { background: %12; border-radius: 10px; }
QLabel[role="accent"] { color: %6; }
QFrame[role="strip"] { background: %12; border-radius: 8px; }
QLabel[role="banner-note"] { color: %3; background: %7; border-radius: 8px; padding: 8px 12px; }
QFrame#sectionRule { background: %24; border: none; border-radius: 1px; }
QLabel#initiativeEntryNote { color: %25; }
QFrame#promptPanel { background: %7; border: 1px solid %6; border-radius: 10px; }


QPushButton {
    background: %2; border: 1px solid %5; border-radius: 7px; padding: 5px 12px; color: %3; min-height: 18px;
}
QPushButton:hover { border-color: %4; }
QPushButton:pressed { background: %1; }
QPushButton:disabled { color: %13; border-color: %5; background: %2; }
QPushButton[variant="primary"] { background: %6; border-color: %6; color: white; font-weight: 600; }
QPushButton[variant="primary"]:hover { background: %14; }
QPushButton[variant="primary"]:disabled { background: %15; border-color: %15; color: white; }
/* An ability that can be used, in the same style as Next turn. Disabled stays the plain button. */
QPushButton[variant="ready"] { background: %6; border-color: %6; color: white; font-weight: 600; }
QPushButton[variant="ready"]:hover { background: %14; }
QPushButton[variant="ready"]:disabled { color: %13; border-color: %5; background: %2; font-weight: 400; }
QWidget#actionButtons QPushButton { padding-left: 4px; padding-right: 4px; }
QPushButton[variant="danger"] { color: %8; }
QPushButton[variant="danger"]:hover { border-color: %8; }
QPushButton[variant="quiet"] { border: none; background: transparent; color: %6; padding: 4px 6px; }
QPushButton[variant="quiet"]:hover { text-decoration: underline; }
QPushButton[variant="quiet"]:disabled { color: %13; text-decoration: none; }
QToolButton { border: 1px solid %5; border-radius: 7px; padding: 5px 8px; background: %2; }
QToolButton::menu-indicator { image: none; width: 0; }

QLineEdit, QSpinBox, QComboBox, QPlainTextEdit {
    background: %2; border: 1px solid %5; border-radius: 7px; padding: 4px 8px; color: %3;
    selection-background-color: %6;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border: 1px solid %6; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: %13; background: %16; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; border: none; }
QSpinBox::up-arrow, QSpinBox::down-arrow { image: none; width: 0; height: 0; }
QComboBox { padding-right: 24px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; border: none; width: 22px; }
QComboBox::down-arrow { image: url(%9); width: 10px; height: 10px; }
QComboBox::down-arrow:disabled { image: url(%10); }
QComboBox QAbstractItemView { border: 1px solid %5; background: %2; selection-background-color: %7; selection-color: %3; }

QTreeWidget, QListWidget { background: %2; border: 1px solid %5; border-radius: 8px; outline: 0; }
QFrame[card="true"] QTreeWidget, QFrame[card="true"] QListWidget { border: none; }
/* The same (empty) border in every state, so picking a row never moves its text. */
QTreeWidget::item, QListWidget::item { padding: 5px 4px; border: 0px solid transparent; }
QTreeWidget::item:selected, QListWidget::item:selected { background: %7; color: %3; border: 0px solid transparent; }
QHeaderView::section {
    background: %2; border: none; border-bottom: 1px solid %5; padding: 6px 4px; color: %4; font-weight: 500;
}

QTabWidget::pane { border: none; border-top: 1px solid %5; top: -1px; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; border: none; padding: 8px 14px; color: %4; }
QTabBar::tab:hover { color: %3; }
QTabBar::tab:selected { color: %6; border-bottom: 2px solid %6; font-weight: 600; }

QCheckBox { spacing: 6px; }
QToolTip { background: %3; color: %2; border: none; padding: 6px 8px; border-radius: 6px; }
QMenu { background: %2; border: 1px solid %5; padding: 6px; border-radius: 8px; }
QMenu::item { padding: 6px 18px; border-radius: 6px; }
QMenu::item:selected { background: %7; color: %3; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %17; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %17; border-radius: 4px; min-width: 30px; }
)")
                              .arg(hex(palette::window), hex(palette::surface), hex(palette::ink),
                                   hex(palette::muted), hex(palette::line), hex(palette::accent),
                                   hex(palette::accentSoft), hex(palette::critical))
                              .arg(chevronFile(palette::muted, QStringLiteral("chevron") + chevronSuffix),
                                   chevronFile(palette::chevronOff, QStringLiteral("chevron-off") + chevronSuffix))
                              .arg(hex(palette::alert), hex(palette::tile), hex(palette::disabledText),
                                   hex(palette::accentHover), hex(palette::accentDisabled),
                                   hex(palette::disabledField), hex(palette::scrollHandle))
                              .arg(pill[0], pill[1], pill[2], pill[3], pill[4], pill[5])
                              .arg(hex(sectionRule), hex(palette::bloodied));
    // Fusion's own boxes all but vanish on the dark panels, so dark mode
    // draws them: an outlined box, filled with the accent when ticked.
    const QString darkBoxes = g_dark ? QStringLiteral(R"(
QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid %1; border-radius: 3px; background: %2; }
QCheckBox::indicator:hover { border-color: %3; }
QCheckBox::indicator:checked { background: %3; border-color: %3; image: url(%4); }
QCheckBox::indicator:disabled { border-color: %5; background: %6; }
QCheckBox::indicator:checked:disabled { background: %7; border-color: %7; }
)").arg(hex(palette::muted), hex(palette::surface), hex(palette::accent), tickFile(), hex(palette::line),
        hex(palette::disabledField), hex(palette::accentDisabled))
                                     : QString();
    app.setStyleSheet(sheet + darkBoxes);
}

QFrame* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setProperty("card", true);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    return card;
}

QLabel* makeTitle(const QString& text)
{
    auto* label = new QLabel(text);
    label->setProperty("role", QStringLiteral("title"));
    return label;
}

QLabel* makeHeading(const QString& text)
{
    auto* label = new QLabel(text);
    label->setProperty("role", QStringLiteral("heading"));
    label->setWordWrap(true);
    label->setIndent(0);
    return label;
}

QLabel* makeMuted(const QString& text)
{
    auto* label = new QLabel(text);
    label->setProperty("role", QStringLiteral("muted"));
    label->setWordWrap(true);
    return label;
}

void makePrimary(QPushButton* button)
{
    button->setProperty("variant", QStringLiteral("primary"));
    repolish(button);
}

void makeDanger(QPushButton* button)
{
    button->setProperty("variant", QStringLiteral("danger"));
    repolish(button);
}

void makeQuiet(QPushButton* button)
{
    button->setProperty("variant", QStringLiteral("quiet"));
    button->setCursor(Qt::PointingHandCursor);
    repolish(button);
}

QColor healthColor(int current, int maximum)
{
    if (maximum <= 0) {
        return palette::muted;
    }
    if (current * 4 <= maximum) {
        return palette::critical;
    }
    if (current * 2 <= maximum) {
        return palette::bloodied;
    }
    return palette::healthy;
}

}  // namespace combat::ui
