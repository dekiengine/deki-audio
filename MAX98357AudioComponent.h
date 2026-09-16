#pragma once

#include <cstdint>
#include <deki/SetupComponent.h>
#include <deki/reflection/Property.h>
#include "chips/MAX98357Audio.h"

namespace DekiAudio
{

/**
 * @brief Boot-scene component for the MAX98357A I2S class-D mono amplifier.
 *
 * Streams 16-bit signed PCM to an external I2S DAC/amp. Requires the
 * deki-esp32-integration package (registers the I2S backend factory) on
 * ESP32 targets.
 *
 * Wire MAX98357A GAIN pin to GND for +9 dB (loudest), SD pin to VDD for
 * left-channel mono mode. These are hardware straps — the component
 * doesn't drive them.
 */
DEKI_CATEGORY("Sensors")
DEKI_DISPLAY_NAME("MAX98357A Audio")
DEKI_DESCRIPTION("Plays sound through a MAX98357A I2S amplifier.")
DEKI_FORMER_NAME("MAX98357AudioComponent")
class MAX98357AudioComponent : public Deki::SetupComponent
{
public:

    DEKI_EXPORT
    DEKI_RANGE(0, 1)
    int32_t i2sPort = 0;

    DEKI_EXPORT
    DEKI_RANGE(0, 48)
    int32_t bclkPin = 39;

    DEKI_EXPORT
    DEKI_RANGE(0, 48)
    int32_t lrclkPin = 40;

    DEKI_EXPORT
    DEKI_RANGE(0, 48)
    int32_t dinPin = 5;

    DEKI_EXPORT
    DEKI_UNIT(Frequency)
    DEKI_RANGE(8000, 48000)
    int32_t sampleRate = 16000;

    DEKI_EXPORT
    DEKI_RANGE(1, 2)
    int32_t channels = 1;

    /** @brief Software playback volume, 0..100 percent. Hardware gain is fixed on the amp. */
    DEKI_EXPORT
    DEKI_RANGE(0, 100)
    int32_t initialVolume = 80;

    MAX98357AudioComponent() = default;
    virtual ~MAX98357AudioComponent() = default;

    void        Setup(SetupCallback onComplete) override;
    const char* GetSetupName() const override { return "MAX98357A Audio"; }
};

}  // namespace DekiAudio

