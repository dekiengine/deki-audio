#pragma once

#ifdef _WIN32
    #ifdef DEKI_AUDIO_EXPORTS
        #define DEKI_AUDIO_API __declspec(dllexport)
    #else
        #define DEKI_AUDIO_API __declspec(dllimport)
    #endif
#else
    #define DEKI_AUDIO_API __attribute__((visibility("default")))
#endif
