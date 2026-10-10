#pragma once

// The sound of a dice throw, made from recordings (dice_samples.h): every time
// a die hits the table, another die, or tips onto its resting face, the
// closest recording of that hit plays. Plain C++ (no audio device): the
// overlay records the hits while it runs the throw ahead, this mixes the
// recordings at those moments, and the player (dice_audio.h, when the build
// has Qt Multimedia) plays them in step with the dice.

#include <cstdint>
#include <vector>

namespace combat::ui {

enum class TableSurface {
    Wood,  // a wooden table
    Felt,  // a felt dice tray
};

// One contact during a throw.
struct DiceImpact {
    enum class Kind {
        Table,   // a die lands or bounces on the table
        Dice,    // two dice knock together
        Settle,  // a die tips onto the face it shows
    };
    Kind kind = Kind::Table;
    double time = 0.0;  // seconds from the start of the throw
    int die = 0;        // which die (index into the voices)
    int other = -1;     // Dice: the other die
    // How hard: the speed into the table (or the closing speed of two dice),
    // in die radii per second, so the sound doesn't change with the window's
    // size.
    float speed = 0.0f;
    // How much of the die meets the table: 1 a corner, 2 an edge, 3 or more a
    // face. It picks the recording (corner, edge or face).
    int contacts = 1;
    float pan = 0.0f;  // -1 at the left of the page to 1 at the right
};

// One die in the throw.
struct DieVoice {
    int sides = 20;
    float detune = 1.0f;  // a little higher or lower, so two dice of a kind don't sound the same
};

struct DiceSoundSettings {
    TableSurface surface = TableSurface::Wood;
    float volume = 0.6f;  // 0 to 1
    int sampleRate = 48000;
};

// Contacts softer than this (die radii per second) make no sound: a die at
// rest touches the table every step.
inline constexpr float kQuietestImpact = 1.6f;

class DiceSampleBank;

// The throw's sound: stereo, interleaved left and right, from the start of the
// throw until the last recording has played out. Each hit plays the closest
// recording; a hit with none of its kind is silent, and so is every hit with
// no recordings at all. Never outside -1 to 1. The same seed gives the same
// sound.
std::vector<float> renderDiceSound(const std::vector<DiceImpact>& impacts, const std::vector<DieVoice>& voices,
                                   const DiceSoundSettings& settings, std::uint32_t seed,
                                   const DiceSampleBank* samples);

// A short fixed throw for trying the sound out (the Options page's test): a
// d4, a d6 and a d20 thrown together, knocking once, and settling.
struct DiceTestThrow {
    std::vector<DiceImpact> impacts;
    std::vector<DieVoice> voices;
};
DiceTestThrow diceTestThrow();

}  // namespace combat::ui
