// The audio core: the .daud header, IMA ADPCM, and the mixer (volume, pan,
// pitch and resampling, loops, pausing, voice stealing, stale handles,
// saturation) for audio in memory and audio streamed from a file system.

#include <gtest/gtest.h>

#include "Audio.h"
#include "AudioFile.h"
#include "AudioOutput.h"
#include "DekiAudio.h"
#include "IDekiAudio.h"
#include "ImaAdpcm.h"
#include "Mixer.h"
#include <deki/providers/FileSystem.h>

#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace DekiAudio;

namespace
{

// A .daud file in memory, built as the editor builds one.
std::vector<uint8_t> MakeFile(const std::vector<int16_t>& samples, uint8_t channels, uint32_t rate, AudioCodec codec,
                              bool stream = false)
{
    AudioFileInfo info;
    info.codec = codec;
    info.channels = channels;
    info.sampleRate = rate;
    info.frameCount = static_cast<uint32_t>(samples.size() / channels);
    info.flags = stream ? kAudioFlagStream : 0;
    info.loopEnd = info.frameCount;
    AudioBlockLayout(codec, channels, info.blockFrames, info.blockBytes);

    std::vector<uint8_t> file(kAudioHeaderBytes + info.DataBytes(), 0);
    WriteAudioHeader(info, file.data());
    uint8_t* data = file.data() + kAudioHeaderBytes;
    if (codec == AudioCodec::ImaAdpcm)
    {
        EncodeAdpcmBlocks(samples.data(), info.frameCount, channels, data);
    }
    else
    {
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const uint16_t v = static_cast<uint16_t>(samples[i]);
            data[i * 2] = static_cast<uint8_t>(v);
            data[i * 2 + 1] = static_cast<uint8_t>(v >> 8);
        }
    }
    return file;
}

std::unique_ptr<Audio> MakeAudio(const std::vector<int16_t>& samples, uint8_t channels = 1, uint32_t rate = 22050,
                                 AudioCodec codec = AudioCodec::Pcm16)
{
    const std::vector<uint8_t> file = MakeFile(samples, channels, rate, codec);
    return std::unique_ptr<Audio>(Audio::LoadFromMemory(file.data(), file.size()));
}

std::vector<int16_t> Ramp(size_t count, int start = 1)
{
    std::vector<int16_t> v(count);
    for (size_t i = 0; i < count; ++i)
    {
        v[i] = static_cast<int16_t>(start + static_cast<int>(i));
    }
    return v;
}

std::vector<int16_t> Render(size_t frames, int channels)
{
    std::vector<int16_t> out(frames * static_cast<size_t>(channels));
    Mixer::Instance().Render(out.data(), frames);
    return out;
}

// Files held in memory under "T:/".
class MemoryFileSystem : public Deki::IFileSystem
{
public:
    std::map<std::string, std::vector<uint8_t>> files;

    struct Open
    {
        const std::vector<uint8_t>* data;
        long position;
    };

    bool Initialize() override { return true; }
    void Shutdown() override {}
    FileHandle OpenFile(const char* path, OpenMode) override
    {
        auto it = files.find(path);
        return it == files.end() ? nullptr : new Open{ &it->second, 0 };
    }
    void CloseFile(FileHandle handle) override { delete static_cast<Open*>(handle); }
    size_t ReadFile(FileHandle handle, void* buffer, size_t size) override
    {
        auto* f = static_cast<Open*>(handle);
        const size_t left = f->data->size() - static_cast<size_t>(f->position);
        const size_t n = size < left ? size : left;
        std::memcpy(buffer, f->data->data() + f->position, n);
        f->position += static_cast<long>(n);
        ++reads;
        return n;
    }
    size_t WriteFile(FileHandle, const void*, size_t) override { return 0; }
    long SeekFile(FileHandle handle, long offset, SeekOrigin) override
    {
        static_cast<Open*>(handle)->position = offset;
        return offset;
    }
    long TellFile(FileHandle handle) override { return static_cast<Open*>(handle)->position; }
    long GetFileSize(FileHandle handle) override { return static_cast<long>(static_cast<Open*>(handle)->data->size()); }
    bool FileExists(const char* path) override { return files.count(path) != 0; }
    bool ConvertPath(const char*, char*, size_t) override { return false; }

    int reads = 0;
};

class MixerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Mixer::Instance().Reset();
        Mixer::Instance().SetMasterVolume(1.0f);
        Mixer::Instance().SetOutputFormat(22050, 1);
        Deki::FileSystem::RegisterFileSystem("T:/", &fs);
    }
    void TearDown() override
    {
        Mixer::Instance().Reset();
        Mixer::Instance().SetOutputFormat(0, 0);
        Deki::FileSystem::UnregisterFileSystem("T:/");
    }

    MemoryFileSystem fs;
};

}  // namespace

