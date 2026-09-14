#!/usr/bin/env python3
"""
ESP32 time_sync_bridge -> Python
Reproduce EXACTAMENTE el mismo wire protocol que el ESP32 (time_sync_uart
compatible), enviando MSG_FULL_SET al puerto serie del GD32.

Trama:
    [0xA5][len][type][payload(len)][CRC8][0x5A]
    CRC8-CCITT (poly 0x07, init 0x00) sobre [type] + payload

Payload FULL_SET (7 bytes): year LSB, year MSB, mes, dia, hora, min, seg (UTC)

CAMBIOS respecto a la versión anterior:
  - La hora ahora se obtiene de un servidor NTP real (socket UDP directo,
    sin dependencias extra), no del reloj del PC - el reloj del PC puede
    llevar su propio desfase acumulado sin que lo notes.
  - Se ha quitado la conversión a hora local de Canarias
    (zoneinfo.ZoneInfo("Atlantic/Canary")) que había en el payload: el
    protocolo espera UTC puro (ver time_sync_uart.c en el GD32 - "the
    GD32 side has no notion of timezone/DST"). Canarias está en WEST
    (UTC+1) en verano, así que esa conversión mandaba la hora con 1h de
    desfase real respecto a UTC durante buena parte del año, etiquetada
    como si fuese UTC.

Requisitos:  pip install pyserial
Uso:
    python time_sync_uart.py -p COM3            # Windows
    python time_sync_uart.py -p /dev/ttyUSB0    # Linux
    python time_sync_uart.py -p COM3 --once     # solo un envío, no resync
"""

import argparse
import datetime
import socket
import struct
import time

import serial  # pyserial

# ---- Wire protocol (idéntico al ESP32) ----
FRAME_START      = 0xA5
FRAME_END        = 0x5A
MSG_FULL_SET     = 0x01
MSG_FULL_SET_LEN = 7

# ---- NTP ----
NTP_SERVER = "pool.ntp.org"
NTP_PORT = 123
NTP_TIMEOUT_S = 5.0
# NTP epoch (1900-01-01) -> Unix epoch (1970-01-01), en segundos.
NTP_DELTA = 2208988800


def crc8_ccitt(data: bytes) -> int:
    """CRC8, polinomio 0x07, init 0x00 - igual que el C del ESP32."""
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def get_ntp_time(server: str = NTP_SERVER, timeout: float = NTP_TIMEOUT_S) -> datetime.datetime:
    """
    Cliente NTP mínimo por socket UDP crudo (protocolo NTPv3/v4, RFC 5905) -
    sin librerías externas (ntplib, etc.), solo socket+struct de la
    librería estándar. Devuelve un datetime UTC con precisión de
    sub-segundo (aware, tzinfo=UTC).

    Paquete NTP: 48 bytes. Para una petición de cliente basta con poner
    el primer byte a 0x1B (LI=0, VN=3, Mode=3 "client") y el resto a
    cero - el servidor rellena su respuesta con el timestamp en los
    últimos 8 bytes ("transmit timestamp").
    """
    packet = b"\x1b" + 47 * b"\0"
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.sendto(packet, (server, NTP_PORT))
        response, _ = sock.recvfrom(48)

    # Transmit timestamp: bytes 40-47, dos enteros de 32 bits (segundos,
    # fracción) en big-endian - offset 40 según el formato del paquete NTP.
    seconds, fraction = struct.unpack("!II", response[40:48])
    unix_seconds = seconds - NTP_DELTA + fraction / 2**32
    return datetime.datetime.fromtimestamp(unix_seconds, tz=datetime.timezone.utc)


def build_full_set_payload(now_utc: datetime.datetime) -> bytes:
    """Payload FULL_SET: año little-endian + fecha/hora, en UTC puro -
    sin ninguna conversión de zona horaria (ver el comentario de
    cabecera de este fichero sobre por qué se quitó)."""
    year = now_utc.year
    return struct.pack("<HBBBBB", year, now_utc.month, now_utc.day,
                       now_utc.hour, now_utc.minute, now_utc.second)


def send_frame(ser: serial.Serial, msg_type: int, payload: bytes) -> bytes:
    """Réplica exacta de send_frame() del ESP32. Devuelve la trama enviada."""
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
    """Consulta NTP y manda el FULL_SET - separado en su propia función
    para que un fallo puntual del servidor NTP (timeout, red caída) no
    tumbe el bucle de resync entero, solo se salta ese intento."""
    try:
        now_utc = get_ntp_time(ntp_server)
    except OSError as exc:
        print(f"NTP query failed ({exc}) - skipping this sync, will retry next interval")
        return
    send_full_set(ser, now_utc)


def main() -> None:
    ap = argparse.ArgumentParser(description="GD32 DeepSDR 101 time sync (Python)")
    ap.add_argument("-p", "--port", required=True, help="Puerto serie (ej. COM3, /dev/ttyUSB0)")
    ap.add_argument("-b", "--baud", type=int, default=115200, help="Baudios (default 115200)")
    ap.add_argument("--ntp-server", default=NTP_SERVER, help=f"Servidor NTP (default {NTP_SERVER})")
    ap.add_argument("--interval", type=float, default=2 * 60 * 60,
                    help="Segundos entre resyncs (default 2 h)")
    ap.add_argument("--once", action="store_true", help="Enviar una sola vez y salir")
    args = ap.parse_args()

    # Igual que en el ESP32: inicial sync y luego resync cada intervalo
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
