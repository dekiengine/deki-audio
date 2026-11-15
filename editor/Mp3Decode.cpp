#ifdef DEKI_EDITOR

// An MP3 (MPEG-1, 2 and 2.5 Layer III) decoder, for importing .mp3 files in
// the editor. It follows the standard's steps in order and favours being plain
// over being fast: a song decodes in well under a second.
//
// Per frame: the header, the side information, then for each granule and
// channel the scalefactors, the Huffman-coded values and their requantization;
// then joint stereo, the reorder of short blocks, alias reduction, the IMDCT
// with its windows and overlap, and the polyphase synthesis filter. The bit
// reservoir carries main data from frame to frame.
//
// The tables are in Mp3Tables.h.

#include "AudioDecode.h"
#include "Mp3Tables.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace DekiAudio
{

// Named, not anonymous: the package builds as a unity build, where helpers of
// the same name in other files would clash.
namespace Mp3
{

constexpr double kPi = 3.14159265358979323846;

// --------------------------------------------------------------------------
// Bits

class BitReader
{
public:
    BitReader(const uint8_t* data, size_t bytes)
        : m_Data(data),
          m_Bits(bytes * 8)
    {
    }

    uint32_t Read(int n)
    {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i)
        {
            v = (v << 1) | ReadBit();
        }
        return v;
    }

    uint32_t ReadBit()
    {
        if (m_Pos >= m_Bits)
        {
            ++m_Pos;  // past the end reads zeros, and Position() shows it
            return 0;
        }
        const uint32_t bit = (m_Data[m_Pos >> 3] >> (7 - (m_Pos & 7))) & 1;
        ++m_Pos;
        return bit;
    }

    size_t Position() const { return m_Pos; }
    void Seek(size_t bit) { m_Pos = bit; }

private:
    const uint8_t* m_Data;
    size_t m_Bits;
    size_t m_Pos = 0;
};

// --------------------------------------------------------------------------
// Huffman trees, built once from the code lists

struct HuffTree
{
    // A node holds its two children; a child >= 0 is a node, < 0 a leaf
    // whose value is -child - 1.
    std::vector<int32_t> nodes;

    void Add(uint32_t code, int length, int value)
    {
        if (nodes.empty())
        {
            nodes.assign(2, 0);
        }
        size_t node = 0;
        for (int i = length - 1; i >= 0; --i)
        {
            const int bit = (code >> i) & 1;
            int32_t& child = nodes[node * 2 + bit];
            if (i == 0)
            {
                child = -value - 1;
                return;
            }
            if (child == 0)
            {
                child = static_cast<int32_t>(nodes.size() / 2);
                nodes.push_back(0);
                nodes.push_back(0);
            }
            node = static_cast<size_t>(nodes[node * 2 + bit]);
        }
    }

    // The value of the next code word, or -1 for bits that are no code.
    int Decode(BitReader& bits) const
    {
        size_t node = 0;
        for (int depth = 0; depth < 24 && !nodes.empty(); ++depth)
        {
            const int32_t child = nodes[node * 2 + bits.ReadBit()];
            if (child < 0)
            {
                return -child - 1;
            }
            if (child == 0)
            {
                return -1;
            }
            node = static_cast<size_t>(child);
        }
        return -1;
    }
};

struct HuffTrees
{
    HuffTree pairs[32];
    HuffTree quadA;
    HuffTree quadB;

    HuffTrees()
    {
        using namespace Mp3Tables;
        for (int t = 0; t < 32; ++t)
        {
            for (int i = 0; i < kHuffCount[t]; ++i)
            {
                const HuffCode& c = kHuffCodes[kHuffStart[t] + i];
                pairs[t].Add(c.code, c.length, c.xy);
            }
        }
        for (const HuffCode& c : kQuadA)
        {
            quadA.Add(c.code, c.length, c.xy);
        }
        for (const HuffCode& c : kQuadB)
        {
            quadB.Add(c.code, c.length, c.xy);
        }
    }
};

const HuffTrees& Trees()
{
    static const HuffTrees s_Trees;
    return s_Trees;
}

// --------------------------------------------------------------------------
// Frame header

enum class Version
{
    Mpeg1,
    Mpeg2,
    Mpeg25,
};

struct Header
{
    Version version = Version::Mpeg1;
    bool crc = false;
    int bitrate = 0;  // kbit/s
    int sampleRate = 0;
    int padding = 0;
    int mode = 0;  // 0 stereo, 1 joint stereo, 2 dual channel, 3 mono
    int modeExt = 0;
    int frameBytes = 0;
    int rateGroup = 0;  // the row of the band tables

    int Channels() const { return mode == 3 ? 1 : 2; }
    int Granules() const { return version == Version::Mpeg1 ? 2 : 1; }
    bool Lsf() const { return version != Version::Mpeg1; }
    int SideInfoBytes() const
    {
        if (version == Version::Mpeg1)
        {
            return Channels() == 1 ? 17 : 32;
        }
        return Channels() == 1 ? 9 : 17;
    }
};

bool ParseHeader(const uint8_t* p, Header& h)
{
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0)
    {
        return false;
    }
    const int versionBits = (p[1] >> 3) & 3;
    const int layer = (p[1] >> 1) & 3;
    const int bitrateIndex = p[2] >> 4;
    const int rateIndex = (p[2] >> 2) & 3;
    if (versionBits == 1 || layer != 1 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3)
    {
        // Reserved, not Layer III, free format or a bad rate.
        return false;
    }
    static const int kBitrates[2][15] = {
        { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 },
        { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 },
    };
    static const int kRates[3] = { 44100, 48000, 32000 };

    h.version = versionBits == 3 ? Version::Mpeg1 : (versionBits == 2 ? Version::Mpeg2 : Version::Mpeg25);
    h.crc = (p[1] & 1) == 0;
    h.bitrate = kBitrates[h.version == Version::Mpeg1 ? 0 : 1][bitrateIndex];
    h.sampleRate = kRates[rateIndex] >> (h.version == Version::Mpeg1 ? 0 : (h.version == Version::Mpeg2 ? 1 : 2));
    h.padding = (p[2] >> 1) & 1;
    h.mode = p[3] >> 6;
    h.modeExt = (p[3] >> 4) & 3;
    const int perFrame = h.version == Version::Mpeg1 ? 144 : 72;
    h.frameBytes = perFrame * h.bitrate * 1000 / h.sampleRate + h.padding;
    // Rows: 11025 and 12000, 8000, 22050, 24000, 16000, 44100, 48000, 32000.
    if (h.version == Version::Mpeg1)
    {
        h.rateGroup = 5 + rateIndex;
    }
    else if (h.version == Version::Mpeg2)
    {
        h.rateGroup = 2 + rateIndex;
    }
    else
    {
        h.rateGroup = rateIndex == 2 ? 1 : 0;
    }
    return true;
}

