#include "Mixer.h"
#include "AudioReader.h"
#include <deki/LogSystem.h>
#include <atomic>

namespace DekiAudio
{

namespace
{
#if defined(DEKI_HOSTED) || defined(DEKI_EDITOR)
constexpr int kDefaultVoices = 32;
#else
constexpr int kDefaultVoices = 8;
#endif

// Frames mixed per pass, so the sums fit on the stack.
constexpr size_t kMixChunk = 64;
// Streamed audio keeps this much read ahead of the output.
constexpr uint32_t kStreamAheadMs = 250;
constexpr int32_t kUnityGain = 1 << 14;

float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int16_t Saturate(int32_t v)
{
    return static_cast<int16_t>(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
}

int16_t ReadI16(const uint8_t* p)
{
    return static_cast<int16_t>(static_cast<uint16_t>(p[0] | (p[1] << 8)));
}
}  // namespace

// A streamed audio's ring of blocks. The game thread fills it (Fill); the
// output thread reads the oldest block and gives it back. `written` and
// `read` count blocks since the start, so their difference is how many are
// waiting.
struct Mixer::Stream
{
    AudioReader reader;
    Deki::Buffer<uint8_t> ring;
    uint32_t slots = 0;
    uint32_t blockBytes = 0;
    uint32_t blockCount = 0;
    uint32_t nextFileBlock = 0;
    bool loop = false;
    bool failed = false;
    std::atomic<uint32_t> written{ 0 };
    std::atomic<uint32_t> read{ 0 };

    bool Open(const Audio& audio, bool loops)
    {
        const AudioFileInfo& info = audio.GetInfo();
        blockBytes = info.blockBytes;
        blockCount = info.BlockCount();
        loop = loops;
        const uint32_t aheadFrames = info.sampleRate * kStreamAheadMs / 1000;
        slots = aheadFrames / info.blockFrames + 2;
        if (slots < 3)
        {
            slots = 3;
        }
        if (!ring.Allocate(static_cast<size_t>(slots) * blockBytes, Deki::Memory::External))
        {
            DEKI_LOG_ERROR("Audio: no memory to stream %s", audio.GetPath().c_str());
            return false;
        }
        if (!reader.Open(audio.GetPath().c_str()))
        {
            DEKI_LOG_ERROR("Audio: cannot open %s to stream it", audio.GetPath().c_str());
            return false;
        }
        Fill();
        return !failed;
    }

    // Reads blocks into the free slots. Game thread.
    void Fill()
    {
        uint32_t w = written.load(std::memory_order_relaxed);
        while (!failed && w - read.load(std::memory_order_acquire) < slots)
        {
            if (nextFileBlock >= blockCount)
            {
                if (!loop)
                {
                    return;
                }
                nextFileBlock = 0;
            }
            uint8_t* slot = ring.Data() + static_cast<size_t>(w % slots) * blockBytes;
            const size_t offset = kAudioHeaderBytes + static_cast<size_t>(nextFileBlock) * blockBytes;
            if (!reader.ReadAt(offset, slot, blockBytes))
            {
                failed = true;
                DEKI_LOG_ERROR("Audio: a streamed audio could not be read");
                return;
            }
            ++nextFileBlock;
            ++w;
            written.store(w, std::memory_order_release);
        }
    }

    // The oldest block, or nullptr when none has been read yet. Output thread.
    const uint8_t* Front() const
    {
        const uint32_t r = read.load(std::memory_order_relaxed);
        if (r == written.load(std::memory_order_acquire))
        {
            return nullptr;
        }
        return ring.Data() + static_cast<size_t>(r % slots) * blockBytes;
    }

    void PopFront() { read.store(read.load(std::memory_order_relaxed) + 1, std::memory_order_release); }
};

Mixer& Mixer::Instance()
{
    static Mixer s_Mixer;
    return s_Mixer;
}

Mixer::Mixer() = default;

Mixer::~Mixer()
{
    Reset();
}

bool Mixer::SetVoiceCount(int count)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    for (int i = 0; i < m_VoiceCount; ++i)
    {
        FreeVoice(m_Voices[i]);
    }
    m_Voices.Reset();
    m_VoiceCount = 0;
    if (count <= 0)
    {
        return true;
    }
    // Read on every output frame, so internal RAM: a voice is under 100 bytes.
    if (!m_Voices.Allocate(static_cast<size_t>(count), Deki::Memory::Internal))
    {
        return false;
    }
    m_VoiceCount = count;
    return true;
}

int Mixer::GetVoiceCount() const
{
    std::lock_guard<std::mutex> lock(m_Lock);
    return m_VoiceCount;
}

void Mixer::SetOutputFormat(int sampleRate, int channels)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    m_SampleRate = sampleRate > 0 ? sampleRate : 0;
    m_Channels = channels == 2 ? 2 : (channels == 1 ? 1 : 0);
    for (int i = 0; i < m_VoiceCount; ++i)
    {
        if (m_Voices[i].state == VoiceState::Playing)
        {
            UpdateStep(m_Voices[i]);
        }
    }
}

