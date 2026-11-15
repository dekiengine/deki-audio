// The MP3 decoder: its tables, files it must refuse, and how closely it
// matches another decoder: each x.mp3 in tests/data next to x.ref.wav, as
// Windows' own MP3 decoder wrote it. DEKI_MP3_REFERENCE_DIR points the same
// check at another folder of such pairs.

#include <gtest/gtest.h>

#include "editor/AudioDecode.h"
#include "editor/Mp3Tables.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace DekiAudio;
namespace fs = std::filesystem;

namespace
{

std::vector<uint8_t> ReadAll(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// How well `ours` matches `ref`, in dB, over the frames both have, with
// `ours` moved by `shift` frames.
double MatchDbAt(const DecodedAudio& ours, const DecodedAudio& ref, long shift, long from, long count)
{
    double signal = 0.0;
    double noise = 0.0;
    const long frames = static_cast<long>(ours.Frames());
    for (long i = from; i < from + count && i < frames; ++i)
    {
        const long j = i + shift;
        if (j < 0 || j >= static_cast<long>(ref.Frames()))
        {
            continue;
        }
        for (int c = 0; c < ours.channels && c < ref.channels; ++c)
        {
            const double a = ours.samples[static_cast<size_t>(i * ours.channels + c)];
            const double b = ref.samples[static_cast<size_t>(j * ref.channels + c)];
            signal += b * b;
            noise += (a - b) * (a - b);
        }
    }
    return noise > 0.0 ? 10.0 * std::log10(signal / noise) : 200.0;
}

// The best match over the shifts decoders differ by (where each starts),
// checked on a stretch and then over the whole file. A correlation would be
// fooled by music that repeats.
double MatchDb(const DecodedAudio& ours, const DecodedAudio& ref, int& shift)
{
    const long n = static_cast<long>(ours.Frames());
    const long from = std::min(n / 4, 4000L);
    double best = -1e300;
    for (int s = -1200; s <= 1200; ++s)
    {
        const double db = MatchDbAt(ours, ref, s, from, 4000);
        if (db > best)
        {
            best = db;
            shift = s;
        }
    }
    return MatchDbAt(ours, ref, shift, 0, n);
}

}  // namespace

TEST(Mp3, EveryTableIsACompletePrefixCode)
{
    using namespace Mp3Tables;
    for (int t = 0; t < 32; ++t)
    {
        double kraft = 0.0;
        std::set<std::pair<int, int>> seen;
        for (int i = 0; i < kHuffCount[t]; ++i)
        {
            const HuffCode& c = kHuffCodes[kHuffStart[t] + i];
            kraft += std::ldexp(1.0, -c.length);
            EXPECT_TRUE(seen.insert({ c.code, c.length }).second) << "table " << t;
        }
        if (kHuffCount[t] > 0)
        {
            EXPECT_DOUBLE_EQ(kraft, 1.0) << "table " << t;
        }
    }
    double a = 0.0;
    double b = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        a += std::ldexp(1.0, -kQuadA[i].length);
        b += std::ldexp(1.0, -kQuadB[i].length);
    }
    EXPECT_DOUBLE_EQ(a, 1.0);
    EXPECT_DOUBLE_EQ(b, 1.0);
    for (int row = 0; row < 8; ++row)
    {
        int sum = 0;
        for (int i = 0; i < 22; ++i)
        {
            sum += kLongWidths[row][i];
        }
        EXPECT_EQ(sum, 576) << "row " << row;
    }
}

TEST(Mp3, WhatIsNotAnMp3IsRefused)
{
    DecodedAudio out;
    std::string error;
    EXPECT_FALSE(DecodeMp3({}, out, error));
    EXPECT_FALSE(DecodeMp3(std::vector<uint8_t>(4000, 0x55), out, error));
    // A Layer II header is not read.
    std::vector<uint8_t> layer2(2000, 0);
    for (size_t at = 0; at + 4 < layer2.size(); at += 418)
    {
        layer2[at] = 0xFF;
        layer2[at + 1] = 0xFD;
        layer2[at + 2] = 0x94;
        layer2[at + 3] = 0x00;
    }
    EXPECT_FALSE(DecodeMp3(layer2, out, error));
    EXPECT_NE(error.find("Layer III"), std::string::npos);
}

TEST(Mp3, MatchesAReferenceDecoder)
{
    const char* env = std::getenv("DEKI_MP3_REFERENCE_DIR");
    const fs::path dir = env ? fs::path(env) : fs::path(__FILE__).parent_path() / "data";
    int checked = 0;
    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (entry.path().extension() != ".mp3")
        {
            continue;
        }
        fs::path refPath = entry.path();
        refPath.replace_extension(".ref.wav");
        if (!fs::exists(refPath))
        {
            continue;
        }
        DecodedAudio ours;
        DecodedAudio ref;
        std::string error;
        ASSERT_TRUE(DecodeMp3(ReadAll(entry.path()), ours, error)) << entry.path() << ": " << error;
        ASSERT_TRUE(DecodeWav(ReadAll(refPath), ref, error)) << refPath << ": " << error;
        EXPECT_EQ(ours.sampleRate, ref.sampleRate) << entry.path();
        EXPECT_EQ(ours.channels, ref.channels) << entry.path();
        int shift = 0;
        const double db = MatchDb(ours, ref, shift);
        std::printf("%-14s %6d Hz %d ch  ours %7zu ref %7zu frames  shift %5d  match %6.1f dB\n",
                    entry.path().filename().string().c_str(), ours.sampleRate, ours.channels, ours.Frames(),
                    ref.Frames(), shift, db);
        EXPECT_GT(db, 40.0) << entry.path();
        ++checked;
    }
    EXPECT_GT(checked, 0);
}
