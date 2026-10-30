/*
 * MSF DE VERDAD, POR LA MISMA PUERTA QUE EL AUDIO DE LA RADIO.
 * ===========================================================
 *
 * sim/msftest.c mide el formato contra una señal que fabrica EL PROPIO
 * BANCO, y pasa: huso, fecha, paridades y el patron fijo, todo correcto, y
 * aguanta un 35% de ruido. Y aun asi un usuario dice que MSF no decodifica
 * nada en la radio. Con un banco sintetico eso no se puede investigar: la
 * señal que entra es la que yo creo que entra.
 *
 * Esto mide la de verdad. El fichero sale de una grabacion de 60 kHz
 * recibida en banda lateral -la portadora cae en 1.000,5 Hz de audio- a la
 * que se le ha sacado la envolvente mezclando a banda base y promediando
 * en bloques de 128 muestras a 48 kHz, o sea 375 muestras por segundo.
 *
 * ESE 375 NO ES UN NUMERO BONITO: es EXACTAMENTE el ritmo al que main.c
 * llama a dcf77_feed() en MSF -una muestra por bloque de 256 a 96 kHz, ver
 * dcf77_submuestras()-. Asi que lo que entra aqui es, muestra a muestra, lo
 * mismo que le entra a la radio.
 *
 * LO QUE LA GRABACION TIENE DENTRO, medido antes de escribir este banco:
 *
 *   portadora          1.000,5 Hz, 36 dB sobre lo demas
 *   contraste          0,98  (alto 0,92 / bajo 0,015)
 *   apagones           159, separados 1,000 s clavados
 *   duraciones         99 / 200 / 299 ms
 *
 * O sea una señal de libro. Si con esto no decodifica, el fallo es nuestro
 * y esta en el decodificador; si decodifica, el fallo del usuario esta
 * aguas arriba -en la cadena de radiofrecuencia- y hay que buscarlo alli.
 * Esa es toda la pregunta que este banco contesta, y por eso existe.
 *
 * DURA 2,5 MINUTOS, y eso es poco a proposito dicho: el decodificador
 * exige DOS tramas seguidas que cuadren -dos minutos solo de tramas- mas
 * lo que tarde en engancharse al hueco del minuto. Con 152,8 s puede que
 * no llegue a LISTO y eso NO seria un fallo. Lo que se mira aqui es si
 * llega a LEYENDO, si cuenta los 59 bits, y que motivo da si descarta.
 */
#include "dcf77.h"
#include "hora_comun.h"
#include <stdio.h>
#include <stdlib.h>

#define RUTA "sim/muestras/msf_yt_env375.f32"
#define FS   375.0f

