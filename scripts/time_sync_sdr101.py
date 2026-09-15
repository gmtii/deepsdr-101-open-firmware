#!/usr/bin/env python3
"""
ESP32 time_sync_bridge -> Python
Reproduce EXACTAMENTE el mismo wire protocol que el ESP32 (time_sync
compatible - ver time_sync.c/.h en el GD32, sobre el puerto USB CDC-ACM
propio de la placa), enviando MSG_FULL_SET y, opcionalmente,
MSG_SET_GRID.

Trama:
    [0xA5][len][type][payload(len)][CRC8][0x5A]
    CRC8-CCITT (poly 0x07, init 0x00) sobre [type] + payload

Payload FULL_SET (7 bytes): year LSB, year MSB, mes, dia, hora, min, seg (UTC)
Payload SET_GRID (4 o 6 bytes): la cuadrícula Maidenhead en ASCII, tal cual
    (sin NUL final - la longitud la lleva el propio byte [len] de la trama)

CAMBIOS respecto a la versión anterior:
  - La hora ahora se obtiene de un servidor NTP real (socket UDP directo,
    sin dependencias extra), no del reloj del PC - el reloj del PC puede
    llevar su propio desfase acumulado sin que lo notes.
  - Se ha quitado la conversión a hora local de Canarias
    (zoneinfo.ZoneInfo("Atlantic/Canary")) que había en el payload: el
    protocolo espera UTC puro (ver time_sync.c en el GD32 - "the
    GD32 side has no notion of timezone/DST"). Canarias está en WEST
    (UTC+1) en verano, así que esa conversión mandaba la hora con 1h de
    desfase real respecto a UTC durante buena parte del año, etiquetada
    como si fuese UTC.
  - Añadido MSG_SET_GRID (09/2026): permite fijar/actualizar la
    cuadrícula Maidenhead propia que usa FT8 para calcular la distancia
    a la estación (ft8_decoder_set_own_grid() en el firmware), sin
    recompilar. El firmware valida el valor recibido (grid_to_latlon())
    y lo guarda en CONFIG.CSV; un valor mal formado se rechaza en
    firmware y no rompe nada, simplemente no se aplica.

Requisitos:  pip install pyserial
Uso:
    python time_sync_sdr101.py -p COM3                    # Windows, solo hora
    python time_sync_sdr101.py -p /dev/ttyUSB0             # Linux, solo hora
    python time_sync_sdr101.py -p COM3 --once               # un solo envío de hora, sin resync
    python time_sync_sdr101.py -p COM3 --grid IL18vl         # fija la cuadrícula y además sincroniza/resincroniza la hora
    python time_sync_sdr101.py -p COM3 --grid IL18vl --once  # fija la cuadrícula y sincroniza la hora una sola vez
"""

import argparse
import datetime
import re
import socket
import struct
import time

import serial  # pyserial

# ---- Wire protocol (idéntico al GD32/time_sync.c) ----
FRAME_START      = 0xA5
FRAME_END        = 0x5A
MSG_FULL_SET     = 0x01
MSG_SET_GRID     = 0x03
MSG_FULL_SET_LEN = 7

# Un locator Maidenhead válido: 2 letras A-R, 2 dígitos, y opcionalmente
# 2 letras más A-X (subcuadrícula) - ver grid_to_latlon() en ft8_decoder.c,
# que es el validador de referencia; esto es solo un filtro de cliente
# para no mandar basura por el puerto serie, el firmware es quien decide
# de verdad si lo acepta.
GRID_RE = re.compile(r"^[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2})?$")

# ---- NTP ----
NTP_SERVER = "pool.ntp.org"
NTP_PORT = 123
NTP_TIMEOUT_S = 5.0
# NTP epoch (1900-01-01) -> Unix epoch (1970-01-01), en segundos.
NTP_DELTA = 2208988800


def crc8_ccitt(data: bytes) -> int:
    """CRC8, polinomio 0x07, init 0x00 - igual que el C del GD32."""
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
    """Réplica exacta de send_frame() del lado ESP32/GD32. Devuelve la
    trama enviada."""
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


def send_set_grid(ser: serial.Serial, grid: str) -> bool:
    """Manda MSG_SET_GRID con la cuadrícula tal cual (ASCII, sin NUL) -
    ver grid_to_latlon() en ft8_decoder.c para la validación real que
    hace el firmware al recibirla; GRID_RE aquí es solo un filtro de
    cliente para no mandar algo obviamente mal formado. Devuelve False
    sin enviar nada si el formato no pasa ese filtro."""
    if not GRID_RE.match(grid):
        print(f"Grid '{grid}' doesn't look like a valid Maidenhead locator "
              "(expected e.g. IL18 or IL18vl) - not sending")
        return False

    payload = grid.encode("ascii")
    frame = send_frame(ser, MSG_SET_GRID, payload)
    print(f"Sending SET_GRID: {grid} | payload:",
          " ".join(f"{b:02X}" for b in payload),
          "| frame:", " ".join(f"{b:02X}" for b in frame))
    return True


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
    ap = argparse.ArgumentParser(description="GD32 DeepSDR 101 time + FT8 grid sync (Python)")
    ap.add_argument("-p", "--port", required=True, help="Puerto serie (ej. COM3, /dev/ttyUSB0)")
    ap.add_argument("-b", "--baud", type=int, default=115200, help="Baudios (default 115200)")
    ap.add_argument("--ntp-server", default=NTP_SERVER, help=f"Servidor NTP (default {NTP_SERVER})")
    ap.add_argument("--interval", type=float, default=2 * 60 * 60,
                    help="Segundos entre resyncs de hora (default 2 h)")
    ap.add_argument("--once", action="store_true", help="Sincronizar la hora una sola vez y salir")
    ap.add_argument("--grid", metavar="LOCATOR",
                    help="Fija la cuadrícula Maidenhead propia para FT8 (ej. IL18vl) "
                         "antes de sincronizar la hora - se manda una sola vez por ejecución")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        if args.grid:
            send_set_grid(ser, args.grid)

        # Igual que antes: sync inicial de hora y luego resync cada intervalo
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
