#pragma once

#include <deki/providers/IPackage.h>
#include <cstdint>
#include <cstddef>

namespace DekiAudio
{

/// Audio output. Streams 16-bit signed PCM samples to an amplifier on I2S
/// (such as the MAX98357A). Games call PlayPCM() with a sample buffer in
/// memory; the implementation blocks or buffers as it needs.
///
/// The sample rate is the buffer's own. Mono is the common case for a single
/// class-D amplifier board; stereo works where the hardware allows it.
class IDekiAudio : public Deki::IPackage
{
public:
    const char* GetPackageCategory() const override { return "audio"; }

    /// Plays a block of interleaved 16-bit signed PCM samples. `count` is per
    /// channel, so a 1-second 16 kHz mono clip has 16000. `sampleRate` (Hz)
    /// must match the hardware's configured rate, or the driver reconfigures
    /// if it can. Returns true when all samples were written.
    virtual bool PlayPCM(const int16_t* samples, size_t count, int sampleRate) = 0;

    virtual void Stop() = 0;
    virtual bool IsPlaying() const = 0;

    /// Software sample scale factor, 0..1. Hardware gain stays fixed.
    virtual void SetVolume(float volume) = 0;
    virtual float GetVolume() const = 0;

    virtual bool IsHardwareConnected() const = 0;
};

}  // namespace DekiAudio
