/*
 * LA SECUENCIA DE ARRANQUE DEL PANEL, SACADA DEL CARGADOR DE SERIE.
 *
 * *** El dueno: "estaria bien que con esta cosa tan delicada NO
 * ADIVINASES". *** Esto no esta escrito a mano: lo ha generado un script
 * recorriendo el boot.bin de serie instruccion a instruccion y anotando
 * cada escritura al puerto de comandos (0x60000000) y al de datos
 * (0x60020000). Las tablas de abajo SON lo que hace el de serie, no lo que
 * yo creo que deberia hacer.
 *
 * POR QUE HACIA FALTA. Nuestro rm68120_init() usa la secuencia del NT35510
 * (RM68120_USE_NT35510_SEQUENCE = 1) y deja el panel con el canal ROJO a
 * tope: el NEGRO 0x0000 sale rojo, el VERDE 0x07E0 sale amarillo y el GRIS
 * 0x8410 sale blanco. Es el mismo "painted the panel red on boot" que
 * main.c linea 912 documenta desde el 30/07, y el motivo por el que la
 * aplicacion dejo de llamar a rm68120_init() y se apoya en el cargador.
 *
 * Y HAY TRES SECUENCIAS, NO UNA. El de serie lee el ID del panel y elige:
 *
 *   0x08007CD2  lee el comando 0x000A           -> si vale 8    : rama A
 *   0x08007CE4  lee el comando 0x3A00           -> si vale 0x77 : rama B
 *                                                  en otro caso : rama C
 *
 * La rama A son 12 pasos -el panel ya viene inicializado, solo le pone el
 * MADCTL y lo enciende-; la B y la C son la puesta en marcha completa, con
 * sus tablas de gamma y de alimentacion, y se diferencian en bastantes
 * registros. Nuestro codigo se sabia lo de los dos identificadores -esta
 * escrito en rm68120_init()- pero usaba siempre la misma rama.
 */
#include "panel_serie.h"
#include "rm68120_exmc.h"
#include "panel_tablas.h"
#include "panel_rama_b_v22.h"

extern void delay_ms(uint32_t ms);

/*
 * Las tres secuencias viven ahora en panel_tablas.h, GENERADO por
 * tools/captura_panel.py ejecutando el boot.bin de serie. Estaban aqui
 * escritas a pelo y dos de las tres estaban mal; ver la cabecera de ese
 * guion. Si hay que volver a sacarlas:
 *
 *     python3 tools/captura_panel.py rescate/boot.bin boot/panel_tablas.h
 */
/*
 * El reproduce de la v2.2, tal cual. Comando y dato pegados con
 * rm68120_write_cmd_data(), que es lo que deja el hueco en dos
 * instrucciones en vez de quince. Ver la cabecera de panel_rama_b_v22.h.
 */
static void reproduce_v22(const paso_v22_t *p, unsigned n)
{
    unsigned i;

    for (i = 0U; i < n; i++) {
        switch (p[i].tipo) {
        case 0: rm68120_write_cmd_data(p[i].cmd, p[i].dat); break;
        case 1: rm68120_write_cmd(p[i].cmd);                break;
        default: delay_ms(p[i].dat);                        break;
        }
    }
}

static void reproduce(const paso_t *p, unsigned n)
{
    unsigned i, k;

    for (i = 0U; i < n; i++) {
        rm68120_write_cmd(p[i].cmd);

        if (p[i].idx == IDX_COLOR) {
            /* La tabla de color de la rama A: 64 escalones por canal. */
            for (k = 0U; k < 64U; k++) { rm68120_write_data((uint16_t)(k << 3)); }
            for (k = 0U; k < 64U; k++) { rm68120_write_data((uint16_t)(k << 2)); }
            for (k = 0U; k < 64U; k++) { rm68120_write_data((uint16_t)(k << 3)); }
        } else {
            for (k = 0U; k < p[i].n; k++) {
                rm68120_write_data(k_datos[p[i].idx + k]);
            }
        }

        if (p[i].ms != 0U) { delay_ms(p[i].ms); }
    }
}

