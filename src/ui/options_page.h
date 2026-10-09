#pragma once

#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace combat::ui {

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
    void themeChanged(const QString& theme);

private:
    void save();
    void showDataFolder();
    void chooseDataFolder();

    QString m_file;
    QCheckBox* m_groupInitiative = nullptr;
    QCheckBox* m_autoPass = nullptr;
    QCheckBox* m_showDice = nullptr;
    QComboBox* m_theme = nullptr;
    QLabel* m_folderPath = nullptr;
    QLabel* m_folderNote = nullptr;
    QPushButton* m_resetFolder = nullptr;
    QString m_dataFolder;
    bool m_loading = false;
};

}  // namespace combat::ui
