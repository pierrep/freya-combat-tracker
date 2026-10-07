#include "ui/combat_page.h"

#include "core/character_store.h"
#include "core/combat_rules.h"
#include "core/encounter_store.h"
#include "core/monster_catalog.h"
#include "data/json_history.h"
#include "ui/flow_layout.h"
#include "ui/page_title.h"
#include "ui/session_state.h"
#include "ui/theme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QValidator>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <array>
#include <limits>
#include <memory>
#include <string_view>

namespace combat::ui {

namespace {

// Target rules, conditions the action gives, and drains, under the action.
void addRuleNotes(QVBoxLayout* layout, const MonsterAttack& attack)
{
    for (const std::string& line : describeActionRules(attack)) {
        auto* note = new QLabel(QString::fromStdString(line));
        note->setWordWrap(true);
        note->setProperty("role", QStringLiteral("muted"));
        note->setObjectName(QStringLiteral("actionRule"));
        layout->addWidget(note);
    }
}


constexpr std::size_t kUndoLimit = 50;
// The tick for a save the target makes with Advantage (SaveSpec::advantageIf).
constexpr const char* kSaveAdvantageChoice = "@saveAdvantage";
constexpr int kLogLimit = 40;

void clearLayout(QLayout* layout)
{
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        if (QLayout* child = item->layout()) {
            clearLayout(child);
        }
        delete item;
    }
}

QLabel* boldLabel(const QString& text)
{
    auto* label = new QLabel(text);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    label->setWordWrap(true);
    return label;
}

QLabel* bodyLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

// The line between the sections of a tab, above each heading. Two pixels of
// soft accent with rounded ends: the grey hairline of a box or list is a
// control's edge, so a divider has to read as something else.
QFrame* sectionRule()
{
    auto* rule = new QFrame;
    rule->setObjectName(QStringLiteral("sectionRule"));
    rule->setFixedHeight(2);
    const QColor tint((palette::accent.red() * 22 + palette::surface.red() * 78) / 100,
                      (palette::accent.green() * 22 + palette::surface.green() * 78) / 100,
                      (palette::accent.blue() * 22 + palette::surface.blue() * 78) / 100);
    rule->setStyleSheet(
        QStringLiteral("QFrame#sectionRule { background: %1; border: none; border-radius: 1px; }").arg(tint.name()));
    return rule;
}

// A section heading in a tab's list, with the line above it unless it is the
// first thing in the list.
void addSectionHeading(QVBoxLayout* layout, QWidget* heading, bool always = false)
{
    if (always || layout->count() > 0) {
        layout->addSpacing(6);
        layout->addWidget(sectionRule());
        layout->addSpacing(4);
    }
    layout->addWidget(heading);
}

// Every action entry is a button column on the left and its text on the
// right. The column has one width for the whole tab, so the buttons line up
// and are never pushed off the card by long text.
constexpr int kButtonColumnWidth = 99;

struct EntryRow {
    QWidget* row = nullptr;
    QVBoxLayout* buttons = nullptr;
    QVBoxLayout* content = nullptr;
};

EntryRow makeEntryRow()
{
    EntryRow entry;
    entry.row = new QWidget;
    auto* line = new QHBoxLayout(entry.row);
    line->setContentsMargins(0, 2, 0, 6);
    line->setSpacing(12);
    auto* column = new QWidget;
    column->setObjectName(QStringLiteral("actionButtons"));
    column->setFixedWidth(kButtonColumnWidth);
    entry.buttons = new QVBoxLayout(column);
    entry.buttons->setContentsMargins(0, 0, 0, 0);
    entry.buttons->setSpacing(3);
    line->addWidget(column, 0, Qt::AlignTop);
    entry.content = new QVBoxLayout;
    entry.content->setContentsMargins(0, 0, 0, 0);
    entry.content->setSpacing(2);
    line->addLayout(entry.content, 1);
    return entry;
}

// A button that fills the column. Its side padding is small so the longest
// labels ("Multiattack", "Recharging") fit the column without being clipped.
void addColumnButton(const EntryRow& entry, QPushButton* button)
{
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(QStringLiteral("QPushButton { padding-left: 4px; padding-right: 4px; }"));
    entry.buttons->addWidget(button);
}

// A small note under the buttons: "2 left today", "costs 1 legendary".
void addColumnNote(const EntryRow& entry, const QString& text)
{
    auto* note = new QLabel(text);
    note->setObjectName(QStringLiteral("actionButtonNote"));
    note->setProperty("role", QStringLiteral("muted"));
    note->setAlignment(Qt::AlignHCenter);
    note->setWordWrap(true);
    QFont font = note->font();
    font.setPointSizeF(font.pointSizeF() * 0.88);
    note->setFont(font);
    entry.buttons->addWidget(note);
}

// Under a legendary action: what it costs and what is left, or why not now.
QString legendaryNote(const Combatant& combatant, bool theirTurn)
{
    if (theirTurn) {
        return QCoreApplication::translate("CombatPage", "after another creature's turn");
    }
    return QCoreApplication::translate("CombatPage", "costs 1 \u00B7 %1 left").arg(combatant.economy.legendaryRemaining);
}

// A label that ticks its tick box when clicked, so a long choice can wrap.
class ToggleLabel : public QLabel {
public:
    ToggleLabel(const QString& text, QCheckBox* box) : QLabel(text), m_box(box)
    {
        setWordWrap(true);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        QLabel::mousePressEvent(event);
        if (m_box->isEnabled()) {
            m_box->toggle();
        }
    }

private:
    QCheckBox* m_box;
};

// A tick box whose text wraps. The text is also the box's accessible name.
QWidget* wrappingCheckBox(QCheckBox* box, const QString& text)
{
    auto* host = new QWidget;
    auto* line = new QHBoxLayout(host);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(6);
    box->setText(QString());
    box->setAccessibleName(text);
    line->addWidget(box, 0, Qt::AlignTop);
    auto* label = new ToggleLabel(text, box);
    label->setToolTip(box->toolTip());
    line->addWidget(label, 1);
    return host;
}

// A flat, slim sword pointing up and left like an arrow cursor. The tip is the
// hotspot, so the click lands where the sword points. Drawn at twice the size
// for sharp edges on high-density screens; a white outline keeps it visible on
// dark and light rows alike.
QPixmap swordPixmap(qreal scale)
{
    const int size = 32;
    QPixmap pixmap(static_cast<int>(size * scale), static_cast<int>(size * scale));
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Drawn upright with the tip at the origin, then turned 45 degrees.
    painter.translate(3.0, 3.0);
    painter.rotate(-45.0);

    QPainterPath blade;
    blade.moveTo(0.0, 0.0);
    blade.lineTo(2.5, 7.0);
    blade.lineTo(2.5, 19.0);
    blade.lineTo(-2.5, 19.0);
    blade.lineTo(-2.5, 7.0);
    blade.closeSubpath();
    QPainterPath guard;
    guard.addRoundedRect(QRectF(-6.0, 19.6, 12.0, 2.6), 1.3, 1.3);
    QPainterPath grip;
    grip.addRoundedRect(QRectF(-1.5, 21.6, 3.0, 7.4), 1.5, 1.5);
    const QPainterPath whole = blade.united(guard).united(grip);

    // Outline first, then the flat fills on top.
    painter.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::white);
    painter.drawPath(whole);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette::ink);
    painter.drawPath(guard.united(grip));
    painter.setBrush(palette::accent);
    painter.drawPath(blade);
    // One fuller line down the blade; the only detail.
    painter.setPen(QPen(QColor(255, 255, 255, 150), 0.9, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(0.0, 6.5), QPointF(0.0, 17.5));
    painter.end();
    return pixmap;
}

QCursor swordCursor()
{
    return QCursor(swordPixmap(2.0), 2, 2);
}

// The sword's counterpart for help: healing, Temporary Hit Points, a War Cry.
// A four-pointed sparkle in the healthy green with a small one beside it,
// outlined in white like the sword so it shows on any row.
QPixmap helpPixmap(qreal scale, bool outline = true)
{
    const int size = 32;
    QPixmap pixmap(static_cast<int>(size * scale), static_cast<int>(size * scale));
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto sparkle = [](QPointF centre, qreal reach, qreal waist) {
        QPainterPath path;
        path.moveTo(centre.x(), centre.y() - reach);
        path.quadTo(centre.x() + waist, centre.y() - waist, centre.x() + reach, centre.y());
        path.quadTo(centre.x() + waist, centre.y() + waist, centre.x(), centre.y() + reach);
        path.quadTo(centre.x() - waist, centre.y() + waist, centre.x() - reach, centre.y());
        path.quadTo(centre.x() - waist, centre.y() - waist, centre.x(), centre.y() - reach);
        path.closeSubpath();
        return path;
    };
    const QPainterPath big = sparkle(QPointF(13.0, 13.0), 11.0, 2.2);
    const QPainterPath small = sparkle(QPointF(25.0, 24.0), 5.0, 1.1);
    if (outline) {
        painter.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::white);
        painter.drawPath(big.united(small));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette::healthy);
    painter.drawPath(big);
    painter.drawPath(small);
    painter.end();
    return pixmap;
}

QCursor helpCursor()
{
    return QCursor(helpPixmap(2.0), 13, 13);  // the hotspot is the big sparkle's centre
}

QIcon helpIcon()
{
    return QIcon(helpPixmap(2.0, false));
}

QIcon swordIcon()
{
    return QIcon(swordPixmap(2.0));
}

// Armor Class drawn as a heater shield: a soft accent face, an accent rim,
// a thin inner line, and the text on top. Used large for a creature's AC on
// its card and small behind "AC" in the turn order's heading.
// The shield's outline in a box: a gently arched top, straight upper sides,
// and curves meeting at the point.
QPainterPath shieldPath(const QRectF& r)
{
    QPainterPath path;
    const qreal dip = r.height() * 0.06;
    path.moveTo(r.left(), r.top() + dip);
    path.quadTo(r.center().x(), r.top() - dip, r.right(), r.top() + dip);
    path.lineTo(r.right(), r.top() + r.height() * 0.42);
    path.cubicTo(r.right(), r.top() + r.height() * 0.74, r.center().x() + r.width() * 0.22, r.top() + r.height() * 0.9,
                 r.center().x(), r.bottom());
    path.cubicTo(r.center().x() - r.width() * 0.22, r.top() + r.height() * 0.9, r.left(), r.top() + r.height() * 0.74,
                 r.left(), r.top() + r.height() * 0.42);
    path.closeSubpath();
    return path;
}

// Draws the shield straight onto a painter, at the painter's own resolution,
// so it is antialiased at any size. The large one has a soft face, an accent
// rim, and a thin inner line. The small one is solid accent with white text:
// a thin rim and a fine line turn to fuzz at 20 pixels, a filled shape stays
// clean, and white on the accent reads at a tiny size.
void paintShield(QPainter& painter, const QRectF& box, const QString& text, qreal fontPixels)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    const bool small = box.width() < 28.0;
    QColor ink = palette::accent;
    if (small) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette::accent);
        painter.drawPath(shieldPath(box.adjusted(0.5, 0.5, -0.5, -0.5)));
        ink = palette::surface;
    } else {
        const qreal rim = std::max(1.5, box.width() * 0.06);
        const QRectF outer = box.adjusted(rim / 2.0, rim / 2.0 + box.height() * 0.03, -rim / 2.0, -rim / 2.0);
        painter.setPen(QPen(palette::accent, rim, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(palette::accentSoft);
        painter.drawPath(shieldPath(outer));
        const qreal inset = rim * 1.9;
        painter.setPen(QPen(QColor(palette::accent.red(), palette::accent.green(), palette::accent.blue(), 70), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shieldPath(outer.adjusted(inset, inset, -inset, -inset * 1.4)));
    }
    QFont font = QApplication::font();
    font.setPixelSize(std::max(6, static_cast<int>(std::lround(fontPixels))));
    font.setWeight(QFont::Bold);
    if (small) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0.3);
    }
    painter.setFont(font);
    painter.setPen(ink);
    // The text sits a little above centre, where the shield is widest. Whole
    // pixels keep the letters sharp.
    const QRectF textBox(std::round(box.left()), std::round(box.top()), std::round(box.width()),
                         std::round(box.height() * 0.84));
    painter.drawText(textBox, Qt::AlignCenter, text);
    painter.restore();
}

// The large shield as a pixmap at the screen's resolution (for a label).
QPixmap shieldPixmap(QSizeF size, const QString& text, qreal fontPixels, qreal scale)
{
    QPixmap pixmap(static_cast<int>(std::ceil(size.width() * scale)), static_cast<int>(std::ceil(size.height() * scale)));
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    paintShield(painter, QRectF(QPointF(0.0, 0.0), size), text, fontPixels);
    painter.end();
    return pixmap;
}

// A d20 seen face on: a solid accent hexagon, its facets in white lines,
// and the face toward you a shade lighter. Drawn as shapes so it stays crisp
// at the 20 pixels of a column heading.
void paintD20(QPainter& painter, const QRectF& box)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF centre = box.center();
    const qreal radius = std::min(box.width() / std::sqrt(3.0), box.height() / 2.0);
    const auto corner = [&centre, radius](int degrees) {
        const qreal angle = degrees * 3.14159265358979 / 180.0;
        return QPointF(centre.x() + radius * std::cos(angle), centre.y() + radius * std::sin(angle));
    };
    QPolygonF hexagon;
    for (const int degrees : {-90, -30, 30, 90, 150, 210}) {
        hexagon << corner(degrees);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette::accent);
    painter.drawPolygon(hexagon);
    // The front face.
    const qreal reach = radius * 0.52;
    const QPointF top(centre.x(), centre.y() - reach);
    const QPointF left(centre.x() - reach * 0.866, centre.y() + reach * 0.5);
    const QPointF right(centre.x() + reach * 0.866, centre.y() + reach * 0.5);
    painter.setBrush(QColor(255, 255, 255, 46));
    painter.drawPolygon(QPolygonF({top, left, right}));
    // The edges: the face, and each of its corners to the three nearest corners
    // of the outline.
    const bool small = box.width() < 28.0;
    painter.setPen(QPen(QColor(255, 255, 255, small ? 185 : 210), small ? 0.95 : 1.4, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(QPolygonF({top, left, right}));
    for (const int degrees : {-150, -90, -30}) {
        painter.drawLine(top, corner(degrees));
    }
    for (const int degrees : {-150, 150, 90}) {
        painter.drawLine(left, corner(degrees));
    }
    for (const int degrees : {-30, 30, 90}) {
        painter.drawLine(right, corner(degrees));
    }
    painter.restore();
}

// A tree header that draws some column titles as icons: the d20 for
// initiative, the shield (with "AC" on it) for Armor Class. The titles stay in
// the model for tooltips and screen readers.
class IconHeader : public QHeaderView {
public:
    enum class Icon { D20, Shield };

    IconHeader(std::map<int, Icon> icons, QWidget* parent)
        : QHeaderView(Qt::Horizontal, parent), m_icons(std::move(icons))
    {
        setSectionsClickable(false);
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

protected:
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override
    {
        const auto found = m_icons.find(logicalIndex);
        if (found == m_icons.end()) {
            QHeaderView::paintSection(painter, rect, logicalIndex);
            return;
        }
        // The section's background and line, without its text.
        QStyleOptionHeader option;
        initStyleOption(&option);
        option.rect = rect;
        option.section = logicalIndex;
        option.text.clear();
        // The style leaves the painter clipped; draw it in its own state.
        painter->save();
        style()->drawControl(QStyle::CE_Header, &option, painter, this);
        painter->restore();
        // Drawn as shapes on whole pixels, not a scaled picture. The d20 sits
        // over the right-aligned initiative numbers; the shield is centred.
        const QSizeF size = sizeOf(found->second);
        const qreal x = found->second == Icon::D20 ? rect.right() - size.width() - 5.0
                                                   : rect.center().x() - size.width() / 2.0 + 1.0;
        const QRectF box(QPointF(std::floor(x), std::floor(rect.center().y() - size.height() / 2.0)), size);
        if (found->second == Icon::D20) {
            paintD20(*painter, box);
        } else {
            paintShield(*painter, box, model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString(), 9.0);
        }
    }

    QSize sectionSizeFromContents(int logicalIndex) const override
    {
        QSize size = QHeaderView::sectionSizeFromContents(logicalIndex);
        const auto found = m_icons.find(logicalIndex);
        if (found != m_icons.end()) {
            const QSizeF icon = sizeOf(found->second);
            size.setWidth(static_cast<int>(icon.width()) + 14);
            size.setHeight(std::max(size.height(), static_cast<int>(icon.height()) + 8));
        }
        return size;
    }

private:
    static QSizeF sizeOf(Icon icon) { return icon == Icon::D20 ? QSizeF(21.0, 23.0) : QSizeF(20.0, 23.0); }

    std::map<int, Icon> m_icons;
};

// Weapon damage first, the rest alphabetically after a line.
void fillDamageTypes(QComboBox* box)
{
    const std::array<const char*, 3> weaponTypes{{"slashing", "bludgeoning", "piercing"}};
    for (const char* type : weaponTypes) {
        box->addItem(QString::fromLatin1(type), QString::fromLatin1(type));
    }
    box->insertSeparator(box->count());
    for (const char* type : kDamageTypes) {
        if (std::find(weaponTypes.begin(), weaponTypes.end(), std::string_view(type)) == weaponTypes.end()) {
            box->addItem(QString::fromLatin1(type), QString::fromLatin1(type));
        }
    }
}

// A tab page that scrolls when its content is taller than the tab.
QScrollArea* scrollingTab(QWidget* page)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    return scroll;
}

// A number box that also takes a change: "-7" takes 7 off, "+5" adds 5.
class RelativeSpinBox : public QSpinBox {
public:
    using QSpinBox::QSpinBox;

protected:
    QValidator::State validate(QString& input, int& position) const override
    {
        const QString text = input.trimmed();
        if (text.startsWith(QLatin1Char('+')) || text.startsWith(QLatin1Char('-')) ||
            text.startsWith(QChar(0x2212))) {
            bool ok = false;
            text.mid(1).toInt(&ok);
            return text.size() == 1 ? QValidator::Intermediate : ok ? QValidator::Acceptable : QValidator::Invalid;
        }
        return QSpinBox::validate(input, position);
    }

    int valueFromText(const QString& input) const override
    {
        const QString text = input.trimmed();
        if (text.size() > 1 && (text.startsWith(QLatin1Char('+')) || text.startsWith(QLatin1Char('-')) ||
                                text.startsWith(QChar(0x2212)))) {
            const long long delta = text.mid(1).toLongLong();
            const long long next = text.startsWith(QLatin1Char('+')) ? static_cast<long long>(value()) + delta
                                                                     : static_cast<long long>(value()) - delta;
            return static_cast<int>(std::clamp<long long>(next, minimum(), maximum()));
        }
        return QSpinBox::valueFromText(input);
    }
};

QSpinBox* makeNumberBox(int minimum, int maximum)
{
    auto* box = new QSpinBox;
    box->setRange(minimum, maximum);
    box->setMaximumWidth(140);
    return box;
}

// Sizes a box to hold a number of the given digits and no more. The amount
// boxes on the turn row take three; Hit Point boxes take four. A box that
// reads "+7" or "-7" gets a character more for the sign.
void fitDigits(QSpinBox* box, int digits, bool signedText = false)
{
    int maximum = 1;
    for (int i = 0; i < digits; ++i) {
        maximum *= 10;
    }
    box->setRange(box->minimum() < 0 ? -(maximum - 1) : 0, maximum - 1);
    if (QLineEdit* edit = box->findChild<QLineEdit*>()) {
        edit->setMaxLength(digits + (signedText ? 1 : 0));
    }
    const QFontMetrics metrics(box->font());
    const int textWidth = metrics.horizontalAdvance(QString(digits, QLatin1Char('8')));
    QStyleOptionSpinBox option;
    option.initFrom(box);
    option.rect = QRect(0, 0, 400, metrics.height() + 16);
    option.frame = box->hasFrame();
    option.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField | QStyle::SC_SpinBoxUp |
                         QStyle::SC_SpinBoxDown;
    const QRect editField = box->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, box);
    const int chrome = option.rect.width() - editField.width();
    box->setFixedWidth(textWidth + chrome + 4);
}

void fitThreeDigitAmount(QSpinBox* box)
{
    fitDigits(box, 3);
}

QTreeWidget* makeCombatantTree(const QStringList& headers, int stretchColumn)
{
    auto* tree = new QTreeWidget;
    tree->setColumnCount(static_cast<int>(headers.size()));
    tree->setHeaderLabels(headers);
    tree->setRootIsDecorated(false);
    tree->setIndentation(0);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setAllColumnsShowFocus(true);
    tree->setUniformRowHeights(true);
    tree->setMinimumHeight(160);
    tree->header()->setStretchLastSection(false);
    for (int column = 0; column < tree->columnCount(); ++column) {
        tree->header()->setSectionResizeMode(column, column == stretchColumn ? QHeaderView::Stretch
                                                                             : QHeaderView::ResizeToContents);
    }
    return tree;
}

int indexOfId(const Encounter& encounter, const std::string& id)
{
    for (int i = 0; i < static_cast<int>(encounter.combatants.size()); ++i) {
        if (encounter.combatants[static_cast<std::size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

// A colour for each condition, so a tag reads at a glance.
QColor conditionColor(const std::string& id)
{
    static const std::vector<std::pair<std::string, QColor>> colors = {
        {"blinded", QColor(0x5B, 0x63, 0x70)},       {"charmed", QColor(0xC2, 0x4F, 0x9A)},
        {"deafened", QColor(0x6A, 0x7F, 0x9C)},      {"frightened", QColor(0x7B, 0x4F, 0xC2)},
        {"grappled", QColor(0xA8, 0x61, 0x2E)},      {"incapacitated", QColor(0x4A, 0x4F, 0x5C)},
        {"invisible", QColor(0x2E, 0x8F, 0xB0)},     {"paralyzed", QColor(0xB8, 0x8A, 0x10)},
        {"petrified", QColor(0x7D, 0x72, 0x66)},     {"poisoned", QColor(0x3E, 0x8E, 0x3A)},
        {"prone", QColor(0x9C, 0x7A, 0x4E)},         {"restrained", QColor(0xC8, 0x6A, 0x1E)},
        {"stunned", QColor(0xC2, 0x41, 0x3A)},       {"unconscious", QColor(0x2E, 0x3F, 0x7A)},
        {"exhaustion", QColor(0x8A, 0x5A, 0x44)},
    };
    for (const auto& [condition, color] : colors) {
        if (condition == id) {
            return color;
        }
    }
    return palette::accent;
}

// "amount" of a over b: 0 is all b, 1 is all a.
QColor blend(const QColor& a, const QColor& b, double amount)
{
    auto mix = [amount](int x, int y) { return static_cast<int>(x * amount + y * (1.0 - amount)); };
    return QColor(mix(a.red(), b.red()), mix(a.green(), b.green()), mix(a.blue(), b.blue()));
}

QString statusText(const Combatant& combatant, const std::vector<Condition>& catalog)
{
    QStringList parts;
    if (combatant.dead) {
        parts << QObject::tr("Dead");
    } else if (isDying(combatant)) {
        parts << QObject::tr("Dying %1✓ %2✗").arg(combatant.deathSaves.successes).arg(combatant.deathSaves.failures);
    } else if (combatant.stable && combatant.hp == 0) {
        parts << QObject::tr("Stable");
    }
    // Bloodied is not listed: the HP bar turns orange and the card has a tag.
    if (!combatant.concentration.empty()) {
        parts << QObject::tr("Conc");
    }
    if (combatant.exhaustion > 0) {
        parts << QObject::tr("Exh %1").arg(combatant.exhaustion);
    }
    for (const ActiveCondition& condition : combatant.conditions) {
        if (condition.id == "unconscious" && (isDying(combatant) || combatant.stable)) {
            continue;
        }
        const auto found = findConditionById(catalog, condition.id);
        parts << (found.has_value() ? QString::fromStdString(found->name) : QString::fromStdString(condition.id));
    }
    // Help someone gave it: "Advantage (War Cry)", "+2 AC (Shimmering Shield)".
    for (const auto& effect : combatant.timedEffects) {
        const QString name = QString::fromStdString(effect.first);
        if (name.startsWith(QStringLiteral("advantage:"))) {
            parts << QObject::tr("Advantage (%1)").arg(name.mid(10));
        } else if (const int ac = timedAcBonus(effect.first); ac != 0) {
            parts << QObject::tr("+%1 AC (%2)").arg(ac).arg(name.section(QLatin1Char(':'), 2));
        } else {
            parts << name;  // a trait's own (Aversion to Fire)
        }
    }
    return parts.join(QStringLiteral(", "));
}

QString economyText(const Combatant& combatant)
{
    const TurnEconomy& economy = combatant.economy;
    // The checkboxes beside this already show the action, bonus action, and
    // reaction; this only adds what they cannot.
    QStringList parts;
    if (economy.attacksRemaining > 0) {
        parts << QObject::tr("%n attack(s) left", nullptr, economy.attacksRemaining);
    }
    if (combatant.statBlock.has_value() && combatant.statBlock->legendaryActionUses > 0) {
        parts << QObject::tr("L %1/%2").arg(economy.legendaryRemaining).arg(combatant.statBlock->legendaryActionUses);
    }
    return parts.join(QStringLiteral("    "));
}

constexpr int kHpRole = Qt::UserRole + 1;
constexpr int kMaxHpRole = Qt::UserRole + 2;
// The combatant whose turn it is.
constexpr int kActiveRole = Qt::UserRole + 3;

// The initiative cell. On the creature whose turn it is, the number sits in a
// filled accent chip, so the turn marker takes no column of its own.
class TurnChipDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (!index.data(kActiveRole).toBool()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        // The row's background (selection included) first, without the text.
        QStyleOptionViewItem background(option);
        initStyleOption(&background, index);
        background.text.clear();
        const QWidget* widget = option.widget;
        QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);
        if ((option.state & QStyle::State_Selected) != 0) {
            painter->fillRect(option.rect, palette::accentSoft);
        }

        const QString text = index.data(Qt::DisplayRole).toString();
        QFont font = option.font;
        font.setWeight(QFont::Bold);
        const QFontMetrics metrics(font);
        const int width = std::max(metrics.horizontalAdvance(text) + 12, metrics.height() + 6);
        const int height = metrics.height() + 4;
        QRect chip(0, 0, std::min(width, option.rect.width() - 2), height);
        chip.moveCenter(option.rect.center());
        chip.moveRight(option.rect.right() - 2);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette::accent);
        painter->drawRoundedRect(chip, height / 2.0, height / 2.0);
        painter->setPen(palette::surface);
        painter->setFont(font);
        painter->drawText(chip, Qt::AlignCenter, text);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        QFont font = option.font;
        font.setWeight(QFont::Bold);
        const QFontMetrics metrics(font);
        const int chip = std::max(metrics.horizontalAdvance(index.data(Qt::DisplayRole).toString()) + 12,
                                  metrics.height() + 6) + 4;
        size.setWidth(std::max(size.width(), chip));
        return size;
    }
};

// HP text over a thin bar coloured by how hurt the creature is.
class HealthBarDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem textOption(option);
        initStyleOption(&textOption, index);
        const int maximum = index.data(kMaxHpRole).toInt();
        const int current = index.data(kHpRole).toInt();
        if (maximum <= 0) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        // The row's background (selection included) over the whole cell, then
        // the text above the bar.
        const QString text = textOption.text;
        textOption.text.clear();
        const QWidget* widget = option.widget;
        QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &textOption, painter, widget);
        if ((option.state & QStyle::State_Selected) != 0) {
            // The style sheet's selection colour, which a delegate's cell
            // does not always get.
            painter->fillRect(option.rect, palette::accentSoft);
        }
        const QRect textRect = option.rect.adjusted(6, 0, -6, -5);
        painter->save();
        painter->setFont(option.font);
        painter->setPen(palette::ink);
        painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, text);
        painter->restore();
        const QRect track(option.rect.left() + 6, option.rect.bottom() - 6, option.rect.width() - 12, 4);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette::line);
        painter->drawRoundedRect(track, 2, 2);
        const double fraction = std::clamp(static_cast<double>(current) / maximum, 0.0, 1.0);
        if (fraction > 0.0) {
            QRect fill = track;
            fill.setWidth(std::max(2, static_cast<int>(track.width() * fraction)));
            painter->setBrush(healthColor(current, maximum));
            painter->drawRoundedRect(fill, 2, 2);
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setWidth(std::max(size.width(), 84));
        size.setHeight(size.height() + 4);
        return size;
    }
};

QTreeWidgetItem* addCombatantRow(QTreeWidget* tree, const Combatant& combatant, bool active, bool initiativeColumns,
                                 const std::vector<Condition>& catalog)
{
    auto* item = new QTreeWidgetItem(tree);
    const QString name = QString::fromStdString(combatant.name);
    const QString hp = QString::fromStdString(formatHitPoints(combatant.hp, combatant.maxHp)) +
                       (combatant.tempHp > 0 ? QStringLiteral(" +%1").arg(combatant.tempHp) : QString());
    item->setData(0, Qt::UserRole, QString::fromStdString(combatant.id));
    if (initiativeColumns) {
        item->setText(0, QString::number(combatant.initiative));
        item->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, kActiveRole, active);
        item->setText(1, name);
        if (active) {
            QFont font = tree->font();
            font.setWeight(QFont::DemiBold);
            item->setFont(1, font);
            item->setToolTip(1, QCoreApplication::translate("CombatPage", "%1's turn").arg(name));
        }
        item->setText(2, QString::number(combatant.ac));
        item->setTextAlignment(2, Qt::AlignCenter);
        item->setText(3, hp);
        item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(3, kHpRole, combatant.hp);
        item->setData(3, kMaxHpRole, combatant.maxHp.value_or(0));
        const QString status = statusText(combatant, catalog);
        item->setText(4, status);
        item->setToolTip(4, status);
    } else {
        item->setText(0, name);
        item->setText(1, hp);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(2, statusText(combatant, catalog));
    }
    return item;
}

