#include "OggOpus.hpp"

#include <ogg/ogg.h>

#if __has_include(<opus/opus.h>)
#  include <opus/opus.h>
#else
#  include <opus.h>
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <ranges>

namespace {

// 20 ms frames. Opus allows 2.5 to 120, but 20 is what every Telegram client emits and
// it is the point where the per-packet overhead stops mattering.
constexpr int FrameMs = 20;

// Ogg granule positions are always counted at 48 kHz whatever the encoder was fed, so a
// 20 ms frame always advances by this much regardless of the input rate.
constexpr ogg_int64_t GranulesPerFrame = 48000 / (1000 / FrameMs);

// Mono voice at 20 kbps is what Telegram itself sends. Raising it makes the upload
// slower on a metered radio without making a phone microphone sound better.
constexpr int Bitrate = 20000;

// Not 10. Encoding has to keep up with the microphone in real time on a 1 GHz Cortex-A8
// while the UI is still drawing, and complexity 5 costs roughly half the CPU of 10 for a
// difference nobody hears on speech. Turn it up if a faster device ever runs this.
constexpr int Complexity = 5;

// Any value is legal for a file with a single stream; the spec only wants it random so
// that multiplexed streams do not collide. Fixed, so the same PCM encodes to the same
// bytes and the round-trip check can compare files.
constexpr long StreamSerial = 0x4d454547;  // "MEEG"

// Longest packet opus can produce (120 ms) times the widest thing we decode.
constexpr int MaxFrameSamples = 5760;

constexpr int MaxPacketBytes = 4000;

void putLE16(unsigned char *p, std::uint16_t v) noexcept
{
    p[0] = static_cast<unsigned char>(v & 0xff);
    p[1] = static_cast<unsigned char>(v >> 8);
}

void putLE32(unsigned char *p, std::uint32_t v) noexcept
{
    p[0] = static_cast<unsigned char>(v & 0xff);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xff);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xff);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xff);
}

std::uint16_t getLE16(const unsigned char *p) noexcept
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

bool OggOpus::isValidSampleRate(int rate) noexcept
{
    // find rather than ranges::contains: that one is C++23 and needs a newer libstdc++
    // than the cross toolchain is guaranteed to have.
    return std::ranges::find(SampleRates, rate) != std::ranges::end(SampleRates);
}

struct OggOpus::Writer::Private
{
    std::ofstream file;

    OpusEncoder *encoder{};

    ogg_stream_state stream{};
    bool streamInit{};

    int sampleRate{};
    int channels{};
    int frameSize{};  // samples per channel in one frame

    ogg_int64_t preSkip{};
    ogg_int64_t granulePos{};
    ogg_int64_t packetNo{};

    // Total samples per channel the caller actually handed over, which is what the final
    // granule position has to report so a decoder trims the padding we add at the end.
    ogg_int64_t inputSamples{};

    std::vector<std::int16_t> pending;

    bool ok{};
    bool closed{};

    // Pages only ever leave through here, so a short write is caught in one place.
    bool writePages(bool flush) noexcept
    {
        ogg_page page;

        while (flush ? ogg_stream_flush(&stream, &page) : ogg_stream_pageout(&stream, &page))
        {
            file.write(reinterpret_cast<const char *>(page.header), page.header_len);
            file.write(reinterpret_cast<const char *>(page.body), page.body_len);
        }

        return static_cast<bool>(file);
    }

    bool encodeFrame(const std::int16_t *samples, bool last) noexcept
    {
        unsigned char data[MaxPacketBytes];

        const auto bytes = opus_encode(encoder, samples, frameSize, data, MaxPacketBytes);
        if (bytes < 0)
            return false;

        granulePos += GranulesPerFrame;

        ogg_packet packet{};

        packet.packet = data;
        packet.bytes = bytes;
        packet.b_o_s = 0;
        packet.e_o_s = last ? 1 : 0;
        packet.packetno = ++packetNo;
        // The real sample count on the last packet, not the padded one: the difference is
        // how a decoder knows to drop the silence close() appended.
        packet.granulepos = last ? preSkip + inputSamples * 48000 / sampleRate : granulePos;

        if (ogg_stream_packetin(&stream, &packet) != 0)
            return false;

        return writePages(last);
    }
};

