#!/usr/bin/env python3
"""
make_icao24_compact.py - build ICAO24.BIN, the compact aircraft database the
DeepSDR 101 HFDL screens read from the external SPI flash volume.

The firmware (built with HFDL_ICAO24_DB=1) looks up the 24-bit ICAO address that
HFDL reports in logon messages and shows the aircraft model next to it, e.g.
"ICAO: 495268  B788 Boeing 787-8".

WHY A NEW FORMAT
  The icao24.db that the PortaPack tool make_icao24_db.py produces stores 153 bytes
  per aircraft (registration, manufacturer, model, type, owner, operator). The full
  OpenSky database is roughly 490,000 aircraft, about 72 MB in that format. This
  board's flash volume is 1 MiB in total, shared with the bootloader's firmware
  image. So this script keeps only what is useful and fits:
    * only jets (ICAO aircraft classes L2J, L3J, L4J by default: airliners,
      business jets, freighters - what actually flies HFDL),
    * 5 bytes per aircraft (3-byte address + 2-byte index into a table of type
      names), no registration/owner/operator.
  With the current OpenSky data that is on the order of 70,000 aircraft, about
  350 KB. The script prints the exact size and refuses to write more than
  --max-kb (default 600) so the file cannot fill the volume by accident.

INPUT (one of)
  --csv aircraftDatabase.csv   the OpenSky file (make_icao24_db.py leaves a copy in
                               its working directory). Best choice: it has the ICAO
                               type designator (B788, A20N, ...).
  --db  icao24.db              the file make_icao24_db.py generated. Works too, but
                               that format has no type designator for most aircraft,
                               so type names are "<manufacturer> <model>" only, and
                               aircraft whose class was missing in the CSV are dropped.

OUTPUT   ICAO24.BIN - see User/icao24_db.h for the byte layout.

IF THE FIRMWARE REPORTS THE FILE AS UNUSABLE
  The debug UART status says what it found. First 16 bytes "FF FF FF ..." mean the flash
  at the file's location was never written: the directory entry and FAT arrived but the
  data did not. To find out whether size matters, make small valid files and copy each one
  the same way (after removing the previous one):
      python3 make_icao24_compact.py --db icao24.db --limit 300   -o ICAO24.BIN   (about 3 KB)
      python3 make_icao24_compact.py --db icao24.db --limit 8000  -o ICAO24.BIN   (about 40 KB)
      python3 make_icao24_compact.py --db icao24.db --limit 25000 -o ICAO24.BIN   (about 100 KB)
  The largest one that works shows where writes stop reaching the chip. If the limit turns out
  to be small, --widebody (needs --csv) keeps only wide-body types, about 50 KB, chosen by
  relevance instead of evenly spread like --limit.

WHY THE FILE STARTS WITH 4 KB OF PADDING
  The flash is erased in 4 KB blocks and the firmware rewrites CONFIG.CSV (the settings) by
  erasing and reprogramming its WHOLE 4 KB block. CONFIG.CSV sits in the first data cluster, and a
  file copied from the PC lands in the next free cluster, i.e. in the SAME block: every time the
  firmware saved a setting, it wiped the first 3.5 KB of ICAO24.BIN (header and start of the
  aircraft table) and the lookup stopped working. So the file now begins with 4096 bytes of
  padding (0xFF) and the real header starts at offset 4096, which is outside any block the
  file can share with CONFIG.CSV wherever the file happens to be placed. The firmware reads both
  layouts (header at 4096, or at 0 for files made without padding). Use --no-pad for the old layout.

USING IT
  1. python3 scripts/make_icao24_compact.py --db icao24.db -o ICAO24.BIN
  2. Copy ICAO24.BIN into the root directory of the flash volume the bootloader
     exposes over USB (the volume update4.bin goes to). The name must stay
     exactly ICAO24.BIN (8.3, upper case). Copy it onto the volume after
     removing old copies so it lands in one piece: the firmware accepts a file
     split into up to 12 fragments and refuses more.
  3. Build the firmware with HFDL_ICAO24_DB=1 and reboot. With
     DEBUG_UART_ENABLED=1 the first lookup prints why the file is unusable, if it is.
"""

