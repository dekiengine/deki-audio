#pragma once

/// Registers the Audio loader and hooks the mixer into the engine's update,
/// which reads streamed audio and frees the voices that ended.
///
/// Called from DekiInitPackageSystems() on firmware and static builds and from
/// DekiPluginInit() when the package is loaded as a DLL. Safe to call more
/// than once.
///
/// At global scope, as every package's init functions are: the editor
/// generates a file that declares them as plain `extern void
/// DekiAudioInitSystem();`, which cannot know the package's namespace.
void DekiAudioInitSystem();
void DekiAudioShutdownSystem();
