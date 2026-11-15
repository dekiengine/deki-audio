// The editor's audio import: reading WAV files, changing channels and rate,
// the per-target settings and what Automatic picks, and the compile from a
// file on disk to a .daud the runtime loads.

#include <gtest/gtest.h>

#include "Audio.h"
#include "editor/AudioCompile.h"
#include "editor/AudioConvert.h"
#include "editor/AudioDecode.h"
#include "editor/AudioSettings.h"
#include <deki-editor/build/PlatformConfig.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace DekiAudio;
namespace fs = std::filesystem;

namespace
{

void PutU16(std::vector<uint8_t>& v, uint16_t x)
{
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

void PutU32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; ++i)
    {
        v.push_back(static_cast<uint8_t>(x >> (8 * i)));
    }
}

void PutTag(std::vector<uint8_t>& v, const char* tag)
{
    v.insert(v.end(), tag, tag + 4);
}

// A WAV file with `data` as its sample bytes.
std::vector<uint8_t> Wav(uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits,
                         const std::vector<uint8_t>& data, bool extensible = false)
{
    std::vector<uint8_t> f;
    PutTag(f, "RIFF");
    PutU32(f, 0);
    PutTag(f, "WAVE");
    // A chunk the reader must step over.
    PutTag(f, "LIST");
    PutU32(f, 3);
    f.insert(f.end(), { 'a', 'b', 'c', 0 });
    PutTag(f, "fmt ");
    PutU32(f, extensible ? 40 : 16);
    PutU16(f, extensible ? 0xFFFE : format);
    PutU16(f, channels);
    PutU32(f, rate);
    PutU32(f, rate * channels * bits / 8);
    PutU16(f, static_cast<uint16_t>(channels * bits / 8));
    PutU16(f, bits);
    if (extensible)
    {
        PutU16(f, 22);
        PutU16(f, bits);
        PutU32(f, 3);
        PutU16(f, format);
        f.insert(f.end(), 14, 0);
    }
    PutTag(f, "data");
    PutU32(f, static_cast<uint32_t>(data.size()));
    f.insert(f.end(), data.begin(), data.end());
    return f;
}

std::vector<uint8_t> Pcm16Bytes(const std::vector<int16_t>& samples)
{
    std::vector<uint8_t> v;
    for (int16_t s : samples)
    {
        PutU16(v, static_cast<uint16_t>(s));
    }
    return v;
}

DecodedAudio Sine(double hz, int rate, double seconds, float amplitude = 0.5f)
{
    DecodedAudio a;
    a.sampleRate = rate;
    a.channels = 1;
    const size_t n = static_cast<size_t>(rate * seconds);
    a.samples.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        a.samples[i] = amplitude * static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * i / rate));
    }
    return a;
}

