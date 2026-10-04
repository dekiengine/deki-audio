// Registers audio source files (.wav/.mp3/.ogg) with AssetTypeRegistry: a
// type name and its extensions, as deki-2d and deki-tilemap do. Nothing
// compiles audio yet; the registration makes file discovery
// (AssetPipeline::IsAssetFile) list these files as "Audio" assets, since the
// engine knows no extensions of its own.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>
#include <deki-editor/AssetTypeRegistry.h>

// Editor extensions live in DekiEditor; the package's own types are in DekiAudio.
using namespace DekiAudio;

namespace DekiEditor
{

class AudioAssetType : public AssetTypeEditor
{
public:
    const char* GetTypeName() const override { return "Audio"; }
    const char* GetDisplayName() const override { return "Audio"; }
    std::vector<std::string> GetExtensions() const override { return { ".wav", ".mp3", ".ogg" }; }
};

REGISTER_EDITOR(AudioAssetType)

namespace
{
struct AudioCategoryRegistrar
{
    AudioCategoryRegistrar()
    {
        auto& reg = AssetTypeRegistry::Instance();
        for (const char* ext : { ".wav", ".mp3", ".ogg" })
        {
            reg.RegisterCategory(ext, AssetCategory::Audio);
        }
    }
};
static AudioCategoryRegistrar s_AudioCategoryRegistrar;
}  // namespace

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
