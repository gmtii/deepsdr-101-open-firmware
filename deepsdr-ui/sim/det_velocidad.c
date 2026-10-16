/*
 * LA VELOCIDAD QUE MIDE EL DETECTOR, CONTRA LA QUE ES.
 *
 * Pedido por el usuario, 07/10/2026.
 *
 * Contestando eso salio un fallo que no se buscaba: el medidor de velocidad
 * del identificador se equivoca, y mucho, sobre señales cifradas.
 *
 *   grabacion de 8992 kHz del usuario   dijo  24,2 Bd   (la verdad: 75,00)
 *   fsk4481_5339.wav                  dijo 616,5 Bd   (la verdad: 74,5)
 *
 * Y con la velocidad mal, la fila de "STANAG 4481 FSK (KG-84)" -que pide
 * entre 70 y 80 baudios- no cuadra, y la pantalla se cae a "CW (Morse)".
 *
 * POR QUE FALLA. Mide LA RACHA MAS CORTA QUE SE REPITE, o sea cada cuanto
 * cambia de tono. En una alternancia limpia eso ES la velocidad; en datos
 * cifrados NO, porque las rachas duran varios simbolos y su reparto es
 * geometrico. Lee bajo cuando las rachas son largas y alto cuando se cuela un
 * submultiplo o el ruido de un desvanecimiento.
 *
 * LA VERDAD DE CADA GRABACION esta medida aparte, por un camino que NO
 * comparte nada con el firmware: se busca a que velocidad caen TODOS los
 * instantes de transicion sobre una misma rejilla de fase, y se mira con que
 * FIRMEZA encajan. La firmeza es la que dice si la medida vale:
 *
 *     sitorb_tfc.wav        100,00 Bd   firmeza 0,976   <- indiscutible
 *     navtex8424.wav        100,00 Bd   firmeza 0,800   <- indiscutible
 *     stanag4481_fsk.wav     75,00 Bd   firmeza 0,486   <- buena
 *     fsk4481_5339.wav      150,00 Bd   firmeza 0,060   <- NO VALE
 *
 * *** Y ESO ULTIMO ME CORRIGE A MI. *** Dije que el detector se equivocaba en
 * 5339 (616,5 Bd contra "74,5"). El 74,5 me lo saque del estandar, no de una
 * medida. Medido, mi propio metodo tampoco engancha ahi: firmeza 0,060, o sea
 * nada. La razon esta en la propia grabacion -"activo 49%", son rafagas con
 * huecos- y en los huecos el discriminador saca transiciones de ruido que no
 * caen en ninguna rejilla. Mi metodo necesita silenciador y no lo tiene.
 *
 * Asi que 5339 NO entra como referencia: con esa grabacion no se puede decir
 * quien se equivoca. Lo que ident.c documenta -74,7 Bd medidos por la radio
 * EN ANTENA sobre esa misma estacion- se midio sobre la señal de verdad, no
 * sobre esta captura por el altavoz de un movil.
 *
 * LO QUE SI QUEDA DEMOSTRADO, y basta para que haya que arreglarlo:
 *
 *     navtex8424.wav   es 100,0 Bd (firmeza 0,80) y el detector dice 610,8
 *     sitorb_tfc.wav   es 100,0 Bd (firmeza 0,98) y el detector dice 103,3
 *
 * La MISMA velocidad y el MISMO salto de 200 Hz, en dos grabaciones limpias:
 * una bien y la otra con un 511 % de error. Eso no es la grabacion, es el
 * medidor.
 *
 * POR ESO ESTE BANCO ESTA EN `make comprueba` DESDE EL 08/10/2026: hoy saldria en rojo
 * a proposito. Es la vara de medir del arreglo, y entra en comprueba el dia
 * que el arreglo pase las cuatro. Y el arreglo NO va a ser cambiar un
 * estimador por el mio sin mas: el mio ya ha fallado una de las cuatro.
 */
#include "stanag_det.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-40s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

static float trabajo[STANAG_DET_FLOATS];

