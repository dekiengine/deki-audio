#pragma once

#include "Audio.h"
#include "Mixer.h"
#include <deki/Component.h>
#include <deki/assets/AssetRef.h>
#include <deki/reflection/Property.h>

namespace DekiAudio
{

/// Plays an audio asset from an object: music, a voice or an effect. With
/// Spatial on, it sounds from the object's place: to the left or right of the
/// Audio Receiver, and quieter with distance until Range, where it is silent.
DEKI_CATEGORY("Audio")
DEKI_DISPLAY_NAME("Audio Player")
DEKI_DESCRIPTION("Plays an audio asset: music, a voice or an effect.")
class AudioPlayer : public Deki::Component
{
public:
    DEKI_EXPORT
    DEKI_TOOLTIP("The audio to play: a .wav, .mp3 or .ogg file from the project.")
    Deki::AssetRef<Audio> audio;

    DEKI_EXPORT
    DEKI_TOOLTIP("0 is silent, 1 is as recorded.")
    DEKI_RANGE(0, 1)
    float volume = 1.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("1 is as recorded. 2 plays an octave higher and twice as fast; 0.5 an octave lower and half as fast.")
    DEKI_RANGE(0.25, 4)
    float pitch = 1.0f;

    DEKI_EXPORT
    DEKI_TOOLTIP("Starts again from the beginning at the end. For music and sounds that go on, such as wind.")
    bool loop = false;

    DEKI_EXPORT
    DEKI_TOOLTIP("Plays as soon as the scene starts. Turn it off to play from code with Play().")
    bool playOnStart = true;

    DEKI_EXPORT
    DEKI_TOOLTIP("Sounds from where the object is: to the left or right of the Audio Receiver, and quieter further "
                 "away. Needs an Audio Receiver in the scene.")
    bool spatial = false;

    DEKI_EXPORT
    DEKI_TOOLTIP("With Spatial on, the distance at which the audio can no longer be heard.")
    DEKI_UNIT(Distance)
    DEKI_RANGE(0.1, 1000)
    float range = 10.0f;

    AudioPlayer() = default;
    ~AudioPlayer() override;

    void Start() override;
    void Update() override;

    /// Plays the audio from the start, stopping it first if it was playing.
    void Play();
    void Stop();
    void SetPaused(bool paused);
    /// True while the audio plays or is paused.
    bool IsPlaying() const;

    /// Plays another audio once from this object, at this player's volume
    /// times `volumeScale`, without stopping the player's own.
    AudioHandle PlayOnce(const Audio* other, float volumeScale = 1.0f);

private:
    void Apply();
    void Spatialize(float& gain, float& pan) const;

    AudioHandle m_Handle = kNoAudio;
    float m_AppliedVolume = -1.0f;
    float m_AppliedPitch = -1.0f;
    float m_AppliedPan = 0.0f;
};

}  // namespace DekiAudio