int Mixer::GetSampleRate() const
{
    std::lock_guard<std::mutex> lock(m_Lock);
    return m_SampleRate;
}

int Mixer::GetChannels() const
{
    std::lock_guard<std::mutex> lock(m_Lock);
    return m_Channels;
}

bool Mixer::EnsureVoices()
{
    if (m_VoiceCount > 0)
    {
        return true;
    }
    if (!m_Voices.Allocate(static_cast<size_t>(kDefaultVoices), Deki::Memory::Internal))
    {
        return false;
    }
    m_VoiceCount = kDefaultVoices;
    return true;
}

void Mixer::FreeVoice(Voice& voice)
{
    delete voice.stream;
    voice.stream = nullptr;
    voice.audio = nullptr;
    voice.block = nullptr;
    voice.state = VoiceState::Free;
}

void Mixer::UpdateGains(Voice& voice) const
{
    const float volume = Clamp(voice.volume, 0.0f, 1.0f) * Clamp(m_MasterVolume, 0.0f, 1.0f);
    const float pan = Clamp(voice.pan, -1.0f, 1.0f);
    const float left = pan > 0.0f ? 1.0f - pan : 1.0f;
    const float right = pan < 0.0f ? 1.0f + pan : 1.0f;
    voice.gainLeft = static_cast<int32_t>(volume * left * kUnityGain + 0.5f);
    voice.gainRight = static_cast<int32_t>(volume * right * kUnityGain + 0.5f);
    voice.gainMono = static_cast<int32_t>(volume * kUnityGain + 0.5f);
}

void Mixer::UpdateStep(Voice& voice) const
{
    if (!voice.audio || m_SampleRate <= 0)
    {
        voice.step = 0;
        return;
    }
    const float ratio = static_cast<float>(voice.audio->GetInfo().sampleRate) / static_cast<float>(m_SampleRate);
    const float step = ratio * Clamp(voice.pitch, 0.1f, 4.0f) * 65536.0f + 0.5f;
    voice.step = static_cast<uint32_t>(Clamp(step, 1.0f, 16.0f * 65536.0f));
}

void Mixer::SetStartHook(StartHook hook)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    m_StartHook = hook;
}

AudioHandle Mixer::Play(const Audio* audio, const PlayOptions& options)
{
    if (!audio || audio->GetInfo().frameCount == 0)
    {
        return kNoAudio;
    }
    if (!audio->IsStreamed() && !audio->GetBlocks())
    {
        return kNoAudio;
    }

    StartHook hook = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        hook = m_StartHook;
    }
    // Outside the lock: an output that starts pulls samples at once.
    if (hook)
    {
        hook();
    }

    // Opened before the lock, since it reads the first blocks from storage.
    Stream* stream = nullptr;
    if (audio->IsStreamed())
    {
        stream = new Stream();
        if (!stream->Open(*audio, options.loop))
        {
            delete stream;
            return kNoAudio;
        }
    }

    std::lock_guard<std::mutex> lock(m_Lock);
    if (m_SampleRate <= 0 || !EnsureVoices())
    {
        delete stream;
        return kNoAudio;
    }

    int chosen = -1;
    for (int i = 0; i < m_VoiceCount && chosen < 0; ++i)
    {
        if (m_Voices[i].state != VoiceState::Playing)
        {
            chosen = i;
        }
    }
    if (chosen < 0)
    {
        chosen = 0;
        for (int i = 1; i < m_VoiceCount; ++i)
        {
            if (m_Voices[i].order < m_Voices[chosen].order)
            {
                chosen = i;
            }
        }
    }

    Voice& v = m_Voices[chosen];
    const uint32_t generation = (v.generation + 1) & 0xFFFFFF;
    FreeVoice(v);
    v = Voice{};
    v.generation = generation ? generation : 1;
    v.order = m_NextOrder++;
    v.audio = audio;
    v.stream = stream;
    v.loop = options.loop;
    v.volume = options.volume;
    v.pan = options.pan;
    v.pitch = options.pitch;
    UpdateGains(v);
    UpdateStep(v);

    // The first two frames, so the voice interpolates from its first sample.
    // A stream holds its first blocks already (Stream::Open).
    if (NextFrame(v, v.prev) == Next::End || NextFrame(v, v.cur) == Next::End)
    {
        v.cur[0] = v.cur[1] = 0;
        v.ending = true;
    }
    v.state = VoiceState::Playing;
    return (v.generation << 8) | static_cast<uint32_t>(chosen);
}

