// Audio files (.wav, .ogg) as assets: the "Audio" type, the editor's cache of
// each one for the active platform, and the version each build ships
// (AudioCompile.h).

#ifdef DEKI_EDITOR

#include "AudioCompile.h"

#include <deki-editor/AssetPipeline.h>
#include <deki-editor/AssetTypeRegistry.h>
#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>
#include <deki-editor/Localization.h>
#include <deki-editor/build/PlatformConfig.h>
#include <deki/LogSystem.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

// Editor extensions live in DekiEditor; the package's own types are in DekiAudio.
using namespace DekiAudio;

namespace fs = std::filesystem;

namespace DekiEditor
{

void RegisterAudioInspector();  // AudioInspector.cpp

namespace
{

constexpr const char* kAudioExtensions[] = { ".wav", ".ogg", ".mp3" };

// Bump when CompileAudio writes a file differently, so every cache is made
// again.
constexpr int kAudioCompileVersion = 1;

class AudioAssetType : public AssetTypeEditor
{
public:
    const char* GetTypeName() const override { return "Audio"; }
    const char* GetDisplayName() const override { return TrMark("Audio"); }
    std::vector<std::string> GetExtensions() const override { return { ".wav", ".mp3", ".ogg" }; }
};

REGISTER_EDITOR(AudioAssetType)

const PlatformConfig* PlatformFor(const AssetExportTarget& target)
{
    return target.platformId.empty() ? nullptr : PlatformRegistry::Instance().GetPlatform(target.platformId);
}

// What the editor's cache of an audio was made from: the target and the
// settings. A cache whose stamp differs is made again.
std::string CacheStamp(const std::string& absolutePath, const AssetExportTarget& target)
{
    std::ifstream in(absolutePath + ".data");
    std::string settings;
    if (in)
    {
        try
        {
            const nlohmann::json data = nlohmann::json::parse(in);
            if (data.contains("settings") && data["settings"].contains("audio"))
            {
                settings = data["settings"]["audio"].dump();
            }
        }
        catch (const nlohmann::json::exception&)
        {
        }
    }
    return std::to_string(kAudioCompileVersion) + "|" + target.platformId + "|" + settings;
}

std::string ReadText(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

AssetCacheResult HandleAudioCache(const AssetCacheContext& ctx)
{
    const AssetExportTarget& target = ctx.pipeline->GetEditorTarget();
    const std::string stampPath = ctx.cachePath + ".stamp";
    const std::string stamp = CacheStamp(ctx.absolutePath, target);
    if (ctx.hasCachedVersion && ctx.cacheIsFresh && ReadText(stampPath) == stamp)
    {
        return AssetCacheResult::Cached;
    }

    const std::string name = fs::path(ctx.relativePath).filename().string();
    std::string why;
    std::vector<std::string> warnings;
    if (!CompileAudio(ctx.absolutePath, PlatformFor(target), ctx.cachePath, name, why, warnings))
    {
        DEKI_LOG_WARNING("Audio: %s", why.c_str());
        std::error_code ec;
        fs::remove(ctx.cachePath, ec);
        fs::remove(stampPath, ec);
        return AssetCacheResult::NotCached;
    }
    for (const std::string& warning : warnings)
    {
        DEKI_LOG_WARNING("Audio: %s", warning.c_str());
    }
    std::ofstream(stampPath, std::ios::trunc) << stamp;
    return AssetCacheResult::Cached;
}

bool EncodeAudioForTarget(const AssetExportContext& ctx)
{
    return CompileAudio(ctx.absolutePath, PlatformFor(ctx.target), ctx.outPath, ctx.relativePath, *ctx.error,
                        *ctx.warnings);
}

// A static initialiser, not work in the package entry point, so the vtable of
// the AssetTypeEditor above stays stable across package rebuilds (see the
// note in EditorExtension.h).
struct AudioAssetRegistrar
{
    AudioAssetRegistrar()
    {
        auto& reg = AssetTypeRegistry::Instance();
        for (const char* ext : kAudioExtensions)
        {
            reg.RegisterCategory(ext, AssetCategory::Audio);
        }
        RegisterAudioInspector();
        AssetPipeline::OnStarted(
            [](AssetPipeline* pipeline)
            {
                for (const char* ext : kAudioExtensions)
                {
                    pipeline->RegisterCacheHandler(ext, HandleAudioCache);
                    pipeline->RegisterExportEncoder(ext, EncodeAudioForTarget);
                }
            });
    }
};
static AudioAssetRegistrar s_AudioAssetRegistrar;

}  // namespace

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
