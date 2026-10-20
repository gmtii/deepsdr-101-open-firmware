/*
 * EL AIRE DE VERDAD, POR EL FIRMWARE DE VERDAD - 14/10/2026
 *
 * Este es el banco que faltaba desde el principio. Los otros veintitantos
 * de HFDL prueban piezas, y hfdl_extremo.c monta una rafaga sintetica y la
 * mete entera; pero una rafaga sintetica la hace el mismo que escribio el
 * decodificador, asi que comparten los errores de interpretacion. Si yo
 * entendi mal el protocolo, mi transmisor lo entiende mal igual y los dos
 * se ponen de acuerdo en verde.
 *
 * Grabaciones reales, no. Vienen de una antena y de aviones que no saben
 * que existimos.
 *
 * COMO ENTRA EL AUDIO. La radio le da a HFDL la pareja I/Q a 12 kHz
 * justo despues del diezmado, con la subportadora en +1440 Hz (ver
 * demod_am.c, "HFDL SE ENGANCHA AQUI"). Una grabacion de audio en USB es
 * real, asi que la pareja equivalente es su señal analitica: I = la
 * grabacion, Q = su transformada de Hilbert. Esa conversion se hace fuera
 * -los .iq.wav de sim/aire/ la llevan ya hecha, en dos canales- para que
 * este banco no tenga que llevar un Hilbert dentro y pueda ser lo que
 * tiene que ser: leer y meter.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "hfdl_modo.h"
#include "hfdl_payload_decode.h"
#include "hfdl_hfnpdu.h"

/* Detalle ráfaga a ráfaga: solo para las grabaciones largas, que son las
 * que tienen bastantes como para ver un patron. */
static int g_detalle;
static int g_detalle_carga = 1;
static const uint8_t *g_vista;

static uint32_t g_dg_t[64];
#if HFDL_DIAG
/* Ver hfdl_pdu.c: cada LPDU que se extrae, en crudo. */
void hfdl_dg_cabecera_mala(uint32_t n, uint32_t nok)
{
    if (nok != 0u) {
        printf("        >>> CABECERA MALA pero %u de %u LPDU con su CRC BUENO\n", nok, n);
    }
}
void hfdl_dg_mpdu(const uint8_t *b, uint32_t n, uint32_t hl, int ok, int dir)
{
    uint32_t z;
    printf("        MPDU  %s  cabecera %u de %u bytes  crc %s :",
           dir ? "SUBIDA " : "bajada ", hl, n, ok ? "OK " : "MAL");
    for (z = 0u; z < hl + 2u && z < 20u; z++) { printf(" %02X", b[z]); }
    printf("\n");
}
void hfdl_dg_lpdu(const uint8_t *b, uint32_t n, int ok)
{
    uint32_t z;
    printf("        LPDU  tipo %02X  %2u bytes  crc %s :", b[0], n, ok ? "OK " : "MAL");
    for (z = 1u; z < n && z < 24u; z++) { printf(" %02X", b[z]); }
    if (n > 24u) { printf(" ..."); }
    printf("\n");
}
#endif
static int16_t *g_iq;
static uint32_t g_n;

/* Lee un wav PCM de 16 bits y dos canales. Sin libreria: la cabecera de
 * un wav que escribe scipy son 44 bytes y no hay mas misterio. */
static int lee_wav(const char *ruta, uint32_t *fs_out)
{
    FILE *f = fopen(ruta, "rb");
    unsigned char cab[44];
    long tam;
    if (!f) { return 0; }
    if (fread(cab, 1, 44, f) != 44) { fclose(f); return 0; }
    if (memcmp(cab, "RIFF", 4) != 0 || memcmp(cab + 8, "WAVE", 4) != 0) {
        fclose(f); return 0;
    }
    *fs_out = (uint32_t)cab[24] | ((uint32_t)cab[25] << 8) |
              ((uint32_t)cab[26] << 16) | ((uint32_t)cab[27] << 24);
    fseek(f, 0, SEEK_END); tam = ftell(f) - 44; fseek(f, 44, SEEK_SET);
    g_n = (uint32_t)(tam / 4);              /* 2 canales x 2 bytes */
    g_iq = (int16_t *)malloc((size_t)tam);
    if (!g_iq) { fclose(f); return 0; }
    if (fread(g_iq, 1, (size_t)tam, f) != (size_t)tam) { fclose(f); return 0; }
    fclose(f);
    return 1;
}

