/**
 * @file AudioAssetType.cpp
 * @brief AssetTypeRegistry binding for audio source files (.wav/.mp3/.ogg).
 *
 * Pure type-name → extension mapping, mirroring deki-2d / deki-tilemap. There
 * is no audio compile pipeline yet — these files are discovered and classified
 * as "Audio" assets, but no consumer in the current editor actually processes
 * them. Adding this registration so file discovery (AssetPipeline::IsAssetFile)
 * doesn't drop them when the engine no longer hardcodes extensions.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>
#include <deki-editor/AssetTypeRegistry.h>

namespace DekiEditor
{

class AudioAssetType : public AssetTypeEditor
{
public:
    const char* GetTypeName() const override    { return "Audio"; }
    const char* GetDisplayName() const override { return "Audio"; }
    std::vector<std::string> GetExtensions() const override
    {
        return { ".wav", ".mp3", ".ogg" };
    }
};

REGISTER_EDITOR(AudioAssetType)

namespace {
struct AudioCategoryRegistrar
{
    AudioCategoryRegistrar()
    {
        auto& reg = AssetTypeRegistry::Instance();
        for (const char* ext : {".wav", ".mp3", ".ogg"})
            reg.RegisterCategory(ext, AssetCategory::Audio);
    }
};
static AudioCategoryRegistrar s_AudioCategoryRegistrar;
}

} // namespace DekiEditor

#endif // DEKI_EDITOR
