// Proves src/OggOpus.cpp writes a file that a decoder can actually read back.
//
// The ogg muxing is the part worth pinning down. Opus itself is a library and either
// works or does not, but the container around it is hand-written here: header pages
// flushed alone, granule positions counted at 48 kHz whatever the encoder was fed, the
// pre-skip declared and then trimmed on the way out, an end-of-stream flag on the last
// page. Every one of those is silent when wrong - the file still opens, it just decodes
// to nothing, or to audio that starts with a click, or to something TDLib rejects on
// upload with no reason given.
//
// So this encodes a known tone, decodes it back, and asserts on what survived: the right
// length at the right rate, and a signal with roughly the energy that went in. Opus is
// lossy, so nothing here compares samples.
//
// Build: target opus_roundtrip. No device, no Qt GUI, no network - the codec alone.

#include "OggOpus.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int SampleRate = 16000;
constexpr int Seconds = 2;
constexpr double Frequency = 440.0;
constexpr double Amplitude = 8000.0;

std::string tempFile(const char *name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

double rms(const std::vector<std::int16_t> &samples)
{
    if (samples.empty())
        return 0.0;

    double total = 0.0;
    for (auto sample : samples)
        total += static_cast<double>(sample) * sample;

    return std::sqrt(total / samples.size());
}

std::vector<std::int16_t> tone()
{
    std::vector<std::int16_t> samples(static_cast<std::size_t>(SampleRate) * Seconds);

    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<std::int16_t>(Amplitude * std::sin(2.0 * M_PI * Frequency * i / SampleRate));

    return samples;
}

void checkRoundTrip(const std::string &path)
{
    const auto input = tone();

    {
        OggOpus::Writer writer(path, SampleRate, 1);
        assert(writer.isOpen());

        // Written in chunks that do not divide evenly into 20 ms frames, because that is
        // what a microphone delivers and it exercises the writer's partial-frame buffer.
        constexpr int Chunk = 511;
        for (std::size_t offset = 0; offset < input.size(); offset += Chunk)
        {
            const auto count = static_cast<int>(std::min<std::size_t>(Chunk, input.size() - offset));
            assert(writer.write(input.data() + offset, count));
        }

        assert(writer.close());
    }

    std::ifstream stream(path, std::ios::binary);
    assert(stream);

    const std::string raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    stream.close();

    // An ogg file starts with a page, and the first packet of an OggOpus stream is the
    // identification header. Both live in the first page, so both are in the first bytes.
    assert(raw.starts_with("OggS"));
    assert(raw.substr(0, 64).find("OpusHead") != std::string::npos);
    assert(raw.find("OpusTags") != std::string::npos);

    int rate = 0;
    int channels = 0;

    const auto output = OggOpus::decode(path, &rate, &channels);

    assert(rate == 48000);
    assert(channels == 1);
    assert(!output.empty());

    // Two seconds at 48 kHz, give or take the frame the tail is padded to. A broken
    // pre-skip or granule position shows up here as a length that is off by the encoder
    // lookahead or by whole seconds.
    const auto expected = static_cast<std::size_t>(48000) * Seconds;
    const auto tolerance = static_cast<std::size_t>(48000) / 20;  // one 50 ms frame either way

    assert(output.size() + tolerance > expected);
    assert(output.size() < expected + tolerance);

    // The tone is still a tone. A stream that muxed wrong decodes to silence, which has
    // an RMS of zero and passes every length check above.
    const auto in = rms(input);
    const auto out = rms(output);

    // Wide bounds on purpose. A pure sine is a pathological input for a speech codec at
    // 20 kbps and the energy it comes back with is not something to pin down tightly; the
    // failure this is here to catch is silence or noise, which misses by far more.
    assert(in > 0.0);
    assert(out > in * 0.5);
    assert(out < in * 1.5);

    std::printf("opus_roundtrip: %zu bytes in, %zu samples out, rms %.0f -> %.0f\n", raw.size(), output.size(), in, out);
}

void checkRejectsGarbage(const std::string &path)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream);
    stream << "this is not an ogg stream, and never was";
    stream.close();

    int rate = 0;
    int channels = 0;

    assert(OggOpus::decode(path, &rate, &channels).empty());
    assert(OggOpus::decode(tempFile("meegram-no-such-voice-note.oga"), &rate, &channels).empty());
}

}  // namespace

int main()
{
    assert(OggOpus::isValidSampleRate(16000));
    assert(OggOpus::isValidSampleRate(48000));
    assert(!OggOpus::isValidSampleRate(44100));

    const auto path = tempFile("meegram-opus-roundtrip.oga");

    checkRoundTrip(path);
    checkRejectsGarbage(path);

    std::filesystem::remove(path);

    std::printf("opus_roundtrip: OK\n");

    return 0;
}
