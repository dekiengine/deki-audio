#include "MAX98357AudioComponent.h"
#include "AudioOutput.h"
#include "Mixer.h"
#include <deki/PackageConfig.h>
#include <deki/LogSystem.h>
#include <string>

namespace DekiAudio
{

static MAX98357Audio* s_Driver = nullptr;

void MAX98357AudioComponent::Setup(SetupCallback onComplete)
{
    if (!s_Driver)
    {
        s_Driver = new MAX98357Audio();
    }

    Deki::PackageConfig cfg;
    cfg.packageId = "audio";
    cfg.enabled = true;
    cfg.pins["BCLK"] = bclkPin;
    cfg.pins["LRCLK"] = lrclkPin;
    cfg.pins["DOUT"] = dinPin;
    cfg.settings["i2sPort"] = std::to_string(i2sPort);
    cfg.settings["sampleRate"] = std::to_string(sampleRate);
    cfg.settings["channels"] = std::to_string(channels);
    s_Driver->Configure(cfg);

    const int volume = initialVolume < 0 ? 0 : (initialVolume > 100 ? 100 : static_cast<int>(initialVolume));
    Mixer::Instance().SetMasterVolume(static_cast<float>(volume) * 0.01f);

    if (s_Driver->Initialize())
    {
        AudioOutput::SetCurrent(s_Driver);
    }
    else
    {
        DEKI_LOG_ERROR("MAX98357AudioComponent: no sound (BCLK=%d LRCLK=%d DOUT=%d @ %d Hz): %s",
                       static_cast<int>(bclkPin), static_cast<int>(lrclkPin), static_cast<int>(dinPin),
                       static_cast<int>(sampleRate), s_Driver->GetLastError());
    }

    // A game without sound still starts.
    if (onComplete)
    {
        onComplete(true);
    }
}

}  // namespace DekiAudio