QString deathSaveWords(DeathSaveResult result)
{
    switch (result) {
    case DeathSaveResult::NotDying:
        return QObject::tr("is not dying");
    case DeathSaveResult::Success:
        return QObject::tr("succeeds");
    case DeathSaveResult::Failure:
        return QObject::tr("fails");
    case DeathSaveResult::DoubleFailure:
        return QObject::tr("rolls a 1: two failures");
    case DeathSaveResult::Stabilized:
        return QObject::tr("succeeds and is stable");
    case DeathSaveResult::Died:
        return QObject::tr("fails and dies");
    case DeathSaveResult::Revived:
        return QObject::tr("rolls a 20 and wakes with 1 HP");
    }
    return {};
}

}  // namespace

CombatPage::~CombatPage()
{
    if (m_swordCursor) {
        QApplication::restoreOverrideCursor();
    }
    flushPendingSave();
}

CombatPage::CombatPage(CharacterStore& characters, MonsterCatalog& catalog, EncounterStore& encounters,
                       std::vector<Spell> spells, std::vector<Condition> conditions, QWidget* parent)
    : QWidget(parent)
    , m_charactersStore(characters)
    , m_catalog(catalog)
    , m_encountersStore(encounters)
    , m_spells(std::move(spells))
    , m_conditions(std::move(conditions))
    , m_dice(std::random_device{}())
{
    try {
        m_encounters = m_encountersStore.loadAll();
    } catch (const EncounterStoreError& error) {
        m_loadError = QString::fromStdString(error.what());
    }

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, [this] { flushPendingSave(); });

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 20, 28, 20);
    outer->setSpacing(14);

    // Header: which fight, whose turn, and the turn controls.
    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    header->setObjectName(QStringLiteral("pageHeader"));
    header->addWidget(makePageTitle(tr("Dashboard")));
    header->addSpacing(12);
    m_encounterCombo = new QComboBox;
    m_encounterCombo->setObjectName(QStringLiteral("encounterCombo"));
    m_encounterCombo->setMinimumWidth(220);
    header->addWidget(m_encounterCombo);
    header->addSpacing(12);
    m_roundLabel = makeMuted(QString());
    m_roundLabel->setObjectName(QStringLiteral("roundLabel"));
    m_roundLabel->setWordWrap(false);
    m_activeLabel = new QLabel;
    m_activeLabel->setObjectName(QStringLiteral("activeCombatant"));
    QFont activeFont = m_activeLabel->font();
    activeFont.setPointSizeF(activeFont.pointSizeF() * 1.25);
    activeFont.setWeight(QFont::DemiBold);
    m_activeLabel->setFont(activeFont);
    header->addWidget(m_roundLabel);
    header->addWidget(m_activeLabel);
    header->addStretch(1);
    m_undoButton = new QPushButton(tr("Undo"));
    m_undoButton->setObjectName(QStringLiteral("undoFight"));
    m_undoButton->setToolTip(tr("Undo the last change (Ctrl+Z)"));
    m_undoButton->setEnabled(false);
    m_nextTurnButton = new QPushButton(tr("Next turn"));
    m_nextTurnButton->setObjectName(QStringLiteral("nextTurn"));
    makePrimary(m_nextTurnButton);
    header->addWidget(m_undoButton);
    header->addWidget(m_nextTurnButton);
    outer->addLayout(header);

    if (hasLoadError()) {
        auto* banner = new QLabel(tr("The encounters file could not be read, so it has not been changed.\n%1")
                                      .arg(m_loadError));
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        banner->setProperty("role", QStringLiteral("banner-error"));
        outer->addWidget(banner);
    }

    m_emptyHint = makeMuted(tr("Choose an encounter above, or create one in Encounter Builder."));
    outer->addWidget(m_emptyHint);

    m_fight = new QWidget;
    m_fight->setObjectName(QStringLiteral("combatFight"));
    auto* fightLayout = new QVBoxLayout(m_fight);
    fightLayout->setContentsMargins(0, 0, 0, 0);
    fightLayout->setSpacing(12);
    outer->addWidget(m_fight, 1);

    // Checks waiting for the GM sit above everything else.
    m_promptHost = new QFrame;
    m_promptHost->setObjectName(QStringLiteral("promptPanel"));
    m_promptHost->setProperty("card", true);
    m_promptHost->setStyleSheet(QStringLiteral("QFrame#promptPanel { border: 1px solid %1; background: %2; }")
                                    .arg(palette::accent.name(), palette::accentSoft.name()));
    m_promptLayout = new QVBoxLayout(m_promptHost);
    m_promptLayout->setContentsMargins(14, 10, 14, 10);
    m_promptHost->hide();
    fightLayout->addWidget(m_promptHost);

    m_rollNote = makeMuted(QString());
    m_rollNote->setObjectName(QStringLiteral("rollNote"));
    m_rollNote->hide();
    fightLayout->addWidget(m_rollNote);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(14);
    fightLayout->addWidget(splitter, 1);

    // Left: the turn order, who is down, and what happened.
    auto* leftHost = new QWidget;
    m_leftHost = leftHost;
    leftHost->installEventFilter(this);  // its height decides how tall the log is
    auto* left = new QVBoxLayout(leftHost);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(12);
    auto* orderCard = makeCard();
    m_orderCard = orderCard;
    auto* orderHeader = new QHBoxLayout;
    orderHeader->addWidget(makeHeading(tr("Turn order")));
    orderHeader->addStretch(1);
    // Rolling everyone's initiative happens on the initiative list (right).
    m_rollAllButton = new QPushButton(tr("Roll monster initiative"));
    m_rollAllButton->setObjectName(QStringLiteral("rollAllMonsters"));
    m_rollAllButton->setToolTip(tr("d20 + each stat block's initiative bonus. Options can give every monster of one "
                                   "kind the same roll."));
    makePrimary(m_rollAllButton);  // the same style as Start combat
    m_rollPlayersButton = new QPushButton(tr("Roll player initiative"));
    makePrimary(m_rollPlayersButton);
    m_rollPlayersButton->setObjectName(QStringLiteral("rollAllCharacters"));
    m_rollPlayersButton->setToolTip(tr("d20 + each character sheet's initiative bonus, for a table that lets the app "
                                       "roll. Type over any total."));
    // Damage and healing for any selected creatures: spells, traps, potions.
    m_openDamageButton = new QPushButton(swordIcon(), tr("Damage"));
    m_openDamageButton->setObjectName(QStringLiteral("openDamage"));
    m_openDamageButton->setToolTip(tr("Damage the selected creatures. Ctrl-click or Shift-click to select several."));
    makeQuiet(m_openDamageButton);
    m_openHealButton = new QPushButton(helpIcon(), tr("Heal"));
    m_openHealButton->setObjectName(QStringLiteral("openHeal"));
    m_openHealButton->setToolTip(tr("Heal the selected creatures."));
    makeQuiet(m_openHealButton);
    orderHeader->addWidget(m_openDamageButton);
    orderHeader->addWidget(m_openHealButton);
    static_cast<QVBoxLayout*>(orderCard->layout())->addLayout(orderHeader);
    // Status takes the spare width; nothing overflows, so the list never
    // scrolls sideways when a row is picked.
    m_initiativeList = makeCombatantTree({tr("Init"), tr("Name"), tr("AC"), tr("HP"), tr("Status")}, 4);
    m_initiativeList->setObjectName(QStringLiteral("initiativeList"));
    m_initiativeList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_initiativeList->setTextElideMode(Qt::ElideRight);
    m_initiativeList->headerItem()->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
    // Initiative as a d20 and "AC" on a small version of the card's shield.
    m_initiativeList->setHeader(
        new IconHeader({{0, IconHeader::Icon::D20}, {2, IconHeader::Icon::Shield}}, m_initiativeList));
    m_initiativeList->header()->setStretchLastSection(false);
    for (int column = 0; column < m_initiativeList->columnCount(); ++column) {
        m_initiativeList->header()->setSectionResizeMode(column, column == 4 ? QHeaderView::Stretch
                                                                             : QHeaderView::ResizeToContents);
    }
    m_initiativeList->headerItem()->setTextAlignment(2, Qt::AlignCenter);
    m_initiativeList->headerItem()->setToolTip(2, tr("Armor Class"));
    m_initiativeList->headerItem()->setToolTip(0, tr("Initiative"));
    m_initiativeList->setItemDelegateForColumn(0, new TurnChipDelegate(m_initiativeList));
    m_initiativeList->setItemDelegateForColumn(3, new HealthBarDelegate(m_initiativeList));
    orderCard->layout()->addWidget(m_initiativeList);
    m_noCombatantHint = makeMuted(tr("No one is in this fight yet. Add characters and monsters in Encounter Builder."));
    orderCard->layout()->addWidget(m_noCombatantHint);
    // The log takes 8 parts of the spare height to the turn order's 27: a
    // fifth less than it had at 2 to 5.
    left->addWidget(orderCard, 27);

    auto* downCard = makeCard();
    downCard->layout()->addWidget(makeHeading(tr("Downed")));
    m_downList = makeCombatantTree({tr("Name"), tr("HP"), tr("Status")}, 0);
    m_downList->setObjectName(QStringLiteral("zeroHpList"));
    m_downList->setMinimumHeight(0);
    m_downList->setHeaderHidden(true);
    m_downList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_downList->setProperty("compact", true);
    downCard->layout()->addWidget(m_downList);
    downCard->layout()->setContentsMargins(16, 10, 16, 8);
    downCard->layout()->setSpacing(4);
    left->addWidget(downCard, 0);

    auto* logCard = makeCard();
    m_logCard = logCard;
    logCard->layout()->addWidget(makeHeading(tr("Log")));
    m_hitLabel = new QLabel(this);
    m_hitLabel->setObjectName(QStringLiteral("attackHit"));
    m_hitLabel->setWordWrap(true);
    m_hitLabel->hide();
    m_logList = new QListWidget;
    m_logList->setObjectName(QStringLiteral("fightLog"));
    m_logList->setWordWrap(true);
    // Never shorter than three lines; a long turn order takes the rest.
    m_logList->setMinimumHeight(3 * (QFontMetrics(m_logList->font()).lineSpacing() + 4) + 2 * m_logList->frameWidth());
    QFont logFont = m_logList->font();
    logFont.setPointSizeF(logFont.pointSizeF() * 0.88);
    m_logList->setFont(logFont);
    m_downList->setFont(logFont);
    m_logList->setProperty("compact", true);
    // Lines wrap to the card's width; the wheel or the scroll bar shows older lines.
    m_logList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_logList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_logList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_logList->setResizeMode(QListView::Adjust);
    m_logList->setTextElideMode(Qt::ElideNone);
    logCard->layout()->addWidget(m_logList);
    left->addWidget(logCard, 8);
    splitter->addWidget(leftHost);

    // Right: the selected combatant. The name, hit points and hit row stay put;
    // each tab scrolls on its own.
    // In the initiative phase, with no one selected, the same place lists the
    // characters so each player's roll can be typed in.
    m_rightHost = new QWidget;
    m_rightHost->setObjectName(QStringLiteral("combatRight"));
    auto* right = new QVBoxLayout(m_rightHost);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(12);
    m_combatantForm = makeCard();
    m_combatantForm->setMinimumWidth(500);
    auto* card = static_cast<QVBoxLayout*>(m_combatantForm->layout());
    card->setSpacing(12);
    right->addWidget(m_combatantForm, 1);

    m_initiativeEntry = makeCard();
    m_initiativeEntry->setObjectName(QStringLiteral("initiativeEntry"));
    m_initiativeEntry->setMinimumWidth(500);
    {
        auto* entry = static_cast<QVBoxLayout*>(m_initiativeEntry->layout());
        entry->setSpacing(10);
        auto* entryHeader = new QHBoxLayout;
        entryHeader->addWidget(makeHeading(tr("Enter initiative")));
        entryHeader->addStretch(1);
        m_entryCount = makeMuted(QString());
        m_entryCount->setObjectName(QStringLiteral("initiativeEntryCount"));
        m_entryCount->setWordWrap(false);
        entryHeader->addWidget(m_entryCount);
        entry->addLayout(entryHeader);
        entry->addWidget(makeMuted(tr("Type each player's total: their d20 roll plus the bonus shown. Enter or Tab "
                                      "moves to the next character. Or let the app roll for them.")));
        auto* playersRow = new QHBoxLayout;
        playersRow->addWidget(m_rollPlayersButton);
        playersRow->addStretch(1);
        entry->addLayout(playersRow);
        auto* gridHost = new QWidget;
        m_entryGrid = new QGridLayout(gridHost);
        m_entryGrid->setContentsMargins(0, 4, 0, 4);
        m_entryGrid->setHorizontalSpacing(14);
        m_entryGrid->setVerticalSpacing(8);
        m_entryGrid->setColumnStretch(0, 1);
        entry->addWidget(gridHost);
        m_entryEmpty = makeMuted(tr("No characters are in this fight."));
        entry->addWidget(m_entryEmpty);

        // Monsters roll with their stat block's bonus.
        auto* monsterHeader = new QHBoxLayout;
        monsterHeader->addWidget(makeHeading(tr("Monsters")));
        monsterHeader->addStretch(1);
        m_entryMonsterCount = makeMuted(QString());
        m_entryMonsterCount->setObjectName(QStringLiteral("initiativeEntryMonsterCount"));
        m_entryMonsterCount->setWordWrap(false);
        monsterHeader->addWidget(m_entryMonsterCount);
        auto* monsterSection = new QVBoxLayout;
        monsterSection->setSpacing(10);
        monsterSection->addLayout(monsterHeader);
        auto* monstersRow = new QHBoxLayout;
        monstersRow->addWidget(m_rollAllButton);
        monstersRow->addStretch(1);
        monsterSection->addLayout(monstersRow);
        entry->addSpacing(6);
        entry->addWidget(sectionRule());
        entry->addSpacing(4);
        entry->addLayout(monsterSection);
        entry->addStretch(1);
        // Start combat is the button at the top of the page.
    }
    m_initiativeEntry->hide();
    right->addWidget(m_initiativeEntry, 1);

    leftHost->setMinimumWidth(380);
    splitter->addWidget(m_rightHost);
    // The turn order gets a little more than half, for its Status column.
    splitter->setStretchFactor(0, 52);
    splitter->setStretchFactor(1, 48);
    splitter->setSizes({624, 576});

    auto* nameRow = new QHBoxLayout;
    m_selectedName = new QLabel;
    m_selectedName->setObjectName(QStringLiteral("selectedName"));
    QFont nameFont = m_selectedName->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.35);
    nameFont.setWeight(QFont::DemiBold);
    m_selectedName->setFont(nameFont);
    // The creature's Armor Class, drawn inside a shield.
    m_selectedMeta = new QLabel;
    m_selectedMeta->setObjectName(QStringLiteral("acShield"));
    // Initiative sits on the Details tab, under its own heading: a typed total
    // for a character, a rolled one (with Reroll) for a monster, and the bonus.
    m_initiative = makeNumberBox(-99, 99);
    m_initiative->setObjectName(QStringLiteral("initiativeField"));
    m_initiative->setFixedWidth(56);
    m_rerollButton = new QPushButton(tr("Reroll"));
    m_rerollButton->setObjectName(QStringLiteral("rerollMonster"));
    makeQuiet(m_rerollButton);
    m_initiativeBonus = new QLabel;
    m_initiativeBonus->setObjectName(QStringLiteral("initiativeBonusValue"));
    m_initiativeBonus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_removeButton = new QPushButton(tr("Remove from fight"));
    m_removeButton->setObjectName(QStringLiteral("removeFromFight"));
    makeQuiet(m_removeButton);
    m_backToEntryButton = new QPushButton(tr("Initiative list"));
    m_backToEntryButton->setObjectName(QStringLiteral("showInitiativeEntry"));
    m_backToEntryButton->setToolTip(tr("Back to the list of characters' initiatives."));
    makeQuiet(m_backToEntryButton);
    nameRow->setSpacing(6);
    nameRow->addWidget(m_selectedName);
    nameRow->addSpacing(10);
    nameRow->addWidget(m_selectedMeta);
    nameRow->addSpacing(14);
    // Hit points sit next to the name (where Initiative was); Initiative is
    // on the Details tab.
    m_hp = new RelativeSpinBox;
    m_hp->setObjectName(QStringLiteral("hpField"));
    fitDigits(m_hp, 4, true);
    // Changes apply on Enter, so "-7" is read against the HP before it.
    m_hp->setKeyboardTracking(false);
    m_hp->setToolTip(tr("Type a new total, or a change: -7 takes 7 off, +5 adds 5. Press Enter."));
    // The maximum is in the turn order ("9 / 31") and the HP box's tooltip;
    // the card leaves it out to save space.
    m_maxHpLabel = new QLabel(this);
    m_maxHpLabel->hide();
    m_maxHpLabel->setObjectName(QStringLiteral("maxHpField"));
    m_maxHpLabel->setWordWrap(false);
    // Bloodied is a state, not a condition: shown here and in the Status column.
    m_bloodiedLabel = new QLabel(tr("Bloodied"));
    m_bloodiedLabel->setObjectName(QStringLiteral("bloodiedTag"));
    m_bloodiedLabel->setProperty("role", QStringLiteral("pill"));
    m_bloodiedLabel->setProperty("level", QStringLiteral("moderate"));
    m_bloodiedLabel->setToolTip(tr("Half its Hit Points or fewer. Not a condition, but Bloodied Fury, Rampage, and "
                                   "some damage check it."));
    m_bloodiedLabel->hide();
    m_tempHp = makeNumberBox(0, 9999);
    m_tempHp->setObjectName(QStringLiteral("tempHpField"));
    fitDigits(m_tempHp, 4);
    // Every level, so the GM picks one rather than typing it.
    m_exhaustion = new QComboBox;
    m_exhaustion->setObjectName(QStringLiteral("exhaustionField"));
    m_exhaustion->addItem(tr("None"), 0);
    for (int level = 1; level <= 5; ++level) {
        m_exhaustion->addItem(tr("Level %1").arg(level), level);
    }
    m_exhaustion->addItem(tr("Level 6 (dies)"), 6);
    m_exhaustionNote = makeMuted(QString());
    m_exhaustionNote->setObjectName(QStringLiteral("exhaustionEffects"));
    m_exhaustionNote->setWordWrap(true);

    nameRow->addWidget(makeMuted(tr("HP")));
    nameRow->addWidget(m_hp);
    // "Temp" over "HP" in small type, close to the HP box.
    nameRow->addSpacing(6);
    auto* tempLabel = makeMuted(tr("Temp\nHP"));
    tempLabel->setObjectName(QStringLiteral("tempHpLabel"));
    tempLabel->setWordWrap(false);
    tempLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont tempFont = tempLabel->font();
    tempFont.setPointSizeF(tempFont.pointSizeF() * 0.78);
    tempLabel->setFont(tempFont);
    nameRow->addWidget(tempLabel);
    nameRow->addWidget(m_tempHp);
    nameRow->addSpacing(8);
    nameRow->addWidget(m_bloodiedLabel);
    nameRow->addStretch(1);
    nameRow->addWidget(m_backToEntryButton);
    card->addLayout(nameRow);


    m_damageAmount = makeNumberBox(0, 999);
    fitThreeDigitAmount(m_damageAmount);
    m_damageAmount->setObjectName(QStringLiteral("damageAmount"));
    m_damageType = new QComboBox;
    m_damageType->setObjectName(QStringLiteral("damageType"));
    fillDamageTypes(m_damageType);
    m_damageCritical = new QCheckBox(tr("Critical"));
    m_damageCritical->setToolTip(tr("A critical hit on a dying character counts as two death save failures."));
    m_damageButton = new QPushButton(tr("Apply"));
    m_damageButton->setObjectName(QStringLiteral("applyDamage"));
    makeDanger(m_damageButton);
    m_healAmount = makeNumberBox(0, 999);
    fitThreeDigitAmount(m_healAmount);
    m_healAmount->setObjectName(QStringLiteral("healAmount"));
    m_healButton = new QPushButton(tr("Apply"));
    m_healButton->setObjectName(QStringLiteral("applyHealing"));
    makePrimary(m_healButton);

    // The Damage / Heal panel opens under the Turn order heading and acts on
    // every selected creature.
    m_hitPanel = new QFrame;
    m_hitPanel->setObjectName(QStringLiteral("hitPanel"));
    m_hitPanel->setProperty("role", QStringLiteral("tile"));
    auto* panel = new QVBoxLayout(m_hitPanel);
    panel->setContentsMargins(12, 10, 12, 10);
    panel->setSpacing(8);
    m_hitTargets = makeMuted(QString());
    m_hitTargets->setObjectName(QStringLiteral("hitTargets"));
    panel->addWidget(m_hitTargets);
    m_damageRow = new QWidget;
    auto* damageRow = new QHBoxLayout(m_damageRow);
    damageRow->setContentsMargins(0, 0, 0, 0);
    damageRow->setSpacing(8);
    damageRow->addWidget(m_damageAmount);
    damageRow->addWidget(m_damageType);
    damageRow->addWidget(m_damageCritical);
    damageRow->addStretch(1);
    damageRow->addWidget(m_damageButton);
    panel->addWidget(m_damageRow);
    m_savedHost = new QWidget;
    m_savedLayout = new QGridLayout(m_savedHost);
    m_savedLayout->setContentsMargins(0, 0, 0, 0);
    m_savedLayout->setHorizontalSpacing(14);
    m_savedLayout->setVerticalSpacing(2);
    panel->addWidget(m_savedHost);
    m_healRow = new QWidget;
    auto* healRow = new QHBoxLayout(m_healRow);
    healRow->setContentsMargins(0, 0, 0, 0);
    healRow->setSpacing(8);
    healRow->addWidget(m_healAmount);
    healRow->addStretch(1);
    healRow->addWidget(m_healButton);
    panel->addWidget(m_healRow);
    auto* closePanel = new QPushButton(tr("Close"));
    closePanel->setObjectName(QStringLiteral("closeHitPanel"));
    makeQuiet(closePanel);
    damageRow->addWidget(closePanel);
    auto* closeHeal = new QPushButton(tr("Close"));
    makeQuiet(closeHeal);
    healRow->addWidget(closeHeal);
    m_hitPanel->hide();
    static_cast<QVBoxLayout*>(m_initiativeList->parentWidget()->layout())->insertWidget(1, m_hitPanel);
    connect(closePanel, &QPushButton::clicked, m_hitPanel, &QWidget::hide);
    connect(closeHeal, &QPushButton::clicked, m_hitPanel, &QWidget::hide);
    connect(m_openDamageButton, &QPushButton::clicked, this, [this] { openHitPanel(false); });
    connect(m_openHealButton, &QPushButton::clicked, this, [this] { openHitPanel(true); });

    m_detailTabs = new QTabWidget;
    m_detailTabs->setObjectName(QStringLiteral("combatantTabs"));
    // Not document mode: that draws a tab-bar base over the corner button.
    card->addWidget(m_detailTabs, 1);

    // Actions tab: what the monster can do now, and how its attacks roll.
    auto* actionsPage = new QWidget;
    auto* actionsLayout = new QVBoxLayout(actionsPage);
    actionsLayout->setContentsMargins(0, 12, 10, 4);
    actionsLayout->setSpacing(10);
    auto* attackOptions = new QHBoxLayout;
    m_rollModeLabel = makeMuted(tr("Attack rolls"));
    m_rollModeLabel->setWordWrap(false);
    m_rollMode = new QComboBox;
    m_rollMode->setObjectName(QStringLiteral("rollMode"));
    m_rollMode->addItem(tr("Automatic"), 0);
    m_rollMode->addItem(tr("Straight roll"), 1);
    m_rollMode->addItem(tr("With Advantage"), 2);
    m_rollMode->addItem(tr("With Disadvantage"), 3);
    m_rollMode->setToolTip(tr("Automatic gives Advantage or Disadvantage from the rules: a Prone, Restrained, or "
                              "Invisible target, a Poisoned or Frightened attacker, and so on. The log says why. "
                              "Pick another to force it (cover, a spell the app doesn't track)."));
    attackOptions->addWidget(m_rollModeLabel);
    attackOptions->addWidget(m_rollMode);
    attackOptions->addStretch(1);
    actionsLayout->addLayout(attackOptions);
    m_economyHost = new QWidget;
    auto* economyRow = new QHBoxLayout(m_economyHost);
    economyRow->setContentsMargins(0, 0, 0, 0);
    m_actionUsed = new QCheckBox(tr("Action"));
    m_actionUsed->setObjectName(QStringLiteral("actionUsed"));
    m_bonusUsed = new QCheckBox(tr("Bonus action"));
    m_reactionUsed = new QCheckBox(tr("Reaction"));
    m_economyNote = makeMuted(QString());
    m_economyNote->setWordWrap(false);
    economyRow->addWidget(makeMuted(tr("Used:")));
    economyRow->addWidget(m_actionUsed);
    economyRow->addWidget(m_bonusUsed);
    economyRow->addWidget(m_reactionUsed);
    economyRow->addSpacing(8);
    economyRow->addWidget(m_economyNote);
    economyRow->addStretch(1);
    actionsLayout->addWidget(m_economyHost);
    m_actionsSection = new QWidget;
    m_actionsSection->setObjectName(QStringLiteral("combatantAttacks"));
    m_actionRows = new QVBoxLayout(m_actionsSection);
    m_actionRows->setContentsMargins(0, 0, 0, 0);
    m_actionRows->setSpacing(8);
    actionsLayout->addWidget(m_actionsSection);
    actionsLayout->addStretch(1);
    m_detailTabs->addTab(scrollingTab(actionsPage), tr("Actions"));

    // Conditions tab: conditions, their durations, and concentration.
    auto* conditionsPage = new QWidget;
    auto* conditionsLayout = new QVBoxLayout(conditionsPage);
    conditionsLayout->setContentsMargins(0, 12, 10, 4);
    conditionsLayout->setSpacing(8);
    // The condition tags sit on the card, under the HP and above the tabs.
    m_conditionChips = new QWidget;
    m_conditionChips->setObjectName(QStringLiteral("combatantConditions"));
    new FlowLayout(m_conditionChips, 6);
    m_conditionChips->hide();
    card->insertWidget(card->indexOf(m_detailTabs), m_conditionChips);

    // The selected tag's rules, at the top of the tab under its name.
    m_conditionHeading = makeHeading(QString());
    m_conditionHeading->setObjectName(QStringLiteral("conditionHeading"));
    conditionsLayout->addWidget(m_conditionHeading);
    // What gave it and how long it lasts, highlighted in the condition's colour.
    m_conditionDetail = new QLabel;
    m_conditionDetail->setObjectName(QStringLiteral("conditionDetail"));
    m_conditionDetail->setWordWrap(true);
    m_conditionDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_conditionDetail->hide();
    conditionsLayout->addWidget(m_conditionDetail);
    m_conditionText = makeMuted(QString());
    m_conditionText->setObjectName(QStringLiteral("conditionText"));
    m_conditionText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    conditionsLayout->addWidget(m_conditionText);
    // The line under the rules shows only while there are rules above it.
    m_conditionRule = sectionRule();
    conditionsLayout->addSpacing(4);
    conditionsLayout->addWidget(m_conditionRule);
    conditionsLayout->addSpacing(2);
    conditionsLayout->addWidget(makeHeading(tr("Add Condition")));
    m_conditionPicker = new QComboBox;
    m_conditionPicker->setObjectName(QStringLiteral("conditionPicker"));
    for (const Condition& condition : searchConditions(m_conditions, "")) {
        m_conditionPicker->addItem(QString::fromStdString(condition.name), QString::fromStdString(condition.id));
    }
    m_durationKind = new QComboBox;
    m_durationKind->setObjectName(QStringLiteral("durationKind"));
    m_durationKind->addItem(tr("until removed"));
    m_durationKind->addItem(tr("until the start of"));
    m_durationKind->addItem(tr("until the end of"));
    m_durationAnchor = new QComboBox;
    m_durationAnchor->setObjectName(QStringLiteral("durationAnchor"));
    m_durationTurns = makeNumberBox(1, 100);
    m_durationTurns->setSuffix(tr(" turn(s)"));
    m_durationTurns->setToolTip(tr("1 is \"next turn\". 10 is one minute."));
    auto* addConditionButton = new QPushButton(tr("Add"));
    addConditionButton->setObjectName(QStringLiteral("addCondition"));
    makePrimary(addConditionButton);
    auto* addRow = new QHBoxLayout;
    addRow->addWidget(m_conditionPicker);
    // What made the creature Invisible decides what ends it.
    m_invisibleCause = new QComboBox;
    m_invisibleCause->setObjectName(QStringLiteral("invisibleCause"));
    m_invisibleCause->addItem(tr("from Hide"));
    m_invisibleCause->addItem(tr("from the Invisibility spell"));
    m_invisibleCause->addItem(tr("until removed"));
    m_invisibleCause->setItemData(0, tr("Ends on an attack roll, a saving-throw effect, or a spell with a Verbal "
                                        "component. Remove it by hand when the creature is found or makes noise."),
                                  Qt::ToolTipRole);
    m_invisibleCause->setItemData(1, tr("Concentration. Ends on an attack roll, dealing damage, or casting a spell."),
                                  Qt::ToolTipRole);
    m_invisibleCause->setItemData(2, tr("Nothing ends it but you."), Qt::ToolTipRole);
    m_invisibleCause->hide();
    addRow->addWidget(m_invisibleCause);
    addRow->addWidget(m_durationKind);
    addRow->addWidget(m_durationAnchor, 1);
    addRow->addWidget(m_durationTurns);
    addRow->addStretch(0);
    conditionsLayout->addLayout(addRow);
    m_saveEnds = new QCheckBox(tr("Ends on a save at the end of each of its turns"));
    m_saveEndsAbility = new QComboBox;
    for (const Ability ability : kAbilityOrder) {
        m_saveEndsAbility->addItem(QString::fromLatin1(abilityLabel(ability)));
    }
    m_saveEndsAbility->setCurrentIndex(static_cast<int>(Ability::Constitution));
    m_saveEndsDc = makeNumberBox(1, 40);
    m_saveEndsDc->setPrefix(tr("DC "));
    m_saveEndsDc->setValue(13);
    auto* saveEndsRow = new QHBoxLayout;
    saveEndsRow->addWidget(m_saveEnds);
    saveEndsRow->addWidget(m_saveEndsAbility);
    saveEndsRow->addWidget(m_saveEndsDc);
    saveEndsRow->addStretch(1);
    conditionsLayout->addLayout(saveEndsRow);
    // Add sits on the left, under the choices it adds.
    auto* addButtonRow = new QHBoxLayout;
    addButtonRow->addWidget(addConditionButton);
    addButtonRow->addStretch(1);
    conditionsLayout->addLayout(addButtonRow);

    // Exhaustion is a condition with levels, so it sits here too.
    addSectionHeading(conditionsLayout, makeHeading(tr("Exhaustion")));
    auto* exhaustionRow = new QHBoxLayout;
    exhaustionRow->setSpacing(10);
    exhaustionRow->addWidget(m_exhaustion);
    exhaustionRow->addWidget(m_exhaustionNote, 1);
    conditionsLayout->addLayout(exhaustionRow);

    conditionsLayout->addSpacing(4);
    conditionsLayout->addWidget(sectionRule());
    conditionsLayout->addSpacing(2);
    auto* concentrationHeader = new QHBoxLayout;
    concentrationHeader->addWidget(makeHeading(tr("Concentration")));
    m_concentrationLabel = makeMuted(QString());
    m_concentrationLabel->setObjectName(QStringLiteral("concentrationLabel"));
    concentrationHeader->addWidget(m_concentrationLabel, 1);
    conditionsLayout->addLayout(concentrationHeader);
    auto* concentrationRow = new QHBoxLayout;
    m_spellSearch = new QLineEdit;
    m_spellSearch->setObjectName(QStringLiteral("concentrationSearch"));
    m_spellSearch->setPlaceholderText(tr("Search concentration spells"));
    m_spellSearch->setClearButtonEnabled(true);
    auto* setConcentrationButton = new QPushButton(tr("Concentrate"));
    setConcentrationButton->setObjectName(QStringLiteral("setConcentration"));
    concentrationRow->addWidget(m_spellSearch, 1);
    concentrationRow->addWidget(setConcentrationButton);
    conditionsLayout->addLayout(concentrationRow);
    m_spellMatches = new QListWidget;
    m_spellMatches->setProperty("inset", true);
    m_spellMatches->setObjectName(QStringLiteral("concentrationMatches"));
    m_spellMatches->setMaximumHeight(96);
    conditionsLayout->addWidget(m_spellMatches);
    // End sits at the foot of the section, on the left, like Add above.
    auto* clearConcentrationButton = new QPushButton(tr("End"));
    clearConcentrationButton->setObjectName(QStringLiteral("clearConcentration"));
    makePrimary(clearConcentrationButton);
    auto* endRow = new QHBoxLayout;
    endRow->addWidget(clearConcentrationButton);
    endRow->addStretch(1);
    conditionsLayout->addLayout(endRow);
    conditionsLayout->addStretch(1);
    m_detailTabs->addTab(scrollingTab(conditionsPage), tr("Conditions"));

    // Details tab: ability scores, quick facts, defenses, and spell slots,
    // rebuilt for each combatant (rebuildDetails).
    // The Initiative section stays put; everything under it is rebuilt.
    auto* detailsPage = new QWidget;
    detailsPage->setObjectName(QStringLiteral("combatantDetails"));
    auto* detailsOuter = new QVBoxLayout(detailsPage);
    detailsOuter->setContentsMargins(0, 14, 10, 6);
    detailsOuter->setSpacing(14);
    {
        auto* initiativeSection = new QWidget;
        initiativeSection->setObjectName(QStringLiteral("initiativeSection"));
        auto* section = new QVBoxLayout(initiativeSection);
        section->setContentsMargins(0, 0, 0, 0);
        section->setSpacing(6);
        section->addWidget(makeHeading(tr("Initiative")));
        auto* initiativeRow = new QHBoxLayout;
        initiativeRow->setSpacing(8);
        initiativeRow->addWidget(makeMuted(tr("Current")));
        initiativeRow->addWidget(m_initiative);
        initiativeRow->addSpacing(20);
        initiativeRow->addWidget(makeMuted(tr("Bonus")));
        initiativeRow->addWidget(m_initiativeBonus);
        initiativeRow->addSpacing(20);
        initiativeRow->addWidget(m_rerollButton);
        initiativeRow->addStretch(1);
        section->addLayout(initiativeRow);
        detailsOuter->addWidget(initiativeSection);
        detailsOuter->addWidget(sectionRule());
    }
    m_detailsLayout = new QVBoxLayout;
    m_detailsLayout->setContentsMargins(0, 0, 0, 0);
    m_detailsLayout->setSpacing(14);
    detailsOuter->addLayout(m_detailsLayout, 1);
    m_acLabel = new QLabel(this);
    m_acLabel->setObjectName(QStringLiteral("acField"));
    m_acLabel->hide();

    auto* deathFrame = new QFrame;
    deathFrame->setProperty("role", QStringLiteral("alert"));
    m_deathSavesHost = deathFrame;
    auto* deathRow = new QHBoxLayout(m_deathSavesHost);
    deathRow->setContentsMargins(12, 8, 12, 8);
    m_deathStatus = new QLabel;
    m_deathStatus->setObjectName(QStringLiteral("deathStatus"));
    auto* successUp = new QPushButton(tr("+ success"));
    successUp->setObjectName(QStringLiteral("deathSuccessUp"));
    auto* failureUp = new QPushButton(tr("+ failure"));
    failureUp->setObjectName(QStringLiteral("deathFailureUp"));
    auto* successDown = new QPushButton(tr("− success"));
    auto* failureDown = new QPushButton(tr("− failure"));
    for (QPushButton* button : {successUp, failureUp, successDown, failureDown}) {
        makeQuiet(button);
    }
    m_stabilizeButton = new QPushButton(tr("Stabilize"));
    m_stabilizeButton->setToolTip(tr("Spare the Dying, or a DC 10 Wisdom (Medicine) check."));
    auto* deathColumn = new QVBoxLayout;
    deathColumn->setSpacing(4);
    auto* deathTop = new QHBoxLayout;
    deathTop->addWidget(m_deathStatus, 1);
    deathTop->addWidget(m_stabilizeButton);
    m_deathSaveButtons = new QWidget;
    m_deathSaveButtons->setObjectName(QStringLiteral("deathSaveButtons"));
    auto* deathBottom = new QHBoxLayout(m_deathSaveButtons);
    deathBottom->setContentsMargins(0, 0, 0, 0);
    deathBottom->addWidget(successUp);
    deathBottom->addWidget(successDown);
    deathBottom->addWidget(failureUp);
    deathBottom->addWidget(failureDown);
    deathBottom->addStretch(1);
    deathColumn->addLayout(deathTop);
    deathColumn->addWidget(m_deathSaveButtons);
    deathRow->addLayout(deathColumn);
    // Death saves sit on the card, under the hit row, while a character is at 0 HP.
    card->insertWidget(card->indexOf(m_detailTabs), m_deathSavesHost);

    m_detailTabs->addTab(scrollingTab(detailsPage), tr("Details"));
    // Remove from fight sits at the far end of the tab row.
    auto* removeHost = new QWidget;
    removeHost->setObjectName(QStringLiteral("removeFromFightHost"));
    auto* removeLayout = new QHBoxLayout(removeHost);
    removeLayout->setContentsMargins(0, 0, 0, 2);
    removeLayout->addWidget(m_removeButton);
    m_detailTabs->setCornerWidget(removeHost, Qt::TopRightCorner);
    connect(m_detailTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (!m_cardCombatantId.empty()) {
            m_tabByCombatant[m_cardCombatantId] = index;
        }
    });

    connect(m_conditionPicker, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_invisibleCause->setVisible(m_conditionPicker->currentData().toString() == QStringLiteral("invisible"));
    });
    connect(m_encounterCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &CombatPage::showEncounter);
    connect(m_spellMatches, &QListWidget::itemDoubleClicked, this, [this] { setSelectedConcentration(); });
    connect(m_initiativeList, &QTreeWidget::itemClicked, this, &CombatPage::onTargetClicked);
    connect(m_downList, &QTreeWidget::itemClicked, this, &CombatPage::onTargetClicked);
    // Ctrl-click or Shift-click selects several creatures (for Damage and
    // Heal); a plain click selects one, across both lists.
    m_initiativeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_downList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto adding = [] {
        return (QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) != 0;
    };
    connect(m_initiativeList, &QTreeWidget::currentItemChanged, this, [this, adding](QTreeWidgetItem* current) {
        if (m_populating || m_armed.has_value()) {
            return;  // choosing targets: the card stays on the action's user
        }
        if (current != nullptr) {
            const QSignalBlocker blocker(m_downList);
            m_downList->setCurrentItem(nullptr);
            if (!adding()) {
                m_downList->clearSelection();
            }
        }
        showCombatant();
    });
    connect(m_downList, &QTreeWidget::currentItemChanged, this, [this, adding](QTreeWidgetItem* current) {
        if (m_populating || m_armed.has_value()) {
            return;  // choosing targets: the card stays on the action's user
        }
        if (current != nullptr) {
            const QSignalBlocker blocker(m_initiativeList);
            m_initiativeList->setCurrentItem(nullptr);
            if (!adding()) {
                m_initiativeList->clearSelection();
            }
        }
        showCombatant();
    });
    for (QTreeWidget* list : {m_initiativeList, m_downList}) {
        connect(list, &QTreeWidget::itemSelectionChanged, this, [this] {
            if (m_hitPanel->isVisible()) {
                refreshHitPanel();
            }
        });
    }
    connect(m_initiative, &QSpinBox::valueChanged, this, &CombatPage::onInitiativeChanged);
    connect(m_initiative, &QSpinBox::editingFinished, this, &CombatPage::onInitiativeEditingFinished);
    connect(m_hp, &QSpinBox::valueChanged, this, &CombatPage::onHpChanged);
    connect(m_tempHp, &QSpinBox::valueChanged, this, &CombatPage::onTempHpChanged);
    connect(m_exhaustion, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) {
            onExhaustionChanged(m_exhaustion->itemData(index).toInt());
        }
    });
    for (QCheckBox* box : {m_actionUsed, m_bonusUsed, m_reactionUsed}) {
        connect(box, &QCheckBox::toggled, this, &CombatPage::onEconomyToggled);
    }
    connect(m_damageButton, &QPushButton::clicked, this, &CombatPage::applySelectedDamage);
    connect(m_healButton, &QPushButton::clicked, this, &CombatPage::applySelectedHealing);
    connect(addConditionButton, &QPushButton::clicked, this, &CombatPage::addSelectedCondition);
    connect(m_durationKind, &QComboBox::currentIndexChanged, this, [this](int index) {
        // Whose turn and how many only mean something for a timed duration.
        m_durationAnchor->setVisible(index > 0);
        m_durationTurns->setVisible(index > 0);
    });
    m_durationAnchor->hide();
    m_durationTurns->hide();
    connect(m_saveEnds, &QCheckBox::toggled, this, [this](bool on) {
        m_saveEndsAbility->setEnabled(on);
        m_saveEndsDc->setEnabled(on);
    });
    m_saveEndsAbility->setEnabled(false);
    m_saveEndsDc->setEnabled(false);
    connect(m_spellSearch, &QLineEdit::textChanged, this, &CombatPage::refreshConcentrationChoices);
    connect(setConcentrationButton, &QPushButton::clicked, this, &CombatPage::setSelectedConcentration);
    connect(clearConcentrationButton, &QPushButton::clicked, this, &CombatPage::clearSelectedConcentration);
    connect(successUp, &QPushButton::clicked, this, [this] { adjustSelectedDeathSave(true, 1); });
    connect(successDown, &QPushButton::clicked, this, [this] { adjustSelectedDeathSave(true, -1); });
    connect(failureUp, &QPushButton::clicked, this, [this] { adjustSelectedDeathSave(false, 1); });
    connect(failureDown, &QPushButton::clicked, this, [this] { adjustSelectedDeathSave(false, -1); });
    connect(m_stabilizeButton, &QPushButton::clicked, this, &CombatPage::stabilizeSelected);
    connect(m_rollAllButton, &QPushButton::clicked, this, &CombatPage::rollAll);
    connect(m_rollPlayersButton, &QPushButton::clicked, this, &CombatPage::rollPlayers);
    connect(m_rerollButton, &QPushButton::clicked, this, &CombatPage::rerollSelected);
    connect(m_removeButton, &QPushButton::clicked, this, &CombatPage::removeSelected);
    connect(m_nextTurnButton, &QPushButton::clicked, this, [this] { nextTurn(); });
    connect(m_backToEntryButton, &QPushButton::clicked, this, &CombatPage::showInitiativeEntry);
    connect(m_undoButton, &QPushButton::clicked, this, &CombatPage::undoLastChange);
    auto* cancelAttack = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    cancelAttack->setContext(Qt::WindowShortcut);
    connect(cancelAttack, &QShortcut::activated, this, [this] {
        if (m_armed.has_value()) {
            addLog(tr("%1 stops choosing targets.").arg(nameOf(m_armed->attackerId)));
            stopTargeting();
        }
    });
    auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, &CombatPage::undoLastChange);

    refreshConcentrationChoices(QString());

    if (hasLoadError()) {
        m_encounterCombo->setEnabled(false);
        m_emptyHint->hide();
        m_fight->hide();
        return;
    }

    reloadCharacters();
    reloadEncounters();
}

