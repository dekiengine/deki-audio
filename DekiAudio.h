#pragma once

#include "Audio.h"
#include "Mixer.h"

// Plays audio from game code without an Audio Player:
//
//   DekiAudio::PlayOnce(coinAudio);
//
// For more control (stop it later, change its pitch), keep the handle that
// Mixer::Instance().Play returns.
namespace DekiAudio
{

/// Plays `audio` once from start to end. Returns its handle, or kNoAudio when
/// nothing can play it.
AudioHandle PlayOnce(const Audio* audio, float volume = 1.0f, float pitch = 1.0f);

/// The volume of everything, 0..1. On a device with its own volume (Tactility)
/// this is the device's volume, so the game and the OS show the same level.
void SetMasterVolume(float volume);
float GetMasterVolume();

void StopAll();

/// Pauses or resumes everything.
void SetAllPaused(bool paused);

}  // namespace DekiAudio
