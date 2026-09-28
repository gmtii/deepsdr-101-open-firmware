#include "wspr_fano.h"
#include <string.h>

#define WSPR_POLY1 0xF2D05351UL
#define WSPR_POLY2 0xE4613C47UL

/*
 * EL SESGO, que es lo que hace que esto funcione.
 *
 * La pila compara caminos de PROFUNDIDAD DISTINTA. Sin sesgo, un camino
 * de diez ramas siempre pinta mejor que uno de ochenta por el simple
 * hecho de haber sumado menos veces, y la busqueda se queda dando vueltas
 * arriba del arbol sin bajar nunca.
 *
 * El sesgo se resta en cada bit de canal, asi que un camino BUENO sube
 * (acierta y gana mas de lo que paga) y uno MALO baja (falla y paga
 * igual). Ese es el criterio entero.
 *
 * 40 sobre una escala de +-127 sale de las pruebas: con menos, los
 * caminos malos no bajan lo bastante deprisa y la pila se llena de
 * basura; con mucho mas, un bit dudoso del camino bueno lo hunde. Ver
 * sim/wspr_pruebas.c, que lo mide con ruido de verdad.
 */
#define WSPR_SESGO   40

/* Tope de pasos. A 110 segundos por emision sobra tiempo, pero no sobra
 * para siempre: esto es lo que convierte "no se ha podido" en una
 * respuesta en vez de en un cuelgue. */
#define WSPR_PASOS_MAX  200000UL

/*
 * Cuanto tiene que cuadrar lo decodificado con lo recibido para creerselo,
 * en tanto por ciento. El numero sale del banco, que mide las dos
 * poblaciones por separado: con señal de verdad sale muy por encima, y
 * con ruido puro se queda cerca de la mitad porque lo unico que ha hecho
 * el decodificador es elegir la palabra de codigo menos mala de entre dos
 * elevado a cincuenta.
 *
 * MEDIDO, no elegido: 40 intentos por nivel de ruido contra 60 tandas de
 * ruido puro.
 *
 *     señal de verdad, del ruido 20 al 180 ....  93 .. 100
 *     ruido puro, sin nada debajo .............  74 ..  83
 *
 * 88 cae en medio con diez puntos de margen a cada lado. Ver
 * sim/wspr_pruebas.c, que imprime las dos poblaciones cada vez que corre
 * para que el dia que dejen de estar separadas se vea.
 */
#define WSPR_CUADRA_MIN  88U

typedef struct {
    int32_t  metrica;
    uint32_t reg;          /* los ultimos 32 bits de entrada */
    uint8_t  hondo;        /* cuantos bits de entrada lleva decididos */
    uint8_t  bits[11];     /* el camino: 81 bits, uno por rama */
} nodo_t;

/*
 * Y QUE MIDA LO QUE DICE QUE MIDE.
 *
 * Aqui habia un `hueco[4]` puesto para "cuadrar a 20 bytes" cuando los
 * campos ya sumaban 20 justos: el nodo medía 24 y el decodificador
 * escribia 512*24 en un sitio de 512*20. Lo cazo el banco con un vuelco
 * de memoria en cuanto la pila se lleno por primera vez -o sea, en cuanto
 * hubo ruido de verdad-, que es la mejor forma posible de que salga: en
 * la radio habria sido escribir 2 kB encima de lo que hubiera al lado.
 *
 * Con esto, el dia que alguien anada un campo, el error sale al compilar
 * y no al escuchar.
 */
_Static_assert(sizeof(nodo_t) == WSPR_FANO_NODO_B,
               "el nodo ya no mide lo que dice WSPR_FANO_NODO_B");

static uint8_t paridad(uint32_t v)
{
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (uint8_t)(v & 1U);
}

/* Lo que aporta un bit de canal esperado `b` frente a lo que se recibio. */
static int32_t aporta(uint8_t b, int8_t v)
{
    return (b ? (int32_t)v : -(int32_t)v) - WSPR_SESGO;
}

static void pon_bit(uint8_t *bits, uint8_t i, uint8_t b)
{
    if (b) { bits[i >> 3] |= (uint8_t)(0x80U >> (i & 7U)); }
    else   { bits[i >> 3] &= (uint8_t)~(0x80U >> (i & 7U)); }
}

/*
 * CUANTO SE PARECE lo decodificado a lo que se recibio, en tanto por
 * ciento ponderado por la confianza de cada bit.
 *
 * Se pondera con |v| a proposito: un bit que llego con un +3 no dice casi
 * nada y no tiene por que contar igual que uno que llego con un +120.
 * 100 = todos los bits coinciden y todos llegaron seguros; 50 = lo que
 * sale de tirar una moneda.
 */
static uint8_t cuadra(const uint8_t bits[7],
                      const int8_t blandos[2U * WSPR_BITS_ENTRA])
{
    uint32_t reg = 0UL;
    int32_t  suma = 0, total = 0;
    uint8_t  i, n = 0U;

    for (i = 0U; i < (uint8_t)WSPR_BITS_ENTRA; i++) {
        uint8_t b = (i < (uint8_t)WSPR_BITS_MSG)
                  ? (uint8_t)((bits[i >> 3] >> (7U - (i & 7U))) & 1U)
                  : 0U;
        uint8_t j;

        reg = (reg << 1) | (uint32_t)b;
        for (j = 0U; j < 2U; j++) {
            uint8_t esp = paridad(reg & ((j == 0U) ? WSPR_POLY1 : WSPR_POLY2));
            int32_t v = (int32_t)blandos[n];
            int32_t a = (v < 0) ? -v : v;

            suma  += esp ? v : -v;
            total += a;
            n++;
        }
    }
    if (total <= 0) { return 0U; }
    /* De [-total, +total] a [0, 100]. */
    return (uint8_t)(((suma + total) * 50) / total);
}