// --- Loading and saving -------------------------------------------------------

void CombatPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!hasLoadError()) {
        reloadCharacters();
        reloadEncounters();
        restoreHistory();
    }
    if (m_reportedLoadError || !hasLoadError()) {
        return;
    }
    m_reportedLoadError = true;
    QTimer::singleShot(0, this, [this] {
        QMessageBox::critical(this, tr("Could not read encounters"),
                              tr("%1\n\nThe file has been left unchanged. Encounters cannot be edited until it "
                                 "is fixed or moved aside.")
                                  .arg(m_loadError));
    });
}

void CombatPage::hideEvent(QHideEvent* event)
{
    disarmAttack();
    flushPendingSave();
    saveHistory();
    QWidget::hideEvent(event);
}

// --- History between runs --------------------------------------------------------

namespace {

HistoryPrompt toHistory(const CombatPage::Prompt& prompt)
{
    HistoryPrompt row;
    row.kind = static_cast<int>(prompt.kind);
    row.combatantId = prompt.combatantId;
    row.conditionId = prompt.conditionId;
    row.sourceId = prompt.sourceId;
    row.auraName = prompt.auraName;
    row.ability = static_cast<int>(prompt.ability);
    row.dc = prompt.dc;
    row.attack = prompt.attack;
    row.riders = prompt.riders;
    row.refund = prompt.refund;
    for (const TypedDamage& part : prompt.damage) {
        row.damage.emplace_back(part.amount, part.type);
    }
    row.advantage = prompt.advantage;
    row.afterHit = prompt.afterHit;
    row.wasBloodied = prompt.wasBloodied;
    return row;
}

std::optional<CombatPage::Prompt> fromHistory(const HistoryPrompt& row)
{
    if (row.kind < 0 || row.kind > static_cast<int>(CombatPage::Prompt::Kind::Escape) || row.ability < 0 ||
        row.ability >= static_cast<int>(kAbilityOrder.size())) {
        return std::nullopt;
    }
    CombatPage::Prompt prompt;
    prompt.kind = static_cast<CombatPage::Prompt::Kind>(row.kind);
    prompt.combatantId = row.combatantId;
    prompt.conditionId = row.conditionId;
    prompt.sourceId = row.sourceId;
    prompt.auraName = row.auraName;
    prompt.ability = static_cast<Ability>(row.ability);
    prompt.dc = row.dc;
    prompt.attack = row.attack;
    prompt.riders = row.riders;
    prompt.refund = row.refund;
    for (const auto& [amount, type] : row.damage) {
        prompt.damage.push_back(TypedDamage{amount, type});
    }
    prompt.advantage = row.advantage;
    prompt.afterHit = row.afterHit;
    prompt.wasBloodied = row.wasBloodied;
    if (prompt.kind == CombatPage::Prompt::Kind::ActionSave &&
        (!prompt.attack.has_value() || (prompt.afterHit ? !prompt.attack->riderSave : !prompt.attack->save))) {
        return std::nullopt;
    }
    return prompt;
}

std::vector<std::string> toLines(const QStringList& log)
{
    std::vector<std::string> lines;
    for (const QString& line : log) {
        lines.push_back(line.toStdString());
    }
    return lines;
}

QStringList fromLines(const std::vector<std::string>& lines)
{
    QStringList log;
    for (const std::string& line : lines) {
        log << QString::fromStdString(line);
    }
    return log;
}

}  // namespace

void CombatPage::setHistoryFile(const QString& path)
{
    m_historyFile = path;
    if (isVisible()) {
        restoreHistory();
    }
}

void CombatPage::saveHistory()
{
    // Only once the saved history has been read, so an early save cannot
    // write over it.
    if (m_historyFile.isEmpty() || !m_historyRestored || hasLoadError()) {
        return;
    }
    flushPendingSave();
    FightHistory history;
    history.roster = m_characters;
    std::vector<std::string> ids{m_shownEncounterId};
    for (const PageUndo& step : m_undo) {
        if (std::find(ids.begin(), ids.end(), step.encounterId) == ids.end()) {
            ids.push_back(step.encounterId);
        }
    }
    for (const Encounter& encounter : m_encounters) {
        if (std::find(ids.begin(), ids.end(), encounter.id) != ids.end()) {
            history.encounters.push_back(encounter);
        }
    }
    history.shownEncounterId = m_shownEncounterId;
    history.log = toLines(m_log);
    for (const Prompt& prompt : m_prompts) {
        history.prompts.push_back(toHistory(prompt));
    }
    for (const PageUndo& undo : m_undo) {
        HistoryStep step;
        step.encounterId = undo.encounterId;
        step.encounter = undo.encounter;
        step.roster = undo.roster;
        step.log = toLines(undo.log);
        for (const Prompt& prompt : undo.prompts) {
            step.prompts.push_back(toHistory(prompt));
        }
        step.selectionId = undo.selectionId;
        history.steps.push_back(std::move(step));
    }
    try {
        saveHistoryFile(std::filesystem::path(m_historyFile.toStdU16String()), history);
    } catch (const std::exception&) {
        // Undo between runs is a convenience; a failed write loses only that.
    }
}

void CombatPage::restoreHistory()
{
    if (m_historyRestored || m_historyFile.isEmpty() || hasLoadError()) {
        return;
    }
    m_historyRestored = true;
    const std::optional<FightHistory> history =
        loadHistoryFile(std::filesystem::path(m_historyFile.toStdU16String()));
    if (!history.has_value()) {
        return;
    }
    // Only what still matches the files: an encounter changed since (in
    // another copy of the app, or by hand) loses its steps, and a changed
    // roster loses them all.
    const auto unchanged = [this, &history](const std::string& id) {
        const Encounter* now = nullptr;
        for (const Encounter& encounter : m_encounters) {
            now = encounter.id == id ? &encounter : now;
        }
        for (const Encounter& saved : history->encounters) {
            if (saved.id == id) {
                return now != nullptr && saved == *now;
            }
        }
        return false;
    };
    const bool rosterSame = history->roster == m_characters;
    std::vector<PageUndo> steps;
    for (const HistoryStep& step : history->steps) {
        if (!rosterSame || !unchanged(step.encounterId)) {
            continue;
        }
        PageUndo undo;
        undo.encounterId = step.encounterId;
        undo.encounter = step.encounter;
        undo.roster = step.roster;
        undo.log = fromLines(step.log);
        for (const HistoryPrompt& row : step.prompts) {
            if (const std::optional<Prompt> prompt = fromHistory(row)) {
                undo.prompts.push_back(*prompt);
            }
        }
        undo.selectionId = step.selectionId;
        steps.push_back(std::move(undo));
    }
    // Steps from this run (none yet, normally) come after the saved ones.
    for (PageUndo& undo : m_undo) {
        steps.push_back(std::move(undo));
    }
    while (steps.size() > kUndoLimit) {
        steps.erase(steps.begin());
    }
    m_undo = std::move(steps);
    m_undoButton->setEnabled(!m_undo.empty());
    if (history->shownEncounterId == m_shownEncounterId && unchanged(m_shownEncounterId)) {
        m_log = fromLines(history->log);
        m_prompts.clear();
        for (const HistoryPrompt& row : history->prompts) {
            if (const std::optional<Prompt> prompt = fromHistory(row)) {
                m_prompts.push_back(*prompt);
            }
        }
        showLog();
        rebuildPrompts();
    }
}

void CombatPage::reloadEncounters()
{
    if (hasLoadError() || m_encounterCombo == nullptr) {
        return;
    }
    flushPendingSave();
    QString selectedId = m_encounterCombo->currentData().toString();
    if (selectedId.isEmpty()) {
        // Opening the app: back to the encounter used last time.
        selectedId = readLastEncounter(m_stateFile);
    }
    std::vector<Encounter> loaded;
    try {
        loaded = m_encountersStore.loadAll();
    } catch (const EncounterStoreError& error) {
        QMessageBox::warning(this, tr("Could not read encounters"), QString::fromStdString(error.what()));
        return;
    }
    // An encounter put back into the initiative phase elsewhere (Reset in
    // Encounter Builder) opens on the initiative list.
    bool backToEntry = false;
    for (const Encounter& fresh : loaded) {
        if (fresh.id != selectedId.toStdString() || fresh.started) {
            continue;
        }
        for (const Encounter& old : m_encounters) {
            backToEntry = backToEntry || (old.id == fresh.id && old != fresh);
        }
    }
    if (loaded != m_encounters) {
        // Changed on another page: undo would restore a fight that no longer
        // matches the file.
        m_undo.clear();
    }
    m_encounters = std::move(loaded);
    if (syncSnapshots()) {
        persistEncounters();
    }
    int select = -1;
    {
        const QSignalBlocker blocker(m_encounterCombo);
        m_encounterCombo->clear();
        for (int i = 0; i < static_cast<int>(m_encounters.size()); ++i) {
            const Encounter& encounter = m_encounters[static_cast<std::size_t>(i)];
            const QString id = QString::fromStdString(encounter.id);
            m_encounterCombo->addItem(QString::fromStdString(encounter.name), id);
            if (!selectedId.isEmpty() && id == selectedId) {
                select = i;
            }
        }
        if (select < 0 && !m_encounters.empty()) {
            select = 0;
        }
        if (select >= 0) {
            m_encounterCombo->setCurrentIndex(select);
        }
    }
    showEncounter();
    if (backToEntry) {
        showInitiativeEntry();
    }
}

void CombatPage::reloadCharacters()
{
    try {
        std::vector<Character> loaded = m_charactersStore.loadAll();
        if (loaded != m_characters) {
            m_undo.clear();
        }
        m_characters = std::move(loaded);
    } catch (const CharacterStoreError&) {
        // The Characters page reports the error. Keep the last good copy, and
        // never write it back over a file that could not be read.
    }
}

// Character rows follow their sheets; monster rows saved before stat blocks
// were kept get one from the catalog, and older SRD copies get newer rules.
bool CombatPage::syncSnapshots()
{
    bool changed = false;
    for (Encounter& encounter : m_encounters) {
        for (Combatant& combatant : encounter.combatants) {
            if (isCharacterCombatant(combatant)) {
                if (const Character* character = characterFor(combatant)) {
                    changed = refreshCharacterCombatant(combatant, *character) || changed;
                }
            } else if (const auto monster = m_catalog.findById(combatant.sourceId)) {
                // Missing stat blocks, and SRD rules an older copy lacks.
                changed = fillMonsterSnapshot(combatant, *monster) || changed;
            }
        }
        changed = assignMonsterCopyNames(encounter.combatants) || changed;
    }
    return changed;
}

void CombatPage::persistEncounters()
{
    m_savePending = false;
    m_saveTimer->stop();
    if (hasLoadError()) {
        return;
    }
    try {
        m_encountersStore.saveAll(m_encounters);
    } catch (const EncounterStoreError& error) {
        QMessageBox::warning(this, tr("Could not save encounters"), QString::fromStdString(error.what()));
    }
}

void CombatPage::persistSoon()
{
    if (hasLoadError()) {
        return;
    }
    m_savePending = true;
    m_saveTimer->start();
}

void CombatPage::flushPendingSave()
{
    if (m_savePending) {
        persistEncounters();
    }
}

void CombatPage::saveCharacters()
{
    try {
        m_charactersStore.saveAll(m_characters);
    } catch (const CharacterStoreError& error) {
        QMessageBox::warning(this, tr("Could not save characters"), QString::fromStdString(error.what()));
    }
}

void CombatPage::carryToSheet(const Combatant& combatant)
{
    if (!isCharacterCombatant(combatant)) {
        return;
    }
    carryCharacterHitPoints(m_characters, combatant);
}

// --- Undo -----------------------------------------------------------------------

CombatPage::PageUndo CombatPage::capture() const
{
    PageUndo undo;
    const int row = m_encounterCombo->currentIndex();
    if (row >= 0 && row < static_cast<int>(m_encounters.size())) {
        undo.encounter = m_encounters[static_cast<std::size_t>(row)];
        undo.encounterId = undo.encounter.id;
    }
    undo.roster = m_characters;
    undo.log = m_log;
    undo.prompts = m_prompts;
    if (m_armed.has_value()) {
        undo.selectionId = m_armed->attackerId;  // choosing targets: the user is selected
    } else if (const QTreeWidgetItem* item = m_initiativeList->currentItem() != nullptr
                                                 ? m_initiativeList->currentItem()
                                                 : m_downList->currentItem()) {
        undo.selectionId = item->data(0, Qt::UserRole).toString().toStdString();
    }
    return undo;
}

void CombatPage::commit(PageUndo before, EditKind kind, const std::string& selectId, bool saveSoon)
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    // A grapple ends when the grappler falls; Restrained "until the grapple
    // ends" ends with it.
    releaseEndedConditions();
    const bool fightChanged = before.encounter != *encounter;
    const bool sheetsChanged = before.roster != m_characters;
    // Answering a question is a step too, so Undo asks it again.
    const bool promptsChanged = before.prompts != m_prompts;
    if (fightChanged || sheetsChanged || promptsChanged) {
        const bool merge = kind != EditKind::Once && kind == m_lastEdit && !m_undo.empty() &&
                           m_undo.back().encounterId == before.encounterId;
        if (!merge) {
            m_undo.push_back(std::move(before));
            if (m_undo.size() > kUndoLimit) {
                m_undo.erase(m_undo.begin());
            }
        }
    }
    m_lastEdit = kind;
    m_undoButton->setEnabled(!m_undo.empty());
    if (sheetsChanged) {
        saveCharacters();
    }
    if (fightChanged) {
        if (saveSoon) {
            persistSoon();
        } else {
            persistEncounters();
        }
    }
    if (kind == EditKind::Once) {
        rebuildCombatantList(selectId);
    }
}

void CombatPage::undoLastChange()
{
    disarmAttack();
    flushPendingSave();
    if (m_undo.empty()) {
        return;
    }
    PageUndo undo = std::move(m_undo.back());
    m_undo.pop_back();
    m_lastEdit = EditKind::Once;
    bool unstarted = false;  // Undo of Start combat: back to the initiative list
    for (Encounter& encounter : m_encounters) {
        if (encounter.id == undo.encounterId) {
            unstarted = encounter.started && !undo.encounter.started;
            encounter = undo.encounter;
        }
    }
    const bool sheets = undo.roster != m_characters;
    m_characters = undo.roster;
    // The log and the open checks go back with the fight: lines and prompts
    // from the undone step disappear.
    m_log = undo.log;
    m_prompts = undo.prompts;
    m_prompts.erase(std::remove_if(m_prompts.begin(), m_prompts.end(),
                                   [this](const Prompt& prompt) { return combatantById(prompt.combatantId) == nullptr; }),
                    m_prompts.end());
    showLog();
    rebuildPrompts();
    m_hitLabel->clear();
    if (sheets) {
        saveCharacters();
    }
    persistEncounters();
    m_undoButton->setEnabled(!m_undo.empty());
    const int row = m_encounterCombo->findData(QString::fromStdString(undo.encounterId));
    if (row >= 0 && row != m_encounterCombo->currentIndex()) {
        m_encounterCombo->setCurrentIndex(row);
    }
    rebuildCombatantList(unstarted ? std::string() : undo.selectionId);
    if (unstarted) {
        showInitiativeEntry();
    }
}

// --- Lookups -----------------------------------------------------------------

int CombatPage::rollD20()
{
    return rollDie(20);
}

int CombatPage::rollDie(int sides)
{
    std::uniform_int_distribution<int> face(1, std::max(1, sides));
    return face(m_dice);
}

RollDie CombatPage::dieRoller()
{
    return [this](int sides) { return rollDie(sides); };
}