import argparse
import csv
import re
import struct
import sys
import unicodedata
from collections import Counter, defaultdict

DB_CODE_LEN = 7                                   # icao24.db: 6 hex chars + NUL
DB_FIELDS = (("registration", 9), ("manufacturer", 33), ("model", 33),
             ("actype", 5), ("owner", 33), ("operator", 33))
DB_REC_LEN = sum(n for _, n in DB_FIELDS)         # 146
CLASS_RE = re.compile(r"^[LSAHGT][1-9][PTJEC]$")  # ICAO Doc 8643 aircraft class, e.g. L2J
HEX6_RE = re.compile(r"^[0-9A-F]{6}$")

# ICAO type-code prefixes of the wide-body airliners and big freighters, the aircraft most likely to
# fly the oceanic routes where HFDL is used (747, 767, 777, 787, A300/A310, A330/A340/A350/A380, MD-11,
# DC-10, Il-96, An-124). About 8,000-10,000 aircraft in the OpenSky data, roughly 45-55 KB of ICAO24.BIN.
WIDEBODY_PREFIXES = ("B74", "B76", "B77", "B78", "A306", "A30B", "A310", "A33", "A34", "A35", "A38",
                     "MD11", "DC10", "IL96", "A124")

PAD_BYTES = 4096   # one flash erase block, see the docstring

MAGIC = b"I24B"
VERSION = 1

# Manufacturer names as OpenSky writes them -> short form for the type name.
MFR_MAP = (
    (r"BOEING", "Boeing"), (r"AIRBUS", "Airbus"), (r"EMBRAER", "Embraer"),
    (r"BOMBARDIER|CANADAIR", "Bombardier"), (r"GULFSTREAM", "Gulfstream"),
    (r"DASSAULT", "Dassault"), (r"CESSNA|TEXTRON", "Cessna"), (r"LEARJET", "Learjet"),
    (r"HAWKER|BEECH|RAYTHEON", "Beech"), (r"MCDONNELL", "MD"), (r"ILYUSHIN", "Ilyushin"),
    (r"TUPOLEV", "Tupolev"), (r"ANTONOV", "Antonov"), (r"COMAC", "COMAC"),
    (r"SUKHOI", "Sukhoi"), (r"LOCKHEED", "Lockheed"), (r"FOKKER", "Fokker"),
    (r"BRITISH AEROSPACE|BAE", "BAe"), (r"PILATUS", "Pilatus"), (r"HONDA", "Honda"),
)


def ascii_clean(s):
    return unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode("ascii").strip()


def short_mfr(name):
    up = ascii_clean(name).upper()
    for pattern, short in MFR_MAP:
        if re.search(pattern, up):
            return short
    words = ascii_clean(name).split()
    return words[0].title() if words else ""


def type_label(typecode, mfr, model, name_len):
    mfr_s = short_mfr(mfr)
    model = ascii_clean(model)
    # Avoid "Boeing Boeing 787": drop a leading manufacturer word from the model text.
    if mfr_s and model.upper().startswith(mfr_s.upper()):
        model = model[len(mfr_s):].strip(" -")
    label = " ".join(p for p in (typecode, mfr_s, model) if p)
    return label[:name_len].rstrip()


