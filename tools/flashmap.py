#!/usr/bin/env python3
"""Say where a built image's flash actually goes, by library and by object.

    tools/flashmap.py [env] [--objects N] [--libraries N]

The CrowPanel is the board that decides what this firmware can still grow (97.6 % of its OTA
slot at v0.1.20), and "Flash: 95.7 %" from the linker says nothing about which code is
responsible. This reads the link map PlatformIO already wrote next to the image.

Reading an ld map is full of traps, and every one of these produced a confident wrong table
while this script was being written:

  1. The map's preamble and its discarded-input-sections block list sections at address 0 that
     are never flashed. Counting them invented 138 KB of code that does not exist.
  2. An output section whose name sits alone on its line -- .flash_rodata_dummy, 1.4 MB of
     address-space reservation -- must still end the previous section, or every one of its bytes
     is charged to that section. This doubled .flash.text.
  3. ld MERGES identical read-only strings. The first contributor of a merged pool is printed
     with the size of the WHOLE pool, followed by a "(size before relaxing)" line giving its own
     real share; every later contributor is listed again at the same address inside that pool.
     Summing the entries charged 168 KB of strings shared by the whole firmware to Arduino's
     FS.cpp.o, which is how an obviously absurd number exposed the bug.
  4. Sections the ELF carries but the flasher never writes -- 30 MB of debug info here -- must be
     excluded, and the right test is not a list of section names but the ELF's own flags:
     a section is in the image when it is ALLOC and PROGBITS.

  5. firmware.map is not always this image's map. Every link that uses the build's flags writes
     it, and an IDE re-configure (PlatformIO regenerating idedata.json) runs CMake's compiler
     check, whose throw-away program is linked with those flags: on 2026-11-21 that replaced the
     CrowPanel's 15 MB map with a 7 KB one hours after the ELF was built. So the map must name
     this environment's own objects before anything is read from it.

So the totals are checked per section against the ELF before any table is printed, and the
script refuses to print one that does not add up. A wrong table is worse than no table: it is
the kind of thing that gets quoted in a roadmap for a week.
"""

import collections
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POOL = "(read-only strings merged across all objects)"
SHF_ALLOC, SHT_PROGBITS = 0x2, 1


def flashed_sections(elf):
    """{name: size} for the sections the flasher writes, from the ELF's own flags."""
    raw = open(elf, "rb").read()
    if raw[:4] != b"\x7fELF" or raw[4] != 1:
        sys.exit("%s is not a 32-bit ELF" % elf)
    end = "<" if raw[5] == 1 else ">"
    shoff, shentsize, shnum, shstrndx = (
        struct.unpack(end + "I", raw[0x20:0x24])[0],
        struct.unpack(end + "H", raw[0x2E:0x30])[0],
        struct.unpack(end + "H", raw[0x30:0x32])[0],
        struct.unpack(end + "H", raw[0x32:0x34])[0],
    )

    def header(i):
        o = shoff + i * shentsize
        return struct.unpack(end + "6I", raw[o:o + 24])      # name, type, flags, addr, off, size

    stroff = header(shstrndx)[4]

    def name_at(n):
        return raw[stroff + n:raw.index(b"\0", stroff + n)].decode()

    out = {}
    for i in range(shnum):
        name, typ, flags, _addr, _off, size = header(i)
        if size and typ == SHT_PROGBITS and flags & SHF_ALLOC:
            out[name_at(name)] = size
    return out