Encounter* CombatPage::selectedEncounter()
{
    if (m_encounterCombo == nullptr) {
        return nullptr;
    }
    const int row = m_encounterCombo->currentIndex();
    if (row < 0 || row >= static_cast<int>(m_encounters.size())) {
        return nullptr;
    }
    return &m_encounters[static_cast<std::size_t>(row)];
}

Combatant* CombatPage::combatantById(const std::string& id)
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return nullptr;
    }
    const int index = indexOfId(*encounter, id);
    return index < 0 ? nullptr : &encounter->combatants[static_cast<std::size_t>(index)];
}

Combatant* CombatPage::selectedCombatant()
{
    // While an action picks its targets, the card stays on the creature
    // using it; clicked rows are its targets, not a new selection.
    if (m_armed.has_value()) {
        if (Combatant* user = combatantById(m_armed->attackerId)) {
            return user;
        }
    }
    QTreeWidgetItem* item = m_initiativeList->currentItem();
    if (item == nullptr) {
        item = m_downList->currentItem();
    }
    if (item == nullptr) {
        return nullptr;
    }
    return combatantById(item->data(0, Qt::UserRole).toString().toStdString());
}

Character* CombatPage::characterFor(const Combatant& combatant)
{
    if (!isCharacterCombatant(combatant)) {
        return nullptr;
    }
    for (Character& character : m_characters) {
        if (character.id == combatant.sourceId) {
            return &character;
        }
    }
    return nullptr;
}

std::string CombatPage::currentTurnId()
{
    const Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return {};
    }
    const int turn = encounter->turnIndex;
    if (!encounter->started || turn < 0 || turn >= static_cast<int>(encounter->combatants.size())) {
        return {};  // the initiative phase: nobody's turn yet
    }
    const Combatant& combatant = encounter->combatants[static_cast<std::size_t>(turn)];
    return isInInitiative(combatant) ? combatant.id : std::string();
}

bool CombatPage::isTheirTurn(const Combatant& combatant)
{
    return currentTurnId() == combatant.id;
}

QString CombatPage::nameOf(const std::string& combatantId)
{
    const Combatant* combatant = combatantById(combatantId);
    return combatant == nullptr ? tr("someone") : QString::fromStdString(combatant->name);
}

QString CombatPage::conditionName(const std::string& id) const
{
    const auto condition = findConditionById(m_conditions, id);
    return condition.has_value() ? QString::fromStdString(condition->name) : QString::fromStdString(id);
}

void CombatPage::addLog(const QString& line)
{
    const Encounter* encounter =
        m_encounterCombo->currentIndex() >= 0 && m_encounterCombo->currentIndex() < static_cast<int>(m_encounters.size())
            ? &m_encounters[static_cast<std::size_t>(m_encounterCombo->currentIndex())]
            : nullptr;
    const QString stamped = encounter == nullptr ? line : tr("R%1  %2").arg(encounter->round).arg(line);
    m_log.prepend(stamped);
    while (m_log.size() > kLogLimit) {
        m_log.removeLast();
    }
    showLog();
    m_hitLabel->setText(line);
}

void CombatPage::showLog()
{
    m_logList->clear();
    m_logList->addItems(m_log);
}

// --- Showing the fight -------------------------------------------------------

void CombatPage::setStateFile(const QString& path)
{
    m_stateFile = path;
    const int row = m_encounterCombo->findData(readLastEncounter(path));
    if (row >= 0 && row != m_encounterCombo->currentIndex()) {
        m_encounterCombo->setCurrentIndex(row);  // shows it
    }
}

void CombatPage::showEncounter()
{
    Encounter* encounter = selectedEncounter();
    if (encounter != nullptr) {
        writeLastEncounter(m_stateFile, QString::fromStdString(encounter->id));
    }
    m_fight->setVisible(encounter != nullptr);
    m_emptyHint->setVisible(encounter == nullptr && !hasLoadError());
    if (encounter == nullptr) {
        disarmAttack();
        m_prompts.clear();
        rebuildPrompts();
        updateTurnLabels();
        return;
    }
    if (m_shownEncounterId != encounter->id) {
        disarmAttack();
        m_prompts.clear();
        m_log.clear();
        m_logList->clear();
        m_hitLabel->clear();
        m_shownEncounterId = encounter->id;
        m_lastEdit = EditKind::Once;
    }
    m_rollNote->hide();
    m_rollNote->clear();
    rebuildPrompts();
    rebuildCombatantList({});
}

// The turn order shows every row it can. A long one takes height from the
// log, which shrinks to no less than three lines; past that the turn order
// scrolls. A short one leaves the log its usual share.
void CombatPage::fitTurnOrder()
{
    if (m_leftHost == nullptr || m_orderCard == nullptr || m_logCard == nullptr) {
        return;
    }
    QHeaderView* header = m_initiativeList->header();
    int wanted = 2 * m_initiativeList->frameWidth() + (header->isHidden() ? 0 : header->sizeHint().height()) + 4;
    for (int i = 0; i < m_initiativeList->topLevelItemCount(); ++i) {
        wanted += m_initiativeList->sizeHintForRow(i) > 0 ? m_initiativeList->sizeHintForRow(i)
                                                          : m_initiativeList->visualItemRect(m_initiativeList->topLevelItem(i)).height();
    }
    // The log keeps its usual share unless the turn order needs more; then it
    // gives up height, down to three lines. Only the log
    // is capped, so the column never asks for more than the window has.
    const int spacing = m_leftHost->layout() != nullptr ? m_leftHost->layout()->spacing() : 12;
    const QWidget* downCard = m_downList->parentWidget();
    const int column = m_leftHost->height() - (downCard->isVisible() ? downCard->height() + spacing : 0) - spacing;
    const int orderChrome = std::max(0, m_orderCard->height() - m_initiativeList->height());
    // The log's usual share is under three tenths of the column; with more
    // to spare than that, it is left alone.
    const int spare = column - orderChrome - wanted;
    const int logHeight = spare >= column * 3 / 10 ? QWIDGETSIZE_MAX
                                                   : std::max(spare, m_logCard->minimumSizeHint().height());
    if (m_logCard->maximumHeight() != logHeight) {
        m_logCard->setMaximumHeight(logHeight);
    }
}

bool CombatPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_leftHost && event->type() == QEvent::Resize) {
        fitTurnOrder();
    }
    return QWidget::eventFilter(watched, event);
}

void CombatPage::rebuildCombatantList(const std::string& selectId)
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    keepTurnInInitiative(*encounter);
    std::string id = selectId;
    if (id.empty()) {
        if (const Combatant* selected = selectedCombatant()) {
            id = selected->id;
        } else {
            id = currentTurnId();
        }
    }
    m_populating = true;
    {
        const QSignalBlocker initiativeBlocker(m_initiativeList);
        const QSignalBlocker downBlocker(m_downList);
        m_initiativeList->clear();
        m_downList->clear();
        QTreeWidgetItem* selectItem = nullptr;
        QTreeWidget* selectList = nullptr;
        for (int i = 0; i < static_cast<int>(encounter->combatants.size()); ++i) {
            const Combatant& combatant = encounter->combatants[static_cast<std::size_t>(i)];
            const bool down = !isInInitiative(combatant);
            QTreeWidget* list = down ? m_downList : m_initiativeList;
            QTreeWidgetItem* item =
                addCombatantRow(list, combatant, !down && encounter->started && i == encounter->turnIndex, !down,
                                m_conditions);
            if (combatant.id == id) {
                selectItem = item;
                selectList = list;
            }
        }
        m_initiativeList->setCurrentItem(selectList == m_initiativeList ? selectItem : nullptr);
        m_downList->setCurrentItem(selectList == m_downList ? selectItem : nullptr);
    }
    m_populating = false;
    // The Down card only takes room when someone is down.
    // The Downed card is only as tall as its rows (up to three, then it scrolls).
    const int downed = m_downList->topLevelItemCount();
    m_downList->parentWidget()->setVisible(downed > 0);
    if (downed > 0) {
        const int rowHeight = m_downList->sizeHintForRow(0);
        m_downList->setFixedHeight(std::min(downed, 3) * rowHeight + 2 * m_downList->frameWidth() + 2);
    }
    fitTurnOrder();

    // Durations name other combatants; keep the anchor list current.
    {
        const QString anchor = m_durationAnchor->currentData().toString();
        const QSignalBlocker blocker(m_durationAnchor);
        m_durationAnchor->clear();
        for (const Combatant& combatant : encounter->combatants) {
            m_durationAnchor->addItem(tr("%1's turn").arg(QString::fromStdString(combatant.name)),
                                      QString::fromStdString(combatant.id));
        }
        int row = m_durationAnchor->findData(anchor);
        if (row < 0) {
            row = m_durationAnchor->findData(QString::fromStdString(currentTurnId()));
        }
        m_durationAnchor->setCurrentIndex(std::max(0, row));
    }
    updateTurnLabels();
    rebuildPrompts();
    showArmedTargets();
    showCombatant();
}

void CombatPage::showArmedTargets()
{
    if (!m_armed.has_value()) {
        return;
    }
    // The creatures picked so far are highlighted; the user's own row is not
    // (it is still the card on the right, and comes back when the action ends).
    const std::vector<std::string>& targets = m_armed->targets;
    for (QTreeWidget* list : {m_initiativeList, m_downList}) {
        const QSignalBlocker blocker(list);  // the list's, not its selection model's: rows keep their flags
        // No current row while choosing (the card follows the action's user;
        // a current row would be highlighted along with the targets).
        list->selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
        list->clearSelection();
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = list->topLevelItem(i);
            const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
            if (std::find(targets.begin(), targets.end(), id) != targets.end()) {
                item->setSelected(true);
            }
        }
    }
}

void CombatPage::selectRow(const std::string& id)
{
    for (QTreeWidget* list : {m_initiativeList, m_downList}) {
        const QSignalBlocker blocker(list);
        list->clearSelection();
        list->setCurrentItem(nullptr);
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = list->topLevelItem(i);
            if (item->data(0, Qt::UserRole).toString().toStdString() == id) {
                list->setCurrentItem(item);
            }
        }
    }
}

void CombatPage::updateTurnLabels()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        // No encounter: no round or turn to show, and nothing to start.
        m_roundLabel->clear();
        m_activeLabel->clear();
        m_nextTurnButton->setText(tr("Start combat"));
        m_nextTurnButton->setEnabled(false);
        m_nextTurnButton->setToolTip(tr("Create an encounter in the Encounter Builder first."));
        return;
    }
    m_roundLabel->setText(encounter->started ? tr("Round %1").arg(encounter->round) : tr("Initiative"));
    const std::string turnId = currentTurnId();
    m_nextTurnButton->setText(encounter->started ? tr("Next turn") : tr("Start combat"));
    m_nextTurnButton->setToolTip(encounter->started
                                     ? tr("End this turn and start the next one.")
                                     : tr("Begin round 1 with the first creature in the order."));
    if (encounter->combatants.empty()) {
        m_activeLabel->setText(tr("No one is in this fight yet."));
    } else if (!encounter->started) {
        m_activeLabel->setText(tr("Set initiative, then Start combat"));
    } else if (turnId.empty()) {
        m_activeLabel->setText(tr("No one is left in the turn order."));
    } else {
        const Combatant* active = combatantById(turnId);
        QString text = tr("%1's turn").arg(QString::fromStdString(active->name));
        if (isDying(*active)) {
            text += tr(" (dying: roll a death save)");
        }
        m_activeLabel->setText(text);
    }
    const bool anyone = !initiativeOrder(encounter->combatants).empty();
    m_nextTurnButton->setEnabled(anyone);
    const bool anyMonster = std::any_of(encounter->combatants.begin(), encounter->combatants.end(),
                                        [](const Combatant& combatant) { return isMonsterCombatant(combatant); });
    m_rollAllButton->setEnabled(anyMonster);
    m_noCombatantHint->setVisible(encounter->combatants.empty());
}

void CombatPage::showCombatant()
{
    if (m_populating) {
        return;
    }
    Combatant* combatant = selectedCombatant();
    const Encounter* encounter = selectedEncounter();
    const bool initiativePhase = encounter != nullptr && !encounter->started && !encounter->combatants.empty();
    const bool entry = combatant == nullptr && initiativePhase;
    m_combatantForm->setVisible(combatant != nullptr);
    m_initiativeEntry->setVisible(entry);
    m_rightHost->setVisible(combatant != nullptr || entry);
    m_backToEntryButton->setVisible(initiativePhase);
    m_removeButton->setEnabled(combatant != nullptr);
    if (entry) {
        refreshInitiativeEntry();
    }
    if (combatant == nullptr) {
        m_cardCombatantId.clear();
        clearLayout(m_actionRows);
        return;
    }
    const bool monster = isMonsterCombatant(*combatant);
    m_deathSavesHost->setVisible(!monster && (combatant->hp == 0 || combatant->dead));
    m_rollMode->setVisible(monster);
    m_rollModeLabel->setVisible(monster);
    m_economyHost->setVisible(monster);
    m_rerollButton->setVisible(monster);
    // Another creature: back to the tab it had open (Actions the first time).
    if (combatant->id != m_cardCombatantId) {
        m_cardCombatantId.clear();  // so the switch below isn't recorded for anyone
        const auto remembered = m_tabByCombatant.find(combatant->id);
        m_detailTabs->setCurrentIndex(remembered == m_tabByCombatant.end() ? 0 : remembered->second);
        m_cardCombatantId = combatant->id;
    }
    m_selectedName->setText(QString::fromStdString(combatant->name));
    m_selectedMeta->setPixmap(shieldPixmap(QSizeF(34.0, 38.0), QString::number(combatant->ac),
                                           combatant->ac >= 100 ? 11.0 : 15.0, m_selectedMeta->devicePixelRatioF()));
    m_selectedMeta->setAccessibleName(tr("AC %1").arg(combatant->ac));
    m_selectedMeta->setToolTip(tr("Armor Class %1").arg(combatant->ac));

    m_populating = true;
    if (monster) {
        m_initiative->setToolTip(tr("The rolled initiative total. Type to change it."));
        m_initiativeBonus->setText(combatant->initiativeBonus.has_value()
                                       ? QString::fromStdString(formatModifier(*combatant->initiativeBonus))
                                       : tr("+0 (none stored)"));
        m_initiativeBonus->setToolTip(combatant->initiativeBonus.has_value()
                                          ? tr("The stat block's initiative bonus. Reroll uses it.")
                                          : tr("The stat block had no initiative bonus. Rolls use +0."));
        m_rerollButton->setToolTip(tr("Roll a d20 and add the bonus."));
    } else {
        m_initiative->setToolTip(tr("Type this character's initiative total."));
        const Character* character = characterFor(*combatant);
        m_initiativeBonus->setText(character == nullptr
                                       ? tr("Not in the roster")
                                       : QString::fromStdString(formatModifier(initiativeModifier(*character))));
        m_initiativeBonus->setToolTip(tr("From the character sheet. Add it to the player's d20 roll."));
    }
    m_initiative->setValue(combatant->initiative);
    m_acLabel->setText(QString::number(combatant->ac));
    m_hp->setValue(combatant->hp);
    m_tempHp->setValue(combatant->tempHp);
    m_exhaustion->setCurrentIndex(std::max(0, m_exhaustion->findData(combatant->exhaustion)));
    if (combatant->exhaustion <= 0) {
        m_exhaustionNote->setText(tr("No effects."));
    } else if (combatant->exhaustion >= 6) {
        m_exhaustionNote->setText(tr("Exhaustion 6: the creature dies."));
    } else {
        m_exhaustionNote->setText(tr("D20 Tests -%1, Speed -%2 ft. A Long Rest removes 1 level; level 6 is death.")
                                      .arg(d20Penalty(*combatant))
                                      .arg(exhaustionSpeedPenalty(combatant->exhaustion)));
    }
    m_bloodiedLabel->setVisible(combatant->hp > 0 && isBloodied(*combatant));
    m_maxHpLabel->setText(combatant->maxHp.has_value() ? QString::number(*combatant->maxHp) : QString());
    m_hp->setToolTip((combatant->maxHp.has_value() ? tr("Hit Point maximum %1. ").arg(*combatant->maxHp) : QString()) +
                     tr("Type a new total, or a change: -7 takes 7 off, +5 adds 5. Press Enter."));
    m_actionUsed->setChecked(combatant->economy.actionUsed);
    m_bonusUsed->setChecked(combatant->economy.bonusActionUsed);
    m_reactionUsed->setChecked(combatant->economy.reactionUsed);
    m_economyNote->setText(economyText(*combatant));



    if (combatant->concentration.empty()) {
        m_concentrationLabel->setText(tr("Not concentrating."));
    } else {
        m_concentrationLabel->setText(concentrationName(combatant->concentration));
    }
    m_populating = false;

    rebuildConditionList(*combatant);
    updateDeathSaveRow(*combatant);
    rebuildActions(*combatant);
    rebuildDetails(*combatant);
}

void CombatPage::updateDeathSaveRow(const Combatant& combatant)
{
    if (combatant.dead) {
        m_deathStatus->setText(tr("Dead. Set HP above 0 to bring them back."));
    } else if (isDying(combatant)) {
        m_deathStatus->setText(tr("Dying: %1 successes, %2 failures")
                                   .arg(combatant.deathSaves.successes)
                                   .arg(combatant.deathSaves.failures));
    } else if (combatant.stable && combatant.hp == 0) {
        m_deathStatus->setText(tr("Stable at 0 HP"));
    } else {
        m_deathStatus->setText(tr("Not dying"));
    }
    // Stable or dead, there are no death saves to count and nothing to
    // stabilize: only the line saying so stays.
    m_stabilizeButton->setVisible(isDying(combatant));
    m_deathSaveButtons->setVisible(isDying(combatant));
}

void CombatPage::rebuildConditionList(const Combatant& combatant)
{
    const Encounter* encounter = selectedEncounter();
    clearLayout(m_conditionChips->layout());
    bool selectedStillThere = false;
    for (const ActiveCondition& condition : combatant.conditions) {
        selectedStillThere = selectedStillThere || QString::fromStdString(condition.id) == m_selectedCondition;
    }
    if (!selectedStillThere) {
        m_selectedCondition = combatant.conditions.empty() ? QString()
                                                           : QString::fromStdString(combatant.conditions.front().id);
    }
    for (const ActiveCondition& condition : combatant.conditions) {
        const QString id = QString::fromStdString(condition.id);
        // The tag holds the name; its cause and duration go under the heading
        // on the Conditions tab.
        const QString text = conditionName(condition.id);
        const QString notes = QString::fromStdString(
            describeCondition(condition, conditionName(condition.id).toStdString(), *encounter));
        const QColor hue = conditionColor(condition.id);
        const bool selected = id == m_selectedCondition;

        auto* chip = new QFrame;
        chip->setObjectName(QStringLiteral("conditionChip"));
        chip->setProperty("conditionId", id);
        chip->setProperty("selected", selected);
        chip->setStyleSheet(QStringLiteral("QFrame#conditionChip { background: %1; border: 2px solid %2; "
                                           "border-radius: 13px; }"
                                           "QFrame#conditionChip QPushButton, QFrame#conditionChip QToolButton { "
                                           "background: transparent; border: none; color: %3; font-weight: 600; "
                                           "padding: 0; }"
                                           "QFrame#conditionChip QToolButton:hover { color: %4; }")
                                .arg(blend(hue, palette::surface, 0.16).name(),
                                     selected ? hue.name() : blend(hue, palette::surface, 0.16).name(),
                                     hue.darker(150).name(), palette::critical.name()));
        auto* row = new QHBoxLayout(chip);
        row->setContentsMargins(12, 3, 6, 3);
        row->setSpacing(4);
        auto* name = new QPushButton(text);
        name->setObjectName(QStringLiteral("conditionChipName"));
        name->setCursor(Qt::PointingHandCursor);
        name->setToolTip(tr("%1\nClick for its rules on the Conditions tab.").arg(notes));
        auto* remove = new QToolButton;
        remove->setObjectName(QStringLiteral("removeConditionChip"));
        remove->setText(QStringLiteral("\u00D7"));
        remove->setCursor(Qt::PointingHandCursor);
        remove->setToolTip(tr("Remove %1.").arg(conditionName(condition.id)));
        row->addWidget(name);
        row->addWidget(remove);
        connect(name, &QPushButton::clicked, this, [this, id] {
            m_selectedCondition = id;
            m_detailTabs->setCurrentIndex(1);  // the Conditions tab shows its rules
            if (const Combatant* current = selectedCombatant()) {
                rebuildConditionList(*current);
            }
        });
        connect(remove, &QToolButton::clicked, this, [this, id] { removeListedCondition(id); });
        m_conditionChips->layout()->addWidget(chip);
    }
    m_conditionChips->setVisible(!combatant.conditions.empty());
    showConditionText();
}

void CombatPage::showConditionText()
{
    if (m_selectedCondition.isEmpty()) {
        m_conditionText->clear();
        m_conditionText->hide();
        m_conditionHeading->hide();
        m_conditionDetail->hide();
        m_conditionRule->hide();
        return;
    }
    m_conditionText->show();
    m_conditionHeading->setText(conditionName(m_selectedCondition.toStdString()));
    m_conditionHeading->show();
    m_conditionRule->show();
    QStringList notes;
    const Encounter* encounter = selectedEncounter();
    if (const Combatant* combatant = selectedCombatant(); combatant != nullptr && encounter != nullptr) {
        for (const ActiveCondition& active : combatant->conditions) {
            if (QString::fromStdString(active.id) == m_selectedCondition && notes.isEmpty()) {
                for (const std::string& note : conditionNotes(active, *encounter)) {
                    QString line = QString::fromStdString(note);
                    if (!line.isEmpty()) {
                        line[0] = line[0].toUpper();
                    }
                    notes << line;
                }
            }
        }
    }
    if (notes.isEmpty()) {
        m_conditionDetail->hide();
    } else {
        const QColor hue = conditionColor(m_selectedCondition.toStdString());
        m_conditionDetail->setText(notes.join(QStringLiteral("\n")));
        m_conditionDetail->setStyleSheet(
            QStringLiteral("QLabel#conditionDetail { background: %1; color: %2; border-left: 3px solid %3; "
                           "border-radius: 4px; padding: 5px 10px; font-weight: 600; }")
                .arg(blend(hue, palette::surface, 0.16).name(), hue.darker(150).name(), hue.name()));
        m_conditionDetail->show();
    }
    const std::optional<Condition> condition = findConditionById(m_conditions, m_selectedCondition.toStdString());
    if (!condition.has_value()) {
        m_conditionText->setText(tr("No SRD description is stored for this condition."));
        return;
    }
    // The SRD text only. The catalog's tags restate parts of it in short
    // ("You have Disadvantage on attack rolls."), so listing them repeated it.
    m_conditionText->setText(QString::fromStdString(condition->description));
}

