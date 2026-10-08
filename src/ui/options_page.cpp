#include "ui/options_page.h"

#include "ui/page_title.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace combat::ui {

namespace {

const QString kGroupInitiativeKey = QStringLiteral("combat/groupInitiative");
const QString kAutoPassKey = QStringLiteral("combat/autoPass");
const QString kShowDiceKey = QStringLiteral("combat/showDice");

// A choice with a line under it saying what it does.
QWidget* option(QCheckBox* box, const QString& explanation)
{
    auto* host = new QWidget;
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(box);
    auto* note = makeMuted(explanation);
    note->setContentsMargins(24, 0, 0, 0);
    layout->addWidget(note);
    return host;
}

}  // namespace

OptionsPage::OptionsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 20, 28, 20);
    outer->setSpacing(14);

    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    header->setObjectName(QStringLiteral("pageHeader"));
    header->addWidget(makePageTitle(tr("Options")));
    header->addStretch(1);
    outer->addLayout(header);

    auto* card = makeCard();
    card->setMaximumWidth(720);
    auto* layout = static_cast<QVBoxLayout*>(card->layout());
    layout->setContentsMargins(20, 16, 20, 18);
    layout->setSpacing(14);
    layout->addWidget(makeHeading(tr("Combat")));

    m_groupInitiative = new QCheckBox(tr("One initiative roll per monster type"));
    m_groupInitiative->setObjectName(QStringLiteral("groupInitiative"));
    layout->addWidget(option(m_groupInitiative,
                             tr("Roll monster initiative gives every monster of the same kind the same roll, so "
                                "they act together.")));

    m_autoPass = new QCheckBox(tr("Pass a monster's turn when its action is spent"));
    m_autoPass->setObjectName(QStringLiteral("autoPass"));
    m_autoPass->setChecked(true);
    layout->addWidget(option(m_autoPass,
                             tr("Once a monster has used its action and has no attacks left, the turn goes to the "
                                "next creature. Turn this off to end each turn yourself.")));

    m_showDice = new QCheckBox(tr("Throw dice across the Dashboard"));
    m_showDice->setObjectName(QStringLiteral("showDice"));
    m_showDice->setChecked(true);
    layout->addWidget(option(m_showDice,
                             tr("The dice the app rolls (attacks, damage, saves, initiative) and the Dice tray's tumble "
                                "across the Dashboard and show what they came up. The log has every roll either way.")));

    outer->addWidget(card);
    outer->addStretch(1);

    connect(m_groupInitiative, &QCheckBox::toggled, this, [this](bool on) {
        save();
        emit groupInitiativeChanged(on);
    });
    connect(m_autoPass, &QCheckBox::toggled, this, [this](bool on) {
        save();
        emit autoPassChanged(on);
    });
    connect(m_showDice, &QCheckBox::toggled, this, [this](bool on) {
        save();
        emit showDiceChanged(on);
    });

}

void OptionsPage::setFile(const QString& path)
{
    m_file = path;
    const QSettings settings(m_file, QSettings::IniFormat);
    m_groupInitiative->setChecked(settings.value(kGroupInitiativeKey, m_groupInitiative->isChecked()).toBool());
    m_autoPass->setChecked(settings.value(kAutoPassKey, m_autoPass->isChecked()).toBool());
    m_showDice->setChecked(settings.value(kShowDiceKey, m_showDice->isChecked()).toBool());
}

bool OptionsPage::showDice() const
{
    return m_showDice->isChecked();
}

bool OptionsPage::groupInitiative() const
{
    return m_groupInitiative->isChecked();
}

bool OptionsPage::autoPass() const
{
    return m_autoPass->isChecked();
}

void OptionsPage::save()
{
    if (m_file.isEmpty()) {
        return;
    }
    QSettings settings(m_file, QSettings::IniFormat);
    settings.setValue(kGroupInitiativeKey, m_groupInitiative->isChecked());
    settings.setValue(kAutoPassKey, m_autoPass->isChecked());
    settings.setValue(kShowDiceKey, m_showDice->isChecked());
}

}  // namespace combat::ui
