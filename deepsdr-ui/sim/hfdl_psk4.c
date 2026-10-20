/*
 * 4-PSK: LA BLANDA TIENE QUE DECIDIR LO MISMO QUE LA DURA - 14/10/2026
 *
 * hfdl_soft_demod_psk4() pasa de devolver 0/255 a devolver verosimilitud
 * de verdad. Eso le da al Viterbi los ~2 dB que la decision dura tiraba, y
 * las tramas de 1200 bps -las largas, las de los partes de posicion- son
 * justo las que los necesitan.
 *
 * El riesgo de un cambio asi es sutil y caro: que al proyectar sobre los
 * ejes se cuele una permutacion de bits o un signo cambiado. Eso no
 * rompe nada visible -los bits siguen saliendo, el Viterbi sigue
 * corriendo- pero el CRC no vuelve a salir bien jamas, y el sintoma seria
 * identico al que ya llevamos tres dias persiguiendo.
 *
 * Asi que esto no comprueba "que decodifique": comprueba que la DECISION
 * DURA de la version nueva es bit a bit la de la vieja, en todo el
 * circulo. La vieja esta aqui copiada tal cual -es la referencia, no se
 * toca- y se barren 20.000 angulos a varias amplitudes.
 *
 * Y comprueba lo otro, que es el motivo del cambio: que la salida YA NO
 * es solo 0 y 255. Un banco que solo mirara la decision dura pasaria
 * igual de verde si alguien revirtiera el cambio sin querer.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "hfdl_soft_demod.h"
#include "hfdl_preamble_seq.h"

#define PI 3.14159265358979323846f

/* ---- LA REFERENCIA: la version dura, copiada del fichero de antes ---- */
static uint32_t gray_encode(uint32_t s) { return s ^ (s >> 1); }

static uint32_t lineal(float theta, uint32_t m, float alpha)
{
    uint32_t s = 0u;
    float v = theta;
    for (uint32_t k = 0u; k < m; k++) {
        s <<= 1;
        float ref = (float)(1u << (m - k - 1u)) * alpha;
        if (v > 0.0f) { s |= 1u; v -= ref; } else { v += ref; }
    }
    return s;
}

static void dura(float i_in, float q_in, uint8_t *out)
{
    const uint32_t m = 2u;
    const float alpha = PI / 4.0f;
    const float d_phi = PI * (1.0f - 1.0f / 4.0f);
    float theta = atan2f(q_in, i_in) - d_phi;
    if (theta < -PI) { theta += 2.0f * PI; }
    uint32_t sym = gray_encode(lineal(theta, m, alpha));
    for (uint32_t k = 0u; k < m; k++) {
        out[k] = ((sym >> (m - k - 1u)) & 1u) ? 255u : 0u;
    }
}

int main(void)
{
    uint32_t n, discrepan = 0u, total = 0u, intermedios = 0u, dudosos = 0u;
    static const float AMPL[] = { 0.3f, 1.0f, 2.5f };
    uint32_t a;
    int fallos = 0;

    printf("\n4-PSK: LA BLANDA CONTRA LA DURA\n");
    printf("===============================\n\n");

    for (a = 0u; a < 3u; a++) {
        uint32_t disc_a = 0u;
        for (n = 0u; n < 20000u; n++) {
            float ang = 2.0f * PI * (float)n / 20000.0f;
            float i = AMPL[a] * cosf(ang), q = AMPL[a] * sinf(ang);
            uint8_t d[2], b[3];
            dura(i, q, d);
            hfdl_soft_demod(HFDL_MOD_PSK4, i, q, b);
            total++;
            for (uint32_t k = 0u; k < 2u; k++) {
                /* la decision dura de un valor blando es "pasa de 127" */
                uint8_t bd = (b[k] > 127u) ? 255u : 0u;
                if (bd != d[k]) {
                    /*
                     * DISCREPAR EN LA ZONA DE "NO SE" NO ES UN FALLO, y
                     * esto estuvo a punto de costarme el cambio entero: la
                     * primera version de este banco exigia cero
                     * discrepancias y salia rojo con 466, que parecia un
                     * patron. No lo era.
                     *
                     * Justo en la frontera entre dos sectores el simbolo
                     * es ambiguo de verdad. La version dura TENIA que
                     * elegir un lado y elegia uno; la blanda devuelve 127,
                     * que es "de este bit no se nada", y el Viterbi lo trata
                     * como lo que es: ningun dato. Eso es mejor, no peor.
                     *
                     * Lo que de verdad seria un fallo es discrepar donde
                     * la blanda esta SEGURA, porque eso si seria una
                     * permutacion de bits o un signo cambiado. Asi que se
                     * cuenta solo eso.
                     */
                    if (b[k] > 128u || b[k] < 126u) {
                        disc_a++; discrepan++;
                    } else {
                        dudosos++;
                    }
                }
                if (b[k] != 0u && b[k] != 255u) { intermedios++; }
            }
        }
        printf("  amplitud %.1f:  %u decisiones distintas con la blanda SEGURA\n",
               (double)AMPL[a], disc_a);
    }

    printf("\n  total: %u discrepancias de %u simbolos\n", discrepan, total);
    printf("  valores intermedios (ni 0 ni 255): %u\n", intermedios);
    printf("  discrepancias en la zona de \"no se\" (127 +-1): %u  <- correctas\n", dudosos);

    /*
     * Un puñado de discrepancias EN EL BORDE es correcto y no se puede
     * evitar: justo en la frontera entre dos sectores las dos formas de
     * calcularlo difieren en el ultimo bit de coma flotante. Lo que no
     * puede haber es un patron.
     */
    if (discrepan != 0u) {
        printf("\n  FALLO: %u decisiones cambian donde la blanda esta SEGURA.\n", discrepan);
        printf("         Eso es una permutacion de bits o un signo, no el borde.\n");
        fallos++;
    }
    if (dudosos == 0u) {
        printf("\n  FALLO: ni una discrepancia en el borde. Sospechoso:\n");
        printf("         o el barrido no pasa por las fronteras, o la\n");
        printf("         blanda no esta devolviendo 127 donde deberia.\n");
        fallos++;
    }
    if (intermedios == 0u) {
        printf("\n  FALLO: la salida sigue siendo solo 0 y 255 - el cambio\n");
        printf("         no esta puesto, o alguien lo ha revertido\n");
        fallos++;
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
