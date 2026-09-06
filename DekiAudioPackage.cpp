/**
 * @file DekiAudioPackage.cpp
 * @brief Package entry point for deki-audio
 */
#include "DekiAudioPackage.h"
#include <deki/interop/Plugin.h>
#include <deki/LogSystem.h>

#ifdef DEKI_EDITOR

extern void DekiAudio_RegisterComponents();
extern int  DekiAudio_GetAutoComponentCount();
extern const Deki::ComponentMeta* DekiAudio_GetAutoComponentMeta(int index);

static bool s_AudioRegistered = false;

extern "C" {

DEKI_AUDIO_API int DekiAudio_EnsureRegistered(void)
{
    if (s_AudioRegistered)
        return DekiAudio_GetAutoComponentCount();
    s_AudioRegistered = true;
    DekiAudio_RegisterComponents();
    return DekiAudio_GetAutoComponentCount();
}

DEKI_PLUGIN_API const char* DekiPlugin_GetName(void)    { return "Deki Audio Package"; }
DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void)
{
#ifdef DEKI_PACKAGE_VERSION
    return DEKI_PACKAGE_VERSION;
#else
    return "0.0.0-dev";
#endif
}
DEKI_PLUGIN_API int  DekiPlugin_Init(void)             { DEKI_LOG_INFO("[deki-audio] DekiPlugin_Init"); return 0; }
DEKI_PLUGIN_API void DekiPlugin_Shutdown(void)         { s_AudioRegistered = false; }
DEKI_PLUGIN_API int  DekiPlugin_GetComponentCount(void){ return DekiAudio_GetAutoComponentCount(); }
DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPlugin_GetComponentMeta(int index)
{
    return DekiAudio_GetAutoComponentMeta(index);
}
DEKI_PLUGIN_API void DekiPlugin_RegisterComponents(void)
{
    int n = DekiAudio_EnsureRegistered();
    DEKI_LOG_INFO("[deki-audio] DekiPlugin_RegisterComponents -> %d component(s)", n);
}


} // extern "C"

#endif // DEKI_EDITOR