// --------------------------------------------------------------------------
// Side information

struct Granule
{
    int part23Length = 0;
    int bigValues = 0;
    int globalGain = 0;
    int scalefacCompress = 0;
    int blockType = 0;  // 0 normal, 1 start, 2 short, 3 stop
    bool mixed = false;
    int tableSelect[3] = { 0, 0, 0 };
    int subblockGain[3] = { 0, 0, 0 };
    int region0Count = 0;
    int region1Count = 0;
    int preflag = 0;
    int scalefacScale = 0;
    int count1Table = 0;
};

struct SideInfo
{
    int mainDataBegin = 0;
    int scfsi[2][4] = {};
    Granule gr[2][2];
};

void ReadSideInfo(BitReader& bits, const Header& h, SideInfo& si)
{
    const int channels = h.Channels();
    if (!h.Lsf())
    {
        si.mainDataBegin = static_cast<int>(bits.Read(9));
        bits.Read(channels == 1 ? 5 : 3);
        for (int ch = 0; ch < channels; ++ch)
        {
            for (int band = 0; band < 4; ++band)
            {
                si.scfsi[ch][band] = static_cast<int>(bits.ReadBit());
            }
        }
    }
    else
    {
        si.mainDataBegin = static_cast<int>(bits.Read(8));
        bits.Read(channels == 1 ? 1 : 2);
    }

    for (int g = 0; g < h.Granules(); ++g)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            Granule& gr = si.gr[g][ch];
            gr.part23Length = static_cast<int>(bits.Read(12));
            gr.bigValues = std::min(288, static_cast<int>(bits.Read(9)));
            gr.globalGain = static_cast<int>(bits.Read(8));
            gr.scalefacCompress = static_cast<int>(bits.Read(h.Lsf() ? 9 : 4));
            if (bits.ReadBit())
            {
                gr.blockType = static_cast<int>(bits.Read(2));
                gr.mixed = bits.ReadBit() != 0;
                gr.tableSelect[0] = static_cast<int>(bits.Read(5));
                gr.tableSelect[1] = static_cast<int>(bits.Read(5));
                gr.tableSelect[2] = 0;
                for (int w = 0; w < 3; ++w)
                {
                    gr.subblockGain[w] = static_cast<int>(bits.Read(3));
                }
                // The region boundaries are implicit for these blocks.
                gr.region0Count = (gr.blockType == 2 && !gr.mixed) ? 8 : 7;
                gr.region1Count = 36;
            }
            else
            {
                gr.blockType = 0;
                gr.mixed = false;
                for (int r = 0; r < 3; ++r)
                {
                    gr.tableSelect[r] = static_cast<int>(bits.Read(5));
                }
                gr.region0Count = static_cast<int>(bits.Read(4));
                gr.region1Count = static_cast<int>(bits.Read(3));
            }
            gr.preflag = h.Lsf() ? 0 : static_cast<int>(bits.ReadBit());
            gr.scalefacScale = static_cast<int>(bits.ReadBit());
            gr.count1Table = static_cast<int>(bits.ReadBit());
        }
    }
}

// --------------------------------------------------------------------------
// Scalefactor bands

// The bands a granule's samples fall into, as start positions, and whether
// each is a short band (and of which window).
struct Bands
{
    // Long bands: 23 boundaries. Short bands: 14 boundaries, per window.
    int longStart[23] = {};
    int shortStart[14] = {};
    // Mixed blocks: below this sample the bands are long.
    int mixedLongEnd = 0;
    int mixedLongBands = 0;
};

