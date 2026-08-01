#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// OggOpus, because Harmattan has none. The device's GStreamer is 0.10 and predates the
// opus elements, and libopus is not in the image, so the codec is linked in statically
// and the container is muxed here. Nothing else will do: inputMessageVoiceNote is the
// only way to send a voice message and TDLib accepts only OggOpus for it.
namespace OggOpus {

// The only input rates libopus accepts, most-preferred first. The microphone has to be
// opened at one of these rather than at whatever the device would rather hand over.
inline constexpr int SampleRates[] = {16000, 24000, 48000, 12000, 8000};

bool isValidSampleRate(int rate) noexcept;

// No Qt below this line, deliberately. The container muxing is the part of this feature
// most likely to be subtly wrong, and keeping it to the standard library is what lets
// tools/opus_roundtrip.cpp build and run on a development machine that has no Qt 4.

// Incremental, so a long recording is never held as raw PCM and stopping does not stall
// on a whole-file encode - a minute of audio takes seconds to encode on a Cortex-A8.
class Writer
{
public:
    Writer(const std::string &path, int sampleRate, int channels);
    ~Writer();

    Writer(const Writer &) = delete;
    Writer &operator=(const Writer &) = delete;

    bool isOpen() const noexcept;

    // 16-bit interleaved samples. A tail that does not fill a whole 20 ms frame stays
    // buffered until the next call, or until close() pads it out.
    bool write(const std::int16_t *samples, int sampleCount) noexcept;

    // Flushes the tail frame and the end-of-stream page. The destructor calls it, but
    // this return value is the only way to know the file came out complete.
    bool close() noexcept;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

// The whole file as 16-bit interleaved PCM, empty on anything that is not a readable
// OggOpus stream. Always decodes at 48 kHz - that is the rate the format is defined at,
// and asking libopus for less would throw away the top octave of a note recorded wider.
//
// ponytail: decodes into one buffer, ~5.5 MB for a minute of mono. Feed QAudioOutput
// page by page if voice notes ever get long enough for that to matter.
std::vector<std::int16_t> decode(const std::string &path, int *sampleRate, int *channels) noexcept;

}  // namespace OggOpus
