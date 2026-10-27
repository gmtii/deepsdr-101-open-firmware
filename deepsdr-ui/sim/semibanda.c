/*
 * ======================================================================
 * EL DIEZMADOR DE MEDIO BANDA, MEDIDO - 23/10/2026
 * ======================================================================
 *
 * Esto es lo que deja entrar los 192 kHz sin que la cadena de audio se
 * entere (ver User/semibanda.h). Lo que se mide, y por que justo esto:
 *
 *   1) QUE NO TOQUE LO QUE TIENE QUE DEJAR PASAR. Un tono dentro de la
 *      banda de paso tiene que salir con la misma amplitud: cualquier
 *      rizado aqui se oye como un cambio de nivel al mover el dial.
 *
 *   2) QUE MATE LO QUE SE VA A DOBLAR ENCIMA. Esta es la unica razon de
 *      ser del filtro. Diezmando por dos, lo que hay a 72 kHz cae sobre
 *      los 24 kHz donde vive la señal que se quiere oir. Se mide
 *      METIENDO el tono a 72 kHz y mirando cuanto sale a 24: el camino
 *      entero, no la respuesta del filtro por separado. Y tambien el
 *      caso peor, el del NCO de sintonia echado al tope: 56 kHz.
 *
 *   3) QUE I Y Q SE RETRASEN LO MISMO. Un retardo distinto entre los
 *      dos raíles es un error de fase, y el error de fase se come el
 *      rechazo de la banda imagen - que es lo que separa la emisora de
 *      su fantasma al otro lado del VFO.
 *
 *   4) QUE LOS BLOQUES SE PEGUEN. El filtro tiene treinta y cuatro muestras de
 *      historia. Sin guardarla entre bloques, cada frontera de bloque es
 *      un escalon: 375 chasquidos por segundo. Se mide partiendo el
 *      MISMO tono en bloques y comparandolo con el seguido.
 *
 *   5) Y QUE DIEZMAR EN SITIO NO SE PISE A SI MISMO, que es la trampa
 *      que tiene escribir la salida m en el indice m mientras se lee
 *      hasta el 2m.
 */
#include "semibanda.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define FS      192000.0
#define N_SAL   256U
#define N_ENT   (2U * N_SAL)

static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-62s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

/* Mete un tono complejo de `hz` y devuelve la amplitud que sale a la
 * salida, medida con un detector sincrono a `hz_sal` sobre los 96 kHz de
 * despues de diezmar. Se tiran los primeros bloques para que la historia
 * del filtro se llene. */
static double pasa(double hz, double hz_sal)
{
    float i[N_ENT], q[N_ENT];
    double sr = 0.0, si = 0.0;
    unsigned b, n, vistas = 0U;
    double fase = 0.0;
    const double dw = 2.0 * M_PI * hz / FS;

    semibanda_reset();

    for (b = 0U; b < 12U; b++) {
        for (n = 0U; n < N_ENT; n++) {
            i[n] = (float)cos(fase);
            q[n] = (float)sin(fase);
            fase += dw;
        }
        semibanda_diezma(i, q, N_SAL);

        if (b < 4U) { continue; }        /* historia llenandose */

        for (n = 0U; n < N_SAL; n++) {
            /* El detector va sobre la tasa de SALIDA: 96 kHz. */
            double w = 2.0 * M_PI * hz_sal * (double)vistas / (FS / 2.0);

            sr += (double)i[n] * cos(w) + (double)q[n] * sin(w);
            si += (double)q[n] * cos(w) - (double)i[n] * sin(w);
            vistas++;
        }
    }
    return sqrt(sr * sr + si * si) / (double)vistas;
}

static double db(double a) { return 20.0 * log10(a > 1e-15 ? a : 1e-15); }