static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static float *lee_wav(const char *ruta, uint32_t *n_out, uint32_t *hz_out)
{
    FILE *f = fopen(ruta, "rb");
    uint8_t cab[12]; uint16_t canales = 1U, bits = 16U; uint32_t hz = 0UL;
    if (!f) { return 0; }
    if (fread(cab, 1, 12, f) != 12U) { fclose(f); return 0; }
    for (;;) {
        uint8_t ch[8]; uint32_t len;
        if (fread(ch, 1, 8, f) != 8U) { break; }
        len = le32(&ch[4]);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fm[16]; uint32_t q = (len < 16U) ? len : 16U;
            if (fread(fm, 1, q, f) != q) { break; }
            canales = le16(&fm[2]); hz = le32(&fm[4]); bits = le16(&fm[14]);
            if (len > 16U) { fseek(f, (long)(len - 16U), SEEK_CUR); }
        } else if (memcmp(ch, "data", 4) == 0) {
            uint32_t m = len / (uint32_t)((bits / 8U) * canales), k;
            int16_t *raw = (int16_t *)malloc(len); float *x;
            if (!raw) { break; }
            if (fread(raw, 1, len, f) != len) { free(raw); break; }
            x = (float *)malloc((size_t)m * sizeof *x);
            for (k = 0U; k < m; k++) { x[k] = (float)raw[k * canales] / 32768.0f; }
            free(raw); *n_out = m; *hz_out = hz; fclose(f); return x;
        } else { fseek(f, (long)len, SEEK_CUR); }
        if (len & 1U) { fseek(f, 1L, SEEK_CUR); }
    }
    fclose(f); return 0;
}

typedef struct { const char *fich; double bd; const char *que; } caso_t;

/* La verdad de cada una, medida aparte con el ajuste de rejilla de fase. */
static const caso_t k_casos[] = {
    /* fichero, verdad medida, firmeza de esa medida */
    { "out/muestras/sitorb_tfc.wav",    100.0,  "SITOR-B 200 Hz   (firmeza 0,98)" },
    { "out/muestras/navtex8424.wav",    100.0,  "NAVTEX  200 Hz   (firmeza 0,80)" },
    { "out/muestras/stanag4481_fsk.wav", 75.0,  "4481 FSK 834 Hz  (firmeza 0,49)" },
    /* 5339 NO entra: mi propia medida da firmeza 0,060 sobre ella, o sea que
     * no se puede usar de referencia. Ver la cabecera. */
};
#define N_CASOS (sizeof k_casos / sizeof k_casos[0])

int main(void)
{
    unsigned c;
    printf("\nLA VELOCIDAD QUE MIDE EL DETECTOR\n");
    printf("=================================\n\n");
    printf("  %-30s %8s %10s %9s\n", "grabacion", "es", "mide", "error");
    printf("  %-30s %8s %10s %9s\n", "------------------------------",
           "--------", "----------", "---------");

    for (c = 0U; c < N_CASOS; c++) {
        uint32_t n = 0U, hz = 0U, i;
        float *x = lee_wav(k_casos[c].fich, &n, &hz);
        stanag_det_t d; double medido = 0.0, err;
        const char *b = strrchr(k_casos[c].fich, '/'); b = b ? b + 1 : k_casos[c].fich;
        char det[96];

        if (!x) { printf("  no puedo abrir %s\n", k_casos[c].fich); fallos++; continue; }
        /*
         * LA FRECUENCIA DE MUESTREO SE LEIA Y NO SE MIRABA - 11/10/2026.
         *
         * Pedido por el usuario.
         *
         * lee_wav() devuelve la frecuencia por `hz` y aqui no se usaba en
         * ninguna linea mas: el audio se metia crudo por stanag_det_mete(),
         * cuya cabecera dice "audio de banda lateral A 12 kHz". Toda la
         * tabla de errores de este banco -incluido el 511 % que es su
         * conclusion- colgaba de una precondicion que nadie comprobaba. Y
         * el banco hermano, sim/stanag_aire.c, usa el MISMO lee_wav() sobre
         * los MISMOS ficheros y si remuestrea.
         *
         * Hoy las cuatro grabaciones son de 12 kHz y no muerde. Pero "hoy
         * no muerde" no es una comprobacion: el dia que alguien deje caer
         * ahi una WAV de 48 kHz, este banco mediria velocidades cuatro
         * veces mas altas y las presentaria como medidas.
         */
        if (hz != 12000U) {
            printf("  %s esta a %u Hz y stanag_det quiere 12.000\n", b, hz);
            free(x); fallos++; continue;
        }
        if (!stanag_det_init(trabajo, STANAG_DET_FLOATS)) { free(x); fallos++; continue; }
        for (i = 0U; i + 256U <= n; i += 256U) {
            stanag_det_mete(&x[i], 256U);
            if (stanag_det_paso(&d)) {
                if (d.baudios_x10 != 0U) { medido = (double)d.baudios_x10 / 10.0; }
            }
        }
        free(x);
        err = (medido > 0.0) ? (100.0 * fabs(medido - k_casos[c].bd) / k_casos[c].bd) : 999.0;
        printf("  %-30s %7.1f  %9.1f  %7.0f %%   %s\n", b, k_casos[c].bd, medido, err,
               k_casos[c].que);
        snprintf(det, sizeof det, "(es %.1f, mide %.1f)", k_casos[c].bd, medido);
        di("   la velocidad sale con menos del 5% de error", err < 5.0, det);
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}
