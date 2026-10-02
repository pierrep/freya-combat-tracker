#pragma once

#include "core/character.h"

#include <QString>
#include <QWidget>

#include <array>
#include <random>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QWidget;

namespace combat {
class CharacterStore;
}

namespace combat::ui {

// Names on the left, the selected character's fields on the right. Every edit
// is written straight to the store.
class CharactersPage : public QWidget {
    Q_OBJECT

public:
    explicit CharactersPage(CharacterStore& store, QWidget* parent = nullptr);

    int count() const;
    bool hasLoadError() const { return !m_loadError.isEmpty(); }
    QString loadError() const { return m_loadError; }

signals:
    void countChanged(int count);

private:
    void addCharacter();
    void deleteSelected();
    void showSelected();
    void onNameEdited(const QString& text);
    void onNameEditingFinished();
    void onNumberChanged();
    void updateModifierLabels();
    void persist();
    Character* selected();

    QSpinBox* makeNumberBox();

    CharacterStore& m_store;
    std::vector<Character> m_characters;
    QString m_loadError;
    std::mt19937_64 m_rng;
    bool m_populating = false;

    QListWidget* m_list = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QWidget* m_form = nullptr;
    QLabel* m_emptyHint = nullptr;
    QLabel* m_nameError = nullptr;
    QLineEdit* m_name = nullptr;
    QSpinBox* m_hp = nullptr;
    QSpinBox* m_ac = nullptr;
    QSpinBox* m_passivePerception = nullptr;
    std::array<QSpinBox*, 6> m_scores{};
    std::array<QLabel*, 6> m_modifiers{};
};

}  // namespace combat::ui
