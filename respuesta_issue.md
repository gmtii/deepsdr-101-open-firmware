Thanks — you're right on both counts, and the second one turned out to be a
real bug rather than a gap in the data. Both are fixed now.

## 1. The missing toolchain

`emisoras_pack.py` and `datos_pack.py` lived only in the simulator tree and
never got copied to the firmware repo, which is the one that's published. So
`tools/icao24.py` documented a three-command workflow and the repo shipped
one of the three. Sorry about that.

Now in `tools/`:

| file | what it is |
|---|---|
| `emisoras_pack.py` | EiBi + Aoki → `EMISORAS.BIN` |
| `datos_pack.py` | `ICAO24.BIN` + `EMISORAS.BIN` → `BD.BIN` |
| `icao24_recorta.py` | trims a too-large aircraft database by aircraft type |
| `emisoras_check.py` | the test bench for all of the above |
| `paises_xy.csv`, `sitios_xy.csv` | the new location tables (see below) |
| `datos/LEEME.md` | where the lists come from, seasons, the full workflow |
| `datos/eibi.txt` | the EiBi A26 list that produced the current `BD.BIN` |

`eibi.txt` is committed because its own header says *"Free to copy +
distribute"*, so you can rebuild the exact same `BD.BIN` byte for byte.
`aoki.txt` carries no copying notice, so that one you download — the URL is
in `datos/LEEME.md`.

A script keeps the simulator's copies in sync with these now, so they can't
drift apart again.

## 2. Why you get CNR 1 instead of Kossuth

It isn't that Kossuth is missing. It's **in** the database — it was just
eighth. There are ten entries on 540 kHz and the radio shows one:

```
540  CHN  CNR 1 Voice of China     Jagdaqi-Hei     0000-2400   <- the "C"
540  J    NHK AM Ishigaku          Okinawa-Ish     0000-2400
540  J    NHK AM Kitakyushu        Fukuoka-Kit     0000-2400
540  J    NHK AM Matsumoto         Matsumoto       0000-2400
540  J    NHK AM Miyazaki          Miyazaki        0000-2400
540  J    NHK AM Nanao             Ishikawa-Na     0000-2400
540  J    NHK AM Yamagata          Yamagata        0000-2400
540  HNG  P1-Kossuth Radio         Solt            0330-2130   <- eighth
540  KWT  R.KUWAIT                 Sulaibiyah      0000-2400
540  THA  Yaan Kraw haa-sii-suun   Bangkok         0000-2400
```

The packer sorted entries within a frequency by **station name**:

```python
filas = sorted(ent.values(), key=lambda v: (v['hz'], v['ini'], v['est']))
```

so "CNR 1" won on the C, for everybody, everywhere. Solt is ~70 km from you;
Jagdaqi is ~6,990 km. We never caught it from Spain because what we mostly
test is shortwave, where the alphabetically-first entry is no worse than any
other — nothing is nearby.

## 3. The fix: `--qth`

Your instinct about using the listener's location was right, and it's much
cheaper than you'd expect, because **no firmware change is needed**.
`emisoras_busca()` already returns currently-on-air entries first and
otherwise preserves file order. So it's enough to write the file in a
different order:

```sh
python3 emisoras_pack.py eibi.txt aoki.txt EMISORAS.BIN 2 --qth JN97
```

`--qth` takes a Maidenhead locator (2, 4, 6 or 8 characters) or `lat,lon`,
and orders each frequency's entries by distance to the transmitter. Also:

```sh
--radio 3000        drop anything beyond 3000 km — a small regional database
--pais HNG,D,I      keep only these ITU country codes
```

Without `--qth` the output is byte-for-byte identical to before, so existing
builds don't change.

Of 1,898 frequencies, **207 get a different first entry** between Budapest
and Madrid. A few:

| kHz | from Budapest | from Madrid |
|---|---|---|
| 531 | Kringvarp Føroya (Akraberg) | R.Algérie Int. (F'Kirina) |
| 540 | **P1-Kossuth Radio (Solt)** | P1-Kossuth Radio (Solt) |
| 603 | Radio România Actualități (Oradea) | Smooth Radio (Littlebourne) |
| 639 | Český rozhlas 2 (Liblice) | RNE (Mesondo) |
| 648 | Radio Murski Val (Cerklje) | Radio Caroline (Orfordness) |

One detail worth knowing: Kossuth is scheduled 0330–2130 UTC, so outside
those hours it is correctly *not* shown as on-air, and you'll get the next
nearest 24-hour entry (R.KUWAIT, ~2,900 km). That's the time filter working,
not the ordering failing.

## 4. Where the locations come from

Two plain CSVs you can edit without touching any code:

- **`tools/paises_xy.csv`** — the 129 ITU country codes that actually appear
  in EiBi and Aoki, with coordinates. The bench verifies this covers the
  lists exactly.
- **`tools/sitios_xy.csv`** — transmitter sites, which **override** the
  country. Because country-level works for Hungary and fails for China:
  Kashi and Dongfang are 3,400 km apart and both are `CHN`. Matching is by
  prefix (Aoki truncates site names to 12 characters) and the longest prefix
  wins, so `Urumqi Hutu` beats `Urumqi`, a different city 90 km away.

`sitios_xy.csv` currently has the ~46 most-used sites out of 824 distinct
names in the lists. **Adding one is a single line**, and that's exactly the
kind of local knowledge that's worth contributing — if you fill in Central
European sites, send them.

## 5. Three honest caveats

- What this sorts by is **distance to the transmitter, not who you hear
  best**. On mediumwave those are nearly the same thing — which is your case.
  On shortwave they are not: a 500 kW relay aimed at you from Issoudun beats
  a 1 kW local, and the ionosphere has a skip zone where near stations
  *can't* be heard. I didn't try to model propagation; distance is what the
  data can actually support.
- Country positions are centroids. Fine for Hungary; for Russia, the USA,
  China or Canada they can be thousands of km off. Hence `sitios_xy.csv`.
- Entries with genuinely unknown locations (clandestine stations, "unknown
  station", UN Radio) are sorted last, never dropped and never invented.

## 6. Two other things that came out of this

- `datos_pack.py` and `icao24.py` computed the `BD.BIN` size limit from the
  2 MB factory chip's start and end addresses. The number was right by
  coincidence. The actual rule is that the data region is always the **last
  1 MB** of the flash, whatever its size, so a bigger chip gives you a bigger
  USB disk and not one extra byte of database. Both tools now state that in
  words and warn at 90%.
- The list expiry date was hardcoded as `20261025`. A26 expires on 25 October
  2026 — three weeks away — so the next list would have been packed with the
  previous season's expiry. It's now read from EiBi's `Valid ... - ...`
  header line.

Note the season matters for the `horas_utc` argument: A26 is UTC+2 for Spain
and B26 is UTC+1, because the seasons line up exactly with European DST.
`datos/LEEME.md` has the table.

## 7. The bench

`tools/emisoras_check.py`, wired into the simulator's `make comprueba`. It
writes its own fake lists in the same column layout, so it needs neither of
the real lists to run, and it checks that 540 kHz gives Kossuth from
Budapest, NHK from Tokyo, R.KUWAIT from Kuwait and CNR 1 from Jagdaqi — plus
that without `--qth` the old order is preserved.

This bench exists because a wrong order **doesn't produce an error, it
produces a different station**, one that looks perfectly plausible. That's
not something you can spot by looking at the radio — which is precisely why
it took someone in Hungary to find it. Thanks for reporting it properly.
