#pragma once
#ifdef DEKI_EDITOR
// Compiles a .wav or .ogg file into the .daud a target plays, for the editor's
// cache and for each build. Editor only.

#include "AudioSettings.h"
#include <string>
#include <vector>

namespace DekiEditor
{
struct PlatformConfig;
}

namespace DekiAudio
{

/// What a source file holds, as read before converting.
struct AudioSourceInfo
{
    bool valid = false;
    int sampleRate = 0;
    int channels = 0;
    float seconds = 0.0f;
    std::string error;
};

/// Reads the source's rate, channels and length (it decodes the file).
AudioSourceInfo ReadAudioSourceInfo(const std::string& absolutePath);

/// Compiles `absolutePath` for `platform` (nullptr: the editor with no
/// platform) into `outPath`. Returns false, with why in `why`, when the file
/// cannot be read or written. `warnings` gets what the target may struggle
/// with, such as a large audio held in a board's memory.
bool CompileAudio(const std::string& absolutePath, const DekiEditor::PlatformConfig* platform,
                  const std::string& outPath, const std::string& name, std::string& why,
                  std::vector<std::string>& warnings);

/// Writes a .daud from 16-bit samples, resolved as `resolved`. Returns the
/// file's bytes.
std::vector<uint8_t> EncodeAudio(const std::vector<int16_t>& samples, const ResolvedAudio& resolved);

}  // namespace DekiAudio

#endif  // DEKI_EDITOR
