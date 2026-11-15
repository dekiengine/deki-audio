#ifdef DEKI_EDITOR

#include "AudioSettings.h"

#include <deki-editor/build/PlatformConfig.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace DekiAudio
{

namespace
{

nlohmann::json ReadSidecar(const std::string& absolutePath)
{
    std::ifstream in(absolutePath + ".data");
    if (!in.is_open())
    {
        return nlohmann::json::object();
    }
    try
    {
        nlohmann::json data = nlohmann::json::parse(in);
        return data.is_object() ? data : nlohmann::json::object();
    }
    catch (const nlohmann::json::exception&)
    {
        return nlohmann::json::object();
    }
}

AudioLevelSettings ReadLevel(const nlohmann::json& j)
{
    AudioLevelSettings out;
    if (!j.is_object())
    {
        return out;
    }
    const std::string format = j.value("format", std::string());
    out.format = format == "pcm16" ? AudioFormatSetting::Pcm16
                                   : (format == "adpcm" ? AudioFormatSetting::Adpcm : AudioFormatSetting::Automatic);
    if (j.contains("sampleRate") && j["sampleRate"].is_number_integer())
    {
        out.sampleRate = std::clamp(j["sampleRate"].get<int>(), 0, 192000);
    }
    if (j.contains("channels") && j["channels"].is_number_integer())
    {
        const int c = j["channels"].get<int>();
        out.channels = (c == 1 || c == 2) ? c : 0;
    }
    const std::string loading = j.value("loading", std::string());
    out.loading = loading == "memory" ? AudioLoading::Memory
                                      : (loading == "stream" ? AudioLoading::Stream : AudioLoading::Automatic);
    return out;
}

nlohmann::json WriteLevel(const AudioLevelSettings& level)
{
    nlohmann::json j = nlohmann::json::object();
    if (level.format != AudioFormatSetting::Automatic)
    {
        j["format"] = level.format == AudioFormatSetting::Pcm16 ? "pcm16" : "adpcm";
    }
    if (level.sampleRate > 0)
    {
        j["sampleRate"] = level.sampleRate;
    }
    if (level.channels > 0)
    {
        j["channels"] = level.channels;
    }
    if (level.loading != AudioLoading::Automatic)
    {
        j["loading"] = level.loading == AudioLoading::Memory ? "memory" : "stream";
    }
    return j;
}

}  // namespace

AudioSettings ReadAudioSettings(const std::string& absolutePath)
{
    AudioSettings out;
    const nlohmann::json data = ReadSidecar(absolutePath);
    const nlohmann::json settings = data.value("settings", nlohmann::json::object());
    if (!settings.is_object())
    {
        return out;
    }
    const nlohmann::json audio = settings.value("audio", nlohmann::json::object());
    out.base = ReadLevel(audio);
    const nlohmann::json targets =
        audio.is_object() ? audio.value("targets", nlohmann::json::object()) : nlohmann::json::object();
    for (auto it = targets.begin(); targets.is_object() && it != targets.end(); ++it)
    {
        const AudioLevelSettings level = ReadLevel(it.value());
        if (!level.Empty())
        {
            out.targets[it.key()] = level;
        }
    }
    return out;
}

bool WriteAudioSettings(const std::string& absolutePath, const AudioSettings& settings)
{
    nlohmann::json data = ReadSidecar(absolutePath);
    if (!data.contains("settings") || !data["settings"].is_object())
    {
        data["settings"] = nlohmann::json::object();
    }
    nlohmann::json audio = WriteLevel(settings.base);
    nlohmann::json targets = nlohmann::json::object();
    for (const auto& [id, level] : settings.targets)
    {
        if (!level.Empty())
        {
            targets[id] = WriteLevel(level);
        }
    }
    if (!targets.empty())
    {
        audio["targets"] = targets;
    }
    data["settings"]["audio"] = audio;

    std::ofstream out(absolutePath + ".data", std::ios::trunc);
    if (!out)
    {
        return false;
    }
    out << data.dump(2);
    return static_cast<bool>(out);
}

ResolvedAudio ResolveAudio(const AudioSettings& settings, const DekiEditor::PlatformConfig* platform, int sourceRate,
                           int sourceChannels, float seconds)
{
    AudioLevelSettings level = settings.base;
    if (platform)
    {
        if (auto it = settings.targets.find(platform->id); it != settings.targets.end())
        {
            const AudioLevelSettings& t = it->second;
            if (t.format != AudioFormatSetting::Automatic)
            {
                level.format = t.format;
            }
            if (t.sampleRate > 0)
            {
                level.sampleRate = t.sampleRate;
            }
            if (t.channels > 0)
            {
                level.channels = t.channels;
            }
            if (t.loading != AudioLoading::Automatic)
            {
                level.loading = t.loading;
            }
        }
    }

    const bool hosted = !platform || platform->Capabilities().count("hosted") != 0;
    const int source = sourceRate > 0 ? sourceRate : 22050;
    const int sourceCh = sourceChannels == 2 ? 2 : 1;

    ResolvedAudio out;
    if (level.format == AudioFormatSetting::Automatic)
    {
        out.codec = hosted ? AudioCodec::Pcm16 : AudioCodec::ImaAdpcm;
    }
    else
    {
        out.codec = level.format == AudioFormatSetting::Pcm16 ? AudioCodec::Pcm16 : AudioCodec::ImaAdpcm;
    }

    if (level.sampleRate > 0)
    {
        out.sampleRate = level.sampleRate;
    }
    else
    {
        const int speaker = hosted ? 48000 : static_cast<int>(platform->OptionU32("audioSampleRate", 22050));
        out.sampleRate = std::min(source, speaker);
    }
    out.sampleRate = std::clamp(out.sampleRate, 4000, 96000);

    if (level.channels > 0)
    {
        out.channels = level.channels;
    }
    else
    {
        const int speaker = hosted ? 2 : static_cast<int>(platform->OptionU32("audioChannels", 1));
        out.channels = std::min(sourceCh, speaker == 2 ? 2 : 1);
    }

    out.stream = level.loading == AudioLoading::Stream ||
                 (level.loading == AudioLoading::Automatic && seconds > kAutomaticStreamSeconds);
    return out;
}

size_t CompiledAudioBytes(const ResolvedAudio& resolved, float seconds)
{
    uint16_t blockFrames = 0;
    uint16_t blockBytes = 0;
    AudioBlockLayout(resolved.codec, static_cast<uint8_t>(resolved.channels), blockFrames, blockBytes);
    const double frames = static_cast<double>(seconds) * resolved.sampleRate;
    const size_t blocks = static_cast<size_t>((frames + blockFrames - 1) / blockFrames);
    return kAudioHeaderBytes + blocks * blockBytes;
}

const char* AudioFormatName(AudioFormatSetting format)
{
    switch (format)
    {
        case AudioFormatSetting::Pcm16: return "PCM 16-bit";
        case AudioFormatSetting::Adpcm: return "ADPCM";
        default: return "Automatic";
    }
}

const char* AudioLoadingName(AudioLoading loading)
{
    switch (loading)
    {
        case AudioLoading::Memory: return "In Memory";
        case AudioLoading::Stream: return "Stream";
        default: return "Automatic";
    }
}

}  // namespace DekiAudio

#endif  // DEKI_EDITOR
