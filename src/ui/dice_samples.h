#pragma once

// Recorded dice: short WAV clips of single hits, which the dice play as they
// land (dice_sound.h). Plain C++, no audio device.
//
// The clips live in a folder (app_paths.h: soundsDirectory()), named by what
// they are, in any order, split by "_", "-" or spaces:
//   wood/d20_face_hard_2.wav     a d20 landing flat on wood, struck hard, take 2
//   felt/d6_corner_soft.wav      a d6 landing on a corner on felt
//   wood/any_settle_1.wav        any die tipping onto its face on wood
//   dice/d6_d20_medium_1.wav     a d6 and a d20 knocking together (any table)
// The folder (wood, felt, dice) or a word in the name gives the table; a die
// is d4, d6, d8, d10, d12, d20 or "any"; the hit is corner, edge, face or
// settle (dice clips need none); the force is soft, medium or hard (medium
// when left out); a number is the take. Other words are ignored, so a name can
// keep its source's description.

#include "ui/dice_sound.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace combat::ui {

// One mono clip, its quiet start trimmed and its loudest point at 0.9.
struct AudioClip {
    std::vector<float> samples;
    int rate = 48000;
};

// Reads a WAV file: 8, 16, 24 or 32-bit PCM, or 32 or 64-bit float, any rate
// and any number of channels (mixed to one). Nothing, with the reason in
// error, when it isn't one.
std::optional<AudioClip> readWav(const std::filesystem::path& file, std::string* error = nullptr);
std::optional<AudioClip> parseWav(const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);

enum class DiceHit { Corner, Edge, Face, Settle, Dice };
enum class DiceForce { Soft, Medium, Hard };

struct DiceSampleName {
    bool anySurface = false;  // dice on dice: the same on any table
    TableSurface surface = TableSurface::Wood;
    DiceHit hit = DiceHit::Face;
    int sides = 0;       // 0: any die
    int otherSides = 0;  // Dice: the other die (0: any)
    DiceForce force = DiceForce::Medium;
    int take = 1;
};

// What a clip is, from its path inside the folder. Nothing when the name
// doesn't say which table (or that it's dice on dice).
std::optional<DiceSampleName> parseSampleName(const std::filesystem::path& relative);

// How hard a hit is, from its speed (die radii a second).
DiceForce forceOf(DiceHit hit, float speed);

class DiceSampleBank {
public:
    // Reads every WAV under the folder. Returns how many it kept; the files it
    // couldn't use are in problems().
    int load(const std::filesystem::path& folder);
    void add(const DiceSampleName& name, AudioClip clip);
    void clear();

    int size() const { return static_cast<int>(m_entries.size()); }
    bool empty() const { return m_entries.empty(); }
    const std::vector<std::string>& problems() const { return m_problems; }
    const std::filesystem::path& folder() const { return m_folder; }

    struct Choice {
        const AudioClip* clip = nullptr;  // none of this kind: the hit is silent
        int id = -1;
        bool otherSurface = false;  // the other table's clip, to be dulled or brightened
    };
    // The closest clip to this hit: the same die, hit and force if there is
    // one, else another force, then the nearest kind of hit, then the nearest
    // die, then any die, then the other table. One of several takes at random,
    // never the one just played (avoid) when there's another.
    Choice pick(TableSurface surface, DiceHit hit, int sides, int otherSides, DiceForce force, std::mt19937& random,
                int avoid = -1) const;

private:
    struct Entry {
        DiceSampleName name;
        AudioClip clip;
    };
    std::vector<Entry> m_entries;
    std::vector<std::string> m_problems;
    std::filesystem::path m_folder;
};

}  // namespace combat::ui
