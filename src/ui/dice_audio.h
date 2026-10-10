#pragma once

// Plays a dice throw's sound (dice_sound.h) on the default audio output.
// Built only with Qt Multimedia (FREYA_HAVE_AUDIO); without it the dice are
// silent.

#include <QObject>

#include <memory>
#include <vector>

class QAudioSink;

namespace combat::ui {

class DiceAudioStream;

class DiceAudio : public QObject {
    Q_OBJECT

public:
    explicit DiceAudio(QObject* parent = nullptr);
    ~DiceAudio() override;

    // Picks the output to play on now (the computer's default may have
    // changed since the last throw). False when there is none.
    bool prepare();
    // The rate to render at for that output.
    int sampleRate() const { return m_rate; }
    // Plays these stereo samples (interleaved left, right) from the start,
    // replacing anything still playing; over and over until stopped when
    // looping.
    void play(std::vector<float> stereo, bool loop = false);
    bool playing() const { return m_sink != nullptr; }
    // Fades out over a few milliseconds and stops (no click).
    void stop();

private:
    void release();

    QAudioSink* m_sink = nullptr;
    DiceAudioStream* m_stream = nullptr;
    int m_rate = 48000;
};

}  // namespace combat::ui