Bands MakeBands(const Header& h)
{
    using namespace Mp3Tables;
    Bands b;
    const int row = h.rateGroup;
    for (int i = 0; i < 22; ++i)
    {
        b.longStart[i + 1] = b.longStart[i] + kLongWidths[row][i];
    }
    for (int i = 0; i < 13; ++i)
    {
        b.shortStart[i + 1] = b.shortStart[i] + kShortWidths[row][i * 3];
    }
    // A mixed block is long up to 36 samples: 8 long bands in MPEG-1, 6 in
    // MPEG-2 and 2.5.
    b.mixedLongBands = h.Lsf() ? 6 : 8;
    b.mixedLongEnd = 36;
    return b;
}

// The first short band of a mixed block's short part.
int MixedFirstShortBand(const Bands& b)
{
    for (int s = 0; s < 13; ++s)
    {
        if (b.shortStart[s] * 3 >= b.mixedLongEnd)
        {
            return s;
        }
    }
    return 13;
}

// --------------------------------------------------------------------------
// Scalefactors

struct Scalefactors
{
    int l[22] = {};
    int s[13][3] = {};
    // LSF intensity stereo: the value that means "not intensity" in a band.
    int isMaxL[22] = {};
    int isMaxS[13] = {};
};

void ReadScalefactorsMpeg1(BitReader& bits, const Granule& gr, const SideInfo& si, int g, int ch, const Bands& bands,
                           Scalefactors& sf, const Scalefactors& previous)
{
    static const int kSlen[16][2] = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 3, 0 }, { 1, 1 }, { 1, 2 }, { 1, 3 },
                                      { 2, 1 }, { 2, 2 }, { 2, 3 }, { 3, 1 }, { 3, 2 }, { 3, 3 }, { 4, 2 }, { 4, 3 } };
    const int slen1 = kSlen[gr.scalefacCompress][0];
    const int slen2 = kSlen[gr.scalefacCompress][1];

    if (gr.blockType == 2)
    {
        int firstShort = 0;
        if (gr.mixed)
        {
            for (int b = 0; b < bands.mixedLongBands; ++b)
            {
                sf.l[b] = static_cast<int>(bits.Read(slen1));
            }
            firstShort = MixedFirstShortBand(bands);
        }
        for (int b = firstShort; b < 12; ++b)
        {
            const int slen = b < 6 ? slen1 : slen2;
            for (int w = 0; w < 3; ++w)
            {
                sf.s[b][w] = static_cast<int>(bits.Read(slen));
            }
        }
        return;
    }

    static const int kGroupStart[5] = { 0, 6, 11, 16, 21 };
    for (int group = 0; group < 4; ++group)
    {
        const int slen = group < 2 ? slen1 : slen2;
        const bool reuse = g == 1 && si.scfsi[ch][group] != 0;
        for (int b = kGroupStart[group]; b < kGroupStart[group + 1]; ++b)
        {
            sf.l[b] = reuse ? previous.l[b] : static_cast<int>(bits.Read(slen));
        }
    }
}

void ReadScalefactorsLsf(BitReader& bits, const Granule& gr, const Header& h, int ch, const Bands& bands,
                         Scalefactors& sf, int& preflag)
{
    // ISO 13818-3, Table B.6: how many bands each of the four lengths covers.
    static const int kBandsPer[6][3][4] = {
        { { 6, 5, 5, 5 }, { 9, 9, 9, 9 }, { 6, 9, 9, 9 } },
        { { 6, 5, 7, 3 }, { 9, 9, 12, 6 }, { 6, 9, 12, 6 } },
        { { 11, 10, 0, 0 }, { 18, 18, 0, 0 }, { 15, 18, 0, 0 } },
        { { 7, 7, 7, 0 }, { 12, 12, 12, 0 }, { 6, 15, 12, 0 } },
        { { 6, 6, 6, 3 }, { 12, 9, 9, 6 }, { 6, 12, 9, 6 } },
        { { 8, 8, 5, 0 }, { 15, 12, 9, 0 }, { 6, 18, 9, 0 } },
    };
    int slen[4] = { 0, 0, 0, 0 };
    int table = 0;
    preflag = 0;
    int sfc = gr.scalefacCompress;
    const bool intensityRight = ch == 1 && h.mode == 1 && (h.modeExt & 1) != 0;
    if (!intensityRight)
    {
        if (sfc < 400)
        {
            slen[0] = (sfc >> 4) / 5;
            slen[1] = (sfc >> 4) % 5;
            slen[2] = (sfc & 15) >> 2;
            slen[3] = sfc & 3;
            table = 0;
        }
        else if (sfc < 500)
        {
            sfc -= 400;
            slen[0] = (sfc >> 2) / 5;
            slen[1] = (sfc >> 2) % 5;
            slen[2] = sfc & 3;
            table = 1;
        }
        else
        {
            sfc -= 500;
            slen[0] = sfc / 3;
            slen[1] = sfc % 3;
            preflag = 1;
            table = 2;
        }
    }
    else
    {
        sfc >>= 1;
        if (sfc < 180)
        {
            slen[0] = sfc / 36;
            slen[1] = (sfc % 36) / 6;
            slen[2] = (sfc % 36) % 6;
            table = 3;
        }
        else if (sfc < 244)
        {
            sfc -= 180;
            slen[0] = (sfc & 63) >> 4;
            slen[1] = (sfc & 15) >> 2;
            slen[2] = sfc & 3;
            table = 4;
        }
        else
        {
            sfc -= 244;
            slen[0] = sfc / 3;
            slen[1] = sfc % 3;
            table = 5;
        }
    }

    const int blockIndex = gr.blockType == 2 ? (gr.mixed ? 2 : 1) : 0;
    // The values in transmission order, with each one's largest value.
    int values[64] = {};
    int maxes[64] = {};
    int count = 0;
    for (int part = 0; part < 4; ++part)
    {
        for (int i = 0; i < kBandsPer[table][blockIndex][part]; ++i)
        {
            values[count] = static_cast<int>(bits.Read(slen[part]));
            maxes[count] = (1 << slen[part]) - 1;
            ++count;
        }
    }

    int at = 0;
    if (gr.blockType == 2)
    {
        int firstShort = 0;
        if (gr.mixed)
        {
            for (int b = 0; b < bands.mixedLongBands && at < count; ++b, ++at)
            {
                sf.l[b] = values[at];
                sf.isMaxL[b] = maxes[at];
            }
            firstShort = MixedFirstShortBand(bands);
        }
        for (int b = firstShort; b < 12 && at < count; ++b)
        {
            for (int w = 0; w < 3; ++w, ++at)
            {
                sf.s[b][w] = values[at];
                sf.isMaxS[b] = maxes[at];
            }
        }
    }
    else
    {
        for (int b = 0; b < 21 && at < count; ++b, ++at)
        {
            sf.l[b] = values[at];
            sf.isMaxL[b] = maxes[at];
        }
    }
}