static int una(const char *nombre)
{
    uint32_t fs = 0u, i, b;
    float bi[32], bq[32];
    uint32_t lineas, aviones, pos = 0u;
    char ruta[256];

    snprintf(ruta, sizeof ruta, "sim/aire/%s.iq.wav", nombre);
    if (!lee_wav(ruta, &fs)) {
        printf("  %-18s (no se pudo leer %s)\n", nombre, ruta);
        return 0;
    }
    if (fs != 12000u) {
        printf("  %-18s (va a %u Hz, hace falta 12000)\n", nombre, fs);
        free(g_iq); return 0;
    }

    hfdl_modo_stop();
    if (!hfdl_modo_start()) { printf("  (no arranca el modo)\n"); free(g_iq); return 0; }
    if (g_detalle) { printf("  %s:\n", nombre); }

    /* Bloques de 32 muestras, que es lo que le llega en la radio desde el
     * diezmador (ver demod_am.c). Y poll() entre bloques, que es donde
     * corre el Viterbi: si se mete todo el audio y se llama a poll() al
     * final, solo se remata la ultima rafaga y las demas se pierden -que
     * es justo lo que hace la radio mal si alguien toca ese reparto. */
    {
        uint32_t tot_ant = 0u, bue_ant = 0u, n_raf = 0u, rec_ant = 0u, lin_ant = 0u;
        uint32_t dg_ant = 0u;
        for (i = 0; i + 32u <= g_n; i += 32u) {
            for (b = 0; b < 32u; b++) {
                bi[b] = (float)g_iq[(i + b) * 2u]     / 32768.0f;
                bq[b] = (float)g_iq[(i + b) * 2u + 1u] / 32768.0f;
            }
            hfdl_modo_mete(bi, bq, 32u);
            hfdl_modo_poll();
            /* Una ráfaga acaba de rematarse: se apunta QUE paso con ella,
             * que es el dato que no se tenia. Un "12 ráfagas, 2 buenas"
             * no dice nada; "la 3 era 8-PSK con coherencia 0,41" si. */
#if HFDL_DIAG
            if (g_detalle && hfdl_modo_dg_n_lista() != dg_ant) {
                dg_ant = hfdl_modo_dg_n_lista();
                if (dg_ant <= 64u) { g_dg_t[dg_ant - 1u] = i; }
            }
#endif
            if (g_detalle && hfdl_modo_rechazadas() != rec_ant) {
                printf("        tirada     t=%5.1fs                  coherencia %.3f\n",
                       (double)i / 12000.0, (double)hfdl_modo_coherencia());
                rec_ant = hfdl_modo_rechazadas();
            }
            if (g_detalle_carga) {
                uint32_t cn = 0u; const uint8_t *cb = hfdl_modo_ultima_carga(&cn);
                if (cb != (const uint8_t *)0 && cb != g_vista) {
                    uint32_t z3; hfdl_hfnpdu_perf_data_t pd;
                    g_vista = cb;
                    printf("        CARGA %3u bytes:", cn);
                    for (z3 = 0; z3 < cn && z3 < 20u; z3++) { printf(" %02X", cb[z3]); }
                    printf("\n           -> parse_performance_data: %s",
                           hfdl_hfnpdu_parse_performance_data(cb, cn, &pd) ? "SI" : "NO");
                    if (cn < 47u) { printf("   (len %u < 47, el minimo)", cn); }
                    printf("\n");
                }
            }
            if (g_detalle && hfdl_modo_total() != tot_ant) {
                n_raf++;
                {
                    uint32_t li = hfdl_modo_lineas();
                    printf("        rafaga %2u  t=%5.1fs  giro %u  coh %.3f  %-9s  "
                           "renglones %u->%u %s\n",
                           n_raf, (double)i / 12000.0,
                           (unsigned)hfdl_payload_decode_arity(),
                           (double)hfdl_modo_coherencia(),
                           (hfdl_modo_buenas() != bue_ant) ? "CRC BUENO" : "crc malo",
                           lin_ant, li,
                           (li == lin_ant) ? "<- NO PINTA NADA" : "");
                    lin_ant = li;
                }
                {
                    uint32_t nb2 = 0u, z;
                    const uint8_t *cr = hfdl_modo_ultimo_crudo(&nb2);
                    uint32_t unos = 0u;
                    printf("                     %3u bytes:", nb2);
                    for (z = 0; z < nb2 && z < 10u; z++) { printf(" %02X", cr[z]); }
                    for (z = 0; z < nb2; z++) {
                        uint8_t v = cr[z];
                        while (v) { unos += (v & 1u); v >>= 1; }
                    }
                    if (nb2) {
                        printf("   (%u%% de unos)", (unsigned)((unos * 100u) / (nb2 * 8u)));
                    }
                    printf("\n");
                }
                tot_ant = hfdl_modo_total();
                bue_ant = hfdl_modo_buenas();
            }
        }
    }

    lineas = hfdl_modo_lineas();
    aviones = hfdl_modo_aviones();
    {
        uint32_t k; int32_t la, lo;
        for (k = 0; k < aviones; k++) {
            if (hfdl_modo_pos(k, &la, &lo)) {
                pos++;
                if (g_detalle) {
                    printf("        POSICION  %s  %.2f %c  %.2f %c\n",
                           hfdl_modo_linea(k),
                           (double)(la < 0 ? -la : la) / 10000.0, la < 0 ? 'S' : 'N',
                           (double)(lo < 0 ? -lo : lo) / 10000.0, lo < 0 ? 'W' : 'E');
                }
            }
        }
    }

    /* Y LO QUE LLEVA CADA AVION, en crudo. Es lo unico que contesta a
     * "¿por que no hay coordenadas?": o no viene ninguna, o viene en una
     * forma que no entendemos. Las dos cosas se ven aqui y en ningun
     * otro sitio. */
    {
        uint32_t z2;
        for (z2 = 0u; z2 < aviones; z2++) {
            const char *b = hfdl_modo_linea_bytes(z2);
            if (b != (const char *)0 && b[0] != '\0') {
                printf("        carga[%u]: %s\n", z2, (b[0] == '\x01') ? b + 1 : b);
            }
        }
    }

    {
        uint32_t a1 = 0u, a2 = 0u, m1 = 0u;
        hfdl_modo_embudo(&a1, &a2, &m1);
        printf("  %-18s %5.1f s   prekey %3u   embudo %4u>%3u>%3u   rafagas %3u  buenas %2u  "
               "tiradas %3u  sinrenglon %2u   renglones %2u  aviones %2u  posiciones %2u\n",
               nombre, (double)g_n / 12000.0, hfdl_modo_prekeys(), a1, a2, m1,
               hfdl_modo_total(), hfdl_modo_buenas(), hfdl_modo_rechazadas(),
               hfdl_modo_sin_renglon(), lineas, aviones, pos);
        /* Y POR GIRO, que es el dato que importa: la radio lleva 0 de 45
         * en 4-PSK y 8-PSK desde siempre. Si aqui un 4-PSK de aire real
         * sale bueno, el orden de los chips de este firmware es el que
         * pide la norma; si no sale ninguno, hay que mirarlo. */
        printf("                     %s\n", hfdl_modo_linea_crc());
#if HFDL_DIAG
        /* DE QUE ES EL "/n" DE LA CHAPA - 15/10/2026.
         * Sin repartir no se puede interpretar: el mismo numero puede ser
         * el decodificador rompiendo cabeceras o la frecuencia llena de
         * trafico de tierra, que es normal y no es un fallo. */
        if (hfdl_modo_sin_renglon() != 0u) {
            uint32_t cab = 0u, slp = 0u, sav = 0u;
            hfdl_modo_dg_sin_renglon(&cab, &slp, &sav);
            printf("                     sinrenglon: cabecera ilegible %u   sin LPDU %u"
                   "   bueno pero sin avion %u\n", cab, slp, sav);
        }
#endif
#if HFDL_DIAG
        { uint32_t mn=0,mx=0; hfdl_modo_edad_prekey(&mn,&mx);
          { uint32_t g = hfdl_modo_prekey_gap_min();
            printf("                     edad del pre-key: min %u max %u   sin armar %u   juntos: %u   MISMO TONO: %u   hueco minimo: %u\n",
                   mn, mx, hfdl_modo_sin_armar(), hfdl_modo_prekeys_juntos(),
                   hfdl_modo_prekeys_mismo_tono(),
                   (unsigned)(g == 0xFFFFFFFFu ? 0u : g)); } }
#endif
#if HFDL_DIAG
        printf("                     pre-key que enganchan %u   los que no, por maximo de A1:"
               " <0,15 %u | <0,25 %u | <0,36 %u | >=0,36 %u\n",
               hfdl_modo_dg_con_a1(), hfdl_modo_dg_banda(0), hfdl_modo_dg_banda(1),
               hfdl_modo_dg_banda(2), hfdl_modo_dg_banda(3));
        printf("                     los que SI, por maximo de A1:"
               " <0,15 %u | <0,25 %u | <0,36 %u | >=0,36 %u    coherencia del tono  si %.3f  no %.3f\n",
               hfdl_modo_dg_ok(0), hfdl_modo_dg_ok(1), hfdl_modo_dg_ok(2), hfdl_modo_dg_ok(3),
               (double)hfdl_modo_dg_coh(1), (double)hfdl_modo_dg_coh(0));
        printf("                     maximo medio de A1   si %.3f  no %.3f        |desvio| medio del tono   si %.1f Hz  no %.1f Hz\n",
               (double)hfdl_modo_dg_pico(1), (double)hfdl_modo_dg_pico(0),
               (double)hfdl_modo_dg_hz(1), (double)hfdl_modo_dg_hz(0));
        printf("                     A1 sobre MEDIO SIMBOLO   si %.3f  no %.3f    gana la media en %u de los que fallan\n",
               (double)hfdl_modo_dg_picoh(1), (double)hfdl_modo_dg_picoh(0),
               hfdl_modo_dg_gana_half());
        { uint32_t sq=0,su=0,ba=0; static const char *kn[6]={"invalido","logon-pide","logon-ok","logoff","DATOS","tipo raro"};
          uint32_t z5; hfdl_modo_dg_clases(&sq,&su,&ba);
          printf("                     clases:  squitter %u   subida %u   BAJADA %u    lpdu:", sq, su, ba);
          for (z5=0; z5<6u; z5++) { if (hfdl_modo_dg_lpdu(z5)) printf(" %s=%u", kn[z5], hfdl_modo_dg_lpdu(z5)); }
          printf("\n");
          { uint32_t na=0, np=0; hfdl_modo_dg_acars(&na,&np);
            printf("                     ACARS leidos %u   de ellos con coordenada %u\n", na, np); } }
        if (g_detalle) {
            uint32_t z4, nn = hfdl_modo_dg_n_lista();
            printf("                     tono a tono:\n");
            for (z4 = 0; z4 < nn; z4++) {
                float coh = 0.f, hz = 0.f, a1 = 0.f;
                uint32_t ok = hfdl_modo_dg_lista(z4, &coh, &hz, &a1);
                { uint32_t cual = 0u; float gg = hfdl_modo_dg_giro(z4, &cual);
                  printf("                       %2u  t=%6.1fs  coh %.3f  desvio %+7.1f Hz  dura %4u sim  A1 %.3f  "
                         "girada %.3f (a %u grados)  %s\n",
                         z4, (double)g_dg_t[z4] / 12000.0, (double)coh, (double)hz,
                         hfdl_modo_dg_dur(z4), (double)a1,
                         (double)gg, (unsigned)(cual * 45u), ok ? "ENGANCHA" : "no"); }
            }
        }
#endif
    }
    free(g_iq);
    return (int)hfdl_modo_buenas();
}

