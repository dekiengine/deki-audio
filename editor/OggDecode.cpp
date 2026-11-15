#ifdef DEKI_EDITOR

// OGG Vorbis through stb_vorbis, which the editor already has with the rest of
// stb (its include path comes with deki-editor).

#include "AudioDecode.h"

#include <cstdlib>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO_WRITE
// NOLINTBEGIN: stb's own code and names
#include "stb_vorbis.c"
// NOLINTEND

namespace DekiAudio
{

bool DecodeOgg(const std::string& path, DecodedAudio& out, std::string& error)
{
    int channels = 0;
    int rate = 0;
    short* samples = nullptr;
    const int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &rate, &samples);
    if (frames < 0 || !samples || channels <= 0 || rate <= 0)
    {
        std::free(samples);
        error = "not an OGG Vorbis file, or a damaged one";
        return false;
    }
    const int kept = channels > 2 ? 2 : channels;
    out.sampleRate = rate;
    out.channels = kept;
    out.samples.resize(static_cast<size_t>(frames) * static_cast<size_t>(kept));
    for (int f = 0; f < frames; ++f)
    {
        for (int c = 0; c < kept; ++c)
        {
            out.samples[static_cast<size_t>(f) * kept + c] = samples[static_cast<size_t>(f) * channels + c] / 32768.0f;
        }
    }
    // stb_vorbis allocates with malloc.
    std::free(samples);
    return true;
}

}  // namespace DekiAudio

#endif  // DEKI_EDITOR
