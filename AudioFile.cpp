#include "AudioFile.h"

namespace DekiAudio
{

namespace
{
uint16_t ReadU16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t ReadU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void WriteU16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void WriteU32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}
}  // namespace

bool AudioFileInfo::IsStreamed() const
{
    return (flags & kAudioFlagStream) != 0;
}

void AudioBlockLayout(AudioCodec codec, uint8_t channels, uint16_t& blockFrames, uint16_t& blockBytes)
{
    if (codec == AudioCodec::ImaAdpcm)
    {
        blockFrames = kAdpcmBlockFrames;
        blockBytes = static_cast<uint16_t>(kAdpcmChannelBlockBytes * channels);
    }
    else
    {
        blockFrames = kPcmBlockFrames;
        blockBytes = static_cast<uint16_t>(kPcmBlockFrames * channels * 2);
    }
}

bool ParseAudioHeader(const uint8_t* data, size_t size, AudioFileInfo& out)
{
    if (!data || size < kAudioHeaderBytes)
    {
        return false;
    }
    if (data[0] != 'D' || data[1] != 'A' || data[2] != 'U' || data[3] != 'D')
    {
        return false;
    }
    if (ReadU16(data + 4) != kAudioFileVersion)
    {
        return false;
    }

    AudioFileInfo info;
    const uint8_t codec = data[6];
    if (codec > static_cast<uint8_t>(AudioCodec::ImaAdpcm))
    {
        return false;
    }
    info.codec = static_cast<AudioCodec>(codec);
    info.channels = data[7];
    info.sampleRate = ReadU32(data + 8);
    info.frameCount = ReadU32(data + 12);
    info.blockFrames = ReadU16(data + 16);
    info.blockBytes = ReadU16(data + 18);
    info.flags = data[20];
    info.loopStart = ReadU32(data + 24);
    info.loopEnd = ReadU32(data + 28);

    if (info.channels < 1 || info.channels > 2 || info.sampleRate < 1000 || info.sampleRate > 192000)
    {
        return false;
    }

    // The block layout is fixed by the codec, so a file whose numbers differ
    // is damaged, and reading it would run past its blocks.
    uint16_t blockFrames = 0;
    uint16_t blockBytes = 0;
    AudioBlockLayout(info.codec, info.channels, blockFrames, blockBytes);
    if (info.blockFrames != blockFrames || info.blockBytes != blockBytes)
    {
        return false;
    }
    if (info.loopStart > info.loopEnd || info.loopEnd > info.frameCount)
    {
        return false;
    }

    out = info;
    return true;
}

void WriteAudioHeader(const AudioFileInfo& info, uint8_t* out)
{
    for (size_t i = 0; i < kAudioHeaderBytes; ++i)
    {
        out[i] = 0;
    }
    out[0] = 'D';
    out[1] = 'A';
    out[2] = 'U';
    out[3] = 'D';
    WriteU16(out + 4, kAudioFileVersion);
    out[6] = static_cast<uint8_t>(info.codec);
    out[7] = info.channels;
    WriteU32(out + 8, info.sampleRate);
    WriteU32(out + 12, info.frameCount);
    WriteU16(out + 16, info.blockFrames);
    WriteU16(out + 18, info.blockBytes);
    out[20] = info.flags;
    WriteU32(out + 24, info.loopStart);
    WriteU32(out + 28, info.loopEnd);
}

}  // namespace DekiAudio
