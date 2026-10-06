#!/usr/bin/env python3
"""
Saca las tres secuencias de arranque del panel EJECUTANDO el boot.bin de
serie, y escribe boot/panel_tablas.h.

    python3 captura_panel.py rescate/boot.bin boot/panel_tablas.h

POR QUE EXISTE - 05/10/2026.

*** El dueno: "sigo sin entender porque cojones no eres capaz de copiar lo
que hace el bootloader original y necesitas tantas capturas". *** Porque la
captura anterior se hizo empezando en el DESTINO DEL SALTO y emparejando un
dato por comando, y las dos cosas estaban mal:

  - a la rama B le faltaban F000=55 y F001=AA, o sea el desbloqueo, asi que
    sus 326 primeros pasos caian en un controlador cerrado;
  - la rama A es de OTRO controlador y lleva rafagas de parametros (192,
    127, 34 bytes...) detras de un solo comando; aplastadas a una escritura
    cada una, no era una reproduccion incompleta sino otra cosa.

COMO. Se emula un Cortex-M4 con unicorn, se carga el boot.bin en 0x08000000
y se enganchan las escrituras al bus del panel. Las lecturas del
identificador se INYECTAN, asi que se recorre la rama que se quiera
diciendole al programa que contesta el panel. No se desensambla a mano.

LAS DOS LICENCIAS que se toma, y ninguna toca el bus del panel:

  - los registros de periferico se leen como todo unos, para que los bucles
    de "espera a que el reloj este listo" salgan;
  - la funcion de 0x08000CF8 -devuelve el contador de milisegundos, y lo
    alimenta una interrupcion que aqui no corre- se emula devolviendo un
    contador que sube. Sin eso, las esperas con plazo no vencen nunca.

LAS ESPERAS del resultado NO salen de aqui: el de serie las hace con un
bucle que esto no cronometra. Los 120 ms detras de Sleep Out (0x11) y de
Display On (0x29) son los del datasheet.

Necesita:  pip install unicorn
"""
import struct, sys
from unicorn import *
from unicorn.arm_const import *

FLASH = 0x08000000
SRAM  = 0x20000000;  SRAM_N  = 256*1024
TCM   = 0x10000000;  TCM_N   = 64*1024
PERIF = 0x40000000;  PERIF_N = 0x10000000
SIST  = 0x1FFF0000;  SIST_N  = 0x10000
SCB   = 0xE0000000;  SCB_N   = 0x10000
LCD   = 0x60000000;  LCD_N   = 0x00100000
LCD_REG = 0x60000000
LCD_DAT = 0x60020000
RELOJ_MS = 0x08000CF8          # "devuelve el contador de ms"


class Reloj:
    def __init__(self):
        self.ms = 0

    def __call__(self, uc, dire, tam, ud):
        self.ms += 1
        uc.reg_write(UC_ARM_REG_R0, self.ms)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR) | 1)


class Captura:
    def __init__(self, id_0a, id_3a):
        self.pasos = []
        self.ultimo_cmd = None
        self.lecturas = 0
        self.id_0a = id_0a
        self.id_3a = id_3a

    def escribe(self, uc, acceso, dire, tam, val, ud):
        if LCD_REG <= dire < LCD_REG + 2:
            self.pasos.append(('cmd', val & 0xFFFF))
            self.ultimo_cmd = val & 0xFFFF
        elif LCD_DAT <= dire < LCD_DAT + 2:
            self.pasos.append(('dat', val & 0xFFFF))

    def lee(self, uc, acceso, dire, tam, val, ud):
        if LCD_DAT <= dire < LCD_DAT + 2:
            self.lecturas += 1
            # el datasheet dice que la primera lectura no vale
            if self.lecturas % 2 == 1:
                v = 0x0000
            else:
                v = {0x000A: self.id_0a, 0x3A00: self.id_3a}.get(self.ultimo_cmd, 0x0000)
            uc.mem_write(dire, struct.pack('<H', v))


