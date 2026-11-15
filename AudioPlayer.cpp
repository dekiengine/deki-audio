#include "AudioPlayer.h"
#include "AudioReceiver.h"
#include <deki/Object.h>
#include <cmath>

namespace DekiAudio
{

AudioPlayer::~AudioPlayer()
{
    Stop();
}

void AudioPlayer::Start()
{
    if (playOnStart)
    {
        Play();
    }
}

void AudioPlayer::Update()
{
    if (m_Handle == kNoAudio)
    {
        return;
    }
    if (!Mixer::Instance().IsPlaying(m_Handle))
    {
        m_Handle = kNoAudio;
        return;
    }
    Apply();
}

void AudioPlayer::Spatialize(float& gain, float& pan) const
{
    gain = 1.0f;
    pan = 0.0f;
    const AudioReceiver* receiver = AudioReceiver::GetCurrent();
    const Deki::Object* self = GetOwner();
    if (!spatial || !receiver || !self || !receiver->GetOwner() || range <= 0.0f)
    {
        return;
    }
    const float dx = self->GetWorldX() - receiver->GetOwner()->GetWorldX();
    const float dy = self->GetWorldY() - receiver->GetOwner()->GetWorldY();
    const float distance = std::sqrt(dx * dx + dy * dy);
    gain = distance >= range ? 0.0f : 1.0f - distance / range;
    pan = dx / range;
    pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
}

void AudioPlayer::Apply()
{
    float gain = 1.0f;
    float pan = 0.0f;
    Spatialize(gain, pan);
    Mixer& mixer = Mixer::Instance();
    const float v = volume * gain;
    if (v != m_AppliedVolume)
    {
        mixer.SetVolume(m_Handle, v);
        m_AppliedVolume = v;
    }
    if (pitch != m_AppliedPitch)
    {
        mixer.SetPitch(m_Handle, pitch);
        m_AppliedPitch = pitch;
    }
    if (pan != m_AppliedPan)
    {
        mixer.SetPan(m_Handle, pan);
        m_AppliedPan = pan;
    }
}

void AudioPlayer::Play()
{
    Stop();
    float gain = 1.0f;
    float pan = 0.0f;
    Spatialize(gain, pan);
    PlayOptions options;
    options.volume = volume * gain;
    options.pitch = pitch;
    options.pan = pan;
    options.loop = loop;
    m_Handle = Mixer::Instance().Play(audio.Get(), options);
    m_AppliedVolume = options.volume;
    m_AppliedPitch = options.pitch;
    m_AppliedPan = options.pan;
}

void AudioPlayer::Stop()
{
    if (m_Handle != kNoAudio)
    {
        Mixer::Instance().Stop(m_Handle);
        m_Handle = kNoAudio;
    }
}

void AudioPlayer::SetPaused(bool paused)
{
    Mixer::Instance().SetPaused(m_Handle, paused);
}

bool AudioPlayer::IsPlaying() const
{
    return m_Handle != kNoAudio && Mixer::Instance().IsPlaying(m_Handle);
}

AudioHandle AudioPlayer::PlayOnce(const Audio* other, float volumeScale)
{
    float gain = 1.0f;
    float pan = 0.0f;
    Spatialize(gain, pan);
    PlayOptions options;
    options.volume = volume * gain * volumeScale;
    options.pitch = pitch;
    options.pan = pan;
    return Mixer::Instance().Play(other, options);
}

}  // namespace DekiAudio