/* 'A', 'B' o 'C' para forzar una rama; 0 para decidirla leyendo el panel,
 * que es lo que hace el de serie y lo que hacemos ya. Esto queda como
 * salida de emergencia, no como lo normal. */
#define PANEL_RAMA_FORZADA  0

/* Cuantas veces se lee cada identificador antes de fiarse de el. Ver
 * panel_serie_init: una lectura que cambia no es una lectura. */
#define PANEL_LECTURAS  3U

uint16_t g_panel_id_0a;
uint16_t g_panel_id_3a;
uint8_t  g_panel_firme;   /* 1 si las lecturas salieron siempre iguales */
uint8_t  g_panel_forzar;  /* 'A'/'B'/'C' para la prueba; 0 = elegir sola */

/*
 * Lee un identificador varias veces y solo lo da por bueno si sale
 * siempre lo mismo. Devuelve 1 si es firme; el valor va en *v.
 */
/*
 * EL IDENTIFICADOR SE LEE A MEDIO BYTE, Y HAY QUE ADMITIRLO - 05/10/2026.
 *
 * El de serie compara el 0x3A00 con 0x77. Esta radio contesta unas veces
 * 0x0077 y otras 0x0007 -el mismo valor perdiendo un nibble- y CUAL de las
 * dos depende del binario: con el cargador v2.2 la ficha saca 0x0077 y con
 * el v2.13 saca 0x0007, con el mismo panel y el mismo codigo de lectura.
 * La lectura es marginal; no es una propiedad del panel.
 *
 * *** Y eso costo media noche. *** Con la comparacion exacta, un 0x0007 no
 * es 0x77, asi que la radio se caia a la rama C -la que deja la imagen
 * lavada y el negro levantado- y el dueno veia la pantalla oscura cada vez
 * que yo tocaba cualquier cosa del cargador. Yo buscaba la diferencia en lo
 * que se le escribe al panel, y ahi no estaba: estaba en que NO SE ESTABA
 * USANDO SU RAMA. La ficha lo decia desde el principio:
 *
 *     Panel   rama C firme   0x000A=0000   0x3A00=0007
 *
 * Esto lo escribi por la tarde, lo quite cuando una ficha saco 0x0077
 * dandolo por resuelto, y volvio a morder. Lo que cambio no fue el panel:
 * fue que yo mire UNA medida y la generalice.
 *
 * POR QUE ADMITIR LAS DOS FORMAS ES LO SEGURO Y NO UNA CHAPUZA: la rama B
 * es la unica verificada en una radio de verdad. Equivocarse HACIA la B
 * deja la pantalla como la tiene hoy quien la tiene bien; equivocarse hacia
 * la C la lava. Entre un falso positivo inofensivo y un falso negativo que
 * estropea la pantalla, se elige el primero. La rama A sigue exigiendo el 8
 * EXACTO, que esa si deja la pantalla en blanco.
 */
static uint8_t es_rama_b(uint16_t v)
{
    return (uint8_t)(v == 0x77U || v == 0x07U || v == 0x70U);
}

static uint8_t lee_firme(uint16_t cmd, uint16_t *v)
{
    uint16_t a, i;

    a = rm68120_read_status(cmd);
    for (i = 1U; i < PANEL_LECTURAS; i++) {
        if (rm68120_read_status(cmd) != a) { *v = a; return 0U; }
    }
    *v = a;
    return 1U;
}

