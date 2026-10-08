/*
 * ECUALIZADOR ADAPTATIVO COMPLEJO - 07/10/2026
 *
 * *** El dueño: "stanag 4415 no lo decodificamos?" ... "si el 4415 va sin
 * cifrar sacarias texto?" ... y, sobre leer el codigo de suscan: "leemos y
 * escribimos". ***
 *
 * LO QUE FALTA PARA LAS DOS STANAG. La radio DETECTA el 4539 y el 4415 desde
 * hace semanas -el modo IDENT los saca por la trama- y no demodula ninguno de
 * los dos. El motivo no es el FEC ni el entrelazado: es que los dos son
 * "tono serie", una sola portadora a 1800 Hz con 2400 simbolos por segundo,
 * y a 2400 baudios en HF el eco de la ionosfera dura VARIOS simbolos. Cada
 * simbolo llega sumado a los dos o tres anteriores. Sin deshacer eso, la
 * constelacion es una nube y no hay decision que tomar.
 *
 * Un ecualizador es un filtro FIR cuyos coeficientes se buscan solos para
 * que la salida se parezca a lo que deberia ser. Aqui hay dos formas de
 * decirle "a lo que deberia ser":
 *
 *   CMA (Godard, 1980). A ciegas, sin saber que se transmite. Se apoya en
 *   que una PSK tiene MODULO constante: cualquier cosa que haga que |y| se
 *   aparte de su radio nominal es culpa del canal. El error no mira la fase,
 *   asi que converge sin saber donde esta la constelacion -y por eso deja la
 *   salida girada un angulo cualquiera, que lo arregla despues el lazo de
 *   portadora-. Es lo unico que sirve para ENGANCHAR.
 *
 *   DIRIGIDO POR DECISION. Una vez abierto el ojo, el error es la distancia
 *   al simbolo mas cercano. Converge mucho mas fino que CMA, pero si se
 *   arranca con el ojo cerrado las decisiones son basura y se realimenta la
 *   basura. Es lo que sirve para AFINAR, nunca para empezar.
 *
 * Asi que el camino es: CMA hasta que el error de modulo baje, y entonces
 * dirigido. ecu_dispersion() es el numero con el que se decide el cambio, y
 * no es un umbral inventado: es el propio coste de Godard, que vale cero
 * exactamente cuando el modulo es el nominal.
 *
 * DE DONDE SALE ESTO. Lei sigutils/equalizer.h de Gonzalo J. Carracedo
 * (BatchDrake) para entender como lo monta el -un su_iir_filt con los pesos
 * moviendose-, y el permiso que da su licencia LGPLv3 NO se ha usado: aqui
 * no hay ni una linea copiada. Esta implementacion es de longitud fija, sin
 * reservar memoria, con los dos algoritmos en vez de uno, y en coma flotante
 * de 32 bits porque este Cortex-M4 tiene FPU de simple precision y no de
 * doble. El dueño: "leemos y escribimos".
 */
#ifndef ECUALIZ_H
#define ECUALIZ_H

#include <stdint.h>

/* Maximo de coeficientes. 32 a 2400 baudios son 13,3 ms de memoria, que
 * cubre el retardo multitrayecto tipico de HF a media distancia. Subirlo
 * cuesta memoria y, sobre todo, RUIDO: cada coeficiente de mas es ruido de
 * mas sumado a la salida. */
#define ECU_TAPS_MAX  32U

#define ECU_CIEGO     0U   /* CMA */
#define ECU_DIRIGIDO  1U

typedef struct {
    float   wr[ECU_TAPS_MAX], wi[ECU_TAPS_MAX];  /* los coeficientes */
    float   xr[ECU_TAPS_MAX], xi[ECU_TAPS_MAX];  /* la linea de retardo */
    uint8_t n;        /* coeficientes en uso */
    uint8_t ptr;      /* cabeza del circular */
    uint8_t modo;     /* ECU_CIEGO | ECU_DIRIGIDO */
    float   mu;       /* paso de adaptacion */
    float   r2;       /* radio de Godard: E[|a|^4]/E[|a|^2] de la constelacion */
    float   disp;     /* coste de Godard suavizado */
    float   err;      /* error de decision suavizado (solo en dirigido) */
} ecu_t;

/* taps impar o par da igual; el coeficiente central arranca a 1 y el resto a
 * cero, o sea "no toques nada" - el unico arranque que no estropea una señal
 * que ya venia limpia. r2 vale 1.0 para cualquier PSK de modulo unidad. */
void  ecu_init(ecu_t *e, uint8_t taps, float mu, float r2);
void  ecu_reset(ecu_t *e);
void  ecu_modo(ecu_t *e, uint8_t modo);

/*
 * Cambiar el paso en caliente. Hace falta de verdad, y lo dice la medida:
 * con el MISMO mu, el dirigido NO mejora al CMA -20,2 dB contra 20,3 a SNR
 * 20-, porque lo que limita a los dos no es el criterio sino el ruido de
 * gradiente, que solo depende de mu. La ventaja del dirigido es que, con el
 * ojo ya abierto, AGUANTA un mu mas pequeño sin perder el enganche; el CMA
 * con ese mu tardaria una eternidad en converger. Asi que el paso a
 * dirigido va siempre acompañado de bajar el paso. Ver sim/ecualiztest.c.
 */
void  ecu_paso(ecu_t *e, float mu);

/* Mete un simbolo y saca el ecualizado. NO adapta: adaptar es ecu_corrige(),
 * que va aparte porque quien llama necesita el y para decidir el simbolo
 * antes de poder decirnos cual era. */
void  ecu_mete(ecu_t *e, float xr, float xi, float *yr, float *yi);

/* Adapta con la ultima muestra metida. En ECU_CIEGO los dos ultimos
 * argumentos se ignoran; en ECU_DIRIGIDO son el simbolo decidido. */
void  ecu_corrige(ecu_t *e, float yr, float yi, float dr, float di);

/* Lo que se usa para decidir el salto a dirigido: baja segun se abre el ojo. */
float ecu_dispersion(const ecu_t *e);
float ecu_error(const ecu_t *e);

/* Energia total de los coeficientes. Si se dispara, el lazo ha divergido
 * -mu demasiado grande- y hay que reiniciar. Quien lo use TIENE que mirarlo:
 * un ecualizador divergido no avisa, solo deja de sacar nada. */
float ecu_energia(const ecu_t *e);

#endif /* ECUALIZ_H */
