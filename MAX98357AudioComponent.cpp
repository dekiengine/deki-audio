#include "MAX98357AudioComponent.h"
#include "DekiAudio.h"
#include <deki/PackageConfig.h>
#include <deki/LogSystem.h>
#include <string>

static MAX98357Audio* s_Driver = nullptr;

void MAX98357AudioComponent::Setup(SetupCallback onComplete)
{
    if (!s_Driver)
        s_Driver = new MAX98357Audio();

    Deki::PackageConfig cfg;
    cfg.packageId = "audio";
    cfg.enabled  = true;
    cfg.pins["BCLK"]  = bclkPin;
    cfg.pins["LRCLK"] = lrclkPin;
    cfg.pins["DOUT"]  = dinPin;
    cfg.settings["i2sPort"]       = std::to_string(i2sPort);
    cfg.settings["sampleRate"]    = std::to_string(sampleRate);
    cfg.settings["channels"]       = std::to_string(channels);
    cfg.settings["initialVolume"] = std::to_string(initialVolume);

    s_Driver->Configure(cfg);

    const bool success = s_Driver->Initialize();
    if (success)
    {
        DekiAudio::SetCurrent(s_Driver);
    }
    else
    {
        DEKI_LOG_ERROR("MAX98357AudioComponent: Failed to initialize (BCLK=%d LRCLK=%d DOUT=%d @ %d Hz): %s",
                       (int)bclkPin, (int)lrclkPin, (int)dinPin, (int)sampleRate,
                       s_Driver->GetLastError());
    }

    if (onComplete) onComplete(success);
}
