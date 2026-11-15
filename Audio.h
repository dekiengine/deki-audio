#pragma once

#include "AudioFile.h"
#include <deki/providers/Buffer.h>
#include <cstdint>
#include <string>

namespace DekiAudio
{

/// An audio asset: music, a voice line or an effect, as the editor compiled it
/// for the target (a .daud file). An Audio Player plays it, and so does
/// DekiAudio::PlayOnce.
///
/// Short audio is loaded whole. Long audio, which the editor marks to stream,
/// keeps only its path here, and each play reads it from storage a block at a
/// time.
class Audio
{
public:
    static constexpr const char* kAssetTypeName = "Audio";

    const AudioFileInfo& GetInfo() const { return m_Info; }

    /// Length in seconds.
    float GetLength() const;

    bool IsStreamed() const { return m_Info.IsStreamed(); }

    /// The blocks, for audio loaded whole; nullptr for streamed audio.
    const uint8_t* GetBlocks() const { return m_Blocks ? m_Blocks.Data() : nullptr; }

    /// Where the file is, for streamed audio.
    const std::string& GetPath() const { return m_Path; }

    /// Reads a .daud file. Streamed audio keeps the path and reads only the
    /// header. Returns nullptr when the file is missing or damaged.
    static Audio* LoadFromFile(const char* path);

    /// Reads a .daud file held in memory, for asset packs. Streamed audio cannot
    /// come from a pack and is refused.
    static Audio* LoadFromMemory(const uint8_t* data, size_t size);

private:
    AudioFileInfo m_Info;
    Deki::Buffer<uint8_t> m_Blocks;
    std::string m_Path;
};

/// Registers the Audio loader with the AssetManager. Called by
/// DekiAudioInitSystem, so the firmware link keeps it.
void RegisterAudioLoader();

}  // namespace DekiAudio