Mixer::Voice* Mixer::Find(AudioHandle handle)
{
    const int index = static_cast<int>(handle & 0xFF);
    if (handle == kNoAudio || index >= m_VoiceCount)
    {
        return nullptr;
    }
    Voice& v = m_Voices[index];
    if (v.state != VoiceState::Playing || v.generation != (handle >> 8))
    {
        return nullptr;
    }
    return &v;
}

const Mixer::Voice* Mixer::Find(AudioHandle handle) const
{
    return const_cast<Mixer*>(this)->Find(handle);
}

void Mixer::Stop(AudioHandle handle)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    if (Voice* v = Find(handle))
    {
        FreeVoice(*v);
    }
}

void Mixer::SetPaused(AudioHandle handle, bool paused)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    if (Voice* v = Find(handle))
    {
        v->paused = paused;
    }
}

void Mixer::SetVolume(AudioHandle handle, float volume)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    if (Voice* v = Find(handle))
    {
        v->volume = volume;
        UpdateGains(*v);
    }
}

void Mixer::SetPitch(AudioHandle handle, float pitch)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    if (Voice* v = Find(handle))
    {
        v->pitch = pitch;
        UpdateStep(*v);
    }
}

void Mixer::SetPan(AudioHandle handle, float pan)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    if (Voice* v = Find(handle))
    {
        v->pan = pan;
        UpdateGains(*v);
    }
}

bool Mixer::IsPlaying(AudioHandle handle) const
{
    std::lock_guard<std::mutex> lock(m_Lock);
    return Find(handle) != nullptr;
}

void Mixer::StopAll()
{
    std::lock_guard<std::mutex> lock(m_Lock);
    for (int i = 0; i < m_VoiceCount; ++i)
    {
        FreeVoice(m_Voices[i]);
    }
}

void Mixer::SetAllPaused(bool paused)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    m_AllPaused = paused;
}

bool Mixer::IsAllPaused() const
{
    std::lock_guard<std::mutex> lock(m_Lock);
    return m_AllPaused;
}

void Mixer::StopAllUsing(const Audio* audio)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    for (int i = 0; i < m_VoiceCount; ++i)
    {
        if (m_Voices[i].state != VoiceState::Free && m_Voices[i].audio == audio)
        {
            FreeVoice(m_Voices[i]);
        }
    }
}

void Mixer::SetMasterVolume(float volume)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    m_MasterVolume = Clamp(volume, 0.0f, 1.0f);
    for (int i = 0; i < m_VoiceCount; ++i)
    {
        UpdateGains(m_Voices[i]);
    }
}

float Mixer::GetMasterVolume() const
{
    std::lock_guard<std::mutex> lock(m_Lock);
    return m_MasterVolume;
}

MixerStats Mixer::GetStats() const
{
    std::lock_guard<std::mutex> lock(m_Lock);
    MixerStats stats = m_Stats;
    stats.voicesPlaying = 0;
    for (int i = 0; i < m_VoiceCount; ++i)
    {
        if (m_Voices[i].state == VoiceState::Playing)
        {
            ++stats.voicesPlaying;
        }
    }
    return stats;
}

void Mixer::Reset()
{
    std::lock_guard<std::mutex> lock(m_Lock);
    for (int i = 0; i < m_VoiceCount; ++i)
    {
        FreeVoice(m_Voices[i]);
    }
    m_Voices.Reset();
    m_VoiceCount = 0;
    m_AllPaused = false;
    m_Stats = MixerStats{};
}

void Mixer::Update()
{
    // The streams are freed only on this thread, so they stay valid while
    // they are filled outside the lock.
    Stream* streams[64];
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(m_Lock);
        for (int i = 0; i < m_VoiceCount; ++i)
        {
            Voice& v = m_Voices[i];
            if (v.state == VoiceState::Finished)
            {
                FreeVoice(v);
            }
            else if (v.state == VoiceState::Playing && v.stream && count < 64)
            {
                streams[count++] = v.stream;
            }
        }
    }
    for (int i = 0; i < count; ++i)
    {
        streams[i]->Fill();
    }
}