// --------------------------------------------------------------------------
// Huffman values

// Reads the granule's quantized values into `is`, and returns how many of
// them can be nonzero.
int ReadValues(BitReader& bits, const Granule& gr, int rateGroup, size_t end, int* is)
{
    const HuffTrees& trees = Trees();
    std::fill(is, is + 576, 0);

    // Region 1 starts after region0Count + 1 bands of the list the block
    // uses, and region 2 after region0Count + region1Count + 2. A block that
    // switches windows has no region 2.
    const uint8_t* widths = Mp3Tables::kLongWidths[rateGroup];
    int listLength = 22;
    if (gr.blockType == 2)
    {
        // A mixed list ends with a 0, which the sum stops at.
        widths = gr.mixed ? Mp3Tables::kMixedWidths[rateGroup] : Mp3Tables::kShortWidths[rateGroup];
        listLength = 39;
    }
    auto sumOf = [&](int count)
    {
        int sum = 0;
        for (int i = 0; i < count && i < listLength && widths[i] != 0; ++i)
        {
            sum += widths[i];
        }
        return std::min(576, sum);
    };
    const int region1 = sumOf(gr.region0Count + 1);
    const int region2 = gr.blockType != 0 ? 576 : sumOf(gr.region0Count + gr.region1Count + 2);

    const int bigEnd = std::min(576, gr.bigValues * 2);
    int i = 0;
    for (; i < bigEnd; i += 2)
    {
        const int table = gr.tableSelect[i < region1 ? 0 : (i < region2 ? 1 : 2)];
        if (table == 0 || Mp3Tables::kHuffCount[table] == 0)
        {
            continue;
        }
        const int xy = trees.pairs[table].Decode(bits);
        if (xy < 0)
        {
            break;
        }
        int x = xy >> 4;
        int y = xy & 15;
        const int linbits = Mp3Tables::kLinbits[table];
        if (x == 15 && linbits)
        {
            x += static_cast<int>(bits.Read(linbits));
        }
        if (x && bits.ReadBit())
        {
            x = -x;
        }
        if (y == 15 && linbits)
        {
            y += static_cast<int>(bits.Read(linbits));
        }
        if (y && bits.ReadBit())
        {
            y = -y;
        }
        is[i] = x;
        is[i + 1] = y;
    }

    // The count1 region: values of -1, 0 or 1, four at a time.
    const HuffTree& quad = gr.count1Table ? trees.quadB : trees.quadA;
    while (i + 4 <= 576 && bits.Position() < end)
    {
        const size_t start = bits.Position();
        const int v = quad.Decode(bits);
        if (v < 0)
        {
            break;
        }
        int q[4] = { (v >> 3) & 1, (v >> 2) & 1, (v >> 1) & 1, v & 1 };
        for (int& value : q)
        {
            if (value && bits.ReadBit())
            {
                value = -1;
            }
        }
        if (bits.Position() > end)
        {
            // Read past the granule: this quadruple was padding.
            bits.Seek(start);
            break;
        }
        for (int k = 0; k < 4; ++k)
        {
            is[i + k] = q[k];
        }
        i += 4;
    }
    return i;
}

// --------------------------------------------------------------------------
// Requantization

float Pow43(int v)
{
    const double a = std::pow(static_cast<double>(v < 0 ? -v : v), 4.0 / 3.0);
    return static_cast<float>(v < 0 ? -a : a);
}

