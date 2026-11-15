#pragma once
#ifdef DEKI_EDITOR
// Reads the audio files a project holds into samples the compile converts for
// each target. Editor only.

#include <cstdint>
#include <string>
#include <vector>

namespace DekiAudio
{

/// Samples from 1 to 2 channels, interleaved, from -1 to 1.
struct DecodedAudio
{
    std::vector<float> samples;
    int sampleRate = 0;
    int channels = 0;

    size_t Frames() const { return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0; }
};

/// Reads a .wav, .ogg or .mp3 file. Returns false, with why in `error`, when the file
/// cannot be read or holds audio this reader does not take. A file with more
/// than two channels keeps its first two.
bool DecodeAudioFile(const std::string& path, DecodedAudio& out, std::string& error);

/// Reads a WAV file held in memory: 8, 16, 24 or 32-bit integer samples, or
/// 32 or 64-bit float.
bool DecodeWav(const std::vector<uint8_t>& file, DecodedAudio& out, std::string& error);

/// Reads an MP3 file held in memory: MPEG-1, 2 or 2.5 Layer III, mono or
/// stereo. The decoder's own delay is cut, and the silence an encoder added
/// at each end when its LAME tag says how much.
bool DecodeMp3(const std::vector<uint8_t>& file, DecodedAudio& out, std::string& error);

}  // namespace DekiAudio

#endif  // DEKI_EDITOR
