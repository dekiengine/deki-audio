#pragma once
#ifdef DEKI_EDITOR
// An audio file's settings, kept in its sidecar (<audio.wav>.data) under
// "settings":
//
//   "audio": { "format": "automatic" | "pcm16" | "adpcm",
//              "sampleRate": 0 (Automatic) or Hz,
//              "channels": 0 (Automatic), 1 or 2,
//              "loading": "automatic" | "memory" | "stream",
//              "targets": { "<platformId>": { the same keys, each optional } } }
//
// A target's own value wins, then the audio's, then Automatic, which picks for
// the target (ResolveAudio). The compile, the editor's cache and the inspector
// all resolve through ResolveAudio, so they cannot disagree.

#include "../AudioFile.h"
#include <map>
#include <string>

namespace DekiEditor
{
struct PlatformConfig;
}

namespace DekiAudio
{

enum class AudioFormatSetting
{
    Automatic,
    Pcm16,
    Adpcm,
};

enum class AudioLoading
{
    Automatic,
    Memory,
    Stream,
};

/// One level of settings: the audio's own, or a target's. Automatic, 0 and
/// "unset" all mean the level above decides.
struct AudioLevelSettings
{
    AudioFormatSetting format = AudioFormatSetting::Automatic;
    int sampleRate = 0;
    int channels = 0;
    AudioLoading loading = AudioLoading::Automatic;
    bool Empty() const
    {
        return format == AudioFormatSetting::Automatic && sampleRate == 0 && channels == 0 &&
               loading == AudioLoading::Automatic;
    }
};

struct AudioSettings
{
    AudioLevelSettings base;
    std::map<std::string, AudioLevelSettings> targets;
};

/// What one target gets.
struct ResolvedAudio
{
    AudioCodec codec = AudioCodec::Pcm16;
    int sampleRate = 0;
    int channels = 1;
    bool stream = false;
};

/// Audio longer than this streams when its loading is Automatic.
constexpr float kAutomaticStreamSeconds = 10.0f;

/// The settings in `<absolutePath>.data`; defaults when it has none.
AudioSettings ReadAudioSettings(const std::string& absolutePath);

/// Writes `settings` into `<absolutePath>.data`, keeping everything else in
/// it. False when the file cannot be written.
bool WriteAudioSettings(const std::string& absolutePath, const AudioSettings& settings);

/// What `platform` (nullptr: the editor with no platform) gets of an audio
/// with these settings, recorded at `sourceRate` with `sourceChannels`, lasting
/// `seconds`.
///
/// Automatic: a computer or phone (a "hosted" platform) gets 16-bit PCM at the
/// source's rate up to 48 kHz, in the source's channels. A board gets IMA
/// ADPCM at its speaker's rate and channels (the platform's audioSampleRate
/// and audioChannels options, 22050 and 1 when it has none), never above the
/// source's. Long audio streams.
ResolvedAudio ResolveAudio(const AudioSettings& settings, const DekiEditor::PlatformConfig* platform, int sourceRate,
                           int sourceChannels, float seconds);

/// The bytes a compiled audio takes, header included.
size_t CompiledAudioBytes(const ResolvedAudio& resolved, float seconds);

const char* AudioFormatName(AudioFormatSetting format);
const char* AudioLoadingName(AudioLoading loading);

}  // namespace DekiAudio

#endif  // DEKI_EDITOR
