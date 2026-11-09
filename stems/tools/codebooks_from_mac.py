#!/usr/bin/env python3
# Writes codebooks.bin (stems/README.md) from the Mac game's binary, for tests without the game: the mod writes the
# same file from the running game. The binary and the file are game data: keep both out of the repository.
#
# usage: codebooks_from_mac.py <Rocksmith2014 Mac binary, decrypted> <out codebooks.bin>
#
# The Wwise Vorbis codebook library is a table of 599 pointers in __DATA, each to where a codebook starts in one block
# of __TEXT __const. The table doesn't say where the last one ends, so, as the mod does, the data runs a little past
# the last start (SLACK bytes); the decoder reads a codebook by its bits, not by its size.
import struct
import sys

TABLE = 0x1018AD3C0      # the pointer table's address in the 2014 Remastered Mac binary
COUNT = 599
SLACK = 0x400            # as the mod's CODEBOOK_SLACK


def segments(data):
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", data, 0)
    if magic != 0xFEEDFACF:
        sys.exit("not a 64-bit Mach-O (a fat or encrypted binary?)")
    off, out = 32, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", data, off + 24)
            out.append((vmaddr, vmsize, fileoff, filesize))
        off += size
    return out


def file_offset(segs, addr):
    for vmaddr, vmsize, fileoff, filesize in segs:
        if vmaddr <= addr < vmaddr + filesize:
            return fileoff + addr - vmaddr
    sys.exit(f"address {addr:#x} is in no segment's file data")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__ or "usage: codebooks_from_mac.py <binary> <out>")
    data = open(sys.argv[1], "rb").read()
    segs = segments(data)
    t = file_offset(segs, TABLE)
    ptrs = struct.unpack_from(f"<{COUNT}Q", data, t)
    first = ptrs[0]
    offsets = [p - first for p in ptrs]
    if any(b < a for a, b in zip(offsets, offsets[1:])) or offsets[:2] != [0, 8]:
        sys.exit("the table doesn't look like the codebook library (another build of the game?)")
    start = file_offset(segs, first)
    blob = data[start:start + offsets[-1] + SLACK]
    with open(sys.argv[2], "wb") as f:
        f.write(b"RSCB" + struct.pack(f"<I{COUNT}I", COUNT, *offsets) + blob)
    print(f"{sys.argv[2]}: {COUNT} codebooks, {len(blob)} bytes of data")


if __name__ == "__main__":
    main()
