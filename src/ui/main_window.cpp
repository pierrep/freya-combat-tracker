#include "ui/main_window.h"

#include "ui/app_paths.h"
#include "core/campaign.h"
#include "data/json_campaign.h"
#include "data/json_encounters.h"
#include "ui/characters_page.h"
#include "ui/dice_samples.h"
#include "ui/combat_page.h"
#include "ui/encounter_builder_page.h"
#include "ui/monsters_page.h"
#include "ui/options_page.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCloseEvent>
#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QToolButton>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStackedWidget>
#include <QStyleHints>
#include <QGuiApplication>
#include <QTimer>

namespace combat::ui {

namespace {

// A small downward chevron after the page title, drawn at twice the size.
QIcon chevronIcon()
{
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(palette::muted, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolyline(QPolygonF({QPointF(4.0, 6.5), QPointF(8.0, 10.5), QPointF(12.0, 6.5)}));
    painter.end();
    return QIcon(pixmap);
}

}  // namespace

MainWindow::MainWindow(CharacterStore& store, MergedMonsterCatalog& catalog, CustomMonsterStore& customStore,
                       EncounterStore& encounters, const std::vector<Spell>& spells,
                       const std::vector<Condition>& conditions, const std::vector<std::string>& species,
                       const QString& attribution, const QString& catalogError, const QString& sheetCatalogError,
                       QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QApplication::applicationDisplayName());

    // The page title is the page menu: click it to go to another page. It sits
    // at the left of each page's header in place of the page's own title.
    m_navigator = new QToolButton;
    m_navigator->setObjectName(QStringLiteral("pageMenu"));
    m_navigator->setToolTip(tr("Pages"));
    m_navigator->setIcon(chevronIcon());
    m_navigator->setIconSize(QSize(16, 16));
    m_navigator->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // Text first, chevron after it.
    m_navigator->setLayoutDirection(Qt::RightToLeft);
    m_navigator->setPopupMode(QToolButton::InstantPopup);
    m_navigator->setCursor(Qt::PointingHandCursor);
    auto* menu = new QMenu(m_navigator);
    menu->setObjectName(QStringLiteral("pageMenuItems"));
    auto* group = new QActionGroup(menu);
    group->setExclusive(true);
    const QStringList names{tr("Dashboard"), tr("Characters"), tr("Monsters"), tr("Encounter Builder"), tr("Options")};
    for (int i = 0; i < names.size(); ++i) {
        QAction* action = menu->addAction(names[i]);
        action->setObjectName(QStringLiteral("pageAction%1").arg(i));
        action->setCheckable(true);
        group->addAction(action);
        if (i == OptionsIndex - 1) {
            menu->addSeparator();
        }
        // After the menu has closed, since the button moves to the new page.
        connect(action, &QAction::triggered, this,
                [this, i] { QTimer::singleShot(0, this, [this, i] { showPage(static_cast<Page>(i)); }); });
    }
    m_navigator->setMenu(menu);

    m_characters = new CharactersPage(store, spells, species, attribution, sheetCatalogError);
    m_combat = new CombatPage(store, catalog, encounters, spells, conditions);
    m_monsters = new MonstersPage(catalog, customStore, attribution, catalogError);

    m_pages = new QStackedWidget;
    m_pages->setObjectName(QStringLiteral("pages"));
    m_pages->insertWidget(DashboardIndex, m_combat);
    m_pages->insertWidget(CharactersIndex, m_characters);
    m_pages->insertWidget(MonstersIndex, m_monsters);
    m_builder = new EncounterBuilderPage(store, catalog, encounters);
    m_pages->insertWidget(EncounterBuilderIndex, m_builder);
    // Parties and adventures live beside the encounters (a test's temporary
    // folder, or the data folder).
    if (const auto* file = dynamic_cast<const JsonEncounterStore*>(&encounters)) {
        m_campaign = std::make_unique<JsonCampaignStore>(file->path().parent_path() / "campaign.json");
    } else {
        m_campaign = std::make_unique<MemoryCampaignStore>();
    }
    m_characters->setCampaign(m_campaign.get(), &encounters);
    m_builder->setCampaign(m_campaign.get());
    m_combat->setCampaign(m_campaign.get());
    m_options = new OptionsPage;
    m_pages->insertWidget(OptionsIndex, m_options);
    m_combat->setGroupInitiative(m_options->groupInitiative());
    m_combat->setAutoPass(m_options->autoPass());
    connect(m_options, &OptionsPage::groupInitiativeChanged, m_combat, &CombatPage::setGroupInitiative);
    connect(m_options, &OptionsPage::autoPassChanged, m_combat, &CombatPage::setAutoPass);
    m_combat->setShowDice(m_options->showDice());
    connect(m_options, &OptionsPage::showDiceChanged, m_combat, &CombatPage::setShowDice);
    m_combat->setDiceSound(m_options->diceSound(), m_options->diceVolume(), m_options->tableSurface());
    connect(m_options, &OptionsPage::diceSoundChanged, m_combat, &CombatPage::setDiceSound);
    m_diceSamples = std::make_shared<DiceSampleBank>();
    loadDiceSamples();
    connect(m_options, &OptionsPage::reloadSamplesRequested, this, &MainWindow::loadDiceSamples);
    connect(m_options, &OptionsPage::themeChanged, this, &MainWindow::setThemeChoice);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Older Qt can't tell when the desktop changes; it reads it at start-up.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_themeChoice == QStringLiteral("system")) {
            setThemeChoice(m_themeChoice);
        }
    });