void Requantize(const int* is, int count, const Granule& gr, const Scalefactors& sf, int preflag, const Bands& bands,
                float* xr)
{
    static const int kPretab[22] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 2, 0 };
    std::fill(xr, xr + 576, 0.0f);
    const double scale = 0.5 * (1 + gr.scalefacScale);
    const double gain = gr.globalGain - 210.0;
    const bool shortBlock = gr.blockType == 2;
    const int longEnd = shortBlock ? (gr.mixed ? bands.mixedLongEnd : 0) : 576;

    int band = 0;
    for (int i = 0; i < count && i < 576; ++i)
    {
        if (is[i] == 0)
        {
            continue;
        }
        double exponent = 0.0;
        if (i < longEnd)
        {
            while (band < 21 && i >= bands.longStart[band + 1])
            {
                ++band;
            }
            const int pre = preflag ? kPretab[band] : 0;
            exponent = gain / 4.0 - scale * (sf.l[band] + pre);
        }
        else
        {
            // Short bands hold their three windows one after the other.
            int s = 0;
            while (s < 12 && i >= bands.shortStart[s + 1] * 3)
            {
                ++s;
            }
            const int width = bands.shortStart[s + 1] - bands.shortStart[s];
            const int w = (i - bands.shortStart[s] * 3) / width;
            exponent = gain / 4.0 - 2.0 * gr.subblockGain[w] - scale * (s < 12 ? sf.s[s][w] : 0);
        }
        xr[i] = static_cast<float>(Pow43(is[i]) * std::pow(2.0, exponent));
    }
}

// --------------------------------------------------------------------------
// Stereo

// The last nonzero sample of a channel, plus one.
int NonzeroEnd(const float* xr)
{
    for (int i = 575; i >= 0; --i)
    {
        if (xr[i] != 0.0f)
        {
            return i + 1;
        }
    }
    return 0;
}

// Intensity stereo in one band: the left channel carries the sound, the
// position says how it splits.
void Intensity(float* left, float* right, int from, int to, int pos, int posMax, const Header& h, const Granule& grR)
{
    float kl = 1.0f;
    float kr = 1.0f;
    if (!h.Lsf())
    {
        if (pos >= 7)
        {
            return;  // not intensity coded
        }
        const double ratio = std::tan(pos * kPi / 12.0);
        kl = static_cast<float>(ratio / (1.0 + ratio));
        kr = static_cast<float>(1.0 / (1.0 + ratio));
    }
    else
    {
        if (pos == posMax)
        {
            return;  // not intensity coded
        }
        const double io = (grR.scalefacCompress & 1) ? 0.70710678118654752 : 0.84089641525371454;
        if (pos & 1)
        {
            kl = static_cast<float>(std::pow(io, (pos + 1) / 2));
        }
        else
        {
            kr = static_cast<float>(std::pow(io, pos / 2));
        }
    }
    for (int i = from; i < to; ++i)
    {
        const float v = left[i];
        left[i] = v * kl;
        right[i] = v * kr;
    }
}

void MidSide(float* left, float* right, int from, int to)
{
    const float k = 0.70710678118654752f;
    for (int i = from; i < to; ++i)
    {
        const float m = left[i];
        const float s = right[i];
        left[i] = (m + s) * k;
        right[i] = (m - s) * k;
    }
}

void JointStereo(float* left, float* right, const Header& h, const Granule& grR, const Scalefactors& sfR,
                 const Bands& bands)
{
    const bool ms = (h.modeExt & 2) != 0;
    const bool intensity = (h.modeExt & 1) != 0;
    if (!intensity)
    {
        if (ms)
        {
            MidSide(left, right, 0, 576);
        }
        return;
    }

    // Where the right channel's own values end, intensity takes over.
    const int rightEnd = NonzeroEnd(right);
    if (grR.blockType == 2)
    {
        // Each window has its own bound, from its highest band with values.
        // Below the bounds, mid/side applies.
        bool isBand[13][3] = {};
        for (int w = 0; w < 3; ++w)
        {
            int last = -1;
            for (int s = 0; s < 13; ++s)
            {
                const int width = bands.shortStart[s + 1] - bands.shortStart[s];
                const int from = bands.shortStart[s] * 3 + w * width;
                for (int i = from; i < from + width; ++i)
                {
                    if (right[i] != 0.0f)
                    {
                        last = s;
                    }
                }
            }
            for (int s = last + 1; s < 13; ++s)
            {
                isBand[s][w] = bands.shortStart[s] * 3 >= (grR.mixed ? bands.mixedLongEnd : 0);
            }
        }
        for (int s = 0; s < 13; ++s)
        {
            const int width = bands.shortStart[s + 1] - bands.shortStart[s];
            for (int w = 0; w < 3; ++w)
            {
                const int from = bands.shortStart[s] * 3 + w * width;
                if (from < (grR.mixed ? bands.mixedLongEnd : 0))
                {
                    continue;
                }
                if (isBand[s][w])
                {
                    // The last band has no scalefactor and takes the one
                    // before. A position that says "not intensity" is
                    // mid/side.
                    const int sb = s < 12 ? s : 11;
                    const int pos = sfR.s[sb][w];
                    const int posMax = h.Lsf() ? sfR.isMaxS[sb] : 7;
                    if (pos == posMax || (!h.Lsf() && pos >= 7))
                    {
                        if (ms)
                        {
                            MidSide(left, right, from, from + width);
                        }
                    }
                    else
                    {
                        Intensity(left, right, from, from + width, pos, posMax, h, grR);
                    }
                }
                else if (ms)
                {
                    MidSide(left, right, from, from + width);
                }
            }
        }
        if (grR.mixed && ms)
        {
            MidSide(left, right, 0, bands.mixedLongEnd);
        }
        return;
    }

    // Long blocks: one bound for the granule.
    int firstIntensityBand = 22;
    for (int b = 0; b < 22; ++b)
    {
        if (bands.longStart[b] >= rightEnd)
        {
            firstIntensityBand = b;
            break;
        }
    }
    if (ms)
    {
        MidSide(left, right, 0, bands.longStart[firstIntensityBand]);
    }
    for (int b = firstIntensityBand; b < 22; ++b)
    {
        const int sb = b < 21 ? b : 20;
        const int pos = sfR.l[sb];
        const int posMax = h.Lsf() ? sfR.isMaxL[sb] : 7;
        if (pos == posMax || (!h.Lsf() && pos >= 7))
        {
            if (ms)
            {
                MidSide(left, right, bands.longStart[b], bands.longStart[b + 1]);
            }
        }
        else
        {
            Intensity(left, right, bands.longStart[b], bands.longStart[b + 1], pos, posMax, h, grR);
        }
    }
}

