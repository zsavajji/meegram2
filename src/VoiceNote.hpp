#pragma once

#include "OggOpus.hpp"

#include <QAudio>
#include <QBuffer>
#include <QObject>

#include <memory>

class QAudioInput;
class QAudioOutput;

// Microphone and speaker for voice notes. The audio devices are Qt's own QtMultimedia -
// the platform does the capture and playback it has always been able to do - and the
// only thing missing on Harmattan, the opus codec, comes from OggOpus.
//
// One instance per chat page drives both directions, because recording while a note
// plays is not a thing the UI offers and sharing the object keeps "only one note plays
// at a time" true without any bookkeeping.
class VoiceNote : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)

    // Seconds captured so far. Whole seconds only: it drives a mm:ss label.
    Q_PROPERTY(int duration READ duration NOTIFY durationChanged)

    // Path of the note being played, so a delegate can tell whether it is the one making
    // the sound without every delegate holding its own player.
    Q_PROPERTY(QString source READ source NOTIFY playingChanged)

public:
    explicit VoiceNote(QObject *parent = nullptr);
    ~VoiceNote() override;

    bool isRecording() const noexcept;
    bool isPlaying() const noexcept;
    int duration() const noexcept;
    QString source() const noexcept;

    // Opens the microphone and starts writing an .oga into the temp directory. Fails
    // loudly through error() rather than silently doing nothing, because a mute record
    // button is indistinguishable from a broken one.
    Q_INVOKABLE void startRecording();

    // Ends the recording and hands the finished file over through recorded(). Anything
    // shorter than a second is discarded instead - that is a mis-tap, not a message.
    Q_INVOKABLE void stopRecording();

    Q_INVOKABLE void cancelRecording();

    Q_INVOKABLE void play(const QString &path);
    Q_INVOKABLE void stop();

signals:
    void recordingChanged();
    void playingChanged();
    void durationChanged();

    void recorded(const QString &path, int duration);
    void error(const QString &message);

private slots:
    void readMicrophone();
    void handlePlaybackState(QAudio::State state);

private:
    // Tears down the microphone and returns the recorded path, empty if nothing usable
    // came out. Both stopRecording and cancelRecording go through it so the device is
    // never left open on one path and closed on the other.
    QString finishRecording();

    QAudioInput *m_input{};
    QIODevice *m_capture{};

    std::unique_ptr<OggOpus::Writer> m_writer;

    QString m_recordingPath;
    int m_sampleRate{};
    qint64 m_recordedSamples{};
    int m_duration{};

    // A read can end on half a sample, and the odd byte belongs to the next one.
    QByteArray m_captureCarry;

    QAudioOutput *m_output{};
    QBuffer m_playback;
    QByteArray m_playbackData;
    QString m_source;
};
