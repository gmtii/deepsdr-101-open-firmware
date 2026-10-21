#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
LOS NUMEROS DE WEFAX, PROBADOS POR MUTACION - 21/10/2026.

Un banco en verde dice que el codigo que hay pasa. No dice que el banco
MIDA ese codigo. La forma de saberlo es romperlo a proposito: si se cambia
una decision del decodificador y el banco sigue en verde, esa decision no
la vigila nadie.

Se hizo a mano en la pasada 26 sobre quince decisiones de User/wefax.c, y
el resultado fue que DIEZ no las vigilaba nadie, entre ellas:

  - la capacidad util del anillo, que es N-1 a proposito. Con N, el
    escritor y el lector acaban en la misma ranura y el recogedor entrega
    el renglon que se esta rellenando. El caso 10 lo producia y su liston
    estaba TRES niveles por encima del escalon que salia;
  - el umbral del tono de arranque DENTRO de la imagen, que es 6.000 y no
    200 para que una trama de semitono no parta el fichero en dos;
  - el del tono de parada, que es 200 para que un tono desvanecido se oiga
    igual y no se cosan dos cartas en un fichero;
  - la razon de 3 a 1 entre los dos bines, que es lo que impide que un
    brillo que oscila a otra frecuencia pase por tono;
  - el tres de FASE_FIN_N, que esta para que un chasquido no adelante el
    final de la señal de fase.

Las cinco se cerraron con casos nuevos -10 apretado, 11b, 11c, 12b, 12c y
12d- y este guion se queda para que la cuenta no se pierda: cada vez que se
toque wefax.c, dice cuantas de estas decisiones siguen vigiladas.

CUELGA DE `comprueba`, y son quince compilaciones y treinta ejecuciones:
cuarenta y cinco segundos. Se penso dejarlo a mano y no: un guardian que hay
que acordarse de correr es un guardian que no se corre, que es la misma
cuenta que ya se hizo con el cerrojo de las cartas. Cuarenta y cinco
segundos sobre los tres minutos que cuesta `comprueba` es un precio que se
paga.

Lo que sale tiene que ser EXACTAMENTE lo que dice ESPERADAS. Si sale menos,
alguien ha quitado un caso del banco y hay que devolverlo; si sale mas, se
ha cerrado un agujero y hay que subir el numero. Las dos cosas paran el
make, porque las dos quieren decir que esta cuenta y el banco ya no dicen lo
mismo.

LOS QUE SOBREVIVEN, Y POR QUE SE DEJAN:

  - `renormaliza 64 -> 1024`: el fasor de pulso_centro() se renormaliza
    cada 64 puntos. Sobrevive porque NO IMPORTA, y eso esta escrito desde
    el principio en su comentario: la deriva de un giro acumulado en float
    a lo largo de 756 pasos es del orden de 1e-5 rad y el umbral que decide
    no se entera. Un mutante que sobrevive porque el codigo mutado es
    igual de bueno no es un agujero;
  - `clamp ppm 30000 -> 3000`: es una barandilla contra una medida
    disparatada. Para ejercitarla hace falta una señal disparatada, y
    entonces lo que se mide es la señal del banco y no el decodificador;
  - `suma minima 4 -> 2` y `media 0.04 -> 0.08`: agujeros de verdad, pero
    pequeños -cuantos puntos blancos minimos hace falta ver para creerse un
    pulso, y la constante de tiempo del detector de tonos-. Quedan
    apuntados y sin cerrar.

Y EL SEGUNDO FRENTE: EL GIRO DEL FICHERO - 21/10/2026.

La pasada 27 le hizo lo mismo a User/wfx_recoloca.c contra sim/recoloca.c.
Quince mutaciones, NUEVE cazadas. Las seis que sobreviven, y por que se
dejan:

  - `nb < 2 -> nb < 1`, `el fin se decide solo por s_nb`, `...solo por pix`,
    y `no se mira si queda imagen al empezar el paso`: las CUATRO son la
    misma cosa, y conviene decirla porque no estaba escrita. El final del
    giro lo deciden tres condiciones -se acabaron los bloques, se acabo la
    imagen, y la de la entrada del paso- y en un fichero de verdad las tres
    caen en el MISMO bloque. No es casualidad: el fichero se recorta al
    cerrarlo -spi_flash_recorta_cadena()- a los bytes que de verdad ocupa,
    asi que no le sobra ni un bloque. O sea que dos de las tres son cinturon
    y tirantes de la tercera, y por construccion no se pueden ejercitar. Se
    quedan igual: lo que protegen es un fichero cuya cadena no es la que su
    entrada de directorio dice, y de eso no hay quien se fie;
  - `el pct no se recorta`: es la barra de progreso;
  - `olvidar no limpia el recorrido`: s_it solo se usa mientras el giro
    anda, y empieza() lo vuelve a abrir. Higiene, no correccion.
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UI   = os.path.join(RAIZ, 'deepsdr-ui')
CC   = os.environ.get('CC', 'gcc')

