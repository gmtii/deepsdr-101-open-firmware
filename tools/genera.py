#!/usr/bin/env python3
"""Saca las tres ramas EJECUTANDO el boot.bin de serie, y escribe las tablas."""
import pickle, sys
import captura as C

def arranque(id_0a, id_3a):
    c = C.corre(id_0a, id_3a)
    p = c.pasos
    fin = next((i for i,(t,v) in enumerate(p) if t=='cmd' and v in (0x2C00,0x002C)), len(p))
    seq = p[:fin]
    # fuera las dos lecturas de identificador del principio
    while seq and seq[0][0]=='cmd' and seq[0][1] in (0x000A,0x3A00):
        seq = seq[1:]
    # Un comando puede llevar UN dato (lo normal) o una RAFAGA -la tabla de
    # color de la rama A son 192 datos detras de un 002D-. Si se emparejan
    # de uno en uno, la rafaga se pierde entera y en silencio.
    pasos, i = [], 0
    while i < len(seq):
        t, v = seq[i]
        if t != 'cmd':
            i += 1
            continue
        datos = []
        j = i + 1
        while j < len(seq) and seq[j][0] == 'dat':
            datos.append(seq[j][1]); j += 1
        if len(datos) == 0:   pasos.append((v, 0, 'solo', None))
        elif len(datos) == 1: pasos.append((v, datos[0], 'dato', None))
        else:                 pasos.append((v, 0, 'rafaga', datos))
        i = j
    return pasos

if __name__ == '__main__':
    ramas = {}
    for nom, a, b in (('A',0x0008,0x0077), ('B',0x0000,0x0077), ('C',0x0000,0x0055)):
        pr = arranque(a,b)
        ramas[nom] = pr
        print('  rama %s  (0x000A=%04X 0x3A00=%04X): %d pasos   primeros: %s'
              % (nom,a,b,len(pr), ' '.join('%04X=%02X'%(c2,d) for c2,d,t,_ in pr[:5])))
    pickle.dump(ramas, open('ramas.pkl','wb'))
