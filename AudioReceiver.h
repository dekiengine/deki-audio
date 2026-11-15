#pragma once

#include <deki/Component.h>
#include <deki/reflection/Property.h>

namespace DekiAudio
{

/// Where spatial audio is heard from, usually on the player or the camera.
/// An Audio Player with Spatial on sounds to the left or right of it and
/// quieter with distance. With more than one, the last one made is used.
DEKI_CATEGORY("Audio")
DEKI_DISPLAY_NAME("Audio Receiver")
DEKI_DESCRIPTION("Where spatial audio is heard from, usually on the player or the camera.")
class AudioReceiver : public Deki::Component
{
public:
    AudioReceiver();
    ~AudioReceiver() override;

    /// The receiver in use, or nullptr when the scene has none.
    static AudioReceiver* GetCurrent();

private:
    AudioReceiver* m_Previous = nullptr;
    static AudioReceiver* s_Current;
};

}  // namespace DekiAudio
