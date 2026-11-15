"""Unpacks the MP3 (ISO 11172-3) Huffman codes from minimp3's packed tables and
PDMP3's code trees, checks the two agree code for code, and writes Mp3Tables.h
with plain (code, length, value) lists, the scalefactor band widths and the
synthesis window.

    python -I tools/extract_mp3_tables.py <minimp3.h> <pdmp3.c> editor/Mp3Tables.h

minimp3: github.com/lieff/minimp3 (minimp3.h). PDMP3: github.com/technosaurus/PDMP3
(pdmp3.c). Neither is part of the build.
"""
import re
import sys

def without_comments(src):
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    return re.sub(r'//[^\n]*', '', src)


minimp3 = without_comments(open(sys.argv[1], encoding='utf-8').read())
pdmp3 = without_comments(open(sys.argv[2], encoding='utf-8').read())
out_path = sys.argv[3]


def c_array(src, name):
    m = re.search(r'%s\s*(\[[^\]]*\])+\s*=\s*\{' % re.escape(name), src)
    if not m:
        raise SystemExit('no array ' + name)
    i = m.end()
    depth = 1
    j = i
    while depth:
        if src[j] == '{':
            depth += 1
        elif src[j] == '}':
            depth -= 1
        j += 1
    body = src[i:j - 1]
    body = re.sub(r'//[^\n]*', '', body)
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    return [x for x in re.split(r'[\s,{}]+', body) if x]


def ints(xs):
    return [int(x, 0) for x in xs]


# minimp3 --------------------------------------------------------------------
tabs = ints(c_array(minimp3, 'tabs'))
tabindex = ints(c_array(minimp3, 'tabindex'))
linbits = ints(c_array(minimp3, 'g_linbits'))
tab32 = ints(c_array(minimp3, 'tab32'))
tab33 = ints(c_array(minimp3, 'tab33'))


def walk_minimp3(base):
    codes = {}

    def level(offset, width, prefix, plen):
        for v in range(1 << width):
            leaf = tabs[base + offset + v]
            if leaf < 0:
                # A sub-table: its index is the next bits minus (leaf >> 3).
                level(-(leaf >> 3), leaf & 7, (prefix << width) | v, plen + width)
            else:
                n = leaf >> 8
                code = (prefix << n) | (v >> (width - n))
                key = (code, plen + n)
                val = leaf & 0xFF
                if key in codes and codes[key] != val:
                    raise SystemExit('conflict')
                codes[key] = val

    level(0, 5, 0, 0)
    return codes


# The leaf's low nibble is x (written first), the high one y.
mm = {}
for t in range(32):
    if t in (0, 4, 14):
        continue
    raw = walk_minimp3(tabindex[t])
    mm[t] = {k: ((v & 15), (v >> 4) & 15) for k, v in raw.items()}


def walk_quad(tab):
    codes = {}
    for v in range(16):
        leaf = tab[v]
        if leaf & 8:
            n = leaf & 7
            codes[(v >> (4 - n), n)] = leaf >> 4
        else:
            w = leaf & 3
            for u in range(1 << w):
                l2 = tab[(leaf >> 3) + u]
                n = l2 & 7  # all the bits of the code
                code = ((v << w) | u) >> (4 + w - n)
                codes[(code, n)] = l2 >> 4
    return codes


quadA = walk_quad(tab32)
quadB = walk_quad(tab33)

# PDMP3 ----------------------------------------------------------------------
ht = ints(c_array(pdmp3, 'g_huffman_table'))
main = re.findall(r'\{\s*g_huffman_table\s*\+?\s*(\d*)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', pdmp3)


def walk_pdmp3(start):
    codes = {}

    def node(p, code, n):
        e = ht[start + p]
        if (e & 0xFF00) == 0:
            codes[(code, n)] = ((e >> 4) & 15, e & 15)
            return
        # A branch: the high byte leads to the 0 child, the low byte to the 1
        # child. An offset of 250 or more is a jump to another entry, whose
        # same byte is followed in turn.
        def follow(q, pick):
            while pick(ht[start + q]) >= 250:
                q += pick(ht[start + q])
            return q + pick(ht[start + q])

        node(follow(p, lambda x: x >> 8), code << 1, n + 1)
        node(follow(p, lambda x: x & 0xFF), (code << 1) | 1, n + 1)

    node(0, 0, 0)
    return codes


pd = {}
ids = [1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15] + list(range(16, 32))
for t, (start, treelen, lb) in zip(ids, main):
    pd[t] = walk_pdmp3(int(start or 0))
    if linbits[t] != int(lb):
        raise SystemExit('linbits differ at %d' % t)

# Check ----------------------------------------------------------------------
for t in mm:
    a = mm[t]
    b = pd.get(t)
    if a != b:
        diff = set(a.items()) ^ set((b or {}).items())
        raise SystemExit('table %d differs: %s' % (t, sorted(diff)[:6]))
    kraft = sum(2.0 ** -n for (c, n) in a)
    if abs(kraft - 1.0) > 1e-9:
        raise SystemExit('table %d is not complete (%f)' % (t, kraft))
for name, q in (('A', quadA), ('B', quadB)):
    kraft = sum(2.0 ** -n for (c, n) in q)
    if abs(kraft - 1.0) > 1e-9 or len(q) != 16:
        raise SystemExit('quad %s wrong: %s' % (name, sorted(q.items())))