// The monster's stat block, with a button for each thing it can do now.
void CombatPage::rebuildActions(const Combatant& combatant)
{
    clearLayout(m_actionRows);
    const bool monster = isMonsterCombatant(combatant);
    m_actionsSection->setVisible(true);
    addEscapeRows(combatant);
    if (!monster) {
        addCharacterAttackRow(combatant);
        return;
    }
    if (!combatant.statBlock.has_value()) {
        m_actionRows->addWidget(bodyLabel(tr("No stat block is stored for this monster.")));
        return;
    }
    const Monster& block = *combatant.statBlock;
    const bool theirTurn = isTheirTurn(combatant);
    const std::string id = combatant.id;

    auto addFeatures = [this, &combatant, id, theirTurn](const QString& heading,
                                                        const std::vector<MonsterFeature>& features,
                                                        std::optional<FeatureKind> kind) {
        if (features.empty()) {
            return;
        }
        addSectionHeading(m_actionRows, boldLabel(heading));
        for (const MonsterFeature& feature : features) {
            const EntryRow entry = makeEntryRow();
            QWidget* row = entry.row;
            QVBoxLayout* layout = entry.content;
            const QString title = QString::fromStdString(feature.name);
            std::optional<int> usesLeft;
            if (feature.perDay.has_value()) {
                const auto left = combatant.usesRemaining.find(feature.name);
                usesLeft = left == combatant.usesRemaining.end() ? *feature.perDay : left->second;
            }
            const bool recharging =
                feature.recharge.has_value() &&
                std::find(combatant.expended.begin(), combatant.expended.end(), feature.name) != combatant.expended.end();
            auto* titleLabel = boldLabel(title);
            titleLabel->setProperty("featureName", QString::fromStdString(feature.name));
            layout->addWidget(titleLabel);
            // A trait gets a Use button when it is limited (Legendary Resistance, 3/Day).
            std::optional<FeatureKind> rowKind = kind;
            if (!rowKind.has_value() && (feature.perDay.has_value() || feature.recharge.has_value())) {
                rowKind = FeatureKind::Trait;
            }
            // "Takes the Disengage or Hide action": a button for each.
            const std::vector<std::vector<std::string>> choices =
                feature.targeted.has_value() ? std::vector<std::vector<std::string>>{}
                                             : standardActionChoices(feature.effect);
            if (rowKind.has_value() && !choices.empty()) {
                const Availability available = featureAvailability(combatant, *rowKind, feature, theirTurn);
                for (const std::vector<std::string>& choice : choices) {
                    QStringList words;
                    for (const std::string& action : choice) {
                        words << QString::fromStdString(action);
                    }
                    QString text = words.join(QStringLiteral(" + "));
                    if (recharging) {
                        text = tr("Recharging");
                    } else if (usesLeft.has_value() && *usesLeft <= 0) {
                        text = tr("None left");
                    }
                    auto* button = new QPushButton(text);
                    button->setObjectName(QStringLiteral("standardAction"));
                    button->setProperty("action", words.join(QLatin1Char('+')));
                    button->setEnabled(available.available);
                    button->setToolTip(available.available
                                           ? tr("Take the %1 action with %2.")
                                                 .arg(words.join(tr(" and ")), QString::fromStdString(feature.name))
                                           : QString::fromStdString(available.reason));
                    const MonsterFeature copy = feature;
                    const FeatureKind which = *rowKind;
                    const std::vector<std::string> actions = choice;
                    connect(button, &QPushButton::clicked, this,
                            [this, id, which, copy, actions] { onStandardActionClicked(id, which, copy, actions); });
                    addColumnButton(entry, button);
                }
                if (*rowKind == FeatureKind::Legendary) {
                    addColumnNote(entry, legendaryNote(combatant, theirTurn));
                }
            } else if (rowKind.has_value() && feature.name != "Legendary Action Uses") {
                const Availability available = featureAvailability(combatant, *rowKind, feature, theirTurn);
                QString text = tr("Use");
                QString hint = tr("Use it.");
                const bool helpful = feature.targeted.has_value() && feature.targeted->benefit.has_value();
                if (feature.targeted.has_value()) {
                    if (helpful) {
                        text = tr("Help…");
                        hint = tr("Pick a creature to help, then click it.");
                    } else if (feature.targeted->attackBonus.has_value()) {
                        text = tr("Attack");
                        hint = tr("Click the target to roll the attack.");
                    } else if (feature.targeted->area) {
                        text = tr("Targets…");
                        hint = tr("Click each creature in the area; Escape when done.");
                    } else {
                        text = tr("Target…");
                        hint = tr("Click the target.");
                    }
                }
                if (recharging) {
                    text = tr("Recharging");
                } else if (usesLeft.has_value() && *usesLeft <= 0) {
                    text = tr("None left");
                }
                const bool armedHere = m_armed.has_value() && m_armed->attackerId == id &&
                                       m_armed->featureKind.has_value() && m_armed->feature.name == feature.name;
                auto* button = new QPushButton(armedHere ? tr("End") : text);
                button->setObjectName(feature.targeted.has_value() ? QStringLiteral("featureTarget")
                                                                   : QStringLiteral("featureUse"));
                button->setEnabled(armedHere || available.available);
                if (helpful) {
                    button->setIcon(helpIcon());
                }
                button->setToolTip(available.available ? hint : QString::fromStdString(available.reason));
                const MonsterFeature copy = feature;
                const FeatureKind which = *rowKind;
                connect(button, &QPushButton::clicked, this,
                        [this, id, which, copy] { onFeatureClicked(id, which, copy); });
                addColumnButton(entry, button);
                if (*rowKind == FeatureKind::Legendary) {
                    addColumnNote(entry, legendaryNote(combatant, theirTurn));
                }
            }
            if (usesLeft.has_value()) {
                addColumnNote(entry, tr("%1 of %2 left today").arg(*usesLeft).arg(*feature.perDay));
            }
            if (feature.aura.has_value()) {
                const bool off = std::find(combatant.aurasOff.begin(), combatant.aurasOff.end(), feature.name) !=
                                 combatant.aurasOff.end();
                auto* toggle = new QPushButton(off ? tr("Switch on") : tr("Switch off"));
                toggle->setObjectName(QStringLiteral("auraToggle"));
                toggle->setToolTip(off ? tr("Ask about this aura again at the start of each opponent's turn.")
                                       : tr("Stop asking about this aura (for example, while the true form is hidden)."));
                makeQuiet(toggle);
                const std::string auraName = feature.name;
                connect(toggle, &QPushButton::clicked, this, [this, id, auraName] { toggleAura(id, auraName); });
                addColumnButton(entry, toggle);
            }
            layout->addWidget(bodyLabel(QString::fromStdString(feature.effect)));
            // What each action it can take does.
            std::vector<std::string> described;
            for (const std::vector<std::string>& choice : choices) {
                for (const std::string& action : choice) {
                    if (std::find(described.begin(), described.end(), action) != described.end()) {
                        continue;
                    }
                    described.push_back(action);
                    auto* rules = bodyLabel(QStringLiteral("<b>%1.</b> %2")
                                                .arg(QString::fromStdString(action).toHtmlEscaped(),
                                                     QString::fromStdString(standardActionRules(action)).toHtmlEscaped()));
                    rules->setTextFormat(Qt::RichText);
                    rules->setObjectName(QStringLiteral("standardActionRules"));
                    rules->setProperty("role", QStringLiteral("muted"));
                    layout->addWidget(rules);
                }
            }
            if (feature.targeted.has_value()) {
                addRuleNotes(layout, *feature.targeted);
                addDamageChoices(layout, combatant, *feature.targeted);
            }
            if (feature.aura.has_value()) {
                const bool off = std::find(combatant.aurasOff.begin(), combatant.aurasOff.end(), feature.name) !=
                                 combatant.aurasOff.end();
                auto* note = bodyLabel(off ? tr("Aura off: no one is asked to save.")
                                           : tr("Aura: at the start of each opponent's turn, you are asked whether "
                                                "it is in range, and the save is rolled or entered there."));
                note->setProperty("role", QStringLiteral("muted"));
                layout->addWidget(note);
            }
            if (feature.selfEffect.has_value()) {
                auto* note = bodyLabel(QString::fromStdString(describeSelfEffect(
                    *feature.selfEffect, conditionName(feature.selfEffect->condition).toStdString())));
                note->setProperty("role", QStringLiteral("muted"));
                layout->addWidget(note);
            }
            if (feature.attackModifier.has_value()) {
                auto* note = bodyLabel(QString::fromStdString(describeAttackModifier(*feature.attackModifier)));
                note->setProperty("role", QStringLiteral("muted"));
                layout->addWidget(note);
            }
            m_actionRows->addWidget(row);
        }
    };

    // Traits the app cannot check change attack rolls when ticked: Pack
    // Tactics, sunlight. Bloodied Fury and the like are automatic.
    if (Encounter* encounter = selectedEncounter()) {
        for (const RollQuestion& question : attackRollQuestions(*encounter, combatant)) {
            auto* box = new QCheckBox(tr("%1: %2 (%3%4)")
                                          .arg(QString::fromStdString(question.trait), QString::fromStdString(question.text),
                                               question.advantage ? tr("Advantage") : tr("Disadvantage"),
                                               question.sticky ? QString() : tr(", next attack")));
            box->setObjectName(QStringLiteral("rollQuestion"));
            box->setChecked(rollTicked(id, question.key));
            box->setToolTip(tr("Used when Attack rolls is Automatic."));
            const std::string key = id + '|' + question.key;
            connect(box, &QCheckBox::toggled, this, [this, key](bool on) { m_rollTicks[key] = on; });
            m_actionRows->addWidget(wrappingCheckBox(box, box->text()));
        }
    }
    // A line under Attack rolls and Used, which sit above the list.
    addSectionHeading(m_actionRows, boldLabel(tr("Actions")), true);
    if (block.attacks.empty()) {
        m_actionRows->addWidget(bodyLabel(tr("No actions are stored for this monster.")));
    }
    for (const MonsterAttack& attack : block.attacks) {
        const EntryRow entry = makeEntryRow();
        QWidget* row = entry.row;
        QVBoxLayout* layout = entry.content;
        QString title = QString::fromStdString(attack.name);
        if (isMultiattack(attack)) {
            title = tr("Multiattack (%n use(s))", nullptr, attack.count);
        } else if (attack.count > 1) {
            title += tr(" × %1 in Multiattack").arg(attack.count);
        }
        std::optional<int> usesLeft;
        if (attack.perDay.has_value()) {
            const auto left = combatant.usesRemaining.find(attack.name);
            usesLeft = left == combatant.usesRemaining.end() ? *attack.perDay : left->second;
        }
        const bool recharging =
            attack.recharge.has_value() &&
            std::find(combatant.expended.begin(), combatant.expended.end(), attack.name) != combatant.expended.end();
        layout->addWidget(boldLabel(title));
        const Availability available = actionAvailability(combatant, attack, theirTurn);
        QString buttonText;
        QString hint;
        if (isMultiattack(attack)) {
            buttonText = tr("Multiattack");
            hint = tr("Spend the action on Multiattack, then use the attacks it names.");
        } else if (attack.benefit.has_value()) {
            buttonText = tr("Help…");
            hint = tr("Pick a creature to help, then click it.");
        } else if (attack.attackBonus.has_value()) {
            buttonText = tr("Attack");
            hint = tr("Click the target to roll the attack.");
        } else if (attack.save.has_value() && attack.area) {
            buttonText = tr("Targets…");
            hint = tr("Click each creature in the area; Escape when done.");
        } else if (attack.save.has_value() || !attackDamageParts(attack).empty()) {
            buttonText = tr("Target…");
            hint = tr("Click the target.");
        } else {
            buttonText = tr("Use");
            hint = tr("Use it.");
        }
        if (recharging) {
            buttonText = tr("Recharging");
        } else if (usesLeft.has_value() && *usesLeft <= 0) {
            buttonText = tr("None left");
        }
        const bool armedHere = m_armed.has_value() && m_armed->attackerId == id && !m_armed->featureKind.has_value() &&
                               m_armed->attack.name == attack.name;
        auto* button = new QPushButton(armedHere ? tr("End") : buttonText);
        button->setObjectName(QStringLiteral("rollAttackDamage"));
        button->setEnabled(armedHere || available.available);
        if (attack.benefit.has_value()) {
            button->setIcon(helpIcon());
        }
        button->setToolTip(available.available ? hint : QString::fromStdString(available.reason));
        const MonsterAttack copy = attack;
        connect(button, &QPushButton::clicked, this, [this, id, copy] { onActionClicked(id, copy); });
        addColumnButton(entry, button);
        if (usesLeft.has_value()) {
            addColumnNote(entry, tr("%1 of %2 left today").arg(*usesLeft).arg(*attack.perDay));
        }
        // During a Multiattack: how many more of this one it allows.
        if (const auto left = combatant.economy.multiattackLeft.find(attack.name);
            combatant.economy.attacksRemaining > 0 && left != combatant.economy.multiattackLeft.end()) {
            addColumnNote(entry, left->second > 0 ? tr("%n more in Multiattack", nullptr, left->second)
                                                  : tr("none left in Multiattack"));
        }
        const std::string summary = attackSummary(attack);
        if (!summary.empty()) {
            auto* rolls = bodyLabel(QString::fromStdString(summary));
            rolls->setProperty("role", QStringLiteral("muted"));
            layout->addWidget(rolls);
        }
        layout->addWidget(bodyLabel(QString::fromStdString(attack.effect)));
        addRuleNotes(layout, attack);
        addDamageChoices(layout, combatant, attack);
        if (attack.selfEffect.has_value()) {
            auto* note = bodyLabel(QString::fromStdString(
                describeSelfEffect(*attack.selfEffect, conditionName(attack.selfEffect->condition).toStdString())));
            note->setProperty("role", QStringLiteral("muted"));
            layout->addWidget(note);
        }
        m_actionRows->addWidget(row);
    }
    addFeatures(tr("Bonus Actions"), block.bonusActions, FeatureKind::BonusAction);
    addFeatures(tr("Reactions"), block.reactions, FeatureKind::Reaction);
    addFeatures(tr("Legendary Actions"), block.legendaryActions, FeatureKind::Legendary);
    // Traits are reference; everything the monster can do comes first.
    addFeatures(tr("Traits"), block.traits, std::nullopt);
}

void CombatPage::addEscapeRows(const Combatant& combatant)
{
    if (combatant.dead) {
        return;
    }
    std::vector<ActiveCondition> grapples;
    for (const ActiveCondition& condition : combatant.conditions) {
        if (condition.id == "grappled") {
            grapples.push_back(condition);
        }
    }
    if (grapples.empty()) {
        return;
    }
    addSectionHeading(m_actionRows, boldLabel(tr("Grappled")));
    const EscapeCheck check = escapeCheck(combatant, characterFor(combatant));
    const std::string id = combatant.id;
    for (const ActiveCondition& grapple : grapples) {
        const EntryRow entry = makeEntryRow();
        entry.content->addWidget(boldLabel(grapple.byId.empty() ? tr("Grappled")
                                                                : tr("Grappled by %1").arg(nameOf(grapple.byId))));
        const std::optional<int> dc = grappleEscapeDc(grapple);
        entry.content->addWidget(bodyLabel(
            tr("Escape as an action: a %1Strength (Athletics) or Dexterity (Acrobatics) check. Best: %2 %3.")
                .arg(dc.has_value() ? tr("DC %1 ").arg(*dc) : QString(), QString::fromStdString(check.skill),
                     QString::fromStdString(formatModifier(check.bonus)))));
        auto* button = new QPushButton(tr("Escape"));
        button->setObjectName(QStringLiteral("escapeGrapple"));
        const bool canAct = !combatant.economy.actionUsed && !isIncapacitated(combatant);
        button->setEnabled(canAct);
        button->setToolTip(canAct ? tr("Spend the action on a check to escape.")
                           : isIncapacitated(combatant) ? tr("An Incapacitated creature can't take actions.")
                                                        : tr("The action is already used this turn."));
        const std::string grapplerId = grapple.byId;
        connect(button, &QPushButton::clicked, this, [this, id, grapplerId] { onEscapeClicked(id, grapplerId); });
        addColumnButton(entry, button);
        m_actionRows->addWidget(entry.row);
    }
}

void CombatPage::onEscapeClicked(const std::string& combatantId, const std::string& grapplerId)
{
    Combatant* combatant = combatantById(combatantId);
    if (combatant == nullptr) {
        return;
    }
    std::optional<int> dc;
    for (const ActiveCondition& condition : combatant->conditions) {
        if (condition.id == "grappled" && condition.byId == grapplerId) {
            dc = grappleEscapeDc(condition);
        }
    }
    PageUndo before = capture();
    combatant->economy.actionUsed = true;
    Prompt prompt;
    prompt.kind = Prompt::Kind::Escape;
    prompt.combatantId = combatantId;
    prompt.sourceId = grapplerId;
    prompt.conditionId = "grappled";
    prompt.dc = dc.value_or(0);
    m_prompts.push_back(prompt);
    addLog(tr("%1 uses its action to try to escape the grapple.").arg(QString::fromStdString(combatant->name)));
    commit(std::move(before), EditKind::Once, combatantId);
    rebuildPrompts();
}

void CombatPage::addCharacterAttackRow(const Combatant& combatant)
{
    m_actionRows->addWidget(boldLabel(tr("Actions")));
    const EntryRow attackEntry = makeEntryRow();
    QWidget* row = attackEntry.row;
    row->setObjectName(QStringLiteral("characterAttackRow"));
    QVBoxLayout* layout = attackEntry.content;
    layout->setSpacing(6);
    auto* top = new QHBoxLayout;
    top->setSpacing(8);
    top->addWidget(makeMuted(tr("Damage")));
    auto* amount = new QSpinBox;
    amount->setObjectName(QStringLiteral("characterAttackDamage"));
    fitThreeDigitAmount(amount);
    amount->setValue(m_characterAttackAmount);
    amount->setToolTip(tr("The damage the player rolled."));
    auto* type = new QComboBox;
    type->setObjectName(QStringLiteral("characterAttackType"));
    fillDamageTypes(type);
    type->setCurrentIndex(std::max(0, type->findData(m_characterAttackType)));
    const std::string id = combatant.id;
    const bool armedHere = m_armed.has_value() && m_armed->attackerId == id && m_armed->fixedDamage.has_value();
    auto* button = new QPushButton(armedHere ? tr("End") : tr("Attack"));
    button->setObjectName(QStringLiteral("characterAttack"));
    button->setEnabled(armedHere || (!combatant.dead && combatant.hp > 0));
    button->setToolTip(tr("Then click the target."));
    addColumnButton(attackEntry, button);
    top->addWidget(amount);
    top->addWidget(type);
    top->addStretch(1);
    layout->addLayout(top);
    auto* note = bodyLabel(tr("Type the damage the player rolled, then click the target. The app makes no attack "
                              "roll. The target's resistances, immunities, and vulnerabilities are applied for you, "
                              "and the log shows what it took."));
    note->setProperty("role", QStringLiteral("muted"));
    layout->addWidget(note);
    m_actionRows->addWidget(row);

    // Healing a creature (Cure Wounds, Healing Word, a potion handed over):
    // the help cursor picks who.
    const EntryRow healEntry = makeEntryRow();
    QWidget* healRow = healEntry.row;
    healRow->setObjectName(QStringLiteral("characterHealRow"));
    QVBoxLayout* healLayout = healEntry.content;
    healLayout->setSpacing(6);
    auto* healTop = new QHBoxLayout;
    healTop->setSpacing(8);
    healTop->addWidget(makeMuted(tr("Hit Points")));
    auto* healAmount = new QSpinBox;
    healAmount->setObjectName(QStringLiteral("characterHealAmount"));
    fitThreeDigitAmount(healAmount);
    healAmount->setValue(m_characterHealAmount);
    healAmount->setToolTip(tr("The Hit Points the player rolled."));
    const bool healArmed = m_armed.has_value() && m_armed->attackerId == id && m_armed->fixedHealing.has_value();
    auto* healButton = new QPushButton(helpIcon(), healArmed ? tr("End") : tr("Heal"));
    healButton->setObjectName(QStringLiteral("characterHeal"));
    healButton->setEnabled(healArmed || (!combatant.dead && combatant.hp > 0));
    healButton->setToolTip(tr("Then click who gets it."));
    addColumnButton(healEntry, healButton);
    healTop->addWidget(healAmount);
    healTop->addStretch(1);
    healLayout->addLayout(healTop);
    auto* healNote = bodyLabel(tr("Type the healing the player rolled, then click who gets it (themselves too)."));
    healNote->setProperty("role", QStringLiteral("muted"));
    healLayout->addWidget(healNote);
    m_actionRows->addWidget(healRow);
    connect(healAmount, &QSpinBox::valueChanged, this, [this](int value) { m_characterHealAmount = value; });
    connect(healButton, &QPushButton::clicked, this, [this, id] { onCharacterHealClicked(id); });

    connect(amount, &QSpinBox::valueChanged, this, [this](int value) { m_characterAttackAmount = value; });
    connect(type, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, type] { m_characterAttackType = type->currentData().toString(); });
    connect(button, &QPushButton::clicked, this, [this, id] { onCharacterAttackClicked(id); });
}

void CombatPage::onCharacterAttackClicked(const std::string& attackerId)
{
    if (m_armed.has_value() && m_armed->attackerId == attackerId && m_armed->fixedDamage.has_value()) {
        disarmAttack();
        showCombatant();
        return;
    }
    const Combatant* attacker = combatantById(attackerId);
    if (attacker == nullptr) {
        return;
    }
    if (m_characterAttackAmount <= 0) {
        addLog(tr("Type %1's damage first.").arg(QString::fromStdString(attacker->name)));
        return;
    }
    ArmedAction armed;
    armed.attackerId = attackerId;
    armed.attack.name = tr("Attack").toStdString();
    armed.spent = true;  // a character's attacks are not counted against a stat block
    armed.fixedDamage = std::vector<TypedDamage>{
        TypedDamage{m_characterAttackAmount, m_characterAttackType.toStdString()}};
    armAction(std::move(armed));
}

void CombatPage::onCharacterHealClicked(const std::string& healerId)
{
    if (m_armed.has_value() && m_armed->attackerId == healerId && m_armed->fixedHealing.has_value()) {
        disarmAttack();
        showCombatant();
        return;
    }
    const Combatant* healer = combatantById(healerId);
    if (healer == nullptr) {
        return;
    }
    if (m_characterHealAmount <= 0) {
        addLog(tr("Type %1's healing first.").arg(QString::fromStdString(healer->name)));
        return;
    }
    ArmedAction armed;
    armed.attackerId = healerId;
    armed.attack.name = tr("Healing").toStdString();
    armed.spent = true;
    armed.fixedHealing = m_characterHealAmount;
    armAction(std::move(armed));
}

void CombatPage::rebuildPrompts()
{
    // Drop prompts that no longer apply, and repeats of the same question.
    std::vector<Prompt> kept;
    for (const Prompt& prompt : m_prompts) {
        const Combatant* combatant = combatantById(prompt.combatantId);
        if (combatant == nullptr || combatant->dead) {
            continue;
        }
        if (prompt.kind == Prompt::Kind::DeathSave && !isDying(*combatant)) {
            continue;
        }
        if (prompt.kind == Prompt::Kind::Concentration && combatant->concentration.empty()) {
            continue;
        }
        if (prompt.kind == Prompt::Kind::SaveToEnd && !hasCondition(*combatant, prompt.conditionId)) {
            continue;
        }
        if (prompt.kind == Prompt::Kind::Aura && !auraCheckFor(prompt).has_value()) {
            continue;  // the monster fell, or its aura was switched off
        }
        if (prompt.kind == Prompt::Kind::Escape &&
            std::none_of(combatant->conditions.begin(), combatant->conditions.end(),
                         [&prompt](const ActiveCondition& condition) {
                             return condition.id == "grappled" && condition.byId == prompt.sourceId;
                         })) {
            continue;  // the grapple ended some other way
        }
        if ((prompt.kind == Prompt::Kind::Rider || prompt.kind == Prompt::Kind::ActionSave) &&
            (combatantById(prompt.sourceId) == nullptr || !prompt.attack)) {
            continue;
        }
        const bool repeat = prompt.kind != Prompt::Kind::Rider && prompt.kind != Prompt::Kind::ActionSave &&
                            std::any_of(kept.begin(), kept.end(), [&prompt](const Prompt& other) {
            return other.kind == prompt.kind && other.combatantId == prompt.combatantId &&
                   other.conditionId == prompt.conditionId && other.sourceId == prompt.sourceId &&
                   other.auraName == prompt.auraName;
        });
        if (!repeat) {
            kept.push_back(prompt);
        }
    }
    m_prompts = std::move(kept);
    clearLayout(m_promptLayout);
    m_promptHost->setVisible(!m_prompts.empty());
    // One creature at a time, oldest question first: the checks for the
    // creature asked about first show; the rest wait until those are answered.
    const std::string focus = m_prompts.empty() ? std::string() : m_prompts.front().combatantId;
    QStringList waiting;
    int waitingCount = 0;
    for (const Prompt& prompt : m_prompts) {
        if (prompt.combatantId != focus) {
            ++waitingCount;
            const QString who = nameOf(prompt.combatantId);
            if (!waiting.contains(who)) {
                waiting << who;
            }
        }
    }
    for (std::size_t i = 0; i < m_prompts.size(); ++i) {
        const Prompt& prompt = m_prompts[i];
        Combatant* combatant = combatantById(prompt.combatantId);
        if (combatant == nullptr || prompt.combatantId != focus) {
            continue;
        }
        // Creature names are bold. They are marked while the sentence is put
        // together, then turned into markup once the rest is escaped.
        const auto bold = [](const QString& who) { return QChar(0x1) + who + QChar(0x2); };
        const QString name = bold(QString::fromStdString(combatant->name));
        QString text;
        QString pass = tr("Passed");
        QString fail = tr("Failed");
        QString dismissText = tr("Dismiss");
        const int bonus = combatant->saveBonuses[static_cast<std::size_t>(prompt.ability)] - d20Penalty(*combatant);
        switch (prompt.kind) {
        case Prompt::Kind::Concentration:
            text = tr("%1 took damage while concentrating: DC %2 Constitution save (%3).")
                       .arg(name)
                       .arg(prompt.dc)
                       .arg(QString::fromStdString(formatModifier(bonus)));
            pass = tr("Kept it");
            fail = tr("Lost it");
            break;
        case Prompt::Kind::SaveToEnd:
            text = tr("%1 repeats the DC %2 %3 save (%4) to end %5.")
                       .arg(name)
                       .arg(prompt.dc)
                       .arg(QString::fromLatin1(abilityLabel(prompt.ability)))
                       .arg(QString::fromStdString(formatModifier(bonus)))
                       .arg(conditionName(prompt.conditionId));
            pass = tr("Ends");
            fail = tr("Stays");
            break;
        case Prompt::Kind::Aura: {
            const std::optional<AuraCheck> check = auraCheckFor(prompt);
            const QString source = bold(nameOf(prompt.sourceId));
            text = tr("%1 (%2): does %3 start its turn %4?")
                       .arg(QString::fromStdString(prompt.auraName), source, name,
                            check.has_value() && !check->aura.range.empty()
                                ? QString::fromStdString(check->aura.range)
                                : tr("in range"));
            if (check.has_value() && !check->aura.who.empty()) {
                text += tr(" Only %1.").arg(QString::fromStdString(check->aura.who));
            }
            text += tr(" DC %1 %2 save (%3).")
                        .arg(prompt.dc)
                        .arg(QString::fromLatin1(abilityLabel(prompt.ability)))
                        .arg(QString::fromStdString(formatModifier(bonus)));
            pass = tr("Saved");
            fail = tr("Failed");
            dismissText = tr("Not affected");
            break;
        }
        case Prompt::Kind::Hide:
            text = tr("%1 hides (%2): DC %3 Dexterity (Stealth) check (%4). It must be Heavily Obscured or behind "
                      "Three-Quarters or Total Cover, and out of enemies' sight.")
                       .arg(name, QString::fromStdString(prompt.auraName))
                       .arg(prompt.dc)
                       .arg(QString::fromStdString(formatModifier(hideCheckBonus(*combatant))));
            pass = tr("Hidden");
            fail = tr("Seen");
            dismissText = tr("Doesn't hide");
            break;
        case Prompt::Kind::DeathSave:
            text = tr("%1 is dying: roll a death saving throw.").arg(name);
            pass = tr("Success");
            fail = tr("Failure");
            break;
        case Prompt::Kind::Escape: {
            const EscapeCheck check = escapeCheck(*combatant, characterFor(*combatant));
            const QString grappler = prompt.sourceId.empty() ? tr("the") : bold(nameOf(prompt.sourceId)) + tr("'s");
            text = (prompt.dc > 0 ? tr("%1 tries to escape %2 grapple: DC %3 Strength (Athletics) or Dexterity "
                                       "(Acrobatics) check (%4 %5).")
                                        .arg(name, grappler)
                                        .arg(prompt.dc)
                                  : tr("%1 tries to escape %2 grapple: a Strength (Athletics) or Dexterity "
                                       "(Acrobatics) check against its escape DC (%4 %5).")
                                        .arg(name, grappler))
                       .arg(QString::fromStdString(check.skill), QString::fromStdString(formatModifier(check.bonus)));
            pass = tr("Escaped");
            fail = tr("Still grappled");
            break;
        }
        case Prompt::Kind::ActionSave: {
            const MonsterAttack& attack = *prompt.attack;
            text = tr("%1's %2: %3 makes a DC %4 %5 save (%6)%7.")
                       .arg(bold(nameOf(prompt.sourceId)), QString::fromStdString(attack.name), name)
                       .arg(prompt.dc)
                       .arg(QString::fromLatin1(abilityLabel(prompt.ability)))
                       .arg(QString::fromStdString(formatModifier(bonus)))
                       .arg(!prompt.afterHit && prompt.advantage ? tr(" with Advantage") : QString());
            QStringList failure;
            if (totalDamage(prompt.damage) > 0) {
                failure << QString::fromStdString(describeDamage(prompt.damage));
            }
            if (!prompt.afterHit && attack.failureHpThreshold.has_value()) {
                failure << (attack.failureHpEffect == "dies" ? tr("dies") : tr("drops to 0"));
            }
            QStringList riderIds;
            for (const ConditionRider* rider : ridersFor(attack, kRiderOnFailure)) {
                for (const std::string& id : rider->conditions) {
                    riderIds << conditionName(id);
                }
            }
            if (!riderIds.isEmpty()) {
                failure << riderIds.join(tr(", "));
            }
            if (!failure.isEmpty()) {
                text += tr(" Failure: %1").arg(failure.join(tr("; ")));
                text += !prompt.afterHit && attack.save->halfOnSuccess && totalDamage(prompt.damage) > 0
                            ? tr(". Success: half damage.")
                            : QStringLiteral(".");
            }
            pass = tr("Saved");
            fail = tr("Failed");
            dismissText = tr("Not affected");
            break;
        }
        case Prompt::Kind::Rider: {
            const MonsterAttack& attack = *prompt.attack;
            const ConditionRider& first = attack.riders[prompt.riders.front()];
            std::vector<std::string> ids;
            for (const std::size_t index : prompt.riders) {
                for (const std::string& id : attack.riders[index].conditions) {
                    ids.push_back(id);
                }
            }
            QString ask = QString::fromStdString(first.ask);
            text = tr("%1's %2: did %3? If so, %4 is %5.")
                       .arg(bold(nameOf(prompt.sourceId)), QString::fromStdString(attack.name), ask, name,
                            conditionList(ids));
            if (prompt.refund > 0) {
                text += tr(" The %1 damage is given back.").arg(prompt.refund);
            }
            pass = tr("Yes");
            dismissText = tr("No");
            break;
        }
        }
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* label = bodyLabel(text.toHtmlEscaped()
                                     .replace(QChar(0x1), QStringLiteral("<b>"))
                                     .replace(QChar(0x2), QStringLiteral("</b>")));
        label->setObjectName(QStringLiteral("promptText"));
        label->setTextFormat(Qt::RichText);
        layout->addWidget(label, 1);
        auto* roll = new QPushButton(tr("Roll"));
        auto* passed = new QPushButton(pass);
        auto* failed = new QPushButton(fail);
        auto* dismiss = new QPushButton(dismissText);
        dismiss->setObjectName(QStringLiteral("promptDismiss"));
        roll->setObjectName(QStringLiteral("promptRoll"));
        passed->setObjectName(QStringLiteral("promptPassed"));
        failed->setObjectName(QStringLiteral("promptFailed"));
        layout->addWidget(roll);
        layout->addWidget(passed);
        layout->addWidget(failed);
        if (prompt.kind == Prompt::Kind::ActionSave && !ridersFor(*prompt.attack, kRiderOnFailureBy5).empty()) {
            auto* byFive = new QPushButton(tr("Failed by 5+"));
            byFive->setObjectName(QStringLiteral("promptFailedBy5"));
            layout->addWidget(byFive);
            connect(byFive, &QPushButton::clicked, this, [this, i] { resolvePrompt(i, 4); });
        }
        layout->addWidget(dismiss);
        if (prompt.kind == Prompt::Kind::Rider) {
            roll->hide();
            failed->hide();
        }
        if (prompt.kind == Prompt::Kind::Escape && prompt.dc <= 0) {
            roll->hide();  // the table knows the DC; the app doesn't
        }
        m_promptLayout->addWidget(row);
        connect(roll, &QPushButton::clicked, this, [this, i] { resolvePrompt(i, 0); });
        connect(passed, &QPushButton::clicked, this, [this, i] { resolvePrompt(i, 1); });
        connect(failed, &QPushButton::clicked, this, [this, i] { resolvePrompt(i, 2); });
        connect(dismiss, &QPushButton::clicked, this, [this, i] { resolvePrompt(i, 3); });
    }
    if (waitingCount > 0) {
        auto* queued = makeMuted(tr("%n more waiting, for %1.", nullptr, waitingCount).arg(waiting.join(tr(", "))));
        queued->setObjectName(QStringLiteral("promptQueued"));
        m_promptLayout->addWidget(queued);
    }
}

