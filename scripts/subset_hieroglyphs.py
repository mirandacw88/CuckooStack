#!/usr/bin/env python3
"""Subsets Noto Sans Egyptian Hieroglyphs (SIL OFL 1.1) to the signs the tombs' walls draw (every 0x1xxxx codepoint in
src/core/Hieroglyphs.h) and writes assets/fonts/NotoSansEgyptianHieroglyphs-tomb.ttf. No dependencies: it keeps those
glyphs' outlines (and any components) and rebuilds the TrueType tables the game's font loader (stb_truetype) and
FreeType read (one Unicode cmap, no glyph names, no layout tables).
  python3 scripts/subset_hieroglyphs.py [font.ttf]
The font: macOS ships it (the default path below); it's also at github.com/google/fonts (ofl/notosansegyptianhieroglyphs).
Its licence: assets/fonts/OFL-notosansegyptianhieroglyphs.txt (Noto declares no Reserved Font Name, so a subset may
keep its name)."""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = sys.argv[1] if len(sys.argv) > 1 else '/System/Library/Fonts/Supplemental/NotoSansEgyptianHieroglyphs-Regular.ttf'
OUT = ROOT / 'assets/fonts/NotoSansEgyptianHieroglyphs-tomb.ttf'


def signs():
    text = (ROOT / 'src/core/Hieroglyphs.h').read_text()
    return sorted({int(h, 16) for h in re.findall(r'0x(1[0-9A-Fa-f]{4})\b', text)})


def tables(data):
    n = struct.unpack('>H', data[4:6])[0]
    out = {}
    for i in range(n):
        tag, _, off, length = struct.unpack('>4sIII', data[12 + 16 * i:28 + 16 * i])
        out[tag.decode('latin-1')] = data[off:off + length]
    return out


def unicode_map(cmap):
    """codepoint -> glyph id, from the font's format 12 (full repertoire) Unicode subtable"""
    for i in range(struct.unpack('>H', cmap[2:4])[0]):
        pid, eid, off = struct.unpack('>HHI', cmap[4 + 8 * i:12 + 8 * i])
        if struct.unpack('>H', cmap[off:off + 2])[0] == 12 and (pid, eid) in ((3, 10), (0, 4), (0, 6)):
            out = {}
            for g in range(struct.unpack('>I', cmap[off + 12:off + 16])[0]):
                first, last, gid = struct.unpack('>III', cmap[off + 16 + 12 * g:off + 28 + 12 * g])
                for cp in range(first, last + 1):
                    out[cp] = gid + cp - first
            return out
    raise SystemExit('no format 12 Unicode cmap in ' + SRC)


def outlines(t):
    count = struct.unpack('>H', t['maxp'][4:6])[0]
    if struct.unpack('>h', t['head'][50:52])[0] == 1:
        offs = struct.unpack('>%dI' % (count + 1), t['loca'][:4 * (count + 1)])
    else:
        offs = [2 * o for o in struct.unpack('>%dH' % (count + 1), t['loca'][:2 * (count + 1)])]
    return [t['glyf'][offs[i]:offs[i + 1]] for i in range(count)]


def components(g):
    """a composite glyph's parts: (glyph id, where that id is stored in the glyph)"""
    out = []
    if len(g) < 10 or struct.unpack('>h', g[:2])[0] >= 0:
        return out
    p = 10
    while True:
        flags, gid = struct.unpack('>HH', g[p:p + 4])
        out.append((gid, p + 2))
        p += 4 + (4 if flags & 0x1 else 2)                                              # ARG_1_AND_2_ARE_WORDS
        p += 2 if flags & 0x8 else 4 if flags & 0x40 else 8 if flags & 0x80 else 0  # a scale, x and y, a 2x2
        if not flags & 0x20:                                                            # MORE_COMPONENTS
            return out


