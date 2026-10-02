#include "ui/main_window.h"

#include "ui/characters_page.h"
#include "ui/dashboard_page.h"
#include "ui/placeholder_page.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QStackedWidget>
#include <QTimer>

namespace combat::ui {

MainWindow::MainWindow(CharacterStore& store, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QApplication::applicationDisplayName());

    m_sidebar = new QListWidget;
    m_sidebar->setObjectName(QStringLiteral("sidebar"));
    m_sidebar->addItems({tr("Dashboard"), tr("Characters"), tr("Monsters"), tr("Combat")});
    m_sidebar->setFixedWidth(180);
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setStyleSheet(QStringLiteral("QListWidget#sidebar::item { padding: 8px 12px; }"));
    QFont sidebarFont = m_sidebar->font();
    sidebarFont.setPointSizeF(sidebarFont.pointSizeF() * 1.15);
    m_sidebar->setFont(sidebarFont);

    m_dashboard = new DashboardPage;
    m_characters = new CharactersPage(store);

    m_pages = new QStackedWidget;
    m_pages->insertWidget(DashboardIndex, m_dashboard);
    m_pages->insertWidget(CharactersIndex, m_characters);
    m_pages->insertWidget(MonstersIndex, new PlaceholderPage(tr("Monsters")));
    m_pages->insertWidget(CombatIndex,
                          new PlaceholderPage(tr("Combat"),
                                              tr("Later this page will track initiative and whose turn it is.")));

    auto* central = new QWidget;
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_sidebar);
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);

    connect(m_sidebar, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_dashboard, &DashboardPage::openPageRequested, this, &MainWindow::showPage);
    connect(m_characters, &CharactersPage::countChanged, m_dashboard, &DashboardPage::setCharacterCount);

    if (m_characters->hasLoadError()) {
        m_dashboard->setLoadError();
    } else {
        m_dashboard->setCharacterCount(m_characters->count());
    }

    m_sidebar->setCurrentRow(DashboardIndex);
}

void MainWindow::showPage(int index)
{
    m_sidebar->setCurrentRow(index);
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
