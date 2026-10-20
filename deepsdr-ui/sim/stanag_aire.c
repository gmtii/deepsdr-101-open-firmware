/*
 * EL DETECTOR CONTRA UNA GRABACION DE VERDAD.
 *
 * Y es lo que faltaba desde el principio. Todo lo que habia hasta ahora
 * probaba el detector contra señales que me habia fabricado YO: eso
 * demuestra que se leer lo que yo mismo escribo, y poco mas. Si mi
 * generador y mi detector comparten una idea equivocada, los dos se ponen
 * de acuerdo y el banco pasa.
 *
 * Esto no. La grabacion la ha hecho otra gente, con una radio de verdad,
 * de una estacion de verdad. Si de ahi sale 106,67 ms, la cadena entera
 * -oscilador, diezmado, autocorrelacion, eleccion del fundamental- vale.
 * Y si no sale, sabemos que no vale ANTES de volver a mandar un binario.
 *
 * Se le pasa un WAV mono. La tasa se lee de la cabecera y se remuestrea a
 * los 12 kHz que espera la radio, que es lo que le llega del demodulador
 * de banda lateral.
 */
#include "stanag_det.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fallos = 0;
static void di(const char *que, int ok)
{
    printf("  %-56s %s\n", que, ok ? "ok" : "MAL");
    if (!ok) { fallos++; }
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/*
 * Un lector de WAV a mano, y a proposito: lo unico que hace falta es
 * encontrar el trozo "fmt " y el "data", y asi el banco no depende de
 * ninguna biblioteca que pueda no estar en la maquina de al lado.
 */
static float *lee_wav(const char *ruta, uint32_t *n_out, uint32_t *hz_out,
                      uint16_t *canales_out)
{
    FILE *f = fopen(ruta, "rb");
    uint8_t cab[12];
    uint16_t canales = 1U, bits = 16U, formato = 1U;
    uint32_t hz = 0UL;
    float *x = 0;

    if (!f) { printf("  no puedo abrir %s\n", ruta); return 0; }
    if (fread(cab, 1, 12, f) != 12 || memcmp(cab, "RIFF", 4) != 0
        || memcmp(&cab[8], "WAVE", 4) != 0) {
        printf("  %s no es un WAV\n", ruta); fclose(f); return 0;
    }

    for (;;) {
        uint8_t ch[8];
        uint32_t tam;

        if (fread(ch, 1, 8, f) != 8) { break; }
        tam = le32(&ch[4]);

        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (tam < 16U || fread(fmt, 1, 16, f) != 16) { break; }
            formato = le16(&fmt[0]);
            canales = le16(&fmt[2]);
            hz      = le32(&fmt[4]);
            bits    = le16(&fmt[14]);
            if (tam > 16U) { fseek(f, (long)(tam - 16U), SEEK_CUR); }
        } else if (memcmp(ch, "data", 4) == 0) {
            uint32_t m = tam / (uint32_t)(bits / 8U);
            uint32_t i;

            if (formato != 1U || (bits != 16U && bits != 8U)) {
                printf("  el WAV no es PCM de 8 o 16 bits (formato %u, %u bits)\n",
                       (unsigned)formato, (unsigned)bits);
                fclose(f); return 0;
            }
            x = (float *)malloc(sizeof(float) * (size_t)m);
            if (!x) { fclose(f); return 0; }
            for (i = 0U; i < m; i++) {
                if (bits == 16U) {
                    uint8_t b[2];
                    if (fread(b, 1, 2, f) != 2) { m = i; break; }
                    x[i] = (float)((int16_t)le16(b));
                } else {
                    int c = fgetc(f);
                    if (c == EOF) { m = i; break; }
                    x[i] = (float)((c - 128) * 256);
                }
            }
            *n_out = m; *hz_out = hz; *canales_out = canales;
            fclose(f);
            return x;
        } else {
            fseek(f, (long)tam + (long)(tam & 1U), SEEK_CUR);
        }
    }
    printf("  %s no trae datos\n", ruta);
    fclose(f);
    return 0;
}

