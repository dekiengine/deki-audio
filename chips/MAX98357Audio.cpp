#include "MAX98357Audio.h"

// The amp needs deki-i2s, which the max98357 feature brings. Without it (a
// desktop project) the driver reports that it has no channel.
#if __has_include("DekiI2S.h")
#define DEKI_AUDIO_HAS_I2S 1
#include "DekiI2S.h"   // from deki-i2s
#include "IDekiI2S.h"  // from deki-i2s
#endif

#include <deki/LogSystem.h>

namespace DekiAudio
{

namespace
{
// Frames mixed per write: 240 is one of the I2S driver's DMA buffers, about
// 11 ms at 22 kHz.
constexpr size_t kChunkFrames = 240;
}  // namespace

MAX98357Audio::~MAX98357Audio()
{
    Shutdown();
}

void MAX98357Audio::Configure(const Deki::PackageConfig& config)
{
    m_Port = config.GetInt("i2sPort", 0);
    m_PinBCLK = config.GetPin("BCLK", -1);
    m_PinLRCLK = config.GetPin("LRCLK", -1);
    m_PinDOUT = config.GetPin("DOUT", -1);
    m_SampleRate = config.GetInt("sampleRate", 22050);
    m_Channels = config.GetInt("channels", 1) == 2 ? 2 : 1;
}

#if DEKI_AUDIO_HAS_I2S

bool MAX98357Audio::Initialize()
{
    // Closes any open channel, so a second Initialize does not leak it.
    Shutdown();

    m_I2S = DekiI2s::DekiI2S::Create();
    if (!m_I2S)
    {
        m_LastError = "MAX98357Audio: no I2S factory registered (platform integration missing)";
        m_State = Deki::PackageState::Error;
        return false;
    }

    Deki::PackageConfig cfg;
    cfg.packageId = "i2s";
    cfg.enabled = true;
    cfg.pins["BCLK"] = m_PinBCLK;
    cfg.pins["LRCLK"] = m_PinLRCLK;
    cfg.pins["DOUT"] = m_PinDOUT;
    cfg.settings["i2sPort"] = std::to_string(m_Port);
    cfg.settings["sampleRate"] = std::to_string(m_SampleRate);
    cfg.settings["bits_per_sample"] = "16";
    cfg.settings["channels"] = std::to_string(m_Channels);

    m_I2S->Configure(cfg);
    if (!m_I2S->Initialize())
    {
        m_LastError = std::string("MAX98357Audio: I2S init failed: ") + m_I2S->GetLastError();
        delete m_I2S;
        m_I2S = nullptr;
        m_State = Deki::PackageState::Error;
        return false;
    }

    m_State = Deki::PackageState::Running;
    return true;
}

void MAX98357Audio::Shutdown()
{
    Stop();
    if (m_I2S)
    {
        m_I2S->Shutdown();
        delete m_I2S;
        m_I2S = nullptr;
    }
    m_State = Deki::PackageState::Uninitialized;
}

bool MAX98357Audio::Start()
{
    if (!m_I2S)
    {
        m_LastError = "MAX98357Audio: no I2S channel";
        return false;
    }
    if (m_Pump.IsRunning())
    {
        return true;
    }
    if (!m_I2S->Start())
    {
        m_LastError = std::string("MAX98357Audio: I2S start failed: ") + m_I2S->GetLastError();
        return false;
    }
    auto write = [](void* self, const int16_t* samples, size_t frames)
    { return static_cast<MAX98357Audio*>(self)->WriteFrames(samples, frames); };
    if (!m_Pump.Start("deki_audio", m_Channels, kChunkFrames, write, this))
    {
        m_I2S->Stop();
        m_LastError = "MAX98357Audio: no memory or thread for the audio";
        return false;
    }
    return true;
}

void MAX98357Audio::Stop()
{
    const bool wasRunning = m_Pump.IsRunning();
    m_Pump.Stop();
    if (wasRunning && m_I2S)
    {
        m_I2S->Stop();
    }
}

bool MAX98357Audio::WriteFrames(const int16_t* samples, size_t frames)
{
    if (!m_I2S)
    {
        return false;
    }
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(samples);
    size_t remaining = frames * static_cast<size_t>(m_Channels) * sizeof(int16_t);
    while (remaining > 0)
    {
        const int written = m_I2S->Write(ptr, remaining, 100);
        if (written <= 0)
        {
            return false;
        }
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
}

#else

bool MAX98357Audio::Initialize()
{
    m_LastError = "MAX98357Audio: the project has no deki-i2s";
    m_State = Deki::PackageState::Error;
    return false;
}

void MAX98357Audio::Shutdown()
{
    m_State = Deki::PackageState::Uninitialized;
}

bool MAX98357Audio::Start()
{
    return false;
}

void MAX98357Audio::Stop()
{
}

bool MAX98357Audio::WriteFrames(const int16_t*, size_t)
{
    return false;
}

#endif

}  // namespace DekiAudio
