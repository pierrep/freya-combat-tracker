#pragma once

#include "core/monster.h"
#include "core/monster_catalog.h"

#include <QString>
#include <QWidget>

#include <array>
#include <random>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QWidget;

namespace combat {
class CustomMonsterStore;
}

namespace combat::ui {

// Search and results on the left, the stat block on the right. SRD rows are
// read-only. Custom rows can be edited and deleted.
class MonstersPage : public QWidget {
    Q_OBJECT

public:
    MonstersPage(MergedMonsterCatalog& catalog, CustomMonsterStore& customStore, const QString& attribution,
                 const QString& catalogError, QWidget* parent = nullptr);

private:
    void addMonster();
    void deleteSelected();
    void showSelected();
    void onSearchChanged();
    void onFilterChanged();
    void onNameEdited(const QString& text);
    void onNameEditingFinished();
    void onFormEdited();
    void onTextFinished();
    void refreshResults();
    void rebuildFilters();
    void persist();
    void selectId(const QString& id);
    QString currentId() const;
    Monster* selectedCustom();
    const Monster* selectedVisible() const;
    QSpinBox* makeNumberBox();

    MergedMonsterCatalog& m_catalog;
    CustomMonsterStore& m_store;
    std::vector<Monster> m_custom;
    std::vector<Monster> m_visible;
    QString m_customError;
    std::mt19937_64 m_rng;
    bool m_populating = false;

    QLineEdit* m_search = nullptr;
    QComboBox* m_typeFilter = nullptr;
    QComboBox* m_crFilter = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QLabel* m_emptyHint = nullptr;

    QWidget* m_stat = nullptr;
    QLabel* m_statName = nullptr;
    QLabel* m_statType = nullptr;
    QLabel* m_statAc = nullptr;
    QLabel* m_statHp = nullptr;
    QLabel* m_statSpeed = nullptr;
    QLabel* m_statInitiative = nullptr;
    QLabel* m_statPerception = nullptr;
    QLabel* m_statChallenge = nullptr;
    std::array<QLabel*, 6> m_statScores{};

    QWidget* m_form = nullptr;
    QLabel* m_nameError = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_size = nullptr;
    QLineEdit* m_creatureType = nullptr;
    QSpinBox* m_hp = nullptr;
    QSpinBox* m_ac = nullptr;
    QLineEdit* m_hitDice = nullptr;
    QLineEdit* m_speed = nullptr;
    QSpinBox* m_initiative = nullptr;
    QSpinBox* m_passivePerception = nullptr;
    QLineEdit* m_challengeRating = nullptr;
    std::array<QSpinBox*, 6> m_scores{};
    std::array<QLabel*, 6> m_modifiers{};
};

}  // namespace combat::ui
