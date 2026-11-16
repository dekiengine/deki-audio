#include "DekiAudio.h"

#include "AudioOutput.h"

namespace DekiAudio
{

AudioHandle PlayOnce(const Audio* audio, float volume, float pitch)
{
    PlayOptions options;
    options.volume = volume;
    options.pitch = pitch;
    return Mixer::Instance().Play(audio, options);
}

namespace
{

// The output whose device has its own volume, which is then the game's.
IDekiAudio* DeviceVolumeOutput()
{
    IDekiAudio* output = AudioOutput::GetCurrent();
    return output && output->HasDeviceVolume() ? output : nullptr;
}

}  // namespace

void SetMasterVolume(float volume)
{
    volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (IDekiAudio* output = DeviceVolumeOutput())
    {
        output->SetDeviceVolume(volume);
        return;
    }
    Mixer::Instance().SetMasterVolume(volume);
}

float GetMasterVolume()
{
    if (IDekiAudio* output = DeviceVolumeOutput())
    {
        return output->GetDeviceVolume();
    }
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
