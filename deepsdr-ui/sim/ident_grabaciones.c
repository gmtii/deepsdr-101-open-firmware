/*
 * EL IDENT CONTRA LAS GRABACIONES, CONTANDO VENTANAS. 10/10/2026.
 *
 * Pedido por el usuario.
 *
 * Y mirando eso salio algo mas gordo que una fila mal puesta.
 *
 * LO QUE PASABA. ident_modo_poll() publicaba `s_m = r`, o sea LA ULTIMA
 * VENTANA, y encima solo le corregia la separacion con una mediana -eso se
 * arreglo el 06/10 porque "una separacion que baila es un nombre que
 * baila"-. La BANDERA de "hay dos tonos" y la VELOCIDAD seguian crudas.
 *
 * Medido con un instrumento aparte, ventana a ventana:
 *
 *   stanag4481_fsk.mp3   837 ventanas
 *       dos tonos en el 97%, mediana 839 Hz
 *       velocidad en el 91%, mediana 74,9 Bd
 *       ULTIMA VENTANA: sin tonos y sin velocidad
 *
 * La fila de 4481 FSK pide 780..920 Hz y 70..80 Bd. La radio tenia la
 * respuesta buena NUEVE DE CADA DIEZ VENTANAS y publicaba la decima.
 *
 * POR QUE ESTE BANCO CUENTA VENTANAS Y NO MIRA EL FINAL. Porque mirar el
 * final es exactamente el fallo que se acaba de arreglar: una grabacion que
 * se desvanece al acabar deja las ultimas ventanas sin señal, y juzgar por
 * ahi es juzgar por el silencio. Lo que importa es QUE DICE LA RADIO
 * MIENTRAS LA SEÑAL ESTA, que es cuando el usuario la mira.
 *
 * LOS MINIMOS NO SON REDONDOS NI SALEN DE UNA NORMA: son lo medido hoy,
 * menos un margen. Un banco cuyos numeros se eligen para que pase no mide
 * nada. Lo medido, antes y despues del arreglo:
 *
 *                            antes   despues
 *     stanag4481_fsk          87%      90%
 *     fsk4481_5339             0%      37%     <- 1 ventana de 380
 *     sitorb_tfc              82%      82%
 *     navtex8424              88%      87%
 *     rtty3712                70%      82%
 *     fsk200_16911            94%      94%
 *     cw4xz_4330              72%      72%     <- CW no se mueve
 *     stanag4285_tfc          94%      94%
 *     stanag4529              96%      96%
 *
 * El 37% de 5339 no es un aprobado brillante y no se disfraza de uno: esa
 * grabacion son rafagas con huecos -"activo 49%"- y en los huecos no hay
 * nada que identificar. Lo que importa es que antes era UNA ventana de 380.
 *
 * Y cw4xz_4330 esta aqui a proposito, con su 72% de CW: es el seguro de
 * que arreglar las FSK no se lleve por delante el Morse. Cualquier cambio
 * futuro que mejore las FSK empujando a todo el mundo hacia "dos tonos"
 * tiene que pagarlo aqui.
 *
 *   make identgrab FIRMWARE=...
 */
#include "ident.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fallos;

static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

/* Lee .f32 (float crudo a 12 kHz) o .wav de 16 bits. Las dos cosas estan
 * en sim/muestras: unas vienen asi del repositorio y otras las fabrica el
 * makefile desde los mp3. */
static float *lee(const char *ruta, long *n_out)
{
    size_t l = strlen(ruta);
    FILE *f = fopen(ruta, "rb");

    if (!f) { return 0; }
    if (l > 4U && strcmp(ruta + l - 4, ".f32") == 0) {
        long m; float *x;
        fseek(f, 0, SEEK_END); m = ftell(f) / 4; fseek(f, 0, SEEK_SET);
        x = (float *)malloc((size_t)m * sizeof *x);
        if (!x || fread(x, 4, (size_t)m, f) != (size_t)m) { free(x); fclose(f); return 0; }
        fclose(f); *n_out = m; return x;
    }
    {
        uint8_t cab[12]; uint16_t canales = 1U, bits = 16U;
        if (fread(cab, 1, 12, f) != 12U) { fclose(f); return 0; }
        for (;;) {
            uint8_t ch[8]; uint32_t len;
            if (fread(ch, 1, 8, f) != 8U) { break; }
            len = le32(&ch[4]);
            if (memcmp(ch, "fmt ", 4) == 0) {
                uint8_t fm[16]; uint32_t q = (len < 16U) ? len : 16U;
                if (fread(fm, 1, q, f) != q) { break; }
                canales = le16(&fm[2]); bits = le16(&fm[14]);
                if (len > 16U) { fseek(f, (long)(len - 16U), SEEK_CUR); }
            } else if (memcmp(ch, "data", 4) == 0) {
                uint32_t m = len / (uint32_t)((bits / 8U) * canales), k;
                int16_t *raw = (int16_t *)malloc(len); float *x;
                if (!raw) { break; }
                if (fread(raw, 1, len, f) != len) { free(raw); break; }
                x = (float *)malloc((size_t)m * sizeof *x);
                for (k = 0U; k < m; k++) { x[k] = (float)raw[k * canales] / 32768.0f; }
                free(raw); fclose(f); *n_out = (long)m; return x;
            } else { fseek(f, (long)len, SEEK_CUR); }
            if (len & 1U) { fseek(f, 1L, SEEK_CUR); }
        }
        fclose(f); return 0;
    }
}

