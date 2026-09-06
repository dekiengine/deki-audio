#pragma once

#include "IDekiAudio.h"

/**
 * @brief Published-instance registry for the currently active audio driver.
 *
 * Chip-specific SetupComponents (e.g., MAX98357AudioComponent) call
 * SetCurrent() in their Setup() once the driver is configured and
 * initialized. Game code reads the current audio output via GetCurrent().
 */
class DekiAudio
{
public:
    static void        SetCurrent(IDekiAudio* audio);
    static IDekiAudio* GetCurrent();

private:
    static IDekiAudio* s_Current;
};
