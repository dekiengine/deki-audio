#pragma once

#include "../IDekiAudio.h"
#include <deki/PackageConfig.h>
#include "IDekiI2S.h"  // from deki-i2s
#include <string>

/**
 * @brief MAX98357A class-D mono amp driver over I2S.
 *
 * The MAX98357A has no digital volume control — hardware gain is fixed
 * via its GAIN pin resistor. Volume is applied in software by scaling
 * samples before handing them to the I2S peripheral.
 *
 * SD pin on the amp should be tied to VDD for left-channel mono mode
 * (the component doesn't drive it — it's a hardware strap).
 */
class MAX98357Audio : public IDekiAudio
{
public:
    MAX98357Audio() = default;
    ~MAX98357Audio() override;

    const char* GetPackageId() const override   { return "audio"; }
    const char* GetPackageName() const override { return "MAX98357A Audio (I\xC2\xB2""S)"; }
    void        Configure(const Deki::PackageConfig& config) override;
    bool        Initialize() override;
    void        Shutdown() override;
    void        Update(float) override {}
    Deki::PackageState GetState() const override      { return m_State; }
    const char* GetLastError() const override  { return m_LastError.c_str(); }

    bool  PlayPCM(const int16_t* samples, size_t count, int sampleRate) override;
    void  Stop() override;
    bool  IsPlaying() const override { return m_Playing; }
    void  SetVolume(float volume) override;
    float GetVolume() const override { return m_Volume; }
    bool  IsHardwareConnected() const override { return m_I2S != nullptr; }

private:
    IDekiI2S*   m_I2S = nullptr;
    int         m_Port       = 0;
    int         m_PinBCLK    = -1;
    int         m_PinLRCLK   = -1;
    int         m_PinDOUT    = -1;
    int         m_SampleRate = 16000;
    int         m_Channels   = 1;
    float       m_Volume     = 0.8f;
    bool        m_Playing    = false;

    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    std::string m_LastError;
};
