// MAX98357Audio over a fake I2S channel: the settings it passes on, volume
// applied in software (the amp's gain is a resistor), long clips sent a chunk
// at a time, partial and failed writes, and every way setup can fail.
//
// The channel comes from deki-i2s's factory, which the tests point at a fake
// as a platform package points it at the real one.

#include <gtest/gtest.h>

#include "chips/MAX98357Audio.h"
#include "DekiI2S.h"

#include <cstring>
#include <vector>

using namespace DekiAudio;

namespace
{

struct Channel
{
    Deki::PackageConfig config;
    std::vector<int16_t> written;
    int starts = 0;
    int stops = 0;
    int alive = 0;  // channels created and not yet deleted
    bool failInitialize = false;
    bool failStart = false;
    size_t maxWrite = 0;  // 0: take everything; else at most this many bytes a call
    int writesBeforeFailing = -1;
};
Channel g_Ch;

class FakeI2S : public DekiI2s::IDekiI2S
{
public:
    FakeI2S() { ++g_Ch.alive; }
    ~FakeI2S() override { --g_Ch.alive; }
    const char* GetPackageId() const override { return "i2s"; }
    const char* GetPackageName() const override { return "Fake I2S"; }
    void Configure(const Deki::PackageConfig& config) override { g_Ch.config = config; }
    bool Initialize() override { return !g_Ch.failInitialize; }
    void Shutdown() override {}
    void Update(float) override {}
    Deki::PackageState GetState() const override { return Deki::PackageState::Running; }
    const char* GetLastError() const override { return "fake"; }
    int GetPort() const override { return 0; }
    int Write(const void* data, size_t bytes, uint32_t) override
    {
        if (g_Ch.writesBeforeFailing == 0)
        {
            return 0;
        }
        if (g_Ch.writesBeforeFailing > 0)
        {
            --g_Ch.writesBeforeFailing;
        }
        const size_t n = g_Ch.maxWrite ? std::min(bytes, g_Ch.maxWrite) : bytes;
        const auto* s = static_cast<const uint8_t*>(data);
        // Whole samples only in this fake; the driver must cope with any count.
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            int16_t v;
            std::memcpy(&v, s + i, 2);
            g_Ch.written.push_back(v);
        }
        return static_cast<int>(n - (n % 2));
    }
    bool Start() override
    {
        ++g_Ch.starts;
        return !g_Ch.failStart;
    }
    bool Stop() override
    {
        ++g_Ch.stops;
        return true;
    }
};

DekiI2s::IDekiI2S* CreateFakeI2S()
{
    return new FakeI2S();
}

class MAX98357Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        g_Ch = Channel{};
        DekiI2s::DekiI2S::SetFactory(&CreateFakeI2S);
    }
    void TearDown() override
    {
        amp.Shutdown();
        EXPECT_EQ(g_Ch.alive, 0) << "a channel was not deleted";
        DekiI2s::DekiI2S::SetFactory(nullptr);
    }

    void Start(int volumePercent = 100)
    {
        Deki::PackageConfig c;
        c.pins["BCLK"] = 39;
        c.pins["LRCLK"] = 40;
        c.pins["DOUT"] = 5;
        c.settings["i2sPort"] = "1";
        c.settings["sampleRate"] = "22050";
        c.settings["channels"] = "2";
        c.settings["initialVolume"] = std::to_string(volumePercent);
        amp.Configure(c);
        ASSERT_TRUE(amp.Initialize());
    }

    MAX98357Audio amp;
};

}  // namespace

TEST_F(MAX98357Test, PassesItsPinsAndFormatToTheChannel)
{
    Start();
    EXPECT_EQ(g_Ch.config.GetPin("BCLK"), 39);
    EXPECT_EQ(g_Ch.config.GetPin("LRCLK"), 40);
    EXPECT_EQ(g_Ch.config.GetPin("DOUT"), 5);
    EXPECT_EQ(g_Ch.config.GetInt("i2sPort"), 1);
    EXPECT_EQ(g_Ch.config.GetInt("sampleRate"), 22050);
    EXPECT_EQ(g_Ch.config.GetInt("channels"), 2);
    EXPECT_EQ(g_Ch.config.GetInt("bits_per_sample"), 16);
    EXPECT_TRUE(amp.IsHardwareConnected());
    EXPECT_EQ(amp.GetState(), Deki::PackageState::Running);
}

