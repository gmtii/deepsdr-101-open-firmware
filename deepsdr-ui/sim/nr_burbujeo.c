/*
 * EL BURBUJEO DEL REDUCTOR DE RUIDO: PRIMERO HACERLO APARECER.
 *
 * *** De un aviso del 07/10/2026: con la señal poco por encima del
 * ruido se oye un burbujeo fuerte, y se preguntaba si lo producia el
 * reductor de ruido. Con señales fuertes -las de 40 metros, transmitiendo
 * con mucha potencia- no se oye nada de eso y las voces graves suenan
 * limpias. ***
 * Pedido por el usuario.
 *
 * ESO ULTIMO MANDA. Si el usuario no lo oye y el banco no lo mide, arreglarlo
 * seria cambiar codigo a ciegas para que suene distinto a alguien que no
 * esta delante. Asi que este banco NO empieza arreglando: empieza
 * REPRODUCIENDO. Si el numero no sube, no hay nada que arreglar y hay que
 * pedir una grabacion.
 *
 * LA PRIMERA VERSION DE ESTE BANCO NO LO VIO, y por tres razones que estaban
 * todas en como lo medi yo, no en la radio:
 *
 *   1. Barri el mando de 512 en 512. El umbral es 10^(11*v/4096), asi que
 *      entre dos de esas posiciones se multiplica por VEINTICUATRO. La
 *      ventana donde el reductor hace algo util mide un par de dB: me la
 *      salte entera. Pase de "no hace nada" a "lo mata todo" sin pisarla.
 *   2. Probe con un TONO. Un tono es estacionario, asi que la ganancia
 *      tambien lo es y no modula nada. El ruido musical se oye en los HUECOS
 *      de la voz, donde la ganancia se desploma y vuelve a subir.
 *   3. Medi la energia de toda la banda. Unos pocos bins saltando entre 64
 *      no mueven el total; mueven SU banda.
 *
 * COMO SE MIDE AHORA. Dos numeros, los dos contra la misma señal sin pasar
 * por el reductor, que es el suelo de lo que mide algo que no burbujea:
 *
 *   VAIVEN   desviacion tipica, en dB, del nivel de cada banda de ~370 Hz
 *            en ventanas de 10 ms. Un ruido estable no se mueve; el ruido
 *            musical salta.
 *   PICOS    por cuadro, cuantos dB saca el bin mas alto a la mediana. El
 *            ruido musical son bins sueltos sobreviviendo: contraste alto.
 *
 * Y LA MITAD QUE IMPIDE HACER TRAMPA: cualquiera baja el burbujeo apagando
 * el reductor. Asi que se mide tambien cuanto ruido quita y cuanto deja
 * pasar de la voz. Un arreglo vale si baja el burbujeo SIN perder esas dos.
 *
 * =====================================================================
 * LO QUE PASO DESPUES, Y POR QUE ESTE BANCO NO "ARREGLA" NADA
 * =====================================================================
 *
 * El material de referencia es la radio con el reductor puesto, subiendo el
 * mando de 800 a 1110 y bajandolo a 1000. Sobre ese audio:
 *
 *   celdas oscuras del fondo    NR  800      8,3 %
 *                               NR 990-1110  8,1 %
 *                               NR 1000      8,4 %
 *
 * PLANO. No hay diferencia. Y la razon esta medida tambien: el rango
 * dinamico de esa grabacion es de 20 dB (percentil 99 menos percentil 5), y
 * el burbujeo vive en los huecos entre silabas, 20-30 dB por debajo de la
 * voz. La habitacion, el microfono del movil y el AAC rellenan justo ese
 * hueco. Lo que se mide ahi es el movil, no la radio.
 *
 * Y ANTES DE ESO CASI ME ENGAÑO: saque espectrogramas de tres tramos y el
 * fondo parecia romperse en islas al subir el mando -la pinta exacta del
 * ruido musical-. Era mi propia normalizacion: cada panel escalado a su
 * propio maximo, y en los tramos de despues la voz es mas fuerte, asi que su
 * fondo PARECE mas oscuro sin haber cambiado nada.
 *
 * ASI QUE ESTE BANCO NO AFIRMA QUE EL BURBUJEO EXISTA NI QUE NO. Lo que hace
 * es dejar FIJADO lo que el reductor hace hoy, para que:
 *
 *   - se vea de un vistazo donde esta la ventana util del mando, que es un
 *     7 % de su recorrido y es un defecto medido por su cuenta;
 *   - cualquier cambio futuro que EMPEORE el vaiven que añade el reductor
 *     salte en rojo. No puedo demostrar el burbujeo fuerte del aviso,
 *     pero si puedo impedir que crezca el que si se mide.
 *
 * Para demostrarlo hace falta una grabacion donde los huecos existan: audio
 * digital de la radio, o el movil pegado al altavoz en silencio con DIEZ
 * SEGUNDOS APAGADO Y DIEZ ENCENDIDO sobre la misma señal. Esa pareja es lo
 * que hace la medida posible, y hasta tenerla el camino de audio no se toca.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "nr_ss.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define N        NR_SS_BLOCK_SAMPLES   /* 32 */