Mixer::Next Mixer::NextFrame(Voice& v, int16_t* out)
{
    const AudioFileInfo& info = v.audio->GetInfo();
    if (v.next >= info.frameCount)
    {
        if (!v.loop)
        {
            return Next::End;
        }
        v.next = 0;
        v.within = 0;
    }

    if (!v.block)
    {
        if (v.stream)
        {
            v.block = v.stream->Front();
            if (!v.block)
            {
                return Next::Hold;
            }
        }
        else
        {
            v.block = v.audio->GetBlocks() + static_cast<size_t>(v.next / info.blockFrames) * info.blockBytes;
        }
    }

    const uint32_t channels = info.channels;
    if (info.codec == AudioCodec::ImaAdpcm)
    {
        for (uint32_t c = 0; c < channels; ++c)
        {
            const uint8_t* part = v.block + c * kAdpcmChannelBlockBytes;
            AdpcmState& state = v.adpcm[c];
            if (v.within == 0)
            {
                state.predictor = ReadI16(part);
                state.stepIndex = part[2] > 88 ? 88 : part[2];
                out[c] = static_cast<int16_t>(state.predictor);
            }
            else
            {
                const uint32_t i = v.within - 1;
                const uint8_t byte = part[4 + (i >> 1)];
                out[c] =
                    DecodeAdpcm(state, (i & 1) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 15));
            }
        }
    }
    else
    {
        const uint8_t* frame = v.block + static_cast<size_t>(v.within) * channels * 2;
        for (uint32_t c = 0; c < channels; ++c)
        {
            out[c] = ReadI16(frame + c * 2);
        }
    }
    if (channels == 1)
    {
        out[1] = out[0];
    }

    ++v.within;
    ++v.next;
    if (v.within >= info.blockFrames || v.next >= info.frameCount)
    {
        if (v.stream)
        {
            v.stream->PopFront();
        }
        v.block = nullptr;
        v.within = 0;
    }
    return Next::Frame;
}

void Mixer::MixVoice(Voice& v, int32_t* acc, size_t frames)
{
    const bool stereoOut = m_Channels == 2;
    for (size_t i = 0; i < frames; ++i)
    {
        // Linear interpolation between the two frames either side, with the
        // fraction cut to 14 bits so the product fits 32 bits, and rounded.
        const int32_t t = static_cast<int32_t>(v.frac >> 2);
        const int32_t left = v.prev[0] + (((v.cur[0] - v.prev[0]) * t + 8192) >> 14);
        const int32_t right = v.prev[1] + (((v.cur[1] - v.prev[1]) * t + 8192) >> 14);
        if (stereoOut)
        {
            acc[i * 2] += (left * v.gainLeft) >> 14;
            acc[i * 2 + 1] += (right * v.gainRight) >> 14;
        }
        else
        {
            acc[i] += (((left + right) >> 1) * v.gainMono) >> 14;
        }

        v.frac += v.step;
        while (v.frac >= 65536)
        {
            int16_t frame[2];
            const Next next = NextFrame(v, frame);
            if (next == Next::Hold)
            {
                // Stays on the last sample until the stream catches up, which
                // is quieter than a jump to zero and back.
                ++m_Stats.underruns;
                v.frac = 65535;
                break;
            }
            v.frac -= 65536;
            v.prev[0] = v.cur[0];
            v.prev[1] = v.cur[1];
            if (next == Next::End)
            {
                if (v.ending)
                {
                    v.state = VoiceState::Finished;
                    return;
                }
                // One more step, to the end's silence.
                v.ending = true;
                v.cur[0] = v.cur[1] = 0;
            }
            else
            {
                v.cur[0] = frame[0];
                v.cur[1] = frame[1];
            }
        }
    }
}

void Mixer::Render(int16_t* out, size_t frames)
{
    std::lock_guard<std::mutex> lock(m_Lock);
    const size_t channels = m_Channels > 0 ? static_cast<size_t>(m_Channels) : 1;
    int32_t acc[kMixChunk * 2];
    while (frames > 0)
    {
        const size_t n = frames < kMixChunk ? frames : kMixChunk;
        for (size_t i = 0; i < n * channels; ++i)
        {
            acc[i] = 0;
        }
        if (!m_AllPaused)
        {
            for (int i = 0; i < m_VoiceCount; ++i)
            {
                Voice& v = m_Voices[i];
                if (v.state == VoiceState::Playing && !v.paused && v.step > 0)
                {
                    MixVoice(v, acc, n);
                }
            }
        }
        for (size_t i = 0; i < n * channels; ++i)
        {
            out[i] = Saturate(acc[i]);
        }
        out += n * channels;
        frames -= n;
        m_Stats.framesRendered += static_cast<uint32_t>(n);
    }
}

}  // namespace DekiAudio