int main(void)
{
    printf("\n== EL DIEZMADOR DE MEDIO BANDA 192 -> 96 kHz ==\n");

    /* ---------------------------------------------------------------- */
    printf("\n  1) lo que tiene que pasar, pasa entero\n\n");
    {
        static const double k[] = { 0.0, 1000.0, 6000.0, 12000.0, 24000.0,
                                    30000.0, 36000.0, 40000.0 };
        unsigned n;
        double peor = 0.0;

        for (n = 0; n < sizeof k / sizeof k[0]; n++) {
            double a = pasa(k[n], k[n]);
            double d = fabs(db(a));

            printf("     %6.1f kHz  %+7.3f dB\n", k[n] / 1000.0, db(a));
            if (d > peor) { peor = d; }
        }
        printf("     el peor: %.3f dB\n", peor);
        exige(peor < 0.05, "de la continua a 40 kHz -el tope del NCO-, menos de 0,05 dB");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  2) LO QUE SE DOBLA ENCIMA DE LOS 24 kHz DE LA SEÑAL\n\n");
    {
        /* Lo que entra a 72 kHz sale a 72-96 = -24 kHz. Se mira ahi. */
        /* 72 kHz es el caso normal -la señal en la frecuencia intermedia
         * baja- y 56 el peor: el NCO de sintonia echado hasta el tope de
         * NCO_OFF_MAX_96K. Ver User/semibanda.h. */
        static const double k[] = { 56000.0, 65000.0, 72000.0, 79000.0 };
        unsigned n;
        double peor = -200.0;

        for (n = 0; n < sizeof k / sizeof k[0]; n++) {
            double a = pasa(k[n], k[n] - 96000.0);

            printf("     entra a %5.1f kHz -> sale a %+6.1f kHz  %7.2f dB\n",
                   k[n] / 1000.0, (k[n] - 96000.0) / 1000.0, db(a));
            if (db(a) > peor) { peor = db(a); }
        }
        printf("     el que mas se cuela: %.2f dB\n", peor);
        exige(peor < -50.0, "la banda que cae sobre la señal, 50 dB abajo o mas");

        /* Y la referencia de que el metodo mide algo: sin filtro, un tono
         * a 72 kHz saldria a -24 con amplitud 1 (0 dB). */
        exige(db(pasa(24000.0, 24000.0)) > -0.05,
              "y a 24 kHz, donde vive la señal, no se pierde nada");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  3) I y Q se retrasan lo mismo\n\n");
    {
        float i[N_ENT], q[N_ENT];
        unsigned n;
        double peor = 0.0;

        /* Un impulso en los dos raíles a la vez: si el retardo fuera
         * distinto, las dos respuestas no coincidirian muestra a muestra. */
        semibanda_reset();
        memset(i, 0, sizeof i);
        memset(q, 0, sizeof q);
        i[48] = 1.0f;
        q[48] = 1.0f;
        semibanda_diezma(i, q, N_SAL);

        for (n = 0; n < N_SAL; n++) {
            double d = fabs((double)i[n] - (double)q[n]);

            if (d > peor) { peor = d; }
        }
        printf("     la mayor diferencia entre los dos raíles: %.3e\n", peor);
        exige(peor < 1e-9, "misma respuesta en I y en Q, muestra a muestra");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  4) LOS BLOQUES SE PEGAN (aqui es donde hace falta la historia)\n\n");
    {
        float ia[N_ENT], qa[N_ENT], ib[N_ENT], qb[N_ENT];
        float larga_i[2U * N_ENT], larga_q[2U * N_ENT];
        float partido_i[2U * N_SAL];
        unsigned n;
        double peor = 0.0, fase = 0.0;
        const double dw = 2.0 * M_PI * 7000.0 / FS;

        /*
         * SIN FORMULA DE RETARDO, Y A PROPOSITO. El primer intento
         * comparaba la juntura contra un coseno retrasado 6,5 muestras
         * calculado a mano, y lo que salio rojo fue el SIGNO de ese 6,5:
         * un fallo del banco, no del filtro. Comparar el mismo tono
         * procesado de dos maneras no necesita saber el retardo.
         */
        for (n = 0; n < 2U * N_ENT; n++) {
            larga_i[n] = (float)cos(fase);
            larga_q[n] = (float)sin(fase);
            fase += dw;
        }
        memcpy(ia, larga_i, sizeof ia);
        memcpy(qa, larga_q, sizeof qa);
        memcpy(ib, larga_i + N_ENT, sizeof ib);
        memcpy(qb, larga_q + N_ENT, sizeof qb);

        /* De una tirada. */
        semibanda_reset();
        semibanda_diezma(larga_i, larga_q, 2U * N_SAL);

        /* Y en dos bloques, que es como lo ve la radio. */
        semibanda_reset();
        semibanda_diezma(ia, qa, N_SAL);
        memcpy(partido_i, ia, N_SAL * sizeof(float));
        semibanda_diezma(ib, qb, N_SAL);
        memcpy(partido_i + N_SAL, ib, N_SAL * sizeof(float));

        for (n = 0; n < 2U * N_SAL; n++) {
            double d = fabs((double)partido_i[n] - (double)larga_i[n]);

            if (d > peor) { peor = d; }
        }
        printf("     partido en dos contra de una tirada: %.3e\n", peor);
        exige(peor < 1e-5, "los dos bloques dan lo mismo que un bloque doble");

        /* Y que la prueba mida algo: sin historia, la juntura salta. */
        {
            double salto = 0.0;

            semibanda_reset();
            memcpy(ia, larga_i, sizeof ia);
            memcpy(qa, larga_q, sizeof qa);
            memcpy(ib, larga_i + N_ENT, sizeof ib);
            memcpy(qb, larga_q + N_ENT, sizeof qb);
            semibanda_diezma(ia, qa, N_SAL);
            semibanda_reset();            /* <- la historia, tirada */
            semibanda_diezma(ib, qb, N_SAL);

            for (n = 0; n < 17U; n++) {
                double d = fabs((double)ib[n] - (double)larga_i[N_SAL + n]);

                if (d > salto) { salto = d; }
            }
            printf("     y tirando la historia a proposito:  %.3e\n", salto);
            exige(salto > 0.01, "tirando la historia la juntura salta (si no, no se mide nada)");
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\n  5) diezmar en sitio no se pisa a si mismo\n\n");
    {
        float i1[N_ENT], q1[N_ENT], i2[N_ENT], q2[N_ENT];
        float ref_i[N_SAL], ref_q[N_SAL];
        unsigned n;
        double peor = 0.0;

        /* Rampa: cada muestra distinta de todas las demas, asi que
         * cualquier muestra pisada cambia el resultado. */
        for (n = 0; n < N_ENT; n++) {
            i1[n] = i2[n] = (float)((double)n * 0.01);
            q1[n] = q2[n] = (float)(1000.0 - (double)n * 0.01);
        }

        /* La referencia: el mismo filtro a mano, sobre una COPIA intacta. */
        semibanda_reset();
        semibanda_diezma(i1, q1, N_SAL);
        memcpy(ref_i, i1, sizeof ref_i);
        memcpy(ref_q, q1, sizeof ref_q);

        /* Y otra vez, partiendo de los mismos datos: tiene que dar igual. */
        semibanda_reset();
        semibanda_diezma(i2, q2, N_SAL);

        for (n = 0; n < N_SAL; n++) {
            double a = fabs((double)i2[n] - (double)ref_i[n]);
            double b = fabs((double)q2[n] - (double)ref_q[n]);

            if (a > peor) { peor = a; }
            if (b > peor) { peor = b; }
        }
        exige(peor == 0.0, "repetible");

        /* Lo que de verdad prueba que no se pisa: comparar con el filtro
         * escrito a pelo sobre un buffer APARTE. */
        {
            static const float h[35] = {
                 0.002334458f, 0.0f, -0.004173014f, 0.0f,  0.007692380f, 0.0f,
                -0.013036249f, 0.0f,  0.021048924f, 0.0f, -0.033419058f, 0.0f,
                 0.054483553f, 0.0f, -0.100254585f, 0.0f,  0.315792748f,
                 0.499061685f,
                 0.315792748f, 0.0f, -0.100254585f, 0.0f,  0.054483553f, 0.0f,
                -0.033419058f, 0.0f,  0.021048924f, 0.0f, -0.013036249f, 0.0f,
                 0.007692380f, 0.0f, -0.004173014f, 0.0f,  0.002334458f };
            float ent[N_ENT];
            double peor2 = 0.0;

            for (n = 0; n < N_ENT; n++) { ent[n] = (float)((double)n * 0.01); }

            /* Solo las salidas cuya ventana entera cae dentro del bloque:
             * las primeras diecisiete miran a la historia, que es cero y
             * ya la mide la prueba 4. */
            for (n = 17U; n < N_SAL; n++) {
                double acc = 0.0;
                unsigned k;

                for (k = 0U; k < 35U; k++) {
                    int idx = (int)(2U * n) - (int)k;

                    if (idx >= 0) { acc += (double)h[k] * (double)ent[idx]; }
                }
                {
                    double d = fabs(acc - (double)ref_i[n]);

                    if (d > peor2) { peor2 = d; }
                }
            }
            printf("     contra el mismo filtro escrito a pelo: %.3e\n", peor2);
            exige(peor2 < 1e-3, "el diezmado en sitio da lo mismo que fuera de sitio");
        }
    }

    /* ---------------------------------------------------------------- */
    printf("\n  6) EL CAMINO DE VUELTA: 96 -> 192 kHz\n\n");
    {
        /*
         * QUE SE MIDE, Y POR QUE NO LO MISMO QUE AL BAJAR.
         *
         * Al subir, lo que entra es AUDIO: ya ha pasado por el filtro de
         * audio, seis kilohercios como mucho. Asi que lo unico que hay
         * que exigir es (a) que no toque lo que pasa y (b) que la IMAGEN
         * -la copia que crea meter ceros, en 192-f- se quede abajo. Pedir
         * aqui los 54 dB de banda ancha del diezmador seria pagar un
         * filtro de 35 tomas para tapar algo que nadie oye: eso es
         * exactamente lo que se entrego el 23/10 y lo que dejo la cascada
         * a rastras.
         */
        static const double k[] = { 300.0, 1000.0, 3000.0, 6000.0 };
        unsigned n;
        double peor_paso = 0.0, peor_img = -200.0;

        for (n = 0; n < sizeof k / sizeof k[0]; n++) {
            int16_t a[2048];
            double sr = 0.0, si = 0.0, ir = 0.0, ii = 0.0;
            unsigned b, m, vistas = 0u, sumadas = 0u;
            double fase = 0.0;
            const double dw = 2.0 * M_PI * k[n] / 96000.0;   /* entra a 96 */

            semibanda_reset();
            for (b = 0; b < 8u; b++) {
                for (m = 0; m < N_SAL; m++) {
                    int16_t v = (int16_t)(10000.0 * cos(fase));

                    a[2 * m]      = v;
                    a[2 * m + 1u] = v;
                    fase += dw;
                }
                semibanda_interpola(a, N_SAL);
                /* Los tres primeros no cuentan -la historia se esta
                 * llenando- pero `vistas` sigue corriendo: es el reloj
                 * del detector y no el numero de muestras sumadas.
                 * Dividir por `vistas` en vez de por las sumadas daba
                 * 5/8 = -4,08 dB en todo, que es lo que salia. */
                if (b < 3u) { vistas += 2u * N_SAL; continue; }
                for (m = 0; m < 2u * N_SAL; m++) {
                    /* El detector va a la tasa de SALIDA: 192 kHz. */
                    double w  = 2.0 * M_PI * k[n] * (double)vistas / 192000.0;
                    /*
                     * Y LA IMAGEN ESTA EN 96-f, NO EN 192-f.
                     *
                     * Meter un cero entre cada dos muestras repite el
                     * espectro de la entrada cada 96 kHz, asi que un tono
                     * de 3 kHz sale tambien en 93 y en 99. Lo de 192-f es
                     * el espejo que tiene cualquier señal real: no se
                     * puede quitar, no es la imagen, y buscarla ahi daba
                     * el mismo numero que la señal -que es justo lo que
                     * salia-.
                     */
                    double wi = 2.0 * M_PI * (96000.0 - k[n])
                              * (double)vistas / 192000.0;
                    double x  = (double)a[2 * m] / 10000.0;

                    sr += x * cos(w);  si -= x * sin(w);
                    ir += x * cos(wi); ii -= x * sin(wi);
                    vistas++;
                    sumadas++;
                }
            }
            {
                double amp = 2.0 * sqrt(sr * sr + si * si) / (double)sumadas;
                double img = 2.0 * sqrt(ir * ir + ii * ii) / (double)sumadas;
                double dp  = fabs(db(amp));

                printf("     %5.1f kHz  paso %+6.3f dB   imagen %7.2f dB\n",
                       k[n] / 1000.0, db(amp), db(img));
                if (dp  > peor_paso) { peor_paso = dp; }
                if (db(img) > peor_img) { peor_img = db(img); }
            }
        }
        exige(peor_paso < 0.2, "el audio sale con su nivel, hasta 6 kHz");
        exige(peor_img < -60.0, "y la imagen se queda 60 dB abajo o mas");
    }

    /* ---------------------------------------------------------------- */
    printf("\n  7) y el de vuelta tambien pega los bloques\n\n");
    {
        int16_t larga[4U * N_SAL * 2U];
        int16_t a[2048], b2[2048];
        int16_t partido[4U * N_SAL];
        unsigned m;
        double fase = 0.0, peor = 0.0;
        const double dw = 2.0 * M_PI * 3000.0 / 96000.0;

        for (m = 0; m < 2u * N_SAL; m++) {
            int16_t v = (int16_t)(10000.0 * cos(fase));

            larga[2 * m] = v; larga[2 * m + 1u] = v;
            fase += dw;
        }
        memcpy(a,  larga,                       N_SAL * 2u * sizeof(int16_t));
        memcpy(b2, larga + N_SAL * 2u,          N_SAL * 2u * sizeof(int16_t));

        semibanda_reset();
        semibanda_interpola(a, N_SAL);
        memcpy(partido, a, 2u * N_SAL * sizeof(int16_t));
        semibanda_interpola(b2, N_SAL);
        memcpy(partido + 2u * N_SAL, b2, 2u * N_SAL * sizeof(int16_t));

        semibanda_reset();
        semibanda_interpola(larga, 2u * N_SAL);

        for (m = 0; m < 4u * N_SAL; m++) {
            double d = fabs((double)partido[m] - (double)larga[m]);

            if (d > peor) { peor = d; }
        }
        printf("     partido en dos contra de una tirada: %.0f cuentas\n", peor);
        exige(peor == 0.0, "los dos bloques dan lo mismo que un bloque doble");
    }

    /* ------------------------------------------------------------------
     * 8) UN BLOQUE CORTO NO SE SALE DEL BUFFER
     * ------------------------------------------------------------------
     * La unica guarda de semibanda_interpola() es n_ent == 0. Con n_ent
     * pequeño, el bucle de las tres primeras muestras arrancaba en la 3
     * aunque esa trama no existiera, y semib_pon() escribe en los indices
     * 4m y 4m+2: con n_ent = 2 eso son el 12 y el 14 de un buffer de
     * ocho. Hoy no pasa -n_ent es siempre el bloque entero- pero la
     * funcion es publica y su cabecera no exige nada.
     *
     * Se comprueba con centinelas detras del hueco util: si alguien
     * escribe de mas, cambian.
     * ------------------------------------------------------------------ */
    printf("\n  8) un bloque corto no se sale del buffer\n\n");
    {
        static int16_t c[64];
        uint32_t ent, j;
        int malos = 0;

        for (ent = 1U; ent <= 4U; ent++) {
            uint32_t util = 4U * ent;   /* 2x tramas, estereo */

            for (j = 0U; j < 64U; j++) { c[j] = (int16_t)0x5A5A; }
            for (j = 0U; j < 2U * ent; j++) { c[j] = (int16_t)(100 * (int)j); }
            semibanda_interpola(c, ent);
            for (j = util; j < 64U; j++) {
                if (c[j] != (int16_t)0x5A5A) { malos++; break; }
            }
        }
        exige(malos == 0, "con 1, 2, 3 y 4 tramas no pisa nada de detras");
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
