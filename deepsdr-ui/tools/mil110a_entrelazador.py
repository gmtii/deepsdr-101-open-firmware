import numpy as np
# MIL-STD-188-110A 5.3.2.3.4 (carga) y 5.3.2.3.5 (lectura).
#
# CARGA: matriz de 40 filas x C columnas. Se empieza en la columna 0 y la
#   fila avanza de 9 en 9 modulo 40 (0, 9, 18, 27, 36, 5, ...) hasta
#   llenar las 40; luego columna 1, y asi.
#       indice de carga m -> celda ( (9*(m%40))%40 , m//40 )
#
# LECTURA: primer bit de fila 0 columna 0. Cada siguiente: fila +1 y
#   columna -17 modulo C. Al llegar la fila al maximo, fila=0 y la
#   columna vuelve a la que tenia cuando la fila valia 0, mas uno.
#       indice de lectura k -> celda ( k%40 , ((k//40) - 17*(k%40)) % C )
#
# Para deshacerlo: de la celda se vuelve al indice de carga. Como
# 9*9 = 81 = 1 (mod 40), el inverso de 9 modulo 40 es 9.
def tabla(C, filas=40, salto=9, dec=17):
    N=filas*C
    k=np.arange(N)
    r=k%filas
    c=((k//filas) - dec*r) % C
    inv=pow(salto,-1,filas)
    i=(inv*r)%filas
    m=c*filas+i
    return m          # desentrelazado[m[k]] = recibido[k]
if __name__=='__main__':
    for C in (18,144):
        m=tabla(C)
        print('C=%3d: %s  (permutacion valida: %s)'%(C,m[:8],len(np.unique(m))==len(m)))
