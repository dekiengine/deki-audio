// Package entry point for deki-audio.
#include "DekiAudioPackage.h"
#include <deki/interop/Plugin.h>
#include <deki/LogSystem.h>

extern void DekiAudioRegisterComponents();
extern int DekiAudioGetAutoComponentCount();
extern const Deki::ComponentMeta* DekiAudioGetAutoComponentMeta(int index);

namespace DekiAudio
{

#ifdef DEKI_EDITOR

static bool s_AudioRegistered = false;

// The exports below are C symbols at global scope; the package's own
// registration helpers and statics live in its namespace.
using namespace DekiAudio;

extern "C"
{
    DEKI_AUDIO_API int DekiAudioEnsureRegistered(void)
    {
        if (s_AudioRegistered)
        {
            return ::DekiAudioGetAutoComponentCount();
        }
        s_AudioRegistered = true;
        ::DekiAudioRegisterComponents();
        return ::DekiAudioGetAutoComponentCount();
    }

    DEKI_PLUGIN_API const char* DekiPluginGetName(void)
    {
        return "Deki Audio Package";
    }
    DEKI_PLUGIN_API const char* DekiPluginGetVersion(void)
    {
#ifdef DEKI_PACKAGE_VERSION
        return DEKI_PACKAGE_VERSION;
#else
        return "0.0.0-dev";
#endif
    }
    DEKI_PLUGIN_API int DekiPluginInit(void)
    {
        return 0;
    }
    DEKI_PLUGIN_API void DekiPluginShutdown(void)
    {
        s_AudioRegistered = false;
    }
    DEKI_PLUGIN_API int DekiPluginGetComponentCount(void)
    {
        return ::DekiAudioGetAutoComponentCount();
    }
    DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPluginGetComponentMeta(int index)
    {
        return ::DekiAudioGetAutoComponentMeta(index);
    }
    DEKI_PLUGIN_API void DekiPluginRegisterComponents(void)
    {
        DekiAudioEnsureRegistered();
    }

}  // extern "C"

#endif  // DEKI_EDITOR
}  // namespace DekiAudio