def checksum(b):
    b += b'\0' * (-len(b) % 4)
    return sum(struct.unpack('>%dI' % (len(b) // 4), b)) & 0xFFFFFFFF


def main():
    t = tables(Path(SRC).read_bytes())
    cmap, glyphs, want = unicode_map(t['cmap']), outlines(t), signs()
    missing = [hex(c) for c in want if c not in cmap]
    if missing:
        raise SystemExit('not in the font: ' + ', '.join(missing))
    keep, todo = {0}, [cmap[c] for c in want]  # (.notdef first, as every font has it)
    while todo:
        g = todo.pop()
        if g not in keep:
            keep.add(g)
            todo += [c for c, _ in components(glyphs[g])]
    order = [0] + sorted(keep - {0})
    new = {old: i for i, old in enumerate(order)}

    glyf, loca = b'', [0]
    for old in order:
        g = bytearray(glyphs[old])
        for c, at in components(bytes(g)):
            struct.pack_into('>H', g, at, new[c])
        glyf += bytes(g) + b'\0' * (-len(g) % 4)
        loca.append(len(glyf))
    metrics, hm = struct.unpack('>H', t['hhea'][34:36])[0], t['hmtx']

    def hmetric(g):  # (advance, left side bearing); past the last full metric the advance repeats
        if g < metrics:
            return struct.unpack('>Hh', hm[4 * g:4 * g + 4])
        lsb = struct.unpack('>h', hm[4 * metrics + 2 * (g - metrics):4 * metrics + 2 * (g - metrics) + 2])[0]
        return struct.unpack('>H', hm[4 * metrics - 4:4 * metrics - 2])[0], lsb

    head, hhea, maxp = bytearray(t['head']), bytearray(t['hhea']), bytearray(t['maxp'])
    struct.pack_into('>I', head, 8, 0)    # checkSumAdjustment (set once the font is laid out)
    struct.pack_into('>h', head, 50, 1)   # long loca offsets
    struct.pack_into('>H', hhea, 34, len(order))
    struct.pack_into('>H', maxp, 4, len(order))
    pairs = sorted((c, new[cmap[c]]) for c in want)
    sub = struct.pack('>HHIII', 12, 0, 16 + 12 * len(pairs), 0, len(pairs)) + b''.join(struct.pack('>III', c, c, g) for c, g in pairs)
    post = bytearray(t['post'][:32])
    struct.pack_into('>I', post, 0, 0x00030000)  # format 3: no glyph names
    out = {
        'cmap': struct.pack('>HHHHI', 0, 1, 3, 10, 12) + sub,
        'glyf': glyf,
        'head': bytes(head),
        'hhea': bytes(hhea),
        'hmtx': b''.join(struct.pack('>Hh', *hmetric(g)) for g in order),
        'loca': struct.pack('>%dI' % len(loca), *loca),
        'maxp': bytes(maxp),
        'name': t['name'],
        'OS/2': t['OS/2'],
        'post': bytes(post),
    }

    tags = sorted(out)
    sel = max(k for k in range(len(tags) + 1) if 2 ** k <= len(tags))
    font = bytearray(struct.pack('>IHHHH', 0x00010000, len(tags), 16 * 2 ** sel, sel, 16 * len(tags) - 16 * 2 ** sel))
    body, at, head_at = b'', 12 + 16 * len(tags), 0
    for tag in tags:
        b = out[tag]
        if tag == 'head':
            head_at = at + len(body)
        font += struct.pack('>4sIII', tag.encode('latin-1'), checksum(b), at + len(body), len(b))
        body += b + b'\0' * (-len(b) % 4)
    font += body
    struct.pack_into('>I', font, head_at + 8, (0xB1B0AFBA - checksum(bytes(font))) & 0xFFFFFFFF)
    OUT.write_bytes(bytes(font))
    print('%s: %d signs, %d glyphs, %d bytes' % (OUT.relative_to(ROOT), len(want), len(order), len(font)))


if __name__ == '__main__':
    main()
