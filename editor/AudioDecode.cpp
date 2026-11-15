#ifdef DEKI_EDITOR

#include "AudioDecode.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace DekiAudio
{

bool DecodeOgg(const std::string& path, DecodedAudio& out, std::string& error);  // OggDecode.cpp

namespace
{
uint16_t U16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t U32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;
}  // namespace

bool DecodeWav(const std::vector<uint8_t>& file, DecodedAudio& out, std::string& error)
{
    const uint8_t* d = file.data();
    const size_t size = file.size();
    if (size < 12 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0)
    {
        error = "not a WAV file";
        return false;
    }

    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t rate = 0;
    uint16_t bits = 0;
    const uint8_t* data = nullptr;
    size_t dataBytes = 0;

    size_t at = 12;
    while (at + 8 <= size)
    {
        const uint8_t* chunk = d + at;
        const uint32_t chunkBytes = U32(chunk + 4);
        const size_t bodyAt = at + 8;
        const size_t available = size - bodyAt;
        const size_t body = chunkBytes < available ? chunkBytes : available;
        if (std::memcmp(chunk, "fmt ", 4) == 0 && body >= 16)
        {
            format = U16(chunk + 8);
            channels = U16(chunk + 10);
            rate = U32(chunk + 12);
            bits = U16(chunk + 22);
            if (format == kFormatExtensible && body >= 26)
            {
                // The sub-format GUID starts with the format tag.
                format = U16(chunk + 8 + 24);
            }
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            data = d + bodyAt;
            dataBytes = body;
        }
        // Chunks are padded to an even size.
        at = bodyAt + chunkBytes + (chunkBytes & 1);
    }

    if (!data || channels == 0 || rate == 0)
    {
        error = "the WAV file has no format or no samples";
        return false;
    }
    const bool isFloat = format == kFormatFloat && (bits == 32 || bits == 64);
    const bool isInt = format == kFormatPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    if (!isFloat && !isInt)
    {
        error = "the WAV file is compressed or " + std::to_string(bits) + "-bit; save it as 16-bit PCM, or as OGG";
        return false;
    }

    const size_t bytesPerSample = bits / 8;
    const size_t frameBytes = bytesPerSample * channels;
    const size_t frames = dataBytes / frameBytes;
    const int kept = channels > 2 ? 2 : channels;
    out.sampleRate = static_cast<int>(rate);
    out.channels = kept;
    out.samples.resize(frames * static_cast<size_t>(kept));

    for (size_t f = 0; f < frames; ++f)
    {
        for (int c = 0; c < kept; ++c)
        {
            const uint8_t* p = data + f * frameBytes + static_cast<size_t>(c) * bytesPerSample;
            float v = 0.0f;
            if (isFloat && bits == 32)
            {
                const uint32_t raw = U32(p);
                std::memcpy(&v, &raw, 4);
            }
            else if (isFloat)
            {
                const uint64_t raw = static_cast<uint64_t>(U32(p)) | (static_cast<uint64_t>(U32(p + 4)) << 32);
                double dv = 0.0;
                std::memcpy(&dv, &raw, 8);
                v = static_cast<float>(dv);
            }
            else if (bits == 8)
            {
                v = (static_cast<int>(p[0]) - 128) / 128.0f;
            }
            else if (bits == 16)
            {
                v = static_cast<int16_t>(U16(p)) / 32768.0f;
            }
            else if (bits == 24)
            {
                int32_t s = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
                if (s & 0x800000)
                {
                    s -= 0x1000000;
                }
                v = s / 8388608.0f;
            }
            else
            {
                v = static_cast<float>(static_cast<int32_t>(U32(p)) / 2147483648.0);
            }
            out.samples[f * static_cast<size_t>(kept) + static_cast<size_t>(c)] = std::clamp(v, -1.0f, 1.0f);
        }
    }
    return true;
}

bool DecodeAudioFile(const std::string& path, DecodedAudio& out, std::string& error)
{
    const std::string ext = Lower(std::filesystem::path(path).extension().string());
    if (ext == ".ogg")
    {
        return DecodeOgg(path, out, error);
    }
    if (ext == ".mp3")
    {
        error = "MP3 files cannot be read yet; save it as OGG or WAV";
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        error = "cannot open the file";
        return false;
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return DecodeWav(file, out, error);
}

}  // namespace DekiAudio

#endif  // DEKI_EDITOR
