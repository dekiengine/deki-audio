# Changelog

Notable changes to `deki-audio`. Engine and editor changes are in the
[engine changelog](https://github.com/dekiengine/deki-engine/blob/master/CHANGELOG.md).

A package's `minEngine` names the engine version it needs. Before 1.0 a
breaking change bumps the minor across the editor, the engine and every
package together, so a package with no changes of its own is still released
alongside one that has them.

## Unreleased

### Added
- **Audio assets.** An `Audio` asset is a sound the editor compiled for the
  target (a `.daud` file): 16-bit PCM or IMA ADPCM, in blocks. Short audio is
  loaded whole; long audio streams from storage a block at a time.
- **A mixer** (`DekiAudio::Mixer`) plays many audios at once, each with its
  own volume, pitch, pan and loop, resampled to the output's rate. Outputs
  pull from it on their own thread or callback; streamed audio is read on the
  game thread, so the output never waits on storage.
- **Audio Player** component: an audio, volume, pitch, loop, Play On Start,
  and Spatial with a Range for sound that comes from where the object is.
  **Audio Receiver** marks where spatial sound is heard from.
- **The editor imports `.wav`, `.ogg` and `.mp3` files** and converts each for every
  target as it builds: Automatic gives computers 16-bit PCM at the file's
  rate, and boards IMA ADPCM at their speaker's rate (the platform's
  `audioSampleRate` and `audioChannels` options) in mono; audio longer than
  10 seconds streams. The audio's inspector sets Format, Sample Rate,
  Channels and Loading for every target or for one, and shows the size each
  target gets.
- **An MP3 decoder of Deki's own** (MPEG-1, 2 and 2.5 Layer III) reads
  `.mp3` files. It matches Windows' own MP3 decoder sample for sample, to
  within 80 dB, on every file Windows' encoder writes (32, 44.1 and 48 kHz,
  mono and joint stereo, 32 to 320 kbit/s).
- `DekiAudio::PlayOnce(audio)`, `SetMasterVolume`, `StopAll` and
  `SetAllPaused` for game code.

### Changed
- **`IDekiAudio` is an output now**: `GetSampleRate`, `GetChannels`, `Start`,
  `Stop` and `IsRunning` replace `PlayPCM`, `SetVolume` and `IsPlaying`. An
  output becomes the current one with `AudioOutput::SetCurrent`, which
  replaces `DekiAudio::DekiAudio::SetCurrent`.
- The MAX98357A driver mixes on its own thread and starts its I2S channel
  with the first sound. Its setup no longer stops the boot when the amp is
  missing; the game starts without sound. Its default rate is 22050 Hz.
- The `max98357` feature requires `deki-i2s`; the package itself requires
  nothing, so a desktop project gets audio without it.

## 0.40.0

### Changed
- `minEngine` 0.40.0: built against engine 0.40.0. A build of this package
  from before does not load and is rebuilt.

## 0.22.0

### Changed
- Translations are keyed by the editor's names with their acronyms ("I2C Port",
  "SPI Host" rather than "I 2c Port", "Spi Host"), which the 0.22 editor shows.
- `minEngine` 0.22.0: built against engine 0.22.0. A build of this package
  from before does not load and is rebuilt.

## 0.20.0

### Fixed
- The component display names the translation check had missed are translated into Japanese, Simplified Chinese and Korean.

## 0.19.0

### Changed
- `minEngine` 0.19.0: built against engine 0.19.0, whose component metadata records the package each class is in. A build of this package from before does not load and is rebuilt.

### Added
- Japanese, Simplified Chinese and Korean: `editor/lang/ja.json`, `zh.json`
  and `ko.json` translate the names, tooltips and enum values this
  package's components show in the Inspector.
- The package's editor text goes through the editor's translation (`Tr`), so
  it shows in the editor's language. Needs the editor release that adds
  `Localization.h`; raise `minEngine` to it when releasing.

## 0.18.0

### Changed
- `minEngine` 0.18.0. Reflection ABI 21: the package must be rebuilt.
- **Names follow the code style** (deki-engine/docs/codestyle): types, functions and enum values are PascalCase, constants kPascalCase, members m_PascalCase, locals and parameters camelCase. The code is formatted with clang-format 22.
- The functions the editor finds by name are PascalCase: DekiAudioRegisterComponents, DekiAudioGetAutoComponentCount, DekiAudioEnsureRegistered and the rest. Built against engine ABI 21; a build of this package from before does not load and is rebuilt.

### Fixed
- MAX98357A: after `Stop()` the speaker stayed silent until a restart, because
  playing never turned the I2S channel back on. `PlayPCM` now does.
- MAX98357A: `PlayPCM` copied the whole clip into internal RAM to apply the
  volume, so a long clip ran the board out of memory. It now scales a small
  chunk at a time.
- MAX98357A: initializing twice leaked the first I2S channel, and a channel
  that failed to start was left open.

### Removed
- The former names from before 0.16.0 (bare class names, and deki-gpio's
  `DekiEsp32::ESP32PinSetup`). A scene that old is upgraded with 0.17 first.

## 0.17.0

### Changed
- `minEngine` 0.17.0. Reflection ABI 20: the package must be rebuilt.

## 0.16.0

### Changed
- **Moved into the `DekiAudio` namespace.** Every component was declared at global
  scope, which made its identity a bare class name — the name a scene file
  stores and the name the registry keys on — so two packages defining one name
  collided there with nothing to tell them apart. Each component carries
  `DEKI_FORMER_NAME` with the name it was saved under before, so existing
  scenes load unchanged and are written back qualified on the next save.
  Code naming these types needs the namespace: `using namespace DekiAudio;` or a
  qualified name.
- Enum properties are stored by name rather than by number, so appending to an
  enum or reordering one no longer changes what a saved scene means. Files
  written before this still read.
- `minEngine` 0.16.0. Reflection ABI 17: the package must be rebuilt.

## 0.15.0

### Changed
- No changes of its own. Released alongside engine 0.15.0 so `minEngine`
  tracks the engine version.
