#pragma once

#include <cstddef>
#include <cstdint>

namespace DekiAudio
{

/// How the samples of a .daud file are stored.
enum class AudioCodec : uint8_t
{
    /// 16-bit signed samples, channels interleaved.
    Pcm16 = 0,
    /// IMA ADPCM, 4 bits a sample: a quarter of the size of Pcm16, and cheap to
    /// decode. Each block starts with each channel's predictor, so a block
    /// decodes on its own.
    ImaAdpcm = 1,
};

/// The header of a compiled audio file (.daud), which the editor writes for
/// each target. The samples follow it in blocks of `blockBytes`, so a player
/// can stream them a block at a time.
///
/// Layout, all little-endian:
///   0  "DAUD"
///   4  uint16 version
///   6  uint8  codec
///   7  uint8  channels (1 or 2)
///   8  uint32 sample rate
///   12 uint32 frame count (one frame holds a sample of each channel)
///   16 uint16 frames per block
///   18 uint16 bytes per block
///   20 uint8  flags (kAudioFlagStream)
///   21 3 bytes, zero
///   24 uint32 loop start frame
///   28 uint32 loop end frame
///   32 the blocks, the last one padded to the full size
///
/// A Pcm16 block is its frames, interleaved. An ImaAdpcm block holds each
/// channel in turn: a 4-byte header (int16 first sample, uint8 step index,
/// uint8 zero), then the other frames as 4-bit codes, the low nibble first.
struct AudioFileInfo
{
    AudioCodec codec = AudioCodec::Pcm16;
    uint8_t channels = 1;
    uint32_t sampleRate = 0;
    uint32_t frameCount = 0;
    uint16_t blockFrames = 0;
    uint16_t blockBytes = 0;
    uint8_t flags = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;

    /// How many blocks hold the frames.
    uint32_t BlockCount() const { return blockFrames ? (frameCount + blockFrames - 1) / blockFrames : 0; }
    /// The size of the blocks together, which follow the header.
    size_t DataBytes() const { return static_cast<size_t>(BlockCount()) * blockBytes; }
    bool IsStreamed() const;
};

constexpr uint16_t kAudioFileVersion = 1;
constexpr size_t kAudioHeaderBytes = 32;

/// Set when the audio plays from storage a block at a time instead of being
/// loaded whole.
constexpr uint8_t kAudioFlagStream = 0x01;

/// An ImaAdpcm channel's share of a block: its 4-byte header and the codes.
constexpr uint16_t kAdpcmChannelBlockBytes = 256;
/// The frames in an ImaAdpcm block: the header's sample and two a byte.
constexpr uint16_t kAdpcmBlockFrames = 1 + (kAdpcmChannelBlockBytes - 4) * 2;
/// The frames in a Pcm16 block.
constexpr uint16_t kPcmBlockFrames = 256;

/// Reads a .daud header from the first `size` bytes of a file. Returns false
/// when it is not one this version reads or its numbers do not agree.
bool ParseAudioHeader(const uint8_t* data, size_t size, AudioFileInfo& out);

/// Writes the header for `info` into `out`, which holds kAudioHeaderBytes.
void WriteAudioHeader(const AudioFileInfo& info, uint8_t* out);

/// The frames per block and bytes per block that `codec` uses for
/// `channels`.
void AudioBlockLayout(AudioCodec codec, uint8_t channels, uint16_t& blockFrames, uint16_t& blockBytes);

}  // namespace DekiAudio
