#!/usr/bin/env python3
"""
ESP32 time_sync_bridge -> Python
Reproduce EXACTLY the same wire protocol as the ESP32 (time_sync_uart
compatible), sending MSG_FULL_SET to the GD32 serial port.

Frame:
    [0xA5][len][type][payload(len)][CRC8][0x5A]
    CRC8-CCITT (poly 0x07, init 0x00) over [type] + payload

FULL_SET payload (7 bytes): year LSB, year MSB, month, day, hour, min, sec (UTC)

CHANGES from the previous version:
  - Time is now fetched from a real NTP server (raw UDP socket,
    no extra dependencies), not from the PC clock - the PC clock may
    have its own accumulated drift that you don't notice.
  - Removed the conversion to Canary Islands local time
    (zoneinfo.ZoneInfo("Atlantic/Canary")) that was in the payload: the
    protocol expects pure UTC (see time_sync_uart.c on the GD32 side -
    "the GD32 side has no notion of timezone/DST"). Canary Islands is on
    WEST (UTC+1) in summer, so that conversion sent the time with a real
    1-hour offset from UTC for much of the year, labeled as if it were UTC.

Requirements:  pip install pyserial
Usage:
    python time_sync_uart.py -p COM3            # Windows
    python time_sync_uart.py -p /dev/ttyUSB0    # Linux
    python time_sync_uart.py -p COM3 --once     # single shot, no resync
"""

import argparse
import datetime
import socket
import struct
import time

import serial  # pyserial

# ---- Wire protocol (identical to ESP32) ----
FRAME_START      = 0xA5
FRAME_END        = 0x5A
MSG_FULL_SET     = 0x01
MSG_FULL_SET_LEN = 7

# ---- NTP ----
NTP_SERVER = "pool.ntp.org"
NTP_PORT = 123
NTP_TIMEOUT_S = 5.0
# NTP epoch (1900-01-01) -> Unix epoch (1970-01-01), in seconds.
NTP_DELTA = 2208988800


def crc8_ccitt(data: bytes) -> int:
    """CRC8, polynomial 0x07, init 0x00 - same as the ESP32 C code."""
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def get_ntp_time(server: str = NTP_SERVER, timeout: float = NTP_TIMEOUT_S) -> datetime.datetime:
    """
    Minimal NTP client via raw UDP socket (NTPv3/v4 protocol, RFC 5905) -
    no external libraries (ntplib, etc.), only standard library socket+struct.
    Returns a UTC datetime with sub-second precision (aware, tzinfo=UTC).

    NTP packet: 48 bytes. For a client request it is enough to set
    the first byte to 0x1B (LI=0, VN=3, Mode=3 "client") and the rest to
    zero - the server fills its response with the timestamp in the last
    8 bytes ("transmit timestamp").
    """
    packet = b"\x1b" + 47 * b"\0"
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.sendto(packet, (server, NTP_PORT))
        response, _ = sock.recvfrom(48)

    # Transmit timestamp: bytes 40-47, two 32-bit integers (seconds,
    # fraction) in big-endian - offset 40 according to the NTP packet format.
    seconds, fraction = struct.unpack("!II", response[40:48])
    unix_seconds = seconds - NTP_DELTA + fraction / 2**32
    return datetime.datetime.fromtimestamp(unix_seconds, tz=datetime.timezone.utc)


def build_full_set_payload(now_utc: datetime.datetime) -> bytes:
    """FULL_SET payload: little-endian year + date/time, in pure UTC -
    without any timezone conversion (see the header comment in this file
    for why it was removed)."""
    year = now_utc.year
    return struct.pack("<HBBBBB", year, now_utc.month, now_utc.day,
                       now_utc.hour, now_utc.minute, now_utc.second)


def send_frame(ser: serial.Serial, msg_type: int, payload: bytes) -> bytes:
    """Exact replica of send_frame() from the ESP32. Returns the sent frame."""
    frame = bytes([FRAME_START, len(payload), msg_type]) \
        + payload + bytes([crc8_ccitt(bytes([msg_type]) + payload), FRAME_END])
    ser.write(frame)
    ser.flush()
    return frame


def send_full_set(ser: serial.Serial, now_utc: datetime.datetime) -> None:
    payload = build_full_set_payload(now_utc)
    frame = send_frame(ser, MSG_FULL_SET, payload)
    year, mon, day, hh, mm, ss = struct.unpack("<HBBBBB", payload)
    print(f"Sending FULL_SET: {year:04d}-{mon:02d}-{day:02d} "
          f"{hh:02d}:{mm:02d}:{ss:02d} UTC | payload:",
          " ".join(f"{b:02X}" for b in payload),
          "| frame:", " ".join(f"{b:02X}" for b in frame))


def sync_once(ser: serial.Serial, ntp_server: str) -> None:
    """Query NTP and send FULL_SET - separated into its own function
    so that a transient NTP server failure (timeout, network down) does
    not crash the entire resync loop, it just skips that attempt."""
    try:
        now_utc = get_ntp_time(ntp_server)
    except OSError as exc:
        print(f"NTP query failed ({exc}) - skipping this sync, will retry next interval")
        return
    send_full_set(ser, now_utc)


def main() -> None:
    ap = argparse.ArgumentParser(description="GD32 DeepSDR 101 time sync (Python)")
    ap.add_argument("-p", "--port", required=True, help="Serial port (e.g. COM3, /dev/ttyUSB0)")
    ap.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate (default 115200)")
    ap.add_argument("--ntp-server", default=NTP_SERVER, help=f"NTP server (default {NTP_SERVER})")
    ap.add_argument("--interval", type=float, default=2 * 60 * 60,
                    help="Seconds between resyncs (default 2 h)")
    ap.add_argument("--once", action="store_true", help="Send once and exit")
    args = ap.parse_args()

    # Same as on the ESP32: initial sync and then resync every interval
    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        sync_once(ser, args.ntp_server)
        if args.once:
            return
        last = time.monotonic()
        while True:
            time.sleep(1)
            if time.monotonic() - last >= args.interval:
                sync_once(ser, args.ntp_server)
                last = time.monotonic()


if __name__ == "__main__":
    main()