/*
 * LO QUE SE ESPERA DE CADA GRABACION, MEDIDO EL 02/10/2026.
 *
 * Estos periodos NO salen de ninguna norma ni de mi memoria: se midieron
 * aqui, con este mismo programa, sobre las grabaciones de sigidwiki.com
 * que trajo el usuario. Lo que hace el banco es comprobar que se siguen
 * midiendo igual - o sea que vale como prueba de no-regresion del
 * detector, no como fuente de la verdad sobre el formato.
 *
 * El margen es del 2 %: lo que se mide son grabaciones con su desvio de
 * frecuencia y su ruido, no señales de laboratorio.
 */
typedef struct {
    const char *clave;
    uint16_t    periodo;   /* 0 = no tiene UN periodo: va a rafagas */
    uint8_t     minimo;    /* % de ventanas que tienen que enganchar */
    uint16_t    rejilla;   /* si periodo==0, todo lo medido cae en esta rejilla */
} espera_t;
static const espera_t k_espera[] = {
    { "4285",     10667U, 80U, 0U },   /* 106,67 ms - 110 de 114 ventanas */
    { "4529",     21169U, 80U, 0U },   /* 211,69 ms - 158 de 162 */
    { "4415",      6681U, 80U, 0U },   /*  66,81 ms - 596 de 600 */
    { "4539",     11961U, 80U, 0U },   /* 119,61 ms - 585 de 631 */

    /*
     * EL 4481 TIENE DOS CARAS, Y ESTO NO LO SABIA HASTA HOY.
     *
     * El 4481 no es una forma de onda: es el sistema de radiodifusion
     * naval, y admite dos modems. El de FSK es la teleimpresora de
     * siempre, 50 baudios y dos tonos separados 831 Hz medidos aqui. El
     * de PSK resulta ser EL 4285: no "parecido", el mismo.
     *
     * Y eso no lo digo de memoria ni de haberlo leido. Se mide: contra la
     * grabacion de PSK, el preambulo del 4285 -la secuencia m de 80
     * simbolos que saco el LFSR de 5 bits- correlaciona a 0,886 con un
     * fondo de 0,084, sale la misma estructura 31/31/194 simbolos, y hay
     * 243 comienzos de trama separados por 1280 muestras exactas, que son
     * 256,000 simbolos. Las mismas tres cifras que dio la grabacion del
     * 4285. Para comparar: el 4529, que SI es otra forma de onda,
     * correlaciona a 0,33 contra ese mismo preambulo.
     *
     * Consecuencia practica, y es buena: el demodulador de 4285 que estoy
     * escribiendo sirve tambien para el 4481 en PSK sin tocar una linea.
     * Lo que lleve dentro cifrado (estas suelen ir con KG-84) no lo toco,
     * que eso ya esta hablado.
     */
    { "4481_psk", 10667U, 90U, 0U },   /* 106,67 ms - 296 de 300, calidad 100 */

    /*
     * El 4481 en FSK se le resiste al metodo, y tiene su motivo: una FSK
     * no repite un preambulo con fase conocida, asi que la
     * autocorrelacion tiene poco a lo que agarrarse. Engancha en 198 de
     * 837 ventanas con calidad 25, frente al 100 limpio del 4285. NO es
     * un fallo del detector: a este hay que identificarlo por sus dos
     * tonos separados 831 Hz, que se ven en el espectro a la primera. Se
     * deja en el banco con el minimo bajo para que quede constancia de
     * cuanto se le resiste.
     */
    { "4481_fsk",  2661U, 15U, 0U },   /*  26,61 ms - 198 de 837, calidad 25 */

    /*
     * EL 4538 NO TIENE UN PERIODO, Y HAY QUE DECIRLO ASI.
     *
     * Es el ALE de tercera generacion: no emite seguido, emite rafagas
     * cortas de formas de onda distintas (las BW0..BW5), cada una con su
     * propio enmarcado. En los 28,9 s de grabacion el detector engancha
     * 31 ventanas de 335, y los periodos que saca son cuatro:
     *
     *     13,33 ms   = 32 simbolos a 2400 Bd
     *     26,67 ms   = 64
     *    106,67 ms   = 256
     *    133,34 ms   = 320
     *
     * Todos multiplos de 32 simbolos. O sea que el detector no se esta
     * equivocando: esta viendo bien cuatro rafagas distintas construidas
     * sobre el mismo bloque de 32.
     *
     * Y de aqui sale el aviso importante para la pantalla: una de esas
     * rafagas cae en 106,67 ms, que es EXACTAMENTE el 4285. Por el
     * periodo solo, un 4538 y un 4285 no se distinguen. Lo que los separa
     * es que el 4285 emite seguido y el 4538 va a rafagas - por eso la
     * tabla de la radio marca el 4285 como dudoso y lo dice en pantalla
     * en vez de cantar un nombre que puede no ser.
     *
     * Asi que aqui no se comprueba "el periodo es X", que seria mentira:
     * se comprueba que todo lo que mida cae en la rejilla de 13,33 ms.
     */
    { "4538",         0U,  5U, 1333U },
};

