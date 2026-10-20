"""把 shot.exe 输出的原始 BGRA 位图转成 PNG（仅依赖标准库）。"""

import struct
import sys
import zlib


def chunk(tag: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def main() -> int:
    src = sys.argv[1] if len(sys.argv) > 1 else "build/shot.raw"
    dst = sys.argv[2] if len(sys.argv) > 2 else "build/shot.png"

    with open(src, "rb") as f:
        w, h = struct.unpack("<ii", f.read(8))
        data = f.read(w * h * 4)

    if len(data) < w * h * 4:
        print(f"short read: {len(data)} < {w * h * 4}")
        return 1

    rows = bytearray()
    stride = w * 4
    for y in range(h):
        base = y * stride
        rgb = bytearray(stride)
        for x in range(w):
            o = base + x * 4
            d = x * 4
            rgb[d] = data[o + 2]
            rgb[d + 1] = data[o + 1]
            rgb[d + 2] = data[o]
            rgb[d + 3] = 255
        rows += b"\x00" + rgb

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")

    with open(dst, "wb") as f:
        f.write(png)
    print(f"{dst} {w}x{h}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
