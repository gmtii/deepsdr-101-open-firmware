# FT8 mode & time sync — dev branch notes

This file only covers the FT8-specific pieces added on this branch and the
time-sync tooling that keeps the RTC accurate. See the main README for
everything else (build, general hardware, other modes).

Thanks to Karlis Goba, YL3JG for his great https://github.com/kgoba/ft8_lib

## FT8 mode

### Current limitation: 1600 kHz window, not the full passband

FT8 detection currently searches a **1600 Hz-wide window** around the tuned
frequency, not the receiver's full instantaneous bandwidth. This was a
deliberate trade-off (`ft8_waterfall_adapter.h`, `time_osr=1`) to fit the
1024-point FFT, the cascade history buffer, and the resampler in available
RAM. Signals outside that window won't be seen even if they're within the
receiver's normal passband — tune to the segment you actually want decoded.

### Scheduling

Slot timing is driven **directly by the RTC**, polled every main-loop
iteration: a new capture arms on the exact second the RTC rolls over to a
multiple of 15 (`:00/:15/:30/:45`). This does *not* use `g_msticks`/SysTick
for the scheduling decision — an earlier design that seeded a fixed grid from
the RTC once and then tracked it via SysTick was found to drift, because the
HXTAL-derived SysTick clock itself doesn't track real time as accurately as
the RTC does once the RTC is kept NTP-disciplined (see the time-sync section
below). `g_msticks` is still used for relative diagnostics (`proc_ms`,
`capture_ms`) but never for deciding *when* a slot boundary is.

Practical implication: **FT8 alignment is only as good as the RTC**. If the
RTC has never been synced (still on the firmware's default epoch), decoding
will not align with real transmissions at all, even though the internal
15-second cadence looks perfectly regular.

### Cascade / expanded text view toggle

Tap the cascade area (the spectrogram-style history above the decode text,
while FT8 mode is showing) to toggle it:

- **Cascade visible** (default): small 8-line decode panel at the bottom,
  same footprint as before.
- **Cascade hidden**: the cascade's own screen area is reclaimed for the
  decode panel too, giving ~17 lines instead of 8, single column, full
  screen width. (An earlier 2-column layout was tried and reverted — column
  width couldn't fit a full decoded line, and the row math didn't use the
  full reclaimed height either. Single column, full width, is correct.)

The toggle is session-only (not persisted to `CONFIG.CSV`) — always starts
with the cascade visible on boot.

### Debug diagnostics

The on-screen FT8 diagnostic block (`run=/blk=/proc=/capms=/ins=/isr=/cyc=/rsec=`
and the `"(no decodes this slot)" + dB min/max` fallback message) are gated
behind the existing `DEBUG_UART_ENABLED` build flag (off by default). Build
with `make DEBUG_UART_ENABLED=1` to bring them back for a debugging session —
no source changes needed either way.

## RTC time sync

The GD32's RTC (LXTAL) has no notion of timezone or DST — it expects **UTC**,
set via a small framed binary protocol.

### The USB connection — how the serial port shows up

Plug the board's own USB connector (PA11/PA12, native USB device
controller, not a bolt-on USB-TTL adapter) into the PC. It enumerates as a
standard **USB CDC-ACM virtual COM port** — Windows 10+/Linux/macOS all have
a generic driver for this class built in, so no vendor driver install
should be needed; check Device Manager (Windows) or `dmesg`/`/dev/ttyACM*`
(Linux) for the assigned port number. This is the port both the ESP32 link
and `time_sync_sdr101.py`/its `.exe` talk to — point `-p` at it.

The **same** USB CDC port also carries `debug_print()` output whenever the
firmware is built with `DEBUG_UART_ENABLED=1` (`debug_uart.c` mirrors to it
for exactly this reason — useful when no separate USB-TTL adapter is handy).
The two no longer fight over pins the way an older revision of this link
did (see below) — but if you open this port in a plain terminal program
with `DEBUG_UART_ENABLED=1`, you'll see readable debug text and the raw
binary time-sync frames interleaved on the same stream. `time_sync.py`/the
`.exe` only ever writes frames and never reads, so this doesn't break
syncing, it just makes the port noisy to eyeball directly.