int main(void)
{
    static const char *k[] = {
        "HFDL-300bps", "HFDL-600bps", "HFDL-1200bps",
        "HFDL", "HFDL_Sound", "Hf-acars",
        /* La grabacion de referencia, por el CAT de su propia radio: su
         * antena, su cadena de recepcion, su propagacion. Es la unica
         * que mide lo que el ve. */
        "radio_oscar",
    };
    uint32_t c; int total = 0;

    printf("\nEL AIRE DE VERDAD, POR EL FIRMWARE DE VERDAD\n");
    printf("============================================\n\n");

    for (c = 0u; c < sizeof k / sizeof k[0]; c++) {
        g_detalle = 0;
        total += una(k[c]);
    }

    /* Y la larga otra vez, ráfaga a ráfaga. */
    printf("\n  DETALLE DE LA GRABACION LARGA\n");
    g_detalle = 1;
    (void)una("radio_oscar");
    g_detalle = 0;

    printf("\n  %d rafagas con el CRC bueno en total\n", total);
    if (total == 0) {
        printf("\n  *** NI UNA. El firmware no decodifica aire real. ***\n");
        return 1;
    }
    printf("\n  (este banco NO exige un numero todavia: primero hay que saber\n");
    printf("   cuantas ráfagas lleva cada grabacion. Lo que si exige es que no\n");
    printf("   sea cero, que es lo unico que ahora mismo se puede afirmar.)\n");
    return 0;
}
