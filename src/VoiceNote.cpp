#include "VoiceNote.hpp"

#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioInput>
#include <QAudioOutput>
#include <QDateTime>
#include <QDir>
#include <QFile>

namespace {

// Mono. A voice note is one person talking into one microphone, and stereo would double
// the upload for nothing.
constexpr int Channels = 1;

// Below this a recording is a mis-tap rather than a message, and sending it would just
// put an empty bubble in the chat.
constexpr int MinimumDurationMs = 1000;

QAudioFormat pcmFormat(int sampleRate) noexcept
{
    QAudioFormat format;

    // setFrequency/setChannels rather than the setSampleRate/setChannelCount spelling:
    // both exist in 4.7 but only these are there in every Qt 4 this might build against.
    format.setFrequency(sampleRate);
    format.setChannels(Channels);
    format.setSampleSize(16);
    format.setSampleType(QAudioFormat::SignedInt);
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setCodec("audio/pcm");

    return format;
}

}  // namespace

VoiceNote::VoiceNote(QObject *parent)
    : QObject(parent)
{
}

VoiceNote::~VoiceNote()
{
    // Not cancelRecording()/stop(): those emit, and a signal sent from here reaches
    // bindings on an object that is already half gone - ~QObject has not run yet, so
    // nothing is disconnected. Same teardown, no notifications.
    if (const auto path = finishRecording(); !path.isEmpty())
        QFile::remove(path);

    if (m_output)
    {
        auto *output = m_output;
        m_output = nullptr;

        output->stop();
        // Deleted outright rather than deferred: there is no event loop turn left for a
        // deleteLater scheduled from a destructor to be serviced in.
        delete output;
    }

    m_playback.close();
}

bool VoiceNote::isRecording() const noexcept
{
    return m_input != nullptr;
}

bool VoiceNote::isPlaying() const noexcept
{
    return m_output != nullptr;
}

int VoiceNote::duration() const noexcept
{
    return m_duration;
}

QString VoiceNote::source() const noexcept
{
    return m_source;
}

void VoiceNote::startRecording()
{
    if (isRecording())
        return;

    // Playback and capture do not share the device gracefully here, and a note that keeps
    // talking while you record over it is not what the button implies anyway.
    stop();

    const auto device = QAudioDeviceInfo::defaultInputDevice();

    // Opus takes only its own five rates, so the microphone is asked for one of those
    // rather than opened at whatever it prefers and resampled afterwards.
    m_sampleRate = 0;
    for (int rate : OggOpus::SampleRates)
    {
        if (device.isFormatSupported(pcmFormat(rate)))
        {
            m_sampleRate = rate;
            break;
        }
    }

    if (m_sampleRate == 0)
    {
        emit error(tr("ErrorOccurred"));
        return;
    }

    m_recordingPath = QDir::tempPath() + QString("/meegram-voice-%1.oga").arg(QDateTime::currentMSecsSinceEpoch());

    m_writer = std::make_unique<OggOpus::Writer>(m_recordingPath.toStdString(), m_sampleRate, Channels);
    if (!m_writer->isOpen())
    {
        m_writer.reset();
        emit error(tr("ErrorOccurred"));
        return;
    }

    m_recordedSamples = 0;
    m_duration = 0;

    m_input = new QAudioInput(device, pcmFormat(m_sampleRate), this);
    m_capture = m_input->start();

    if (!m_capture)
    {
        // finishRecording clears m_recordingPath, so the half-written file has to be
        // removed by the path it hands back rather than by the member.
        if (const auto path = finishRecording(); !path.isEmpty())
            QFile::remove(path);

        emit error(tr("ErrorOccurred"));
        return;
    }

    connect(m_capture, SIGNAL(readyRead()), SLOT(readMicrophone()));

    emit durationChanged();
    emit recordingChanged();
}