ESPERADAS = 11   # de las 15 de wefax.c. Ver la cabecera.
ESPERADAS_REC = 9   # de las 15 de wfx_recoloca.c.

MUTANTES = [
 ('el anillo, de ocho renglones a cuatro',
  '#define WFX_ANILLO 8U', '#define WFX_ANILLO 4U'),
 ('la capacidad util del anillo, de N-1 a N',
  'if (s_linea_lista >= (WFX_ANILLO - 1U))', 'if (s_linea_lista >= WFX_ANILLO)'),
 ('las lineas seguidas que acaban la fase, de 3 a 2',
  '#define FASE_FIN_N 3U', '#define FASE_FIN_N 2U'),
 ('las lineas que se miden por pasada, de 16 a 8',
  '#define FASE_LINEAS 16U', '#define FASE_LINEAS 8U'),
 ('"la linea es negra", de 80 a 120',
  'if (media > 80UL) { return -1.0f; }', 'if (media > 120UL) { return -1.0f; }'),
 ('"los claros estan juntos", de 0,85 a 0,50',
  'if (r < 0.85f) { return -1.0f; }', 'if (r < 0.50f) { return -1.0f; }'),
 ('los puntos claros minimos de un pulso, de 4 a 2',
  'if (suma < 4UL ', 'if (suma < 2UL '),
 ('el tono de arranque DENTRO de la imagen, de 6.000 a 2.000',
  '? 6000.0f : 200.0f', '? 2000.0f : 200.0f'),
 ('el tono de parada, de 200 a 400',
  'if (s_apt450 > 200.0f', 'if (s_apt450 > 400.0f'),
 ('la media del detector de tonos, de medio segundo a un cuarto',
  's_apt300 += 0.04f', 's_apt300 += 0.08f'),
 ('la razon entre los dos bines, de 3 a 1,5',
  's_apt300 > s_apt450 * 3.0f', 's_apt300 > s_apt450 * 1.5f'),
 ('el pulso se lleva por su borde y no por su centro',
  'float q = FASE_PULSO * 0.5f;', 'float q = FASE_PULSO;'),
 ('el tope de la correccion, de 30.000 ppm a 3.000',
  'if (ppm > 30000.0f)  { ppm = 30000.0f; }',
  'if (ppm > 3000.0f)  { ppm = 3000.0f; }'),
 ('el fasor se renormaliza cada 1.024 puntos y no cada 64',
  'if ((i & 63U) == 63U)', 'if ((i & 1023U) == 1023U)'),
 ('la espera maxima en la fase, de 35 s a 40',
  '(ARRANQUE_SEG + FASE_SEG)) / 60UL;', '(40U)) / 60UL;'),
]

BANCOS = ('wefaxtest.c', 'wefaxfuzz.c')
OTROS  = ('disc_fm.c', 'nco.c', 'nco_tabla.c')

# --- el segundo frente: el giro del fichero ---
MUTANTES_REC = [
 ('el bloque de trabajo, de 4 kB a 2',
  '#define BLOQUE  WFX_REC_BLOQUE', '#define BLOQUE  2048U'),
 ('se admite el corte en la columna cero',
  'if (corte == 0U || (uint32_t)corte >= (uint32_t)ancho) { return 0U; }',
  'if ((uint32_t)corte >= (uint32_t)ancho) { return 0U; }'),
 ('un fichero de un solo bloque vale',
  'if (nb < 2UL) { return 0U; }', 'if (nb < 1UL) { return 0U; }'),
 ('no se comprueba que la altura quepa',
  '    if ((1UL + (((uint32_t)alto * (uint32_t)ancho) + BLOQUE - 1UL) / BLOQUE) > nb) {',
  '    if (0) {'),
 ('el giro no da la vuelta por el final',
  'if (j >= s_ancho) { j = 0UL; }', 'if (j > s_ancho) { j = 0UL; }'),
 ('el carro se copia desde su principio y no desde donde toca',
  'for (i = 0UL; i < queda; i++) { s_buf[i] = s_carro[dentro + i]; }',
  'for (i = 0UL; i < queda; i++) { s_buf[i] = s_carro[i]; }'),
 ('el carro no se da por guardado',
  '        s_carro_hay = 1U;', '        s_carro_hay = 0U;'),
 ('el final se decide solo por los bloques',
  'if (s_b >= s_nb || ((s_b - 1UL) * BLOQUE) >= pix) {',
  'if (s_b >= s_nb) {'),
 ('el final se decide solo por la imagen',
  'if (s_b >= s_nb || ((s_b - 1UL) * BLOQUE) >= pix) {',
  'if (((s_b - 1UL) * BLOQUE) >= pix) {'),
 ('no se mira si queda imagen al empezar el paso',
  'if (p0 >= pix) {', 'if (0) {'),
 ('el tanto por ciento no se recorta',
  'if (hechos > total) { hechos = total; }', 'if (0) { hechos = total; }'),
 ('olvidar no limpia el recorrido', 's_it.vale   = 0U;', '(void)0;'),
 ('la cabecera del BMP no se salta',
  '    if (!spi_flash_bloques_sig(&s_it, &s_a_act)) { return 0U; }\n'
  '    if (!spi_flash_bloques_sig(&s_it, &s_a_act)) { return 0U; }',
  '    if (!spi_flash_bloques_sig(&s_it, &s_a_act)) { return 0U; }'),
 ('la fila que cruza la frontera no se guarda girada',
  'gira(s_carro, s_fila);', 'memcpy(s_carro, s_fila, s_ancho);'),
 ('el bloque girado no se escribe',
  '    spi_flash_write_block_4k(s_a_act, s_buf);\n\n    /* 5. Un bloque mas. */',
  '    /* 5. Un bloque mas. */'),
]