// outcome: 0 roll, 1 passed, 2 failed, 3 dismiss, 4 failed by 5 or more.
void CombatPage::resolvePrompt(std::size_t index, int outcome)
{
    if (index >= m_prompts.size()) {
        return;
    }
    const Prompt prompt = m_prompts[index];
    // Taken with the question still open, so Undo asks it again.
    PageUndo before = capture();
    m_prompts.erase(m_prompts.begin() + static_cast<std::ptrdiff_t>(index));
    Combatant* combatant = combatantById(prompt.combatantId);
    if (combatant == nullptr || outcome == 3) {
        QTimer::singleShot(0, this, [this] { rebuildPrompts(); });
        return;
    }
    const QString name = QString::fromStdString(combatant->name);
    if (prompt.kind == Prompt::Kind::Rider) {
        Combatant* attacker = combatantById(prompt.sourceId);
        if (attacker != nullptr && prompt.attack.has_value()) {
            const MonsterAttack& attack = *prompt.attack;
            if (prompt.refund > 0) {
                const HealingResult healed = applyHealing(*combatant, prompt.refund);
                addLog(tr("%1 gets the %2 damage back (%3 grapples instead).")
                           .arg(name)
                           .arg(healed.healed)
                           .arg(QString::fromStdString(attacker->name)));
            }
            for (const std::size_t riderIndex : prompt.riders) {
                giveRider(*attacker, *combatant, attack, attack.riders[riderIndex]);
            }
            // The extra damage that comes with the same circumstance (a charge).
            const std::string ask = attack.riders[prompt.riders.front()].ask;
            std::vector<DamagePart> extra;
            for (const DamagePart& part : attackDamageParts(attack)) {
                if (part.when == DamageWhen::Conditional && part.condition == ask) {
                    DamagePart always = part;
                    always.when = DamageWhen::Always;
                    always.condition.clear();
                    extra.push_back(always);
                }
            }
            if (!extra.empty() && prompt.refund == 0) {
                const std::vector<TypedDamage> damage = rollDamageParts(extra, DamageOptions{}, dieRoller());
                const DamageResult result = applyDamage(*combatant, damage);
                afterDamage(*combatant, result,
                            tr("%1's %2 (extra)").arg(QString::fromStdString(attacker->name),
                                                       QString::fromStdString(attack.name)));
            }
            carryToSheet(*combatant);
        }
    } else if (prompt.kind == Prompt::Kind::ActionSave) {
        resolveActionSave(prompt, outcome);
    } else if (prompt.kind == Prompt::Kind::Escape) {
        bool escaped = outcome == 1;
        const QString grappler = nameOf(prompt.sourceId);
        if (outcome == 0) {
            const EscapeCheck check = escapeCheck(*combatant, characterFor(*combatant));
            const int face = rollD20();
            const int total = face + check.bonus;
            escaped = total >= prompt.dc;
            addLog(tr("%1 rolls %2 for %3 (%4 %5) against DC %6: %7.")
                       .arg(name)
                       .arg(total)
                       .arg(QString::fromStdString(check.skill))
                       .arg(face)
                       .arg(QString::fromStdString(formatModifier(check.bonus)))
                       .arg(prompt.dc)
                       .arg(escaped ? tr("escapes") : tr("still grappled")));
        }
        const QString whose = prompt.sourceId.empty() ? tr("the grapple") : tr("%1's grapple").arg(grappler);
        if (escaped) {
            escapeGrapple(*combatant, prompt.sourceId);
            addLog(tr("%1 escapes %2.").arg(name, whose));
            releaseEndedConditions();
        } else if (outcome != 0) {
            addLog(tr("%1 fails to escape %2.").arg(name, whose));
        }
    } else if (prompt.kind == Prompt::Kind::Hide) {
        bool hidden = outcome == 1;
        if (outcome == 0) {
            const int face = rollD20();
            const int total = face + hideCheckBonus(*combatant);
            hidden = total >= prompt.dc;
            addLog(tr("%1 rolls %2 for Stealth (%3 + %4): %5.")
                       .arg(name)
                       .arg(total)
                       .arg(face)
                       .arg(hideCheckBonus(*combatant))
                       .arg(hidden ? tr("hidden; %1 is the DC to find it").arg(total) : tr("not hidden")));
        } else {
            addLog(hidden ? tr("%1 is hidden.").arg(name) : tr("%1 fails to hide.").arg(name));
        }
        if (hidden) {
            const std::vector<ActiveCondition> was = combatant->conditions;
            removeCondition(*combatant, "invisible");
            ActiveCondition invisible;
            invisible.id = "invisible";
            invisible.source = tr("Hide").toStdString();
            invisible.endsOn = hideEndsOn();
            logConditionsGone(*combatant, was, invisible.id);
            if (addCondition(*combatant, invisible) == AddConditionResult::Added) {
                addLog(tr("%1 is Invisible (Hide).").arg(name));
            }
        }
    } else if (prompt.kind == Prompt::Kind::DeathSave) {
        DeathSaveResult result = DeathSaveResult::NotDying;
        if (outcome == 0) {
            const int face = rollD20();
            result = rollDeathSave(*combatant, face);
            addLog(tr("%1 rolls %2 on a death save and %3.").arg(name).arg(face).arg(deathSaveWords(result)));
        } else {
            adjustDeathSave(*combatant, outcome == 1, 1);
            addLog(tr("%1's death save: %2.").arg(name, outcome == 1 ? tr("success") : tr("failure")));
        }
        carryToSheet(*combatant);
        // The death save is the dying character's turn: it ends there, in the
        // same undo step. A 20 brings them back with 1 HP, and they act.
        if (isTheirTurn(*combatant) && result != DeathSaveResult::Revived && combatant->hp == 0) {
            QTimer::singleShot(0, this, [this] { rebuildPrompts(); });
            nextTurn(std::move(before));
            return;
        }
    } else {
        bool success = outcome == 1;
        if (outcome == 0) {
            const SaveRoll roll = rollSave(*combatant, prompt.ability, prompt.dc, rollD20());
            success = roll.success;
            addLog(tr("%1 rolls %2 (%3) against DC %4: %5.")
                       .arg(name)
                       .arg(roll.total)
                       .arg(roll.face)
                       .arg(prompt.dc)
                       .arg(success ? tr("success") : tr("failure")));
        }
        if (prompt.kind == Prompt::Kind::Aura) {
            if (const std::optional<AuraCheck> check = auraCheckFor(prompt)) {
                resolveAura(*combatant, *check, success);
            }
        } else if (prompt.kind == Prompt::Kind::Concentration) {
            if (!success) {
                addLog(tr("%1 loses concentration.").arg(name));
                const std::vector<ActiveCondition> linked = combatant->conditions;
                setConcentration(*combatant, "");
                logConditionsGone(*combatant, linked);
            } else if (outcome != 0) {
                addLog(tr("%1 keeps concentration.").arg(name));
            }
        } else if (success) {
            removeCondition(*combatant, prompt.conditionId);
            addLog(tr("%1 is no longer %2.").arg(name, conditionName(prompt.conditionId)));
        } else {
            // "Second Failure: the target has the Petrified condition instead."
            const bool concentrating = !combatant->concentration.empty();
            const std::vector<std::string> worse = worsenCondition(*combatant, prompt.conditionId);
            if (!worse.empty()) {
                addLog(tr("%1 is no longer %2 and is now %3.")
                           .arg(name, conditionName(prompt.conditionId), conditionList(worse)));
            }
            if (concentrating && combatant->concentration.empty()) {
                addLog(tr("%1 loses concentration.").arg(name));
            }
        }
    }
    QTimer::singleShot(0, this, [this] { rebuildPrompts(); });
    commit(std::move(before));
}

namespace {

// A labelled value: small muted label over the value.
QWidget* factBlock(const QString& label, const QString& value, const QString& objectName = {})
{
    auto* host = new QWidget;
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);
    auto* caption = makeMuted(label);
    caption->setWordWrap(false);
    auto* text = new QLabel(value);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (!objectName.isEmpty()) {
        text->setObjectName(objectName);
    }
    layout->addWidget(caption);
    layout->addWidget(text);
    return host;
}

QString joinNames(const std::vector<std::string>& names)
{
    QStringList list;
    for (const std::string& name : names) {
        list << QString::fromStdString(name);
    }
    return list.join(QStringLiteral(", "));
}

}  // namespace

void CombatPage::rebuildDetails(const Combatant& combatant)
{
    clearLayout(m_detailsLayout);
    const bool monster = isMonsterCombatant(combatant);
    const Monster* block = monster && combatant.statBlock.has_value() ? &*combatant.statBlock : nullptr;
    Character* character = monster ? nullptr : characterFor(combatant);

    // Ability scores: six tiles with the score, its modifier, and the save.
    const AbilityScores* scores = block != nullptr ? &block->abilities : character != nullptr ? &character->abilities
                                                                                               : nullptr;
    auto* abilities = new QGridLayout;
    abilities->setHorizontalSpacing(8);
    abilities->setVerticalSpacing(8);
    for (std::size_t i = 0; i < kAbilityOrder.size(); ++i) {
        const Ability ability = kAbilityOrder[i];
        auto* tile = new QFrame;
        tile->setProperty("role", QStringLiteral("tile"));
        tile->setObjectName(QStringLiteral("abilityTile%1").arg(i));
        auto* layout = new QVBoxLayout(tile);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(0);
        auto* name = makeMuted(QString::fromLatin1(abilityLabel(ability)));
        name->setWordWrap(false);
        name->setAlignment(Qt::AlignCenter);
        layout->addWidget(name);
        if (scores != nullptr) {
            const int score = abilityScore(*scores, ability);
            auto* modifier = new QLabel(QString::fromStdString(formatModifier(abilityModifier(score))));
            QFont big = modifier->font();
            big.setPointSizeF(big.pointSizeF() * 1.6);
            big.setWeight(QFont::DemiBold);
            modifier->setFont(big);
            modifier->setAlignment(Qt::AlignCenter);
            auto* value = makeMuted(QString::number(score));
            value->setAlignment(Qt::AlignCenter);
            layout->addWidget(modifier);
            layout->addWidget(value);
        }
        const bool proficient = block != nullptr       ? block->savingThrows[i].has_value()
                                : character != nullptr ? saveProficient(character->savingThrows, ability)
                                                       : false;
        auto* save = new QLabel(
            tr("Save %1").arg(QString::fromStdString(formatModifier(combatant.saveBonuses[i]))) +
            (proficient ? QStringLiteral("  \u25CF") : QString()));
        save->setAlignment(Qt::AlignCenter);
        save->setToolTip(proficient ? tr("Proficient") : tr("Not proficient"));
        save->setProperty("role", proficient ? QStringLiteral("accent") : QStringLiteral("muted"));
        layout->addSpacing(4);
        layout->addWidget(save);
        abilities->addWidget(tile, 0, static_cast<int>(i));
        abilities->setColumnStretch(static_cast<int>(i), 1);
    }
    m_detailsLayout->addLayout(abilities);
    if (scores == nullptr) {
        m_detailsLayout->addWidget(makeMuted(tr("No ability scores are stored for %1.")
                                                 .arg(QString::fromStdString(combatant.name))));
    }

    // Quick facts.
    auto* facts = new QHBoxLayout;
    facts->setSpacing(28);
    if (block != nullptr) {
        facts->addWidget(factBlock(tr("Speed"), QString::fromStdString(block->speed)));
        facts->addWidget(factBlock(tr("Passive Perception"), QString::number(block->passivePerception)));
        facts->addWidget(factBlock(tr("Challenge"), tr("CR %1 (%2 XP)")
                                                         .arg(QString::fromStdString(block->challengeRating))
                                                         .arg(monsterXp(*block))));
        if (block->legendaryActionUses > 0) {
            facts->addWidget(factBlock(tr("Legendary actions"),
                                       tr("%1 of %2").arg(combatant.economy.legendaryRemaining)
                                           .arg(block->legendaryActionUses)));
        }
    } else if (character != nullptr) {
        facts->addWidget(factBlock(tr("Speed"), character->speed.empty() ? tr("Not set")
                                                                          : QString::fromStdString(character->speed)));
        facts->addWidget(factBlock(tr("Passive Perception"), QString::number(character->passivePerception)));
        facts->addWidget(factBlock(tr("Proficiency bonus"),
                                   QString::fromStdString(formatModifier(proficiencyBonus(*character))),
                                   QStringLiteral("derivedModifiers")));
        facts->addWidget(factBlock(tr("Level"), QString::number(totalClassLevel(*character))));
    } else {
        facts->addWidget(makeMuted(tr("This character is not in the roster, so the sheet is not shown.")));
    }
    facts->addStretch(1);
    m_detailsLayout->addLayout(facts);

    // Defenses: one row per kind that applies.
    m_detailsLayout->addWidget(sectionRule());
    m_detailsLayout->addWidget(makeHeading(tr("Defenses")));
    auto* defenses = new QGridLayout;
    defenses->setHorizontalSpacing(16);
    defenses->setVerticalSpacing(6);
    int row = 0;
    auto addDefense = [&](const QString& label, const QString& value) {
        if (value.isEmpty()) {
            return;
        }
        auto* caption = makeMuted(label);
        caption->setWordWrap(false);
        auto* text = new QLabel(value);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        defenses->addWidget(caption, row, 0, Qt::AlignTop);
        defenses->addWidget(text, row, 1);
        ++row;
    };
    addDefense(tr("Resistant"), joinNames(combatant.defenses.resistances));
    addDefense(tr("Immune"), joinNames(combatant.defenses.immunities));
    addDefense(tr("Vulnerable"), joinNames(combatant.defenses.vulnerabilities));
    QStringList conditionNames;
    for (const std::string& id : combatant.conditionImmunities) {
        conditionNames << conditionName(id);
    }
    addDefense(tr("Cannot be"), conditionNames.join(QStringLiteral(", ")));
    defenses->setColumnStretch(1, 1);
    auto* defenseHost = new QWidget;
    defenseHost->setObjectName(QStringLiteral("combatantDefenses"));
    defenseHost->setLayout(defenses);
    m_detailsLayout->addWidget(defenseHost);
    defenseHost->setVisible(row > 0);
    if (row == 0) {
        m_detailsLayout->addWidget(makeMuted(tr("No resistances, immunities, or vulnerabilities.")));
    }

    // Spell slots, as pips.
    if (character != nullptr && !character->spellSlots.empty()) {
        m_detailsLayout->addWidget(sectionRule());
        m_detailsLayout->addWidget(makeHeading(tr("Spell slots")));
        auto* slotGrid = new QGridLayout;
        slotGrid->setHorizontalSpacing(14);
        slotGrid->setVerticalSpacing(6);
        int slotRow = 0;
        for (const SpellSlot& slot : character->spellSlots) {
            QString pips;
            for (int i = 0; i < slot.max; ++i) {
                pips += i < slot.current ? QStringLiteral("\u25CF ") : QStringLiteral("\u25CB ");
            }
            auto* level = makeMuted(slot.shortRest ? tr("Level %1 (Pact Magic)").arg(slot.level)
                                                   : tr("Level %1").arg(slot.level));
            level->setWordWrap(false);
            auto* marks = new QLabel(pips.trimmed());
            marks->setProperty("role", QStringLiteral("accent"));
            marks->setToolTip(tr("%1 of %2 left").arg(slot.current).arg(slot.max));
            auto* spend = new QPushButton(tr("Spend"));
            spend->setProperty("level", slot.level);
            spend->setEnabled(slot.current >= 1);
            makeQuiet(spend);
            slotGrid->addWidget(level, slotRow, 0);
            slotGrid->addWidget(marks, slotRow, 1);
            slotGrid->addWidget(spend, slotRow, 2);
            connect(spend, &QPushButton::clicked, this, &CombatPage::spendSelectedSlot);
            ++slotRow;
        }
        slotGrid->setColumnStretch(3, 1);
        m_detailsLayout->addLayout(slotGrid);
    }
    m_detailsLayout->addStretch(1);
}

// --- Edits on the selected combatant -------------------------------------------

void CombatPage::onInitiativeChanged(int value)
{
    Combatant* combatant = selectedCombatant();
    if (m_populating || combatant == nullptr) {
        return;
    }
    PageUndo before = capture();
    combatant->initiative = value;
    const QString id = QString::fromStdString(combatant->id);
    for (int i = 0; i < m_initiativeList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_initiativeList->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == id) {
            item->setText(0, QString::number(value));
        }
    }
    commit(std::move(before), EditKind::Initiative, {}, true);
}

void CombatPage::onInitiativeEditingFinished()
{
    Encounter* encounter = selectedEncounter();
    Combatant* combatant = selectedCombatant();
    if (m_populating || encounter == nullptr || combatant == nullptr) {
        return;
    }
    PageUndo before = capture();
    const std::string id = combatant->id;
    encounter->turnIndex = sortByInitiative(encounter->combatants, encounter->turnIndex);
    commit(std::move(before), EditKind::Initiative);
    rebuildCombatantList(id);
}

// --- The initiative list -------------------------------------------------------

void CombatPage::refreshInitiativeEntry()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    std::vector<std::string> ids;
    for (const Combatant& combatant : encounter->combatants) {
        if (isCharacterCombatant(combatant)) {
            ids.push_back(combatant.id);
        }
    }
    // The rows keep their places while totals are typed, so Tab and Enter go
    // down the list; the Turn order on the left shows the sorted result. They
    // are rebuilt only when the fight or who is in it changes.
    std::vector<std::string> shown;
    for (const InitiativeEntryRow& row : m_entryRows) {
        shown.push_back(row.combatantId);
    }
    std::vector<std::string> wanted = ids;
    std::sort(shown.begin(), shown.end());
    std::sort(wanted.begin(), wanted.end());
    if (m_entryEncounterId != encounter->id || shown != wanted) {
        m_entryEncounterId = encounter->id;
        m_entryRows.clear();
        clearLayout(m_entryGrid);
        int line = 0;
        for (const std::string& id : ids) {
            InitiativeEntryRow row;
            row.combatantId = id;
            row.name = new QLabel;
            row.name->setObjectName(QStringLiteral("initiativeEntryName"));
            row.bonus = makeMuted(QString());
            row.bonus->setObjectName(QStringLiteral("initiativeEntryBonus"));
            row.bonus->setWordWrap(false);
            row.box = makeNumberBox(-99, 99);
            row.box->setObjectName(QStringLiteral("initiativeEntryField"));
            row.box->setProperty("combatantId", QString::fromStdString(id));
            row.box->setFixedWidth(64);
            row.box->setToolTip(tr("This character's initiative total."));
            row.note = new QLabel;
            row.note->setObjectName(QStringLiteral("initiativeEntryNote"));
            row.note->setStyleSheet(QStringLiteral("color: %1;").arg(palette::bloodied.name()));
            m_entryGrid->addWidget(row.name, line, 0);
            m_entryGrid->addWidget(row.bonus, line, 1);
            m_entryGrid->addWidget(row.box, line, 2);
            m_entryGrid->addWidget(row.note, line, 3);
            connect(row.box, &QSpinBox::valueChanged, this,
                    [this, id](int value) { onEntryInitiativeChanged(id, value); });
            connect(row.box, &QSpinBox::editingFinished, this, &CombatPage::onEntryEditingFinished);
            m_entryRows.push_back(row);
            ++line;
        }
        // Enter moves down the list; after the last character, to Start combat
        // at the top of the page.
        for (std::size_t i = 0; i < m_entryRows.size(); ++i) {
            QWidget* next = i + 1 < m_entryRows.size() ? static_cast<QWidget*>(m_entryRows[i + 1].box)
                                                       : static_cast<QWidget*>(m_nextTurnButton);
            if (auto* edit = m_entryRows[i].box->findChild<QLineEdit*>()) {
                connect(edit, &QLineEdit::returnPressed, next, [next] {
                    next->setFocus(Qt::TabFocusReason);
                    if (auto* box = qobject_cast<QSpinBox*>(next)) {
                        box->selectAll();
                    }
                });
            }
            if (i > 0) {
                QWidget::setTabOrder(m_entryRows[i - 1].box, m_entryRows[i].box);
            }
        }
        if (!m_entryRows.empty()) {
            QWidget::setTabOrder(m_entryRows.back().box, m_nextTurnButton);
        }
    }

    int entered = 0;
    for (InitiativeEntryRow& row : m_entryRows) {
        const Combatant* combatant = combatantById(row.combatantId);
        if (combatant == nullptr) {
            continue;
        }
        row.name->setText(QString::fromStdString(combatant->name));
        const Character* character = characterFor(*combatant);
        row.bonus->setText(character == nullptr
                               ? QString()
                               : tr("bonus %1").arg(QString::fromStdString(formatModifier(initiativeModifier(*character)))));
        if (row.box->value() != combatant->initiative) {
            const QSignalBlocker blocker(row.box);
            row.box->setValue(combatant->initiative);
        }
        const bool missing = combatant->initiative == 0;
        row.note->setText(missing ? tr("not entered") : QString());
        if (!missing) {
            ++entered;
        }
    }
    m_entryEmpty->setVisible(m_entryRows.empty());
    m_rollPlayersButton->setEnabled(!m_entryRows.empty());
    int monsters = 0;
    int rolled = 0;
    for (const Combatant& combatant : encounter->combatants) {
        if (isMonsterCombatant(combatant)) {
            ++monsters;
            rolled += combatant.initiative != 0 ? 1 : 0;
        }
    }
    m_entryMonsterCount->setText(monsters == 0 ? tr("none in this fight")
                                 : rolled == 0 ? tr("not rolled yet")
                                               : tr("%1 of %2 rolled").arg(rolled).arg(monsters));
    m_entryCount->setText(m_entryRows.empty()
                              ? QString()
                              : tr("%1 of %2 entered").arg(entered).arg(static_cast<int>(m_entryRows.size())));
}

void CombatPage::onEntryInitiativeChanged(const std::string& combatantId, int value)
{
    Combatant* combatant = combatantById(combatantId);
    if (m_populating || combatant == nullptr || combatant->initiative == value) {
        return;
    }
    PageUndo before = capture();
    combatant->initiative = value;
    const QString id = QString::fromStdString(combatantId);
    for (int i = 0; i < m_initiativeList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_initiativeList->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == id) {
            item->setText(0, QString::number(value));
        }
    }
    // The whole pass down the list is one undo step (typed edits merge).
    commit(std::move(before), EditKind::Initiative, {}, true);
    refreshInitiativeEntry();
}

void CombatPage::onEntryEditingFinished()
{
    Encounter* encounter = selectedEncounter();
    if (m_populating || encounter == nullptr || encounter->started) {
        return;
    }
    PageUndo before = capture();
    encounter->turnIndex = sortByInitiative(encounter->combatants, encounter->turnIndex);
    if (before.encounter == *encounter) {
        flushPendingSave();  // the order is the same; write what was typed now
        return;
    }
    commit(std::move(before), EditKind::Initiative);
    // Redraws the sorted Turn order. Nobody is selected, so the list stays on
    // the right and keeps its rows (and the focus) as they are.
    rebuildCombatantList({});
}

void CombatPage::showInitiativeEntry()
{
    {
        const QSignalBlocker initiativeBlocker(m_initiativeList);
        const QSignalBlocker downBlocker(m_downList);
        m_initiativeList->setCurrentItem(nullptr);
        m_initiativeList->clearSelection();
        m_downList->setCurrentItem(nullptr);
        m_downList->clearSelection();
    }
    showCombatant();
    if (!m_entryRows.empty()) {
        m_entryRows.front().box->setFocus(Qt::OtherFocusReason);
        m_entryRows.front().box->selectAll();
    }
}

bool CombatPage::monstersWithoutInitiative()
{
    const Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || encounter->started) {
        return false;
    }
    bool any = false;
    for (const Combatant& combatant : encounter->combatants) {
        if (isMonsterCombatant(combatant)) {
            if (combatant.initiative != 0) {
                return false;  // they have rolled; a 0 among them is a real roll
            }
            any = true;
        }
    }
    return any;
}

QStringList CombatPage::charactersWithoutInitiative()
{
    QStringList names;
    const Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || encounter->started) {
        return names;
    }
    for (const Combatant& combatant : encounter->combatants) {
        if (isCharacterCombatant(combatant) && isInInitiative(combatant) && combatant.initiative == 0) {
            names << QString::fromStdString(combatant.name);
        }
    }
    return names;
}

void CombatPage::onHpChanged(int value)
{
    Combatant* combatant = selectedCombatant();
    if (m_populating || combatant == nullptr) {
        return;
    }
    PageUndo before = capture();
    setHitPoints(*combatant, value);
    carryToSheet(*combatant);
    if (m_hp->value() != combatant->hp) {
        const QSignalBlocker blocker(m_hp);
        m_hp->setValue(combatant->hp);
    }
    const std::string id = combatant->id;
    commit(std::move(before), EditKind::HitPoints, id, true);
    rebuildCombatantList(id);
}

void CombatPage::onTempHpChanged(int value)
{
    Combatant* combatant = selectedCombatant();
    if (m_populating || combatant == nullptr) {
        return;
    }
    PageUndo before = capture();
    setTemporaryHitPoints(*combatant, value);
    carryToSheet(*combatant);
    const std::string id = combatant->id;
    commit(std::move(before), EditKind::TemporaryHp, id, true);
    rebuildCombatantList(id);
}

void CombatPage::onExhaustionChanged(int value)
{
    Combatant* combatant = selectedCombatant();
    if (m_populating || combatant == nullptr) {
        return;
    }
    PageUndo before = capture();
    const bool wasDead = combatant->dead;
    setExhaustion(*combatant, value);
    carryToSheet(*combatant);
    const std::string id = combatant->id;
    if (!wasDead && combatant->dead) {
        addLog(tr("%1 reaches Exhaustion 6 and dies.").arg(QString::fromStdString(combatant->name)));
    }
    // Each pick from the list is its own undo step.
    commit(std::move(before), EditKind::Once, id);
}

void CombatPage::onEconomyToggled()
{
    Combatant* combatant = selectedCombatant();
    if (m_populating || combatant == nullptr) {
        return;
    }
    PageUndo before = capture();
    combatant->economy.actionUsed = m_actionUsed->isChecked();
    combatant->economy.bonusActionUsed = m_bonusUsed->isChecked();
    combatant->economy.reactionUsed = m_reactionUsed->isChecked();
    if (!combatant->economy.actionUsed) {
        combatant->economy.attacksRemaining = 0;
    }
    commit(std::move(before), EditKind::Once, combatant->id);
}

void CombatPage::afterDamage(Combatant& target, const DamageResult& result, const QString& source)
{
    const QString name = QString::fromStdString(target.name);
    QStringList notes;
    for (const std::string& note : result.notes) {
        notes << QString::fromStdString(note);
    }
    QString line = tr("%1: %2 takes %3 damage").arg(source, name).arg(result.taken);
    if (result.taken != result.rolled) {
        line += tr(" (%1 rolled; %2)").arg(result.rolled).arg(notes.join(QStringLiteral(", ")));
    }
    if (result.tempSpent > 0) {
        line += tr(", %1 from temporary HP").arg(result.tempSpent);
    }
    line += QLatin1Char('.');
    // Becoming Bloodied matters to some abilities (Rampage, Bloodied Fury).
    if (target.hp > 0 && isBloodied(target) && target.maxHp.has_value() &&
        (target.hp + result.hpLost) * 2 > *target.maxHp) {
        line += tr(" %1 is now Bloodied.").arg(name);
    }
    if (result.instantDeath) {
        line += tr(" Massive damage: %1 dies.").arg(name);
    } else if (result.died && isCharacterCombatant(target)) {
        line += tr(" %1 dies.").arg(name);
    } else if (result.died) {
        line += tr(" %1 drops.").arg(name);
    } else if (result.droppedToZero) {
        line += tr(" %1 drops to 0 HP and is dying.").arg(name);
    } else if (result.deathSaveFailures > 0) {
        line += tr(" %n death save failure(s).", nullptr, result.deathSaveFailures);
    }
    if (result.concentrationEnded) {
        line += tr(" Concentration ends.");
    }
    addLog(line);
    if (Encounter* encounter = selectedEncounter(); encounter != nullptr && result.taken > 0) {
        for (const std::string& trait : noteDamageTaken(*encounter, target, result)) {
            addLog(tr("%1 has Disadvantage on attack rolls until the end of its next turn (%2).")
                       .arg(name, QString::fromStdString(trait)));
        }
    }
    if (result.taken > 0) {
        // "Until it takes damage."
        for (const ActiveCondition& ended : endConditionsOnDamage(target, m_damageAttackerId, m_damageAction)) {
            addLog(tr("%1 is no longer %2.").arg(name, conditionName(ended.id)));
        }
    }
    if (result.concentrationDc.has_value()) {
        Prompt prompt;
        prompt.kind = Prompt::Kind::Concentration;
        prompt.combatantId = target.id;
        prompt.ability = Ability::Constitution;
        prompt.dc = *result.concentrationDc;
        m_prompts.push_back(prompt);
        rebuildPrompts();
    }
    carryToSheet(target);
}

std::vector<std::string> CombatPage::hitTargets()
{
    std::vector<std::string> ids;
    for (const QTreeWidget* list : {m_initiativeList, m_downList}) {
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            const QTreeWidgetItem* item = list->topLevelItem(i);
            if (item->isSelected()) {
                ids.push_back(item->data(0, Qt::UserRole).toString().toStdString());
            }
        }
    }
    if (ids.empty()) {
        if (const Combatant* combatant = selectedCombatant()) {
            ids.push_back(combatant->id);
        }
    }
    return ids;
}

void CombatPage::openHitPanel(bool healing)
{
    m_healing = healing;
    m_hitPanel->show();
    refreshHitPanel();
    (healing ? m_healAmount : m_damageAmount)->setFocus();
    (healing ? m_healAmount : m_damageAmount)->selectAll();
}

void CombatPage::refreshHitPanel()
{
    const std::vector<std::string> targets = hitTargets();
    QStringList names;
    for (const std::string& id : targets) {
        names << nameOf(id);
    }
    m_hitTargets->setText(names.isEmpty() ? tr("Select one or more creatures in the turn order.")
                                          : (m_healing ? tr("Heal %1") : tr("Damage %1"))
                                                .arg(names.join(QStringLiteral(", "))));
    m_damageRow->setVisible(!m_healing);
    m_healRow->setVisible(m_healing);
    // One "saved" tick per target, for spells that deal half on a save.
    clearLayout(m_savedLayout);
    m_savedHost->setVisible(!m_healing && !targets.empty());
    int index = 0;
    for (const std::string& id : targets) {
        auto* box = new QCheckBox(tr("%1 saved (half)").arg(nameOf(id)));
        box->setObjectName(QStringLiteral("savedHalf"));
        box->setProperty("combatantId", QString::fromStdString(id));
        m_savedLayout->addWidget(box, index / 3, index % 3);
        ++index;
    }
}

void CombatPage::applySelectedDamage()
{
    const int amount = m_damageAmount->value();
    const std::vector<std::string> targets = hitTargets();
    if (amount <= 0 || targets.empty()) {
        return;
    }
    std::vector<std::string> saved;
    for (const QCheckBox* box : m_savedHost->findChildren<QCheckBox*>(QStringLiteral("savedHalf"))) {
        if (box->isChecked()) {
            saved.push_back(box->property("combatantId").toString().toStdString());
        }
    }
    PageUndo before = capture();
    const std::string type = m_damageType->currentData().toString().toStdString();
    for (const std::string& id : targets) {
        Combatant* combatant = combatantById(id);
        if (combatant == nullptr || combatant->dead) {
            continue;
        }
        const bool half = std::find(saved.begin(), saved.end(), id) != saved.end();
        const DamageResult result =
            applyDamage(*combatant, half ? amount / 2 : amount, type, m_damageCritical->isChecked());
        afterDamage(*combatant, result, half ? tr("Damage (saved, half)") : tr("Damage"));
    }
    m_damageAmount->setValue(0);
    m_damageCritical->setChecked(false);
    m_hitPanel->hide();
    commit(std::move(before), EditKind::Once, targets.front());
}

