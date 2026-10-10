#include "ui/options_page.h"

#include "ui/app_paths.h"
#include "ui/dice_samples.h"
#include "ui/dice_sound.h"
#ifdef FREYA_HAVE_AUDIO
#include "ui/dice_audio.h"
#endif
#include "ui/page_title.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QPushButton>
#include <QComboBox>
#include <QFrame>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <QHideEvent>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace combat::ui {

namespace {

const QString kGroupInitiativeKey = QStringLiteral("combat/groupInitiative");
const QString kAutoPassKey = QStringLiteral("combat/autoPass");
const QString kShowDiceKey = QStringLiteral("combat/showDice");
const QString kDiceSoundKey = QStringLiteral("combat/diceSound");
const QString kDiceVolumeKey = QStringLiteral("combat/diceVolume");
const QString kTableSurfaceKey = QStringLiteral("combat/tableSurface");
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
    // Everything scrolls when the window is shorter than the page.
    auto* frame = new QVBoxLayout(this);
    frame->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("optionsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget;
    scroll->setWidget(content);
    frame->addWidget(scroll);
    auto* outer = new QVBoxLayout(content);
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

    m_diceSound = new QCheckBox(tr("Dice make a sound as they land"));
    m_diceSound->setObjectName(QStringLiteral("diceSound"));
    m_diceSound->setChecked(soundAvailable());
    layout->addWidget(option(m_diceSound,
                             soundAvailable()
                                 ? tr("Each die clatters as it hits the table or another die, played from recordings "
                                      "of real dice: the closest one to each hit, louder the harder it lands.")
                                 : tr("This copy of the app was built without Qt Multimedia, so the dice are silent.")));
    auto* soundRow = new QHBoxLayout;
    soundRow->setContentsMargins(24, 0, 0, 0);
    soundRow->setSpacing(10);
    auto* volumeLabel = new QLabel(tr("Volume"));
    m_diceVolume = new QSlider(Qt::Horizontal);
    m_diceVolume->setObjectName(QStringLiteral("diceVolume"));
    m_diceVolume->setRange(0, 100);
    m_diceVolume->setValue(60);
    m_diceVolume->setFixedWidth(160);
    volumeLabel->setBuddy(m_diceVolume);
    m_volumeValue = new QLabel(QStringLiteral("60%"));
    m_volumeValue->setMinimumWidth(36);
    auto* surfaceLabel = new QLabel(tr("Table"));
    m_tableSurface = new QComboBox;
    m_tableSurface->setObjectName(QStringLiteral("tableSurface"));
    m_tableSurface->addItem(tr("Wooden table"), QStringLiteral("wood"));
    m_tableSurface->addItem(tr("Felt dice tray"), QStringLiteral("felt"));
    surfaceLabel->setBuddy(m_tableSurface);
    soundRow->addWidget(volumeLabel);
    soundRow->addWidget(m_diceVolume);
    soundRow->addWidget(m_volumeValue);
    soundRow->addSpacing(14);
    soundRow->addWidget(surfaceLabel);
    soundRow->addWidget(m_tableSurface);
    soundRow->addStretch(1);
    layout->addLayout(soundRow);

    // The recordings: how many, and a way to read new ones.
    auto* sourceRow = new QHBoxLayout;
    sourceRow->setContentsMargins(24, 0, 0, 0);
    sourceRow->setSpacing(10);
    m_reloadSamples = new QPushButton(tr("Reload recordings"));
    m_reloadSamples->setObjectName(QStringLiteral("reloadSamples"));
    sourceRow->addWidget(m_reloadSamples);
    sourceRow->addStretch(1);
    layout->addLayout(sourceRow);
    m_samplesNote = makeMuted(QString());
    m_samplesNote->setObjectName(QStringLiteral("diceSamplesNote"));
    m_samplesNote->setWordWrap(true);
    m_samplesNote->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_samplesNote->setContentsMargins(24, 0, 0, 0);
    layout->addWidget(m_samplesNote);

    auto* testRow = new QHBoxLayout;
    testRow->setContentsMargins(24, 0, 0, 0);
    testRow->setSpacing(10);
    m_testSound = new QPushButton(tr("Play test throw"));
    m_testSound->setObjectName(QStringLiteral("playTestSound"));
    m_testLoop = new QCheckBox(tr("Loop"));
    m_testLoop->setObjectName(QStringLiteral("testSoundLoop"));
    testRow->addWidget(m_testSound);
    testRow->addWidget(m_testLoop);
    testRow->addStretch(1);
    layout->addLayout(testRow);
    showSamplesNote();
    showSoundChoices();

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
        showSoundChoices();
        emit showDiceChanged(on);
    });
    const auto soundChanged = [this] {
        m_volumeValue->setText(tr("%1%").arg(m_diceVolume->value()));
        showSoundChoices();
        save();
        emit diceSoundChanged(diceSound(), diceVolume(), tableSurface());
    };
    connect(m_diceSound, &QCheckBox::toggled, this, soundChanged);
    connect(m_diceVolume, &QSlider::valueChanged, this, soundChanged);
    connect(m_tableSurface, &QComboBox::currentIndexChanged, this, soundChanged);
    connect(m_reloadSamples, &QPushButton::clicked, this, [this] { emit reloadSamplesRequested(); });
    connect(m_testSound, &QPushButton::clicked, this, [this] {
        if (m_testLooping) {
            stopTestSound();
        } else {
            playTestSound();
        }
    });
    connect(m_testLoop, &QCheckBox::toggled, this, [this](bool loop) {
        if (!loop && m_testLooping) {
            stopTestSound();
        }
    });
    // A looping test plays the new choice at once (the volume once its slider
    // is let go: restarting while it's dragged would only stutter).
    const auto restartTest = [this] {
        if (m_testLooping) {
            playTestSound();
        }
    };
    connect(m_diceVolume, &QSlider::valueChanged, this, [this, restartTest] {
        if (!m_diceVolume->isSliderDown()) {
            restartTest();
        }
    });
    connect(m_diceVolume, &QSlider::sliderReleased, this, restartTest);
    connect(m_tableSurface, &QComboBox::currentIndexChanged, this, restartTest);

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
    const bool sound = settings.value(kDiceSoundKey, m_diceSound->isChecked()).toBool();
    const int volume = std::clamp(settings.value(kDiceVolumeKey, m_diceVolume->value()).toInt(), 0, 100);
    const QString surface = settings.value(kTableSurfaceKey, tableSurface()).toString();
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
    m_diceSound->setChecked(sound && soundAvailable());
    m_diceVolume->setValue(volume);
    m_volumeValue->setText(tr("%1%").arg(volume));
    if (const int index = m_tableSurface->findData(surface); index >= 0) {
        m_tableSurface->setCurrentIndex(index);
    }
    showSamplesNote();
    showSoundChoices();
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
            for (const char* name :
                 {"characters.json", "custom-monsters.json", "encounters.json", "campaign.json", "history.json"}) {
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

bool OptionsPage::soundAvailable()
{
#ifdef FREYA_HAVE_AUDIO
    return true;
#else
    return false;
#endif
}

void OptionsPage::showSoundChoices()
{
    // The sound comes with the dice: no dice on the page, no clatter.
    m_diceSound->setEnabled(soundAvailable() && m_showDice->isChecked());
    const bool on = m_diceSound->isEnabled() && m_diceSound->isChecked();
    m_diceVolume->setEnabled(on);
    m_tableSurface->setEnabled(on);
    m_reloadSamples->setEnabled(on);
    m_testSound->setEnabled(on);
    m_testLoop->setEnabled(on);
    if (!on) {
        stopTestSound();
    }
}

void OptionsPage::setSamples(const DiceSampleBank* samples)
{
    m_samples = samples;
    showSamplesNote();
    if (m_testLooping) {
        playTestSound();
    }
}

void OptionsPage::showSamplesNote()
{
    const int count = m_samples != nullptr ? m_samples->size() : 0;
    const QString folder =
        m_samples != nullptr ? QDir::toNativeSeparators(QString::fromStdU16String(m_samples->folder().u16string()))
                             : QString();
    QString text;
    if (count == 0) {
        text = folder.isEmpty()
                   ? tr("No recordings yet, so the dice are silent.")
                   : tr("No recordings found yet, so the dice are silent. Put WAV clips in %1 or the app's "
                        "data/sounds (its README.md says how to name them), then Reload recordings.")
                         .arg(folder);
    } else {
        text = tr("%n recording(s) from %1. A kind of hit with none recorded is silent.", nullptr, count).arg(folder);
    }
    if (m_samples != nullptr && !m_samples->problems().empty()) {
        const auto& problems = m_samples->problems();
        text += QLatin1Char('\n') + tr("Skipped %n file(s): ", nullptr, static_cast<int>(problems.size())) +
                QString::fromStdString(problems.front()) + (problems.size() > 1 ? QStringLiteral(", ...") : QString());
    }
    m_samplesNote->setText(text);
}

void OptionsPage::playTestSound()
{
    const DiceTestThrow test = diceTestThrow();
    DiceSoundSettings settings;
    settings.surface = tableSurface() == QStringLiteral("felt") ? TableSurface::Felt : TableSurface::Wood;
    settings.volume = static_cast<float>(diceVolume()) / 100.0f;
#ifdef FREYA_HAVE_AUDIO
    if (m_testAudio == nullptr) {
        m_testAudio = new DiceAudio(this);
    }
    const bool output = m_testAudio->prepare();
    if (output) {
        settings.sampleRate = m_testAudio->sampleRate();
    }
#endif
    m_lastTestSound = renderDiceSound(test.impacts, test.voices, settings, QRandomGenerator::global()->generate(),
                                      m_samples);
    const bool loop = m_testLoop->isChecked();
#ifdef FREYA_HAVE_AUDIO
    if (output) {
        m_testAudio->play(m_lastTestSound, loop);
    }
#endif
    m_testLooping = loop;
    m_testSound->setText(loop ? tr("Stop") : tr("Play test throw"));
}

void OptionsPage::stopTestSound()
{
#ifdef FREYA_HAVE_AUDIO
    if (m_testAudio != nullptr) {
        m_testAudio->stop();
    }
#endif
    m_testLooping = false;
    if (m_testSound != nullptr) {
        m_testSound->setText(tr("Play test throw"));
    }
}

void OptionsPage::hideEvent(QHideEvent* event)
{
    stopTestSound();
    QWidget::hideEvent(event);
}

bool OptionsPage::diceSound() const
{
    return soundAvailable() && m_diceSound->isChecked();
}

int OptionsPage::diceVolume() const
{
    return m_diceVolume->value();
}

QString OptionsPage::tableSurface() const
{
    return m_tableSurface->currentData().toString();
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
    if (soundAvailable()) {
        settings.setValue(kDiceSoundKey, m_diceSound->isChecked());
    }
    settings.setValue(kDiceVolumeKey, m_diceVolume->value());
    settings.setValue(kTableSurfaceKey, tableSurface());
    settings.setValue(kThemeKey, theme());
    settings.remove(kOldDarkModeKey);
    if (m_dataFolder.isEmpty()) {
        settings.remove(dataFolderOptionKey());
    } else {
        settings.setValue(dataFolderOptionKey(), m_dataFolder);
    }
}

}  // namespace combat::ui
