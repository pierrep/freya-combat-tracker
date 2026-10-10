#include "ui/dice_audio.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cstring>

namespace combat::ui {

// The samples, read by the sink as it needs them, in the output's format. A
// stop fades the rest out instead of cutting it.
class DiceAudioStream : public QIODevice {
public:
    DiceAudioStream(std::vector<float> stereo, const QAudioFormat& format, bool loop, QObject* parent)
        : QIODevice(parent), m_samples(std::move(stereo)), m_format(format), m_loop(loop)
    {
    }

    bool isSequential() const override { return true; }
    // Qt's PulseAudio output (6.8) rewinds its source when it stops, and the
    // base class would warn that a stream can't seek. There is nothing to
    // rewind: the next throw brings a new stream.
    bool reset() override { return true; }
    bool atEnd() const override { return !m_loop && m_frame * 2 >= m_samples.size(); }
    qint64 bytesAvailable() const override
    {
        if (m_loop) {
            return static_cast<qint64>(m_samples.size() / 2) * m_format.bytesPerFrame() + QIODevice::bytesAvailable();
        }
        const std::size_t framesLeft = m_samples.size() / 2 - std::min(m_frame, m_samples.size() / 2);
        return static_cast<qint64>(framesLeft) * m_format.bytesPerFrame() + QIODevice::bytesAvailable();
    }

    void fadeOut(int frames)
    {
        if (m_loop) {
            // The rest of this time through is all there is.
            m_loop = false;
        }
        const std::size_t total = m_samples.size() / 2;
        m_fadeLeft = std::max(1, frames);
        m_fadeFrames = m_fadeLeft;
        // Nothing after the fade.
        const std::size_t end = std::min(total, m_frame + static_cast<std::size_t>(m_fadeLeft));
        m_samples.resize(end * 2);
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const int frameBytes = m_format.bytesPerFrame();
        const int channels = m_format.channelCount();
        if (frameBytes <= 0 || channels <= 0) {
            return 0;
        }
        const std::size_t total = m_samples.size() / 2;
        if (total == 0) {
            return 0;
        }
        const std::size_t wanted = static_cast<std::size_t>(maxSize / frameBytes);
        std::size_t count = 0;
        m_block.clear();
        while (count < wanted) {
            if (m_frame >= total) {
                if (!m_loop) {
                    break;
                }
                m_frame = 0;
            }
            float left = m_samples[m_frame * 2];
            float right = m_samples[m_frame * 2 + 1];
            if (m_fadeFrames > 0) {
                const float gain = static_cast<float>(std::max(0, m_fadeLeft)) / static_cast<float>(m_fadeFrames);
                left *= gain;
                right *= gain;
                --m_fadeLeft;
            }
            m_block.push_back(left);
            m_block.push_back(right);
            ++m_frame;
            ++count;
        }
        const int sampleBytes = m_format.bytesPerSample();
        char* at = data;
        for (std::size_t i = 0; i < count; ++i) {
            const float left = m_block[i * 2];
            const float right = m_block[i * 2 + 1];
            for (int c = 0; c < channels; ++c) {
                // Mono gets both; a third channel and on get nothing.
                const float value = channels == 1 ? 0.5f * (left + right) : (c == 0 ? left : (c == 1 ? right : 0.0f));
                write(at, value);
                at += sampleBytes;
            }
        }
        return static_cast<qint64>(count) * frameBytes;
    }

    qint64 writeData(const char*, qint64) override { return -1; }

private:
    void write(char* at, float value) const
    {
        value = std::clamp(value, -1.0f, 1.0f);
        switch (m_format.sampleFormat()) {
        case QAudioFormat::UInt8: {
            const auto sample = static_cast<quint8>(std::lround((value + 1.0f) * 127.5f));
            std::memcpy(at, &sample, sizeof sample);
            break;
        }
        case QAudioFormat::Int16: {
            const auto sample = static_cast<qint16>(std::lround(value * 32767.0f));
            std::memcpy(at, &sample, sizeof sample);
            break;
        }
        case QAudioFormat::Int32: {
            const auto sample = static_cast<qint32>(std::lround(static_cast<double>(value) * 2147483647.0));
            std::memcpy(at, &sample, sizeof sample);
            break;
        }
        case QAudioFormat::Float:
            std::memcpy(at, &value, sizeof value);
            break;
        default:
            std::memset(at, 0, static_cast<std::size_t>(m_format.bytesPerSample()));
            break;
        }
    }

    std::vector<float> m_samples;
    QAudioFormat m_format;
    bool m_loop = false;
    std::vector<float> m_block;
    std::size_t m_frame = 0;
    int m_fadeLeft = 0;
    int m_fadeFrames = 0;
};

DiceAudio::DiceAudio(QObject* parent) : QObject(parent) {}

DiceAudio::~DiceAudio()
{
    if (m_sink != nullptr) {
        m_sink->disconnect(this);
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    delete m_stream;
    m_stream = nullptr;
}

bool DiceAudio::prepare()
{
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        return false;
    }
    m_rate = device.preferredFormat().sampleRate() > 0 ? device.preferredFormat().sampleRate() : 48000;
    return true;
}

void DiceAudio::play(std::vector<float> stereo, bool loop)
{
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull() || stereo.empty()) {
        return;
    }
    QAudioFormat format;
    format.setSampleRate(m_rate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Float);
    }
    if (!device.isFormatSupported(format)) {
        format = device.preferredFormat();  // its rate is the one rendered at
    }
    // One output, kept from throw to throw: PulseAudio's (Qt 6.4) can crash
    // when an output is deleted soon after it played, so a new one is made
    // only when the computer's output or its format changes.
    if (m_sink != nullptr && (device.id() != m_deviceId || format != m_format)) {
        m_sink->disconnect(this);
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    if (m_sink == nullptr) {
        m_sink = new QAudioSink(device, format, this);
        // Small enough that the clatter keeps up with the dice on the page,
        // big enough that PipeWire and PulseAudio don't run dry at the start.
        m_sink->setBufferSize(format.bytesForDuration(100000));
        m_deviceId = device.id();
        m_format = format;
        connect(m_sink, &QAudioSink::stateChanged, this, &DiceAudio::onState);
    } else {
        m_sink->stop();  // whatever the last throw still had to play
    }
    if (m_stream != nullptr) {
        // Read no more; let go of it once this event is done.
        m_stream->close();
        m_stream->deleteLater();
    }
    m_stream = new DiceAudioStream(std::move(stereo), format, loop, nullptr);
    m_stream->open(QIODevice::ReadOnly);
    m_playing = true;
    m_sink->start(m_stream);
}

void DiceAudio::onState(QAudio::State state)
{
    if (m_sink == nullptr) {
        return;
    }
    if (state == QAudio::StoppedState && m_sink->error() != QAudio::NoError && m_sink->error() != QAudio::UnderrunError) {
        qWarning("Dice sound: the audio output stopped (error %d)", static_cast<int>(m_sink->error()));
    }
    // Idle also means the output ran dry for a moment (an underrun, common as
    // a stream starts); only a stream played to its end is finished. The
    // output stays, idle, for the next throw.
    if (state == QAudio::IdleState && m_stream != nullptr && m_stream->atEnd()) {
        m_playing = false;
    }
    if (state == QAudio::StoppedState) {
        m_playing = false;
    }
}

void DiceAudio::stop()
{
    if (m_sink == nullptr || m_stream == nullptr) {
        return;
    }
    // About 8 ms: gone at once to the ear, without a pop.
    m_stream->fadeOut(std::max(1, m_rate / 125));
}

}  // namespace combat::ui
