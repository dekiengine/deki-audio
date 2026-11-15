#include "DekiAudio.h"

namespace DekiAudio
{

AudioHandle PlayOnce(const Audio* audio, float volume, float pitch)
{
    PlayOptions options;
    options.volume = volume;
    options.pitch = pitch;
    return Mixer::Instance().Play(audio, options);
}

void SetMasterVolume(float volume)
{
    Mixer::Instance().SetMasterVolume(volume);
}

float GetMasterVolume()
{
    return Mixer::Instance().GetMasterVolume();
}

void StopAll()
{
    Mixer::Instance().StopAll();
}

void SetAllPaused(bool paused)
{
    Mixer::Instance().SetAllPaused(paused);
}

}  // namespace DekiAudio
