/*
 * EL IDENT, CONTRA UN AUDIO DE VERDAD.
 *
 * sim/identtest.c prueba la TABLA con medidas inventadas, que es lo que hay
 * que hacer con una funcion pura. Esto es lo otro: mete una grabacion por
 * la misma puerta que el audio de la radio -ident_modo_mete(), a 12 kHz- y
 * enseña lo que la radio diria.
 *
 * Sirve para dos cosas distintas y las dos hacen falta:
 *
 *  - contestar "que es esto" con el instrumento de la radio y no a ojo.
 *  - convertir una grabacion en una fila COMPROBADA. La regla de ident.h
 *    dice que una firma solo entra con su numero medido; esto es donde se
 *    mide.
 *
 * El audio entra como float de 32 bits, mono, 12000 Hz, que es exactamente
 * lo que le llega a ident_modo_mete() dentro de la radio:
 *
 *   ffmpeg -i loquesea.ogg -ac 1 -ar 12000 -f f32le salida.f32
 */
#include "ident.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    FILE *f;
    float buf[256];
    size_t n;
    unsigned paso = 0U, bloques = 0U;
    stanag_det_t m;
    ident_cad_t c;
    uint8_t cand[8];
    uint16_t k, nn, total;
    const ident_firma_t *t = ident_firmas(&total);

    if (argc < 2) {
        printf("uso: identaire <fichero.f32 mono 12 kHz>\n");
        return 1;
    }
    f = fopen(argv[1], "rb");
    if (!f) { printf("no puedo abrir %s\n", argv[1]); return 1; }

    if (!ident_modo_start()) { printf("no arranca\n"); return 1; }

    while ((n = fread(buf, sizeof buf[0], 256U, f)) > 0U) {
        ident_modo_mete(buf, (uint16_t)n);
        bloques++;
        if (++paso >= 4U) { paso = 0U; ident_modo_poll(); }
    }
    fclose(f);
    ident_modo_poll();

    ident_medidas(&m);
    ident_cadencia(&c);

    printf("\n%s  (%.2f s)\n\n", argv[1], (double)bloques * 256.0 / 12000.0);
    printf("  LO QUE MIDE EL MOTOR\n");
    printf("    ancho -3 dB      %u Hz\n", (unsigned)m.ancho3_hz);
    printf("    ancho -10 dB     %u Hz\n", (unsigned)m.ancho10_hz);
    printf("    ancho -20 dB     %u Hz\n", (unsigned)m.ancho20_hz);
    printf("    centro           %u Hz\n", (unsigned)m.centro_hz);
    printf("    pico en +-8 Hz   %u %%\n", (unsigned)m.pico_pc);
    if (m.hay_tonos) {
        printf("    dos tonos        %u y %u Hz, salto %u\n",
               (unsigned)m.tono_bajo, (unsigned)m.tono_alto, (unsigned)m.sep_hz);
    } else {
        printf("    dos tonos        no\n");
    }
    printf("    velocidad        %u,%u Bd\n",
           (unsigned)(m.baudios_x10 / 10U), (unsigned)(m.baudios_x10 % 10U));
    printf("    activo           %u %%\n", (unsigned)m.ciclo_pc);
    if (m.simb_x10) {
        printf("    simbolo          %u,%u Bd, ~%u tonos (calidad %u%%)\n",
               (unsigned)(m.simb_x10 / 10U), (unsigned)(m.simb_x10 % 10U),
               (unsigned)m.tonos, (unsigned)m.simb_q);
    } else {
        printf("    simbolo          no se le ve ritmo de simbolo\n");
    }
    if (m.cr7_pc) {
        printf("    alfabeto 7 bits  %u %% de grupos con 4 unos, a %u,%u Bd"
               "   (azar 27 %%)\n",
               (unsigned)m.cr7_pc,
               (unsigned)(m.cr7_bd_x10 / 10U), (unsigned)(m.cr7_bd_x10 % 10U));
    } else {
        printf("    alfabeto 7 bits  sin medir\n");
    }
    if (m.vale) {
        printf("    trama            %u,%02u ms (calidad %u%%, %u picos)\n",
               (unsigned)(m.periodo_us / 100U), (unsigned)(m.periodo_us % 100U),
               (unsigned)m.calidad, (unsigned)m.picos);
    } else {
        printf("    trama            no se repite\n");
    }
    if (c.vale) {
        printf("    ciclo            %u,%u s, ocupa %u%% (firmeza %u%%)\n",
               (unsigned)(c.periodo_ds / 10U), (unsigned)(c.periodo_ds % 10U),
               (unsigned)c.ocupa_pc, (unsigned)c.firmeza);
    } else {
        printf("    ciclo            no (solo %u s de escucha)\n",
               (unsigned)c.segundos);
    }

    /*
     * ESTO SE IMPRIMIA A MANO Y POR ESO MENTIA - 05/10/2026.
     *
     * El banco llamaba a ident_casa() y se escribia SU PROPIA version de
     * lo que diria la radio: nombre, "(?)" y nota. Mientras la pantalla no
     * hiciera nada mas, cuadraba. El dia que la pantalla aprendio a decir
     * "Sin estructura medible" cuando lo unico que cuadra es el ancho,
     * esto siguio enseñando el nombre a secas - o sea que el banco decia
     * que el fallo seguia ahi cuando ya estaba arreglado.
     *
     * Es la misma piedra de copias_check.py y por el mismo motivo: un
     * banco que reimplementa lo que mide, mide su reimplementacion. Ahora
     * se piden LAS LINEAS DE VERDAD, las que pinta la radio.
     */
    printf("\n  LO QUE DIRIA LA RADIO\n");
    {
        uint8_t nl = ident_modo_lineas(), li;
        for (li = 0U; li < nl; li++) {
            const char *s = ident_modo_linea(li);
            if (s && *s) { printf("    %s\n", s); }
        }
    }
    (void)cand; (void)nn; (void)k; (void)t;
    printf("\n");

    ident_modo_stop();
    return 0;
}