// --------------------------------------------------------------------------
// Reorder, alias reduction, IMDCT

// Short blocks arrive band by band, each band's three windows one after the
// other; the IMDCT wants each subband's 18 values with the windows
// interleaved.
void Reorder(float* xr, const Granule& gr, const Bands& bands)
{
    if (gr.blockType != 2)
    {
        return;
    }
    float out[576];
    std::memcpy(out, xr, sizeof(out));
    const int first = gr.mixed ? MixedFirstShortBand(bands) : 0;
    for (int s = first; s < 13; ++s)
    {
        const int start = bands.shortStart[s];
        const int width = bands.shortStart[s + 1] - start;
        for (int w = 0; w < 3; ++w)
        {
            for (int j = 0; j < width; ++j)
            {
                out[3 * start + 3 * j + w] = xr[3 * start + w * width + j];
            }
        }
    }
    std::memcpy(xr, out, sizeof(out));
}

void AliasReduction(float* xr, const Granule& gr)
{
    static const double kC[8] = { -0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037 };
    static float cs[8];
    static float ca[8];
    static bool made = false;
    if (!made)
    {
        for (int i = 0; i < 8; ++i)
        {
            const double d = std::sqrt(1.0 + kC[i] * kC[i]);
            cs[i] = static_cast<float>(1.0 / d);
            ca[i] = static_cast<float>(kC[i] / d);
        }
        made = true;
    }
    if (gr.blockType == 2 && !gr.mixed)
    {
        return;
    }
    const int limit = (gr.blockType == 2 && gr.mixed) ? 2 : 32;
    for (int sb = 1; sb < limit; ++sb)
    {
        for (int i = 0; i < 8; ++i)
        {
            float& lo = xr[18 * sb - 1 - i];
            float& hi = xr[18 * sb + i];
            const float a = lo;
            const float b = hi;
            lo = a * cs[i] - b * ca[i];
            hi = b * cs[i] + a * ca[i];
        }
    }
}

struct ImdctTables
{
    float cos36[18][36];
    float cos12[6][12];
    float window[4][36];

    ImdctTables()
    {
        for (int k = 0; k < 18; ++k)
        {
            for (int i = 0; i < 36; ++i)
            {
                cos36[k][i] = static_cast<float>(std::cos(kPi / 72.0 * (2 * i + 1 + 18) * (2 * k + 1)));
            }
        }
        for (int k = 0; k < 6; ++k)
        {
            for (int i = 0; i < 12; ++i)
            {
                cos12[k][i] = static_cast<float>(std::cos(kPi / 24.0 * (2 * i + 1 + 6) * (2 * k + 1)));
            }
        }
        for (int i = 0; i < 36; ++i)
        {
            const float sine = static_cast<float>(std::sin(kPi / 36.0 * (i + 0.5)));
            window[0][i] = sine;
            window[1][i] =
                i < 18 ? sine
                       : (i < 24 ? 1.0f : (i < 30 ? static_cast<float>(std::sin(kPi / 12.0 * (i - 18 + 0.5))) : 0.0f));
            window[3][i] =
                i < 6 ? 0.0f
                      : (i < 12 ? static_cast<float>(std::sin(kPi / 12.0 * (i - 6 + 0.5))) : (i < 18 ? 1.0f : sine));
        }
        for (int i = 0; i < 12; ++i)
        {
            window[2][i] = static_cast<float>(std::sin(kPi / 12.0 * (i + 0.5)));
        }
    }
};

const ImdctTables& Imdct()
{
    static const ImdctTables s_Tables;
    return s_Tables;
}

// One subband's 18 values to 18 time samples, overlapping the previous
// granule's second half.
void HybridSubband(const float* in, int blockType, float* overlap, float* out)
{
    const ImdctTables& t = Imdct();
    float y[36] = {};
    if (blockType == 2)
    {
        for (int w = 0; w < 3; ++w)
        {
            for (int i = 0; i < 12; ++i)
            {
                float sum = 0.0f;
                for (int k = 0; k < 6; ++k)
                {
                    sum += in[3 * k + w] * t.cos12[k][i];
                }
                y[6 + 6 * w + i] += sum * t.window[2][i];
            }
        }
    }
    else
    {
        for (int i = 0; i < 36; ++i)
        {
            float sum = 0.0f;
            for (int k = 0; k < 18; ++k)
            {
                sum += in[k] * t.cos36[k][i];
            }
            y[i] = sum * t.window[blockType][i];
        }
    }
    for (int i = 0; i < 18; ++i)
    {
        out[i] = y[i] + overlap[i];
        overlap[i] = y[i + 18];
    }
}

