#include "ui/dice_sound.h"

#include "ui/dice_samples.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace combat::ui {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;
// A die's first bounce is about this fast (radii a second); harder than this
// plays no louder.
constexpr float kHardTable = 24.0f;
constexpr float kHardKnock = 20.0f;
// The quiet left after the last recording.
constexpr double kTail = 0.12;
// Contacts of one die closer together than this are one sound (the step that
// meets the table and the one after it).
constexpr double kMergeWindow = 0.012;

// One-pole low-pass, coefficient for a cutoff.
float lowPassCoefficient(float cutoff, int rate)
{
    return 1.0f - static_cast<float>(std::exp(-kTwoPi * static_cast<double>(cutoff) / rate));
}

// Equal-power pan.
std::pair<float, float> panGains(float pan)
{
    const double angle = (static_cast<double>(std::clamp(pan, -1.0f, 1.0f)) + 1.0) * kTwoPi / 8.0;
    return {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
}

// One die's contacts close together are one sound (the loudest).
std::vector<DiceImpact> merged(std::vector<DiceImpact> impacts)
{
    std::sort(impacts.begin(), impacts.end(),
              [](const DiceImpact& a, const DiceImpact& b) { return a.time < b.time; });
    std::vector<DiceImpact> kept;
    for (const DiceImpact& impact : impacts) {
        auto same = std::find_if(kept.rbegin(), kept.rend(), [&impact](const DiceImpact& earlier) {
            return earlier.kind == impact.kind && earlier.die == impact.die && earlier.other == impact.other &&
                   impact.time - earlier.time < kMergeWindow;
        });
        if (same != kept.rend()) {
            if (impact.speed > same->speed) {
                const double time = same->time;
                *same = impact;
                same->time = time;
            }
            continue;
        }
        kept.push_back(impact);
    }
    return kept;
}

// Recorded hits: how loud a hit of this strength plays its clip (the clips are
// all made as loud as each other).
constexpr float kSampleLevel = 0.62f;
constexpr int kMostVoices = 24;
constexpr double kCutFade = 0.004;

struct SampleHit {
    const AudioClip* clip = nullptr;
    std::size_t start = 0;   // output frame
    double step = 1.0;       // clip samples per output frame
    std::size_t length = 0;  // output frames
    std::size_t cutAt = std::numeric_limits<std::size_t>::max();
    float left = 0.0f;
    float right = 0.0f;
    float loudness = 0.0f;
    float dull = 0.0f;  // 0, or the low-pass coefficient that makes a wooden clip felt
    bool shorten = false;
};

DiceHit hitFor(const DiceImpact& impact)
{
    switch (impact.kind) {
    case DiceImpact::Kind::Dice:
        return DiceHit::Dice;
    case DiceImpact::Kind::Settle:
        return DiceHit::Settle;
    default:
        return impact.contacts >= 3 ? DiceHit::Face : (impact.contacts == 2 ? DiceHit::Edge : DiceHit::Corner);
    }
}

void mix(std::vector<float>& out, const SampleHit& hit, int rate)
{
    const std::vector<float>& in = hit.clip->samples;
    const std::size_t fade = static_cast<std::size_t>(kCutFade * rate);
    const std::size_t end = hit.cutAt == std::numeric_limits<std::size_t>::max()
                                ? hit.length
                                : std::min(hit.length, hit.cutAt - hit.start + fade);
    // A felt tray swallows a wooden clip's ring.
    const float shortDecay = hit.shorten ? std::exp(-1.0f / (0.07f * static_cast<float>(rate))) : 1.0f;
    float tail = 1.0f;
    float low = 0.0f;
    for (std::size_t i = 0; i < end; ++i) {
        const double at = static_cast<double>(i) * hit.step;
        const std::size_t j = static_cast<std::size_t>(at);
        if (j + 1 >= in.size()) {
            break;
        }
        const float t = static_cast<float>(at - static_cast<double>(j));
        float sample = in[j] + t * (in[j + 1] - in[j]);
        if (hit.dull > 0.0f) {
            low += hit.dull * (sample - low);
            sample = low;
        }
        sample *= tail;
        tail *= shortDecay;
        if (hit.start + i >= hit.cutAt) {
            sample *= 1.0f - static_cast<float>(hit.start + i - hit.cutAt) / static_cast<float>(std::max<std::size_t>(fade, 1));
        }
        const std::size_t frame = (hit.start + i) * 2;
        if (frame + 1 >= out.size()) {
            break;
        }
        out[frame] += sample * hit.left;
        out[frame + 1] += sample * hit.right;
    }
}

// Past kMostVoices at once, the quietest is cut short (or, if it's the new
// one, never starts).
void limitVoices(std::vector<SampleHit>& hits)
{
    std::sort(hits.begin(), hits.end(), [](const SampleHit& a, const SampleHit& b) { return a.start < b.start; });
    std::vector<std::size_t> playing;
    std::vector<SampleHit> kept;
    for (SampleHit& hit : hits) {
        playing.erase(std::remove_if(playing.begin(), playing.end(),
                                     [&kept, &hit](std::size_t k) {
                                         const SampleHit& other = kept[k];
                                         return std::min(other.start + other.length, other.cutAt) <= hit.start;
                                     }),
                      playing.end());
        if (static_cast<int>(playing.size()) >= kMostVoices) {
            const auto quietest = std::min_element(playing.begin(), playing.end(), [&kept](std::size_t a, std::size_t b) {
                return kept[a].loudness < kept[b].loudness;
            });
            if (kept[*quietest].loudness >= hit.loudness) {
                continue;
            }
            kept[*quietest].cutAt = hit.start;
            playing.erase(quietest);
        }
        playing.push_back(kept.size());
        kept.push_back(hit);
    }
    hits = std::move(kept);
}


// Never past full scale: a soft knee above 0.8, so a pile of hard hits is
// squeezed rather than clipped.
float limit(float x)
{
    const float size = std::abs(x);
    if (size <= 0.8f) {
        return x;
    }
    const float squeezed = 0.8f + 0.2f * std::tanh((size - 0.8f) / 0.2f);
    return std::copysign(std::min(squeezed, 0.999f), x);
}

}  // namespace