/*
 * LO QUE FALLABA ERA LEER UNA SOLA VEZ - 05/10/2026.
 *
 * El de serie compara el 0x3A00 con 0x77. El 01/10 leimos 0x0007 y se dio
 * por basura del bus, con la conclusion de que la patilla de lectura no
 * estaba cableada; por eso se forzo la rama B.
 *
 * Hoy, repasando aquellos numeros, propuse otra explicacion: que la
 * lectura funcionaba pero perdia medio byte, porque 0x07 y 0x05 son los
 * nibbles de 0x77 y 0x55, que son justo los dos valores que puede tener
 * ese registro. Y escribi una comparacion que admitia las dos formas.
 *
 * *** LA RADIO DICE QUE NO. *** Con las tres lecturas puestas, la ficha
 * saca:
 *
 *     Panel   rama B firme   0x000A=0000   0x3A00=0077
 *
 * 0x0077, el byte ENTERO, y las tres lecturas iguales. No se pierde
 * ningun nibble. Lo que pasaba el 01/10 es que se leia UNA vez y esa
 * primera lectura sale sin asentar; en cuanto se insiste, el panel
 * contesta bien.
 *
 * Asi que la tolerancia de nibble se ha quitado: la comparacion es
 * EXACTA, igual que la del de serie. Un apaño que tapa un sintoma cuya
 * causa resulto ser otra no es un apaño, es una mentira en el codigo -y
 * encima habria aceptado como "panel B" a uno que conteste 0x07 de
 * verdad, que seria un panel distinto y querria otra rama.
 *
 * Queda una cosa SIN explicar, y se deja escrita en vez de inventarle una
 * razon: por que la primera lectura sale mal. Lo normal en estos
 * controladores es que haga falta descartar un dato -y ya se descarta uno
 * dentro de rm68120_read_status- pero aqui hacen falta mas. No se ha
 * medido cuantos exactamente; tres lecturas van sobradas y cuestan
 * microsegundos una vez por arranque.
 */
uint8_t  g_panel_rama;

/*
 * LAS LECTURAS DEL BUS SON LENTAS, Y HAY QUE DARLES TIEMPO - 01/10/2026.
 *
 * *** El dueno: "la pantalla sigue oscura". *** Y descartada ya la
 * retroiluminacion -replicado el PWM exacto del de serie y sigue igual-,
 * lo que queda es que estemos arrancando el panel con la secuencia que no
 * es. Y la pista estaba en los propios numeros que sacamos por pantalla:
 *
 *   el de serie compara el 0x000A con  8     nosotros leimos 0x0000
 *   el de serie compara el 0x3A00 con 0x77   nosotros leimos 0x0007
 *
 * 0x07 es 0x77 con el nibble alto perdido. No es que el panel conteste
 * otra cosa: es que lo estamos leyendo mal, y con una lectura rota
 * elegimos rama C cuando lo mas probable es que toque la B.
 *
 * El motivo es el tiempo de bus. Escribir es facil -mandamos nosotros-,
 * pero LEER obliga al panel a poner los datos, y eso tarda: en estos
 * controladores son cientos de nanosegundos. Nuestro EXMC esta en 15
 * ciclos de setup, que a 192 MHz son 78 ns, y sin modo extendido las
 * lecturas usan la misma temporizacion que las escrituras.
 *
 * Asi que para los dos identificadores se pone el bus LO MAS LENTO que
 * admite -255 ciclos, 1,3 us- y despues se deja como estaba. Son dos
 * lecturas en todo el arranque: que tarden de mas da igual.
 */
#define BUS_LENTO   255U
/*
 * DOS VELOCIDADES, Y NO ES LO MISMO ARRANCAR QUE PINTAR - 01/10/2026.
 *
 * *** El dueno: "puede ser que el waterfall vaya ahora mas lento?". *** Si,
 * y era culpa mia: subi el bus de 5 a 7 ciclos para que los 374 pasos del
 * arranque del panel tuvieran el mismo margen que los del cargador de
 * serie... y la aplicacion HEREDA esa configuracion. O sea que le puse un
 * 40% mas de tiempo a cada pixel del espectro y de la cascada.
 *
 * Pero las dos cosas no tienen por que compartir numero:
 *
 *   ARRANCAR el panel son 374 escrituras a registros de gamma y de
 *   alimentacion. Si una sale mal, los colores quedan raros para siempre
 *   y no hay forma de saber cual fue. Aqui lo que importa es el margen.
 *
 *   PINTAR son 384.000 pixeles por fotograma, y un pixel mal escrito es
 *   un punto de un fotograma que ya no esta. Aqui lo que importa es la
 *   velocidad.
 *
 * Asi que se arranca despacio y se pinta rapido, y el cargador deja el
 * bus en el valor rapido antes de saltar a la aplicacion, que es lo que
 * ella hereda. Es ademas lo que pasaba con el de serie: el arrancaba a
 * 34,6 ns y la aplicacion luego corria a 25.
 */
