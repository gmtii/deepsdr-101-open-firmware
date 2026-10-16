/*
 * EL IDENT CON VARIAS SEÑALES EN EL PASO DE BANDA. 10/10/2026.
 *
 * Pedido por el usuario.
 *
 * Y el "aprs?" no era una suposicion suya: ES LO QUE LE DIJO LA RADIO. El
 * Ident contesto "APRS / paquete AFSK 1200, 70%" sobre tres RTTY de
 * aficionado. Esta grabacion es la suya, 12 s del tramo donde se oyen las
 * tres a la vez.
 *
 * LO QUE PASABA, QUE NO ES UN FALLO DE LA TABLA
 * ---------------------------------------------
 * El detector de dos tonos de stanag_det.c cogia LA RAYA MAS ALTA y luego
 * LA MAS ALTA DE TODO LO QUE ESTUVIERA A MAS DE 120 Hz de ella. Con una
 * sola señal eso son sus dos tonos y funciona. Con tres:
 *
 *     canal 1    372 / 542 Hz      salto 170
 *     canal 2    855 / 1028 Hz     salto 173
 *     canal 3   2045 / 2224 Hz     salto 179
 *
 * cogia el 2227 de una y el 850 de OTRA, y declaraba un salto de 1019 Hz.
 * 1019 cae cerca del 1200/2200 del APRS, asi que la tabla -que estaba
 * bien- contestaba APRS con un 70%.
 *
 * Medido: dandole el canal 3 aislado, la MISMA tabla y el MISMO motor
 * dicen "RTTY de aficionado 70%, 45,5 Bd, salto 174". O sea que lo que
 * fallaba era el paso de antes, igual que ya paso el 02/10 con el
 * detector de dos tonos y las PSK anchas.
 *
 * QUE SE EXIGE AQUI
 * -----------------
 * No que acierte el nombre -con tres señales encima eso es pedir mucho-
 * sino que NO SE INVENTE UNO. Las dos cosas por separado:
 *
 *   1. que cuente las señales que hay, y diga que hay mas de una;
 *   2. que el salto que publique sea uno de los tres de verdad (~170) y
 *      no uno fabricado juntando dos señales distintas.
 *
 * Un "hay 3 señales, estrecha el filtro" sirve. Un "APRS 70%" no: manda al
 * usuario a poner un modo que no va a decodificar nada, y encima con la
 * seguridad de un 70%.
 */
#include "ident.h"
#include "stanag_det.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FS      12000.0f
#define BLOQUE  256U

static int fallos;
static void di(const char *q, int ok, const char *det)
{
    printf("    %-52s %s%s%s\n", q, ok ? "ok" : "*** MAL ***",
           det ? "   " : "", det ? det : "");
    if (!ok) { fallos++; }
}

/*
 * Pasa un .f32 de 12 kHz por LA MISMA PUERTA QUE EL AUDIO DE LA RADIO
 * -ident_modo_mete()-, no por el detector a pelo: asi se mide lo que el
 * usuario vio en pantalla y no una version de laboratorio.
 */
static int corre(const char *ruta, stanag_det_t *out, ident_cad_t *cad)
{
    FILE *f = fopen(ruta, "rb");
    float buf[BLOQUE];
    size_t n;
    unsigned paso = 0U, bloques = 0U;

    if (f == NULL) { printf("    no encuentro %s\n", ruta); return 0; }
    if (!ident_modo_start()) { printf("    no arranca el modo\n"); fclose(f); return 0; }
    while ((n = fread(buf, sizeof buf[0], BLOQUE, f)) > 0U) {
        ident_modo_mete(buf, (uint16_t)n);
        bloques++;
        if (++paso >= 4U) { paso = 0U; ident_modo_poll(); }
    }
    fclose(f);
    ident_modo_poll();
    ident_medidas(out);
    ident_cadencia(cad);
    return (int)bloques;
}

int main(void)
{
    stanag_det_t m;
    ident_cad_t cad;
    int medidas;

    printf("\nIDENT CON VARIAS SEÑALES A LA VEZ\n");
    printf("  tres RTTY de aficionado en 7 MHz, grabados por el usuario\n\n");

    medidas = corre("sim/muestras/rtty_3x_7mhz.f32", &m, &cad);
    if (medidas == 0) { printf("  sin medidas\n"); return 2; }

    printf("  LO QUE MIDE EL MOTOR (%d bloques, %.1f s)\n",
           medidas, medidas * (double)BLOQUE / (double)FS);
    printf("    ancho -3 dB      %u Hz\n", m.ancho3_hz);
    printf("    centro           %u Hz\n", m.centro_hz);
    printf("    hay_tonos        %u\n", m.hay_tonos);
    printf("    tonos            %u y %u, salto %u Hz\n",
           m.tono_bajo, m.tono_alto, m.sep_hz);
    printf("    señales vistas   %u\n", m.n_senales);
    printf("    velocidad        %u,%u Bd\n", m.baudios_x10 / 10U, m.baudios_x10 % 10U);
    printf("\n");

    /* 1. Que vea que hay mas de una. */
    {
        char d[40];
        sprintf(d, "(dice %u)", m.n_senales);
        di("ve que hay mas de una señal", m.n_senales >= 2U, d);
        /*
         * DICE DOS, Y SON TRES. No se toca el umbral para que salga tres:
         * la grabacion no da para mas y el numero esta medido.
         *
         * De 113 ventanas con tonos, la cuenta sale 2 en 92, 1 en 45 y 0 en
         * 3. Nunca 3. El motivo es el desvanecimiento: para contar una raya
         * hay que verla sobresalir 9 dB de su propio ruido (TONO_REALCE), y
         * en 7 MHz las tres estaciones no estan arriba a la vez casi nunca.
         * Ademas la grabacion viene en Opus a 31 kbit/s, que es un codec de
         * VOZ: se come lo que no sea tonal y deja las rayas flojas peor de
         * lo que estaban en antena.
         *
         * Y para lo que sirve esto da igual: lo que la pantalla tiene que
         * decir es "hay varias, estrecha el filtro", y eso con 2 ya se dice.
         * Exigir 3 seria exigirle al banco que adivine lo que la señal no
         * trae.
         */
        di("y la cuenta no se pasa de largo", m.n_senales <= 4U, d);
    }

    /* 2. Que el salto sea de verdad y no fabricado. */
    {
        char d[40];
        sprintf(d, "(salto %u Hz)", m.sep_hz);
        di("*** el salto NO es uno inventado entre dos señales ***",
           !m.hay_tonos || m.sep_hz < 400U, d);
        di("y si publica tonos, son los de un RTTY (150..200 Hz)",
           !m.hay_tonos || (m.sep_hz >= 150U && m.sep_hz <= 200U), d);
    }

    /* 3. Y lo que de verdad se lee desde fuera: que NO diga APRS. */
    {
        uint16_t n = 0U;
        const ident_firma_t *fs = ident_firmas(&n);
        uint16_t i, cuantas;
        uint8_t idx[4];

        cuantas = ident_casa(&m, &cad, idx, 4U);
        printf("\n  LO QUE DIRIA LA RADIO (%u filas)\n", cuantas);
        for (i = 0U; i < cuantas; i++) {
            printf("    %s\n", ident_nombre(&fs[idx[i]]));
        }
        {
            int aprs = 0;
            for (i = 0U; i < cuantas; i++) {
                if (strstr(ident_nombre(&fs[idx[i]]), "APRS") != 0) { aprs = 1; }
            }
            di("*** no dice APRS sobre tres RTTY ***", !aprs, 0);
        }
    }

    printf("\n  %s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
