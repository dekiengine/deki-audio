#include "ImaAdpcm.h"
#include "AudioFile.h"

namespace DekiAudio
{

namespace
{
// The IMA ADPCM tables, as every decoder of the format has them.
constexpr int16_t kStepTable[89] = {
    7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

constexpr int8_t kIndexTable[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

int32_t Clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
}  // namespace

int16_t DecodeAdpcm(AdpcmState& state, uint8_t code)
{
    const int32_t step = kStepTable[state.stepIndex];
    int32_t diff = step >> 3;
    if (code & 1)
    {
        diff += step >> 2;
    }
    if (code & 2)
    {
        diff += step >> 1;
    }
    if (code & 4)
    {
        diff += step;
    }
    state.predictor = Clamp((code & 8) ? state.predictor - diff : state.predictor + diff, -32768, 32767);
    state.stepIndex = Clamp(state.stepIndex + kIndexTable[code & 15], 0, 88);
    return static_cast<int16_t>(state.predictor);
}

#ifdef DEKI_EDITOR
uint8_t EncodeAdpcm(AdpcmState& state, int16_t sample)
{
    const int32_t step = kStepTable[state.stepIndex];
    int32_t diff = sample - state.predictor;
    uint8_t code = 0;
    if (diff < 0)
    {
        code = 8;
        diff = -diff;
    }
    if (diff >= step)
    {
        code |= 4;
        diff -= step;
    }
    if (diff >= (step >> 1))
    {
        code |= 2;
        diff -= step >> 1;
    }
    if (diff >= (step >> 2))
    {
        code |= 1;
    }
    // The decoder's own step keeps the two in agreement, so errors do not
    // pile up.
    DecodeAdpcm(state, code);
    return code;
}

void EncodeAdpcmBlocks(const int16_t* samples, size_t frames, uint8_t channels, uint8_t* out)
{
    AdpcmState states[2];
    const size_t blocks = (frames + kAdpcmBlockFrames - 1) / kAdpcmBlockFrames;
    for (size_t b = 0; b < blocks; ++b)
    {
        const size_t first = b * kAdpcmBlockFrames;
        for (uint8_t c = 0; c < channels; ++c)
        {
            uint8_t* block = out + b * kAdpcmChannelBlockBytes * channels + c * kAdpcmChannelBlockBytes;
            auto sampleAt = [&](size_t frame) -> int16_t
            { return frame < frames ? samples[frame * channels + c] : static_cast<int16_t>(0); };

            AdpcmState& state = states[c];
            state.predictor = sampleAt(first);
            const uint16_t head = static_cast<uint16_t>(static_cast<int16_t>(state.predictor));
            block[0] = static_cast<uint8_t>(head);
            block[1] = static_cast<uint8_t>(head >> 8);
            block[2] = static_cast<uint8_t>(state.stepIndex);
            block[3] = 0;

            uint8_t* codes = block + 4;
            for (size_t i = 1; i < kAdpcmBlockFrames; ++i)
            {
                const uint8_t code = EncodeAdpcm(state, sampleAt(first + i));
                const size_t at = (i - 1) >> 1;
                if (((i - 1) & 1) == 0)
                {
                    codes[at] = code;
                }
                else
                {
                    codes[at] = static_cast<uint8_t>(codes[at] | (code << 4));
                }
            }
        }
    }
}
#endif

}  // namespace DekiAudio