#define FS       12000.0
#define BLOQUES  900                   /* 2,4 s */
#define ASIENTO  60
#define MUESTRAS (BLOQUES * N)
#define VENT     120                   /* 10 ms */
#define BANDAS   8

/* Igual que sim/nr_curva.c: la cadena trabaja en cuentas de ADC, no en +-1.
 * Y ojo -demod_am.c lo dice- el reductor va ANTES del AGC, asi que este
 * nivel depende de la estacion y no esta normalizado. */
#define ESCALA   2000.0f

static int fallos = 0;
static void di(const char *que, int ok, const char *det)
{
    printf("    %-44s %s%s%s\n", que, ok ? "ok" : "*** MAL ***",
           det ? "  " : "", det ? det : "");
    if (!ok) { fallos++; }
}

static uint32_t sem;
static float ruidito(void)
{
    sem = sem * 1103515245u + 12345u;
    return (float)((int32_t)(sem >> 8) % 20001 - 10000) / 10000.0f;
}

/*
 * VOZ DE MENTIRA, PERO CON LO QUE IMPORTA: armonicos de 120 Hz -un baritono,
 * como los del aviso- dentro de la banda de SSB, y una envolvente silabica
 * de 3,5 Hz con silencios de verdad entre silabas. Los silencios son el
 * punto: es donde se oye el burbujeo.
 */
static double voz(long t)
{
    double s = 0.0, env;
    double tt = (double)t / FS;
    int k;
    for (k = 1; k <= 20; k++) {
        double f = 120.0 * k;
        if (f < 300.0 || f > 2700.0) { continue; }
        s += sin(2.0 * M_PI * f * tt + 0.7 * k) / (double)k;
    }
    env = 0.5 + 0.5 * sin(2.0 * M_PI * 3.5 * tt);
    env = env * env * env;            /* huecos marcados entre silabas */
    return s * env;
}

/* nivel_db: lo que cambia el NIVEL TOTAL de la salida. NO es "cuanto ruido
 * quita": cuando hay voz, esa energia la domina la voz. Esa confusion es
 * exactamente el fallo que tenia sim/nr_curva.c hasta hoy, y lo tenia
 * tambien este fichero - se vio al escribir la comprobacion de abajo, que
 * dio +2,1 dB de neto usando nivel_db donde hacia falta ruido_db.
 * ruido_db / voz_db: cada uno medido con la otra cosa AUSENTE, que es la
 * unica forma de que los dos sean comparables entre si. */
typedef struct { double vaiven, picos, nivel_db, ruido_db, voz_db; } res_t;

static float g_sal[MUESTRAS];

/* Una pasada; devuelve la energia media y deja la salida en g_sal. */
static double pasada(uint16_t v, uint8_t on, double amp_voz, double amp_ruido)
{
    int b; unsigned i; long t = 0; double e = 0.0;

    sem = 12345u;
    nr_ss_init();
    nr_ss_set_enabled(on);
    nr_ss_set_strength(v);

    for (b = 0; b < BLOQUES; b++) {
        float buf[N];
        for (i = 0; i < N; i++) {
            double x = amp_ruido * ruidito() + amp_voz * voz(t);
            buf[i] = (float)(x * ESCALA);
            t++;
        }
        if (on) { nr_ss_process(buf, N); }
        for (i = 0; i < N; i++) { g_sal[b * N + i] = buf[i]; }
    }
    for (b = ASIENTO; b < BLOQUES; b++) {
        for (i = 0; i < N; i++) { e += (double)g_sal[b*N+i] * g_sal[b*N+i]; }
    }
    return e / (double)((BLOQUES - ASIENTO) * N);
}