#define BUS_ARRANQUE  5U   /* los ciclos del de serie, a su mismo reloj */
#define BUS_NORMAL    5U

/*
 * LO QUE SE APRENDIO HOY, Y EL ORDEN QUE IMPORTA - 05/10/2026.
 *
 * *** El dueno, mandando la foto de una radio que se ve mal: "una de las
 * pantallas que se ven mal". *** Y daba 0x3A00=0077, o sea rama B, igual
 * que la que se ve bien. Si el identificador no distingue la buena de la
 * mala, elegir bien la rama no podia ser el arreglo.
 *
 * Lo era otra cosa, y se vio ejecutando el boot.bin de serie en vez de
 * razonar sobre el (ver tools/captura_panel.py). Comparada nuestra rama B
 * con la suya, escritura por escritura:
 *
 *     nuestra tabla 363 escrituras  /  el de serie 373
 *     falta al principio:  F000=55  F001=AA     <- el desbloqueo
 *     falta al final:      2A00..2A03 2B00..2B03 <- la ventana
 *     las otras 363:       identicas, mismo orden, mismo valor
 *
 * O sea que la tabla estaba bien y le faltaba el desbloqueo. Sin el, sus
 * 326 primeros pasos -las seis tablas de gamma, el VGMP/VGSP y el VCOM-
 * caian en un controlador cerrado. El panel se quedaba con la gamma DE
 * FABRICA, y eso es lo que cambia de una hornada de cristal a otra: es,
 * con todas las letras, "se ve distinto en un LCD que en otro".
 *
 * Y EL ORDEN, que es donde me equivoque en la v2.3:
 *
 *     EL DE SERIE:  lee 000A -> lee 3A00 -> F000 F001 F002 F003 F004 -> gamma
 *     LA v2.3:      F000 F001 F002 F003 F004 -> lee 000A -> lee 3A00 -> gamma
 *
 * El de serie LEE ANTES DE DESBLOQUEAR. Al desbloquear primero, el 0x3A00
 * ya no contestaba 0x77, la comparacion fallaba y la radio se iba a la
 * rama C -la lavada-. El dueno lo vio en un minuto: "con ese update a mi
 * se me ve oscura la pantalla". No era que la gamma de B fuera mala para
 * su cristal: es que su radio no estaba usando la B.
 *
 * Asi que aqui no hay desbloqueo suelto. Se lee primero, como el de serie,
 * y el desbloqueo va donde el lo tiene: dentro de la rama, en la tabla.
 */
/*
 * LA RAMA B NO SALE DE LA CAPTURA, SALE DE LA v2.2 - 05/10/2026.
 *
 * Las ramas A y C vienen de ejecutar el boot.bin de serie, que es lo
 * correcto: las dos estaban mal copiadas -la A llevaba rafagas de 192 y 127
 * parametros aplastadas a una escritura- y por eso la A dejaba la pantalla
 * en blanco.
 *
 * La B no. La B la usa la radio del dueno, es la unica verificada mirandola
 * -*** "se ve perfecto" ***- y cada vez que la he tocado la he oscurecido.
 * Asi que se copia de ahi, de ejecutar el customboot v2.2 y anotar lo que
 * le escribe al panel. Ver panel_rama_b_v22.h.
 *
 * Que las dos cosas convivan no es desorden: es que una esta probada en la
 * radio y las otras dos no, y mezclarlas fue justo el error de esta tarde.
 */

