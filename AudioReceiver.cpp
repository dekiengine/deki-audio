#include "AudioReceiver.h"

namespace DekiAudio
{

AudioReceiver* AudioReceiver::s_Current = nullptr;

AudioReceiver::AudioReceiver()
    : m_Previous(s_Current)
{
    s_Current = this;
}

AudioReceiver::~AudioReceiver()
{
    // Gives the place back to the one before, or unlinks this one from the
    // middle of the chain.
    if (s_Current == this)
    {
        s_Current = m_Previous;
        return;
    }
    for (AudioReceiver* r = s_Current; r; r = r->m_Previous)
    {
        if (r->m_Previous == this)
        {
            r->m_Previous = m_Previous;
            return;
        }
    }
}

AudioReceiver* AudioReceiver::GetCurrent()
{
    return s_Current;
}

}  // namespace DekiAudio