REC_OTROS = ('spi_flash.c', 'bmp.c')
REC_SIM   = ('recoloca.c', 'w25q_sim.c', 'gd32_pines.c')


def compila_y_corre(wefax, banco, tmp):
    exe = os.path.join(tmp, 'b')
    orden = [CC, '-O2', '-w', '-std=gnu99',
             '-I' + os.path.join(RAIZ, 'User'),
             '-I' + os.path.join(UI, 'hostinc'),
             '-I' + os.path.join(UI, 'sim'),
             '-o', exe, os.path.join(UI, 'sim', banco), wefax]
    orden += [os.path.join(RAIZ, 'User', x) for x in OTROS]
    orden += ['-lm']
    if subprocess.call(orden, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL) != 0:
        return None                      # no compila: cuenta como cazado
    return subprocess.call([exe], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)


def rec_compila_y_corre(rec, tmp, img):
    """El banco del giro: necesita el volumen FAT12 vacio como argumento."""
    exe = os.path.join(tmp, 'r')
    orden = [CC, '-O2', '-w', '-std=gnu99',
             '-I' + os.path.join(RAIZ, 'User'),
             '-I' + os.path.join(UI, 'hostinc'),
             '-I' + os.path.join(UI, 'sim'),
             '-o', exe]
    orden += [os.path.join(UI, 'sim', x) for x in REC_SIM]
    orden += [os.path.join(RAIZ, 'User', x) for x in REC_OTROS]
    orden += [rec, os.path.join(UI, 'gfx2', 'idioma.c'), '-lm']
    if subprocess.call(orden, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL) != 0:
        return None
    # Cada mutante estrena volumen: el banco escribe en el.
    propio = os.path.join(tmp, 'vol.img')
    with open(img, 'rb') as f, open(propio, 'wb') as g:
        g.write(f.read())
    return subprocess.call([exe, propio], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)


def frente(titulo, fuente, mutantes, esperadas, corre):
    bueno = open(fuente, encoding='utf-8').read()
    cazados = 0
    print('\n%s' % titulo)
    with tempfile.TemporaryDirectory() as tmp:
        w = os.path.join(tmp, os.path.basename(fuente))
        for (nom, a, b) in mutantes:
            n = bueno.count(a)
            if n != 1:
                print('  %-56s el patron sale %d veces: el guion ha dejado'
                      ' de medir este.   MAL' % (nom, n))
                return 1
            open(w, 'w', encoding='utf-8').write(bueno.replace(a, b))
            rojo = corre(w, tmp)
            cazados += 1 if rojo else 0
            print('  %-56s %s' % (nom, 'lo caza el banco' if rojo
                                  else 'SOBREVIVE'))
    print('  %d de %d decisiones vigiladas (se esperan %d)'
          % (cazados, len(mutantes), esperadas))
    if cazados < esperadas:
        print('  han dejado de vigilarse %d: se ha quitado un caso del banco.'
              '   MAL' % (esperadas - cazados))
        return 1
    if cazados > esperadas:
        print('  son MAS de las esperadas: sube el numero en este guion.'
              '   MAL')
        return 1
    return 0


def main():
    print('los numeros de WEFAX, probados rompiendolos a proposito')
    img = os.path.join(UI, 'out', 'fat12_vacio.img')
    if not os.path.exists(img):
        print('  falta %s: lo hace `make out/fat12_vacio.img`.   MAL' % img)
        return 1

    def corre_wefax(w, tmp):
        for banco in BANCOS:
            r = compila_y_corre(w, banco, tmp)
            if r is None or r != 0:
                return True
        return False

    def corre_rec(w, tmp):
        r = rec_compila_y_corre(w, tmp, img)
        return r is None or r != 0

    malos = 0
    malos |= frente('la señal: los numeros de User/wefax.c',
                    os.path.join(RAIZ, 'User', 'wefax.c'),
                    MUTANTES, ESPERADAS, corre_wefax)
    malos |= frente('el giro: los numeros de User/wfx_recoloca.c',
                    os.path.join(RAIZ, 'User', 'wfx_recoloca.c'),
                    MUTANTES_REC, ESPERADAS_REC, corre_rec)
    if malos:
        return 1
    print('\n  ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
