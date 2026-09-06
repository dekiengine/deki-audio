#include "DekiAudio.h"

IDekiAudio* DekiAudio::s_Current = nullptr;

void DekiAudio::SetCurrent(IDekiAudio* audio)
{
    s_Current = audio;
}

IDekiAudio* DekiAudio::GetCurrent()
{
    return s_Current;
}
