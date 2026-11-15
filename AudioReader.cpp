#include "AudioReader.h"
#include <deki/providers/FileSystem.h>

namespace DekiAudio
{

bool AudioReader::Open(const char* path)
{
    Close();
    m_Fs = Deki::FileSystem::GetFileSystemForPath(path);
    if (!m_Fs)
    {
        return false;
    }
    m_Handle = m_Fs->OpenFile(path, Deki::IFileSystem::OpenMode::ReadBinary);
    m_Position = 0;
    return m_Handle != nullptr;
}

void AudioReader::Close()
{
    if (m_Handle && m_Fs)
    {
        m_Fs->CloseFile(m_Handle);
    }
    m_Handle = nullptr;
    m_Fs = nullptr;
    m_Position = 0;
}

bool AudioReader::ReadAt(size_t offset, void* out, size_t bytes)
{
    if (!m_Handle)
    {
        return false;
    }
    // Streaming reads follow each other, so the seek is only needed after a
    // loop back to the start.
    if (offset != m_Position)
    {
        if (m_Fs->SeekFile(m_Handle, static_cast<long>(offset), Deki::IFileSystem::SeekOrigin::Begin) < 0)
        {
            return false;
        }
        m_Position = offset;
    }
    const size_t got = m_Fs->ReadFile(m_Handle, out, bytes);
    m_Position += got;
    return got == bytes;
}

long AudioReader::Size()
{
    return m_Handle ? m_Fs->GetFileSize(m_Handle) : -1;
}

}  // namespace DekiAudio