void CombatPage::applySelectedHealing()
{
    const int amount = m_healAmount->value();
    const std::vector<std::string> targets = hitTargets();
    if (amount <= 0 || targets.empty()) {
        return;
    }
    PageUndo before = capture();
    for (const std::string& id : targets) {
        Combatant* combatant = combatantById(id);
        if (combatant == nullptr) {
            continue;
        }
        const HealingResult result = applyHealing(*combatant, amount);
        carryToSheet(*combatant);
        const QString name = QString::fromStdString(combatant->name);
        if (combatant->dead) {
            addLog(tr("%1 is dead; healing does nothing. Set HP to bring them back.").arg(name));
        } else {
            addLog(result.revived ? tr("%1 regains %2 HP and is back up.").arg(name).arg(result.healed)
                                  : tr("%1 regains %2 HP.").arg(name).arg(result.healed));
        }
    }
    m_healAmount->setValue(0);
    m_hitPanel->hide();
    commit(std::move(before), EditKind::Once, targets.front());
}

void CombatPage::addSelectedCondition()
{
    Combatant* combatant = selectedCombatant();
    Encounter* encounter = selectedEncounter();
    if (combatant == nullptr || encounter == nullptr || m_conditionPicker->currentIndex() < 0) {
        return;
    }
    ActiveCondition condition;
    condition.id = m_conditionPicker->currentData().toString().toStdString();
    if (m_durationKind->currentIndex() > 0 && m_durationAnchor->currentIndex() >= 0) {
        condition.duration = makeDuration(*encounter, m_durationAnchor->currentData().toString().toStdString(),
                                          m_durationKind->currentIndex() == 1 ? TurnBoundary::Start : TurnBoundary::End,
                                          m_durationTurns->value());
    }
    if (m_saveEnds->isChecked()) {
        condition.saveEnds =
            SaveEnds{kAbilityOrder[static_cast<std::size_t>(m_saveEndsAbility->currentIndex())], m_saveEndsDc->value()};
    }
    PageUndo before = capture();
    const bool concentrating = !combatant->concentration.empty();
    const QString name = QString::fromStdString(combatant->name);
    const QString what = conditionName(condition.id);
    if (condition.id == "invisible") {
        const std::vector<ActiveCondition> was = combatant->conditions;
        if (m_invisibleCause->currentIndex() == 1) {
            applyAbilityEffect(*combatant, SelfEffect{"invisible", tr("Invisibility").toStdString(), "invisibility",
                                                      invisibilitySpellEndsOn()});
            commit(std::move(before), EditKind::Once, combatant->id);
            return;
        }
        // A new cause replaces the old one.
        removeCondition(*combatant, condition.id);
        if (m_invisibleCause->currentIndex() == 0) {
            condition.source = tr("Hide").toStdString();
            condition.endsOn = hideEndsOn();
        }
        logConditionsGone(*combatant, was, condition.id);
    }
    switch (addCondition(*combatant, condition)) {
    case AddConditionResult::Added:
        addLog(condition.id == "exhaustion" ? tr("%1 is at Exhaustion %2.").arg(name).arg(combatant->exhaustion)
                                            : tr("%1 is %2.").arg(name, what));
        if (concentrating && combatant->concentration.empty()) {
            addLog(tr("%1 loses concentration.").arg(name));
        }
        carryToSheet(*combatant);
        break;
    case AddConditionResult::Immune:
        addLog(tr("%1 cannot be %2.").arg(name, what));
        break;
    case AddConditionResult::Duplicate:
        addLog(tr("%1 is already %2.").arg(name, what));
        break;
    case AddConditionResult::Empty:
        return;
    }
    commit(std::move(before), EditKind::Once, combatant->id);
}

void CombatPage::removeListedCondition(const QString& conditionId)
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr || conditionId.isEmpty()) {
        return;
    }
    PageUndo before = capture();
    const bool concentrating = !combatant->concentration.empty();
    removeCondition(*combatant, conditionId.toStdString());
    if (concentrating && combatant->concentration.empty()) {
        // Ending the Invisibility spell or Vanish ends its concentration.
        addLog(tr("%1's concentration ends.").arg(QString::fromStdString(combatant->name)));
    }
    commit(std::move(before), EditKind::Once, combatant->id);
}

void CombatPage::refreshConcentrationChoices(const QString& text)
{
    if (m_spellMatches == nullptr) {
        return;
    }
    m_spellMatches->clear();
    for (const Spell& spell : searchSpells(m_spells, text.toStdString())) {
        if (!spell.concentration && !text.isEmpty()) {
            continue;
        }
        auto* item = new QListWidgetItem(QString::fromStdString(spell.name));
        item->setData(Qt::UserRole, QString::fromStdString(spell.id));
        m_spellMatches->addItem(item);
    }
}

void CombatPage::setSelectedConcentration()
{
    Combatant* combatant = selectedCombatant();
    const QListWidgetItem* item = m_spellMatches->currentItem();
    if (combatant == nullptr || item == nullptr) {
        return;
    }
    if (isIncapacitated(*combatant)) {
        addLog(tr("%1 is incapacitated and cannot concentrate.").arg(QString::fromStdString(combatant->name)));
        return;
    }
    PageUndo before = capture();
    const std::string spellId = item->data(Qt::UserRole).toString().toStdString();
    // Concentrating means the spell was just cast: casting can end Invisible
    // (any spell for the Invisibility spell, a Verbal one for Hide).
    if (const std::optional<Spell> spell = findSpellById(m_spells, spellId)) {
        endByEvents(*combatant, spellEvents(*spell));
    }
    const QString was = concentrationName(combatant->concentration);
    const std::vector<ActiveCondition> linked = combatant->conditions;
    setConcentration(*combatant, spellId);
    if (!was.isEmpty() && was != concentrationName(spellId)) {
        addLog(tr("%1 stops concentrating on %2.").arg(QString::fromStdString(combatant->name), was));
    }
    logConditionsGone(*combatant, linked);
    commit(std::move(before), EditKind::Once, combatant->id);
}

void CombatPage::clearSelectedConcentration()
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr || combatant->concentration.empty()) {
        return;
    }
    PageUndo before = capture();
    const std::vector<ActiveCondition> linked = combatant->conditions;
    setConcentration(*combatant, "");
    logConditionsGone(*combatant, linked);
    commit(std::move(before), EditKind::Once, combatant->id);
}

void CombatPage::adjustSelectedDeathSave(bool success, int delta)
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr || isMonsterCombatant(*combatant)) {
        return;
    }
    PageUndo before = capture();
    const bool wasDead = combatant->dead;
    adjustDeathSave(*combatant, success, delta);
    if (!wasDead && combatant->dead) {
        addLog(tr("%1 fails a third death save and dies.").arg(QString::fromStdString(combatant->name)));
    }
    commit(std::move(before), EditKind::Once, combatant->id);
}

void CombatPage::stabilizeSelected()
{
    Combatant* combatant = selectedCombatant();
    if (combatant == nullptr) {
        return;
    }
    PageUndo before = capture();
    if (stabilize(*combatant)) {
        addLog(tr("%1 is stable.").arg(QString::fromStdString(combatant->name)));
    }
    commit(std::move(before), EditKind::Once, combatant->id);
}

void CombatPage::spendSelectedSlot()
{
    Combatant* combatant = selectedCombatant();
    auto* button = qobject_cast<QPushButton*>(sender());
    if (combatant == nullptr || button == nullptr) {
        return;
    }
    Character* character = characterFor(*combatant);
    if (character == nullptr) {
        return;
    }
    PageUndo before = capture();
    if (!spendSpellSlot(*character, button->property("level").toInt())) {
        return;
    }
    const std::string id = combatant->id;
    // The button lives in the slot layout, which a rebuild deletes, so finish
    // after this click returns.
    auto undo = std::make_shared<PageUndo>(std::move(before));
    QTimer::singleShot(0, this, [this, id, undo] { commit(std::move(*undo), EditKind::Once, id); });
}

// --- Initiative, turns, and removal -----------------------------------------

void CombatPage::rollAll()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    PageUndo before = capture();
    const int missing =
        rollAllMonsterInitiatives(*encounter, [this] { return rollD20(); }, m_groupInitiative);
    if (missing == 0) {
        m_rollNote->hide();
    } else {
        m_rollNote->setText(tr("%n monster(s) had no initiative bonus stored. Those rolls used +0.", nullptr, missing));
        m_rollNote->show();
    }
    addLog(tr("Rolled initiative for the monsters."));
    commit(std::move(before));
}

void CombatPage::rollPlayers()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    PageUndo before = capture();
    std::map<std::string, int> bonuses;
    for (const Combatant& combatant : encounter->combatants) {
        if (const Character* character = isCharacterCombatant(combatant) ? characterFor(combatant) : nullptr) {
            bonuses[combatant.id] = initiativeModifier(*character);
        }
    }
    rollAllCharacterInitiatives(*encounter, [this] { return rollD20(); }, bonuses);
    addLog(tr("Rolled initiative for the players."));
    commit(std::move(before));
}

void CombatPage::rerollSelected()
{
    Encounter* encounter = selectedEncounter();
    Combatant* combatant = selectedCombatant();
    if (encounter == nullptr || combatant == nullptr || !isMonsterCombatant(*combatant)) {
        return;
    }
    const std::string id = combatant->id;
    PageUndo before = capture();
    bool missing = false;
    if (!rerollMonsterInitiative(*encounter, id, [this] { return rollD20(); }, &missing)) {
        return;
    }
    if (missing) {
        m_rollNote->setText(tr("The initiative bonus was missing. That roll used +0."));
        m_rollNote->show();
    }
    commit(std::move(before), EditKind::Once, id);
}

void CombatPage::removeSelected()
{
    Encounter* encounter = selectedEncounter();
    Combatant* combatant = selectedCombatant();
    if (encounter == nullptr || combatant == nullptr) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Remove combatant"),
        tr("Remove %1 from this fight? The character or monster itself is not deleted. Undo puts them back.")
            .arg(QString::fromStdString(combatant->name)));
    if (answer != QMessageBox::Yes) {
        return;
    }
    disarmAttack();
    PageUndo before = capture();
    const std::string id = combatant->id;
    addLog(tr("%1 leaves the fight.").arg(QString::fromStdString(combatant->name)));
    encounter->turnIndex = removeCombatant(encounter->combatants, indexOfId(*encounter, id), encounter->turnIndex);
    assignMonsterCopyNames(encounter->combatants);
    m_prompts.erase(std::remove_if(m_prompts.begin(), m_prompts.end(),
                                   [&id](const Prompt& prompt) { return prompt.combatantId == id; }),
                    m_prompts.end());
    rebuildPrompts();
    commit(std::move(before));
}

void CombatPage::nextTurn()
{
    disarmAttack();
    const QStringList missing = charactersWithoutInitiative();
    const bool monstersUnrolled = monstersWithoutInitiative();
    if (!missing.isEmpty() || monstersUnrolled) {
        QStringList problems;
        if (!missing.isEmpty()) {
            problems << tr("No initiative has been entered for %1.").arg(missing.join(tr(", ")));
        }
        if (monstersUnrolled) {
            problems << tr("The monsters have not rolled initiative.");
        }
        const auto answer = QMessageBox::question(
            this, tr("Start combat"), problems.join(QLatin1Char(' ')) + tr(" Start combat anyway?"));
        if (answer != QMessageBox::Yes) {
            showInitiativeEntry();
            for (const InitiativeEntryRow& row : m_entryRows) {
                if (row.box->value() == 0) {
                    row.box->setFocus(Qt::OtherFocusReason);
                    row.box->selectAll();
                    break;
                }
            }
            return;
        }
    }
    nextTurn(capture());
}

void CombatPage::nextTurn(PageUndo before)
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    disarmAttack();
    m_damageChoices.clear();
    const bool starting = !encounter->started;
    const std::vector<TurnEvent> events =
        starting ? startCombat(*encounter, dieRoller()) : advanceTurn(*encounter, dieRoller());
    if (starting) {
        addLog(tr("Combat starts: round 1, %1 goes first.").arg(nameOf(currentTurnId())));
    }
    handleTurnEvents(events);
    addAuraPrompts(currentTurnId());
    commit(std::move(before), EditKind::Once, currentTurnId());
}

void CombatPage::addAuraPrompts(const std::string& combatantId)
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr || combatantId.empty()) {
        return;
    }
    for (const AuraCheck& check : aurasAtTurnStart(*encounter, combatantId)) {
        Prompt prompt;
        prompt.kind = Prompt::Kind::Aura;
        prompt.combatantId = combatantId;
        prompt.sourceId = check.sourceId;
        prompt.auraName = check.name;
        prompt.ability = check.aura.ability;
        prompt.dc = check.aura.dc;
        m_prompts.push_back(prompt);
    }
    rebuildPrompts();
}

std::optional<AuraCheck> CombatPage::auraCheckFor(const Prompt& prompt)
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return std::nullopt;
    }
    for (const AuraCheck& check : aurasAtTurnStart(*encounter, prompt.combatantId)) {
        if (check.sourceId == prompt.sourceId && check.name == prompt.auraName) {
            return check;
        }
    }
    return std::nullopt;
}

void CombatPage::resolveAura(Combatant& target, const AuraCheck& check, bool saved)
{
    Encounter* encounter = selectedEncounter();
    const QString name = QString::fromStdString(target.name);
    const QString aura = QString::fromStdString(check.name);
    const QString source = nameOf(check.sourceId);
    if (saved) {
        if (check.aura.immuneOnSuccess) {
            markAuraImmune(target, check);
            addLog(tr("%1 saves against %2's %3 and is immune to it for the rest of the fight.")
                       .arg(name, source, aura));
        } else {
            addLog(tr("%1 saves against %2's %3.").arg(name, source, aura));
        }
        return;
    }
    if (check.aura.failureDie > 0) {
        const int roll = rollDie(check.aura.failureDie);
        const std::string outcome = auraFailureOutcome(check.aura, roll);
        addLog(tr("%1 fails against %2's %3 and rolls %4 on a d%5: %6.")
                   .arg(name, source, aura)
                   .arg(roll)
                   .arg(check.aura.failureDie)
                   .arg(outcome.empty() ? tr("see the trait") : QString::fromStdString(outcome)));
    }
    if (!check.aura.condition.empty() && encounter != nullptr) {
        const bool concentrating = !target.concentration.empty();
        switch (applyAuraFailure(*encounter, target, check)) {
        case AddConditionResult::Added:
            addLog(tr("%1 is %2 until the start of its next turn (%3).")
                       .arg(name, conditionName(check.aura.condition), aura));
            break;
        case AddConditionResult::Immune:
            addLog(tr("%1 cannot be %2.").arg(name, conditionName(check.aura.condition)));
            break;
        case AddConditionResult::Duplicate:
        case AddConditionResult::Empty:
            break;
        }
        if (concentrating && target.concentration.empty()) {
            addLog(tr("%1 loses concentration.").arg(name));
        }
    }
}

void CombatPage::toggleAura(const std::string& monsterId, const std::string& auraName)
{
    Combatant* monster = combatantById(monsterId);
    if (monster == nullptr) {
        return;
    }
    PageUndo before = capture();
    const auto found = std::find(monster->aurasOff.begin(), monster->aurasOff.end(), auraName);
    const QString name = QString::fromStdString(monster->name);
    if (found == monster->aurasOff.end()) {
        monster->aurasOff.push_back(auraName);
        addLog(tr("%1's %2 is off.").arg(name, QString::fromStdString(auraName)));
    } else {
        monster->aurasOff.erase(found);
        addLog(tr("%1's %2 is on again.").arg(name, QString::fromStdString(auraName)));
    }
    commit(std::move(before), EditKind::Once, monsterId);
}

void CombatPage::handleTurnEvents(const std::vector<TurnEvent>& events)
{
    for (const TurnEvent& event : events) {
        const QString name = nameOf(event.combatantId);
        switch (event.kind) {
        case TurnEvent::Kind::ConditionEnded:
            addLog(tr("%1 is no longer %2.").arg(name, conditionName(event.conditionId)));
            break;
        case TurnEvent::Kind::SaveToEnd: {
            Prompt prompt;
            prompt.kind = Prompt::Kind::SaveToEnd;
            prompt.combatantId = event.combatantId;
            prompt.conditionId = event.conditionId;
            prompt.ability = event.ability;
            prompt.dc = event.dc;
            m_prompts.push_back(prompt);
            break;
        }
        case TurnEvent::Kind::DeathSave: {
            Prompt prompt;
            prompt.kind = Prompt::Kind::DeathSave;
            prompt.combatantId = event.combatantId;
            m_prompts.push_back(prompt);
            break;
        }
        case TurnEvent::Kind::Recharged:
            addLog(tr("%1 rolls %2: %3 recharges.")
                       .arg(name)
                       .arg(event.roll)
                       .arg(QString::fromStdString(event.actionName)));
            break;
        case TurnEvent::Kind::NotRecharged:
            addLog(tr("%1 rolls %2: %3 does not recharge.")
                       .arg(name)
                       .arg(event.roll)
                       .arg(QString::fromStdString(event.actionName)));
            break;
        case TurnEvent::Kind::OngoingDamage:
            if (Combatant* target = combatantById(event.combatantId); target != nullptr && !target->dead) {
                const std::vector<TypedDamage> damage = rollDamageParts(event.damage, DamageOptions{}, dieRoller());
                const DamageResult result = applyDamage(*target, damage);
                QString source = QString::fromStdString(event.source);
                const qsizetype comma = source.indexOf(QLatin1Char(','));
                if (comma > 0) {
                    source = source.left(comma);  // without the escape DC
                }
                afterDamage(*target, result, source.isEmpty() ? tr("Ongoing damage") : source);
            }
            break;
        }
    }
    rebuildPrompts();
}

// --- Monster actions -----------------------------------------------------------

void CombatPage::onActionClicked(const std::string& attackerId, const MonsterAttack& attack)
{
    if (m_armed.has_value() && m_armed->attackerId == attackerId && !m_armed->featureKind.has_value() &&
        m_armed->attack.name == attack.name) {
        stopTargeting();  // done choosing (a breath weapon's last creature)
        return;
    }
    Combatant* attacker = combatantById(attackerId);
    if (attacker == nullptr) {
        return;
    }
    const bool theirTurn = isTheirTurn(*attacker);
    const Availability available = actionAvailability(*attacker, attack, theirTurn);
    if (!available.available) {
        addLog(tr("%1 cannot use %2: %3.")
                   .arg(QString::fromStdString(attacker->name), QString::fromStdString(attack.name),
                        QString::fromStdString(available.reason)));
        return;
    }
    const QString name = QString::fromStdString(attacker->name);
    const bool needsTarget = attack.attackBonus.has_value() || attack.save.has_value() || attack.benefit.has_value() ||
                             !attackDamageParts(attack).empty();
    if (isMultiattack(attack) || !needsTarget) {
        PageUndo before = capture();
        useAction(*attacker, attack, theirTurn);
        addLog(isMultiattack(attack) ? tr("%1 takes the Multiattack action (%n use(s)).", nullptr, attack.count).arg(name)
                                     : tr("%1 uses %2.").arg(name, QString::fromStdString(attack.name)));
        endByEvents(*attacker, actionEvents(attack.name, attack.effect, &attack, false));
        applyAbilityEffect(*attacker, attack.selfEffect);
        const std::string id = attacker->id;
        commit(std::move(before), EditKind::Once, id);
        maybeAutoPass(id);
        return;
    }
    ArmedAction armed;
    armed.attackerId = attackerId;
    armed.attack = attack;
    if (attack.save.has_value()) {
        armed.saveDamage =
            rollDamageParts(attackDamageParts(attack), damageOptionsFor(*attacker, nullptr, attack), dieRoller());
    }
    armAction(std::move(armed));
}

void CombatPage::onFeatureClicked(const std::string& combatantId, FeatureKind kind, const MonsterFeature& feature)
{
    if (m_armed.has_value() && m_armed->attackerId == combatantId && m_armed->featureKind.has_value() &&
        m_armed->feature.name == feature.name) {
        stopTargeting();  // done choosing (a breath weapon's last creature)
        return;
    }
    Combatant* combatant = combatantById(combatantId);
    if (combatant == nullptr) {
        return;
    }
    if (feature.targeted.has_value()) {
        // Aimed like an action: spent on the first target.
        const Availability available = featureAvailability(*combatant, kind, feature, isTheirTurn(*combatant));
        if (!available.available) {
            addLog(tr("%1 cannot use %2: %3.")
                       .arg(QString::fromStdString(combatant->name), QString::fromStdString(feature.name),
                            QString::fromStdString(available.reason)));
            return;
        }
        ArmedAction armed;
        armed.attackerId = combatantId;
        armed.attack = *feature.targeted;
        armed.featureKind = kind;
        armed.feature = feature;
        if (armed.attack.save.has_value()) {
            armed.saveDamage = rollDamageParts(attackDamageParts(armed.attack),
                                               damageOptionsFor(*combatant, nullptr, armed.attack), dieRoller());
        }
        armAction(std::move(armed));
        return;
    }
    PageUndo before = capture();
    if (!useFeature(*combatant, kind, feature, isTheirTurn(*combatant))) {
        return;
    }
    const QString name = QString::fromStdString(combatant->name);
    QString line = tr("%1 uses %2.").arg(name, QString::fromStdString(feature.name));
    if (feature.perDay.has_value()) {
        const auto left = combatant->usesRemaining.find(feature.name);
        line += tr(" %n use(s) left today.", nullptr, left == combatant->usesRemaining.end() ? 0 : left->second);
    }
    if (kind == FeatureKind::Legendary) {
        line += tr(" %n legendary use(s) left.", nullptr, combatant->economy.legendaryRemaining);
    }
    if (!combatant->economy.grantedAttack.empty()) {
        line += tr(" Its %1 attack can be used now.").arg(QString::fromStdString(combatant->economy.grantedAttack));
    }
    addLog(line);
    endByEvents(*combatant, actionEvents(feature.name, feature.effect, nullptr, false));
    applyAbilityEffect(*combatant, feature.selfEffect);
    const std::string id = combatant->id;
    commit(std::move(before), EditKind::Once, id);
    if (kind == FeatureKind::BonusAction) {
        maybeAutoPass(id);
    }
}

void CombatPage::onStandardActionClicked(const std::string& combatantId, FeatureKind kind, const MonsterFeature& feature,
                                         const std::vector<std::string>& actions)
{
    Combatant* combatant = combatantById(combatantId);
    if (combatant == nullptr) {
        return;
    }
    PageUndo before = capture();
    if (!useFeature(*combatant, kind, feature, isTheirTurn(*combatant))) {
        return;
    }
    const QString name = QString::fromStdString(combatant->name);
    QStringList words;
    for (const std::string& action : actions) {
        words << QString::fromStdString(action);
    }
    addLog(tr("%1 uses %2: %3.").arg(name, QString::fromStdString(feature.name), words.join(tr(" and "))));
    for (const std::string& action : actions) {
        if (action == "Hide") {
            // Hide is a check: asked at the top, rolled or entered there.
            Prompt prompt;
            prompt.kind = Prompt::Kind::Hide;
            prompt.combatantId = combatant->id;
            prompt.ability = Ability::Dexterity;
            prompt.dc = kHideDc;
            prompt.auraName = feature.name;
            m_prompts.push_back(prompt);
        } else {
            takeStandardAction(*combatant, action);
        }
    }
    endByEvents(*combatant, actionEvents(feature.name, feature.effect, nullptr, false));
    const std::string id = combatant->id;
    commit(std::move(before), EditKind::Once, id);
    rebuildPrompts();
    if (kind == FeatureKind::BonusAction) {
        maybeAutoPass(id);
    }
}

void CombatPage::armAction(ArmedAction action)
{
    // Nothing is logged until a target is clicked: the sword cursor and the
    // End button show the action is ready. Undoing the first target puts
    // the log back as it was here.
    action.logBefore = m_log;
    // Help (healing, a War Cry) gets the sparkle; everything else the sword.
    const bool helpful = action.fixedHealing.has_value() || action.attack.benefit.has_value();
    m_armed = std::move(action);
    if (m_swordCursor) {
        QApplication::changeOverrideCursor(helpful ? helpCursor() : swordCursor());
    } else {
        QApplication::setOverrideCursor(helpful ? helpCursor() : swordCursor());
        m_swordCursor = true;
    }
    showArmedTargets();
    showCombatant();
}

void CombatPage::stopTargeting()
{
    // A spent action (a breath weapon that caught its creatures) may have been
    // the monster's last thing to do: its turn passes, as after any action.
    const bool spent = m_armed.has_value() && m_armed->spent && !m_armed->targets.empty();
    const std::string attackerId = m_armed.has_value() ? m_armed->attackerId : std::string();
    disarmAttack();
    rebuildCombatantList(attackerId);  // back to the action's user, its row selected
    if (spent) {
        maybeAutoPass(attackerId);
    }
}

void CombatPage::disarmAttack()
{
    if (!m_armed.has_value()) {
        return;
    }
    const std::string userId = m_armed->attackerId;
    m_armed.reset();
    if (m_swordCursor) {
        QApplication::restoreOverrideCursor();
        m_swordCursor = false;
    }
    // The targets' highlight goes; the action's user is selected again.
    selectRow(userId);
}

void CombatPage::onTargetClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!m_armed.has_value() || item == nullptr) {
        return;
    }
    const std::string targetId = item->data(0, Qt::UserRole).toString().toStdString();
    showArmedTargets();  // the click moved the current row; it belongs to the user
    resolveArmedOn(targetId);
    // Still choosing (an area, or a target that was turned down): the picked
    // creatures stay highlighted, not the one just clicked.
    showArmedTargets();
}

