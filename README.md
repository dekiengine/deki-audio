# Deki Audio

Docs: https://dekiengine.github.io/deki-audio/ (components and properties, generated from the code)

Audio playback peripheral interface for the Deki Engine, with a MAX98357 (I²S) backend.

Part of [Deki Engine](https://github.com/dekiengine/deki-engine).

## Namespace

Types live in `DekiAudio`. Scene files store the qualified name, and so does code:

```cpp
using namespace DekiAudio;
obj->AddComponent<SomeComponent>();
```

Scenes saved before 0.16.0 used bare names and still load; saving writes the current one.

## Dependencies

| Dependency | Type |
|---|---|
| `deki-i2s` | Deki package |

## Install

Package Manager in the Deki Editor, or `DekiEditor --packages-add deki-audio <project>`.

## License

Apache 2.0. See [LICENSE](LICENSE).
