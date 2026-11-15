#ifdef DEKI_EDITOR

#include "AudioCompile.h"
#include "AudioConvert.h"
#include "AudioDecode.h"
#include "../ImaAdpcm.h"

#include <deki-editor/build/PlatformConfig.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace DekiAudio
{

namespace
{
// Over this, audio held whole in a board's memory gets a warning.
constexpr size_t kBoardMemoryWarnBytes = 256 * 1024;

std::string Kilobytes(size_t bytes)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    return buf;
}
}  // namespace

AudioSourceInfo ReadAudioSourceInfo(const std::string& absolutePath)
{
    AudioSourceInfo info;
    DecodedAudio audio;
    if (!DecodeAudioFile(absolutePath, audio, info.error))
    {
        return info;
    }
    info.valid = true;
    info.sampleRate = audio.sampleRate;
    info.channels = audio.channels;
    info.seconds = audio.sampleRate > 0 ? static_cast<float>(audio.Frames()) / audio.sampleRate : 0.0f;
    return info;
}

std::vector<uint8_t> EncodeAudio(const std::vector<int16_t>& samples, const ResolvedAudio& resolved)
{
    AudioFileInfo info;
    info.codec = resolved.codec;
    info.channels = static_cast<uint8_t>(resolved.channels);
    info.sampleRate = static_cast<uint32_t>(resolved.sampleRate);
    info.frameCount = static_cast<uint32_t>(samples.size() / info.channels);
    info.flags = resolved.stream ? kAudioFlagStream : 0;
    info.loopStart = 0;
    info.loopEnd = info.frameCount;
    AudioBlockLayout(info.codec, info.channels, info.blockFrames, info.blockBytes);

    std::vector<uint8_t> file(kAudioHeaderBytes + info.DataBytes(), 0);
    WriteAudioHeader(info, file.data());
    uint8_t* data = file.data() + kAudioHeaderBytes;
    if (info.codec == AudioCodec::ImaAdpcm)
    {
        EncodeAdpcmBlocks(samples.data(), info.frameCount, info.channels, data);
    }
    else
    {
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const uint16_t v = static_cast<uint16_t>(samples[i]);
            data[i * 2] = static_cast<uint8_t>(v);
            data[i * 2 + 1] = static_cast<uint8_t>(v >> 8);
        }
    }
    return file;
}

bool CompileAudio(const std::string& absolutePath, const DekiEditor::PlatformConfig* platform,
                  const std::string& outPath, const std::string& name, std::string& why,
                  std::vector<std::string>& warnings)
{
    DecodedAudio audio;
    std::string error;
    if (!DecodeAudioFile(absolutePath, audio, error))
    {
        why = name + ": " + error;
        return false;
    }
    if (audio.Frames() == 0)
    {
        why = name + ": the file holds no sound";
        return false;
    }

    const float seconds = static_cast<float>(audio.Frames()) / audio.sampleRate;
    const ResolvedAudio resolved =
        ResolveAudio(ReadAudioSettings(absolutePath), platform, audio.sampleRate, audio.channels, seconds);
    ConvertChannels(audio, resolved.channels);
    Resample(audio, resolved.sampleRate);
    const std::vector<uint8_t> file = EncodeAudio(ToPcm16(audio), resolved);

    const bool board = platform && platform->Capabilities().count("hosted") == 0;
    if (board && !resolved.stream && file.size() > kBoardMemoryWarnBytes)
    {
        const std::string where = platform->displayName.empty() ? platform->id : platform->displayName;
        warnings.push_back(name + " takes " + Kilobytes(file.size()) + " of " + where +
                           "'s memory while it plays; set its Loading to Stream to play it from storage instead");
    }

    std::error_code ec;
    fs::create_directories(fs::path(outPath).parent_path(), ec);
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        why = name + ": cannot write " + outPath;
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out)
    {
        why = name + ": cannot write " + outPath;
        return false;
    }
    return true;
}

}  // namespace DekiAudio

#endif  // DEKI_EDITOR