OggOpus::Writer::Writer(const std::string &path, int sampleRate, int channels)
    : d(std::make_unique<Private>())
{
    if (!isValidSampleRate(sampleRate) || channels < 1 || channels > 2)
        return;

    d->sampleRate = sampleRate;
    d->channels = channels;
    d->frameSize = sampleRate / (1000 / FrameMs);

    int error = OPUS_OK;
    d->encoder = opus_encoder_create(sampleRate, channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK || !d->encoder)
        return;

    opus_encoder_ctl(d->encoder, OPUS_SET_BITRATE(Bitrate));
    opus_encoder_ctl(d->encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(d->encoder, OPUS_SET_COMPLEXITY(Complexity));

    // The encoder's own algorithmic delay, which the header has to declare so a decoder
    // drops exactly that much and playback does not start with a click.
    opus_int32 lookahead = 0;
    opus_encoder_ctl(d->encoder, OPUS_GET_LOOKAHEAD(&lookahead));
    d->preSkip = static_cast<ogg_int64_t>(lookahead) * 48000 / sampleRate;
    d->granulePos = d->preSkip;

    d->file.open(path, std::ios::binary | std::ios::trunc);
    if (!d->file)
        return;

    if (ogg_stream_init(&d->stream, StreamSerial) != 0)
        return;

    d->streamInit = true;

    unsigned char head[19];

    std::memcpy(head, "OpusHead", 8);
    head[8] = 1;  // version
    head[9] = static_cast<unsigned char>(channels);
    putLE16(head + 10, static_cast<std::uint16_t>(d->preSkip));
    putLE32(head + 12, static_cast<std::uint32_t>(sampleRate));  // informational only
    putLE16(head + 16, 0);                                 // output gain
    head[18] = 0;                                          // channel mapping family

    ogg_packet packet{};

    packet.packet = head;
    packet.bytes = sizeof(head);
    packet.b_o_s = 1;
    packet.granulepos = 0;
    packet.packetno = 0;

    if (ogg_stream_packetin(&d->stream, &packet) != 0)
        return;

    // Flushed rather than paged out, because the spec puts the identification header
    // alone on the first page and the comment header on a page boundary of its own.
    if (!d->writePages(true))
        return;

    static constexpr char Vendor[] = "MeeGram";
    static constexpr std::uint32_t VendorLength = sizeof(Vendor) - 1;

    std::vector<unsigned char> tags(8 + 4 + VendorLength + 4);

    std::memcpy(tags.data(), "OpusTags", 8);
    putLE32(tags.data() + 8, VendorLength);
    std::memcpy(tags.data() + 12, Vendor, VendorLength);
    putLE32(tags.data() + 12 + VendorLength, 0);  // no user comments

    packet = {};
    packet.packet = tags.data();
    packet.bytes = static_cast<long>(tags.size());
    packet.granulepos = 0;
    packet.packetno = 1;

    if (ogg_stream_packetin(&d->stream, &packet) != 0)
        return;

    if (!d->writePages(true))
        return;

    d->packetNo = 1;
    d->ok = true;
}

OggOpus::Writer::~Writer()
{
    close();

    if (d->encoder)
        opus_encoder_destroy(d->encoder);

    if (d->streamInit)
        ogg_stream_clear(&d->stream);
}

bool OggOpus::Writer::isOpen() const noexcept
{
    return d->ok && !d->closed;
}

bool OggOpus::Writer::write(const std::int16_t *samples, int sampleCount) noexcept
{
    if (!isOpen() || !samples || sampleCount < 0)
        return false;

    d->inputSamples += sampleCount / d->channels;
    d->pending.insert(d->pending.end(), samples, samples + sampleCount);

    const auto frameSamples = static_cast<std::size_t>(d->frameSize) * d->channels;

    std::size_t offset = 0;
    for (; d->pending.size() - offset >= frameSamples; offset += frameSamples)
    {
        if (!d->encodeFrame(d->pending.data() + offset, false))
        {
            d->ok = false;
            return false;
        }
    }

    d->pending.erase(d->pending.begin(), d->pending.begin() + offset);

    return true;
}

bool OggOpus::Writer::close() noexcept
{
    if (!d->ok || d->closed)
        return false;

    d->closed = true;

    // Padded to a whole frame and encoded unconditionally, even when nothing is pending:
    // a stream with no audio packet at all has nowhere to put the end-of-stream flag, and
    // an unterminated ogg file is one TDLib rejects.
    d->pending.resize(static_cast<std::size_t>(d->frameSize) * d->channels, 0);

    const auto encoded = d->encodeFrame(d->pending.data(), true);

    d->pending.clear();
    d->file.close();

    return encoded;
}

std::vector<std::int16_t> OggOpus::decode(const std::string &path, int *sampleRate, int *channels) noexcept
{
    std::vector<std::int16_t> pcm;

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return pcm;

    ogg_sync_state sync;
    ogg_sync_init(&sync);

    ogg_stream_state stream{};
    bool streamInit = false;

    OpusDecoder *decoder = nullptr;
    int decodedChannels = 0;
    ogg_int64_t preSkip = 0;
    ogg_int64_t packetNo = 0;

    std::vector<std::int16_t> frame(static_cast<std::size_t>(MaxFrameSamples) * 2);

    for (bool eof = false; !eof;)
    {
        char *buffer = ogg_sync_buffer(&sync, 8192);
        if (!buffer)
            break;

        file.read(buffer, 8192);

        // gcount, not the stream state: a short final read sets eofbit but still returns
        // the bytes, and those bytes hold the last page.
        if (const auto read = file.gcount(); read <= 0)
            eof = true;
        else
            ogg_sync_wrote(&sync, static_cast<long>(read));

        ogg_page page;
        // -1 is a hole in the data, which is worth skipping past rather than giving up on:
        // the rest of the stream still decodes.
        while (ogg_sync_pageout(&sync, &page) > 0)
        {
            if (!streamInit)
            {
                if (!ogg_page_bos(&page))
                    continue;

                if (ogg_stream_init(&stream, ogg_page_serialno(&page)) != 0)
                    break;

                streamInit = true;
            }

            if (ogg_stream_pagein(&stream, &page) != 0)
                continue;

            ogg_packet packet;
            while (ogg_stream_packetout(&stream, &packet) > 0)
            {
                if (packetNo == 0)
                {
                    // 19 bytes of OpusHead, and only mapping family 0 - anything else is a
                    // multichannel stream no voice note ever is.
                    if (packet.bytes < 19 || std::memcmp(packet.packet, "OpusHead", 8) != 0 || packet.packet[18] != 0)
                    {
                        eof = true;
                        break;
                    }

                    decodedChannels = packet.packet[9];
                    preSkip = getLE16(packet.packet + 10);

                    if (decodedChannels < 1 || decodedChannels > 2)
                    {
                        eof = true;
                        break;
                    }

                    int error = OPUS_OK;
                    decoder = opus_decoder_create(48000, decodedChannels, &error);
                    if (error != OPUS_OK || !decoder)
                    {
                        eof = true;
                        break;
                    }
                }
                else if (packetNo > 1)  // 1 is OpusTags, which carries nothing we show
                {
                    const auto samples = opus_decode(decoder, packet.packet, static_cast<opus_int32>(packet.bytes), frame.data(), MaxFrameSamples, 0);
                    if (samples < 0)
                        break;

                    // The encoder's lead-in, dropped here rather than played as the click
                    // it would otherwise be.
                    const auto skip = static_cast<int>(std::min<ogg_int64_t>(preSkip, samples));
                    preSkip -= skip;

                    const auto begin = frame.begin() + static_cast<std::size_t>(skip) * decodedChannels;
                    const auto end = frame.begin() + static_cast<std::size_t>(samples) * decodedChannels;

                    pcm.insert(pcm.end(), begin, end);
                }

                ++packetNo;
            }
        }
    }

    if (decoder)
        opus_decoder_destroy(decoder);

    if (streamInit)
        ogg_stream_clear(&stream);

    ogg_sync_clear(&sync);

    // ponytail: no end trimming. The last granule position says how much of the final
    // frame is real, but the difference is under 20 ms of near-silence.
    if (sampleRate)
        *sampleRate = 48000;
    if (channels)
        *channels = decodedChannels;

    return pcm;
}