std::vector<float> renderDiceSound(const std::vector<DiceImpact>& impacts, const std::vector<DieVoice>& voices,
                                   const DiceSoundSettings& settings, std::uint32_t seed, const DiceSampleBank* samples)
{
    const int rate = std::clamp(settings.sampleRate, 8000, 192000);
    double last = 0.0;
    for (const DiceImpact& impact : impacts) {
        last = std::max(last, impact.time);
    }
    std::size_t frames = static_cast<std::size_t>((last + kTail) * rate);
    const float volume = std::clamp(settings.volume, 0.0f, 1.0f);
    if (volume <= 0.0f || voices.empty() || samples == nullptr || samples->empty()) {
        return std::vector<float>(frames * 2, 0.0f);
    }
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    std::vector<int> lastTake(voices.size(), -1);
    std::vector<SampleHit> hits;
    for (const DiceImpact& impact : merged(impacts)) {
        if (impact.die < 0 || impact.die >= static_cast<int>(voices.size()) || impact.speed < kQuietestImpact) {
            continue;
        }
        const DiceHit hit = hitFor(impact);
        const DieVoice& voice = voices[static_cast<std::size_t>(impact.die)];
        const int other = impact.other >= 0 && impact.other < static_cast<int>(voices.size())
                              ? voices[static_cast<std::size_t>(impact.other)].sides
                              : 0;
        int& avoid = lastTake[static_cast<std::size_t>(impact.die)];
        const DiceSampleBank::Choice choice =
            samples->pick(settings.surface, hit, voice.sides, other, forceOf(hit, impact.speed), random, avoid);
        if (choice.clip == nullptr || choice.clip->samples.size() < 2) {
            continue;  // nothing recorded of this kind
        }
        avoid = choice.id;
        const float hardest = hit == DiceHit::Dice ? kHardKnock : kHardTable;
        const float strength = std::min(impact.speed / hardest, 1.4f);
        SampleHit play;
        play.clip = choice.clip;
        play.start = static_cast<std::size_t>(std::max(0.0, impact.time) * rate);
        // Each hit a little higher or lower, and the die's own detune.
        const double pitch = static_cast<double>(voice.detune) * (1.0 + 0.03 * static_cast<double>(jitter(random)));
        play.step = static_cast<double>(choice.clip->rate) / rate * pitch;
        play.length = static_cast<std::size_t>(static_cast<double>(choice.clip->samples.size() - 1) / play.step);
        play.loudness = std::pow(strength, 1.2f) * (1.0f + 0.1f * jitter(random));
        const float level = kSampleLevel * volume * play.loudness;
        const auto [left, right] = panGains(impact.pan);
        play.left = left * level;
        play.right = right * level;
        if (choice.otherSurface && settings.surface == TableSurface::Felt) {
            play.dull = lowPassCoefficient(1800.0f, rate);
            play.shorten = true;
        }
        frames = std::max(frames, play.start + play.length + static_cast<std::size_t>(kTail * rate));
        hits.push_back(play);
    }
    std::vector<float> out(frames * 2, 0.0f);
    limitVoices(hits);
    for (const SampleHit& hit : hits) {
        mix(out, hit, rate);
    }
    for (float& sample : out) {
        sample = limit(sample);
    }
    return out;
}