int main(int argc, char **argv)
{
    /* Sin argumento, la muestra de siempre; con el, la que se diga. Asi el
     * mismo banco sirve para comparar dos grabaciones sin recompilar. */
    const char *ruta = (argc > 1) ? argv[1] : RUTA;
    FILE *f = fopen(ruta, "rb");
    float *v;
    long n, i;
    dcf_info_t inf;
    uint8_t estado_ant = 0xFFU;
    long seg_ant = -1;

    /*
     * La muestra vive en sim/muestras/, que esta en .gitignore: quien
     * clone el repositorio no la tiene. Se salta diciendolo, como hace
     * sim/als_real.c, en vez de tumbar la tanda entera.
     */
    if (!f) {
        printf("\n  (sin %s: me salto la prueba de MSF con señal real)\n\n",
               ruta);
        return 0;
    }
    fseek(f, 0, SEEK_END); n = ftell(f) / 4; fseek(f, 0, SEEK_SET);
    v = malloc((size_t)n * sizeof *v);
    if (!v || fread(v, sizeof *v, (size_t)n, f) != (size_t)n) {
        printf("  no puedo leer %s\n", ruta); return 1;
    }
    fclose(f);

    printf("\n  MSF DE VERDAD  (%ld muestras, %.1f s)\n\n", n, n / FS);

    hora_huso_pon(0);                 /* UTC, que es lo que manda MSF */
    dcf77_start(HORA_MSF, FS);

    for (i = 0; i < n; i++) {
        dcf77_feed(v[i]);
        if ((i % 375) == 0) {
            long seg = i / 375;
            dcf77_info(&inf);
            if (inf.estado != estado_ant || seg - seg_ant >= 15) {
                static const char *const k[] = {
                    "BUSCANDO", "LEYENDO", "CONFIRMANDO", "LISTO", "PERDIDO"
                };
                printf("  %3lds  %-12s bits %2u  marca %3u ms  ciclo %4u ms"
                       "  buenas %u  malas %u  raras %u  contraste %.2f\n",
                       seg, k[inf.estado < 5U ? inf.estado : 0U], inf.bits,
                       inf.ms_marca, inf.ms_ciclo, inf.tramas_ok,
                       inf.tramas_malas, inf.marcas_raras, inf.contraste);
                estado_ant = inf.estado; seg_ant = seg;
            }
        }
    }

    dcf77_info(&inf);
    printf("\n  AL FINAL:\n");
    printf("    estado        %u\n", inf.estado);
    printf("    tramas buenas %u\n", inf.tramas_ok);
    printf("    tramas malas  %u   (motivo %u)\n",
           inf.tramas_malas, dcf77_motivo());
    printf("    marcas raras  %u\n", inf.marcas_raras);
    if (inf.estado == DCF_LISTO || inf.tramas_ok > 0U) {
        static const char *const k_dia[8] = {
            "?", "lunes", "martes", "miercoles", "jueves",
            "viernes", "sabado", "domingo"
        };
        printf("    hora          %02u:%02u   %02u/%02u/%02u   %s\n",
               inf.hora, inf.minuto, inf.dia, inf.mes, inf.anno,
               k_dia[inf.dia_semana < 8U ? inf.dia_semana : 0U]);
    }
    /*
     * EL VEREDICTO, y es lo que convierte esto en banco y no en volcado.
     *
     * Con 152,8 s no se puede exigir LISTO -haria falta otro minuto-, asi
     * que lo que se exige es lo que esta grabacion SI tiene que dar: que
     * enganche el hueco del minuto, que cuente una trama entera y que esa
     * trama pase las paridades. Si manana se consigue una grabacion mas
     * larga, aqui se sube el liston a LISTO y a la hora exacta.
     */
    {
        int mal = 0;

        printf("\n  LO QUE SE EXIGE:\n");
        printf("    engancha el hueco del minuto y lee .... %s\n",
               (inf.estado != DCF_BUSCANDO) ? "ok" : "*** NO ***");
        if (inf.estado == DCF_BUSCANDO) { mal++; }
        printf("    al menos una trama buena .............. %s\n",
               (inf.tramas_ok >= 1U) ? "ok" : "*** NO ***");
        if (inf.tramas_ok < 1U) { mal++; }
        printf("    el contraste pasa del minimo .......... %s\n",
               (inf.contraste > 0.25f) ? "ok" : "*** NO ***");
        if (inf.contraste <= 0.25f) { mal++; }

        /*
         * Y LA COMPROBACION QUE NO DEPENDE DE NADIE - 30/10/2026.
         *
         * La trama trae la FECHA (bits 25A-35A) y el DIA DE LA SEMANA
         * (36A-38A) por caminos separados, con paridades distintas. Si los
         * dos coinciden con el calendario de verdad, el fotograma no es
         * que "parezca bueno": es que dos datos independientes cuadran con
         * una tercera fuente que no somos nosotros.
         *
         * Es el mismo criterio que hizo util el banco de ALS162: no
         * compararse consigo mismo. Y aqui sale gratis, porque la norma
         * del NPL manda las dos cosas en la misma trama.
         */
        if (inf.tramas_ok > 0U) {
            /*
             * Sakamoto. La tabla es ESTA y no otra: la primera vez puse
             * {0,3,3,6,1,4,6,2,5,0,3,5}, que es de otra variante, y el
             * banco acuso al decodificador de equivocarse de dia con una
             * trama que estaba bien. Enero coincidia en las dos tablas, o
             * sea que la primera muestra paso y la segunda no. Casi
             * apunto a un fallo que era mio.
             */
            static const int k_mes[12] = { 0,3,2,5,0,3,5,1,4,6,2,4 };
            int a = 2000 + (int)inf.anno, m = (int)inf.mes, d = (int)inf.dia;
            int y = a - (m < 3 ? 1 : 0);
            int w;   /* 0 = domingo, calendario gregoriano */

            w = (y + y/4 - y/100 + y/400 + k_mes[m - 1] + d) % 7;
            {
                /* dcf_info_t usa 1 = lunes; el calculo da 0 = domingo. */
                uint8_t esperado = (uint8_t)((w == 0) ? 7 : w);

                printf("    la fecha cuadra con el calendario ..... %s\n",
                       (inf.dia_semana == esperado) ? "ok" : "*** NO ***");
                if (inf.dia_semana != esperado) {
                    printf("      la trama dice dia %u y el %02u/%02u/%04d "
                           "fue dia %u\n", inf.dia_semana, d, m, a, esperado);
                    mal++;
                }
            }
        }

        if (mal) {
            printf("\n  *** %d fallo(s): el decodificador de MSF no saca "
                   "esta señal ***\n\n", mal);
            free(v);
            return 1;
        }
        printf("\n  MSF decodifica señal de verdad  ok\n\n");
    }
    free(v);
    return 0;
}
