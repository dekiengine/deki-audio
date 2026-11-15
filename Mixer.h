#pragma once

#include "Audio.h"
#include "ImaAdpcm.h"
#include <deki/providers/Buffer.h>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace DekiAudio
{

/// Names one playing audio, so it can be stopped or changed later. A handle
/// goes stale when its audio ends; using a stale one does nothing.
using AudioHandle = uint32_t;
constexpr AudioHandle kNoAudio = 0;

/// How a new audio plays.
struct PlayOptions
{
    /// 0 is silent, 1 is as recorded.
    float volume = 1.0f;
    /// 1 is as recorded, 2 an octave up and twice as fast.
    float pitch = 1.0f;
    /// -1 is left, 0 the middle, 1 right. Ignored by a mono output.
    float pan = 0.0f;
    /// Starts again from the beginning at the end.
    bool loop = false;
};

/// What the mixer has done, for STATS and tests.
struct MixerStats
{
    uint32_t voicesPlaying = 0;
    /// Output frames in which a streamed audio held its last sample because
    /// its next block had not been read yet.
    uint32_t underruns = 0;
    uint32_t framesRendered = 0;
};

/// Mixes every playing audio into the output's samples.
///
/// The game thread plays and stops audio. The output pulls mixed samples with
/// Render(), from its own thread (I2S, ALSA, Tactility) or its callback
/// (SDL). A short lock keeps the two apart; rendering never allocates.
///
/// Streamed audio is read from storage on the game thread, in Update(), into
/// a ring of blocks the output thread reads, so the output never waits on the
/// flash or the SD card.
class Mixer
{
public:
    static Mixer& Instance();

    Mixer();
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    /// Sets how many audios can play at once. Stops everything. Returns false
    /// when there is no memory for them.
    bool SetVoiceCount(int count);
    int GetVoiceCount() const;

    /// The output's sample rate and channels (1 or 2). Set by the output when
    /// it becomes the current one; 0 means there is no output, and then
    /// nothing plays.
    void SetOutputFormat(int sampleRate, int channels);
    int GetSampleRate() const;
    int GetChannels() const;

    /// Starts `audio` and returns its handle, or kNoAudio when there is no
    /// output or the audio cannot play. When every voice is busy, the one that
    /// started first gives way.
    AudioHandle Play(const Audio* audio, const PlayOptions& options = {});

    void Stop(AudioHandle handle);
    void SetPaused(AudioHandle handle, bool paused);
    void SetVolume(AudioHandle handle, float volume);
    void SetPitch(AudioHandle handle, float pitch);
    void SetPan(AudioHandle handle, float pan);

    /// True while the audio plays or is paused.
    bool IsPlaying(AudioHandle handle) const;

    void StopAll();

    /// Pauses everything, such as while the game is paused, and resumes it.
    void SetAllPaused(bool paused);
    bool IsAllPaused() const;

    /// Stops every voice playing `audio`, before it is freed.
    void StopAllUsing(const Audio* audio);

    /// 0..1, applied to everything.
    void SetMasterVolume(float volume);
    float GetMasterVolume() const;

    /// Mixes `frames` frames into `out`, interleaved in the output's channels.
    /// Called by the output.
    void Render(int16_t* out, size_t frames);

    /// Reads streamed audio from storage and frees the voices that ended.
    /// Called on the game thread once a frame.
    void Update();

    MixerStats GetStats() const;

    /// Called on the game thread before the first Play() that finds the output
    /// stopped, so an output can start only once something plays.
    using StartHook = void (*)();
    void SetStartHook(StartHook hook);

    /// Stops everything and frees the voices. The next Play() makes them again.
    void Reset();

private:
    struct Stream;

    enum class VoiceState : uint8_t
    {
        Free,
        Playing,
        /// Ended on the output thread; the game thread frees it.
        Finished,
    };

    enum class Next : uint8_t
    {
        Frame,
        /// A streamed audio's next block has not been read yet.
        Hold,
        End,
    };

    // Plain data, so the voices sit in one Deki::Buffer.
    struct Voice
    {
        const Audio* audio;
        Stream* stream;
        uint32_t generation;
        uint32_t order;
        VoiceState state;
        bool paused;
        bool loop;
        bool ending;
        float volume;
        float pan;
        float pitch;
        // Gains in 2.14 fixed point, the master volume included.
        int32_t gainLeft;
        int32_t gainRight;
        int32_t gainMono;
        // How far one output frame moves through the audio, in 16.16.
        uint32_t step;
        uint32_t frac;
        // The next frame to decode, and where it is in its block.
        uint32_t next;
        uint32_t within;
        const uint8_t* block;
        AdpcmState adpcm[2];
        int16_t prev[2];
        int16_t cur[2];
    };

    Voice* Find(AudioHandle handle);
    const Voice* Find(AudioHandle handle) const;
    bool EnsureVoices();
    void FreeVoice(Voice& voice);
    void UpdateGains(Voice& voice) const;
    void UpdateStep(Voice& voice) const;
    Next NextFrame(Voice& voice, int16_t* out);
    void MixVoice(Voice& voice, int32_t* acc, size_t frames);

    mutable std::mutex m_Lock;
    Deki::Buffer<Voice> m_Voices;
    int m_VoiceCount = 0;
    int m_SampleRate = 0;
    int m_Channels = 0;
    float m_MasterVolume = 1.0f;
    bool m_AllPaused = false;
    uint32_t m_NextOrder = 1;
    MixerStats m_Stats;
    StartHook m_StartHook = nullptr;
};

}  // namespace DekiAudio
