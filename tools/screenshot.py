#!/usr/bin/env python3
"""Decode a TFT Panel `screenshot` console dump into a PNG (standard library only).

The firmware prints "[shot] begin W H", base64 lines starting with "~" (RLE pairs:
u16 count, u16 RGB565, little endian) and "[shot] end <pairs>". Other log lines may
appear in between and are ignored.

    tools/screenshot.py serial.log out.png      # decode the last dump in a captured log
    tools/screenshot.py --port /dev/cu.usbserial-XXXX out.png   # ask the panel and decode

Screenshots can show lyrics: never commit them (copyright).
"""
import base64
import re
import struct
import sys
import time
import zlib


LOG_RE = re.compile(r"\[\s*\d+\]\[[EWIDV]\]")
B64_RE = re.compile(r"[A-Za-z0-9+/=]+")


def decode(lines):
    start = max((i for i, l in enumerate(lines) if l.startswith("[shot] begin")), default=None)
    if start is None:
        raise SystemExit("no '[shot] begin' in the input")
    w, h = map(int, lines[start].split()[2:4])
    raw, end_pairs = bytearray(), None
    body = lines[start + 1:]
    i = 0
    while i < len(body):
        l = body[i]
        if l.startswith("[shot] end"):
            end_pairs = int(l.split()[2])
            break
        if l.startswith("~"):
            data = l[1:].strip()
            # An ESP-IDF log line ("[  123][E][file.cpp:1] ...") can cut a data line in two:
            # drop the log text and glue the remainder from the next line back on.
            m = LOG_RE.search(data)
            if m:
                data = data[:m.start()]
                if i + 1 < len(body) and B64_RE.fullmatch(body[i + 1].strip()):
                    i += 1
                    data += body[i].strip()
            raw += base64.b64decode(data)
        i += 1
    if end_pairs is None:
        raise SystemExit("dump truncated: no '[shot] end'")
    pairs = len(raw) // 4
    if pairs != end_pairs:
        raise SystemExit(f"dump corrupted: {pairs} pairs decoded, firmware sent {end_pairs}")
    px = bytearray()
    for i in range(pairs):
        count, c = struct.unpack_from("<HH", raw, i * 4)
        r, g, b = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
        px += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))) * count
    if len(px) != w * h * 3:
        raise SystemExit(f"pixel count {len(px) // 3} != {w}x{h}")
    return w, h, px


def write_png(path, w, h, rgb):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    rows = b"".join(b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def from_port(port):
    import serial  # pyserial (bundled with PlatformIO's Python)
    s = serial.Serial()
    s.port, s.baudrate, s.timeout, s.dtr, s.rts = port, 115200, 0.5, False, False
    s.open()
    s.reset_input_buffer()
    s.write(b"screenshot\n")
    buf, deadline = b"", time.time() + 120
    while time.time() < deadline and b"[shot] end" not in buf:
        buf += s.read(65536)
    buf += s.read(256)
    s.close()
    return buf.decode("utf-8", "replace").replace("\r", "").splitlines()


def main(argv):
    if len(argv) == 4 and argv[1] == "--port":
        lines, out = from_port(argv[2]), argv[3]
    elif len(argv) == 3:
        with open(argv[1], "rb") as f:
            lines = f.read().decode("utf-8", "replace").replace("\r", "").splitlines()
        out = argv[2]
    else:
        raise SystemExit(__doc__)
    w, h, px = decode(lines)
    write_png(out, w, h, px)
    print(f"{out}: {w}x{h}")


if __name__ == "__main__":
    main(sys.argv)
