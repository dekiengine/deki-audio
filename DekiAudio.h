#pragma once

#include "IDekiAudio.h"

namespace DekiAudio
{

/// Holds the active audio driver. A chip's SetupComponent (such as
/// MAX98357AudioComponent) calls SetCurrent() in its Setup() once the driver
/// is configured and running; game code gets it from GetCurrent().
class DekiAudio
{
public:
    static void SetCurrent(IDekiAudio* audio);
    static IDekiAudio* GetCurrent();

private:
    static IDekiAudio* s_Current;
};

}  // namespace DekiAudio
