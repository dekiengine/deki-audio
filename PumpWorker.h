#pragma once

#include <deki/providers/Buffer.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace DekiAudio
{

/// Runs an output whose writes block until the hardware has room (I2S, ALSA,
/// Tactility): on its own thread, it mixes a chunk and writes it, again and
/// again. The write blocking is what paces it.
///
/// On an ESP32 the thread runs on the core the game is not on.
class PumpWorker
{
public:
    /// Writes `frames` interleaved frames, blocking until the hardware takes
    /// them. Returns false when the write failed.
    using WriteFn = bool (*)(void* context, const int16_t* samples, size_t frames);

    PumpWorker() = default;
    ~PumpWorker() { Stop(); }
    PumpWorker(const PumpWorker&) = delete;
    PumpWorker& operator=(const PumpWorker&) = delete;

    /// Starts the thread. `chunkFrames` frames of `channels` channels are mixed
    /// at a time: smaller answers sooner, larger costs less. Returns false
    /// when there is no memory or no thread.
    bool Start(const char* name, int channels, size_t chunkFrames, WriteFn write, void* context);

    /// Stops the thread and waits for it.
    void Stop();

    bool IsRunning() const { return m_Running.load(); }

private:
    void Run();

    std::thread m_Thread;
    std::atomic<bool> m_Running{ false };
    Deki::Buffer<int16_t> m_Chunk;
    size_t m_ChunkFrames = 0;
    WriteFn m_Write = nullptr;
    void* m_Context = nullptr;
};

}  // namespace DekiAudio