DiceTestThrow diceTestThrow()
{
    DiceTestThrow test;
    const auto voice = [](int sides, float detune) {
        DieVoice v;
        v.sides = sides;
        v.detune = detune;
        return v;
    };
    test.voices = {voice(4, 1.0f), voice(6, 1.01f), voice(20, 0.99f)};
    const auto hit = [&test](DiceImpact::Kind kind, double time, int die, float speed, int contacts, float pan,
                             int other = -1) {
        DiceImpact impact;
        impact.kind = kind;
        impact.time = time;
        impact.die = die;
        impact.other = other;
        impact.speed = speed;
        impact.contacts = contacts;
        impact.pan = pan;
        test.impacts.push_back(impact);
    };
    using Kind = DiceImpact::Kind;
    // Three dice land one after another, bounce, knock once and come to rest.
    hit(Kind::Table, 0.05, 2, 23.0f, 1, -0.5f);
    hit(Kind::Table, 0.09, 1, 21.0f, 2, -0.2f);
    hit(Kind::Table, 0.13, 0, 19.0f, 3, -0.35f);
    hit(Kind::Table, 0.24, 2, 14.0f, 2, -0.2f);
    hit(Kind::Dice, 0.31, 1, 12.0f, 1, 0.0f, 2);
    hit(Kind::Table, 0.33, 1, 11.0f, 1, 0.05f);
    hit(Kind::Table, 0.40, 0, 9.0f, 2, -0.1f);
    hit(Kind::Table, 0.43, 2, 8.5f, 1, 0.15f);
    hit(Kind::Table, 0.52, 1, 6.0f, 3, 0.25f);
    hit(Kind::Table, 0.58, 2, 5.0f, 2, 0.35f);
    hit(Kind::Table, 0.66, 2, 3.5f, 1, 0.45f);
    hit(Kind::Settle, 0.74, 0, 3.0f, 3, 0.0f);
    hit(Kind::Settle, 0.82, 1, 2.8f, 3, 0.3f);
    hit(Kind::Table, 0.86, 2, 2.4f, 1, 0.5f);
    hit(Kind::Settle, 0.95, 2, 2.6f, 3, 0.55f);
    return test;
}


}  // namespace combat::ui
