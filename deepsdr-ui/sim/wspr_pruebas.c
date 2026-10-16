/*
 * WSPR: LA CAPA DE MENSAJE, DE PUNTA A PUNTA.
 *
 * Pedido por el usuario.
 *
 * Esto prueba lo unico que se puede probar sin antena y sin radio, que
 * resulta ser casi todo: se coge un mensaje, se empaqueta en sus 50 bits,
 * se codifica en 162 simbolos y se deshace el camino entero. Si algo del
 * protocolo esta mal escrito -el vector de sincronismo, el entrelazado,
 * las bases del indicativo- no sale el mismo mensaje.
 *
 * Y ese es el fallo que importa aqui: un WSPR mal empaquetado no falla,
 * DECODIFICA OTRO INDICATIVO. En la pantalla eso es un aficionado que no
 * ha transmitido nunca, y no hay forma de distinguirlo de uno bueno.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "wspr_msg.h"
#include "wspr_fano.h"
#include <stdlib.h>

static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-54s %s\n", que, cond ? "bien" : "MAL");
    if (!cond) { fallos++; }
}

static void ida_y_vuelta(const char *cal, const char *loc, uint8_t dbm)
{
    wspr_msg_t a, b;
    uint8_t bits[7];
    int ok;

    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    snprintf(a.indicativo, sizeof a.indicativo, "%s", cal);
    snprintf(a.locutor, sizeof a.locutor, "%s", loc);
    a.dbm = dbm;

    if (!wspr_empaqueta(&a, bits)) {
        printf("    %-8s %-5s %2u dBm   no se pudo empaquetar        MAL\n",
               cal, loc, dbm);
        fallos++;
        return;
    }
    ok = wspr_desempaqueta(bits, &b)
      && strcmp(a.indicativo, b.indicativo) == 0
      && strcmp(a.locutor, b.locutor) == 0
      && a.dbm == b.dbm;

    printf("    %-8s %-5s %2u dBm  ->  %-8s %-5s %2u dBm   %s\n",
           cal, loc, dbm, b.indicativo, b.locutor, b.dbm, ok ? "bien" : "MAL");
    if (!ok) { fallos++; }
}

int main(void)
{
    printf("\n  WSPR: LA CAPA DE MENSAJE\n");

    printf("\n  mensajes que tienen que sobrevivir la ida y la vuelta\n");
    ida_y_vuelta("EA4XYZ", "IN80", 37U);
    ida_y_vuelta("G0ABC",  "IO91", 23U);
    ida_y_vuelta("K1AB",   "FN42", 30U);
    ida_y_vuelta("W1AW",   "FN31", 60U);
    ida_y_vuelta("JA1ZZZ", "PM95",  0U);
    ida_y_vuelta("VK7AB",  "QE37", 10U);
    ida_y_vuelta("3A2ABC", "JN23", 27U);   /* el digito en el hueco 1 */
    ida_y_vuelta("EA1A",   "IN52", 20U);

    printf("\n  los que NO tienen forma de WSPR: hay que decir que no\n");
    {
        wspr_msg_t m; uint8_t b[7];
        memset(&m, 0, sizeof m);
        snprintf(m.locutor, sizeof m.locutor, "IN80"); m.dbm = 20U;

        snprintf(m.indicativo, sizeof m.indicativo, "EAXYZ");
        exige(!wspr_empaqueta(&m, b), "sin digito");
        snprintf(m.indicativo, sizeof m.indicativo, "EA");
        exige(!wspr_empaqueta(&m, b), "demasiado corto");
        snprintf(m.indicativo, sizeof m.indicativo, "EA4XYZW");
        exige(!wspr_empaqueta(&m, b), "demasiado largo");
        snprintf(m.indicativo, sizeof m.indicativo, "EA4XYZ");
        m.dbm = 99U;
        exige(!wspr_empaqueta(&m, b), "potencia imposible");
        m.dbm = 20U;
        snprintf(m.locutor, sizeof m.locutor, "ZZ99");
        exige(!wspr_empaqueta(&m, b), "localizador que no existe");
    }

    printf("\n  el entrelazado\n");
    {
        uint8_t a[WSPR_SIMBOLOS], b[WSPR_SIMBOLOS], c[WSPR_SIMBOLOS];
        uint8_t i;
        int igual = 1, movido = 0;

        for (i = 0U; i < (uint8_t)WSPR_SIMBOLOS; i++) { a[i] = i; }
        wspr_entrelaza(a, b, 0U);
        wspr_entrelaza(b, c, 1U);
        for (i = 0U; i < (uint8_t)WSPR_SIMBOLOS; i++) {
            if (a[i] != c[i]) { igual = 0; }
            if (a[i] != b[i]) { movido = 1; }
        }
        exige(igual, "deshacerlo devuelve exactamente lo que habia");
        exige(movido, "y hacerlo mueve algo, que si no no entrelaza nada");
    }

    printf("\n  los 162 simbolos\n");
    {
        wspr_msg_t m; uint8_t bits[7], sim[WSPR_SIMBOLOS];
        uint8_t i; int rango = 1, sincro = 1, unos = 0;

        memset(&m, 0, sizeof m);
        snprintf(m.indicativo, sizeof m.indicativo, "EA4XYZ");
        snprintf(m.locutor, sizeof m.locutor, "IN80");
        m.dbm = 37U;
        exige(wspr_empaqueta(&m, bits) != 0U, "se empaqueta");
        wspr_codifica(bits, sim);

        for (i = 0U; i < (uint8_t)WSPR_SIMBOLOS; i++) {
            if (sim[i] > 3U) { rango = 0; }
            /* El bit de menos peso de cada simbolo ES el sincronismo: eso
             * es lo que permite encontrar la emision sin saber nada del
             * mensaje. Si esto falla, el receptor no engancha nunca. */
            if ((sim[i] & 1U) != k_wspr_sincro[i]) { sincro = 0; }
            if (sim[i] >= 2U) { unos++; }
        }
        exige(rango, "todos caen en 0..3");
        exige(sincro, "y el bit bajo de cada uno es el sincronismo");
        /* Ni todo ceros ni todo unos: eso seria un codificador que no
         * codifica. Con 81 bits de entrada la mitad larga sale a uno. */
        exige(unos > 40 && unos < 122, "y los datos no son todos iguales");
        printf("      %d de 162 simbolos llevan un uno\n", unos);
    }

    /* ================= LA VUELTA ENTERA ================= */
    printf("\n  de mensaje a simbolos y de vuelta a mensaje\n");
    {
        static uint8_t ram[WSPR_FANO_RAM];
        wspr_msg_t a, b; uint8_t bits[7], sim[WSPR_SIMBOLOS];
        int8_t blandos[2 * WSPR_BITS_ENTRA];
        uint8_t desmez[WSPR_SIMBOLOS], canal[WSPR_SIMBOLOS], sal[7];
        uint32_t pasos = 0; uint8_t i; int ok;

        memset(&a, 0, sizeof a);
        snprintf(a.indicativo, sizeof a.indicativo, "EA4XYZ");
        snprintf(a.locutor, sizeof a.locutor, "IN80");
        a.dbm = 37U;
        (void)wspr_empaqueta(&a, bits);
        wspr_codifica(bits, sim);

        /* El receptor: quitar el sincronismo, deshacer el entrelazado y
         * convertir cada bit en un valor con signo. Sin ruido, +-127. */
        for (i = 0U; i < (uint8_t)WSPR_SIMBOLOS; i++) { canal[i] = (uint8_t)(sim[i] >> 1); }
        wspr_entrelaza(canal, desmez, 1U);
        for (i = 0U; i < (uint8_t)WSPR_SIMBOLOS; i++) {
            blandos[i] = desmez[i] ? (int8_t)127 : (int8_t)-127;
        }

        ok = wspr_fano(blandos, ram, sal, &pasos) && memcmp(sal, bits, 7) == 0;
        printf("      sin ruido: %u pasos\n", (unsigned)pasos);
        exige(ok, "los 50 bits vuelven exactamente igual");
        if (ok) {
            memset(&b, 0, sizeof b);
            exige(wspr_desempaqueta(sal, &b) != 0U, "y se desempaquetan");
            printf("      -> %s %s %u dBm\n", b.indicativo, b.locutor, b.dbm);
            exige(strcmp(a.indicativo, b.indicativo) == 0, "el indicativo es el mismo");
        }

        /* ---- y ahora con ruido, que es para lo que existe WSPR ---- */
        printf("\n  con ruido: cuanto aguanta\n");
        {
            int sigma;
            for (sigma = 20; sigma <= 160; sigma += 20) {
                int intento, bien = 0; unsigned pmax = 0;

                srand(12345u + (unsigned)sigma);
                for (intento = 0; intento < 20; intento++) {
                    int8_t bl[2 * WSPR_BITS_ENTRA];
                    uint8_t s2[7];
                    uint32_t ps = 0;
                    int k;

                    for (k = 0; k < 2 * (int)WSPR_BITS_ENTRA; k++) {
                        /* Ruido gaussiano a ojo: suma de cuatro uniformes. */
                        int r = 0, q;
                        for (q = 0; q < 4; q++) { r += (rand() % (2 * sigma + 1)) - sigma; }
                        r = r / 2 + (int)blandos[k];
                        if (r > 127) { r = 127; }
                        if (r < -127) { r = -127; }
                        bl[k] = (int8_t)r;
                    }
                    if (wspr_fano(bl, ram, s2, &ps) && memcmp(s2, bits, 7) == 0) { bien++; }
                    if (ps > pmax) { pmax = ps; }
                }
                printf("      ruido %3d  ->  %2d de 20 buenas   (hasta %u pasos)\n",
                       sigma, bien, pmax);
                if (sigma <= 40) {
                    exige(bien >= 19, "con poco ruido tiene que salir casi siempre");
                }
            }
        }

        /* ---- y lo que NO puede pasar: inventarse un mensaje ---- */
        printf("\n  ruido puro, sin ninguna señal debajo\n");
        {
            int intento, inventados = 0, llegaron = 0, qmin = 101, qmax = -1;
            uint8_t q = 0U;

            srand(999u);
            for (intento = 0; intento < 30; intento++) {
                int8_t bl[2 * WSPR_BITS_ENTRA];
                uint8_t s2[7];
                wspr_msg_t m;
                int k;

                for (k = 0; k < 2 * (int)WSPR_BITS_ENTRA; k++) {
                    bl[k] = (int8_t)((rand() % 255) - 127);
                }
                if (wspr_fano_ex(bl, ram, s2, 0, &q)) {
                    llegaron++;
                    if (q < qmin) { qmin = q; }
                    if (q > qmax) { qmax = q; }
                }
                if (wspr_fano(bl, ram, s2, 0) && wspr_desempaqueta(s2, &m)) {
                    inventados++;
                    printf("      se invento %s %s %u dBm\n", m.indicativo, m.locutor, m.dbm);
                }
            }
            printf("      %d de 30 llegaron al final del arbol, cuadrando %d..%d\n",
                   llegaron, qmin, qmax);
            printf("      y %d pasaron el umbral de %d\n", inventados, 88);
            /* Que salga ALGUNO no es un fallo del decodificador -el codigo
             * no lleva CRC y 30 tandas de ruido puro son muchas-, pero si
             * salen casi todas, el sesgo esta mal puesto. */
            /*
             * El decodificador SIEMPRE encuentra una palabra de codigo:
             * no lleva CRC y busca justamente eso. Lo que no puede pasar
             * es que la de un ruido pase el umbral. Si esto se pone en
             * rojo, las dos poblaciones han dejado de estar separadas y
             * el numero de WSPR_CUADRA_MIN hay que volver a medirlo.
             */
            exige(llegaron > 20, "el arbol se recorre entero tambien con ruido");
            exige(qmax < 88, "pero ninguno cuadra lo bastante para creerselo");
            exige(inventados == 0, "asi que no se inventa ni un indicativo");
        }
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