void VoiceNote::readMicrophone()
{
    if (!m_capture || !m_writer)
        return;

    const auto data = m_capture->readAll();
    if (data.isEmpty())
        return;

    // An odd trailing byte is half a sample. Carried into the next read rather than
    // dropped, which would shift every sample after it by one byte.
    m_captureCarry.append(data);

    const auto usable = m_captureCarry.size() & ~1;
    if (usable == 0)
        return;

    const auto *samples = reinterpret_cast<const std::int16_t *>(m_captureCarry.constData());
    const auto count = static_cast<int>(usable / 2);

    m_writer->write(samples, count);
    m_recordedSamples += count / Channels;

    m_captureCarry.remove(0, usable);

    if (const auto seconds = static_cast<int>(m_recordedSamples / m_sampleRate); seconds != m_duration)
    {
        m_duration = seconds;
        emit durationChanged();
    }
}

QString VoiceNote::finishRecording()
{
    if (m_capture)
    {
        m_capture->disconnect(this);
        m_capture = nullptr;
    }

    if (m_input)
    {
        // Cleared before the teardown, so anything the device emits on its way down finds
        // an object that already considers itself stopped.
        auto *input = m_input;
        m_input = nullptr;

        input->stop();
        input->deleteLater();
    }

    const auto complete = m_writer && m_writer->close();

    m_writer.reset();
    m_captureCarry.clear();

    const auto path = m_recordingPath;
    m_recordingPath.clear();

    return complete ? path : QString();
}

void VoiceNote::stopRecording()
{
    if (!isRecording())
        return;

    const auto milliseconds = m_sampleRate > 0 ? m_recordedSamples * 1000 / m_sampleRate : 0;
    const auto path = finishRecording();

    emit recordingChanged();

    if (path.isEmpty())
    {
        emit error(tr("ErrorOccurred"));
        return;
    }

    if (milliseconds < MinimumDurationMs)
    {
        QFile::remove(path);
        return;
    }

    // Rounded up, so a 1.4 second note does not arrive claiming to last one second.
    emit recorded(path, static_cast<int>((milliseconds + 999) / 1000));
}

void VoiceNote::cancelRecording()
{
    if (!isRecording())
        return;

    const auto path = finishRecording();

    if (!path.isEmpty())
        QFile::remove(path);

    emit recordingChanged();
}

void VoiceNote::play(const QString &path)
{
    // Refused rather than handled: silently binning a recording someone is halfway
    // through would be worse than a tap that does nothing.
    if (isRecording() || path.isEmpty())
        return;

    stop();

    int sampleRate = 0;
    int channels = 0;

    const auto pcm = OggOpus::decode(path.toStdString(), &sampleRate, &channels);
    if (pcm.empty() || channels < 1)
    {
        emit error(tr("ErrorOccurred"));
        return;
    }

    auto format = pcmFormat(sampleRate);
    format.setChannels(channels);

    // QBuffer over the decoded samples, rather than a temp wav and a file player: the
    // whole note is already in memory by the time it is decoded.
    m_playbackData = QByteArray(reinterpret_cast<const char *>(pcm.data()), static_cast<int>(pcm.size() * sizeof(std::int16_t)));

    m_playback.setBuffer(&m_playbackData);
    if (!m_playback.open(QIODevice::ReadOnly))
    {
        emit error(tr("ErrorOccurred"));
        return;
    }

    m_source = path;
    m_output = new QAudioOutput(format, this);

    connect(m_output, SIGNAL(stateChanged(QAudio::State)), SLOT(handlePlaybackState(QAudio::State)));

    m_output->start(&m_playback);

    emit playingChanged();
}

void VoiceNote::handlePlaybackState(QAudio::State state)
{
    // Idle means the buffer ran dry, which for a fixed buffer is the note having finished.
    if (m_output && state == QAudio::IdleState)
        stop();
}

void VoiceNote::stop()
{
    if (!m_output)
        return;

    // Same order as finishRecording, and for the same reason: stop() re-enters
    // handlePlaybackState, which must find a player that is already gone.
    auto *output = m_output;
    m_output = nullptr;

    output->stop();
    output->deleteLater();

    m_playback.close();
    m_playbackData.clear();
    m_source.clear();

    emit playingChanged();
}