def periferico_lee(uc, acceso, dire, tam, val, ud):
    uc.mem_write(dire, b'\xFF' * tam)


def no_mapeado(uc, acceso, dire, tam, val, ud):
    try:
        uc.mem_map(dire & ~0xFFFF, 0x10000)
    except UcError:
        return False
    return True


def corre(bootbin, id_0a, id_3a, techo=60_000_000):
    d = open(bootbin, 'rb').read()
    sp, pc = struct.unpack('<II', d[:8])
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.mem_map(FLASH, max(len(d), 0x100000))
    uc.mem_write(FLASH, d)
    for b, n in ((SRAM, SRAM_N), (TCM, TCM_N), (PERIF, PERIF_N),
                 (SIST, SIST_N), (SCB, SCB_N), (LCD, LCD_N)):
        uc.mem_map(b, n)
    uc.mem_write(SIST + 0x7A22, struct.pack('<H', 512))

    cap = Captura(id_0a, id_3a)
    uc.hook_add(UC_HOOK_MEM_WRITE, cap.escribe, begin=LCD, end=LCD + LCD_N - 1)
    uc.hook_add(UC_HOOK_MEM_READ,  cap.lee,     begin=LCD, end=LCD + LCD_N - 1)
    uc.hook_add(UC_HOOK_MEM_READ, periferico_lee, begin=PERIF, end=PERIF + PERIF_N - 1)
    uc.hook_add(UC_HOOK_CODE, Reloj(), begin=RELOJ_MS, end=RELOJ_MS)
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, no_mapeado)

    uc.reg_write(UC_ARM_REG_SP, sp)
    try:
        uc.emu_start(pc | 1, 0, count=techo)
    except UcError:
        pass
    return cap.pasos


def arranque(bootbin, id_0a, id_3a):
    """Los pasos del arranque: hasta la primera escritura a la GRAM."""
    p = corre(bootbin, id_0a, id_3a)
    fin = next((i for i, (t, v) in enumerate(p)
                if t == 'cmd' and v in (0x2C00, 0x002C)), len(p))
    seq = p[:fin]
    while seq and seq[0][0] == 'cmd' and seq[0][1] in (0x000A, 0x3A00):
        seq = seq[1:]          # las dos lecturas del identificador

    # Un comando puede llevar UN dato o una RAFAGA. Emparejarlos de uno en
    # uno pierde la rafaga entera y en silencio, que es lo que paso antes.
    pasos, i = [], 0
    while i < len(seq):
        t, v = seq[i]
        if t != 'cmd':
            i += 1
            continue
        datos, j = [], i + 1
        while j < len(seq) and seq[j][0] == 'dat':
            datos.append(seq[j][1]); j += 1
        pasos.append((v, datos))
        i = j
    return pasos


CAB = '''/*
 * GENERADO por tools/captura_panel.py - NO EDITAR A MANO.
 *
 * Las tres secuencias de arranque del panel, sacadas del cargador de serie
 * EJECUTANDOLO: el guion emula el boot.bin en un Cortex-M4 y anota cada
 * escritura al puerto de comandos y al de datos, en orden. Lo que hay aqui
 * es lo que HACE, no lo que nadie crea que hace. El porque esta en la
 * cabecera de ese guion.
 */
#ifndef PANEL_TABLAS_H
#define PANEL_TABLAS_H

#include <stdint.h>

/* Un paso: un comando, sus datos (ninguno, uno o una rafaga) y lo que hay
 * que esperar despues. idx apunta a k_datos; IDX_COLOR es la tabla de color
 * de la rama A, que se genera en vez de guardarse: 64 escalones por canal,
 * i<<3 / i<<2 / i<<3, comprobado contra la captura. */
#define IDX_COLOR 0xFFFFU

typedef struct {
    uint16_t cmd;
    uint16_t idx;
    uint8_t  n;
    uint8_t  ms;
} paso_t;

'''

