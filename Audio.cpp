#include "Audio.h"
#include "AudioReader.h"
#include "Mixer.h"
#include <deki/assets/AssetManager.h>
#include <deki/LogSystem.h>
#include <memory>

namespace DekiAudio
{

float Audio::GetLength() const
{
    return m_Info.sampleRate ? static_cast<float>(m_Info.frameCount) / static_cast<float>(m_Info.sampleRate) : 0.0f;
}

Audio* Audio::LoadFromFile(const char* path)
{
    AudioReader reader;
    if (!path || !reader.Open(path))
    {
        DEKI_LOG_ERROR("Audio: cannot open %s", path ? path : "(no path)");
        return nullptr;
    }

    uint8_t header[kAudioHeaderBytes];
    std::unique_ptr<Audio> audio(new Audio());
    if (!reader.ReadAt(0, header, sizeof(header)) || !ParseAudioHeader(header, sizeof(header), audio->m_Info))
    {
        DEKI_LOG_ERROR("Audio: %s is not an audio file this version reads", path);
        return nullptr;
    }

    const size_t dataBytes = audio->m_Info.DataBytes();
    const long size = reader.Size();
    if (size < 0 || static_cast<size_t>(size) < kAudioHeaderBytes + dataBytes)
    {
        DEKI_LOG_ERROR("Audio: %s is shorter than its header says", path);
        return nullptr;
    }

    if (audio->m_Info.IsStreamed())
    {
        audio->m_Path = path;
        return audio.release();
    }

    if (dataBytes > 0)
    {
        if (!audio->m_Blocks.Allocate(dataBytes, Deki::Memory::External))
        {
            DEKI_LOG_ERROR("Audio: no memory for %s (%u bytes)", path, static_cast<unsigned>(dataBytes));
            return nullptr;
        }
        if (!reader.ReadAt(kAudioHeaderBytes, audio->m_Blocks.Data(), dataBytes))
        {
            DEKI_LOG_ERROR("Audio: cannot read %s", path);
            return nullptr;
        }
    }
    return audio.release();
}

Audio* Audio::LoadFromMemory(const uint8_t* data, size_t size)
{
    std::unique_ptr<Audio> audio(new Audio());
    if (!ParseAudioHeader(data, size, audio->m_Info))
    {
        return nullptr;
    }
    if (audio->m_Info.IsStreamed())
    {
        DEKI_LOG_ERROR("Audio: streamed audio cannot play from an asset pack");
        return nullptr;
    }
    const size_t dataBytes = audio->m_Info.DataBytes();
    if (size < kAudioHeaderBytes + dataBytes)
    {
        return nullptr;
    }
    if (dataBytes > 0)
    {
        if (!audio->m_Blocks.Allocate(dataBytes, Deki::Memory::External))
        {
            return nullptr;
        }
        const uint8_t* from = data + kAudioHeaderBytes;
        uint8_t* to = audio->m_Blocks.Data();
        for (size_t i = 0; i < dataBytes; ++i)
        {
            to[i] = from[i];
        }
    }
    return audio.release();
}

namespace
{
bool s_LoaderRegistered = false;
}  // namespace

void RegisterAudioLoader()
{
    if (s_LoaderRegistered)
    {
        return;
    }
    s_LoaderRegistered = true;

    auto loader = [](const char* path) -> void* { return Audio::LoadFromFile(path); };
    auto memLoader = [](const uint8_t* data, size_t size) -> void* { return Audio::LoadFromMemory(data, size); };
    // A voice may still be reading the audio: stop it first.
    auto unloader = [](void* asset)
    {
        auto* audio = static_cast<Audio*>(asset);
        Mixer::Instance().StopAllUsing(audio);
        delete audio;
    };
    Deki::AssetManager::RegisterLoader(Audio::kAssetTypeName, loader, unloader, memLoader);
}

}  // namespace DekiAudio