def owner(path):
    """(bucket, object) for a map path.

    An IDF archive names itself. Everything PlatformIO compiles arrives as a plain object under
    .pio/build/<env>/, where the directory is the real owner: lib<hash>/lvgl is LVGL, not "our
    code", and lumping the two together hid the largest single item in the image.
    """
    path = path.strip()
    m = re.match(r"^(.*?\.a)\(([^)]+\.(?:o|obj))\)$", path)
    if m:                                                    # archive.a(member.o) and nothing else:
        archive = m.group(1).split("/")[-1]                  # a PROVIDE expression also has ()
        return archive, archive + "(" + m.group(2) + ")"
    leaf = path.split("/")[-1]
    if not leaf.endswith((".o", ".obj")):
        return None, None
    parts = path.split("/")
    if "build" in parts:
        parts = parts[parts.index("build") + 2:]             # drop .pio/build/<env>/
    if len(parts) > 1 and re.match(r"^lib[0-9a-f]{3}$", parts[0]):
        return parts[1], leaf                                # lib89f/lvgl/... -> lvgl
    if len(parts) > 1 and parts[0] == "src":
        return "the panel's own code (src/)", leaf
    if len(parts) > 1:
        return parts[0], leaf
    return "embedded assets (fonts, brand)", leaf


OUT_RE = re.compile(r"^(\.[\w.$-]+)(?:\s|$)")                        # column 0: output section
FULL_RE = re.compile(r"^ (\.[\w.$*-]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
NAME_RE = re.compile(r"^ (\.[\w.$*-]+)\s*$")                         # input section, size next line
ADDR_RE = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
HEAD_RE = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s*$")       # output section's own size
FILL_RE = re.compile(r"^\s+\*fill\*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)")
RELAX_RE = re.compile(r"^\s+0x([0-9a-f]+)\s+\(size before relaxing\)")


def parse(mapfile, wanted):
    """Attribute the bytes of `wanted` sections. Returns (by_bucket, by_object, by_section, fill)."""
    by_bucket, by_object, by_section, fill = (collections.Counter() for _ in range(4))
    mark = {}                                                # highest address counted, per section
    lines = open(mapfile, errors="replace").read().split("\n")
    try:
        i = next(n for n, l in enumerate(lines) if l.startswith("Linker script and memory map")) + 1
    except StopIteration:
        sys.exit("no linker map in %s -- was the image linked with -Map?" % mapfile)

    cur, pending, want_head = None, None, False
    while i < len(lines):
        line, i = lines[i], i + 1
        m = OUT_RE.match(line)
        if m:
            cur, pending = m.group(1), None
            want_head = line.strip() == m.group(1)           # trap 2: size is on the next line
            continue
        if cur not in wanted:                                # trap 4
            continue
        if want_head:
            want_head = False
            if HEAD_RE.match(line):
                continue
        m = FILL_RE.match(line)
        if m and int(m.group(1), 16):
            # Alignment fill is an entry like any other and obeys the same rule: on RISC-V,
            # relaxation leaves *fill* entries that overlap bytes already accounted for, and
            # summing them blindly put .flash.rodata 1,829 bytes over the ELF on the P4.
            addr, size = int(m.group(1), 16), int(m.group(2), 16)
            fill[cur] += max(0, addr + size - max(addr, mark.get(cur, 0)))
            mark[cur] = max(mark.get(cur, 0), addr + size)
            continue
        m = FULL_RE.match(line)
        if m:
            addr, size, (bucket, obj) = int(m.group(2), 16), int(m.group(3), 16), owner(m.group(4))
            pending = None
        else:
            m = NAME_RE.match(line)
            if m:
                pending = m.group(1)
                continue
            m = ADDR_RE.match(line) if pending else None
            if not m:
                continue
            addr, size, (bucket, obj) = int(m.group(1), 16), int(m.group(2), 16), owner(m.group(3))
            pending = None
        if not (bucket and size and addr):                   # trap 1: address 0 is never flashed
            continue
        end = addr + size
        eff = max(0, end - max(addr, mark.get(cur, 0)))       # trap 3: only what is not counted yet
        mark[cur] = max(mark.get(cur, 0), end)
        if not eff:
            continue
        own = eff
        if i < len(lines):
            m = RELAX_RE.match(lines[i])
            if m:                                            # trap 3: the pool declares its share
                own, i = min(eff, int(m.group(1), 16)), i + 1
        if eff - own:
            by_bucket[POOL] += eff - own
            by_object[POOL] += eff - own
            by_section[cur] += eff - own
        by_bucket[bucket] += own
        by_object[obj] += own
        by_section[cur] += own
    return by_bucket, by_object, by_section, fill


def foreign_map(mapfile, env):
    """None when the map links this environment's objects; else the first object it does link."""
    first = None
    for line in open(mapfile, errors="replace"):
        if line.startswith("LOAD "):
            path = line[5:].strip()
            if (".pio/build/%s/" % env) in path:
                return None
            first = first or path
    return first or "(no object at all)"


def check(by_section, fill, expect, out=sys.stdout):
    """Print the per-section comparison. Returns the sections that are out of tolerance."""
    bad = []
    out.write("parse check -- map against the ELF's own section sizes:\n")
    for name in sorted(expect):
        got = by_section.get(name, 0) + fill.get(name, 0)
        delta = got - expect[name]
        # A residual of a few hundred bytes is one more merged pool the map does not flag.
        # Anything larger means the parse is wrong, and then the table below is fiction.
        tol = max(256, expect[name] // 1000)
        ok = abs(delta) <= tol
        if not ok:
            bad.append(name)
        out.write("  %-22s map %9d   elf %9d   %+7d   %s\n"
                  % (name, got, expect[name], delta,
                     "ok" if ok else "OFF, over the %d-byte tolerance" % tol))
    return bad


def main(argv):
    env = "crowpanel7"
    n_obj, n_lib = 30, 22
    rest = []
    i = 0
    while i < len(argv):
        if argv[i] == "--objects":
            n_obj, i = int(argv[i + 1]), i + 2
        elif argv[i] == "--libraries":
            n_lib, i = int(argv[i + 1]), i + 2
        else:
            rest.append(argv[i]); i += 1
    if rest:
        env = rest[0]

    build = os.path.join(HERE, ".pio", "build", env)
    elf, mapfile = os.path.join(build, "firmware.elf"), os.path.join(build, "firmware.map")
    for p in (elf, mapfile):
        if not os.path.exists(p):
            sys.exit("%s is missing: build it first with `pio run -e %s`.\n"
                     "Careful -- building another environment can wipe .pio/build entirely."
                     % (p, env))

    stranger = foreign_map(mapfile, env)                     # trap 5
    if stranger:
        sys.exit("%s is not the map of this build: it links %s, not .pio/build/%s/.\n"
                 "Something else was linked into that directory since (an IDE re-configure runs "
                 "CMake's compiler check, which writes this file). Run `pio run -e %s` again."
                 % (mapfile, stranger, env, env))
    expect = flashed_sections(elf)
    by_bucket, by_object, by_section, fill = parse(mapfile, set(expect))
    bad = check(by_section, fill, expect)
    if bad:
        sys.exit("\n%d section(s) do not add up (%s): refusing to print a table that would be "
                 "fiction." % (len(bad), ", ".join(bad)))

    total = sum(by_bucket.values())
    binpath = os.path.join(build, "firmware.bin")
    print("\n%s: attributed %d bytes + %d of alignment fill = %d"
          % (env, total, sum(fill.values()), total + sum(fill.values())))
    if os.path.exists(binpath):
        size = os.path.getsize(binpath)
        print("published firmware.bin is %d bytes (%+d: the image header, segment headers and "
              "padding)" % (size, size - sum(expect.values())))

    print("\nwhere the image goes, by library:")
    for bucket, n in by_bucket.most_common(n_lib):
        print("  %7d  %5.1f%%  %s" % (n, 100.0 * n / total, bucket))
    print("\nthe %d largest single objects:" % n_obj)
    for obj, n in by_object.most_common(n_obj):
        print("  %7d  %s" % (n, obj))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