typedef struct {
    const char *fich;
    const char *espera;   /* trozo que tiene que salir en el renglon de arriba */
    int         minimo;   /* % de ventanas, medido hoy menos margen */
    int         medido;   /* lo que salio hoy, para que se vea la holgura */
} caso_t;

static const caso_t k_casos[] = {
    { "out/muestras/stanag4481_fsk.wav", "STANAG 4481 FSK",    80, 90 },
    { "out/muestras/fsk4481_5339.wav",   "STANAG 4481 FSK",    25, 37 },
    { "out/muestras/sitorb_tfc.wav",     "SITOR-B",            70, 82 },
    { "out/muestras/navtex8424.wav",     "NAVTEX",             75, 87 },
    { "out/muestras/stanag4285_tfc.wav", "4285",               85, 94 },
    { "out/muestras/stanag4529.wav",     "4529",               85, 96 },
    { "sim/muestras/rtty3712.f32",       "RTTY comercial",     70, 82 },
    { "sim/muestras/fsk200_16911.f32",   "FSK 50 Bd / 200 Hz", 85, 94 },
    { "sim/muestras/cw4xz_4330.f32",     "CW (Morse)",         60, 72 }
};
#define N_CASOS (sizeof k_casos / sizeof k_casos[0])

int main(void)
{
    unsigned c;

    printf("\nEL IDENT CONTRA LAS GRABACIONES, VENTANA A VENTANA\n");
    printf("==================================================\n\n");
    printf("  %-28s %-20s %8s %7s\n", "grabacion", "tiene que decir",
           "aciertos", "minimo");

    for (c = 0U; c < N_CASOS; c++) {
        long n = 0, i; float *x = lee(k_casos[c].fich, &n);
        long tot = 0, si = 0; unsigned paso = 0U; int pc;
        const char *b = strrchr(k_casos[c].fich, '/');
        b = b ? b + 1 : k_casos[c].fich;

        if (!x) {
            printf("  %-28s no puedo abrirla (¿falta ffmpeg?)\n", b);
            fallos++;
            continue;
        }
        /*
         * PARAR ANTES DE ARRANCAR, Y NO ES UN DETALLE DEL BANCO.
         *
         * ident_modo_start() empieza con `if (s_activo) return 1;`: si ya
         * estaba en marcha no vuelve a llamar a stanag_det_init(), asi que
         * el anillo del motor sigue lleno del AUDIO ANTERIOR. La primera
         * version de este banco no paraba entre grabaciones y 5339 salia
         * con 0 aciertos de 380, cuando en un proceso suyo saca 139. O
         * sea que el banco se estaba midiendo a si mismo.
         */
        ident_modo_stop();
        if (!ident_modo_start()) { printf("  no arranca\n"); free(x); fallos++; continue; }
        for (i = 0; i + 256 <= n; i += 256) {
            ident_modo_mete(&x[i], 256U);
            if (++paso >= 4U) {
                paso = 0U;
                ident_modo_poll();
                tot++;
                if (strstr(ident_modo_linea(0), k_casos[c].espera)) { si++; }
            }
        }
        free(x);
        pc = (tot > 0) ? (int)((100 * si + tot / 2) / tot) : 0;
        printf("  %-28s %-20s %4ld/%-4ld %3d%%  %s  (hoy %d%%)\n",
               b, k_casos[c].espera, si, tot, k_casos[c].minimo,
               (pc >= k_casos[c].minimo) ? "ok        " : "*** MAL ***",
               pc);
        if (pc < k_casos[c].minimo) { fallos++; }
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}
