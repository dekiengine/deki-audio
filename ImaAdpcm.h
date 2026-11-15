#pragma once

#include <cstddef>
#include <cstdint>

namespace DekiAudio
{

/// The running state of one IMA ADPCM channel: the last sample and the step
/// size index.
struct AdpcmState
{
    int32_t predictor = 0;
    int32_t stepIndex = 0;
};

/// Decodes one 4-bit code and returns the sample it gives, updating `state`.
int16_t DecodeAdpcm(AdpcmState& state, uint8_t code);

#ifdef DEKI_EDITOR
/// Encodes `sample` as a 4-bit code, updating `state` the same way the
/// decoder will.
uint8_t EncodeAdpcm(AdpcmState& state, int16_t sample);

/// Encodes `frames` frames of interleaved samples into whole ImaAdpcm blocks
/// (AudioFile.h), the last block padded with silence. `out` holds
/// BlockCount * blockBytes bytes. The step index carries from block to block,
/// so a block starts at the right loudness.
void EncodeAdpcmBlocks(const int16_t* samples, size_t frames, uint8_t channels, uint8_t* out);
#endif

}  // namespace DekiAudio
