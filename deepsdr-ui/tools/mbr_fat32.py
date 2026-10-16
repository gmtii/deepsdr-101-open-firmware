#!/usr/bin/env python3
"""Pone una tabla de particiones con UNA particion FAT32 LBA.

Existe porque mkfs.fat no particiona: hace el sistema de ficheros y nada
mas. Y un pincho de fabrica SI viene particionado, con el arranque en el
sector 2048 y no en el 0. Sin una imagen asi, el banco del FAT32 aprobaria
codigo que no sabe atravesar un MBR."""
import struct, sys

ruta = sys.argv[1]
ini  = int(sys.argv[2])
with open(ruta, 'r+b') as f:
    f.seek(0, 2)
    total = f.tell() // 512
    mbr = bytearray(512)
    e = bytearray(16)
    e[4] = 0x0C                       # FAT32 con direccionamiento LBA
    struct.pack_into('<I', e, 8, ini)
    struct.pack_into('<I', e, 12, total - ini)
    mbr[446:462] = e
    mbr[510], mbr[511] = 0x55, 0xAA
    f.seek(0)
    f.write(mbr)
print("  MBR: particion FAT32 en el sector %d, %d sectores" % (ini, total - ini))
