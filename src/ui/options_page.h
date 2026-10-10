#pragma once

#include <QString>
#include <QWidget>


#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

namespace combat::ui {

class DiceAudio;
class DiceSampleBank;

// Settings for how the app runs a fight. Kept in an options file when the app
// gives one (setFile); otherwise they last for the session.
class OptionsPage : public QWidget {
    Q_OBJECT

public:
    explicit OptionsPage(QWidget* parent = nullptr);

    // Reads saved choices from this INI file, and writes changes to it.
    void setFile(const QString& path);

    bool groupInitiative() const;
    bool autoPass() const;
    bool showDice() const;
    // The dice's sound: on, how loud (0 to 100), and the table they land on
    // ("wood" or "felt"). Off, and fixed, in a build without sound.
    bool diceSound() const;
    int diceVolume() const;
    QString tableSurface() const;
    // Whether this build can play sound at all.
    static bool soundAvailable();
    // The recordings the dice play, for the test throw and the note about
    // them. Kept by the caller.
    void setSamples(const DiceSampleBank* samples);
    // Plays the test throw (once, or over and over with Loop ticked); again
    // while it loops stops it. The sound it made is kept, for checking.
    void playTestSound();
    void stopTestSound();
    const std::vector<float>& lastTestSound() const { return m_lastTestSound; }
    // "system" (follow the computer's light or dark setting), "light" or "dark".
    QString theme() const;
    // The folder chosen for the saved data; empty when the default is used.
    // Takes effect the next time the app starts.
    QString dataFolder() const;
    // Picks the folder (empty: back to the default). Copies the data files that
    // are not already in it from the folder in use. Returns false when the
    // folder cannot be written to.
    bool setDataFolder(const QString& folder);

signals:
    void groupInitiativeChanged(bool on);
    void autoPassChanged(bool on);
    void showDiceChanged(bool on);
    void diceSoundChanged(bool on, int volume, const QString& surface);
    // Asked to read the recordings folder again (new files dropped in).
    void reloadSamplesRequested();
    void themeChanged(const QString& theme);

protected:
    void hideEvent(QHideEvent* event) override;

private:
    void save();
    void showSamplesNote();
    void showDataFolder();
    void chooseDataFolder();

    QString m_file;
    QCheckBox* m_groupInitiative = nullptr;
    QCheckBox* m_autoPass = nullptr;
    QCheckBox* m_showDice = nullptr;
    QCheckBox* m_diceSound = nullptr;
    QSlider* m_diceVolume = nullptr;
    QLabel* m_volumeValue = nullptr;
    QComboBox* m_tableSurface = nullptr;
    QLabel* m_samplesNote = nullptr;
    QPushButton* m_reloadSamples = nullptr;
    QPushButton* m_testSound = nullptr;
    QCheckBox* m_testLoop = nullptr;
    const DiceSampleBank* m_samples = nullptr;
    std::vector<float> m_lastTestSound;
    DiceAudio* m_testAudio = nullptr;  // only with Qt Multimedia
    bool m_testLooping = false;
    void showSoundChoices();
    QComboBox* m_theme = nullptr;
    QLabel* m_folderPath = nullptr;
    QLabel* m_folderNote = nullptr;
    QPushButton* m_resetFolder = nullptr;
    QString m_dataFolder;
    bool m_loading = false;
};

}  // namespace combat::ui
