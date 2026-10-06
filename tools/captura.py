#!/usr/bin/env python3
"""
Emula el cargador de serie y anota TODO lo que le escribe al panel.

POR QUE. panel_serie.c tiene las tres ramas del de serie, pero se
capturaron EMPEZANDO EN EL DESTINO DEL SALTO, asi que todo lo que el de
serie hace desde el reset hasta ramificar se quedo fuera. Eso se noto el
05/10: a la rama B le faltaban los dos primeros comandos del desbloqueo
-F000/F001- y por eso sus 326 primeros pasos caian en un controlador
cerrado. Aplicarlos sobre una base incompleta sale PEOR que no aplicar
nada, medido en la radio.

Aqui no se desensambla a mano ni se adivina: se EJECUTA el binario en un
Cortex-M4 emulado y se anota cada escritura al bus del panel, en orden.
Lo que salga es lo que hace, no lo que yo crea que hace.
"""
import struct, sys
from unicorn import *
from unicorn.arm_const import *

FLASH = 0x08000000
SRAM  = 0x20000000;  SRAM_N = 256*1024
TCM   = 0x10000000;  TCM_N  = 64*1024
PERIF = 0x40000000;  PERIF_N= 0x10000000
SIST  = 0x1FFF0000;  SIST_N = 0x10000
SCB   = 0xE0000000;  SCB_N  = 0x10000
LCD   = 0x60000000;  LCD_N  = 0x00100000
LCD_REG = 0x60000000          # puerto de comandos
LCD_DAT = 0x60020000          # puerto de datos

class Captura:
    def __init__(self, id_0a, id_3a, techo=200_000_000):
        self.pasos = []         # ('cmd',v) / ('dat',v)
        self.ultimo_cmd = None
        self.lecturas = 0
        self.id_0a = id_0a
        self.id_3a = id_3a
        self.techo = techo
        self.fin = None
        self.reloj = None        # se engancha en corre(); fecha cada escritura

    def w(self, uc, acceso, dire, tam, val, ud):
        if dire == LCD_REG or (LCD_REG <= dire < LCD_REG+2):
            self.pasos.append(('cmd', val & 0xFFFF, self.reloj.ms))
            self.ultimo_cmd = val & 0xFFFF
        elif LCD_DAT <= dire < LCD_DAT+2:
            self.pasos.append(('dat', val & 0xFFFF, self.reloj.ms))

    def r(self, uc, acceso, dire, tam, val, ud):
        if LCD_DAT <= dire < LCD_DAT+2:
            self.lecturas += 1
            c = self.ultimo_cmd
            # el datasheet dice que la PRIMERA lectura no vale
            if self.lecturas % 2 == 1:
                v = 0x0000
            else:
                v = {0x000A: self.id_0a, 0x3A00: self.id_3a}.get(c, 0x0000)
            uc.mem_write(dire, struct.pack('<H', v))

# EL RELOJ DE MILISEGUNDOS, A MANO - 0x08000CF8.
#
# Esa funcion es "devuelve el contador de ms" (ldr r0,[pc,#4]; ldr r0,[r0,#8];
# bx lr) y lo alimenta la interrupcion de SysTick, que aqui no corre. Sin
# eso, los bucles de espera con tiempo limite -"espera a que se apague el
# bit, pero no mas de 2 ms"- no salen NUNCA: ni se apaga el bit ni vence el
# plazo. Asi que se emula la funcion entera: devolver un contador que sube.
# No toca el bus del panel, solo deja correr el tiempo.
RELOJ_MS = 0x08000CF8

class Reloj:
    def __init__(self): self.ms = 0
    def __call__(self, uc, dire, tam, ud):
        self.ms += 1
        uc.reg_write(UC_ARM_REG_R0, self.ms)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR) | 1)


# LO QUE NO ESTA EN EL MAPA SE MAPEA Y SE SIGUE. No interesa reproducir el
# chip entero, solo llegar hasta el panel; cualquier zona que el de serie
# toque y que aqui no exista se crea vacia y se anota, para saber que se
# ha hecho.
TOCADO = []

def no_mapeado(uc, acceso, dire, tam, val, ud):
    base = dire & ~0xFFFF
    TOCADO.append((uc.reg_read(UC_ARM_REG_PC), dire))
    try:
        uc.mem_map(base, 0x10000)
    except UcError:
        return False
    return True


def periferico_lee(uc, acceso, dire, tam, val, ud):
    """Los bits de 'listo' se dan por puestos: si no, los bucles de espera
    del reloj no salen nunca.  Es la unica licencia que se toma aqui, y no
    toca el bus del panel."""
    uc.mem_write(dire, b'\xFF' * tam)

def corre(id_0a, id_3a, techo=60_000_000):
    d = open('boot.bin','rb').read()
    sp, pc = struct.unpack('<II', d[:8])
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.mem_map(FLASH, max(len(d), 0x100000))
    uc.mem_write(FLASH, d)
    for b, n in ((SRAM,SRAM_N),(TCM,TCM_N),(PERIF,PERIF_N),(SIST,SIST_N),(SCB,SCB_N),(LCD,LCD_N)):
        uc.mem_map(b, n)
    uc.mem_write(SIST+0x7A10, struct.pack('<III', 0x54485643, 0x0A333833, 0x38483650))
    uc.mem_write(SIST+0x7A22, struct.pack('<H', 512))     # flash en kB

    reloj = Reloj()
    cap = Captura(id_0a, id_3a, techo)
    cap.reloj = reloj
    uc.hook_add(UC_HOOK_MEM_WRITE, cap.w, begin=LCD, end=LCD+LCD_N-1)
    uc.hook_add(UC_HOOK_MEM_READ,  cap.r, begin=LCD, end=LCD+LCD_N-1)
    uc.hook_add(UC_HOOK_MEM_READ, periferico_lee, begin=PERIF, end=PERIF+PERIF_N-1)
    uc.hook_add(UC_HOOK_CODE, reloj, begin=RELOJ_MS, end=RELOJ_MS)
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, no_mapeado)

    uc.reg_write(UC_ARM_REG_SP, sp)
    try:
        uc.emu_start(pc | 1, 0, count=techo)
        cap.fin = 'fin del presupuesto'
    except UcError as e:
        cap.fin = '%s en PC=0x%08X' % (e, uc.reg_read(UC_ARM_REG_PC))
    return cap

if __name__ == '__main__':
    a = int(sys.argv[1],16) if len(sys.argv)>1 else 0x0000
    b = int(sys.argv[2],16) if len(sys.argv)>2 else 0x0077
    c = corre(a,b)
    print('  0x000A=%04X  0x3A00=%04X' % (a,b))
    print('  escrituras al panel: %d   lecturas: %d' % (len(c.pasos), c.lecturas))
    print('  termina: %s' % c.fin)
    for i,(t,v,ms) in enumerate(c.pasos[:40]):
        print('    %4d  %s %04X  (%d ms)' % (i,t,v,ms))
    if TOCADO:
        import collections
        print('  zonas creadas al vuelo:')
        for (pc,d2),n in collections.Counter(TOCADO).most_common(6):
            print('     PC=0x%08X  -> 0x%08X  (%d)' % (pc,d2,n))