TEST_F(MAX98357Test, InitialVolumeIsAPercentClamped)
{
    Start(80);
    EXPECT_FLOAT_EQ(amp.GetVolume(), 0.8f);
    Start(150);
    EXPECT_FLOAT_EQ(amp.GetVolume(), 1.0f);
    Start(-5);
    EXPECT_FLOAT_EQ(amp.GetVolume(), 0.0f);
}

TEST_F(MAX98357Test, VolumeScalesTheSamples)
{
    Start();
    amp.SetVolume(0.5f);
    const int16_t pcm[] = { 1000, -1000, 32767, -32768, 0 };
    ASSERT_TRUE(amp.PlayPCM(pcm, 5, 22050));
    EXPECT_EQ(g_Ch.written, (std::vector<int16_t>{ 500, -500, 16383, -16384, 0 }));
    EXPECT_FALSE(amp.IsPlaying());
}

TEST_F(MAX98357Test, SetVolumeIsClamped)
{
    Start();
    amp.SetVolume(3.0f);
    EXPECT_FLOAT_EQ(amp.GetVolume(), 1.0f);
    amp.SetVolume(-1.0f);
    EXPECT_FLOAT_EQ(amp.GetVolume(), 0.0f);
}

TEST_F(MAX98357Test, ALongClipIsSentWhole)
{
    // Scaled 256 samples at a time on the stack; every sample still goes.
    Start();
    std::vector<int16_t> clip(1000);
    for (size_t i = 0; i < clip.size(); ++i)
    {
        clip[i] = static_cast<int16_t>(i);
    }
    ASSERT_TRUE(amp.PlayPCM(clip.data(), clip.size(), 22050));
    EXPECT_EQ(g_Ch.written, clip);
}

TEST_F(MAX98357Test, PartialWritesAreContinued)
{
    // DMA takes what fits; the driver keeps writing the rest.
    Start();
    g_Ch.maxWrite = 6;
    std::vector<int16_t> clip(300, 7);
    ASSERT_TRUE(amp.PlayPCM(clip.data(), clip.size(), 22050));
    EXPECT_EQ(g_Ch.written.size(), 300u);
}

TEST_F(MAX98357Test, AFailedWriteStopsTheClip)
{
    Start();
    g_Ch.writesBeforeFailing = 1;
    std::vector<int16_t> clip(600, 1);
    EXPECT_FALSE(amp.PlayPCM(clip.data(), clip.size(), 22050));
    EXPECT_FALSE(amp.IsPlaying());
}

TEST_F(MAX98357Test, EachClipRestartsTheChannel)
{
    // Stop() disables the channel, and writes to a disabled one fail.
    Start();
    const int startsAfterSetup = g_Ch.starts;
    const int16_t pcm[] = { 1 };
    amp.PlayPCM(pcm, 1, 22050);
    amp.Stop();
    amp.PlayPCM(pcm, 1, 22050);
    EXPECT_EQ(g_Ch.starts, startsAfterSetup + 2);
}

TEST_F(MAX98357Test, NothingPlaysWithoutAChannelOrSamples)
{
    const int16_t pcm[] = { 1 };
    EXPECT_FALSE(amp.PlayPCM(pcm, 1, 22050));  // not initialised
    Start();
    EXPECT_FALSE(amp.PlayPCM(nullptr, 1, 22050));
    EXPECT_FALSE(amp.PlayPCM(pcm, 0, 22050));
}

TEST_F(MAX98357Test, SetupFailuresLeaveNoChannelBehind)
{
    Deki::PackageConfig c;
    g_Ch.failInitialize = true;
    amp.Configure(c);
    EXPECT_FALSE(amp.Initialize());
    EXPECT_EQ(amp.GetState(), Deki::PackageState::Error);
    EXPECT_FALSE(amp.IsHardwareConnected());
    EXPECT_EQ(g_Ch.alive, 0);

    g_Ch.failInitialize = false;
    g_Ch.failStart = true;
    EXPECT_FALSE(amp.Initialize());
    EXPECT_EQ(g_Ch.alive, 0);

    DekiI2s::DekiI2S::SetFactory(nullptr);
    EXPECT_FALSE(amp.Initialize());
    EXPECT_STRNE(amp.GetLastError(), "");
}

TEST_F(MAX98357Test, InitializingTwiceDoesNotLeakAChannel)
{
    Start();
    Start();
    EXPECT_EQ(g_Ch.alive, 1);
}
