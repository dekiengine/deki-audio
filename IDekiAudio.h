#pragma once

#include <deki/providers/IPackage.h>

namespace DekiAudio
{

/// An audio output: a speaker, an amplifier or the computer's sound card. Once
/// started it pulls mixed samples from the Mixer (Mixer::Render), from its own
/// thread or a callback, until it is stopped.
///
/// Platform packages provide one (SDL3 on desktop and web, I2S on boards,
/// ALSA on the Anbernic handhelds, Tactility's speaker) and make it the
/// current one with AudioOutput::SetCurrent.
class IDekiAudio : public Deki::IPackage
{
public:
    const char* GetPackageCategory() const override { return "audio"; }

    /// The rate and channels (1 or 2) the hardware plays.
    virtual int GetSampleRate() const = 0;
    virtual int GetChannels() const = 0;

    /// Starts pulling samples. Returns false when the output cannot play.
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;

    virtual bool IsHardwareConnected() const = 0;

    /// True when the device has its own volume, as a phone does. The game's
    /// volume is then the device's: DekiAudio::SetMasterVolume moves it, and
    /// the mixer plays at full.
    virtual bool HasDeviceVolume() const { return false; }
    /// The device's volume, 0..1.
    virtual float GetDeviceVolume() const { return 1.0f; }
    virtual void SetDeviceVolume(float) {}
};

}  // namespace DekiAudio