// ---------------------------------------------------------------------------
// The file header

TEST(AudioFile, TheHeaderReadsBackAsWritten)
{
    AudioFileInfo info;
    info.codec = AudioCodec::ImaAdpcm;
    info.channels = 2;
    info.sampleRate = 44100;
    info.frameCount = 12345;
    info.flags = kAudioFlagStream;
    info.loopStart = 10;
    info.loopEnd = 12000;
    AudioBlockLayout(info.codec, info.channels, info.blockFrames, info.blockBytes);
    uint8_t header[kAudioHeaderBytes];
    WriteAudioHeader(info, header);

    AudioFileInfo back;
    ASSERT_TRUE(ParseAudioHeader(header, sizeof(header), back));
    EXPECT_EQ(back.codec, AudioCodec::ImaAdpcm);
    EXPECT_EQ(back.channels, 2);
    EXPECT_EQ(back.sampleRate, 44100u);
    EXPECT_EQ(back.frameCount, 12345u);
    EXPECT_EQ(back.blockFrames, kAdpcmBlockFrames);
    EXPECT_EQ(back.blockBytes, 512);
    EXPECT_TRUE(back.IsStreamed());
    EXPECT_EQ(back.loopStart, 10u);
    EXPECT_EQ(back.loopEnd, 12000u);
    EXPECT_EQ(back.BlockCount(), (12345u + kAdpcmBlockFrames - 1) / kAdpcmBlockFrames);
}

TEST(AudioFile, DamagedHeadersAreRefused)
{
    const std::vector<uint8_t> good = MakeFile(Ramp(10), 1, 22050, AudioCodec::Pcm16);
    AudioFileInfo info;
    ASSERT_TRUE(ParseAudioHeader(good.data(), good.size(), info));

    auto broken = [&](size_t at, uint8_t value)
    {
        std::vector<uint8_t> f = good;
        f[at] = value;
        AudioFileInfo out;
        return !ParseAudioHeader(f.data(), f.size(), out);
    };
    EXPECT_TRUE(broken(0, 'X'));   // magic
    EXPECT_TRUE(broken(4, 9));     // version
    EXPECT_TRUE(broken(6, 7));     // codec
    EXPECT_TRUE(broken(7, 3));     // channels
    EXPECT_TRUE(broken(16, 1));    // frames per block
    EXPECT_TRUE(broken(28, 200));  // loop end past the end
    EXPECT_FALSE(ParseAudioHeader(good.data(), kAudioHeaderBytes - 1, info));
    EXPECT_FALSE(ParseAudioHeader(nullptr, 64, info));
}

TEST(AudioFile, AShortFileDoesNotLoad)
{
    std::vector<uint8_t> f = MakeFile(Ramp(600), 1, 22050, AudioCodec::Pcm16);
    f.resize(f.size() - 1);
    EXPECT_EQ(Audio::LoadFromMemory(f.data(), f.size()), nullptr);
}

// ---------------------------------------------------------------------------
// IMA ADPCM

TEST(ImaAdpcm, ASineSurvivesTheRoundTrip)
{
    const size_t frames = 4000;
    std::vector<int16_t> sine(frames);
    for (size_t i = 0; i < frames; ++i)
    {
        sine[i] = static_cast<int16_t>(12000.0 * std::sin(i * 2.0 * 3.14159265 * 440.0 / 22050.0));
    }
    const std::vector<uint8_t> file = MakeFile(sine, 1, 22050, AudioCodec::ImaAdpcm);
    std::unique_ptr<Audio> audio(Audio::LoadFromMemory(file.data(), file.size()));
    ASSERT_TRUE(audio);
    EXPECT_EQ(file.size(), kAudioHeaderBytes + audio->GetInfo().BlockCount() * 256u);

    // Decoded through the mixer at the audio's own rate.
    Mixer::Instance().Reset();
    Mixer::Instance().SetOutputFormat(22050, 1);
    ASSERT_NE(Mixer::Instance().Play(audio.get()), kNoAudio);
    const std::vector<int16_t> out = Render(frames, 1);
    double error = 0.0;
    double signal = 0.0;
    for (size_t i = 0; i < frames; ++i)
    {
        error += std::pow(out[i] - sine[i], 2.0);
        signal += std::pow(static_cast<double>(sine[i]), 2.0);
    }
    // About 30 dB of signal to noise, which is what 4-bit ADPCM gives.
    EXPECT_GT(10.0 * std::log10(signal / error), 28.0);
    Mixer::Instance().Reset();
    Mixer::Instance().SetOutputFormat(0, 0);
}

