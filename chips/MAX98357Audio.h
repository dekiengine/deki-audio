#pragma once

#include "../IDekiAudio.h"
#include "../PumpWorker.h"
#include <deki/PackageConfig.h>
#include <string>

namespace DekiI2s
{
class IDekiI2S;
}

namespace DekiAudio
{

/// The MAX98357A class-D mono amp over I2S, as an audio output: once started
/// it mixes on its own thread and writes to the I2S channel, whose DMA paces
/// it. The channel is open only while the output runs, from the first sound
/// to Stop, so a game without sound keeps its DMA buffers' internal RAM.
///
/// The amp's gain is set by its GAIN pin, so loudness is the mixer's master
/// volume. Tie the amp's SD pin to VDD for left-channel mono; it is a hardware
/// strap the driver does not drive.
class MAX98357Audio : public IDekiAudio
{
public:
    MAX98357Audio() = default;
    ~MAX98357Audio() override;

    const char* GetPackageId() const override { return "audio"; }
    const char* GetPackageName() const override
    {
        return "MAX98357A Audio (I\xC2\xB2"
               "S)";
    }
    void Configure(const Deki::PackageConfig& config) override;
    /// Checks that the platform has I2S. The channel opens in Start().
    bool Initialize() override;
    void Shutdown() override;
    void Update(float) override {}
    Deki::PackageState GetState() const override { return m_State; }
    const char* GetLastError() const override { return m_LastError.c_str(); }

    int GetSampleRate() const override { return m_SampleRate; }
    int GetChannels() const override { return m_Channels; }
    bool Start() override;
    void Stop() override;
    bool IsRunning() const override { return m_Pump.IsRunning(); }
    bool IsHardwareConnected() const override { return m_State == Deki::PackageState::Running; }

    /// Writes interleaved frames to the channel, blocking until it took them.
    /// What the pump thread calls; public for tests.
    bool WriteFrames(const int16_t* samples, size_t frames);

private:
    bool OpenChannel();
    void CloseChannel();

    DekiI2s::IDekiI2S* m_I2S = nullptr;
    PumpWorker m_Pump;
    int m_Port = 0;
    int m_PinBCLK = -1;
    int m_PinLRCLK = -1;
    int m_PinDOUT = -1;
    int m_SampleRate = 22050;
    int m_Channels = 1;

    Deki::PackageState m_State = Deki::PackageState::Uninitialized;
    std::string m_LastError;
};

}  // namespace DekiAudio
