// MAX98357Audio over a fake I2S channel: the settings it passes on, the

// channel running only while the output runs, partial and failed writes, the

// mixer's samples reaching the channel, and every way setup can fail.

//

// The channel comes from deki-i2s's factory, which the tests point at a fake

// as a platform package points it at the real one.

#include <gtest/gtest.h>

#include "chips/MAX98357Audio.h"

#include "DekiI2S.h"

#include "Mixer.h"

#include <chrono>

#include <cstring>

#include <mutex>

#include <thread>

#include <vector>

using namespace DekiAudio;

namespace

{

struct Channel

{
    std::mutex lock;  // the pump thread writes while the test reads

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

        std::lock_guard<std::mutex> guard(g_Ch.lock);

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
        g_Ch.config = Deki::PackageConfig{};

        g_Ch.written.clear();

        g_Ch.starts = g_Ch.stops = g_Ch.alive = 0;

        g_Ch.failInitialize = g_Ch.failStart = false;

        g_Ch.maxWrite = 0;

        g_Ch.writesBeforeFailing = -1;

        DekiI2s::DekiI2S::SetFactory(&CreateFakeI2S);
    }

    void TearDown() override

    {
        amp.Shutdown();

        EXPECT_EQ(g_Ch.alive, 0) << "a channel was not deleted";

        DekiI2s::DekiI2S::SetFactory(nullptr);

        Mixer::Instance().Reset();

        Mixer::Instance().SetOutputFormat(0, 0);
    }

    void Open(int channels = 2)

    {
        Deki::PackageConfig c;

        c.pins["BCLK"] = 7;

        c.pins["LRCLK"] = 5;

        c.pins["DOUT"] = 6;

        c.settings["i2sPort"] = "1";

        c.settings["sampleRate"] = "22050";

        c.settings["channels"] = std::to_string(channels);

        amp.Configure(c);

        ASSERT_TRUE(amp.Initialize());
    }

    MAX98357Audio amp;
};

}  // namespace

TEST_F(MAX98357Test, PassesItsPinsAndFormatToTheChannel)

{
    Open();

    EXPECT_EQ(g_Ch.config.GetPin("BCLK"), 7);

    EXPECT_EQ(g_Ch.config.GetPin("LRCLK"), 5);

    EXPECT_EQ(g_Ch.config.GetPin("DOUT"), 6);

    EXPECT_EQ(g_Ch.config.GetInt("i2sPort"), 1);

    EXPECT_EQ(g_Ch.config.GetInt("sampleRate"), 22050);

    EXPECT_EQ(g_Ch.config.GetInt("channels"), 2);

    EXPECT_EQ(g_Ch.config.GetInt("bits_per_sample"), 16);

    EXPECT_EQ(amp.GetSampleRate(), 22050);

    EXPECT_EQ(amp.GetChannels(), 2);

    EXPECT_TRUE(amp.IsHardwareConnected());

    EXPECT_EQ(amp.GetState(), Deki::PackageState::Running);
}

TEST_F(MAX98357Test, TheChannelRunsOnlyWhileTheOutputRuns)

{
    // A game that plays nothing leaves the amplifier idle.

    Open();

    EXPECT_EQ(g_Ch.starts, 0);

    EXPECT_FALSE(amp.IsRunning());

    ASSERT_TRUE(amp.Start());

    EXPECT_TRUE(amp.IsRunning());

    EXPECT_EQ(g_Ch.starts, 1);

    EXPECT_TRUE(amp.Start());  // already running

    EXPECT_EQ(g_Ch.starts, 1);

    amp.Stop();

    EXPECT_FALSE(amp.IsRunning());

    EXPECT_EQ(g_Ch.stops, 1);
}

TEST_F(MAX98357Test, PartialWritesAreContinued)

{
    // DMA takes what fits; the driver keeps writing the rest.

    Open(1);

    g_Ch.maxWrite = 6;

    std::vector<int16_t> frames(300, 7);

    ASSERT_TRUE(amp.WriteFrames(frames.data(), frames.size()));

    EXPECT_EQ(g_Ch.written, frames);
}

TEST_F(MAX98357Test, AFailedWriteIsReported)

{
    Open(1);

    g_Ch.writesBeforeFailing = 0;

    std::vector<int16_t> frames(10, 1);

    EXPECT_FALSE(amp.WriteFrames(frames.data(), frames.size()));
}

TEST_F(MAX98357Test, TheMixersSamplesReachTheChannel)

{
    // Silence while nothing plays: the pump keeps the DMA fed with zeros.

    Open(1);

    Mixer::Instance().SetOutputFormat(amp.GetSampleRate(), amp.GetChannels());

    ASSERT_TRUE(amp.Start());

    for (int i = 0; i < 200; ++i)

    {
        {
            std::lock_guard<std::mutex> guard(g_Ch.lock);

            if (g_Ch.written.size() >= 480)

            {
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    amp.Stop();

    std::lock_guard<std::mutex> guard(g_Ch.lock);

    ASSERT_GE(g_Ch.written.size(), 480u);

    for (int16_t s : g_Ch.written)

    {
        EXPECT_EQ(s, 0);
    }
}

TEST_F(MAX98357Test, StartFailsWithoutAChannel)

{
    EXPECT_FALSE(amp.Start());

    Open();

    g_Ch.failStart = true;

    EXPECT_FALSE(amp.Start());

    EXPECT_FALSE(amp.IsRunning());
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

    DekiI2s::DekiI2S::SetFactory(nullptr);

    g_Ch.failInitialize = false;

    EXPECT_FALSE(amp.Initialize());

    EXPECT_STRNE(amp.GetLastError(), "");
}

TEST_F(MAX98357Test, InitializingTwiceDoesNotLeakAChannel)

{
    Open();

    Open();

    EXPECT_EQ(g_Ch.alive, 1);
}