// ---------------------------------------------------------------------------
// The mixer

TEST_F(MixerTest, NothingPlaysWithoutAnOutput)
{
    auto audio = MakeAudio(Ramp(10));
    Mixer::Instance().SetOutputFormat(0, 0);
    EXPECT_EQ(Mixer::Instance().Play(audio.get()), kNoAudio);
    EXPECT_EQ(Mixer::Instance().Play(nullptr), kNoAudio);
}

TEST_F(MixerTest, AudioAtTheOutputsRatePlaysSampleForSample)
{
    auto audio = MakeAudio(Ramp(600));
    const AudioHandle h = Mixer::Instance().Play(audio.get());
    ASSERT_NE(h, kNoAudio);
    EXPECT_TRUE(Mixer::Instance().IsPlaying(h));
    const std::vector<int16_t> out = Render(600, 1);
    EXPECT_EQ(out, Ramp(600));

    // Then the voice is done.
    Render(4, 1);
    EXPECT_FALSE(Mixer::Instance().IsPlaying(h));
    EXPECT_EQ(Render(8, 1), std::vector<int16_t>(8, 0));
}

TEST_F(MixerTest, VolumeAndMasterVolumeScale)
{
    auto audio = MakeAudio(std::vector<int16_t>(100, 10000));
    PlayOptions options;
    options.volume = 0.5f;
    Mixer::Instance().Play(audio.get(), options);
    EXPECT_NEAR(Render(1, 1)[0], 5000, 1);
    Mixer::Instance().SetMasterVolume(0.5f);
    EXPECT_NEAR(Render(1, 1)[0], 2500, 1);
}

// An output whose device has its own volume, as Tactility's does.
class DeviceVolumeOutput : public IDekiAudio
{
public:
    float deviceVolume = 0.4f;

    const char* GetPackageId() const override { return "audio"; }
    const char* GetPackageName() const override { return "Device Volume Output"; }
    void Configure(const Deki::PackageConfig&) override {}
    bool Initialize() override { return true; }
    void Shutdown() override {}
    void Update(float) override {}
    Deki::PackageState GetState() const override { return Deki::PackageState::Running; }
    const char* GetLastError() const override { return ""; }
    int GetSampleRate() const override { return 22050; }
    int GetChannels() const override { return 1; }
    bool Start() override { return true; }
    void Stop() override {}
    bool IsRunning() const override { return true; }
    bool IsHardwareConnected() const override { return true; }
    bool HasDeviceVolume() const override { return true; }
    float GetDeviceVolume() const override { return deviceVolume; }
    void SetDeviceVolume(float volume) override { deviceVolume = volume; }
};

TEST_F(MixerTest, TheMasterVolumeIsTheDevicesWhenItHasOne)
{
    DeviceVolumeOutput output;
    AudioOutput::SetCurrent(&output);
    EXPECT_FLOAT_EQ(GetMasterVolume(), 0.4f);

    SetMasterVolume(0.7f);
    EXPECT_FLOAT_EQ(output.deviceVolume, 0.7f);
    EXPECT_FLOAT_EQ(GetMasterVolume(), 0.7f);
    // The mixer plays at full; the device scales it.
    EXPECT_FLOAT_EQ(Mixer::Instance().GetMasterVolume(), 1.0f);
    SetMasterVolume(3.0f);
    EXPECT_FLOAT_EQ(output.deviceVolume, 1.0f);

    // Without such an output, the mixer has the volume.
    AudioOutput::SetCurrent(nullptr);
    SetMasterVolume(0.25f);
    EXPECT_FLOAT_EQ(Mixer::Instance().GetMasterVolume(), 0.25f);
    EXPECT_FLOAT_EQ(output.deviceVolume, 1.0f);
}

TEST_F(MixerTest, PanMovesAMonoAudioBetweenTheChannels)
{
    Mixer::Instance().SetOutputFormat(22050, 2);
    auto audio = MakeAudio(std::vector<int16_t>(100, 10000));
    PlayOptions options;
    options.pan = -1.0f;
    const AudioHandle h = Mixer::Instance().Play(audio.get(), options);
    std::vector<int16_t> out = Render(1, 2);
    EXPECT_EQ(out[0], 10000);
    EXPECT_EQ(out[1], 0);

    Mixer::Instance().SetPan(h, 0.0f);
    out = Render(1, 2);
    EXPECT_EQ(out[0], 10000);
    EXPECT_EQ(out[1], 10000);
}

