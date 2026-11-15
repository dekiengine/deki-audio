#include "PumpWorker.h"
#include "Mixer.h"
#include <chrono>

#if defined(ESP_PLATFORM)
#include "esp_pthread.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace DekiAudio
{

bool PumpWorker::Start(const char* name, int channels, size_t chunkFrames, WriteFn write, void* context)
{
    Stop();
    if (!write || chunkFrames == 0 || channels < 1 || channels > 2)
    {
        return false;
    }
    // Written every few milliseconds, so internal RAM.
    if (!m_Chunk.Allocate(chunkFrames * static_cast<size_t>(channels), Deki::Memory::Internal))
    {
        return false;
    }
    m_ChunkFrames = chunkFrames;
    m_Write = write;
    m_Context = context;
    m_Running = true;

#if defined(ESP_PLATFORM)
    // Above the game and the display worker, so a busy frame does not starve
    // the speaker, and on the other core. The mixing itself takes little time.
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.pin_to_core = (xPortGetCoreID() + 1) % portNUM_PROCESSORS;
    cfg.prio = uxTaskPriorityGet(nullptr) + 2;
    cfg.thread_name = name;
    cfg.stack_size = 4096;
    esp_pthread_set_cfg(&cfg);
#else
    (void)name;
#endif
    m_Thread = std::thread([this] { Run(); });
#if defined(ESP_PLATFORM)
    esp_pthread_cfg_t restore = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&restore);
#endif
    return true;
}

void PumpWorker::Stop()
{
    m_Running = false;
    if (m_Thread.joinable())
    {
        m_Thread.join();
    }
}

void PumpWorker::Run()
{
    Mixer& mixer = Mixer::Instance();
    while (m_Running.load())
    {
        mixer.Render(m_Chunk.Data(), m_ChunkFrames);
        if (!m_Write(m_Context, m_Chunk.Data(), m_ChunkFrames))
        {
            // The hardware refused the samples. Waits rather than spinning, and
            // tries again: a speaker can come back (a stream reopened).
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

}  // namespace DekiAudio
