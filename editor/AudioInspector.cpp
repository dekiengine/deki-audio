#ifdef DEKI_EDITOR

// The inspector of an audio file (.wav, .ogg): what the file holds, and what
// each of the project's platforms gets of it (format, sample rate, channels,
// and whether it streams), with the size that comes to.
//
// It draws through EditorUI and the editor's target cards, never ImGui: a
// package DLL has no ImGui context of its own.

#include "AudioCompile.h"
#include "AudioSettings.h"

#include <deki-editor/AssetPipeline.h>
#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorTheme.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/Localization.h>
#include <deki-editor/TargetCards.h>
#include <deki-editor/build/PlatformConfig.h>
#include <deki/LogSystem.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace DekiAudio
{
namespace
{

using DekiEditor::Tr;
using DekiEditor::TrMark;

const char* const kFormatLabels[] = { TrMark("Automatic"), TrMark("PCM 16-bit"), TrMark("ADPCM") };
constexpr int kFormatCount = 3;

const int kRates[] = { 0, 8000, 11025, 16000, 22050, 32000, 44100, 48000 };
const char* const kRateLabels[] = { TrMark("Automatic"), "8000 Hz",  "11025 Hz", "16000 Hz",
                                    "22050 Hz",          "32000 Hz", "44100 Hz", "48000 Hz" };
constexpr int kRateCount = 8;

const char* const kChannelLabels[] = { TrMark("Automatic"), TrMark("Mono"), TrMark("Stereo") };
constexpr int kChannelCount = 3;

const char* const kLoadingLabels[] = { TrMark("Automatic"), TrMark("In Memory"), TrMark("Stream") };
constexpr int kLoadingCount = 3;

int RateIndex(int rate)
{
    for (int i = 0; i < kRateCount; ++i)
    {
        if (kRates[i] == rate)
        {
            return i;
        }
    }
    return 0;
}

std::string SizeText(size_t bytes)
{
    char buf[32];
    if (bytes >= 1024 * 1024)
    {
        std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    }
    return buf;
}

std::string Summary(const ResolvedAudio& r, float seconds)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s, %d Hz, %s%s, %s", Tr(r.codec == AudioCodec::ImaAdpcm ? "ADPCM" : "PCM 16-bit"),
                  r.sampleRate, Tr(r.channels == 2 ? "Stereo" : "Mono"), r.stream ? Tr(", streamed") : "",
                  SizeText(CompiledAudioBytes(r, seconds)).c_str());
    return buf;
}

class AudioInspector : public DekiEditor::FileInspector
{
public:
    const char** GetExtensions() const override
    {
        static const char* s_Extensions[] = { ".wav", ".ogg", ".mp3" };
        return s_Extensions;
    }
    int GetExtensionCount() const override { return 3; }

