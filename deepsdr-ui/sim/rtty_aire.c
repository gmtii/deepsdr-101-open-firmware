/*
 * EL RTTY DE VERDAD, CONTRA UNA GRABACION.
 *
 * Pedido por el usuario, 06/10/2026.
 *
 * Las dos son la misma emision -el boletin maritimo del DWD- y las dos
 * estan bien sintonizadas: medidas sobre sus fotos, las rayas del
 * oscilador caen encima de los tonos en las dos.
 *
 *     3712   marcas 918 y 1385 Hz   tonos 927 y 1374   -> basura
 *     3382   marcas 2438 y 2904     tonos 2438 y 2904  -> texto limpio
 *
 * Lo que las separa es el DESVANECIMIENTO, y la linea que no lo aguanta
 * es la decision del bit:
 *
 *     bit = (mark_mag >= space_mag) ? 1U : 0U;
 *
 * Un tono contra EL OTRO. Es exactamente la misma piedra que tenia
 * stanag_det.c hasta el 06/10 por la mañana: en onda corta la ionosfera
 * desvanece los dos tonos por separado y uno puede llegar 10 dB mas flojo,
 * asi que esa comparacion acaba decidiendo por cual se desvanece y no por
 * cual se ha transmitido.
 *
 * Este banco mete la grabacion por rtty_process(), que es la MISMA funcion
 * que corre en la radio, y cuenta cuanto del boletin sale.
 *
 *   make rttyaire FIRMWARE=... && ./out/rttyaire fichero.f32 mark space baud
 */
#include "rtty.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    FILE *f;
    float buf[RTTY_BLOCK_SAMPLES];
    size_t n;
    char texto[8192];
    unsigned nt = 0U;
    char c;
    float mark = 927.0f, space = 1374.0f, baud = 50.0f;

    if (argc < 2) { printf("uso: rttyaire <f32 12kHz> [mark] [space] [baud]\n"); return 1; }
    if (argc > 2) mark  = (float)atof(argv[2]);
    if (argc > 3) space = (float)atof(argv[3]);
    if (argc > 4) baud  = (float)atof(argv[4]);

    f = fopen(argv[1], "rb");
    if (!f) { printf("no puedo abrir %s\n", argv[1]); return 1; }

    rtty_init();
    rtty_set_enabled(1U);
    rtty_set_mark_space_hz(mark, space);
    rtty_set_baud(baud);

    while ((n = fread(buf, sizeof buf[0], RTTY_BLOCK_SAMPLES, f)) == RTTY_BLOCK_SAMPLES) {
        rtty_process(buf, RTTY_BLOCK_SAMPLES);
        while (rtty_get_char(&c)) {
            if (nt < sizeof texto - 1U) { texto[nt++] = c; }
        }
    }
    fclose(f);
    texto[nt] = '\0';

    printf("marca %.1f  espacio %.1f  %.2f Bd   ->  %u caracteres\n",
           (double)mark, (double)space, (double)baud, nt);
    printf("----------------------------------------------------------------\n");
    {
        unsigned i;
        for (i = 0U; i < nt; i++) {
            char q = texto[i];
            putchar((q == '\r') ? '\n' : ((q >= 32 && q < 127) || q == '\n') ? q : '.');
        }
    }
    printf("\n----------------------------------------------------------------\n");
    return 0;
}
