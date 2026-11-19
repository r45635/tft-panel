#!/usr/bin/env python3
"""Host test for tools/flashmap.py: no board, no build, no toolchain.

    python3 -I tools/test_flashmap.py

Every case here is a trap that produced a confident wrong table while the tool was written, so
each one is a regression test for a specific lie the parser used to tell. The synthetic map
below is deliberately shaped like the real one, including the bits that are easy to read past.
"""

import io
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flashmap                                               # noqa: E402

FAILED = []


def check(name, got, want):
    if got != want:
        FAILED.append("%s: got %r, want %r" % (name, got, want))
        print("  FAIL %s: got %r, want %r" % (name, got, want))
    else:
        print("  ok   %s" % name)


# A map with, in order: the preamble that mentions object files (trap 1), a discarded block whose
# sections sit at address 0 (trap 1), a real section, an output section whose name is alone on its
# line (trap 2), a merged string pool with its "(size before relaxing)" line and two more
# contributors inside it (trap 3), and a debug section that is in the ELF but never flashed
# (trap 4).
MAP = """Archive member included to satisfy reference by file (symbol)

.pio/build/demo/lib456/FS/FS.cpp.o (_ZdlPvj)

Discarded input sections

 .group         0x00000000       0x10 .pio/build/demo/lib456/FS/FS.cpp.o
 .text.unused   0x00000000      0x100 .pio/build/demo/src/app/ui.cpp.o

Memory Configuration

Linker script and memory map

.flash.text     0x42000020      0x300
 .text.a        0x42000020      0x100 .pio/build/demo/src/app/ui.cpp.o
 .text.b
                0x42000120      0x180 .pio/build/demo/lib89f/lvgl/core/lv_obj.c.o
 *fill*         0x420002a0       0x80
 *fill*         0x420002a0       0x40
.flash_rodata_dummy
                0x3c160000    0x160000
.flash.rodata   0x3c160020      0x200
 .rodata.pool.str1.1
                0x3c160020      0x180 .pio/build/demo/lib456/FS/FS.cpp.o
                                0x20 (size before relaxing)
 .rodata.more.str1.1
                0x3c1601a0       0x40 .pio/build/demo/src/net/net.cpp.o
 .rodata.tail   0x3c1601a0       0x40 .pio/build/demo/lib89f/lvgl/core/lv_obj.c.o
.debug_info     0x00000000    0x500000
 .debug_info    0x00000000    0x500000 .pio/build/demo/src/app/ui.cpp.o
"""

WANTED = {".flash.text", ".flash.rodata"}

print("parsing the synthetic map")
with tempfile.NamedTemporaryFile("w", suffix=".map", delete=False) as fh:
    fh.write(MAP)
    path = fh.name
bucket, obj, sect, fill = flashmap.parse(path, WANTED)
os.unlink(path)

# trap 2: the dummy section's 1.4 MB must not land in .flash.text, which is 0x100 + 0x180 of code
# plus 0x80 of fill.
check(".flash.text code", sect[".flash.text"], 0x100 + 0x180)
# The second *fill* lies inside the first, which is what RISC-V relaxation produces; counting
# both put a real section 1,829 bytes over its true size.
check(".flash.text fill, overlapping entry counted once", fill[".flash.text"], 0x80)

# trap 3: the pool is 0x180 wide, of which FS.cpp.o really owns 0x20; the rest belongs to no one
# object. .rodata.more begins exactly at the pool's end so it counts in full, and
# .rodata.tail then ends inside what .rodata.more already covered, so it counts for nothing.
# (At 0x80 it would run past that end and legitimately contribute the difference: the rule
# is per byte, not per entry.)
check("pool bucket", bucket[flashmap.POOL], 0x180 - 0x20)
check("FS.cpp.o own share", obj["FS.cpp.o"], 0x20)
check(".flash.rodata total", sect[".flash.rodata"], 0x180 + 0x40)
check("no double count for the overlapping tail", obj.get("lv_obj.c.o"), 0x180)

