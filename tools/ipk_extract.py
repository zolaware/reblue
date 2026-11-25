#!/usr/bin/env python3
"""List or extract entries from Blue Dragon IPK1 pack archives.

Layout: 'IPK1', u32 dir offset (0x80), u32 entry count, u32 total size, then
the directory at 0x10 with a 0x60 stride: a 0x40-byte NUL-padded name followed
by four u32 (flags, id, data offset, data size). Entry data is zlib-compressed.
Text comes in two flavours: the dev-authored CSVs under bd_gamedat are
Shift-JIS, while the shipped .u16 message tables are UTF-16 BIG-endian with a
BOM, this being a 360 title. Decoding a .u16 as little-endian yields text that
looks almost right in places, so check the BOM rather than the eyeball.

  ipk_extract.py list  <pack.ipk> [substring]
  ipk_extract.py dump  <pack.ipk> <substring> [outdir]
"""
import os, struct, sys, zlib

STRIDE, DIR = 0x60, 0x10


def entries(path):
    d = open(path, "rb").read()
    if d[:4] != b"IPK1":
        raise SystemExit(f"{path}: not an IPK1 archive")
    count = struct.unpack_from("<I", d, 8)[0]
    for i in range(count):
        o = DIR + i * STRIDE
        name = d[o:o + 0x40].split(b"\0")[0].decode("latin1")
        _, _, off, size = struct.unpack_from("<4I", d, o + 0x40)
        yield name, d[off:off + size]


def decode(blob):
    """Inflate if compressed; decode as Shift-JIS when it looks like text."""
    try:
        blob = zlib.decompress(blob)
    except zlib.error:
        pass
    return blob


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    cmd, pack = sys.argv[1], sys.argv[2]
    needle = sys.argv[3] if len(sys.argv) > 3 else ""
    if cmd == "list":
        for name, blob in entries(pack):
            if needle in name:
                print(f"{len(blob):>9}  {name}")
    elif cmd == "dump":
        outdir = sys.argv[4] if len(sys.argv) > 4 else "."
        for name, blob in entries(pack):
            if needle not in name:
                continue
            raw = decode(blob)
            dest = os.path.join(outdir, os.path.basename(name.replace("\\", "/")))
            with open(dest, "wb") as f:
                f.write(raw)
            print(f"wrote {dest} ({len(raw)} bytes)")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