TEST_F(MixerTest, AStereoAudioIsMixedDownForAMonoOutput)
{
    std::vector<int16_t> stereo;
    for (int i = 0; i < 50; ++i)
    {
        stereo.push_back(8000);
        stereo.push_back(2000);
    }
    auto audio = MakeAudio(stereo, 2);
    Mixer::Instance().Play(audio.get());
    EXPECT_EQ(Render(1, 1)[0], 5000);
}

TEST_F(MixerTest, PitchTwoSkipsEveryOtherFrame)
{
    auto audio = MakeAudio(Ramp(100, 0));
    PlayOptions options;
    options.pitch = 2.0f;
    Mixer::Instance().Play(audio.get(), options);
    const std::vector<int16_t> out = Render(10, 1);
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_EQ(out[i], 2 * i);
    }
}

TEST_F(MixerTest, AudioAtHalfTheRateIsInterpolated)
{
    auto audio = MakeAudio({ 0, 100, 200, 300 }, 1, 11025);
    Mixer::Instance().Play(audio.get());
    const std::vector<int16_t> out = Render(6, 1);
    EXPECT_EQ(out, (std::vector<int16_t>{ 0, 50, 100, 150, 200, 250 }));
}

TEST_F(MixerTest, ALoopStartsAgainAtTheEnd)
{
    auto audio = MakeAudio({ 1, 2, 3 });
    PlayOptions options;
    options.loop = true;
    const AudioHandle h = Mixer::Instance().Play(audio.get(), options);
    EXPECT_EQ(Render(8, 1), (std::vector<int16_t>{ 1, 2, 3, 1, 2, 3, 1, 2 }));
    EXPECT_TRUE(Mixer::Instance().IsPlaying(h));
}

TEST_F(MixerTest, APausedAudioWaitsWhereItWas)
{
    auto audio = MakeAudio(Ramp(100));
    const AudioHandle h = Mixer::Instance().Play(audio.get());
    Render(3, 1);
    Mixer::Instance().SetPaused(h, true);
    EXPECT_EQ(Render(5, 1), std::vector<int16_t>(5, 0));
    EXPECT_TRUE(Mixer::Instance().IsPlaying(h));
    Mixer::Instance().SetPaused(h, false);
    EXPECT_EQ(Render(1, 1)[0], 4);

    Mixer::Instance().SetAllPaused(true);
    EXPECT_EQ(Render(2, 1), std::vector<int16_t>(2, 0));
    Mixer::Instance().SetAllPaused(false);
    EXPECT_EQ(Render(1, 1)[0], 5);
}

TEST_F(MixerTest, StopSilencesAndTheHandleGoesStale)
{
    auto audio = MakeAudio(std::vector<int16_t>(100, 500));
    const AudioHandle h = Mixer::Instance().Play(audio.get());
    Mixer::Instance().Stop(h);
    EXPECT_FALSE(Mixer::Instance().IsPlaying(h));
    EXPECT_EQ(Render(4, 1), std::vector<int16_t>(4, 0));

    // A stale handle changes nothing, even once its voice plays something else.
    const AudioHandle other = Mixer::Instance().Play(audio.get());
    Mixer::Instance().Stop(h);
    Mixer::Instance().SetVolume(h, 0.0f);
    EXPECT_TRUE(Mixer::Instance().IsPlaying(other));
    EXPECT_EQ(Render(1, 1)[0], 500);
}

TEST_F(MixerTest, LoudAudiosAddUpWithoutWrapping)
{
    auto audio = MakeAudio(std::vector<int16_t>(100, 30000));
    Mixer::Instance().Play(audio.get());
    Mixer::Instance().Play(audio.get());
    EXPECT_EQ(Render(1, 1)[0], 32767);
}

TEST_F(MixerTest, TheOldestVoiceGivesWayWhenAllAreBusy)
{
    ASSERT_TRUE(Mixer::Instance().SetVoiceCount(2));
    auto audio = MakeAudio(std::vector<int16_t>(100, 1));
    const AudioHandle first = Mixer::Instance().Play(audio.get());
    const AudioHandle second = Mixer::Instance().Play(audio.get());
    const AudioHandle third = Mixer::Instance().Play(audio.get());
    EXPECT_FALSE(Mixer::Instance().IsPlaying(first));
    EXPECT_TRUE(Mixer::Instance().IsPlaying(second));
    EXPECT_TRUE(Mixer::Instance().IsPlaying(third));
    EXPECT_EQ(Mixer::Instance().GetStats().voicesPlaying, 2u);
}