# Bands and the window --------------------------------------------------------
long_w = ints(c_array(minimp3, 'g_scf_long'))
short_w = ints(c_array(minimp3, 'g_scf_short'))
mixed_w = ints(c_array(minimp3, 'g_scf_mixed'))
dtbl = [float(x.rstrip('fF')) for x in c_array(pdmp3, 'g_synth_dtbl')]
if len(dtbl) != 512:
    raise SystemExit('window has %d values' % len(dtbl))


def rows(flat, width):
    return [flat[i * width:(i + 1) * width] for i in range(len(flat) // width)]


longs = rows(long_w, 23)
shorts = rows(short_w, 40)
mixed = []
cur = []
for v in mixed_w:
    cur.append(v)
    if v == 0:
        mixed.append(cur)
        cur = []
for group in (longs, shorts, mixed):
    for r in group:
        if sum(r) != 576:
            raise SystemExit('a band row sums to %d' % sum(r))

# Write ----------------------------------------------------------------------
o = []
w = o.append
w('#pragma once')
w('#ifdef DEKI_EDITOR')
w('// MP3 (ISO 11172-3 and 13818-3) tables for Mp3Decode.cpp, generated by')
w('// tools/extract_mp3_tables.py from the standard\'s tables as minimp3 (CC0)')
w('// and PDMP3 (public domain) publish them; it checks the two against each')
w('// other. Do not edit by hand.')
w('')
w('#include <cstdint>')
w('')
w('namespace DekiAudio')
w('{')
w('namespace Mp3Tables')
w('{')
w('')
w('/// One Huffman code word: its bits (right-aligned), how many, and the pair')
w('/// it stands for, x in the high nibble and y in the low.')
w('struct HuffCode')
w('{')
w('    uint16_t code;')
w('    uint8_t length;')
w('    uint8_t xy;')
w('};')
w('')
flat = []
starts = [0] * 32
counts = [0] * 32
for t in list(range(1, 16)) + [16, 24]:
    if t not in mm:
        continue
    starts[t] = len(flat)
    for (code, n), (x, y) in sorted(mm[t].items(), key=lambda kv: (kv[0][1], kv[0][0])):
        flat.append((code, n, (x << 4) | y))
    counts[t] = len(mm[t])
for t in range(17, 24):
    starts[t], counts[t] = starts[16], counts[16]
for t in range(25, 32):
    starts[t], counts[t] = starts[24], counts[24]
w('/// Every table\'s code words, shortest first. Tables 16..23 share the codes of')
w('/// 16, and 24..31 those of 24; they differ in kLinbits.')
w('constexpr HuffCode kHuffCodes[%d] = {' % len(flat))
for i in range(0, len(flat), 4):
    w('    ' + ' '.join('{ 0x%04X, %d, 0x%02X },' % e for e in flat[i:i + 4]))
w('};')
w('constexpr uint16_t kHuffStart[32] = { %s };' % ', '.join(map(str, starts)))
w('constexpr uint16_t kHuffCount[32] = { %s };' % ', '.join(map(str, counts)))
w('constexpr uint8_t kLinbits[32] = { %s };' % ', '.join(map(str, linbits)))
w('')


def quad(name, q):
    w('constexpr HuffCode %s[16] = {' % name)
    items = sorted(q.items(), key=lambda kv: (kv[0][1], kv[0][0]))
    for i in range(0, 16, 4):
        w('    ' + ' '.join('{ 0x%04X, %d, 0x%02X },' % (c, n, v) for (c, n), v in items[i:i + 4]))
    w('};')


w('/// The count1 tables: four values, v w x y from the high bit down, each 0 or 1.')
quad('kQuadA', quadA)
quad('kQuadB', quadB)
w('')
w('/// Scalefactor band widths in samples, for the 8 rate groups: 11025 and 12000,')
w('/// 8000, 22050, 24000, 16000, 44100, 48000, 32000. Long blocks have 22 bands;')
w('/// short blocks list each band three times, once a window (39 entries); mixed')
w('/// blocks start with long bands, then short ones, ending with 0.')
w('constexpr uint8_t kLongWidths[8][22] = {')
for r in longs:
    w('    { %s },' % ', '.join(map(str, r[:22])))
w('};')
w('constexpr uint8_t kShortWidths[8][39] = {')
for r in shorts:
    w('    { %s },' % ', '.join(map(str, r[:39])))
w('};')
w('constexpr uint8_t kMixedWidths[8][40] = {')
for r in mixed:
    w('    { %s },' % ', '.join(map(str, r)))
w('};')
w('')
w('/// The synthesis window D (Table 3-B.3).')
w('constexpr float kSynthWindow[512] = {')
for i in range(0, 512, 6):
    w('    ' + ' '.join('%.9ef,' % v for v in dtbl[i:i + 6]))
w('};')
w('')
w('}  // namespace Mp3Tables')
w('}  // namespace DekiAudio')
w('')
w('#endif  // DEKI_EDITOR')
open(out_path, 'w', encoding='utf-8', newline='\n').write('\n'.join(o) + '\n')
print('ok: %d codes, tables %s, quads %d/%d' % (len(flat), sorted(mm), len(quadA), len(quadB)))
