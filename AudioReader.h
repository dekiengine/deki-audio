#pragma once

#include <deki/providers/IFileSystem.h>
#include <cstddef>

namespace DekiAudio
{

/// Reads parts of a file through the engine's file systems (flash, SD card,
/// the desktop's disk). Closes the file when it goes.
class AudioReader
{
public:
    AudioReader() = default;
    ~AudioReader() { Close(); }
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;

    /// Opens `path` for reading. Returns false when it cannot be opened.
    bool Open(const char* path);
    void Close();
    bool IsOpen() const { return m_Handle != nullptr; }

    /// Reads `bytes` bytes from `offset`. Returns false unless all of them
    /// were read.
    bool ReadAt(size_t offset, void* out, size_t bytes);

    /// The file's size in bytes, or -1 when it is not open.
    long Size();

private:
    Deki::IFileSystem* m_Fs = nullptr;
    Deki::IFileSystem::FileHandle m_Handle = nullptr;
    size_t m_Position = 0;
};

}  // namespace DekiAudio