/* Goertzel-ish: energia de una banda en una ventana, por DFT directa corta. */
static void metricas(double *vaiven, double *picos)
{
    static double lvl[BANDAS][MUESTRAS / VENT];
    int nv = 0, k, bd;
    double sp = 0.0; int np = 0;
    int ini = ASIENTO * N;

    for (k = ini; k + VENT <= MUESTRAS && nv < (int)(MUESTRAS/VENT); k += VENT) {
        double bin[64]; int j, m;
        /* DFT de 120 muestras -> 60 bins de 100 Hz */
        for (m = 1; m <= 27; m++) {          /* 100..2700 Hz */
            double re = 0.0, im = 0.0;
            for (j = 0; j < VENT; j++) {
                double w = -2.0 * M_PI * m * j / VENT;
                re += g_sal[k+j] * cos(w); im += g_sal[k+j] * sin(w);
            }
            bin[m] = re*re + im*im;
        }
        /* bandas de ~370 Hz */
        for (bd = 0; bd < BANDAS; bd++) {
            double s = 0.0; int lo = 3 + bd*3, hi = lo + 3;
            for (m = lo; m < hi && m <= 27; m++) { s += bin[m]; }
            lvl[bd][nv] = 10.0 * log10(s + 1e-12);
        }
        /* contraste: el bin mas alto contra la mediana */
        {
            double v[27]; int n2 = 0, a, c;
            for (m = 3; m <= 27; m++) { v[n2++] = bin[m]; }
            for (a = 0; a < n2; a++) for (c = a+1; c < n2; c++)
                if (v[c] < v[a]) { double tp = v[a]; v[a] = v[c]; v[c] = tp; }
            sp += 10.0 * log10((v[n2-1] + 1e-12) / (v[n2/2] + 1e-12));
            np++;
        }
        nv++;
    }
    {   /* media de la desviacion por banda */
        double acc = 0.0;
        for (bd = 0; bd < BANDAS; bd++) {
            double s1 = 0.0, s2 = 0.0; int a;
            for (a = 0; a < nv; a++) { s1 += lvl[bd][a]; s2 += lvl[bd][a]*lvl[bd][a]; }
            acc += sqrt(s2/nv - (s1/nv)*(s1/nv));
        }
        *vaiven = acc / BANDAS;
    }
    *picos = (np > 0) ? (sp / np) : 0.0;
}