uint8_t wspr_fano(const int8_t blandos[2U * WSPR_BITS_ENTRA],
                  void *ram, uint8_t bits[7], uint32_t *pasos_out)
{
    uint8_t q = 0U;
    uint8_t r = wspr_fano_ex(blandos, ram, bits, pasos_out, &q);

    return (uint8_t)(r && (q >= WSPR_CUADRA_MIN));
}

uint8_t wspr_fano_ex(const int8_t blandos[2U * WSPR_BITS_ENTRA],
                     void *ram, uint8_t bits[7], uint32_t *pasos_out,
                     uint8_t *cuadra_out)
{
    nodo_t  *pila = (nodo_t *)ram;
    uint16_t n = 0U;            /* cuantos hay en la pila */
    uint32_t pasos = 0UL;

    if (pasos_out != (uint32_t *)0) { *pasos_out = 0UL; }
    if (cuadra_out != (uint8_t *)0) { *cuadra_out = 0U; }
    if (ram == (void *)0) { return 0U; }

    /* La raiz: nada decidido, registro a cero, metrica cero. */
    memset(&pila[0], 0, sizeof pila[0]);
    n = 1U;

    while (n > 0U && pasos < WSPR_PASOS_MAX) {
        uint16_t mejor = 0U, i;
        nodo_t   padre;
        uint8_t  b, ultimo;

        pasos++;

        /* El de mejor metrica. Recorrer los 512 es mas barato que
         * mantener un monticulo y mucho mas facil de creerse. */
        for (i = 1U; i < n; i++) {
            if (pila[i].metrica > pila[mejor].metrica) { mejor = i; }
        }
        padre = pila[mejor];

        /* Se saca: el hueco lo tapa el ultimo. */
        n--;
        if (mejor != n) { pila[mejor] = pila[n]; }

        if (padre.hondo >= (uint8_t)WSPR_BITS_ENTRA) {
            /*
             * Llegamos al final del arbol por el camino que mejor pinta.
             * Pero "el que mejor pinta" NO quiere decir "el bueno": el
             * codigo de WSPR no lleva CRC, asi que con ruido puro delante
             * esto siempre encuentra ALGUNA palabra de codigo y la
             * devuelve tan contento.
             *
             * *** Lo cazo el banco: 16 de 30 tandas de ruido salieron
             * como indicativos con su localizador y su potencia. ***
             * Y un indicativo inventado es el peor fallo posible aqui,
             * porque en la pantalla no se distingue de uno bueno.
             *
             * Asi que se corrobora: se vuelven a codificar los 50 bits y
             * se mira CUANTO se parecen los 162 bits de canal que salen a
             * los que se recibieron. Un decodificado de verdad coincide
             * casi en todo; uno pescado del ruido coincide en poco mas de
             * la mitad, porque lo unico que ha hecho es buscar la palabra
             * de codigo que menos mal encajaba.
             */
            uint8_t k;

            memset(bits, 0, 7);
            for (k = 0U; k < (uint8_t)WSPR_BITS_MSG; k++) {
                pon_bit(bits, k, (uint8_t)((padre.bits[k >> 3] >> (7U - (k & 7U))) & 1U));
            }
            if (pasos_out != (uint32_t *)0) { *pasos_out = pasos; }
            if (cuadra_out != (uint8_t *)0) { *cuadra_out = cuadra(bits, blandos); }
            return 1U;
        }

        /*
         * Los 31 ultimos bits de entrada son cola de ceros conocida, asi
         * que ahi el arbol NO se abre: una sola rama. Es la mitad del
         * trabajo que se ahorra, y ademas es lo que obliga al camino a
         * terminar donde tiene que terminar.
         */
        ultimo = (uint8_t)((padre.hondo >= (uint8_t)WSPR_BITS_MSG) ? 0U : 1U);

        for (b = 0U; b <= ultimo; b++) {
            nodo_t  *h;
            uint32_t reg = (padre.reg << 1) | (uint32_t)b;
            uint8_t  p1 = paridad(reg & WSPR_POLY1);
            uint8_t  p2 = paridad(reg & WSPR_POLY2);
            int32_t  m = padre.metrica
                       + aporta(p1, blandos[2U * padre.hondo])
                       + aporta(p2, blandos[2U * padre.hondo + 1U]);

            if (n < (uint16_t)WSPR_FANO_NODOS) {
                h = &pila[n];
                n++;
            } else {
                /*
                 * La pila esta llena: se tira el PEOR, que es el camino
                 * que menos futuro tiene. Si el nuevo es todavia peor que
                 * ese, no entra. Perder el peor es perder un camino que
                 * casi seguro no era; no hacerlo seria quedarse sin sitio
                 * para el bueno.
                 */
                uint16_t peor = 0U;

                for (i = 1U; i < n; i++) {
                    if (pila[i].metrica < pila[peor].metrica) { peor = i; }
                }
                if (m <= pila[peor].metrica) { continue; }
                h = &pila[peor];
            }

            h->metrica = m;
            h->reg     = reg;
            h->hondo   = (uint8_t)(padre.hondo + 1U);
            memcpy(h->bits, padre.bits, sizeof h->bits);
            pon_bit(h->bits, padre.hondo, b);
        }
    }

    if (pasos_out != (uint32_t *)0) { *pasos_out = pasos; }
    return 0U;   /* se rinde: mejor eso que inventarse un indicativo */
}
