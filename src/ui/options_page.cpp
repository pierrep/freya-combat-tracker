#include "ui/options_page.h"

#include "ui/app_paths.h"
#include "ui/page_title.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QPushButton>
#include <QComboBox>
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
const QString kThemeKey = QStringLiteral("appearance/theme");
// The first version kept only a dark-mode tick.
const QString kOldDarkModeKey = QStringLiteral("appearance/darkMode");

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

    auto* looks = makeCard();
    looks->setMaximumWidth(720);
    auto* looksLayout = static_cast<QVBoxLayout*>(looks->layout());
    looksLayout->setContentsMargins(20, 16, 20, 18);
    looksLayout->setSpacing(14);
    looksLayout->addWidget(makeHeading(tr("Appearance")));
    auto* themeRow = new QHBoxLayout;
    themeRow->setSpacing(10);
    auto* themeLabel = new QLabel(tr("Theme"));
    m_theme = new QComboBox;
    m_theme->setObjectName(QStringLiteral("themeChoice"));
    m_theme->addItem(tr("Match system"), QStringLiteral("system"));
    m_theme->addItem(tr("Light"), QStringLiteral("light"));
    m_theme->addItem(tr("Dark"), QStringLiteral("dark"));
    themeLabel->setBuddy(m_theme);
    themeRow->addWidget(themeLabel);
    themeRow->addWidget(m_theme);
    themeRow->addStretch(1);
    looksLayout->addLayout(themeRow);
    looksLayout->addWidget(makeMuted(tr("Match system follows your computer's light or dark setting, and changes "
                                        "with it. Light or Dark keeps that look whatever the computer uses.")));
    outer->addWidget(looks);

    auto* storage = makeCard();
    storage->setMaximumWidth(720);
    auto* storageLayout = static_cast<QVBoxLayout*>(storage->layout());
    storageLayout->setContentsMargins(20, 16, 20, 18);
    storageLayout->setSpacing(10);
    storageLayout->addWidget(makeHeading(tr("Saved data")));
    storageLayout->addWidget(makeMuted(tr("Characters, custom monsters, encounters and the undo history are saved in "
                                          "this folder.")));
    m_folderPath = new QLabel;
    m_folderPath->setObjectName(QStringLiteral("dataFolderPath"));
    m_folderPath->setWordWrap(true);
    m_folderPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    storageLayout->addWidget(m_folderPath);
    m_folderNote = makeMuted(QString());
    m_folderNote->setObjectName(QStringLiteral("dataFolderNote"));
    storageLayout->addWidget(m_folderNote);
    auto* folderButtons = new QHBoxLayout;
    folderButtons->setSpacing(10);
    auto* change = new QPushButton(tr("Change folder..."));
    change->setObjectName(QStringLiteral("changeDataFolder"));
    m_resetFolder = new QPushButton(tr("Reset to default"));
    m_resetFolder->setObjectName(QStringLiteral("resetDataFolder"));
    folderButtons->addWidget(change);
    folderButtons->addWidget(m_resetFolder);
    folderButtons->addStretch(1);
    storageLayout->addLayout(folderButtons);
    outer->addWidget(storage);
    outer->addStretch(1);
    connect(change, &QPushButton::clicked, this, &OptionsPage::chooseDataFolder);
    connect(m_resetFolder, &QPushButton::clicked, this, [this] { setDataFolder(QString()); });
    showDataFolder();

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
    connect(m_theme, &QComboBox::currentIndexChanged, this, [this] {
        save();
        emit themeChanged(theme());
    });

}

void OptionsPage::setFile(const QString& path)
{
    m_file = path;
    // Read every choice before any of them is written back.
    const QSettings settings(m_file, QSettings::IniFormat);
    const bool group = settings.value(kGroupInitiativeKey, m_groupInitiative->isChecked()).toBool();
    const bool pass = settings.value(kAutoPassKey, m_autoPass->isChecked()).toBool();
    const bool dice = settings.value(kShowDiceKey, m_showDice->isChecked()).toBool();
    QString look = settings.value(kThemeKey).toString();
    if (look.isEmpty()) {
        look = settings.value(kOldDarkModeKey, false).toBool() ? QStringLiteral("dark") : theme();
    }
    m_dataFolder = settings.value(dataFolderOptionKey()).toString();
    showDataFolder();
    m_loading = true;
    m_groupInitiative->setChecked(group);
    m_autoPass->setChecked(pass);
    m_showDice->setChecked(dice);
    if (const int index = m_theme->findData(look); index >= 0) {
        m_theme->setCurrentIndex(index);
    }
    m_loading = false;
}

QString OptionsPage::dataFolder() const
{
    return m_dataFolder;
}

void OptionsPage::showDataFolder()
{
    const QString inUse = QDir::toNativeSeparators(QString::fromStdU16String(appDataFolder().u16string()));
    const QString next = m_dataFolder.isEmpty()
                             ? QDir::toNativeSeparators(QString::fromStdU16String(defaultDataFolder().u16string()))
                             : QDir::toNativeSeparators(m_dataFolder);
    m_folderPath->setText(m_dataFolder.isEmpty() ? tr("%1 (default)").arg(next) : next);
    m_folderNote->setText(QDir::cleanPath(inUse) == QDir::cleanPath(next)
                              ? QString()
                              : tr("Restart the app to start using this folder."));
    m_folderNote->setVisible(!m_folderNote->text().isEmpty());
    m_resetFolder->setEnabled(!m_dataFolder.isEmpty());
}

void OptionsPage::chooseDataFolder()
{
    const QString start = m_dataFolder.isEmpty() ? QString::fromStdU16String(appDataFolder().u16string()) : m_dataFolder;
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose the folder for saved data"), start);
    if (!folder.isEmpty()) {
        setDataFolder(folder);
    }
}

bool OptionsPage::setDataFolder(const QString& folder)
{
    QString target = folder.isEmpty() ? QString() : QDir(folder).absolutePath();
    if (!target.isEmpty()) {
        if (!QDir().mkpath(target)) {
            m_folderNote->setText(tr("That folder cannot be used."));
            m_folderNote->show();
            return false;
        }
        // Bring the data along, but never replace what the folder already has.
        const QString from = QString::fromStdU16String(appDataFolder().u16string());
        if (QDir(from).absolutePath() != target) {
            for (const char* name : {"characters.json", "custom-monsters.json", "encounters.json", "history.json"}) {
                const QString source = QDir(from).filePath(QString::fromLatin1(name));
                const QString copy = QDir(target).filePath(QString::fromLatin1(name));
                if (QFile::exists(source) && !QFile::exists(copy)) {
                    QFile::copy(source, copy);
                }
            }
        }
    }
    m_dataFolder = target;
    save();
    showDataFolder();
    return true;
}

QString OptionsPage::theme() const
{
    return m_theme->currentData().toString();
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
    if (m_file.isEmpty() || m_loading) {
        return;
    }
    QSettings settings(m_file, QSettings::IniFormat);
    settings.setValue(kGroupInitiativeKey, m_groupInitiative->isChecked());
    settings.setValue(kAutoPassKey, m_autoPass->isChecked());
    settings.setValue(kShowDiceKey, m_showDice->isChecked());
    settings.setValue(kThemeKey, theme());
    settings.remove(kOldDarkModeKey);
    if (m_dataFolder.isEmpty()) {
        settings.remove(dataFolderOptionKey());
    } else {
        settings.setValue(dataFolderOptionKey(), m_dataFolder);
    }
}

}  // namespace combat::ui