# trap 1: nothing from the preamble or the discarded block may appear anywhere.
check("discarded .text.unused not counted", bucket["the panel's own code (src/)"], 0x100 + 0x40)

# trap 4: a section outside `wanted` is never read, however large.
check(".debug_info ignored", sect.get(".debug_info"), None)

# owner() maps each kind of path to the thing that is really responsible.
print("attributing paths to their owner")
check("IDF archive", flashmap.owner("/x/y/libnet80211.a(wl_cnx.o)"),
      ("libnet80211.a", "libnet80211.a(wl_cnx.o)"))
check("a PlatformIO library", flashmap.owner(".pio/build/demo/lib89f/lvgl/core/lv_obj.c.o"),
      ("lvgl", "lv_obj.c.o"))
check("our own source", flashmap.owner(".pio/build/demo/src/net/net.cpp.o"),
      ("the panel's own code (src/)", "net.cpp.o"))
check("an embedded asset", flashmap.owner(".pio/build/demo/NotoSans-400.ttf.txt.o"),
      ("embedded assets (fonts, brand)", "NotoSans-400.ttf.txt.o"))
check("not an object file", flashmap.owner("0x1234 PROVIDE (x = .)"), (None, None))

# The check must refuse a table that does not add up: that refusal is the tool's only safeguard.
print("refusing a table that does not add up")
out = io.StringIO()
check("a section that is short is reported",
      flashmap.check({".flash.text": 1000}, {}, {".flash.text": 2000}, out), [".flash.text"])
check("a residual inside the tolerance passes",
      flashmap.check({".flash.rodata": 407434}, {}, {".flash.rodata": 407284}, out), [])

# flashed_sections() must read the ELF's flags, not a list of section names. Build a minimal
# ELF32 with one ALLOC+PROGBITS section, one ALLOC+NOBITS (.bss, not in the image) and one
# PROGBITS without ALLOC (debug info).
print("reading the flashed sections out of an ELF")


def tiny_elf():
    names = b"\0.flash.text\0.bss\0.debug_info\0.shstrtab\0"
    off = {n: names.index(b"\0" + n + b"\0") + 1 for n in (b".flash.text", b".bss",
                                                           b".debug_info", b".shstrtab")}
    secs = [(0, 0, 0, 0, 0, 0),
            (off[b".flash.text"], 1, 0x2, 0x42000000, 0x200, 1234),      # PROGBITS + ALLOC
            (off[b".bss"], 8, 0x3, 0x3fc80000, 0x200, 999),              # NOBITS + ALLOC
            (off[b".debug_info"], 1, 0x0, 0, 0x200, 555555),             # PROGBITS, no ALLOC
            (off[b".shstrtab"], 3, 0x0, 0, 0x200, len(names))]
    shoff = 0x200 + len(names)
    head = bytearray(0x34)
    head[0:4] = b"\x7fELF"
    head[4], head[5] = 1, 1
    struct.pack_into("<I", head, 0x20, shoff)
    struct.pack_into("<H", head, 0x2E, 40)
    struct.pack_into("<H", head, 0x30, len(secs))
    struct.pack_into("<H", head, 0x32, 4)
    body = bytearray(head) + bytearray(0x200 - len(head)) + names
    for s in secs:
        body += struct.pack("<6I", *s) + bytes(16)
    return bytes(body)


with tempfile.NamedTemporaryFile("wb", suffix=".elf", delete=False) as fh:
    fh.write(tiny_elf())
    path = fh.name
got = flashmap.flashed_sections(path)
os.unlink(path)
check("only ALLOC+PROGBITS is in the image", got, {".flash.text": 1234})

print()
if FAILED:
    print("%d check(s) failed:" % len(FAILED))
    for f in FAILED:
        print("  " + f)
    sys.exit(1)
print("all checks passed")