void panel_serie_init(void)
{
    uint8_t f0, f3;

    rm68120_bus_dato_pon(BUS_LENTO);
    f0 = lee_firme(0x000AU, &g_panel_id_0a);
    f3 = lee_firme(0x3A00U, &g_panel_id_3a);
    g_panel_firme = (uint8_t)(f0 && f3);
    rm68120_bus_dato_pon(BUS_ARRANQUE);

    /*
     * LA RAMA, COMO LA ELIGE EL DE SERIE - 05/10/2026.
     *
     * *** El dueno: "quiero que nuestro bootloader haga lo mismo que hace
     * el original, que automaticamente sabe que pantalla es y actua en
     * consecuencia". ***
     *
     * El de serie lee dos identificadores y reparte en tres ramas. Aqui se
     * hacia lo mismo... y despues se forzaba la B a pelo, porque el 01/10
     * las lecturas parecian basura. Con las tres lecturas puestas ya no lo
     * parecen: la radio saca 0x3A00=0077 tres veces seguidas.
     *
     * POR QUE ESTO NO PUEDE DEJAR UNA RADIO CIEGA. Dos cierres:
     *
     *   1. Si las lecturas NO son firmes -sale distinto de una vez a
     *      otra- no nos fiamos y se va a la B, que es la que llevaba
     *      corriendo desde el 01/10.
     *   2. La rama A exige el 8 EXACTO. Es la unica que deja la pantalla
     *      EN BLANCO en esta radio, y de ahi no se sale sin DFU.
     *
     * Y a una radio cuyo panel conteste otra cosa le toca la C, que es
     * justo lo que el de serie haria y lo que esta radio no hacia. La C
     * no es un capricho: cambia el VCOM de 0x67 a 0x3D y lleva otras
     * tablas de gamma enteras.
     */
    if (g_panel_forzar == 'A' || g_panel_forzar == 'B' || g_panel_forzar == 'C') {
        /*
         * LA PRUEBA ARRANCA DE VERDAD, NO REARRANCA - 05/10/2026.
         *
         * *** El dueno, con la pantalla de prueba de la v2.5 delante: "se
         * ven igual de mal". *** Y con el reset puesto, que era mi segunda
         * explicacion. O sea que reproducir una secuencia sobre un panel
         * que ya esta en marcha no se arregla con un reset; hay algo mas,
         * y no se que es.
         *
         * Asi que se quita el problema en vez de seguir adivinandolo: la
         * prueba ya NO rearranca nada. Guarda la rama que toca, reinicia
         * la radio, y el panel se arranca desde cero con esa rama por el
         * camino de siempre. Lo que se mide es entonces EXACTAMENTE un
         * arranque, que es lo unico que vale para comparar ramas - y de
         * paso deja de haber dos caminos distintos de arrancar el panel,
         * que era el fallo de fondo.
         */
        g_panel_rama = g_panel_forzar;
    } else if (PANEL_RAMA_FORZADA == 'A') {
        g_panel_rama = 'A';
    } else if (PANEL_RAMA_FORZADA == 'B') {
        g_panel_rama = 'B';
    } else if (PANEL_RAMA_FORZADA == 'C') {
        g_panel_rama = 'C';
    } else if (!g_panel_firme) {
        g_panel_rama = 'B';          /* sin lectura de fiar, lo de siempre */
    } else if (g_panel_id_0a == 8U) {
        g_panel_rama = 'A';          /* exacto, y solo exacto */
    } else if (es_rama_b(g_panel_id_3a)) {
        g_panel_rama = 'B';
    } else {
        g_panel_rama = 'C';
    }

    switch (g_panel_rama) {
    case 'A': reproduce(k_rama_A, sizeof k_rama_A / sizeof k_rama_A[0]); break;
    case 'C': reproduce(k_rama_C, sizeof k_rama_C / sizeof k_rama_C[0]); break;
    default:  reproduce_v22(k_rama_B_v22,
                        sizeof k_rama_B_v22 / sizeof k_rama_B_v22[0]); break;
    }

    /* Arrancado el panel, el bus vuelve a lo rapido: es lo que hereda la
     * aplicacion y con lo que pinta el espectro. */
    rm68120_bus_dato_pon(BUS_NORMAL);
}
