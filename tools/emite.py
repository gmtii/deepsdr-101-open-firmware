#!/usr/bin/env python3
"""Escribe boot/panel_tablas.h a partir de lo capturado del boot.bin de serie."""
import pickle, sys

CAB = '''/*
 * GENERADO por tools/captura_panel.py - NO EDITAR A MANO.
 *
 * Las tres secuencias de arranque del panel, sacadas del cargador de serie
 * EJECUTANDOLO: tools/captura_panel.py emula el boot.bin en un Cortex-M4 y
 * anota cada escritura al puerto de comandos (0x60000000) y al de datos
 * (0x60020000), en orden. Lo que hay aqui es lo que hace, no lo que nadie
 * crea que hace.
 *
 * POR QUE SE REHIZO - 05/10/2026.
 *
 * *** El dueno: "sigo sin entender porque cojones no eres capaz de copiar lo
 * que hace el bootloader original". *** Tenia razon. La captura anterior
 * empezaba en el DESTINO DEL SALTO, asi que se perdia todo lo anterior, y
 * ademas emparejaba un dato por comando. Eso rompio dos ramas:
 *
 *   rama B  le faltaban F000=55 y F001=AA al principio. Sus 326 primeros
 *           pasos -las seis tablas de gamma, el VGMP/VGSP y el VCOM-
 *           caian en un controlador cerrado y se perdian. Las otras 363
 *           escrituras SI coincidian una a una con las del de serie.
 *
 *   rama A  es de OTRO controlador: lleva rafagas de parametros (192, 127,
 *           34, 26, 19 bytes...) detras de un solo comando. La tabla vieja
 *           aplastaba cada rafaga a UNA escritura, asi que no era una
 *           reproduccion incompleta: era otra cosa. Por eso dejaba la
 *           pantalla en blanco.
 *
 * Y EL ORDEN, que es lo que de verdad se nos escapaba: el de serie LEE los
 * dos identificadores ANTES de desbloquear, y desbloquea dentro de la rama.
 * La v2.3 desbloqueaba antes de leer; con eso el 0x3A00 ya no contestaba
 * 0x77, la comparacion fallaba y la radio se iba a la rama C -la lavada-.
 * No era que la gamma de B fuera mala: es que no se estaba usando la B.
 *
 * LAS ESPERAS no salen de la emulacion: el de serie las hace con un bucle
 * que este emulador no cronometra. Los 120 ms detras de Sleep Out (0x11) y
 * de Display On (0x29) son los del datasheet, y son los mismos que ya
 * llevaba la tabla anterior.
 */
#ifndef PANEL_TABLAS_H
#define PANEL_TABLAS_H

#include <stdint.h>

/* Un paso: un comando, sus datos (ninguno, uno o una rafaga) y lo que hay
 * que esperar despues. idx apunta a k_datos; IDX_COLOR es la tabla de color
 * de la rama A, que se genera en vez de guardarse: son 64 escalones por
 * canal, i<<3 / i<<2 / i<<3, comprobado contra la captura. */
#define IDX_COLOR 0xFFFFU

typedef struct {
    uint16_t cmd;
    uint16_t idx;
    uint8_t  n;
    uint8_t  ms;
} paso_t;

'''

def main():
    r = pickle.load(open('ramas.pkl','rb'))
    datos, out = [], {}
    for nom in ('A','B','C'):
        pasos = []
        for cmd, d, tipo, dd in r[nom]:
            if tipo == 'rafaga' and cmd == 0x002D:
                pasos.append((cmd, 'IDX_COLOR', 0, 0)); continue
            if tipo == 'solo':      vals = []
            elif tipo == 'dato':    vals = [d]
            else:                   vals = list(dd)
            idx = len(datos); datos.extend(vals)
            ms = 120 if cmd in (0x1100, 0x0011, 0x2900, 0x0029) else 0
            pasos.append((cmd, '%uU' % idx, len(vals), ms))
        out[nom] = pasos

    with open('panel_tablas.h','w',encoding='utf-8') as f:
        f.write(CAB)
        f.write('static const uint16_t k_datos[] = {\n')
        for i in range(0, len(datos), 12):
            f.write('    ' + ' '.join('0x%03X,' % v for v in datos[i:i+12]) + '\n')
        f.write('};\n\n')
        for nom in ('A','B','C'):
            f.write('static const paso_t k_rama_%s[] = {\n' % nom)
            for cmd, idx, n, ms in out[nom]:
                f.write('    {0x%04X, %s, %u, %u},\n' % (cmd, idx, n, ms))
            f.write('};\n\n')
        f.write('#endif\n')
    print('  k_datos: %d valores' % len(datos))
    for nom in ('A','B','C'):
        print('  rama %s: %d pasos' % (nom, len(out[nom])))

main()
