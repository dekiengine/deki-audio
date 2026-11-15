#include "AudioOutput.h"
#include "Mixer.h"
#include <deki/LogSystem.h>

namespace DekiAudio
{

IDekiAudio* AudioOutput::s_Current = nullptr;

namespace
{
void StartCurrent()
{
    IDekiAudio* output = AudioOutput::GetCurrent();
    if (output && !output->IsRunning() && !output->Start())
    {
        DEKI_LOG_WARNING("Audio: the output %s did not start: %s", output->GetPackageName(), output->GetLastError());
    }
}
}  // namespace

void AudioOutput::SetCurrent(IDekiAudio* output)
{
    if (s_Current && s_Current != output)
    {
        s_Current->Stop();
    }
    s_Current = output;

    Mixer& mixer = Mixer::Instance();
    if (output)
    {
        mixer.SetOutputFormat(output->GetSampleRate(), output->GetChannels());
        mixer.SetStartHook(&StartCurrent);
    }
    else
    {
        mixer.SetOutputFormat(0, 0);
        mixer.SetStartHook(nullptr);
    }
}

IDekiAudio* AudioOutput::GetCurrent()
{
    return s_Current;
}

}  // namespace DekiAudio
