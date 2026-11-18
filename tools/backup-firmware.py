#!/usr/bin/env python3
"""Identify the board, then save its whole flash and prove the copy is good.

Run this **before the first flash**: whatever the board came with — a vendor demo, a previous
firmware — is overwritten and cannot be downloaded again from anywhere.

    tools/backup-firmware.py --port /dev/cu.usbserial-XXXX

It reads the chip, its revision, the flash size and the MAC, says which TFT Panel image that
board takes, dumps the entire flash, writes a `.sha256` beside it, and then **reads the flash
back and compares it against the file**. A hash of what we just wrote proves nothing about the
device; `verify-flash` does, because it is the board that is asked again.

Nothing is written to the board. Exit code 0 only when the copy is verified.

⚠️ The copy is the **whole** flash, which includes the panel's settings: the Wi-Fi password it was
given, any access token, and its pairing with your music server. Restoring it elsewhere hands that
board the same credentials. Keep the file as you keep a password, and never attach one to a bug
report — a firmware image alone is `tools/…/firmware.bin`, which carries none of this.

    --out DIR     where to keep it (default ~/tft-panel-backups)
    --baud N      serial speed (default: for a read, what the board's adapter tolerates over a
                  whole flash dump; for --restore, the speed the board is flashed at)
    --restore F   print the exact command to put a backup back, and stop. The board comes from
                  the file's name (this tool names it after the board); only a renamed file
                  makes it ask the board, which restarts it.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from collections import namedtuple
from datetime import date

PY = sys.executable

# What each chip and flash size means here. The two ESP32-P4 boards differ only by flash size,
# which is exactly why the browser installer cannot tell them apart.
# Reading is not writing: a CH340 that flashes happily at 460800 loses bytes somewhere in four
# megabytes of read ("Serial data stream stopped"), and a backup that stops early is not a backup.
# The VIEWE's CH340 even loses them at 230400 over its 16 MB in one go (died after 284 KB), so it
# is read in retryable 512 KB pieces, which worked with one retry in 32. Measured on the boards.
# flash_baud is the board's upload_speed in platformio.ini; keep them equal (the test pins all three).
Board = namedtuple("Board", "name env asset read_baud flash_baud piece")
BOARDS = {
    ("ESP32-S3", 4): Board("Elecrow CrowPanel 7\" V3.0", "crowpanel7", "tft-panel",
                           230400, 460800, None),
    ("ESP32-P4", 32): Board("Waveshare ESP32-P4-WIFI6-Touch-LCD-7B", "waveshare-p4-7b",
                            "tft-panel-waveshare-p4-7b", 921600, 921600, None),
    ("ESP32-P4", 16): Board("VIEWE UEP4S070H1024V600C-WB-C", "p4-s070-wba",
                            "tft-panel-p4-s070-wba", 230400, 230400, 512 * 1024),
}
UNKNOWN = Board("unknown board", "", "", 115200, 115200, None)
ATTEMPTS = 3   # per piece, in total; a whole-flash read gets them too


def esptool(args, port, baud=None, capture=True, after=None):
    cmd = [PY, "-m", "esptool", "--port", port]
    if baud:
        cmd += ["--baud", str(baud)]
    if after:
        cmd += ["--after", after]
    cmd += args
    if capture:
        r = subprocess.run(cmd, capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr
    return subprocess.call(cmd), ""


def identify(port):
    """Chip, revision, flash megabytes and MAC, read from the board itself."""
    code, out = esptool(["flash-id"], port)
    if code != 0:
        raise SystemExit("Could not talk to the board on %s.\n%s" % (port, out.strip()))
    chip = rev = mac = None
    mb = 0
    for line in out.splitlines():
        # esptool 5 prints "Chip type: ESP32-S3 (QFN56) (revision v0.2)"; esptool 4, "Chip is …".
        m = (re.search(r"Chip type:\s*(ESP32\S*)", line) or
             re.search(r"Detecting chip type\.\.\.\s*(ESP32\S*)", line) or
             re.search(r"^Chip is (ESP32\S*)", line))
        if m and not chip:
            chip = m.group(1)
        m = re.search(r"\(revision v?([\d.]+)\)", line)
        if m and not rev:
            rev = m.group(1)
        m = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", line)
        if m and not mac:
            mac = m.group(1).lower()
        m = re.search(r"Detected flash size:\s*(\d+)MB", line)
        if m:
            mb = int(m.group(1))
    if not chip or not mb:
        raise SystemExit("The board answered, but not with a chip and a flash size:\n" + out.strip())
    return chip, rev, mb, mac


def read_flash(port, baud, size, piece, path):
    """Dump `size` bytes of flash to `path`, `piece` bytes per esptool call (None: one call).

    A piece that fails or comes back short is asked again, up to ATTEMPTS attempts. `path` only
    appears once every piece has arrived; on failure nothing is left behind. Returns success.
    Every read leaves the chip in its bootloader (`--after no-reset`): the firmware must not run,
    and maybe write its settings, between two pieces. The verify that follows restarts it.
    """
    step = piece or size
    plan = [(off, min(step, size - off)) for off in range(0, size, step)]
    partial, part = path + ".partial", path + ".piece"
    try:
        with open(partial, "wb") as out:
            for n, (off, length) in enumerate(plan, 1):
                for attempt in range(1, ATTEMPTS + 1):
                    if os.path.exists(part):
                        os.remove(part)
                    code, log = esptool(["read-flash", hex(off), str(length), part], port, baud,
                                        capture=len(plan) > 1, after="no-reset")
                    if code == 0 and os.path.exists(part) and os.path.getsize(part) == length:
                        break
                    what = "piece %d of %d (0x%x)" % (n, len(plan), off) if len(plan) > 1 else "read"
                    print("  %s failed, attempt %d of %d" % (what, attempt, ATTEMPTS))
                else:
                    print(log.strip())
                    return False
                with open(part, "rb") as f:
                    shutil.copyfileobj(f, out)
                if len(plan) > 1:
                    print("  piece %d of %d read" % (n, len(plan)))
        os.replace(partial, path)
        return True
    finally:
        for leftover in (part, partial):
            if os.path.exists(leftover):
                os.remove(leftover)


def print_restore(port, backup, baud):
    named = os.path.basename(backup).split("-factory-")[0]
    board = next((b for b in BOARDS.values() if b.env == named), None)
    if not board:
        print("The file's name does not say which board it came from: asking the board (this "
              "restarts it)...")
        try:
            chip, _, mb, _ = identify(port)
            board = BOARDS.get((chip, mb), UNKNOWN)
        except SystemExit as e:
            print("%s\nNo answer: the command below uses a slow, safe speed." % e)
            board = UNKNOWN
    print("Put this backup back on the board (%s), then check it:\n" % board.name)
    print("  %s -m esptool --port %s --baud %d write-flash 0x0 %s"
          % (PY, port, baud or board.flash_baud, backup))
    print("  %s -m esptool --port %s verify-flash 0x0 %s" % (PY, port, backup))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--out", default=os.path.expanduser("~/tft-panel-backups"))
    ap.add_argument("--baud", type=int)
    ap.add_argument("--restore", metavar="FILE")
    a = ap.parse_args()

    if a.restore:
        return print_restore(a.port, a.restore, a.baud)

    print("Asking the board what it is (this restarts it)...")
    chip, rev, mb, mac = identify(a.port)
    known = BOARDS.get((chip, mb))
    board = known or UNKNOWN

    print("  chip        %s%s" % (chip, " rev " + rev if rev else ""))
    print("  flash       %d MB" % mb)
    print("  MAC         %s" % (mac or "unknown"))
    if known:
        print("  board       %s" % board.name)
        print("  its image   %s  (pio run -e %s)" % (board.asset, board.env))
    else:
        print("  board       not one of the three this firmware supports.")
        print("              Back it up anyway, but do not flash TFT Panel onto it: a panel")
        print("              driven by the wrong settings stays dark. docs/HARDWARE.md explains.")

    baud = a.baud or board.read_baud
    env = board.env
    os.makedirs(a.out, exist_ok=True)
    stem = "%s-factory-%s-%s" % (env or chip.lower(), (mac or "nomac").replace(":", ""), date.today())
    path = os.path.join(a.out, stem + ".bin")
    size = mb * 1024 * 1024

    if os.path.exists(path):
        sys.exit("There is already a backup at %s — it is not overwritten. Move it or pass --out."
                 % path)

    print("\nReading all %d MB at %d baud. This takes a few minutes; do not unplug the board." %
          (mb, baud))
    if not read_flash(a.port, baud, size, board.piece, path):
        sys.exit("The read failed, so there is no backup. Nothing was written to the board; "
                 "unplug and replug it to restart it.")
    got = os.path.getsize(path)
    if got != size:
        sys.exit("The file is %d bytes, not the %d the board reports: the read was cut short, "
                 "and a partial backup is not a backup. Delete %s and run this again."
                 % (got, size, path))

    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    sha = digest.hexdigest()
    with open(path + ".sha256", "w") as f:
        f.write("%s  %s\n" % (sha, os.path.basename(path)))

    print("\nReading the flash back to compare it with the file...")
    code, out = esptool(["verify-flash", "0x0", path], a.port, baud)
    if code != 0:
        print(out.strip())
        sys.exit("The board does not read back the same bytes, so this copy cannot be trusted. "
                 "Delete %s and run this again, with --baud 115200 if it keeps happening." % path)

    print("\nVerified against the board.")
    print("  %s" % path)
    print("  sha256 %s" % sha)
    print("\n  This file holds the panel's settings as well as its firmware: the Wi-Fi password")
    print("  it was given, any token, and its pairing with your music server. Keep it like a")
    print("  password, and never attach it to a bug report.")
    print("\nPut it back one day with:")
    print("  tools/backup-firmware.py --port %s --restore %s" % (a.port, path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