double Rms(const std::vector<float>& v, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        sum += v[i] * v[i];
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

DekiEditor::PlatformConfig Board()
{
    DekiEditor::PlatformConfig p;
    p.id = "test_board";
    p.provides = { "filesystem" };
    p.SetOptionU32("audioSampleRate", 22050);
    p.SetOptionU32("audioChannels", 1);
    return p;
}

DekiEditor::PlatformConfig Desktop()
{
    DekiEditor::PlatformConfig p;
    p.id = "test_desktop";
    p.provides = { "hosted" };
    return p;
}

struct TempDir
{
    fs::path path = fs::temp_directory_path() / "deki-audio-import-test";
    TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string Write(const std::string& name, const std::vector<uint8_t>& bytes) const
    {
        const fs::path p = path / name;
        std::ofstream(p, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return p.string();
    }
};

std::vector<uint8_t> ReadAll(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

// ---------------------------------------------------------------------------
// WAV

TEST(AudioImport, ReadsSixteenBitStereo)
{
    DecodedAudio a;
    std::string error;
    ASSERT_TRUE(DecodeWav(Wav(1, 2, 44100, 16, Pcm16Bytes({ 16384, -16384, 0, 32767 })), a, error)) << error;
    EXPECT_EQ(a.sampleRate, 44100);
    EXPECT_EQ(a.channels, 2);
    ASSERT_EQ(a.Frames(), 2u);
    EXPECT_FLOAT_EQ(a.samples[0], 0.5f);
    EXPECT_FLOAT_EQ(a.samples[1], -0.5f);
    EXPECT_FLOAT_EQ(a.samples[2], 0.0f);
}

TEST(AudioImport, ReadsEightTwentyFourAndFloat)
{
    DecodedAudio a;
    std::string error;
    ASSERT_TRUE(DecodeWav(Wav(1, 1, 8000, 8, { 128, 192, 64 }), a, error));
    EXPECT_FLOAT_EQ(a.samples[0], 0.0f);
    EXPECT_FLOAT_EQ(a.samples[1], 0.5f);
    EXPECT_FLOAT_EQ(a.samples[2], -0.5f);

    ASSERT_TRUE(DecodeWav(Wav(1, 1, 48000, 24, { 0x00, 0x00, 0x40, 0x00, 0x00, 0xC0 }), a, error));
    EXPECT_FLOAT_EQ(a.samples[0], 0.5f);
    EXPECT_FLOAT_EQ(a.samples[1], -0.5f);

    const float values[] = { 0.25f, -1.0f };
    std::vector<uint8_t> bytes(sizeof(values));
    std::memcpy(bytes.data(), values, sizeof(values));
    ASSERT_TRUE(DecodeWav(Wav(3, 1, 22050, 32, bytes, true), a, error)) << error;
    EXPECT_FLOAT_EQ(a.samples[0], 0.25f);
    EXPECT_FLOAT_EQ(a.samples[1], -1.0f);
}

TEST(AudioImport, CompressedAndBrokenWavsAreRefused)
{
    DecodedAudio a;
    std::string error;
    EXPECT_FALSE(DecodeWav(Wav(2, 1, 22050, 4, { 1, 2, 3 }), a, error));  // MS ADPCM
    EXPECT_NE(error.find("16-bit"), std::string::npos);
    EXPECT_FALSE(DecodeWav({ 'R', 'I', 'F', 'F' }, a, error));
    std::vector<uint8_t> noData = Wav(1, 1, 22050, 16, {});
    noData.resize(noData.size() - 8);  // drops the data chunk's header
    EXPECT_FALSE(DecodeWav(noData, a, error));
}

// ---------------------------------------------------------------------------
// Converting

TEST(AudioImport, ChannelsMixDownAndSpreadUp)
{
    DecodedAudio a;
    a.sampleRate = 8000;
    a.channels = 2;
    a.samples = { 0.5f, 0.1f, -0.2f, -0.4f };
    ConvertChannels(a, 1);
    ASSERT_EQ(a.channels, 1);
    EXPECT_FLOAT_EQ(a.samples[0], 0.3f);
    EXPECT_FLOAT_EQ(a.samples[1], -0.3f);
    ConvertChannels(a, 2);
    EXPECT_EQ(a.samples, (std::vector<float>{ 0.3f, 0.3f, -0.3f, -0.3f }));
}

TEST(AudioImport, ResamplingKeepsATone)
{
    DecodedAudio a = Sine(1000.0, 44100, 0.5);
    const double before = Rms(a.samples, 1000, a.samples.size() - 1000);
    Resample(a, 22050);
    EXPECT_EQ(a.sampleRate, 22050);
    EXPECT_NEAR(static_cast<double>(a.Frames()), 22050 * 0.5, 1.0);
    EXPECT_NEAR(Rms(a.samples, 500, a.samples.size() - 500), before, before * 0.02);

    // Still 1 kHz: 2 zero crossings a period.
    int crossings = 0;
    for (size_t i = 501; i < a.samples.size() - 500; ++i)
    {
        crossings += (a.samples[i - 1] < 0.0f) != (a.samples[i] < 0.0f);
    }
    const double seconds = (a.samples.size() - 1001) / 22050.0;
    EXPECT_NEAR(crossings / seconds, 2000.0, 20.0);
}

TEST(AudioImport, ResamplingDownRemovesWhatTheLowerRateCannotHold)
{
    // 15 kHz is above 22050's Nyquist (11025): left in, it would fold to 7 kHz.
    DecodedAudio a = Sine(15000.0, 44100, 0.25);
    Resample(a, 22050);
    EXPECT_LT(Rms(a.samples, 500, a.samples.size() - 500), 0.005);
}

TEST(AudioImport, ResamplingUpInterpolates)
{
    DecodedAudio a = Sine(440.0, 11025, 0.25);
    Resample(a, 44100);
    EXPECT_NEAR(Rms(a.samples, 2000, a.samples.size() - 2000), 0.5 / std::sqrt(2.0), 0.01);
}

// ---------------------------------------------------------------------------
// Settings and Automatic

TEST(AudioImport, AutomaticSuitsTheTarget)
{
    const AudioSettings settings;
    const DekiEditor::PlatformConfig board = Board();
    const DekiEditor::PlatformConfig desktop = Desktop();

    ResolvedAudio r = ResolveAudio(settings, &desktop, 44100, 2, 3.0f);
    EXPECT_EQ(r.codec, AudioCodec::Pcm16);
    EXPECT_EQ(r.sampleRate, 44100);
    EXPECT_EQ(r.channels, 2);
    EXPECT_FALSE(r.stream);

    r = ResolveAudio(settings, &desktop, 96000, 1, 3.0f);
    EXPECT_EQ(r.sampleRate, 48000);

    r = ResolveAudio(settings, &board, 44100, 2, 3.0f);
    EXPECT_EQ(r.codec, AudioCodec::ImaAdpcm);
    EXPECT_EQ(r.sampleRate, 22050);
    EXPECT_EQ(r.channels, 1);

    // Never above the file's own rate.
    r = ResolveAudio(settings, &board, 11025, 1, 3.0f);
    EXPECT_EQ(r.sampleRate, 11025);

    // Long audio streams.
    r = ResolveAudio(settings, &board, 22050, 1, 60.0f);
    EXPECT_TRUE(r.stream);

    // No platform: the editor, as a computer.
    r = ResolveAudio(settings, nullptr, 22050, 2, 1.0f);
    EXPECT_EQ(r.codec, AudioCodec::Pcm16);
    EXPECT_EQ(r.channels, 2);
}

TEST(AudioImport, ATargetsOwnSettingWinsOverTheAudios)
{
    AudioSettings settings;
    settings.base.format = AudioFormatSetting::Pcm16;
    settings.base.sampleRate = 16000;
    settings.base.loading = AudioLoading::Stream;
    AudioLevelSettings own;
    own.format = AudioFormatSetting::Adpcm;
    own.channels = 2;
    settings.targets["test_board"] = own;

    const DekiEditor::PlatformConfig board = Board();
    const DekiEditor::PlatformConfig desktop = Desktop();
    ResolvedAudio r = ResolveAudio(settings, &board, 44100, 2, 1.0f);
    EXPECT_EQ(r.codec, AudioCodec::ImaAdpcm);
    EXPECT_EQ(r.sampleRate, 16000);
    EXPECT_EQ(r.channels, 2);
    EXPECT_TRUE(r.stream);

    r = ResolveAudio(settings, &desktop, 44100, 2, 1.0f);
    EXPECT_EQ(r.codec, AudioCodec::Pcm16);
    EXPECT_EQ(r.sampleRate, 16000);
}

TEST(AudioImport, SettingsReadBackAsWritten)
{
    TempDir dir;
    const std::string path = dir.Write("a.wav", {});
    // Something else already in the sidecar stays.
    std::ofstream(path + ".data") << R"({"guid":"abc","settings":{"other":{"x":1}}})";

    AudioSettings settings;
    settings.base.format = AudioFormatSetting::Adpcm;
    settings.base.sampleRate = 32000;
    AudioLevelSettings own;
    own.loading = AudioLoading::Memory;
    own.channels = 1;
    settings.targets["lilygo_t_deck"] = own;
    ASSERT_TRUE(WriteAudioSettings(path, settings));

    const AudioSettings back = ReadAudioSettings(path);
    EXPECT_EQ(back.base.format, AudioFormatSetting::Adpcm);
    EXPECT_EQ(back.base.sampleRate, 32000);
    EXPECT_EQ(back.base.channels, 0);
    ASSERT_EQ(back.targets.count("lilygo_t_deck"), 1u);
    EXPECT_EQ(back.targets.at("lilygo_t_deck").loading, AudioLoading::Memory);
    EXPECT_EQ(back.targets.at("lilygo_t_deck").channels, 1);

    std::ifstream in(path + ".data");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("\"guid\""), std::string::npos);
    EXPECT_NE(text.find("\"other\""), std::string::npos);
}

TEST(AudioImport, TheSizeMatchesTheCompiledFile)
{
    ResolvedAudio r;
    r.codec = AudioCodec::ImaAdpcm;
    r.sampleRate = 22050;
    r.channels = 1;
    // 2 s at 22050 is 44100 frames: 88 ADPCM blocks of 505.
    EXPECT_EQ(CompiledAudioBytes(r, 2.0f), kAudioHeaderBytes + 88u * 256u);
}

// ---------------------------------------------------------------------------
// Compiling

TEST(AudioImport, AWavCompilesForABoardAndLoads)
{
    TempDir dir;
    std::vector<int16_t> stereo;
    for (int i = 0; i < 44100; ++i)  // 1 s at 44100
    {
        const int16_t s = static_cast<int16_t>(10000.0 * std::sin(i * 0.05));
        stereo.push_back(s);
        stereo.push_back(s);
    }
    const std::string source = dir.Write("hit.wav", Wav(1, 2, 44100, 16, Pcm16Bytes(stereo)));
    const std::string out = (dir.path / "hit.daud").string();
    const DekiEditor::PlatformConfig board = Board();
    std::string why;
    std::vector<std::string> warnings;
    ASSERT_TRUE(CompileAudio(source, &board, out, "hit.wav", why, warnings)) << why;
    EXPECT_TRUE(warnings.empty());

    const std::vector<uint8_t> file = ReadAll(out);
    std::unique_ptr<Audio> audio(Audio::LoadFromMemory(file.data(), file.size()));
    ASSERT_TRUE(audio);
    EXPECT_EQ(audio->GetInfo().codec, AudioCodec::ImaAdpcm);
    EXPECT_EQ(audio->GetInfo().sampleRate, 22050u);
    EXPECT_EQ(audio->GetInfo().channels, 1);
    EXPECT_NEAR(audio->GetInfo().frameCount, 22050u, 1u);
    EXPECT_FALSE(audio->IsStreamed());

    // The same file for a computer keeps its rate and channels.
    const DekiEditor::PlatformConfig desktop = Desktop();
    ASSERT_TRUE(CompileAudio(source, &desktop, out, "hit.wav", why, warnings)) << why;
    const std::vector<uint8_t> pcm = ReadAll(out);
    std::unique_ptr<Audio> full(Audio::LoadFromMemory(pcm.data(), pcm.size()));
    ASSERT_TRUE(full);
    EXPECT_EQ(full->GetInfo().codec, AudioCodec::Pcm16);
    EXPECT_EQ(full->GetInfo().sampleRate, 44100u);
    EXPECT_EQ(full->GetInfo().channels, 2);
    EXPECT_EQ(full->GetInfo().frameCount, 44100u);
}

TEST(AudioImport, AFileThatCannotBeReadSaysWhy)
{
    TempDir dir;
    const std::string source = dir.Write("bad.wav", { 'n', 'o', 'p', 'e' });
    std::string why;
    std::vector<std::string> warnings;
    EXPECT_FALSE(CompileAudio(source, nullptr, (dir.path / "x").string(), "bad.wav", why, warnings));
    EXPECT_NE(why.find("bad.wav"), std::string::npos);

    const std::string mp3 = dir.Write("song.mp3", { 'I', 'D', '3' });
    EXPECT_FALSE(CompileAudio(mp3, nullptr, (dir.path / "y").string(), "song.mp3", why, warnings));
    EXPECT_NE(why.find("MP3"), std::string::npos);
}