TEST_F(MixerTest, FreeingAnAudioStopsItsVoices)
{
    auto audio = MakeAudio(std::vector<int16_t>(100, 1));
    auto other = MakeAudio(std::vector<int16_t>(100, 2));
    const AudioHandle a = Mixer::Instance().Play(audio.get());
    const AudioHandle b = Mixer::Instance().Play(other.get());
    Mixer::Instance().StopAllUsing(audio.get());
    EXPECT_FALSE(Mixer::Instance().IsPlaying(a));
    EXPECT_TRUE(Mixer::Instance().IsPlaying(b));
}

TEST_F(MixerTest, PlayOncePlaysFromStartToEnd)
{
    auto audio = MakeAudio({ 7, 8, 9 });
    const AudioHandle h = PlayOnce(audio.get(), 1.0f);
    ASSERT_NE(h, kNoAudio);
    EXPECT_EQ(Render(3, 1), (std::vector<int16_t>{ 7, 8, 9 }));
}

// ---------------------------------------------------------------------------
// Streaming

TEST_F(MixerTest, StreamedAudioPlaysAsItIsRead)
{
    const std::vector<int16_t> source = Ramp(3000);
    fs.files["T:/long.daud"] = MakeFile(source, 1, 22050, AudioCodec::Pcm16, true);
    std::unique_ptr<Audio> audio(Audio::LoadFromFile("T:/long.daud"));
    ASSERT_TRUE(audio);
    EXPECT_TRUE(audio->IsStreamed());
    EXPECT_EQ(audio->GetBlocks(), nullptr);

    const AudioHandle h = Mixer::Instance().Play(audio.get());
    ASSERT_NE(h, kNoAudio);
    std::vector<int16_t> out;
    for (int i = 0; i < 30; ++i)
    {
        const std::vector<int16_t> part = Render(100, 1);
        out.insert(out.end(), part.begin(), part.end());
        Mixer::Instance().Update();  // the game thread reads the next blocks
    }
    EXPECT_EQ(out, source);
    EXPECT_EQ(Mixer::Instance().GetStats().underruns, 0u);
}

TEST_F(MixerTest, AStreamThatIsNotReadInTimeHoldsAndCatchesUp)
{
    const std::vector<int16_t> source = Ramp(20000);
    fs.files["T:/long.daud"] = MakeFile(source, 1, 22050, AudioCodec::Pcm16, true);
    std::unique_ptr<Audio> audio(Audio::LoadFromFile("T:/long.daud"));
    ASSERT_TRUE(audio);
    Mixer::Instance().Play(audio.get());

    // Far more than the ring holds, with no Update in between.
    const std::vector<int16_t> starved = Render(15000, 1);
    EXPECT_GT(Mixer::Instance().GetStats().underruns, 0u);
    // It held its last sample rather than jumping to silence.
    const int16_t held = starved.back();
    EXPECT_EQ(held, 23 * 256);

    Mixer::Instance().Update();
    EXPECT_EQ(Render(3, 1),
              (std::vector<int16_t>{ held, static_cast<int16_t>(held + 1), static_cast<int16_t>(held + 2) }));
}

TEST_F(MixerTest, AStreamedAdpcmLoopKeepsGoing)
{
    std::vector<int16_t> source(2000);
    for (size_t i = 0; i < source.size(); ++i)
    {
        source[i] = static_cast<int16_t>(8000.0 * std::sin(i * 0.05));
    }
    fs.files["T:/loop.daud"] = MakeFile(source, 1, 22050, AudioCodec::ImaAdpcm, true);
    std::unique_ptr<Audio> audio(Audio::LoadFromFile("T:/loop.daud"));
    ASSERT_TRUE(audio);
    PlayOptions options;
    options.loop = true;
    const AudioHandle h = Mixer::Instance().Play(audio.get(), options);
    for (int i = 0; i < 100; ++i)
    {
        Render(200, 1);
        Mixer::Instance().Update();
    }
    EXPECT_TRUE(Mixer::Instance().IsPlaying(h));
    EXPECT_EQ(Mixer::Instance().GetStats().underruns, 0u);
}

TEST_F(MixerTest, AMissingStreamDoesNotPlay)
{
    fs.files["T:/gone.daud"] = MakeFile(Ramp(3000), 1, 22050, AudioCodec::Pcm16, true);
    std::unique_ptr<Audio> audio(Audio::LoadFromFile("T:/gone.daud"));
    ASSERT_TRUE(audio);
    fs.files.clear();
    EXPECT_EQ(Mixer::Instance().Play(audio.get()), kNoAudio);
}
