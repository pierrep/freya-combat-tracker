#include "ui/theme.h"

#include <QApplication>
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

void repolish(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

}  // namespace

void applyTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette colors;
    colors.setColor(QPalette::Window, palette::window);
    colors.setColor(QPalette::Base, palette::surface);
    colors.setColor(QPalette::AlternateBase, QColor(0xF6, 0xF7, 0xF9));
    colors.setColor(QPalette::Text, palette::ink);
    colors.setColor(QPalette::WindowText, palette::ink);
    colors.setColor(QPalette::ButtonText, palette::ink);
    colors.setColor(QPalette::Button, palette::surface);
    colors.setColor(QPalette::Highlight, palette::accent);
    colors.setColor(QPalette::HighlightedText, palette::surface);
    colors.setColor(QPalette::PlaceholderText, palette::muted);
    colors.setColor(QPalette::Mid, palette::line);
    colors.setColor(QPalette::Dark, palette::muted);
    colors.setColor(QPalette::Disabled, QPalette::Text, QColor(0xA4, 0xAB, 0xB5));
    colors.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xA4, 0xAB, 0xB5));
    colors.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0xA4, 0xAB, 0xB5));
    app.setPalette(colors);

    QFont font = app.font();
    if (font.pointSizeF() > 0 && font.pointSizeF() < 10.0) {
        font.setPointSizeF(10.0);
    }
    app.setFont(font);

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
QLabel[role="banner-error"] { color: %8; background: #FBEDEC; border-radius: 8px; padding: 8px 12px; }
QFrame[role="alert"] { background: #FBEDEC; border-radius: 8px; }
QFrame[role="alert"] QPushButton[variant="quiet"] { color: %8; }
QLabel[role="pill"] { background: %7; color: %6; border-radius: 11px; padding: 3px 12px; font-weight: 600; }
QLabel[role="pill"][level="low"] { background: #E3F2EA; color: #2C7A53; }
QLabel[role="pill"][level="moderate"] { background: #FBEFD9; color: #9A6212; }
QLabel[role="pill"][level="high"] { background: #FBE4E2; color: #A8322C; }
QFrame[card="true"] QListWidget[inset="true"], QListWidget[inset="true"] { border: 1px solid %5; border-radius: 8px; }
QFrame[role="tile"] { background: #F6F7F9; border-radius: 10px; }
QLabel[role="accent"] { color: %6; }
QFrame[role="strip"] { background: #F6F7F9; border-radius: 8px; }
QLabel[role="banner-note"] { color: %3; background: %7; border-radius: 8px; padding: 8px 12px; }

QPushButton {
    background: %2; border: 1px solid %5; border-radius: 7px; padding: 5px 12px; color: %3; min-height: 18px;
}
QPushButton:hover { border-color: %4; }
QPushButton:pressed { background: %1; }
QPushButton:disabled { color: #A4ABB5; border-color: %5; background: %2; }
QPushButton[variant="primary"] { background: %6; border-color: %6; color: white; font-weight: 600; }
QPushButton[variant="primary"]:hover { background: #3E3B8E; }
QPushButton[variant="primary"]:disabled { background: #B8B7DA; border-color: #B8B7DA; color: white; }
/* An ability that can be used, in the same style as Next turn. Disabled stays the plain button. */
QPushButton[variant="ready"] { background: %6; border-color: %6; color: white; font-weight: 600; }
QPushButton[variant="ready"]:hover { background: #3E3B8E; }
QPushButton[variant="ready"]:disabled { color: #A4ABB5; border-color: %5; background: %2; font-weight: 400; }
QWidget#actionButtons QPushButton { padding-left: 4px; padding-right: 4px; }
QPushButton[variant="danger"] { color: %8; }
QPushButton[variant="danger"]:hover { border-color: %8; }
QPushButton[variant="quiet"] { border: none; background: transparent; color: %6; padding: 4px 6px; }
QPushButton[variant="quiet"]:hover { text-decoration: underline; }
QPushButton[variant="quiet"]:disabled { color: #A4ABB5; text-decoration: none; }
QToolButton { border: 1px solid %5; border-radius: 7px; padding: 5px 8px; background: %2; }
QToolButton::menu-indicator { image: none; width: 0; }

QLineEdit, QSpinBox, QComboBox, QPlainTextEdit {
    background: %2; border: 1px solid %5; border-radius: 7px; padding: 4px 8px; color: %3;
    selection-background-color: %6;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border: 1px solid %6; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: #A4ABB5; background: #F7F8FA; }
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
QToolTip { background: %3; color: white; border: none; padding: 6px 8px; border-radius: 6px; }
QMenu { background: %2; border: 1px solid %5; padding: 6px; border-radius: 8px; }
QMenu::item { padding: 6px 18px; border-radius: 6px; }
QMenu::item:selected { background: %7; color: %3; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #C9CED6; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #C9CED6; border-radius: 4px; min-width: 30px; }
)")
                              .arg(hex(palette::window), hex(palette::surface), hex(palette::ink),
                                   hex(palette::muted), hex(palette::line), hex(palette::accent),
                                   hex(palette::accentSoft), hex(palette::critical))
                              .arg(chevronFile(palette::muted, QStringLiteral("chevron")),
                                   chevronFile(QColor(0xC4, 0xC9, 0xD0), QStringLiteral("chevron-off")));
    app.setStyleSheet(sheet);
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
