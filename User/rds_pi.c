#include "rds_pi.h"

/*
 * La lista. Ordenada por codigo, que es como se lee y como el banco puede
 * comprobar que no hay repetidos.
 *
 * Ver rds_pi.h para de donde sale cada uno y por que esta tabla solo vale
 * para Espana.
 */
static const struct { uint16_t pi; const char *nombre; } k_pi[] = {
    { 0xE211U, "RNE 1" },
    { 0xE212U, "RNE Clásica" },
    { 0xE213U, "RNE 3" },
    { 0xE235U, "LOS40" },
    { 0xE237U, "Cadena Dial" },
    { 0xE239U, "Cadena SER" },
    /* Medido en la radio por el usuario del proyecto, 25/09/2026: la suya
     * decodifico E256 y el la identifico como Intereconomia. Es el primero
     * de esta tabla que no sale de una lista publicada sino de una escucha
     * propia, y por eso queda dicho de donde viene. */
    { 0xE256U, "Intereconomía" },
    { 0xE2CAU, "COPE" },
    /* Confirmada EN EL AIRE por el usuario del proyecto el 05/11/2026 en
     * 101,7 MHz: la cabecera enseño "E2CF" sola mientras el enganche iba
     * flojo -el PI va en el bloque A de todos los grupos y aguanta lo que
     * el nombre no-, y al mover la radio y coger mejor cobertura aparecio
     * el nombre al lado. Que es exactamente para lo que esta puesto el
     * codigo en crudo en el renglon: para poder anotarlo.
     *
     * Y encaja con su vecina: E2CA es COPE, y las referencias de programa
     * consecutivas se reparten dentro de la misma casa - igual que RNE 1,
     * Clasica y 3 van seguidas en E211, E212 y E213. */
    { 0xE2CFU, "Rock FM" },
};

#define PI_N ((uint16_t)(sizeof k_pi / sizeof k_pi[0]))

const char *rds_pi_nombre(uint16_t pi)
{
    uint16_t i;
    /* Busqueda lineal: son siete. Una binaria aqui seria mas codigo que
     * datos, y esto corre una vez por cambio de emisora. */
    for (i = 0U; i < PI_N; i++) {
        if (k_pi[i].pi == pi) { return k_pi[i].nombre; }
    }
    return 0;
}

/*
 * EL RENGLON FIJO DE LA CABECERA DE FM - 05/11/2026.
 *
 * POR QUE ESTA AQUI Y NO EN main.c.
 *
 * Estaba dentro de la funcion que arma la marquesina, en main.c, y por tanto
 * no habia forma de probarlo: main.c no se compila en el simulador. Vive
 * aqui por lo mismo que hora_sello() vive en hora_comun.c - para que un
 * banco pueda pasarle los casos de borde uno a uno. Ver sim/rdstest.c.
 *
 * QUE DECIDE. En orden:
 *
 *   1. Si el codigo de programa esta en la lista, SE ENSEÑA LA CADENA Y YA.
 *      El nombre manda: nadie necesita ver "COPE . E2CA".
 *
 *   2. Si no esta, se enseñan LAS DOS COSAS que haya - el nombre que manda
 *      la emisora y el codigo en crudo-, separadas por un punto volado:
 *
 *          RADIO 1 . 7201
 *
 *      Pedido asi por el usuario del proyecto el 05/11/2026, a su vez a
 *      peticion de un usuario de fuera que echaba en falta ver el codigo.
 *      Y tiene sentido: el codigo es lo que identifica la emisora cuando el
 *      nombre no nos dice nada, y es lo que hay que anotar para poder
 *      anadirla a la lista de arriba.
 *
 * Y AQUI SE CAE UN AGUJERO QUE HABIA. Antes esto era una cadena de tres
 * ramas excluyentes -lista, si no PS, si no codigo-, asi que EN CUANTO
 * llegaba un PS el codigo dejaba de ser alcanzable. Y el PS se da por
 * recibido con que haya pasado el CRC UN segmento de dos caracteres: si ese
 * segmento son dos espacios -señal floja, o una emisora con PS dinamico que
 * manda un hueco entre palabras-, los ocho caracteres son espacios, el
 * recorte deja la cadena vacia y el renglon se quedaba EN BLANCO con el
 * codigo perfectamente sabido. El comentario de main.c decia que un hueco
 * vacio se leeria como "no hay RDS" y que eso seria mentira; era
 * exactamente lo que pasaba. Ahora las dos cosas se intentan por separado,
 * asi que un PS en blanco no tapa nada.
 *
 * EL RECORTE ES SOLO POR LA DERECHA, como siempre: el PS son ocho
 * caracteres rellenos con espacios. Los de la izquierda, cuando los hay, son
 * segmentos que todavia no han llegado, y quitarlos haria creer que el
 * nombre empieza donde no empieza.
 *
 * Devuelve cuantos BYTES ha escrito sin contar el cierre. Bytes y no
 * caracteres: el punto volado son dos en UTF-8.
 */
uint8_t rds_rotulo(char *out, uint8_t tope, uint8_t tiene_pi, uint16_t pi,
                   uint8_t tiene_ps, const char *ps)
{
    static const char hx[] = "0123456789ABCDEF";
    const char *n;
    uint8_t k = 0U;

    if (out == 0 || tope == 0U) { return 0U; }
    out[0] = '\0';

    n = tiene_pi ? rds_pi_nombre(pi) : 0;
    if (n) {
        while (n[k] != '\0' && k < (uint8_t)(tope - 1U)) { out[k] = n[k]; k++; }
        out[k] = '\0';
        return k;
    }

    if (tiene_ps && ps) {
        uint8_t i;
        for (i = 0U; i < 8U && k < (uint8_t)(tope - 1U); i++) { out[k++] = ps[i]; }
        while (k > 0U && out[k - 1U] == ' ') { k--; }
    }

    /*
     * El codigo, y el separador solo si hay algo delante. Los OCHO bytes
     * -separador y cuatro digitos- se piden ENTEROS: medio codigo escrito
     * porque no cabia seria peor que no escribirlo.
     *
     * OCHO Y NO SIETE: el separador es " . " con el punto volado, y ese
     * punto son DOS bytes en UTF-8, asi que el separador ocupa cuatro y no
     * tres. Escrito a mano salio siete, y con eso rds_rotulo() escribia el
     * cierre UNA POSICION MAS ALLA del buffer en el caso justo - ocho
     * caracteres de nombre y tope veinte no lo pillaba, pero el caso (g) de
     * sim/rdstest.c, que aprieta el tope a 16, si. 05/11/2026.
     */
    if (tiene_pi && (uint8_t)(k + ((k > 0U) ? 8U : 4U)) <= (uint8_t)(tope - 1U)) {
        if (k > 0U) {
            out[k++] = ' ';
            out[k++] = (char)0xC2; out[k++] = (char)0xB7;   /* punto volado */
            out[k++] = ' ';
        }
        out[k++] = hx[(pi >> 12) & 0xFU];
        out[k++] = hx[(pi >> 8)  & 0xFU];
        out[k++] = hx[(pi >> 4)  & 0xFU];
        out[k++] = hx[ pi        & 0xFU];
    }

    out[k] = '\0';
    return k;
}

uint16_t rds_pi_cuantas(void) { return PI_N; }
uint16_t rds_pi_codigo(uint16_t i) { return (i < PI_N) ? k_pi[i].pi : 0U; }
const char *rds_pi_nombre_i(uint16_t i) { return (i < PI_N) ? k_pi[i].nombre : 0; }
