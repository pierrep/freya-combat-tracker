#pragma once

#include <QString>
#include <QWidget>

class QCheckBox;

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

signals:
    void groupInitiativeChanged(bool on);
    void autoPassChanged(bool on);
    void showDiceChanged(bool on);

private:
    void save();

    QString m_file;
    QCheckBox* m_groupInitiative = nullptr;
    QCheckBox* m_autoPass = nullptr;
    QCheckBox* m_showDice = nullptr;
};

}  // namespace combat::ui