int main(int argc, char **argv)
{
    static float trabajo[STANAG_DET_FLOATS];
    const char *ruta = (argc > 1) ? argv[1] : 0;
    float *x = 0, *y = 0;
    uint32_t n = 0UL, hz = 0UL, m, i;
    uint16_t canales = 1U;
    stanag_det_t r, ult;
    uint32_t vistas = 0UL, pasos = 0UL;
    /* Para el caso del 4538: no hay UN periodo, asi que se mira si todo
     * lo que sale cae en una rejilla. Se guarda el peor desvio. */
    uint16_t fuera_rejilla = 0U, distintos[8];
    uint32_t n_distintos = 0UL;

    if (!ruta) { printf("uso: stanagaire <fichero.wav>\n"); return 1; }
    printf("\n=== El detector contra una grabacion de verdad ===\n\n");

    x = lee_wav(ruta, &n, &hz, &canales);
    if (!x) { return 1; }
    printf("  %s: %u muestras, %u Hz, %u canal(es)  =  %.1f s\n",
           ruta, (unsigned)n, (unsigned)hz, (unsigned)canales,
           (double)n / ((double)hz * (double)canales));

    /* A mono y a 12 kHz, que es lo que le llega al detector en la radio.
     * Interpolacion lineal: para medir periodicidad sobra, y no meto un
     * remuestreador bueno porque entonces habria que probar ESE. */
    {
        uint32_t marcos = n / canales;
        m = (uint32_t)(((double)marcos * 12000.0) / (double)hz);
        y = (float *)malloc(sizeof(float) * (size_t)m);
        if (!y) { return 1; }
        for (i = 0U; i < m; i++) {
            double t = ((double)i * (double)hz) / 12000.0;
            uint32_t k = (uint32_t)t;
            double fr = t - (double)k;
            float a, b;
            if ((k + 1U) >= marcos) { k = marcos - 2U; fr = 1.0; }
            a = x[k * canales]; b = x[(k + 1U) * canales];
            y[i] = (float)((1.0 - fr) * (double)a + fr * (double)b);
        }
        printf("  remuestreada a 12 kHz: %u muestras\n", (unsigned)m);
    }

    di("hay al menos dos segundos de audio", m > 24000U);
    di("el detector coge su memoria",
       stanag_det_init(trabajo, STANAG_DET_FLOATS) == 1U);

    memset(&ult, 0, sizeof ult);
    memset(distintos, 0, sizeof distintos);
    for (i = 0U; i + 64U <= m; i += 64U) {
        stanag_det_mete(&y[i], 64U);
        if (stanag_det_paso(&r)) {
            pasos++;
            if (r.vale) {
                uint32_t k;
                vistas++; ult = r;
                /* la lista de periodos distintos que ha dado la grabacion,
                 * agrupando los que esten a menos del 2 % unos de otros */
                for (k = 0U; k < n_distintos; k++) {
                    uint16_t d = (uint16_t)((r.periodo_us > distintos[k])
                                 ? (r.periodo_us - distintos[k])
                                 : (distintos[k] - r.periodo_us));
                    if ((uint32_t)d * 50UL <= (uint32_t)distintos[k]) { break; }
                }
                if (k == n_distintos
                    && n_distintos < (sizeof distintos / sizeof distintos[0])) {
                    distintos[n_distintos++] = r.periodo_us;
                }
            }
        }
    }

    printf("\n  ventanas procesadas: %u   con trama: %u\n",
           (unsigned)pasos, (unsigned)vistas);
    if (ult.vale) {
        printf("  ultimo periodo: %u,%02u ms   retardo %u   picos %u   calidad %u\n",
               (unsigned)(ult.periodo_us / 100U), (unsigned)(ult.periodo_us % 100U),
               (unsigned)ult.retardo, (unsigned)ult.picos, (unsigned)ult.calidad);
    }

    di("encuentra trama en la grabacion", vistas > 0UL);

    /*
     * Y lo que se espera DE ESTA grabacion, si es una de las conocidas.
     * Se busca la clave en el nombre del fichero: asi el banco no tiene
     * que saber en que orden se le pasan.
     */
    {
        uint32_t k;
        const espera_t *e = 0;

        for (k = 0U; k < (sizeof k_espera / sizeof k_espera[0]); k++) {
            if (strstr(ruta, k_espera[k].clave) != 0) { e = &k_espera[k]; break; }
        }
        if (e != 0) {
            uint32_t pc = (pasos > 0UL) ? ((vistas * 100UL) / pasos) : 0UL;

            printf("  periodos distintos medidos: %u  ->", (unsigned)n_distintos);
            for (k = 0U; k < n_distintos; k++) {
                printf(" %u,%02u", (unsigned)(distintos[k] / 100U),
                       (unsigned)(distintos[k] % 100U));
            }
            printf(" ms\n");

            if (e->periodo != 0U) {
                uint16_t tol = (uint16_t)(e->periodo / 50U);   /* el 2 % */
                printf("  esperado para el %s: %u,%02u ms  y al menos el %u%% de las ventanas\n",
                       e->clave, (unsigned)(e->periodo / 100U),
                       (unsigned)(e->periodo % 100U), (unsigned)e->minimo);
                di("el periodo es el que se midio en su dia",
                   ult.vale && ult.periodo_us >= (e->periodo - tol)
                            && ult.periodo_us <= (e->periodo + tol));
            } else {
                /*
                 * Sin periodo fijo: lo que se exige es que TODO lo medido
                 * caiga en la rejilla, con el 2 % de margen de siempre.
                 */
                printf("  esperado para el %s: sin periodo fijo, todo multiplo de %u,%02u ms\n",
                       e->clave, (unsigned)(e->rejilla / 100U),
                       (unsigned)(e->rejilla % 100U));
                for (k = 0U; k < n_distintos; k++) {
                    uint32_t v = distintos[k];
                    uint32_t n_veces = (v + (e->rejilla / 2U)) / e->rejilla;
                    uint32_t ideal = n_veces * (uint32_t)e->rejilla;
                    uint32_t d = (v > ideal) ? (v - ideal) : (ideal - v);
                    if (n_veces == 0UL || d * 50UL > ideal) { fuera_rejilla++; }
                }
                di("todos los periodos caen en la rejilla", fuera_rejilla == 0U);
                di("y sale mas de un periodo, que es lo que se espera",
                   n_distintos > 1UL);
            }
            di("y engancha en tantas ventanas como entonces",
               pc >= (uint32_t)e->minimo);
        }
    }
    /*
     * 106,67 ms con un 1 % de margen. El numero NO sale de este banco:
     * son los 256 simbolos a 2400 baudios de gr-digitalhf, los 106,66 ms
     * del README de LimeAuto y las muestras 5333/10667 de su codigo. Tres
     * fuentes que no se conocen entre si. Ver stanag_det.h.
     */
    free(x); free(y);
    printf("\n  %s\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO");
    return fallos ? 1 : 0;
}