static res_t mide(uint16_t v, double amp_voz, double amp_ruido)
{
    res_t r; double e_on, e_off;
    double va_off, pi_off;

    e_on = pasada(v, 1U, amp_voz, amp_ruido);
    metricas(&r.vaiven, &r.picos);
    e_off = pasada(v, 0U, amp_voz, amp_ruido);
    metricas(&va_off, &pi_off);

    r.vaiven -= va_off;          /* lo que AÑADE el reductor */
    r.picos  -= pi_off;
    r.nivel_db = 10.0 * log10((e_on + 1e-12) / (e_off + 1e-12));
    /*
     * LA MITAD QUE ESTABA ANUNCIADA Y NO SE CALCULABA - 11/10/2026.
     *
     * Pedido por el usuario.
     *
     * Aqui habia `r.voz_db = 0.0; (void)amp_voz;`. O sea: el campo existia,
     * la cabecera de este fichero lo prometia con todas las letras
     * -"se mide tambien cuanto ruido quita y cuanto deja pasar de la voz.
     * Un arreglo vale si baja el burbujeo SIN perder esas dos"- y no se
     * calculaba. Ni se imprimia ni se comprobaba en ninguna linea.
     *
     * Es la misma forma que el fallo de sim/nr_curva.c del 25/09: una
     * medida que existe por su nombre y no por su formula. Y aqui era peor,
     * porque la cabecera decia para que estaba: sin ella, un cambio que
     * silenciara la voz ENTERA bajaba el burbujeo y salia en verde.
     *
     * Se mide como se tiene que medir: la voz SOLA, sin ruido, con el
     * reductor encendido y apagado. Lo que sale es cuantos dB de voz se
     * deja por el camino - y eso ya no se puede fingir atenuando.
     */
    if (amp_ruido > 0.0 && amp_voz > 0.0) {
        /* El ruido SOLO. Sin esto, con voz delante, "cuanto ruido quita"
         * sale contaminado por la voz y da 10,4 dB donde son 20,9. */
        double r_on  = pasada(v, 1U, 0.0, amp_ruido);
        double r_off = pasada(v, 0U, 0.0, amp_ruido);
        r.ruido_db = 10.0 * log10((r_on + 1e-12) / (r_off + 1e-12));
    } else {
        r.ruido_db = r.nivel_db;   /* sin voz, el nivel ES el ruido */
    }
    if (amp_voz > 0.0) {
        double v_on  = pasada(v, 1U, amp_voz, 0.0);
        double v_off = pasada(v, 0U, amp_voz, 0.0);
        r.voz_db = 10.0 * log10((v_on + 1e-12) / (v_off + 1e-12));
    } else {
        r.voz_db = 0.0;   /* sin voz no hay voz que medir */
    }
    return r;
}

