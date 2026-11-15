#pragma once
#ifdef DEKI_EDITOR
// Changes decoded audio into what a target plays: its channels, its sample
// rate, then 16-bit samples. Editor only.

#include "AudioDecode.h"
#include <cstdint>
#include <vector>

namespace DekiAudio
{

/// Mixes to one channel (the average of both) or spreads one to two.
void ConvertChannels(DecodedAudio& audio, int channels);

/// Changes the sample rate with a windowed-sinc filter, which also removes
/// what the lower rate cannot hold, so going down does not alias.
void Resample(DecodedAudio& audio, int sampleRate);

/// Rounds the samples to 16 bits.
std::vector<int16_t> ToPcm16(const DecodedAudio& audio);

}  // namespace DekiAudio

#endif  // DEKI_EDITOR
