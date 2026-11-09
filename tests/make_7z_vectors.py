#!/usr/bin/env python3
"""Writes tests/vectors_7z.h: LZMA and LZMA2 streams made by liblzma (Python's lzma), and a small 7z archive laid out as
the game's cache archives are (one file per folder, each LZMA, the CRCs in the substreams, the header packed with
LZMA), with an empty file and a name with a backslash. test_7z.c decodes them. Only needed to make them again:

    python3 tests/make_7z_vectors.py > tests/vectors_7z.h
"""
import lzma
import struct
import zlib

WORDS = ["tuning", "string", "tone", "lead", "rhythm", "bass", "standard", "drop", "open", "the", "a", "and",
         "Guitarcade", "manifest", "json", "{", "}", "\"", ":", ",", "\n", "\t", "0", "-5", "E", "B", "C#"]


def text(n, seed):
    """The same text as test_7z.c's make_text"""
    s, out = seed, bytearray()
    while len(out) < n:
        s = (s * 1103515245 + 12345) & 0xFFFFFFFF
        for ch in WORDS[(s >> 16) % len(WORDS)].encode():
            if len(out) < n:
                out.append(ch)
        if len(out) < n:
            out.append(ord(" "))
    return bytes(out)


def noise(n, seed):
    """The same bytes as test_7z.c's make_noise"""
    s, out = seed, bytearray()
    for _ in range(n):
        s = (s * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((s >> 16) & 0xFF)
    return bytes(out)


def raw(data, **filt):
    return lzma.compress(data, lzma.FORMAT_RAW, filters=[filt])


def wnum(v):
    for n in range(8):
        if v < 1 << (7 * (n + 1)):
            return bytes([((0xFF00 >> n) & 0xFF) | (v >> (8 * n))]) + v.to_bytes(8, "little")[:n]
    return b"\xff" + v.to_bytes(8, "little")


def lzma1(data):
    return raw(data, id=lzma.FILTER_LZMA1, lc=3, lp=0, pb=2, dict_size=1 << 20), b"\x5d\x00\x00\x10\x00"


def archive(files):
    """files: [(name, data)]; an empty data is an empty file"""
    full = [(n, d) for n, d in files if d]
    packs = [lzma1(d) for _, d in full]
    h = bytearray(b"\x01\x04\x06" + wnum(0) + wnum(len(packs)) + b"\x09")
    for p, _ in packs:
        h += wnum(len(p))
    h += b"\x00\x07\x0b" + wnum(len(packs)) + b"\x00"
    for _, props in packs:
        h += b"\x01\x23\x03\x01\x01" + wnum(len(props)) + props
    h += b"\x0c"
    for _, d in full:
        h += wnum(len(d))
    h += b"\x00\x08\x0a\x01" + b"".join(struct.pack("<I", zlib.crc32(d)) for _, d in full) + b"\x00\x00"
    n = len(files)
    h += b"\x05" + wnum(n)
    empty = [not d for _, d in files]
    bits = bytearray((n + 7) // 8)
    for i, e in enumerate(empty):
        if e:
            bits[i // 8] |= 0x80 >> (i % 8)
    h += b"\x0e" + wnum(len(bits)) + bits
    ne = sum(empty)
    h += b"\x0f" + wnum((ne + 7) // 8) + bytes([0xFF << (8 - ne) & 0xFF] if ne else [])
    names = b"\x00" + b"".join(name.encode("utf-16-le") + b"\x00\x00" for name, _ in files)
    h += b"\x11" + wnum(len(names)) + names
    times = b"\x01\x00" + b"".join(struct.pack("<Q", 132000000000000000 + i) for i in range(n))
    h += b"\x14" + wnum(len(times)) + times
    attrs = b"\x01\x00" + b"".join(struct.pack("<I", 0x20) for _ in range(n))
    h += b"\x15" + wnum(len(attrs)) + attrs
    h += b"\x00\x00"
    body = b"".join(p for p, _ in packs)
    hp, hprops = lzma1(bytes(h))
    eh = (b"\x17\x06" + wnum(len(body)) + wnum(1) + b"\x09" + wnum(len(hp)) + b"\x00"
          + b"\x07\x0b" + wnum(1) + b"\x00\x01\x23\x03\x01\x01" + wnum(len(hprops)) + hprops
          + b"\x0c" + wnum(len(h)) + b"\x0a\x01" + struct.pack("<I", zlib.crc32(bytes(h))) + b"\x00\x00")
    start = struct.pack("<QQI", len(body) + len(hp), len(eh), zlib.crc32(eh))
    return b"7z\xbc\xaf\x27\x1c\x00\x04" + struct.pack("<I", zlib.crc32(start)) + start + body + hp + eh


def emit(name, data):
    print("static const unsigned char %s[%d] = {" % (name, len(data)))
    for i in range(0, len(data), 24):
        print("    " + ", ".join("0x%02x" % b for b in data[i:i + 24]) + ",")
    print("};")


print("// Made by tests/make_7z_vectors.py (liblzma, through Python's lzma): do not edit.")
emit("LZMA_TEXT", raw(text(6000, 1), id=lzma.FILTER_LZMA1, lc=3, lp=0, pb=2, dict_size=1 << 16))
emit("LZMA_LC0_LP2", raw(text(3000, 2), id=lzma.FILTER_LZMA1, lc=0, lp=2, pb=0, dict_size=1 << 16))
emit("LZMA2_TEXT", raw(text(4000, 3), id=lzma.FILTER_LZMA2, dict_size=1 << 16))
emit("LZMA2_NOISE", raw(noise(600, 4), id=lzma.FILTER_LZMA2, dict_size=1 << 16))
emit("ARCHIVE", archive([("manifests/a.json", text(3000, 5)), ("empty.txt", b""),
                         ("manifests\\b.json", text(500, 6))]))