// --------------------------------------------------------------------------
// Synthesis filter

struct SynthTables
{
    float n[64][32];

    SynthTables()
    {
        for (int i = 0; i < 64; ++i)
        {
            for (int k = 0; k < 32; ++k)
            {
                n[i][k] = static_cast<float>(std::cos((16 + i) * (2 * k + 1) * kPi / 64.0));
            }
        }
    }
};

const SynthTables& Synth()
{
    static const SynthTables s_Tables;
    return s_Tables;
}

struct Channel
{
    float overlap[32][18] = {};
    float v[1024] = {};
    int vOffset = 0;
};

// 32 subband samples to 32 output samples.
void Synthesize(Channel& c, const float* subbands, float* out)
{
    const SynthTables& t = Synth();
    c.vOffset = (c.vOffset - 64) & 1023;
    float* v = c.v;
    for (int i = 0; i < 64; ++i)
    {
        float sum = 0.0f;
        for (int k = 0; k < 32; ++k)
        {
            sum += t.n[i][k] * subbands[k];
        }
        v[(c.vOffset + i) & 1023] = sum;
    }
    for (int j = 0; j < 32; ++j)
    {
        float sum = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            sum += v[(c.vOffset + 128 * i + j) & 1023] * Mp3Tables::kSynthWindow[64 * i + j];
            sum += v[(c.vOffset + 128 * i + 96 + j) & 1023] * Mp3Tables::kSynthWindow[64 * i + 32 + j];
        }
        out[j] = sum;
    }
}

// --------------------------------------------------------------------------
// Finding frames

size_t SkipId3(const std::vector<uint8_t>& file)
{
    if (file.size() >= 10 && std::memcmp(file.data(), "ID3", 3) == 0)
    {
        const size_t size = (static_cast<size_t>(file[6] & 0x7F) << 21) | (static_cast<size_t>(file[7] & 0x7F) << 14) |
                            (static_cast<size_t>(file[8] & 0x7F) << 7) | static_cast<size_t>(file[9] & 0x7F);
        const bool footer = (file[5] & 0x10) != 0;
        return 10 + size + (footer ? 10 : 0);
    }
    return 0;
}

// A frame starts here when its header parses and the next one does too, so a
// stray 0xFF in the data is not taken for one.
bool FrameAt(const std::vector<uint8_t>& file, size_t at, Header& h)
{
    if (at + 4 > file.size() || !ParseHeader(file.data() + at, h))
    {
        return false;
    }
    const size_t next = at + static_cast<size_t>(h.frameBytes);
    if (next + 4 > file.size())
    {
        return next <= file.size();
    }
    Header n;
    return ParseHeader(file.data() + next, n) && n.sampleRate == h.sampleRate && n.version == h.version;
}

uint32_t ReadBe32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// An encoder's information frame (Xing, Info or VBRI) holds no sound. LAME
// writes in it how many samples of silence it added at each end.
bool InfoFrame(const std::vector<uint8_t>& file, size_t at, const Header& h, int& delay, int& padding)
{
    const size_t side = at + 4 + (h.crc ? 2 : 0) + static_cast<size_t>(h.SideInfoBytes());
    const size_t end = at + static_cast<size_t>(h.frameBytes);
    if (side + 8 <= end && (std::memcmp(&file[side], "Xing", 4) == 0 || std::memcmp(&file[side], "Info", 4) == 0))
    {
        const uint32_t flags = ReadBe32(&file[side + 4]);
        size_t lame = side + 8;
        lame += (flags & 1) ? 4 : 0;
        lame += (flags & 2) ? 4 : 0;
        lame += (flags & 4) ? 100 : 0;
        lame += (flags & 8) ? 4 : 0;
        if (lame + 24 <= end && (std::memcmp(&file[lame], "LAME", 4) == 0 || std::memcmp(&file[lame], "Lavc", 4) == 0))
        {
            const uint8_t* d = &file[lame + 21];
            delay = (d[0] << 4) | (d[1] >> 4);
            padding = ((d[1] & 15) << 8) | d[2];
        }
        return true;
    }
    const size_t vbri = at + 4 + 32;
    return vbri + 4 <= end && std::memcmp(&file[vbri], "VBRI", 4) == 0;
}

}  // namespace Mp3

