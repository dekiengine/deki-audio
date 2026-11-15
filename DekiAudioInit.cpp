#include "DekiAudioInit.h"
#include "Audio.h"
#include "Mixer.h"
#include <deki/Engine.h>

#include <cstdint>

namespace
{
constexpr size_t kNotRegistered = SIZE_MAX;
size_t s_UpdateId = kNotRegistered;
}  // namespace

void DekiAudioInitSystem()
{
    DekiAudio::RegisterAudioLoader();
    if (s_UpdateId != kNotRegistered)
    {
        return;
    }
    s_UpdateId = Deki::Engine::GetInstance().RegisterUpdate([](uint32_t /*deltaTimeMs*/)
                                                            { DekiAudio::Mixer::Instance().Update(); });
}

void DekiAudioShutdownSystem()
{
    if (s_UpdateId != kNotRegistered)
    {
        // Before the DLL unloads: the callback's code lives here.
        Deki::Engine::GetInstance().UnregisterUpdate(s_UpdateId);
        s_UpdateId = kNotRegistered;
    }
    DekiAudio::Mixer::Instance().Reset();
}