int main(void)
{
    uint16_t v;
    double peor_v = 0.0, peor_p = 0.0; uint16_t peor_pos = 0;
    double peor_voz_v = 0.0, peor_voz_db = 0.0;

    printf("\nEL BURBUJEO DEL REDUCTOR DE RUIDO\n");
    printf("=================================\n\n");
    printf("  EL MANDO CAMBIO DE SIGNIFICADO EL 11/10/2026: ya no es un umbral\n");
    printf("  absoluto de once decadas, es k = cuantas veces por encima del\n");
    printf("  SUELO DE RUIDO hay que estar. Ver nr_ss.c y sim/nr_curva.c.\n");
    printf("  Por eso los puntos de sonda de este banco se han movido: 820,\n");
    printf("  1100 y 1536 describian la geometria del mando VIEJO.\n\n");

    printf("  SOLO RUIDO (lo que se oye entre silabas)\n");
    printf("  mando  umbral    vaiven+  picos+   ruido\n");
    printf("  -----  --------  -------  ------   ------\n");
    for (v = 1200U; v <= 3000U; v = (uint16_t)(v + 60U)) {
        res_t r = mide(v, 0.0, 1.0);
        double th = pow(10.0, 11.0 * (double)v / 4096.0);
        printf("  %5u  %8.1f  %6.2f   %6.2f   %6.1f dB%s\n",
               v, th, r.vaiven, r.picos, r.ruido_db,
               (r.vaiven > peor_v) ? "  <-" : "");
        if (r.vaiven > peor_v) { peor_v = r.vaiven; peor_p = r.picos; peor_pos = v; }
    }
    printf("\n  lo peor: mando %u -> vaiven +%.2f dB, picos +%.2f dB\n\n",
           peor_pos, peor_v, peor_p);

    printf("  VOZ DE BARITONO SOBRE RUIDO, en el peor mando (%u)\n", peor_pos);
    printf("  voz/ruido  vaiven+  picos+   ruido      voz        neto\n");
    printf("  ---------  -------  ------   -------   --------   ------\n");
    {
        double snr[] = { 0.0, 3.0, 6.0, 10.0, 20.0 };
        unsigned i;
        for (i = 0; i < sizeof snr / sizeof snr[0]; i++) {
            double a = pow(10.0, snr[i] / 20.0);
            res_t r = mide(peor_pos, a, 1.0);
            printf("  %6.0f dB   %6.2f   %6.2f   %6.1f dB   %7.1f dB  %+6.1f\n",
                   snr[i], r.vaiven, r.picos, r.ruido_db, r.voz_db,
                   r.voz_db - r.ruido_db);
            if (r.vaiven > peor_voz_v) { peor_voz_v = r.vaiven; }
            if (r.voz_db < peor_voz_db) { peor_voz_db = r.voz_db; }
        }
    }
    printf("\n");

    /*
     * 1. LA VENTANA UTIL DEL MANDO. Lo que de verdad esta roto y se puede
     * demostrar: por debajo de 820 no hace nada y por encima de ~1500 se
     * lleva la señal por delante. Si alguien cambia el mapeo del mando, esto
     * lo dice.
     */
    {
        res_t flojo = mide(800U,  0.0, 1.0);
        res_t util  = mide(2000U, 0.0, 1.0);
        res_t tope  = mide(4095U, 0.0, 1.0);
        res_t tono_util = mide(2000U, 3.0, 1.0);
        res_t tono_tope = mide(4095U, 3.0, 1.0);
        char det[128];

        snprintf(det, sizeof det, "(mando 800 quita %.1f dB)", flojo.ruido_db);
        di("por debajo de 800 el mando apenas hace nada", flojo.ruido_db > -4.0, det);
        snprintf(det, sizeof det, "(mando 2000 quita %.1f dB)", util.ruido_db);
        di("en 2000 -el centro- quita ruido de verdad", util.ruido_db < -8.0, det);

        /*
         * AQUI HABIA "y en 1536 ya es una guillotina", Y SE HA QUITADO
         * PORQUE YA NO LA HAY - 11/10/2026.
         *
         * Era verdad del mando viejo y era un DEFECTO, no una
         * caracteristica: media carrera del mando destrozaba el audio. El
         * umbral nuevo se mide contra el suelo de ruido, asi que subirlo
         * atenua mas pero no se lleva la senal por delante.
         *
         * Asi que la comprobacion se da la vuelta: lo que hay que exigir
         * ahora es justo lo contrario, que NI SIQUIERA EN EL TOPE se coma
         * la voz. Con el mando viejo esto salia en rojo (dejaba -31,8 dB
         * de voz en 1536); con el nuevo tiene que pasar.
         */
        snprintf(det, sizeof det, "(en el tope deja %.1f dB de voz)", tono_tope.voz_db);
        di("ni en el tope del mando se come la voz", tono_tope.voz_db > -6.0, det);
        snprintf(det, sizeof det, "(en el tope quita %.1f dB de ruido)", tope.ruido_db);
        di("y en el tope sigue quitando ruido", tope.ruido_db < -10.0, det);

        snprintf(det, sizeof det, "(quita %.1f dB de ruido y %.1f dB de voz: %+.1f neto)",
                 tono_util.ruido_db, tono_util.voz_db,
                 tono_util.voz_db - tono_util.ruido_db);
        di("en 2000 quita mas ruido que voz, y por bastante",
           (tono_util.voz_db - tono_util.ruido_db) > 6.0, det);
        snprintf(det, sizeof det, "(lo peor con voz: %.1f dB)", peor_voz_db);
        di("con voz encima, en el peor mando del barrido no la destroza",
           peor_voz_db > -8.0, det);
        printf("\n      La ventana util va de ~1200 a ~3000, con el pico en el centro.\n");
        printf("      Y el umbral YA NO es absoluto: se mide contra el suelo de ruido,\n");
        printf("      asi que el mismo numero hace lo mismo con cualquier estacion.\n\n");
    }

    /*
     * 2. EL TOPE DE VAIVEN. No demuestra que haya burbujeo: fija el que HAY
     * para que no crezca. Los numeros de hoy son +1,40 dB (solo ruido) y
     * +2,71 dB (con voz a 6 dB). El tope se pone un poco por encima, no
     * mucho: holgura para el ruido de medida, no para una regresion.
     */
    {
        char det[128];
        snprintf(det, sizeof det, "(hoy +%.2f dB, tope 0,90)", peor_v);
        di("el reductor no añade mas vaiven que el de hoy", peor_v < 0.90, det);
        snprintf(det, sizeof det, "(hoy +%.2f dB, tope 1,80)", peor_p);
        di("ni mas contraste de picos que el de hoy", peor_p < 1.80, det);
    }

    printf("\n  %s\n\n", fallos ? "*** HAY FALLOS ***" : "todo bien");
    return fallos ? 1 : 0;
}