#endif
    setThemeChoice(m_options->theme());

    auto* central = new QWidget;
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);

    // Every page saves typing after a short pause. Write those edits before
    // another page reads the files.
    showPage(DashboardIndex);
}

void MainWindow::showPage(Page page)
{
    if (m_pages->currentIndex() != page) {
        flushPendingSaves();
        m_pages->setCurrentIndex(page);
    }
    if (QAction* action = m_navigator->menu()->findChild<QAction*>(QStringLiteral("pageAction%1").arg(page))) {
        action->setChecked(true);
        m_navigator->setText(action->text());
    }
    placeNavigator();
}

MainWindow::Page MainWindow::currentPage() const
{
    return static_cast<Page>(m_pages->currentIndex());
}

void MainWindow::placeNavigator()
{
    QWidget* page = m_pages->currentWidget();
    auto* header = page == nullptr ? nullptr : page->findChild<QHBoxLayout*>(QStringLiteral("pageHeader"));
    if (header == nullptr || header->indexOf(m_navigator) == 0) {
        return;
    }
    header->insertWidget(0, m_navigator);
    m_navigator->show();
    // The menu button shows the title, so the page's own title label hides.
    for (int i = 0; i < header->count(); ++i) {
        if (auto* label = qobject_cast<QLabel*>(header->itemAt(i)->widget());
            label != nullptr && label->property("role").toString() == QStringLiteral("title")) {
            label->hide();
        }
    }
}

void MainWindow::setDarkTheme(bool on)
{
    if (on == darkMode()) {
        return;
    }
    setDarkMode(on);
    applyTheme(*qApp);
    m_navigator->setIcon(chevronIcon());
    m_combat->refreshTheme();
    for (QWidget* widget : QApplication::allWidgets()) {
        widget->update();
    }
}

void MainWindow::setThemeChoice(const QString& choice)
{
    m_themeChoice = choice;
    const bool dark =
        choice == QStringLiteral("dark") || (choice == QStringLiteral("system") && systemPrefersDark());
    setDarkTheme(dark);
}

void MainWindow::loadDiceSamples()
{
    m_diceSamples->load(soundsDirectory());
    m_options->setSamples(m_diceSamples.get());
    m_combat->setDiceSamples(m_diceSamples.get());
}

void MainWindow::setOptionsFile(const QString& path)
{
    m_options->setFile(path);
    // The same file remembers the encounter last used, to open it next time.
    m_combat->setStateFile(path);
    m_builder->setStateFile(path);
}

void MainWindow::setHistoryFile(const QString& path)
{
    m_combat->setHistoryFile(path);
}

void MainWindow::flushPendingSaves()
{
    m_characters->flushPendingSave();
    m_combat->flushPendingSave();
    m_monsters->flushPendingSave();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    flushPendingSaves();
    m_combat->saveHistory(true);
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (m_reportedLoadError || !m_characters->hasLoadError()) {
        return;
    }
    m_reportedLoadError = true;
    QTimer::singleShot(0, this, [this] {
        QMessageBox::critical(this, tr("Could not read characters"),
                              tr("%1\n\nThe file has been left unchanged. Characters cannot be edited until it "
                                 "is fixed or moved aside.")
                                  .arg(m_characters->loadError()));
    });
}

}  // namespace combat::ui