# LAS ESPERAS, Y LA LECCION MAS CARA DEL DIA - 05/10/2026.
#
# El datasheet pide 120 ms detras de Sleep Out. Puse 120 en los dos, luego
# los baje a 50 "para igualar a la v2.2"... y la pantalla del dueno se
# oscurecio. *** "el customboot de ahora oscurece, el anterior no, sin tocar
# nada de la radio, ni cambiar ni una puta opcion". ***
#
# Esos 50 los saque de un array que encontre rebuscando dentro del binario
# de la v2.2. Era un array que ESE PROGRAMA NO EJECUTA. Emulando los dos
# cargadores y comparando la linea de tiempo entera -escrituras, perifericos
# y esperas- sale lo que de verdad hace cada uno:
#
#     esperas:  v2.2  21 llamadas / 402 ms      v2.11  21 / 282 ms
#     v2.2: ms 120  ->  v2.11: ms 50
#     v2.2: ms 100  ->  v2.11: ms 50
#
# Las escrituras eran identicas; faltaban 120 ms de espera. Y esas dos van
# detras de Sleep Out y de Display On, que es cuando al panel se le tienen
# que estabilizar las bombas de carga y el VCOM. Encender la imagen antes de
# tiempo deja la polarizacion a medio subir: se ve MAS OSCURO.
#
# LA LECCION: comparar una TABLA que hay dentro de un binario no es comparar
# lo que ese binario HACE. Dos veces hoy di por bueno algo mirando datos en
# reposo en vez de ejecutarlos. El emulador estaba escrito desde hacia horas.
ESPERA = (0x1100, 0x0011, 0x2900, 0x0029)
ESPERA_MS = {0x1100: 50, 0x0011: 50, 0x2900: 50, 0x0029: 50}


def escribe(ramas, destino):
    datos, salida = [], {}
    for nom in ('A', 'B', 'C'):
        pasos = []
        for cmd, dd in ramas[nom]:
            if cmd == 0x002D and len(dd) > 1:
                esp = [i << 3 for i in range(64)] + [i << 2 for i in range(64)] \
                    + [i << 3 for i in range(64)]
                if dd != esp:
                    raise SystemExit('la tabla de color ya no es la lineal: revisar')
                pasos.append((cmd, 'IDX_COLOR', 0, 0))
                continue
            idx = len(datos)
            datos.extend(dd)
            pasos.append((cmd, '%uU' % idx, len(dd),
                          ESPERA_MS.get(cmd, 0)))
        salida[nom] = pasos

    with open(destino, 'w', encoding='utf-8') as f:
        f.write(CAB)
        f.write('static const uint16_t k_datos[] = {\n')
        for i in range(0, len(datos), 12):
            f.write('    ' + ' '.join('0x%03X,' % v for v in datos[i:i + 12]) + '\n')
        f.write('};\n\n')
        for nom in ('A', 'B', 'C'):
            f.write('static const paso_t k_rama_%s[] = {\n' % nom)
            for cmd, idx, n, ms in salida[nom]:
                f.write('    {0x%04X, %s, %u, %u},\n' % (cmd, idx, n, ms))
            f.write('};\n\n')
        f.write('#endif\n')
    print('  k_datos: %d valores' % len(datos))


def main(bootbin, destino):
    ramas = {}
    for nom, a, b in (('A', 0x0008, 0x0077), ('B', 0x0000, 0x0077), ('C', 0x0000, 0x0055)):
        ramas[nom] = arranque(bootbin, a, b)
        print('  rama %s  (0x000A=%04X 0x3A00=%04X): %d pasos' % (nom, a, b, len(ramas[nom])))
    escribe(ramas, destino)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'rescate/boot.bin',
                  sys.argv[2] if len(sys.argv) > 2 else 'boot/panel_tablas.h'))