void CombatPage::resolveArmedOn(const std::string& targetId)
{
    if (!m_armed.has_value()) {
        return;
    }
    ArmedAction& armed = *m_armed;
    Combatant* attacker = combatantById(armed.attackerId);
    Combatant* target = combatantById(targetId);
    if (attacker == nullptr || target == nullptr) {
        disarmAttack();
        return;
    }
    if (std::find(armed.targets.begin(), armed.targets.end(), targetId) != armed.targets.end()) {
        addLog(tr("%1 has already been targeted.").arg(QString::fromStdString(target->name)));
        return;
    }
    if (target->dead || (isMonsterCombatant(*target) && !isInInitiative(*target))) {
        addLog(tr("%1 is already down. Pick another target, or Escape.").arg(QString::fromStdString(target->name)));
        return;
    }
    // Charmed: no attacking the charmer, and nothing harmful aimed at it.
    // Helping it (healing, a benefit) is still allowed.
    if (!armed.fixedHealing.has_value() && !armed.attack.benefit.has_value()) {
        const std::string problem = charmedProblem(*attacker, *target);
        if (!problem.empty()) {
            addLog(QString::fromStdString(problem) + tr(" Pick another target, or Escape."));
            return;
        }
    }
    // "One Frightened creature", "one creature Grappled by the chuul", "one
    // Medium or smaller creature".
    {
        const Character* sheet = characterFor(*target);
        const std::string problem =
            targetRequirementProblem(armed.attack, *target, sheet != nullptr ? sheet->species : std::string(),
                                     attacker->id);
        if (!problem.empty()) {
            addLog(QString::fromStdString(problem) + tr(" Pick another target, or Escape."));
            return;
        }
    }
    // Damage from here on is this action's: a condition that spares it (the
    // Vampire's Bite on its Charmed target) stays.
    m_damageAttackerId = attacker->id;
    m_damageAction = armed.attack.name;
    struct ClearDamageSource {
        CombatPage* page;
        ~ClearDamageSource()
        {
            page->m_damageAttackerId.clear();
            page->m_damageAction.clear();
        }
    } clearDamageSource{this};
    PageUndo before = capture();
    if (armed.targets.empty()) {
        before.log = armed.logBefore;
    }
    if (!armed.spent && armed.featureKind.has_value()) {
        if (!useFeature(*attacker, *armed.featureKind, armed.feature, isTheirTurn(*attacker))) {
            addLog(tr("%1 cannot use %2 now.").arg(QString::fromStdString(attacker->name),
                                                 QString::fromStdString(armed.feature.name)));
            disarmAttack();
            commit(std::move(before));
            return;
        }
        armed.spent = true;
        if (armed.feature.perDay.has_value()) {
            const auto left = attacker->usesRemaining.find(armed.feature.name);
            addLog(tr("%1: %n use(s) left today.", nullptr,
                      left == attacker->usesRemaining.end() ? 0 : left->second)
                       .arg(QString::fromStdString(armed.feature.name)));
        }
        if (*armed.featureKind == FeatureKind::Legendary) {
            addLog(tr("%1 has %n legendary use(s) left.", nullptr, attacker->economy.legendaryRemaining)
                       .arg(QString::fromStdString(attacker->name)));
        }
    }
    if (!armed.spent) {
        if (!useAction(*attacker, armed.attack, isTheirTurn(*attacker))) {
            addLog(tr("%1 cannot use %2 now.").arg(QString::fromStdString(attacker->name),
                                                 QString::fromStdString(armed.attack.name)));
            disarmAttack();
            commit(std::move(before));
            return;
        }
        armed.spent = true;
    }
    armed.targets.push_back(targetId);
    // Rampage: "after dealing damage to a creature that was already Bloodied".
    const bool targetWasBloodied =
        target->maxHp.has_value() && *target->maxHp > 0 && target->hp > 0 && target->hp * 2 <= *target->maxHp;
    const QString source = tr("%1's %2").arg(QString::fromStdString(attacker->name),
                                             QString::fromStdString(armed.attack.name));
    const MonsterAttack& attack = armed.attack;
    const std::vector<DamagePart> parts = attackDamageParts(attack);
    bool keepArmed = false;
    bool dealtDamage = false;

    if (armed.fixedHealing.has_value()) {
        // A character's healing, typed in.
        const QString targetName = QString::fromStdString(target->name);
        const HealingResult healed = applyHealing(*target, *armed.fixedHealing);
        addLog(healed.revived ? tr("%1 heals %2 for %3: %2 is back up with %4 HP.")
                                    .arg(QString::fromStdString(attacker->name), targetName)
                                    .arg(healed.healed)
                                    .arg(target->hp)
                              : tr("%1 heals %2 for %3 (%4 HP).")
                                    .arg(QString::fromStdString(attacker->name), targetName)
                                    .arg(healed.healed)
                                    .arg(target->hp));
        carryToSheet(*target);
    } else if (attack.benefit.has_value()) {
        // A helpful action: War Cry, Shimmering Shield.
        const BenefitResult result = applyBenefit(*selectedEncounter(), *attacker, *target, attack, dieRoller());
        const QString targetName = QString::fromStdString(target->name);
        QStringList what;
        if (result.tempHp > 0) {
            what << (result.tempHpKept ? tr("keeps its %1 temporary HP (%2 rolled)").arg(target->tempHp).arg(result.tempHp)
                                       : tr("gains %1 temporary HP").arg(result.tempHp));
        }
        if (result.healed > 0) {
            what << tr("regains %1 HP").arg(result.healed);
        }
        if (result.advantage) {
            what << tr("has Advantage on attack rolls");
        }
        if (result.acBonus != 0) {
            what << tr("gets +%1 AC (AC %2)").arg(result.acBonus).arg(target->ac);
        }
        QString until;
        if (result.advantage || result.acBonus != 0) {
            until = attack.benefit->until == kUntilSourceEnd
                        ? tr(" until the end of %1's next turn").arg(QString::fromStdString(attacker->name))
                        : tr(" until the start of %1's next turn").arg(QString::fromStdString(attacker->name));
        }
        addLog(tr("%1: %2 %3%4.").arg(source, targetName, what.join(tr(" and ")), until));
        carryToSheet(*target);
    } else if (armed.fixedDamage.has_value()) {
        // A character's attack: the player rolled it; the damage is typed in.
        const DamageResult result = applyDamage(*target, *armed.fixedDamage);
        addLog(tr("%1 hits %2 for %3.")
                   .arg(QString::fromStdString(attacker->name), QString::fromStdString(target->name),
                        QString::fromStdString(describeDamage(*armed.fixedDamage))));
        dealtDamage = result.taken > 0;
        attacker->economy.actionUsed = true;
        afterDamage(*target, result, source);
    } else if (attack.attackBonus.has_value()) {
        const bool melee = isMeleeAttack(attack);
        RollMode mode = RollMode::Normal;
        const bool automatic = m_rollMode->currentData().toInt() == 0;
        AttackModeChoice choice;
        if (automatic) {
            std::vector<std::string> ticked;
            for (const RollQuestion& question : attackRollQuestions(*selectedEncounter(), *attacker)) {
                if (rollTicked(attacker->id, question.key)) {
                    ticked.push_back(question.key);
                }
            }
            choice = decideAttackMode(*selectedEncounter(), *attacker, *target, attack, ticked);
            // A tick for one attack (Pack Tactics) is used up; sunlight stays.
            for (const RollQuestion& question : attackRollQuestions(*selectedEncounter(), *attacker)) {
                if (!question.sticky) {
                    m_rollTicks.erase(attacker->id + '|' + question.key);
                }
            }
        }
        switch (m_rollMode->currentData().toInt()) {
        case 0:
            mode = choice.mode;
            break;
        case 2:
            mode = RollMode::Advantage;
            break;
        case 3:
            mode = RollMode::Disadvantage;
            break;
        default:
            break;
        }
        const int first = rollD20();
        const int second = rollD20();
        const int face = pickD20(mode, first, second);
        const AttackRoll roll = resolveAttackRoll(*attack.attackBonus, target->ac, d20Penalty(*attacker), face);
        const bool critical = roll.critical || (roll.hit && melee && meleeHitIsCritical(*target));
        // Automatic says why: "advantage: Aria is Prone".
        const auto joined = [](const std::vector<std::string>& list) {
            QStringList out;
            for (const std::string& item : list) {
                out << QString::fromStdString(item);
            }
            return out.join(QStringLiteral(", "));
        };
        QString why;
        if (automatic && mode != RollMode::Normal) {
            why = QStringLiteral(": ") + joined(mode == RollMode::Advantage ? choice.advantages : choice.disadvantages);
        }
        QString rolled = mode == RollMode::Normal ? QString::number(face)
                                                  : tr("%1 (%2 and %3, %4%5)")
                                                        .arg(face)
                                                        .arg(first)
                                                        .arg(second)
                                                        .arg(mode == RollMode::Advantage ? tr("advantage")
                                                                                         : tr("disadvantage"),
                                                             why);
        if (automatic && mode == RollMode::Normal && !choice.advantages.empty()) {
            // Both apply, so they cancel: say so rather than look like a mistake.
            rolled += tr(" (advantage from %1 and disadvantage from %2 cancel)")
                          .arg(joined(choice.advantages), joined(choice.disadvantages));
        }
        if (!roll.hit) {
            addLog(tr("%1 misses %2: %3 rolled, %4 against AC %5.")
                       .arg(source, QString::fromStdString(target->name), rolled)
                       .arg(roll.total)
                       .arg(target->ac));
        } else {
            DamageOptions options = damageOptionsFor(*attacker, target, attack);
            options.critical = critical;
            options.advantage = mode == RollMode::Advantage;
            const std::vector<TypedDamage> damage = rollDamageParts(parts, options, dieRoller());
            addLog(tr("%1 %2 %3: %4 rolled, %5 against AC %6, for %7.")
                       .arg(source, critical ? tr("critically hits") : tr("hits"), QString::fromStdString(target->name),
                            rolled)
                       .arg(roll.total)
                       .arg(target->ac)
                       .arg(QString::fromStdString(describeDamage(damage))));
            const DamageResult result = applyDamage(*target, damage, critical);
            dealtDamage = dealtDamage || result.taken > 0;
            afterDamage(*target, result, source);
            if (attack.drain.has_value()) {
                applyDrainTo(*attacker, *target, *attack.drain, damage, result);
            }
            applyRiders(*attacker, *target, attack, kRiderOnHit, mode, result.taken);
            // "If this damage reduces the target to 0 Hit Points": not a target
            // already at 0, which takes a death save failure instead.
            if (result.droppedToZero && !target->dead && isCharacterCombatant(*target)) {
                applyRiders(*attacker, *target, attack, kRiderOnZeroHp, mode, result.taken);
            }
            // "If the target is a creature, it is subjected to the following effect."
            if (attack.riderSave.has_value() && !target->dead &&
                (!ridersFor(attack, kRiderOnFailure).empty() || !ridersFor(attack, kRiderOnFailureBy5).empty())) {
                Prompt prompt;
                prompt.kind = Prompt::Kind::ActionSave;
                prompt.combatantId = target->id;
                prompt.sourceId = attacker->id;
                prompt.ability = attack.riderSave->ability;
                prompt.dc = attack.riderSave->dc;
                prompt.attack = attack;
                prompt.afterHit = true;
                prompt.advantage = mode == RollMode::Advantage;
                askActionSave(std::move(prompt));
            }
        }
    } else if (attack.save.has_value()) {
        // The save is asked at the top of the page: rolled here, or the
        // table's result entered. "With Advantage if you or your allies are
        // fighting it" is the GM's tick.
        Prompt prompt;
        prompt.kind = Prompt::Kind::ActionSave;
        prompt.combatantId = target->id;
        prompt.sourceId = attacker->id;
        prompt.ability = attack.save->ability;
        prompt.dc = attack.save->dc;
        prompt.attack = attack;
        prompt.damage = armed.saveDamage;
        prompt.advantage =
            !attack.save->advantageIf.empty() && choiceTicked(attacker->id, attack.name, kSaveAdvantageChoice);
        prompt.wasBloodied = targetWasBloodied;
        askActionSave(std::move(prompt));
        keepArmed = attack.area;
    } else {
        const std::vector<TypedDamage> damage =
            rollDamageParts(parts, damageOptionsFor(*attacker, target, attack), dieRoller());
        const DamageResult result = applyDamage(*target, damage);
        dealtDamage = dealtDamage || result.taken > 0;
        afterDamage(*target, result, source);
    }
    if (dealtDamage && targetWasBloodied && isMonsterCombatant(*attacker)) {
        for (const std::string& ready : noteDamagedBloodied(*attacker)) {
            addLog(tr("%1 damaged a creature that was already Bloodied: %2 can be used now.")
                       .arg(QString::fromStdString(attacker->name), QString::fromStdString(ready)));
        }
    }
    // After the roll, so an attack still had Advantage from being unseen.
    std::vector<std::string> events = actionEvents(attack.name, attack.effect, &attack, dealtDamage);
    if (armed.fixedDamage.has_value()) {
        events.emplace_back(kEndsOnAttackRoll);  // the player rolled one
    }
    endByEvents(*attacker, events);

    if (!keepArmed) {
        clearChoices(attacker->id, attack.name);
        disarmAttack();
    }
    const std::string attackerId = attacker->id;
    commit(std::move(before), EditKind::Once, attackerId);
    if (!m_armed.has_value()) {
        maybeAutoPass(attackerId);
    }
}

void CombatPage::askActionSave(Prompt prompt)
{
    Combatant* target = combatantById(prompt.combatantId);
    if (target == nullptr) {
        return;
    }
    // A save it fails automatically (Paralyzed, for Strength and Dexterity)
    // has nothing to ask.
    if (rollSave(*target, prompt.ability, prompt.dc, 20).automaticFailure) {
        resolveActionSave(prompt, 0);
        return;
    }
    m_prompts.push_back(std::move(prompt));
    rebuildPrompts();
}

void CombatPage::resolveActionSave(const Prompt& prompt, int outcome)
{
    Combatant* target = combatantById(prompt.combatantId);
    Combatant* attacker = combatantById(prompt.sourceId);
    if (target == nullptr || attacker == nullptr || !prompt.attack.has_value()) {
        return;
    }
    const MonsterAttack& attack = *prompt.attack;
    const std::optional<SaveSpec>& spec = prompt.afterHit ? attack.riderSave : attack.save;
    if (!spec.has_value()) {
        return;
    }
    // Damage from here on is this action's (a condition that spares it stays).
    struct DamageSource {
        CombatPage* page;
        std::string attackerId;
        std::string action;
        ~DamageSource()
        {
            page->m_damageAttackerId = attackerId;
            page->m_damageAction = action;
        }
    } restore{this, m_damageAttackerId, m_damageAction};
    m_damageAttackerId = attacker->id;
    m_damageAction = attack.name;

    const QString targetName = QString::fromStdString(target->name);
    const QString source = tr("%1's %2").arg(QString::fromStdString(attacker->name),
                                             QString::fromStdString(attack.name));
    bool success = outcome == 1;
    bool byFive = outcome == 4;
    if (outcome == 0) {
        int face = rollD20();
        if (!prompt.afterHit && prompt.advantage) {
            const int second = rollD20();
            addLog(tr("%1 saves with Advantage (%2 and %3).").arg(targetName).arg(face).arg(second));
            face = std::max(face, second);
        }
        const SaveRoll roll = rollSave(*target, spec->ability, spec->dc, face);
        success = roll.success;
        byFive = !success && roll.total <= spec->dc - 5;
        addLog(tr("%1: %2 %3 save %4 (%5) against DC %6: %7.")
                   .arg(source, targetName, QString::fromLatin1(abilityShort(spec->ability)))
                   .arg(roll.total)
                   .arg(roll.face)
                   .arg(spec->dc)
                   .arg(roll.automaticFailure ? tr("fails automatically")
                        : success             ? tr("success")
                                              : tr("failure")));
    } else {
        addLog(tr("%1: %2 %3 the DC %4 %5 save%6.")
                   .arg(source, targetName, success ? tr("succeeds on") : tr("fails"))
                   .arg(spec->dc)
                   .arg(QString::fromLatin1(abilityLabel(spec->ability)), byFive ? tr(" by 5 or more") : QString()));
    }
    const bool fiveRiders = !ridersFor(attack, kRiderOnFailureBy5).empty();
    const std::string failureRiders = byFive && fiveRiders ? kRiderOnFailureBy5 : kRiderOnFailure;

    if (prompt.afterHit) {
        if (!success && !target->dead) {
            applyRiders(*attacker, *target, attack, failureRiders,
                        prompt.advantage ? RollMode::Advantage : RollMode::Normal, 0);
        }
        carryToSheet(*target);
        return;
    }

    std::vector<TypedDamage> damage;
    if (!success) {
        damage = prompt.damage;
    } else if (attack.save->halfOnSuccess) {
        damage = halveDamage(prompt.damage);
    }
    // Death Glare, Slaying Bow, Consume Life: low enough Hit Points and the
    // target drops to 0 or dies instead of taking the damage.
    bool tookLife = false;
    if (!success && attack.failureHpThreshold.has_value() && target->hp <= *attack.failureHpThreshold) {
        if (attack.failureHpEffect == "dies") {
            killOutright(*target);
            tookLife = true;
            addLog(attack.targetAtZeroHp ? tr("%1 dies.").arg(targetName)
                                         : tr("%1 has %2 Hit Points or fewer and dies.")
                                               .arg(targetName)
                                               .arg(*attack.failureHpThreshold));
        } else {
            setHitPoints(*target, 0);
            addLog(isCharacterCombatant(*target)
                       ? tr("%1 has %2 Hit Points or fewer and drops to 0: dying.")
                             .arg(targetName)
                             .arg(*attack.failureHpThreshold)
                       : tr("%1 has %2 Hit Points or fewer and drops to 0.")
                             .arg(targetName)
                             .arg(*attack.failureHpThreshold));
        }
        carryToSheet(*target);
        damage.clear();
    }
    // "The target dies, and the wisp regains 10 (3d6) Hit Points."
    if (!success && !attack.failureSelfHealing.empty() && (tookLife || !attack.failureHpThreshold.has_value())) {
        if (const std::optional<Dice> dice = parseDice(attack.failureSelfHealing)) {
            const HealingResult healed = applyHealing(*attacker, rollDice(*dice, dieRoller(), false));
            addLog(tr("%1 regains %2 Hit Points (%3 HP).")
                       .arg(QString::fromStdString(attacker->name))
                       .arg(healed.healed)
                       .arg(attacker->hp));
            carryToSheet(*attacker);
        }
    }
    // The Incubus's Nightmare: a target with few enough Hit Points falls
    // Unconscious instead of taking the damage.
    if (!success) {
        for (const ConditionRider* rider : ridersFor(attack, kRiderOnFailure)) {
            if (rider->targetMaxHp.has_value() && target->hp <= *rider->targetMaxHp) {
                damage.clear();
            }
        }
    }
    bool dealtDamage = false;
    if (totalDamage(damage) > 0) {
        const DamageResult result = applyDamage(*target, damage);
        dealtDamage = result.taken > 0;
        afterDamage(*target, result, source);
        if (attack.drain.has_value()) {
            applyDrainTo(*attacker, *target, *attack.drain, damage, result);
        }
    }
    if (!success && !target->dead) {
        applyRiders(*attacker, *target, attack, failureRiders, RollMode::Normal, 0);
        if (attack.riders.empty() && totalDamage(damage) == 0 && !attack.failureHpThreshold.has_value() &&
            attack.failureSelfHealing.empty()) {
            addLog(tr("Apply the effect's conditions to %1 by hand.").arg(targetName));
        }
    }
    if (dealtDamage) {
        if (prompt.wasBloodied && isMonsterCombatant(*attacker)) {
            for (const std::string& ready : noteDamagedBloodied(*attacker)) {
                addLog(tr("%1 damaged a creature that was already Bloodied: %2 can be used now.")
                           .arg(QString::fromStdString(attacker->name), QString::fromStdString(ready)));
            }
        }
        endByEvents(*attacker, {kEndsOnDealsDamage});
    }
}

QString CombatPage::conditionList(const std::vector<std::string>& ids) const
{
    QStringList names;
    for (const std::string& id : ids) {
        names << conditionName(id);
    }
    if (names.size() <= 1) {
        return names.join(QString());
    }
    const QString last = names.takeLast();
    return names.join(QStringLiteral(", ")) + tr(" and ") + last;
}

std::string CombatPage::choiceKey(const std::string& attackerId, const std::string& action,
                                  const std::string& condition)
{
    return attackerId + '\n' + action + '\n' + condition;
}

bool CombatPage::rollTicked(const std::string& attackerId, const std::string& questionKey) const
{
    const auto found = m_rollTicks.find(attackerId + '|' + questionKey);
    return found != m_rollTicks.end() && found->second;
}

bool CombatPage::choiceTicked(const std::string& attackerId, const std::string& action,
                              const std::string& condition) const
{
    const auto found = m_damageChoices.find(choiceKey(attackerId, action, condition));
    return found != m_damageChoices.end() && found->second;
}

void CombatPage::clearChoices(const std::string& attackerId, const std::string& action)
{
    const std::string prefix = attackerId + '\n' + action + '\n';
    for (auto it = m_damageChoices.begin(); it != m_damageChoices.end();) {
        it = it->first.rfind(prefix, 0) == 0 ? m_damageChoices.erase(it) : std::next(it);
    }
}

namespace {

// Placeholders for "or" and extra parts whose stat block gives no condition.
const char* const kAnyAlternative = "@or";
const char* const kAnyExtra = "@extra";

bool appSees(const std::string& condition)
{
    return condition == kDamageIfTargetBloodied || condition == kDamageIfSelfBloodied ||
           condition == kDamageIfGrappledBySelf;
}

}  // namespace

DamageOptions CombatPage::damageOptionsFor(const Combatant& attacker, const Combatant* target,
                                           const MonsterAttack& attack) const
{
    DamageOptions options;
    options.conditionsMet = damageConditionsMet(attacker, target);
    for (const DamagePart& part : attackDamageParts(attack)) {
        if (!part.condition.empty() && !appSees(part.condition) &&
            choiceTicked(attacker.id, attack.name, part.condition)) {
            options.conditionsMet.push_back(part.condition);
        }
    }
    options.alternative = choiceTicked(attacker.id, attack.name, kAnyAlternative);
    options.conditional = choiceTicked(attacker.id, attack.name, kAnyExtra);
    return options;
}

void CombatPage::addDamageChoices(QVBoxLayout* layout, const Combatant& attacker, const MonsterAttack& attack)
{
    // A save the target makes with Advantage in a case only the GM knows.
    if (attack.save.has_value() && !attack.save->advantageIf.empty()) {
        const QString text = tr("If %1: the target saves with Advantage")
                                 .arg(QString::fromStdString(attack.save->advantageIf));
        auto* box = new QCheckBox(text);
        box->setObjectName(QStringLiteral("saveAdvantageChoice"));
        box->setChecked(choiceTicked(attacker.id, attack.name, kSaveAdvantageChoice));
        box->setToolTip(tr("Tick before using it. It stays ticked for this action."));
        const std::string id = attacker.id;
        const std::string action = attack.name;
        connect(box, &QCheckBox::toggled, this,
                [this, id, action](bool on) { m_damageChoices[choiceKey(id, action, kSaveAdvantageChoice)] = on; });
        layout->addWidget(wrappingCheckBox(box, text));
    }
    // One tick box per thing only the GM knows: "If the boar moved 20+ feet
    // straight toward it immediately before the hit: extra 1d6 piercing".
    std::vector<std::string> seen;
    for (const DamagePart& part : attackDamageParts(attack)) {
        if ((part.when != DamageWhen::Alternative && part.when != DamageWhen::Conditional) || appSees(part.condition)) {
            continue;
        }
        const bool alternative = part.when == DamageWhen::Alternative;
        const std::string key = part.condition.empty() ? (alternative ? kAnyAlternative : kAnyExtra) : part.condition;
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            continue;
        }
        seen.push_back(key);
        const QString dice = QString::fromStdString(part.dice + (part.type.empty() ? "" : " " + part.type));
        QString text;
        if (part.condition.empty()) {
            text = alternative ? tr("Use the \"or\" damage: %1 instead").arg(dice) : tr("Add the extra %1").arg(dice);
        } else {
            const QString reason = QString::fromStdString(part.condition);
            text = alternative ? tr("If %1: %2 instead").arg(reason, dice) : tr("If %1: extra %2").arg(reason, dice);
        }
        auto* box = new QCheckBox(text);
        box->setObjectName(QStringLiteral("damageChoice"));
        box->setChecked(choiceTicked(attacker.id, attack.name, key));
        box->setToolTip(tr("Tick before using it. The app can't see this; it applies to this use only."));
        const std::string id = attacker.id;
        const std::string action = attack.name;
        connect(box, &QCheckBox::toggled, this,
                [this, id, action, key](bool on) { m_damageChoices[choiceKey(id, action, key)] = on; });
        layout->addWidget(wrappingCheckBox(box, text));
    }
}

void CombatPage::applyRiders(Combatant& attacker, Combatant& target, const MonsterAttack& attack,
                             const std::string& on, RollMode mode, int dealt)
{
    const Character* sheet = characterFor(target);
    const std::string species = sheet != nullptr ? sheet->species : std::string();
    // Riders that wait on the same question are asked together.
    std::vector<std::pair<std::string, std::vector<std::size_t>>> questions;
    for (std::size_t i = 0; i < attack.riders.size(); ++i) {
        const ConditionRider& rider = attack.riders[i];
        if (rider.on != on) {
            continue;
        }
        const std::string blocked = riderBlocked(rider, target, species);
        if (!blocked.empty()) {
            addLog(tr("%1 does not make %2 %3: %4.")
                       .arg(QString::fromStdString(riderSourceName(attacker, attack)),
                            QString::fromStdString(target.name), conditionList(rider.conditions),
                            QString::fromStdString(blocked)));
            continue;
        }
        if (rider.ask == "@advantage") {
            if (mode == RollMode::Advantage) {
                giveRider(attacker, target, attack, rider);
            }
            continue;
        }
        // Ticked on the action (a charge): the GM already answered yes.
        if (!rider.ask.empty() && !(choiceTicked(attacker.id, attack.name, rider.ask) && !rider.refundDamage)) {
            auto found = std::find_if(questions.begin(), questions.end(),
                                      [&rider](const auto& row) { return row.first == rider.ask; });
            if (found == questions.end()) {
                questions.push_back({rider.ask, {i}});
            } else {
                found->second.push_back(i);
            }
            continue;
        }
        giveRider(attacker, target, attack, rider);
    }
    for (const auto& [ask, indexes] : questions) {
        Prompt prompt;
        prompt.kind = Prompt::Kind::Rider;
        prompt.combatantId = target.id;
        prompt.sourceId = attacker.id;
        prompt.attack = attack;
        prompt.riders = indexes;
        for (const std::size_t index : indexes) {
            if (attack.riders[index].refundDamage) {
                prompt.refund = dealt;
            }
        }
        m_prompts.push_back(std::move(prompt));
    }
    if (!questions.empty()) {
        rebuildPrompts();
    }
}

void CombatPage::giveRider(Combatant& attacker, Combatant& target, const MonsterAttack& attack,
                           const ConditionRider& rider)
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    const QString name = QString::fromStdString(target.name);
    const bool concentrating = !target.concentration.empty();
    const RiderOutcome outcome = applyRider(*encounter, attacker, target, attack, rider);
    if (!outcome.removed.empty()) {
        addLog(tr("%1 is no longer %2.").arg(name, conditionList(outcome.removed)));
    }
    if (!outcome.added.empty()) {
        QString detail;
        for (const ActiveCondition& condition : target.conditions) {
            if (condition.id == outcome.added.front() && condition.byId == attacker.id) {
                // "Grappled (Ankheg's Bite, escape DC 13)" with how long it lasts.
                const std::string text = describeCondition(condition, std::string(), *encounter);
                detail = QString::fromStdString(text).trimmed();
            }
        }
        addLog(detail.isEmpty() ? tr("%1 is %2.").arg(name, conditionList(outcome.added))
                                : tr("%1 is %2 %3.").arg(name, conditionList(outcome.added), detail));
    }
    if (!outcome.immune.empty()) {
        addLog(tr("%1 cannot be %2.").arg(name, conditionList(outcome.immune)));
    }
    if (outcome.stabilized) {
        addLog(tr("%1 is stable.").arg(name));
        carryToSheet(target);
    }
    if (concentrating && target.concentration.empty()) {
        addLog(tr("%1 loses concentration.").arg(name));
    }
}

void CombatPage::applyDrainTo(Combatant& attacker, Combatant& target, const HpDrain& drain,
                              const std::vector<TypedDamage>& damage, const DamageResult& result)
{
    const DrainResult drained = applyDrain(target, attacker, drain, damage, result);
    if (drained.reduced <= 0) {
        return;
    }
    const QString name = QString::fromStdString(target.name);
    addLog(drained.died ? tr("%1's Hit Point maximum drops by %2 to 0: %1 dies.").arg(name).arg(drained.reduced)
                        : tr("%1's Hit Point maximum drops by %2 to %3 (until a Long Rest).")
                              .arg(name)
                              .arg(drained.reduced)
                              .arg(target.maxHp.value_or(0)));
    if (drained.healed > 0) {
        addLog(tr("%1 regains %2 Hit Points.").arg(QString::fromStdString(attacker.name)).arg(drained.healed));
    }
    carryToSheet(target);
}

void CombatPage::releaseEndedConditions()
{
    Encounter* encounter = selectedEncounter();
    if (encounter == nullptr) {
        return;
    }
    for (const ReleasedCondition& row : releaseConditions(*encounter)) {
        const QString name = nameOf(row.combatantId);
        addLog(row.condition.source.empty()
                   ? tr("%1 is no longer %2.").arg(name, conditionName(row.condition.id))
                   : tr("%1 is no longer %2 (%3 ended).")
                         .arg(name, conditionName(row.condition.id), QString::fromStdString(row.condition.source)));
    }
}

void CombatPage::endByEvents(Combatant& creature, const std::vector<std::string>& events)
{
    // An action can also switch off the monster's aura (the Sea Hag's
    // Illusory Appearance hides its true form).
    const std::string prefix = kEndsOnActionPrefix;
    for (const std::string& event : events) {
        if (event.rfind(prefix, 0) != 0) {
            continue;
        }
        for (const std::string& off : suppressAuras(creature, event.substr(prefix.size()))) {
            addLog(tr("%1's %2 is off (switch it on again from its Actions tab).")
                       .arg(QString::fromStdString(creature.name), QString::fromStdString(off)));
        }
    }
    const bool concentrating = !creature.concentration.empty();
    const QString name = QString::fromStdString(creature.name);
    for (const ActiveCondition& ended : endConditionsOn(creature, events)) {
        addLog(ended.source.empty()
                   ? tr("%1 is no longer %2.").arg(name, conditionName(ended.id))
                   : tr("%1 is no longer %2 (%3 ended).")
                         .arg(name, conditionName(ended.id), QString::fromStdString(ended.source)));
    }
    if (concentrating && creature.concentration.empty()) {
        addLog(tr("%1's concentration ends.").arg(name));
    }
}

void CombatPage::applyAbilityEffect(Combatant& creature, const std::optional<SelfEffect>& effect)
{
    if (!effect.has_value()) {
        return;
    }
    const QString name = QString::fromStdString(creature.name);
    const QString was = concentrationName(creature.concentration);
    const std::vector<ActiveCondition> before = creature.conditions;
    const AddConditionResult result = applySelfEffect(creature, *effect);
    logConditionsGone(creature, before, effect->condition);
    switch (result) {
    case AddConditionResult::Added:
    case AddConditionResult::Duplicate:
        break;
    case AddConditionResult::Immune:
        addLog(tr("%1 cannot be %2.").arg(name, conditionName(effect->condition)));
        return;
    case AddConditionResult::Empty:
        return;
    }
    if (!was.isEmpty() && !effect->concentration.empty() && was != concentrationName(effect->concentration)) {
        addLog(tr("%1 stops concentrating on %2.").arg(name, was));
    }
    QString line = tr("%1 is %2").arg(name, conditionName(effect->condition));
    if (!effect->source.empty()) {
        line += tr(" (%1)").arg(QString::fromStdString(effect->source));
    }
    if (!effect->concentration.empty()) {
        line += tr(", concentrating on %1").arg(concentrationName(effect->concentration));
    }
    addLog(line + QLatin1Char('.'));
}

void CombatPage::logConditionsGone(const Combatant& creature, const std::vector<ActiveCondition>& before,
                                   const std::string& except)
{
    const QString name = QString::fromStdString(creature.name);
    for (const ActiveCondition& old : before) {
        if (old.id == except || hasCondition(creature, old.id)) {
            continue;
        }
        addLog(old.source.empty() ? tr("%1 is no longer %2.").arg(name, conditionName(old.id))
                                  : tr("%1 is no longer %2 (%3 ended).")
                                        .arg(name, conditionName(old.id), QString::fromStdString(old.source)));
    }
}

QString CombatPage::concentrationName(const std::string& id) const
{
    if (id.empty()) {
        return {};
    }
    const std::optional<Spell> spell = findSpellById(m_spells, id);
    return QString::fromStdString(spell.has_value() ? spell->name : id);
}

void CombatPage::maybeAutoPass(const std::string& attackerId)
{
    if (!m_autoPass || m_armed.has_value()) {
        return;
    }
    Combatant* attacker = combatantById(attackerId);
    if (attacker == nullptr || !isMonsterCombatant(*attacker) || !isTheirTurn(*attacker) ||
        !monsterActionSpent(*attacker)) {
        return;
    }
    // Bonus actions it can still use keep the turn open (Rampage only once
    // its trigger has happened).
    if (attacker->statBlock.has_value()) {
        for (const MonsterFeature& bonus : attacker->statBlock->bonusActions) {
            if (featureAvailability(*attacker, FeatureKind::BonusAction, bonus, true).available) {
                return;
            }
        }
    }
    nextTurn();
}

}  // namespace combat::ui
