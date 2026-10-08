#include "bip.h"
#include "nco.h"

/* Lo que se oye. 9000 de los 32767 que caben: se oye claro sin saturar y
 * sin que el salto respecto al audio normal -que el AGC deja cerca de
 * 18000 de pico- de un susto. */
#define BIP_AMPL   9000.0f

/* Subida y bajada, en milisegundos. Ver el comentario de bip.h. */
#define BIP_RAMPA_MS  5U

typedef struct {
    uint16_t hz;
    uint16_t ms;
} tramo_t;

/*
 * REVISION A FONDO DEL 08/10/2026: LOS TRES QUE CRUZAN CON LA
 * INTERRUPCION DE AUDIO, VOLATILE.
 *
 * s_n lo escribe el bucle principal (bip_pide) y lo lee la interrupcion
 * (bip_mete); s_i y s_abierto los escribe la interrupcion y los lee el
 * bucle principal (bip_sonando, y el propio bip_pide para decidir si
 * vaciar la cola). Sin volatile el compilador puede quedarse cualquiera
 * de los tres en un registro -el atajo del principio de bip_mete() y el
 * bucle de muestras los leen varias veces seguidas- y entonces el cambio
 * del otro lado no se ve: el pitido no suena, o la cola no se vacia y se
 * queda muda para siempre, que es EXACTAMENTE el sintoma que ya se pago
 * el 05/10/2026 y que el comentario de abajo cuenta. Un sintoma asi no
 * apunta a su causa ni de lejos.
 */
static tramo_t  s_cola[BIP_TRAMOS];
static volatile uint8_t  s_n;   /* cuantos tramos hay encolados */
static volatile uint8_t  s_i;   /* cual suena */
static uint32_t s_quedan;       /* marcos que le quedan al tramo de ahora */
static uint32_t s_total;        /* los que tenia al empezar, para la rampa */
static uint32_t s_rampa;        /* marcos de subida/bajada */
static uint32_t s_fase;         /* acumulador del oscilador, vuelta = 2^32 */
static uint32_t s_inc;
static volatile uint8_t  s_abierto;  /* hay un tramo cargado */

/*
 * LA COLA SE VACIA AQUI, NO EN LA INTERRUPCION - 05/10/2026.
 *
 * *** El dueño: "la segunda captura no ha hecho el pitido ni de inicio ni
 * de fin". ***
 *
 * La cola la vaciaba bip_mete(), desde la interrupcion, cuando se encontraba
 * sin tramos DENTRO del bucle de muestras. Pero si el ultimo tramo acababa
 * justo con la ultima muestra del bloque, el bucle terminaba antes de llegar
 * ahi y la siguiente llamada se salia por el atajo de arriba del todo sin
 * vaciar nada. Y eso no es raro: los 120 ms del pitido de "hecho" son 11.520
 * muestras a 96 kHz, o sea 45 bloques de 256 EXACTOS. Pasaba siempre.
 *
 * Resultado: s_i se quedaba igual a s_n, y como el aviso de empezar son tres
 * tramos y el de acabar uno, despues de UNA captura la cola marcaba cuatro
 * de cuatro -BIP_TRAMOS- y todos los bip_pide() siguientes se iban por el
 * "la cola esta llena". Muda para siempre hasta reiniciar.
 *
 * Ahora vacia el que encola, que es el bucle principal, y la interrupcion
 * solo adelanta s_i. Asi solo hay un escritor de s_n y el orden es el que
 * tiene que ser: primero s_n a cero -con eso la interrupcion ya no mira la
 * cola-, luego s_i, luego el tramo, y s_n al final.
 */
void bip_pide(uint16_t hz, uint16_t ms)
{
    if (ms == 0U) { return; }
    if (!s_abierto && s_i >= s_n) {
        s_n = 0U;
        s_i = 0U;
    }
    if (s_n >= (uint8_t)BIP_TRAMOS) { return; }
    /*
     * El orden importa: primero los datos del tramo y DESPUES el contador
     * que lo hace visible. Al reves, la interrupcion podria encontrarse un
     * tramo contado pero sin rellenar y sonaria lo que hubiera antes.
     */
    s_cola[s_n].hz = hz;
    s_cola[s_n].ms = ms;
    /* Y el orden hay que EXIGIRLO, no solo escribirlo en este orden: sin
     * la barrera el compilador puede adelantar la subida de s_n por
     * delante del relleno del tramo, que es justo lo que el parrafo de
     * aqui arriba dice que no puede pasar. (Revision a fondo del
     * 08/10/2026.) */
    __asm volatile ("" ::: "memory");
    s_n = (uint8_t)(s_n + 1U);
}

void bip_listo(void)
{
    bip_pide(1200U, 120U);
}

void bip_empieza(void)
{
    bip_pide(900U, 70U);
    bip_pide(0U, 60U);
    bip_pide(900U, 70U);
}

void bip_error(void)
{
    bip_pide(300U, 250U);
}

uint8_t bip_sonando(void)
{
    return (uint8_t)(s_abierto || (s_i < s_n));
}

void bip_mete(int16_t *estereo, uint32_t n, float fs_hz)
{
    uint32_t k;

    /* Lo normal: no hay nada que sonar y esto no cuesta una comparacion por
     * muestra sino una por bloque. */
    if (!s_abierto && s_i >= s_n) { return; }
    if (estereo == 0 || n == 0U || fs_hz <= 0.0f) { return; }

    for (k = 0U; k < n; k++) {
        float v;

        if (!s_abierto) {
            if (s_i >= s_n) {
                /* Se acabo la secuencia. La cola NO se toca aqui: la vacia
                 * bip_pide(), ver el comentario de ahi arriba. */
                return;
            }
            s_total  = (uint32_t)(fs_hz * (float)s_cola[s_i].ms * 0.001f);
            if (s_total == 0U) { s_total = 1U; }
            s_quedan = s_total;
            s_rampa  = (uint32_t)(fs_hz * (float)BIP_RAMPA_MS * 0.001f);
            if (s_rampa * 2U > s_total) { s_rampa = s_total / 2U; }
            if (s_cola[s_i].hz == 0U) {
                s_inc = 0U;
            } else {
                /* El incremento de fase: hz/fs de vuelta por muestra, en la
                 * rejilla de 2^32. En double, por la misma razon que
                 * nco_freq(): 2^32 no cabe en los 24 bits de un float. */
                double f = (double)s_cola[s_i].hz / (double)fs_hz;
                s_inc = (uint32_t)(f * 4294967296.0 + 0.5);
            }
            s_fase = 0U;
            s_abierto = 1U;
            s_i = (uint8_t)(s_i + 1U);
        }

        if (s_inc == 0U) {
            v = 0.0f;                     /* tramo de silencio */
        } else {
            float sen, cos_;
            float env = 1.0f;

            nco_sen_cos_fase(s_fase, &sen, &cos_);
            s_fase += s_inc;

            if (s_rampa > 0U) {
                uint32_t hechos = s_total - s_quedan;
                if (hechos < s_rampa) {
                    env = (float)hechos / (float)s_rampa;
                } else if (s_quedan < s_rampa) {
                    env = (float)s_quedan / (float)s_rampa;
                }
            }
            v = sen * env * BIP_AMPL;
        }

        {
            int32_t o = (int32_t)v;
            if (o > 32767)  { o = 32767; }
            if (o < -32768) { o = -32768; }
            estereo[2U * k]      = (int16_t)o;
            estereo[2U * k + 1U] = (int16_t)o;
        }

        s_quedan--;
        if (s_quedan == 0U) { s_abierto = 0U; }
    }
}
