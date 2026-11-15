#ifdef DEKI_EDITOR

#include "AudioConvert.h"

#include <algorithm>
#include <cmath>

namespace DekiAudio
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
// Zero crossings of the sinc on each side: more is sharper and slower.
constexpr int kZeroCrossings = 16;

double Sinc(double x)
{
    return x == 0.0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
}

// Blackman window over -1..1.
double Window(double x)
{
    if (x <= -1.0 || x >= 1.0)
    {
        return 0.0;
    }
    const double t = (x + 1.0) * 0.5;
    return 0.42 - 0.5 * std::cos(2.0 * kPi * t) + 0.08 * std::cos(4.0 * kPi * t);
}
}  // namespace

void ConvertChannels(DecodedAudio& audio, int channels)
{
    if (channels == audio.channels || audio.channels <= 0)
    {
        return;
    }
    const size_t frames = audio.Frames();
    std::vector<float> out(frames * static_cast<size_t>(channels));
    for (size_t f = 0; f < frames; ++f)
    {
        if (channels == 1)
        {
            out[f] = 0.5f * (audio.samples[f * 2] + audio.samples[f * 2 + 1]);
        }
        else
        {
            out[f * 2] = out[f * 2 + 1] = audio.samples[f];
        }
    }
    audio.samples.swap(out);
    audio.channels = channels;
}

void Resample(DecodedAudio& audio, int sampleRate)
{
    if (sampleRate <= 0 || sampleRate == audio.sampleRate || audio.sampleRate <= 0 || audio.channels <= 0)
    {
        return;
    }
    const size_t channels = static_cast<size_t>(audio.channels);
    const size_t inFrames = audio.Frames();
    const double ratio = static_cast<double>(sampleRate) / audio.sampleRate;
    const size_t outFrames = static_cast<size_t>(std::llround(inFrames * ratio));
    // Below the lower rate's Nyquist, a little under it so the window's
    // slope stays out of the band.
    const double cutoff = std::min(1.0, ratio) * 0.97;
    const double halfWidth = kZeroCrossings / cutoff;

    std::vector<float> out(outFrames * channels, 0.0f);
    for (size_t n = 0; n < outFrames; ++n)
    {
        const double center = n / ratio;
        const long first = static_cast<long>(std::ceil(center - halfWidth));
        const long last = static_cast<long>(std::floor(center + halfWidth));
        double sum[2] = { 0.0, 0.0 };
        for (long j = std::max(first, 0L); j <= last && j < static_cast<long>(inFrames); ++j)
        {
            const double d = center - static_cast<double>(j);
            const double h = cutoff * Sinc(cutoff * d) * Window(d / halfWidth);
            for (size_t c = 0; c < channels; ++c)
            {
                sum[c] += h * audio.samples[static_cast<size_t>(j) * channels + c];
            }
        }
        for (size_t c = 0; c < channels; ++c)
        {
            out[n * channels + c] = static_cast<float>(sum[c]);
        }
    }
    audio.samples.swap(out);
    audio.sampleRate = sampleRate;
}

std::vector<int16_t> ToPcm16(const DecodedAudio& audio)
{
    std::vector<int16_t> out(audio.samples.size());
    for (size_t i = 0; i < out.size(); ++i)
    {
        const long v = std::lround(audio.samples[i] * 32767.0f);
        out[i] = static_cast<int16_t>(std::clamp(v, -32768L, 32767L));
    }
    return out;
}

}  // namespace DekiAudio

#endif  // DEKI_EDITOR
