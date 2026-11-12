#!/usr/bin/env python3
"""Capture TFT Panel screens with invented content, for the user guide and promotion.

Drives the panel's USB console (pyserial, bundled with PlatformIO's Python) and fetches each
frame over Wi-Fi (GET /screenshot, enabled by the console only). The panel runs its showcase:
an invented track, original lyrics, invented zones and a generated cover, so the pictures can
be published. Colour warmth is set to 0 % during the capture (true UI colours) and restored.

    tools/showcase.py --port /dev/cu.usbserial-XXXX --host 192.168.1.42 [--out docs/images]
    tools/showcase.py ... --frames out/frames      # also the lyric-scroll animation frames

Close any other program using the serial port first (it would steal the console).
"""
import argparse
import os
import re
import struct
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshot import write_png  # noqa: E402

SCREENS = [  # file name, console commands before the capture
    ("lyrics", ["show none", "show lyrics"]),
    ("cover-view", ["show none", "show cover"]),
    ("controls", ["show none", "show lyrics", "show controls"]),
    ("zones", ["show none", "show zones"]),
    ("volume", ["show none", "show volume"]),
    ("lyrics-timing", ["show none", "show offset"]),
    ("settings", ["show none", "show settings"]),
    ("colour-test", ["show none", "show colours"]),
    ("idle-clock", ["show none", "show clock"]),
    ("update-available", ["show none", "show update"]),
    ("update-confirm", ["show none", "show confirm"]),
    ("update-progress", ["show none", "show progress"]),
    ("wifi-setup", ["show none", "show setup"]),
    ("wifi-list", ["show none", "show wifi"]),
    ("wifi-keyboard", ["show none", "show wifi-password"]),
    # Waveshare P4 only (HAS_LIBRARY): unknown screens on the CrowPanel.
    ("library", ["show none", "show library"]),
    ("library-actions", ["show none", "show library-actions"]),
    ("upload-code", ["show none", "show code"]),
]


class Console:
    def __init__(self, port):
        import serial  # pyserial
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.2
        # DTR high, RTS low opens both boards without a reset (other states reset the P4, whose
        # settings read before the reset were then lost).
        self.s.dtr, self.s.rts = True, False
        self.s.open()
        self.s.reset_input_buffer()

    def run(self, cmd, wait=0.6):
        self.s.write(cmd.encode() + b"\n")
        end, out = time.time() + wait, b""
        while time.time() < end:
            out += self.s.read(4096)
        return out.decode("utf-8", "replace")

    def close(self):
        self.s.close()


def grab(host):
    with urllib.request.urlopen(f"http://{host}/screenshot", timeout=20) as r:
        data = r.read()
    if data[:4] != b"TFTS":
        raise SystemExit(f"unexpected screenshot answer: {data[:60]!r}")
    w, h = struct.unpack_from("<HH", data, 4)
    px = memoryview(data)[8:]
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        c = px[2 * i] | (px[2 * i + 1] << 8)
        r, g, b = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
        rgb[3 * i:3 * i + 3] = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return w, h, rgb


def save(host, path):
    w, h, rgb = grab(host)
    write_png(path, w, h, rgb)
    print(f"  {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--host", required=True, help="panel IP or tft-panel-xxxx.local")
    ap.add_argument("--out", default="docs/images")
    ap.add_argument("--frames", help="directory for the lyric-scroll animation frames")
    ap.add_argument("--position", type=int, default=98000, help="showcase position, ms")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    con = Console(a.port)
    # The warmth is set to 0 for neutral pictures and put back after: never guess it.
    m = None
    for _ in range(3):
        m = re.search(r"warmth (\d+) %", con.run("status", 1.5))
        if m:
            break
    if not m:
        raise SystemExit("could not read the panel's colour warmth from `status`: nothing changed")
    warmth = int(m.group(1))
    try:
        con.run("shots on")
        con.run("warmth 0")
        con.run("demo", 2.5)              # first time: builds the generated cover (~1 s)
        con.run(f"demo pos {a.position}", 1.0)
        for name, cmds in SCREENS:
            for c in cmds:
                con.run(c, 0.8)
            time.sleep(0.6)
            save(a.host, os.path.join(a.out, f"{name}.png"))
        if a.frames:
            os.makedirs(a.frames, exist_ok=True)
            con.run("show none", 0.8)
            n = 0

            def frame():
                nonlocal n
                save(a.host, os.path.join(a.frames, f"f{n:03d}.png"))
                n += 1

            # Two line changes, each as 11 slide frames, then the swipe to the cover view.
            con.run("demo manual on")
            pos = a.position
            for _ in range(2):
                frame()
                pos += 4600                     # next line (showcase timing)
                con.run(f"demo pos {pos}", 0.8)
                for p in range(0, 101, 10):
                    con.run(f"demo slide {p}", 0.3)
                    frame()
            con.run("show cover", 1.0)
            frame()
            con.run("demo manual off")
            print(f"  {n} frames in {a.frames}")
    finally:
        con.run("show none")
        con.run("demo off", 1.0)
        con.run(f"warmth {warmth}")
        con.run("shots off")
        con.close()


if __name__ == "__main__":
    main()