bool DecodeMp3(const std::vector<uint8_t>& file, DecodedAudio& out, std::string& error)
{
    using namespace Mp3;
    size_t at = SkipId3(file);
    Header first;
    while (at + 4 <= file.size() && !FrameAt(file, at, first))
    {
        ++at;
    }
    if (at + 4 > file.size())
    {
        error = "no MP3 frames found (only Layer III is read)";
        return false;
    }

    const int channels = first.Channels();
    out.sampleRate = first.sampleRate;
    out.channels = channels;
    out.samples.clear();

    int delay = -1;
    int padding = 0;
    Header h;
    if (InfoFrame(file, at, first, delay, padding))
    {
        at += static_cast<size_t>(first.frameBytes);
    }

    std::vector<uint8_t> reservoir;
    std::vector<uint8_t> main;
    Channel state[2];
    SideInfo si;
    Scalefactors sf[2][2];
    int is[576];
    float xr[2][576];
    float pcm[2][18][32];
    float subbands[32];
    size_t frames = 0;

    while (at + 4 <= file.size())
    {
        if (!ParseHeader(file.data() + at, h) || h.sampleRate != first.sampleRate || h.Channels() != channels ||
            at + static_cast<size_t>(h.frameBytes) > file.size())
        {
            // Lost: look for the next frame.
            ++at;
            continue;
        }
        const size_t sideAt = at + 4 + (h.crc ? 2 : 0);
        const size_t mainAt = sideAt + static_cast<size_t>(h.SideInfoBytes());
        const size_t frameEnd = at + static_cast<size_t>(h.frameBytes);
        if (mainAt > frameEnd)
        {
            break;
        }
        BitReader sideBits(file.data() + sideAt, static_cast<size_t>(h.SideInfoBytes()));
        si = SideInfo{};
        ReadSideInfo(sideBits, h, si);

        // The main data starts mainDataBegin bytes back, in earlier frames.
        const bool haveData = static_cast<int>(reservoir.size()) >= si.mainDataBegin;
        main.assign(reservoir.end() - std::min<size_t>(reservoir.size(), static_cast<size_t>(si.mainDataBegin)),
                    reservoir.end());
        main.insert(main.end(), file.begin() + static_cast<long>(mainAt), file.begin() + static_cast<long>(frameEnd));
        reservoir.insert(reservoir.end(), file.begin() + static_cast<long>(mainAt),
                         file.begin() + static_cast<long>(frameEnd));
        if (reservoir.size() > 4096)
        {
            reservoir.erase(reservoir.begin(), reservoir.end() - 4096);
        }
        at = frameEnd;
        if (!haveData)
        {
            // The first frames after a cut point back past the start: there
            // is nothing to decode yet.
            continue;
        }

        const Bands bands = MakeBands(h);
        BitReader bits(main.data(), main.size());
        for (int g = 0; g < h.Granules(); ++g)
        {
            int preflag[2] = { 0, 0 };
            for (int ch = 0; ch < channels; ++ch)
            {
                const Granule& gr = si.gr[g][ch];
                const size_t part2Start = bits.Position();
                const Scalefactors previous = sf[0][ch];
                sf[g][ch] = Scalefactors{};
                if (h.Lsf())
                {
                    ReadScalefactorsLsf(bits, gr, h, ch, bands, sf[g][ch], preflag[ch]);
                }
                else
                {
                    ReadScalefactorsMpeg1(bits, gr, si, g, ch, bands, sf[g][ch], previous);
                    preflag[ch] = gr.preflag;
                }
                const size_t end = part2Start + static_cast<size_t>(gr.part23Length);
                const int count = ReadValues(bits, gr, h.rateGroup, end, is);
                bits.Seek(end);
                Requantize(is, count, gr, sf[g][ch], preflag[ch], bands, xr[ch]);
            }

            if (channels == 2 && h.mode == 1)
            {
                JointStereo(xr[0], xr[1], h, si.gr[g][1], sf[g][1], bands);
            }

            for (int ch = 0; ch < channels; ++ch)
            {
                const Granule& gr = si.gr[g][ch];
                Reorder(xr[ch], gr, bands);
                AliasReduction(xr[ch], gr);
                for (int sb = 0; sb < 32; ++sb)
                {
                    // Mixed blocks are long in their first two subbands.
                    const int blockType = (gr.blockType == 2 && gr.mixed && sb < 2) ? 0 : gr.blockType;
                    float time[18];
                    HybridSubband(&xr[ch][sb * 18], blockType, state[ch].overlap[sb], time);
                    for (int t = 0; t < 18; ++t)
                    {
                        // Odd subbands are mirrored in frequency; flipping the
                        // sign of their odd samples undoes it.
                        pcm[ch][t][sb] = (sb & 1) && (t & 1) ? -time[t] : time[t];
                    }
                }
                for (int t = 0; t < 18; ++t)
                {
                    for (int sb = 0; sb < 32; ++sb)
                    {
                        subbands[sb] = pcm[ch][t][sb];
                    }
                    Synthesize(state[ch], subbands, pcm[ch][t]);
                }
            }
            for (int t = 0; t < 18; ++t)
            {
                for (int j = 0; j < 32; ++j)
                {
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        out.samples.push_back(std::clamp(pcm[ch][t][j], -1.0f, 1.0f));
                    }
                }
            }
        }
        ++frames;
    }

    if (frames == 0)
    {
        error = "the MP3 file holds no frames that could be decoded";
        return false;
    }

    // The synthesis filter starts 528 samples late, which every decoder cuts.
    // With a LAME tag, the encoder's own silence at each end goes too.
    {
        const size_t total = out.Frames();
        const int start = delay >= 0 ? delay + 529 : 528;
        const int end = delay >= 0 ? std::max(0, padding - 529) : 0;
        const size_t cutStart = std::min(total, static_cast<size_t>(start));
        const size_t cutEnd = std::min(total - cutStart, static_cast<size_t>(end));
        const size_t c = static_cast<size_t>(channels);
        out.samples.erase(out.samples.end() - static_cast<long>(cutEnd * c), out.samples.end());
        out.samples.erase(out.samples.begin(), out.samples.begin() + static_cast<long>(cutStart * c));
    }
    return true;
}

}  // namespace DekiAudio

#endif  // DEKI_EDITOR