    void OnInspectorGUI(const std::string& assetPath, const std::string& assetGuid) override
    {
        (void)assetGuid;
        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        if (!pipeline)
        {
            return;
        }
        const std::string projectPath = pipeline->GetProjectPath();
        const std::string absolutePath = (fs::path(projectPath) / assetPath).string();
        std::error_code ec;
        const auto written = fs::last_write_time(absolutePath, ec);
        if (absolutePath != m_Path || written != m_Written)
        {
            m_Path = absolutePath;
            m_Written = written;
            m_Settings = ReadAudioSettings(absolutePath);
            m_Source = ReadAudioSourceInfo(absolutePath);
            m_Open.clear();
        }
        DrawSource();
        if (m_Source.valid)
        {
            DrawTargets(projectPath, assetPath);
        }
    }

private:
    void DrawSource()
    {
        auto& ui = DekiEditor::EditorUI::Get();
        DekiEditor::FullBleedSeparator();
        if (!DekiEditor::SchematicSectionBegin("Audio"))
        {
            return;
        }
        if (!m_Source.valid)
        {
            ui.TextWrapped(Tr("This file cannot be read:"));
            ui.TextWrapped(m_Source.error.c_str());
            DekiEditor::SchematicSectionEnd();
            return;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f s", static_cast<double>(m_Source.seconds));
        ui.PropertyRow(Tr("Length"));
        ui.ValueText(buf);
        std::snprintf(buf, sizeof(buf), "%d Hz", m_Source.sampleRate);
        ui.PropertyRow(Tr("Sample Rate"));
        ui.ValueText(buf);
        ui.PropertyRow(Tr("Channels"));
        ui.ValueText(Tr(m_Source.channels == 2 ? "Stereo" : "Mono"));
        if (ui.IsItemHovered())
        {
            ui.SetTooltip(Tr("What the file holds. Each platform gets its own version, as its card below says."));
        }
        DekiEditor::SchematicSectionEnd();
    }

    void Save(const std::string& assetPath)
    {
        if (!WriteAudioSettings(m_Path, m_Settings))
        {
            DEKI_LOG_ERROR("Audio: cannot write %s.data", m_Path.c_str());
            return;
        }
        std::error_code ec;
        m_Written = fs::last_write_time(m_Path, ec);
        // Builds it again for the editor, with the new settings.
        if (auto* pipeline = DekiEditor::AssetPipeline::Instance())
        {
            pipeline->RefreshAsset(assetPath);
        }
    }

    // The four rows of the Default card, which hold the audio's own values.
    void DrawDefaultRows(const std::string& assetPath, float labelX, float valueX, float valueW)
    {
        auto& ui = DekiEditor::EditorUI::Get();
        AudioLevelSettings& base = m_Settings.base;

        int format = static_cast<int>(base.format);
        DekiEditor::TargetCardRow(TrMark("Format"), labelX, valueX);
        ui.SetNextItemWidth(valueW);
        if (ui.Combo("##Format", &format, kFormatLabels, kFormatCount))
        {
            base.format = static_cast<AudioFormatSetting>(format);
            Save(assetPath);
        }
        if (ui.IsItemHovered())
        {
            ui.SetTooltip(Tr("PCM 16-bit is the sound as recorded. ADPCM is a quarter of the size and sounds close "
                             "to it. Automatic picks PCM on computers and ADPCM on boards."));
        }

        int rate = RateIndex(base.sampleRate);
        DekiEditor::TargetCardRow(TrMark("Sample Rate"), labelX, valueX);
        ui.SetNextItemWidth(valueW);
        if (ui.Combo("##Rate", &rate, kRateLabels, kRateCount))
        {
            base.sampleRate = kRates[rate];
            Save(assetPath);
        }
        if (ui.IsItemHovered())
        {
            ui.SetTooltip(Tr("Samples per second. Lower is smaller and duller. Automatic uses the speaker's rate on "
                             "a board, and never goes above the file's own."));
        }

        int channels = base.channels;
        DekiEditor::TargetCardRow(TrMark("Channels"), labelX, valueX);
        ui.SetNextItemWidth(valueW);
        if (ui.Combo("##Channels", &channels, kChannelLabels, kChannelCount))
        {
            base.channels = channels;
            Save(assetPath);
        }
        if (ui.IsItemHovered())
        {
            ui.SetTooltip(Tr("Mono is half the size of stereo. Automatic keeps the file's channels where the "
                             "speaker has two, and mixes to mono where it has one."));
        }

        int loading = static_cast<int>(base.loading);
        DekiEditor::TargetCardRow(TrMark("Loading"), labelX, valueX);
        ui.SetNextItemWidth(valueW);
        if (ui.Combo("##Loading", &loading, kLoadingLabels, kLoadingCount))
        {
            base.loading = static_cast<AudioLoading>(loading);
            Save(assetPath);
        }
        if (ui.IsItemHovered())
        {
            ui.SetTooltip(Tr("In Memory loads it whole, for short sounds played often. Stream plays it from storage "
                             "a little at a time, for music. Automatic streams audio longer than 10 seconds."));
        }
    }

    void DrawTargets(const std::string& projectPath, const std::string& assetPath)
    {
        if (!DekiEditor::TargetCardsBegin())
        {
            return;
        }
        {
            DekiEditor::TargetCardInfo info;
            info.id = "##default";
            info.name = Tr("Default");
            info.note = Tr("all targets");
            info.nameTip = Tr("What every target uses, unless its own card overrides it");
            info.summary = std::string(Tr(kFormatLabels[static_cast<int>(m_Settings.base.format)])) + ", " +
                           Tr(kRateLabels[RateIndex(m_Settings.base.sampleRate)]);
            DekiEditor::TargetCard(info, m_Open["##default"], [&](float labelX, float valueX, float valueW)
                                   { DrawDefaultRows(assetPath, labelX, valueX, valueW); });
        }

        for (const DekiEditor::PlatformConfig* platform : DekiEditor::ProjectTargets(projectPath))
        {
            DrawTargetCard(*platform, assetPath);
        }
        DekiEditor::TargetCardsEnd();
    }

    void DrawTargetCard(const DekiEditor::PlatformConfig& platform, const std::string& assetPath)
    {
        auto& ui = DekiEditor::EditorUI::Get();
        const std::string& id = platform.id;
        AudioLevelSettings own;
        if (auto it = m_Settings.targets.find(id); it != m_Settings.targets.end())
        {
            own = it->second;
        }
        const ResolvedAudio resolved =
            ResolveAudio(m_Settings, &platform, m_Source.sampleRate, m_Source.channels, m_Source.seconds);

        DekiEditor::TargetCardInfo info;
        info.id = id.c_str();
        info.overridden = !own.Empty();
        info.name = platform.displayName.empty() ? id : platform.displayName;
        info.nameTip = info.overridden ? Tr("Overrides Default") : nullptr;
        info.summary = Summary(resolved, m_Source.seconds);
        auto open = m_Open.find(id);
        if (open == m_Open.end())
        {
            open = m_Open.emplace(id, info.overridden).first;
        }

        char defaultLabel[96];
        auto apply = [&](const AudioLevelSettings& changed)
        {
            if (changed.Empty())
            {
                m_Settings.targets.erase(id);
            }
            else
            {
                m_Settings.targets[id] = changed;
            }
            Save(assetPath);
        };

        DekiEditor::TargetCard(
            info, open->second,
            [&](float labelX, float valueX, float valueW)
            {
                // Index 0 is Default; the lists start at 1, past Automatic.
                int format = static_cast<int>(own.format);
                DekiEditor::TargetCardRow(TrMark("Format"), labelX, valueX);
                std::snprintf(defaultLabel, sizeof(defaultLabel), Tr("Default (%s)"),
                              Tr(resolved.codec == AudioCodec::ImaAdpcm ? "ADPCM" : "PCM 16-bit"));
                if (DekiEditor::TargetOverrideCombo(
                        "##TargetFormat", valueW, Tr(resolved.codec == AudioCodec::ImaAdpcm ? "ADPCM" : "PCM 16-bit"),
                        format == 0, defaultLabel, [](int i) { return kFormatLabels[i]; }, kFormatCount, 1, format))
                {
                    AudioLevelSettings changed = own;
                    changed.format = static_cast<AudioFormatSetting>(format);
                    apply(changed);
                }

                int rate = RateIndex(own.sampleRate);
                char shownRate[32];
                std::snprintf(shownRate, sizeof(shownRate), "%d Hz", resolved.sampleRate);
                DekiEditor::TargetCardRow(TrMark("Sample Rate"), labelX, valueX);
                std::snprintf(defaultLabel, sizeof(defaultLabel), Tr("Default (%s)"), shownRate);
                if (DekiEditor::TargetOverrideCombo(
                        "##TargetRate", valueW, shownRate, rate == 0, defaultLabel,
                        [](int i) { return kRateLabels[i]; }, kRateCount, 1, rate))
                {
                    AudioLevelSettings changed = own;
                    changed.sampleRate = kRates[rate];
                    apply(changed);
                }

                int channels = own.channels;
                const char* shownChannels = Tr(resolved.channels == 2 ? "Stereo" : "Mono");
                DekiEditor::TargetCardRow(TrMark("Channels"), labelX, valueX);
                std::snprintf(defaultLabel, sizeof(defaultLabel), Tr("Default (%s)"), shownChannels);
                if (DekiEditor::TargetOverrideCombo(
                        "##TargetChannels", valueW, shownChannels, channels == 0, defaultLabel,
                        [](int i) { return kChannelLabels[i]; }, kChannelCount, 1, channels))
                {
                    AudioLevelSettings changed = own;
                    changed.channels = channels;
                    apply(changed);
                }

                int loading = static_cast<int>(own.loading);
                const char* shownLoading = Tr(resolved.stream ? "Stream" : "In Memory");
                DekiEditor::TargetCardRow(TrMark("Loading"), labelX, valueX);
                std::snprintf(defaultLabel, sizeof(defaultLabel), Tr("Default (%s)"), shownLoading);
                if (DekiEditor::TargetOverrideCombo(
                        "##TargetLoading", valueW, shownLoading, loading == 0, defaultLabel,
                        [](int i) { return kLoadingLabels[i]; }, kLoadingCount, 1, loading))
                {
                    AudioLevelSettings changed = own;
                    changed.loading = static_cast<AudioLoading>(loading);
                    apply(changed);
                }

                DekiEditor::TargetCardRow(TrMark("Size"), labelX, valueX);
                ui.ValueText(SizeText(CompiledAudioBytes(resolved, m_Source.seconds)).c_str());
            });
    }

    std::string m_Path;
    fs::file_time_type m_Written{};
    AudioSettings m_Settings;
    AudioSourceInfo m_Source;
    std::map<std::string, bool> m_Open;
};

}  // namespace
}  // namespace DekiAudio

namespace DekiEditor
{
void RegisterAudioInspector()
{
    static DekiAudio::AudioInspector s_Inspector;
    FileInspectorRegistry::Instance().Register(&s_Inspector);
}
}  // namespace DekiEditor

#endif  // DEKI_EDITOR
