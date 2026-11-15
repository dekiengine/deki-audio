#pragma once

#include "IDekiAudio.h"

namespace DekiAudio
{

/// Holds the output the mixer plays through. A platform's setup component
/// makes its output current once it is ready.
class AudioOutput
{
public:
    /// Makes `output` the one the mixer plays through, or none with nullptr.
    /// The previous one is stopped. The output starts with the first sound
    /// that plays, so a game that plays none leaves it idle.
    static void SetCurrent(IDekiAudio* output);
    static IDekiAudio* GetCurrent();

private:
    static IDekiAudio* s_Current;
};

}  // namespace DekiAudio
