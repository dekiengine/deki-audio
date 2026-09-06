#pragma once

#include <deki/providers/IPackage.h>
#include <cstdint>
#include <cstddef>

/**
 * @brief Audio output interface.
 *
 * Streams 16-bit signed PCM samples to an I2S-connected amplifier (e.g.,
 * MAX98357A). Games call PlayPCM() with an in-memory sample buffer; the
 * implementation blocks or buffers as needed.
 *
 * Sample rate is the buffer's native rate. Mono is the common case for a
 * single class-D amplifier board; stereo is supported where the hardware
 * allows it.
 */
class IDekiAudio : public Deki::IPackage
{
public:
    const char* GetPackageCategory() const override { return "audio"; }

    /**
     * @brief Play a block of interleaved 16-bit signed PCM samples.
     * @param samples    Pointer to samples.
     * @param count      Number of samples (per channel count, so for mono a
     *                   1-second 16 kHz clip has 16000).
     * @param sampleRate Sample rate in Hz. Must match the hardware's
     *                   configured rate, or the driver reconfigures if it can.
     * @return true if all samples were written.
     */
    virtual bool PlayPCM(const int16_t* samples, size_t count, int sampleRate) = 0;

    virtual void Stop() = 0;
    virtual bool IsPlaying() const = 0;

    /** @brief Software sample scale factor, 0..1. Hardware gain stays fixed. */
    virtual void  SetVolume(float volume) = 0;
    virtual float GetVolume() const = 0;

    virtual bool IsHardwareConnected() const = 0;
};