def read_csv(path):
    """Same 27-column OpenSky layout make_icao24_db.py parses. Yields
    (icao24, typecode, manufacturer, model, aircraft_class)."""
    with open(path, "rt", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()[1:]
    for row in csv.reader(lines, quotechar='"', delimiter=",", quoting=csv.QUOTE_ALL, skipinitialspace=True):
        if len(row) != 27:
            continue
        icao = row[0][:6].upper()
        if not HEX6_RE.match(icao):
            continue
        cls = row[8] if len(row[8]) == 3 else ""
        yield icao, row[5][:4], row[3][:32], row[4][:32], cls


def read_db(path):
    """icao24.db: N * 7-byte code table, then N * 146-byte records in the same order.
    'actype' holds the aircraft CLASS when the CSV had one, otherwise the type code."""
    data = open(path, "rb").read()
    n = len(data) // (DB_CODE_LEN + DB_REC_LEN)
    if n * (DB_CODE_LEN + DB_REC_LEN) != len(data):
        sys.exit("%s does not look like an icao24.db (size is not a multiple of %d bytes)"
                 % (path, DB_CODE_LEN + DB_REC_LEN))
    rec0 = n * DB_CODE_LEN
    off = {}
    pos = 0
    for name, size in DB_FIELDS:
        off[name] = (pos, size)
        pos += size
    for i in range(n):
        icao = data[i * DB_CODE_LEN:i * DB_CODE_LEN + 6].decode("ascii", "ignore").upper()
        base = rec0 + i * DB_REC_LEN

        def field(name):
            o, size = off[name]
            return data[base + o:base + o + size].split(b"\0", 1)[0].decode("ascii", "ignore")

        actype = field("actype")
        cls = actype if CLASS_RE.match(actype) else ""
        yield icao, "", field("manufacturer"), field("model"), cls


def main():
    ap = argparse.ArgumentParser(description="Build ICAO24.BIN for the DeepSDR 101 firmware (HFDL_ICAO24_DB=1).")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--csv", help="OpenSky aircraftDatabase.csv")
    src.add_argument("--db", help="icao24.db generated by make_icao24_db.py")
    ap.add_argument("-o", "--output", default="ICAO24.BIN")
    ap.add_argument("--classes", default="L2J,L3J,L4J",
                    help="comma-separated ICAO aircraft classes to keep (default: %(default)s; "
                         "add e.g. L2T,L4T for turboprops)")
    ap.add_argument("--all-classes", action="store_true", help="keep every aircraft that has a class (much bigger)")
    ap.add_argument("--widebody", action="store_true",
                    help="keep only wide-body types (a small file, ~50 KB). Needs --csv: it selects by ICAO type code")
    ap.add_argument("--typecodes", metavar="P1,P2,...",
                    help="keep only aircraft whose ICAO type code starts with one of these prefixes, e.g. B77,B78,A35. "
                         "Needs --csv. Replaces the class filter")
    ap.add_argument("--name-len", type=int, default=24, choices=range(8, 33), metavar="8..32",
                    help="bytes per type name (default 24; the firmware appends it to a 47-column line)")
    ap.add_argument("--max-kb", type=int, default=600, help="refuse to write a file bigger than this (default %(default)s KB)")
    ap.add_argument("--no-pad", action="store_true",
                    help="do NOT put 4 KB of padding in front of the header (old layout; the head of the file "
                         "gets wiped whenever the firmware saves a setting, see the docstring)")
    ap.add_argument("--limit", type=int, default=0, metavar="N",
                    help="TEST FILES ONLY: keep about N aircraft, spread evenly over the address range, and only "
                         "the types they use. Gives a valid ICAO24.BIN of any small size, to check that a copy over "
                         "USB reaches the flash (see 'if the firmware reports the file as unusable' below)")
    args = ap.parse_args()

    keep = set(c.strip().upper() for c in args.classes.split(",") if c.strip())
    prefixes = ()
    if args.widebody:
        prefixes = WIDEBODY_PREFIXES
    if args.typecodes:
        prefixes = tuple(p.strip().upper() for p in args.typecodes.split(",") if p.strip())
    if prefixes and not args.csv:
        sys.exit("--widebody / --typecodes select by ICAO type code, which icao24.db does not keep: use --csv aircraftDatabase.csv")
    rows = list(read_csv(args.csv)) if args.csv else list(read_db(args.db))

    kept = {}                          # icao24 -> (typecode, manufacturer, model)
    seen_classes = Counter()
    for icao, typecode, mfr, model, cls in rows:
        seen_classes[cls or "(none)"] += 1
        if prefixes:                                   # the type code decides, the class is not needed
            if typecode.upper().startswith(prefixes):
                kept[icao] = (typecode, mfr, model)
            continue
        if not cls:
            continue
        if args.all_classes or cls in keep:
            kept[icao] = (typecode, mfr, model)

    if args.limit and len(kept) > args.limit:
        step = -(-len(kept) // args.limit)                     # ceil: about `limit` aircraft, evenly spread
        kept = {k: kept[k] for k in sorted(kept)[::step]}
    print("read %d aircraft; kept %d" % (len(rows), len(kept)))
    print("  by class in the input: " + ", ".join("%s %d" % kv for kv in seen_classes.most_common(8)))
    if not kept:
        sys.exit("nothing kept - check --classes")

    # One type-name entry per ICAO type code (or per manufacturer+model when there is no code),
    # named after the most common manufacturer/model text seen for it.
    groups = defaultdict(Counter)
    keys = {}
    for icao, (typecode, mfr, model) in kept.items():
        k = typecode if typecode else "M:" + ascii_clean(mfr).upper() + "|" + ascii_clean(model).upper()
        keys[icao] = k
        groups[k][(mfr, model)] += 1
    type_index = {}
    names = []
    for k in sorted(groups):
        mfr, model = groups[k].most_common(1)[0][0]
        code = k if not k.startswith("M:") else ""
        type_index[k] = len(names)
        names.append(type_label(code, mfr, model, args.name_len))
    if len(names) > 65535:
        sys.exit("more than 65535 distinct types - narrow --classes")

    records = sorted(kept)
    n = len(records)
    body = bytearray()
    for icao in records:
        body += bytes.fromhex(icao) + struct.pack("<H", type_index[keys[icao]])
    types = b"".join(nm.encode("ascii", "replace").ljust(args.name_len, b"\0") for nm in names)
    types_off = 16 + len(body)
    header = MAGIC + struct.pack("<BBHII", VERSION, args.name_len, len(names), n, types_off)
    blob = header + bytes(body) + types
    pad = b"" if args.no_pad else b"\xff" * PAD_BYTES
    blob = pad + blob

    kb = len(blob) / 1024.0
    print("types: %d   file: %d bytes (%.0f KB) = %d padding + 16 header + %d x 5 aircraft + %d x %d type names"
          % (len(names), len(blob), kb, len(pad), n, len(names), args.name_len))
    if kb > args.max_kb:
        sys.exit("%.0f KB is over --max-kb %d: narrow --classes or raise the limit if the volume has room" % (kb, args.max_kb))
    with open(args.output, "wb") as f:
        f.write(blob)

    # Read it back the way the firmware does: binary search over the 5-byte records.
    chk = open(args.output, "rb").read()
    base = len(pad)
    assert chk[base:base + 4] == MAGIC and len(chk) == len(blob)
    for icao in (records[0], records[n // 2], records[-1]):
        want = bytes.fromhex(icao)
        lo, hi, found = 0, n, None
        while lo < hi:
            mid = (lo + hi) // 2
            rec = chk[base + 16 + mid * 5:base + 16 + mid * 5 + 5]
            if rec[:3] == want:
                idx = struct.unpack("<H", rec[3:])[0]
                found = chk[base + types_off + idx * args.name_len:base + types_off + (idx + 1) * args.name_len].split(b"\0")[0].decode()
                break
            if rec[:3] < want:
                lo = mid + 1
            else:
                hi = mid
        assert found, "self-check failed for " + icao
        print("  self-check %s -> %s" % (icao, found))
    print("wrote %s" % args.output)


if __name__ == "__main__":
    main()
