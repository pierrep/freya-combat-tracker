#include "ui/dashboard_page.h"

#include "ui/main_window.h"
#include "ui/page_title.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace combat::ui {

DashboardPage::DashboardPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(16);
    layout->addWidget(makePageTitle(tr("Dashboard")));

    m_count = new QLabel;
    m_count->setObjectName(QStringLiteral("characterCount"));
    QFont countFont = m_count->font();
    countFont.setPointSizeF(countFont.pointSizeF() * 1.2);
    m_count->setFont(countFont);
    layout->addWidget(m_count);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(16);
    const struct {
        QString label;
        int page;
    } destinations[] = {
        {tr("Characters"), MainWindow::CharactersIndex},
        {tr("Monsters"), MainWindow::MonstersIndex},
        {tr("Combat"), MainWindow::CombatIndex},
    };
    for (const auto& destination : destinations) {
        auto* button = new QPushButton(destination.label);
        button->setMinimumSize(180, 96);
        QFont font = button->font();
        font.setPointSizeF(font.pointSizeF() * 1.3);
        button->setFont(font);
        const int page = destination.page;
        connect(button, &QPushButton::clicked, this, [this, page] { emit openPageRequested(page); });
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);
    layout->addStretch(1);
}

void DashboardPage::setCharacterCount(int count)
{
    m_count->setText(count == 1 ? tr("1 character saved") : tr("%1 characters saved").arg(count));
}

void DashboardPage::setLoadError()
{
    m_count->setText(tr("The characters file could not be read."));
}

}  // namespace combat::ui
