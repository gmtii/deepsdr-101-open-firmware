#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
UNA IMAGEN FAT12 CON LA GEOMETRIA DE LA RADIO, PARA RESCATAR LA FLASH SPI.

Estaba en restore_spiflash/, una carpeta de la raiz con este unico fichero
dentro y que no nombraba NADIE: ni un makefile, ni el README, ni el manual.
Desde el 16/10/2026 vive aqui, que es donde estan las demas herramientas.

Que hace, en una linea: escribe una imagen cruda con EXACTAMENTE la
geometria que spi_flash.c lleva clavada en sus constantes, en vez de
esperar que mkfs.vfat adivine la misma geometria rara -raiz de 512
entradas en 32 sectores, 4 reservados, FAT de 6- que trae la tarjeta de
fabrica. El texto de abajo es el original, en ingles, y se deja tal cual:
describe el formato, no el proyecto.

    python3 tools/fat12_imagen.py salida.img

Y ANTES DE VOLCARLA A UN DISCO DE VERDAD, mira dos veces cual es: un dd
al dispositivo equivocado se lleva por delante lo que haya ahi.

---

Builds a raw FAT12 image with the EXACT same geometry that
spi_flash.c has "hardcoded" in its constants - instead of relying on
mkfs.vfat guessing the same non-standard geometry (root dir of 512
entries/32 sectors, 4 reserved sectors, FAT of 6 sectors) that
was originally used to format this card from the factory.

Geometry (from spi_flash.c, 08/21/2026):
  bytes_per_sector   = 512
  sectors_per_cluster= 1
  reserved_sectors   = 4   (FAT1_LBA=4)
  num_fats           = 2
  sectors_per_fat    = 6   (FAT_SECTORS=6)
  root_dir_sectors   = 32  (ROOT_DIR_LBA=16, DATA_START_SECTOR=48 -> 48-16=32)
  root_dir_entries   = 512 (32*512/32)
  total_sectors      = 2048 (48 + DATA_CLUSTER_COUNT=2000)

Usage:
  python3 tools/fat12_imagen.py output.img
  sudo dd if=output.img of=/dev/sdX bs=512 conv=fsync status=progress

*** CONFIRM THE DEVICE WITH lsblk BEFORE RUNNING dd TO A REAL /dev/sdX ***
"""
import sys
import struct

SECTOR = 512
RESERVED = 4
NUM_FATS = 2
FAT_SECTORS = 6
ROOT_DIR_SECTORS = 32
TOTAL_SECTORS = 2048
MEDIA = 0xF8

def build_boot_sector():
    bs = bytearray(SECTOR)
    bs[0:3] = b'\xEB\x3C\x90'                    # BS_jmpBoot
    bs[3:11] = b'MSDOS5.0'                        # BS_OEMName
    struct.pack_into('<H', bs, 11, SECTOR)        # BPB_BytsPerSec
    bs[13] = 1                                    # BPB_SecPerClus
    struct.pack_into('<H', bs, 14, RESERVED)      # BPB_RsvdSecCnt
    bs[16] = NUM_FATS                             # BPB_NumFATs
    struct.pack_into('<H', bs, 17, 512)           # BPB_RootEntCnt (512 entries)
    struct.pack_into('<H', bs, 19, TOTAL_SECTORS) # BPB_TotSec16
    bs[21] = MEDIA                                # BPB_Media
    struct.pack_into('<H', bs, 22, FAT_SECTORS)   # BPB_FATSz16
    struct.pack_into('<H', bs, 24, 32)            # BPB_SecPerTrk (legacy, unused)
    struct.pack_into('<H', bs, 26, 64)            # BPB_NumHeads (legacy, unused)
    struct.pack_into('<I', bs, 28, 0)             # BPB_HiddSec
    struct.pack_into('<I', bs, 32, 0)             # BPB_TotSec32 (using TotSec16 instead)
    bs[36] = 0x00                                 # BS_DrvNum
    bs[37] = 0x00                                 # BS_Reserved1
    bs[38] = 0x29                                 # BS_BootSig
    struct.pack_into('<I', bs, 39, 0x12345678)    # BS_VolID
    bs[43:54] = b'HFDLCARD   '[:11]                # BS_VolLab (11 bytes)
    bs[54:62] = b'FAT12   '                        # BS_FilSysType
    bs[510] = 0x55
    bs[511] = 0xAA
    return bytes(bs)

def build_fat_sectors():
    fat = bytearray(FAT_SECTORS * SECTOR)
    # FAT[0] = 0xF00 | media byte, FAT[1] = 0xFFF (standard convention)
    fat[0] = MEDIA
    fat[1] = 0xFF
    fat[2] = 0xFF
    # rest of the FAT stays zero = every cluster from 2 upward is free
    return bytes(fat)

def main():
    if len(sys.argv) != 2:
        print(f"use: {sys.argv[0]} output.img")
        sys.exit(1)

    out_path = sys.argv[1]
    image = bytearray(TOTAL_SECTORS * SECTOR)

    # Sector 0: boot sector. Sectors 1-3: reserved, stay zero.
    image[0:SECTOR] = build_boot_sector()

    # FAT1 at sector RESERVED (4), FAT2 right after (sector 10)
    fat_bytes = build_fat_sectors()
    fat1_off = RESERVED * SECTOR
    fat2_off = (RESERVED + FAT_SECTORS) * SECTOR
    image[fat1_off:fat1_off + len(fat_bytes)] = fat_bytes
    image[fat2_off:fat2_off + len(fat_bytes)] = fat_bytes

    # Root directory at sector (RESERVED + 2*FAT_SECTORS) = 16,
    # ROOT_DIR_SECTORS (32) sectors, all zero = empty (no entries)
    root_dir_start_sector = RESERVED + NUM_FATS * FAT_SECTORS
    assert root_dir_start_sector == 16, root_dir_start_sector
    # already zero from bytearray init - nothing to do

    data_start_sector = root_dir_start_sector + ROOT_DIR_SECTORS
    assert data_start_sector == 48, data_start_sector
    # data area already zero - fine, unallocated until files are written

    with open(out_path, 'wb') as f:
        f.write(image)

    print(f"written {out_path}: {len(image)} bytes ({len(image)/1024:.0f} KiB, {TOTAL_SECTORS} sectors)")
    print(f"  boot sector: sector 0")
    print(f"  FAT1: sector {RESERVED}, {FAT_SECTORS} sectors")
    print(f"  FAT2: sector {RESERVED + FAT_SECTORS}, {FAT_SECTORS} sectors")
    print(f"  root directory: sector {root_dir_start_sector}, {ROOT_DIR_SECTORS} sectors (512 entries)")
    print(f"  data: sector {data_start_sector} onwards, {TOTAL_SECTORS - data_start_sector} sectors")


if __name__ == '__main__':
    main()