### (Legacy, no longer used) USART0/PA9-PA10 link

An earlier revision (`time_sync_uart.c/.h`) ran this same protocol over
USART0 on PA9(TX)/PA10(RX) — an external USB-TTL adapter wired to those
pins, mutually exclusive with `debug_uart.c`'s own use of PA9 for
`debug_print()` (selected at build time by `DEBUG_UART_ENABLED`). That
module is still in the tree but **is no longer called from `main()`** —
`time_sync.c` (USB CDC-ACM, above) replaced it outright, specifically to
drop that mutual-exclusion problem and `debug_uart.c`'s own suspected
PA9-toggling RF noise into the HF front-end. Left for reference only; new
work should target `time_sync.c`.

### Normal source: ESP32 over USB

In normal operation, a companion ESP32 fetches time via NTP over WiFi and
sends `MSG_FULL_SET` once after its own boot, over the USB connection above.
This is what `time_sync.c` is written against.

### Wire protocol

```
[0xA5]  start marker
[len]   payload length in bytes
[type]  0x01 = MSG_FULL_SET, 0x02 = MSG_SHIFT
[...]   payload (see below)
[crc8]  CRC8-CCITT (poly 0x07, init 0x00), over type+payload only
[0x5A]  end marker
```

- **MSG_FULL_SET** (0x01), 7-byte payload: `year_lo, year_hi, month, day,
  hour, minute, second`. Hard-sets the calendar (`rtc_hw_set()`) — used for
  the first sync after boot (calendar may be anything, including the
  firmware's default epoch), and for any subsequent full resync.
- **MSG_SHIFT** (0x02), 3-byte payload: `add_one_second (0/1),
  subsecond_fraction_lo, subsecond_fraction_hi`. Nudges the running
  calendar by at most ±1 second (`rtc_hw_apply_shift()`) without disturbing
  it — intended for routine fine correction once the calendar is already
  close. **The Python tool below only ever sends MSG_FULL_SET** — it has no
  fine-correction mode.

### PC-side alternative: `time_sync_sdr101.py`

For bench testing without an ESP32 attached, `time_sync_sdr101.py` sends the
identical wire protocol directly from a PC to the board's own USB CDC-ACM
port (see "The USB connection" above) — no ESP32, and no separate USB-TTL
adapter, needed at all.

**Requirements:** `pip install pyserial` (Python 3, stdlib `socket`/`struct`
for the raw NTP query — no other dependencies).

**Usage:**

```
python time_sync_sdr101.py -p COM3              # Windows
python time_sync_sdr101.py -p /dev/ttyUSB0      # Linux
python time_sync_sdr101.py -p COM3 --once       # send once and exit, no resync loop
```

Options:

| Flag | Default | Meaning |
|---|---|---|
| `-p`, `--port` | *(required)* | Serial port |
| `-b`, `--baud` | `115200` | Baud rate to open the port with. On a USB CDC-ACM virtual port this is mostly cosmetic (the OS/device largely ignore it, unlike a real UART), so the default is fine to leave as-is |
| `--ntp-server` | `pool.ntp.org` | NTP server queried for UTC |
| `--interval` | `7200` (2h) | Seconds between resyncs |
| `--once` | off | Send one `MSG_FULL_SET` and exit |

Time comes from a real NTP query (raw UDP, RFC 5905), **not the PC's own
clock** — the PC's clock can carry its own unnoticed drift. The payload is
sent in pure UTC; an earlier version of this script converted to Canary
Islands local time first, which silently mislabeled WEST (UTC+1, summer) as
UTC — that conversion has been removed.

**Windows executable:** a standalone `.exe` was built from this script with
PyInstaller (`pyinstaller --onefile time_sync_sdr101.py`) so it can run on a
Windows console without a Python install. Same arguments as above, e.g.:

```
time_sync_sdr101.exe -p COM3
```

**Caveat:** since every sync from this tool is a hard `MSG_FULL_SET` (never
the gentler `MSG_SHIFT`), a resync landing mid-capture can disturb at most
that one in-progress FT8 slot (the RTC-driven scheduler above just picks up
the corrected time on its very next poll) — harmless at the default 2-hour
interval, just worth knowing if `--interval` is set much shorter.