#pragma once

#include "core/character.h"
#include "core/sheet.h"

#include <QString>
#include <QWidget>

class QHideEvent;
class QShowEvent;
class QTimer;

#include <random>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class QWidget;

namespace combat {
class CharacterStore;
}

namespace combat::ui {

// Names on the left, the selected character's sheet on the right. Every edit
// is written straight to the store. Spell and condition text comes from the
// catalogs passed in; widgets do not hard-code rules text.
class CharactersPage : public QWidget {
    Q_OBJECT

public:
    CharactersPage(CharacterStore& store, std::vector<Spell> spells, std::vector<std::string> species,
                   const QString& attribution, const QString& catalogError, QWidget* parent = nullptr);

    // Writes an edit that is still waiting for typing to pause.
    void flushPendingSave();

    int count() const;
    bool hasLoadError() const { return !m_loadError.isEmpty(); }
    QString loadError() const { return m_loadError; }

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

signals:
    void countChanged(int count);

private:
    void addCharacter();
    void importPdf();
    void deleteSelected();
    void showSelected();
    void onNameEdited(const QString& text);
    void onNameEditingFinished();
    void onSpeciesEdited(const QString& text);
    void onNumberChanged();
    void onSpeedEdited(const QString& text);
    void onNotesChanged();
    void updateModifierLabels();
    void addClass();
    void onClassNameEdited(const QString& text);
    void onClassNameFinished();
    void onClassLevelChanged(int value);
    void onSubclassEdited(const QString& text);
    void onClassRemove();
    void rebuildClasses();
    void onSpellSearch(const QString& text);
    void addCatalogSpell();
    void addCustomSpell();
    void onSpellSelected();
    void onPreparedToggled(bool prepared);
    void removeSelectedSpell();
    void refreshSpellList(int selectRow);
    void addSlot();
    void onSlotLevelChanged(int value);
    void onSlotNumberChanged();
    void onSlotRemove();
    void rebuildSlots();
    void addGear();
    void onGearNameEdited(const QString& text);
    void onGearNameFinished();
    void onGearQuantityChanged(int value);
    void onGearEquippedToggled(bool equipped);
    void onGearRemove();
    void rebuildGear();
    void onSlotShortRestToggled(bool shortRest);
    void onDefensesEdited();
    void longRestSelected();
    void shortRestSelected();
    void updateDerivedLabels();
    void persist();
    void saveNow();
    void reloadRoster();
    Character* selected();
    const Character* selected() const;

    QSpinBox* makeNumberBox(int minimum, int maximum);

    CharacterStore& m_store;
    std::vector<Character> m_characters;
    std::vector<Spell> m_spells;
    std::vector<std::string> m_speciesNames;
    QString m_loadError;
    QString m_catalogError;
    std::mt19937_64 m_rng;
    bool m_populating = false;
    bool m_savePending = false;
    QTimer* m_saveTimer = nullptr;

    QListWidget* m_list = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_importButton = nullptr;
    QWidget* m_form = nullptr;
    QLabel* m_emptyHint = nullptr;
    QLabel* m_nameError = nullptr;
    QLabel* m_speciesHint = nullptr;
    QLineEdit* m_name = nullptr;
    QComboBox* m_species = nullptr;
    QSpinBox* m_hpCurrent = nullptr;
    QSpinBox* m_hpMax = nullptr;
    QSpinBox* m_tempHp = nullptr;
    QSpinBox* m_ac = nullptr;
    QLineEdit* m_speed = nullptr;
    QCheckBox* m_initiativeOverride = nullptr;
    QSpinBox* m_initiative = nullptr;
    QLabel* m_initiativeDerived = nullptr;
    QCheckBox* m_proficiencyOverride = nullptr;
    QSpinBox* m_proficiency = nullptr;
    QLabel* m_proficiencyDerived = nullptr;
    QSpinBox* m_exhaustion = nullptr;
    QLabel* m_exhaustionNote = nullptr;
    QLineEdit* m_resistances = nullptr;
    QLineEdit* m_immunities = nullptr;
    QLineEdit* m_vulnerabilities = nullptr;
    QLabel* m_defenseHint = nullptr;
    QPushButton* m_shortRestButton = nullptr;
    QPushButton* m_longRestButton = nullptr;
    QSpinBox* m_passivePerception = nullptr;
    std::array<QSpinBox*, 6> m_scores{};
    std::array<QLabel*, 6> m_modifiers{};
    std::array<QCheckBox*, 6> m_saves{};
    std::array<QLabel*, 6> m_saveBonuses{};
    std::array<QCheckBox*, 18> m_skillBoxes{};
    QVBoxLayout* m_classLayout = nullptr;
    QLineEdit* m_spellSearch = nullptr;
    QListWidget* m_spellMatches = nullptr;
    QLineEdit* m_customSpell = nullptr;
    QListWidget* m_spellList = nullptr;
    QCheckBox* m_prepared = nullptr;
    QLabel* m_spellDescription = nullptr;
    QVBoxLayout* m_slotLayout = nullptr;
    QVBoxLayout* m_gearLayout = nullptr;
    QPlainTextEdit* m_notes = nullptr;
};

}  // namespace combat::ui
