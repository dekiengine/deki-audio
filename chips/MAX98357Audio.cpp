#include "MAX98357Audio.h"
#include "DekiI2S.h"  // from deki-i2s
#include <deki/LogSystem.h>
#include <vector>

MAX98357Audio::~MAX98357Audio()
{
    Shutdown();
}

void MAX98357Audio::Configure(const Deki::PackageConfig& config)
{
    m_Port       = config.GetInt("i2sPort", 0);
    m_PinBCLK    = config.GetPin("BCLK", -1);
    m_PinLRCLK   = config.GetPin("LRCLK", -1);
    m_PinDOUT    = config.GetPin("DOUT", -1);
    m_SampleRate = config.GetInt("sampleRate", 16000);
    m_Channels   = config.GetInt("channels", 1);

    const int vol_pct = config.GetInt("initialVolume", 80);
    m_Volume = (vol_pct < 0) ? 0.0f : (vol_pct > 100 ? 1.0f : (float)vol_pct * 0.01f);
}

bool MAX98357Audio::Initialize()
{
    m_I2S = DekiI2S::Create();
    if (!m_I2S)
    {
        m_LastError = "MAX98357Audio: no I2S factory registered (platform integration missing)";
        m_State = Deki::PackageState::Error;
        return false;
    }

    Deki::PackageConfig cfg;
    cfg.packageId = "i2s";
    cfg.enabled  = true;
    cfg.pins["BCLK"]  = m_PinBCLK;
    cfg.pins["LRCLK"] = m_PinLRCLK;
    cfg.pins["DOUT"]  = m_PinDOUT;
    cfg.settings["i2sPort"]        = std::to_string(m_Port);
    cfg.settings["sampleRate"]     = std::to_string(m_SampleRate);
    cfg.settings["bits_per_sample"] = "16";
    cfg.settings["channels"]        = std::to_string(m_Channels);

    m_I2S->Configure(cfg);
    if (!m_I2S->Initialize())
    {
        m_LastError = std::string("MAX98357Audio: I2S init failed: ") + m_I2S->GetLastError();
        delete m_I2S;
        m_I2S = nullptr;
        m_State = Deki::PackageState::Error;
        return false;
    }

    if (!m_I2S->Start())
    {
        m_LastError = std::string("MAX98357Audio: I2S start failed: ") + m_I2S->GetLastError();
        m_State = Deki::PackageState::Error;
        return false;
    }

    m_State = Deki::PackageState::Running;
    return true;
}

void MAX98357Audio::Shutdown()
{
    if (m_I2S)
    {
        m_I2S->Stop();
        m_I2S->Shutdown();
        delete m_I2S;
        m_I2S = nullptr;
    }
    m_Playing = false;
    m_State = Deki::PackageState::Uninitialized;
}

void MAX98357Audio::SetVolume(float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    m_Volume = volume;
}

bool MAX98357Audio::PlayPCM(const int16_t* samples, size_t count, int sampleRate)
{
    if (!m_I2S || !samples || count == 0) return false;
    (void)sampleRate; // Rate is set at Configure time; resampling is the caller's job for now.

    std::vector<int16_t> scaled(count);
    const float vol = m_Volume;
    for (size_t i = 0; i < count; ++i)
    {
        int32_t s = (int32_t)((float)samples[i] * vol);
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        scaled[i] = (int16_t)s;
    }

    m_Playing = true;
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(scaled.data());
    size_t remaining = count * sizeof(int16_t);
    while (remaining > 0)
    {
        int w = m_I2S->Write(ptr, remaining, 1000);
        if (w <= 0)
        {
            m_Playing = false;
            return false;
        }
        ptr += w;
        remaining -= (size_t)w;
    }
    m_Playing = false;
    return true;
}

void MAX98357Audio::Stop()
{
    if (m_I2S) m_I2S->Stop();
    m_Playing = false;
}
