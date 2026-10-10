#include "ui/dice_samples.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

namespace combat::ui {

namespace {

// Longer than any one hit rings; the rest of a long file is dropped.
constexpr double kLongestClip = 2.0;
// The trimmed start keeps this much before the hit.
constexpr double kPreRoll = 0.001;
constexpr double kEndFade = 0.005;

std::uint32_t readU32(const std::vector<std::uint8_t>& b, std::size_t at)
{
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

std::uint16_t readU16(const std::vector<std::uint8_t>& b, std::size_t at)
{
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

bool tagIs(const std::vector<std::uint8_t>& b, std::size_t at, const char* tag)
{
    return std::memcmp(&b[at], tag, 4) == 0;
}

void fail(std::string* error, const char* why)
{
    if (error != nullptr) {
        *error = why;
    }
}

// The dice in the order of their size, for "the nearest die".
int dieRank(int sides)
{
    static constexpr std::array<int, 6> kDice{4, 6, 8, 10, 12, 20};
    if (sides <= 0) {
        return -1;
    }
    int best = 0;
    for (int i = 1; i < static_cast<int>(kDice.size()); ++i) {
        if (std::abs(kDice[static_cast<std::size_t>(i)] - sides) <
            std::abs(kDice[static_cast<std::size_t>(best)] - sides)) {
            best = i;
        }
    }
    return best;
}

// How far one die's clip is from another's: 0 the same, 2 a size apart, and
// "any" a little further than the nearest neighbour.
float dieDistance(int wanted, int have)
{
    if (have == 0) {
        return 5.0f;
    }
    if (wanted == 0) {
        return 0.0f;
    }
    return 2.0f * static_cast<float>(std::abs(dieRank(wanted) - dieRank(have)));
}

float hitDistance(DiceHit wanted, DiceHit have)
{
    // Corner, edge and face in a row; a settle is a soft landing on a face.
    const auto at = [](DiceHit hit) {
        switch (hit) {
        case DiceHit::Corner:
            return 0.0f;
        case DiceHit::Edge:
            return 1.0f;
        case DiceHit::Face:
            return 2.0f;
        default:
            return 2.5f;
        }
    };
    return 3.0f * std::abs(at(wanted) - at(have));
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::vector<std::string> wordsOf(const std::string& text)
{
    std::vector<std::string> words;
    std::string word;
    for (const char c : text) {
        if (c == '_' || c == '-' || c == ' ' || c == '.' || c == '(' || c == ')') {
            if (!word.empty()) {
                words.push_back(word);
            }
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }
    return words;
}

bool isNumber(const std::string& word)
{
    return !word.empty() && word.size() < 6 &&
           std::all_of(word.begin(), word.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

}  // namespace

std::optional<AudioClip> parseWav(const std::vector<std::uint8_t>& bytes, std::string* error)
{
    if (bytes.size() < 12 || !tagIs(bytes, 0, "RIFF") || !tagIs(bytes, 8, "WAVE")) {
        fail(error, "not a WAV file");
        return std::nullopt;
    }
    int format = 0;
    int channels = 0;
    int rate = 0;
    int bits = 0;
    std::size_t dataAt = 0;
    std::size_t dataSize = 0;
    std::size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const std::size_t size = readU32(bytes, at + 4);
        const std::size_t body = at + 8;
        const std::size_t available = std::min(size, bytes.size() - body);
        if (tagIs(bytes, at, "fmt ") && available >= 16) {
            format = readU16(bytes, body);
            channels = readU16(bytes, body + 2);
            rate = static_cast<int>(readU32(bytes, body + 4));
            bits = readU16(bytes, body + 14);
            if (format == 0xFFFE && available >= 26) {
                format = readU16(bytes, body + 24);  // the sub-format's tag
            }
        } else if (tagIs(bytes, at, "data")) {
            dataAt = body;
            dataSize = available;  // a recorder that never finished the header
        }
        at = body + size + (size & 1U);
    }
    if (dataAt == 0 || channels <= 0 || rate < 4000 || rate > 384000) {
        fail(error, "no sound in it");
        return std::nullopt;
    }
    const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool floating = format == 3 && (bits == 32 || bits == 64);
    if (!pcm && !floating) {
        fail(error, "a WAV encoding that isn't PCM or float");
        return std::nullopt;
    }
    const std::size_t sampleBytes = static_cast<std::size_t>(bits / 8);
    const std::size_t frameBytes = sampleBytes * static_cast<std::size_t>(channels);
    std::size_t frames = dataSize / frameBytes;
    frames = std::min(frames, static_cast<std::size_t>(kLongestClip * 4.0 * rate));  // before trimming
    AudioClip clip;
    clip.rate = rate;
    clip.samples.resize(frames, 0.0f);
    for (std::size_t f = 0; f < frames; ++f) {
        double sum = 0.0;
        for (int c = 0; c < channels; ++c) {
            const std::size_t p = dataAt + f * frameBytes + static_cast<std::size_t>(c) * sampleBytes;
            double value = 0.0;
            if (floating && bits == 32) {
                float v = 0.0f;
                std::memcpy(&v, &bytes[p], sizeof v);
                value = static_cast<double>(v);
            } else if (floating) {
                std::memcpy(&value, &bytes[p], sizeof value);
            } else if (bits == 8) {
                value = (static_cast<double>(bytes[p]) - 128.0) / 128.0;
            } else if (bits == 16) {
                value = static_cast<double>(static_cast<std::int16_t>(readU16(bytes, p))) / 32768.0;
            } else if (bits == 24) {
                std::uint32_t raw = static_cast<std::uint32_t>(bytes[p]) | (static_cast<std::uint32_t>(bytes[p + 1]) << 8) |
                                    (static_cast<std::uint32_t>(bytes[p + 2]) << 16);
                if ((raw & 0x800000U) != 0) {
                    raw |= 0xFF000000U;
                }
                value = static_cast<double>(static_cast<std::int32_t>(raw)) / 8388608.0;
            } else {
                value = static_cast<double>(static_cast<std::int32_t>(readU32(bytes, p))) / 2147483648.0;
            }
            if (!std::isfinite(value)) {
                value = 0.0;
            }
            sum += value;
        }
        clip.samples[f] = static_cast<float>(sum / channels);
    }
    float peak = 0.0f;
    for (const float s : clip.samples) {
        peak = std::max(peak, std::abs(s));
    }
    if (peak < 1e-5f) {
        fail(error, "silent");
        return std::nullopt;
    }
    // Start at the hit (so it lands with the die), end within the longest.
    const float threshold = peak * 0.02f;  // about 34 dB down
    const auto first = std::find_if(clip.samples.begin(), clip.samples.end(),
                                    [threshold](float s) { return std::abs(s) >= threshold; });
    const auto preRoll = static_cast<std::ptrdiff_t>(kPreRoll * rate);
    const std::ptrdiff_t start = std::max<std::ptrdiff_t>(0, std::distance(clip.samples.begin(), first) - preRoll);
    clip.samples.erase(clip.samples.begin(), clip.samples.begin() + start);
    if (clip.samples.size() > static_cast<std::size_t>(kLongestClip * rate)) {
        clip.samples.resize(static_cast<std::size_t>(kLongestClip * rate));
    }
    // Trailing silence goes too.
    const float quiet = peak * 0.002f;
    while (clip.samples.size() > 16 && std::abs(clip.samples.back()) < quiet) {
        clip.samples.pop_back();
    }
    const float gain = 0.9f / peak;
    const std::size_t fade = std::min(clip.samples.size(), static_cast<std::size_t>(kEndFade * rate));
    for (std::size_t i = 0; i < clip.samples.size(); ++i) {
        float s = clip.samples[i] * gain;
        const std::size_t fromEnd = clip.samples.size() - i;
        if (fromEnd <= fade) {
            s *= static_cast<float>(fromEnd - 1) / static_cast<float>(std::max<std::size_t>(fade, 1));
        }
        clip.samples[i] = s;
    }
    return clip;
}

std::optional<AudioClip> readWav(const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        fail(error, "can't be opened");
        return std::nullopt;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size > 64U * 1024U * 1024U) {
        fail(error, ec ? "can't be read" : "too big for one hit");
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return parseWav(bytes, error);
}

std::optional<DiceSampleName> parseSampleName(const std::filesystem::path& relative)
{
    DiceSampleName name;
    bool table = false;
    bool dice = false;
    std::vector<int> sides;
    bool sawDie = false;
    std::vector<std::string> words;
    for (const auto& part : relative.parent_path()) {
        for (const std::string& word : wordsOf(lower(part.string()))) {
            words.push_back(word);
        }
    }
    for (const std::string& word : wordsOf(lower(relative.stem().string()))) {
        words.push_back(word);
    }
    for (const std::string& word : words) {
        if (word == "table") {
            table = true;  // the kind of table from another word (wood when none)
        } else if (word == "wood" || word == "wooden") {
            table = true;
            name.surface = TableSurface::Wood;
        } else if (word == "felt" || word == "tray" || word == "mat" || word == "neoprene") {
            table = true;
            name.surface = TableSurface::Felt;
        } else if (word == "dice" || word == "click" || word == "clack" || word == "knock") {
            dice = true;
        } else if (word == "corner") {
            name.hit = DiceHit::Corner;
        } else if (word == "edge") {
            name.hit = DiceHit::Edge;
        } else if (word == "face" || word == "flat") {
            name.hit = DiceHit::Face;
        } else if (word == "settle" || word == "settles" || word == "tip") {
            name.hit = DiceHit::Settle;
        } else if (word == "soft") {
            name.force = DiceForce::Soft;
        } else if (word == "medium" || word == "mid") {
            name.force = DiceForce::Medium;
        } else if (word == "hard") {
            name.force = DiceForce::Hard;
        } else if (word == "any") {
            sides.push_back(0);
            sawDie = true;
        } else if (word.size() >= 2 && word[0] == 'd' && isNumber(word.substr(1))) {
            const int n = std::stoi(word.substr(1));
            if (n == 4 || n == 6 || n == 8 || n == 10 || n == 12 || n == 20 || n == 100) {
                sides.push_back(n == 100 ? 10 : n);
                sawDie = true;
            }
        } else if (isNumber(word)) {
            name.take = std::stoi(word);
        }
    }
    // The folder decides between table and dice: "dice/d6_d20" is two dice
    // knocking, "wood/dice_d6" is a die on wood.
    const std::string top = relative.has_parent_path() ? lower(relative.begin()->string()) : std::string();
    if (top == "dice") {
        table = false;
        dice = true;
    } else if (top == "wood" || top == "felt") {
        dice = false;
    } else if (table) {
        dice = false;  // "dice on felt" is a die landing on felt
    }
    if (!table && !dice) {
        return std::nullopt;
    }
    if (dice) {
        name.anySurface = true;
        name.hit = DiceHit::Dice;
    }
    if (sawDie) {
        name.sides = sides[0];
        name.otherSides = sides.size() > 1 ? sides[1] : 0;
    }
    if (name.hit == DiceHit::Dice && name.otherSides != 0 && name.sides > name.otherSides) {
        std::swap(name.sides, name.otherSides);  // the pair either way round
    }
    return name;
}

DiceForce forceOf(DiceHit hit, float speed)
{
    // The same speeds the sound treats as its hardest (dice_sound.cpp).
    const float hardest = hit == DiceHit::Dice ? 20.0f : 24.0f;
    const float strength = speed / hardest;
    if (strength < 0.30f) {
        return DiceForce::Soft;
    }
    return strength < 0.65f ? DiceForce::Medium : DiceForce::Hard;
}

int DiceSampleBank::load(const std::filesystem::path& folder)
{
    clear();
    m_folder = folder;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return 0;
    }
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec) && lower(it->path().extension().string()) == ".wav") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());  // the same order every run
    for (const std::filesystem::path& file : files) {
        const std::filesystem::path relative = file.lexically_relative(folder);
        const std::optional<DiceSampleName> name = parseSampleName(relative);
        if (!name) {
            m_problems.push_back(relative.generic_string() + ": the name doesn't say wood, felt or dice");
            continue;
        }
        std::string why;
        std::optional<AudioClip> clip = readWav(file, &why);
        if (!clip) {
            m_problems.push_back(relative.generic_string() + ": " + why);
            continue;
        }
        add(*name, std::move(*clip));
    }
    return size();
}

void DiceSampleBank::add(const DiceSampleName& name, AudioClip clip)
{
    m_entries.push_back(Entry{name, std::move(clip)});
}

void DiceSampleBank::clear()
{
    m_entries.clear();
    m_problems.clear();
}

DiceSampleBank::Choice DiceSampleBank::pick(TableSurface surface, DiceHit hit, int sides, int otherSides,
                                            DiceForce force, std::mt19937& random, int avoid) const
{
    const bool dice = hit == DiceHit::Dice;
    if (dice && otherSides != 0 && (sides == 0 || sides > otherSides)) {
        std::swap(sides, otherSides);
    }
    std::vector<float> costs(m_entries.size(), std::numeric_limits<float>::infinity());
    float best = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const DiceSampleName& name = m_entries[i].name;
        if ((name.hit == DiceHit::Dice) != dice) {
            continue;
        }
        float cost = std::abs(static_cast<float>(static_cast<int>(name.force) - static_cast<int>(force)));
        if (dice) {
            cost += std::min(dieDistance(sides, name.sides) + dieDistance(otherSides, name.otherSides),
                             dieDistance(sides, name.otherSides) + dieDistance(otherSides, name.sides));
        } else {
            cost += hitDistance(hit, name.hit) + dieDistance(sides, name.sides);
            if (name.surface != surface) {
                cost += 100.0f;
            }
        }
        costs[i] = cost;
        best = std::min(best, cost);
    }
    if (!std::isfinite(best)) {
        return {};
    }
    // The takes of the best: all as close as each other.
    std::vector<int> takes;
    for (std::size_t i = 0; i < costs.size(); ++i) {
        if (costs[i] <= best + 0.01f) {
            takes.push_back(static_cast<int>(i));
        }
    }
    if (takes.size() > 1) {
        takes.erase(std::remove(takes.begin(), takes.end(), avoid), takes.end());
    }
    std::uniform_int_distribution<std::size_t> which(0, takes.size() - 1);
    const int id = takes[which(random)];
    const Entry& entry = m_entries[static_cast<std::size_t>(id)];
    return {&entry.clip, id, !dice && entry.name.surface != surface};
}

}  // namespace combat::ui
