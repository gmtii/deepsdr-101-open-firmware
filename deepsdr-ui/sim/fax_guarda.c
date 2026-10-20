/*
 * GUARDAR LAS CARTAS DE FAX: EL BOTON MANDA.
 *
 * POR QUE EXISTE, Y POR QUE ESTA ESCRITO OTRA VEZ
 * ----------------------------------------------
 * En un mismo dia, por tres causas distintas, el mismo sintoma: el disco
 *    todo lo que va decodificando, y cuando se salgan o le den otra vez al
 *    boton que cierre el fichero. Punto."
 *
 * O sea: un INTERRUPTOR. Y un interruptor no tiene estados.
 *
 * Este banco corre la condicion de verdad -wfx_abre_toca(), la que compila
 * el firmware- dentro de un poll de mentira con la regla nueva, y cuenta
 * FICHEROS Y RENGLONES, que es lo que el usuario ve. El estado del
 * decodificador se mueve por todas partes a proposito: lo que se exige es
 * que NO CAMBIE NADA.
 */
#include "wfx_guarda.h"
#include <stdio.h>
#include <string.h>

static int fallos = 0;

static void di(const char *que, int bien, const char *detalle)
{
    printf("    %-54s %s%s%s\n", que, bien ? "bien" : "*** MAL ***",
           detalle ? "  " : "", detalle ? detalle : "");
    if (!bien) { fallos++; }
}

enum { BUSCA = 0, FASE, IMAGEN };

/*
 * LAS CIFRAS SALEN DE main.c, NO DE AQUI - 20/10/2026.
 *
 * Aqui ponia:
 *
 *     #define ALTO       600U     / * WFX_ALTO_MAX * /
 *
 * cuando WFX_ALTO_MAX llevaba tiempo valiendo 1024. O sea que este banco
 * juraba que una carta se trocea a los cinco minutos mientras el firmware
 * la trocea a los ocho y medio, y lo unico que lo decia era un comentario:
 * nada lo comprobaba.
 *
 * Es la familia de fallo que este proyecto lleva barriendo todo el mes
 * -algo declara un dato y nada comprueba que sea verdad- metida dentro de
 * la propia red que tenia que cazarla.
 *
 * sim/wfx_tabla.h lo genera tools/gen_wfx.py leyendo los #define de
 * main.c. Si alguien cambia una cifra alli, cambia aqui; si le quita el
 * #define, el generador para la compilacion.
 */
#include "wfx_tabla.h"

#define ALTO       WFX_ALTO_MAX
#define CERROJO_MS WFX_CERROJO_MS
#define PASO_MS      500U

typedef struct {
    uint8_t  hay, fallo, hubo, disco_lleno, en_modo;
    char     dueno;                /* 'W' la carta de fax, 'S' el SSTV */
    uint32_t ms, ult;
    uint16_t enfichero, escritos, ficheros, intentos;
} poll_t;

/*
 * EL CERROJO LO ARMA EL CIERRE, NO EL QUE CIERRA - 20/10/2026.
 *
 * Aqui ponia un parametro `por_lleno` y este comentario:
 *
 *   "Ese caso NO arma el cerrojo: es una continuacion legitima y ademas no
 *    puede desbocarse."
 *
 * El razonamiento era correcto para ESE caso y equivocado en general: si
 * el cerrojo lo arma quien cierra, basta UN cerrador que no se acuerde
 * para que no se arme nunca. Y lo hubo: sstv_poll() cerraba la carta del
 * fax en cada vuelta del bucle principal, sin saber que era del fax, y de
 * ahi los veintiun ficheros en un minuto que mando el usuario.
 *
 * Ahora lo arma el cierre, siempre, igual que gsv_cierra() en el firmware.
 * Lo que se pierde es poder encadenar dos cartas sin esperar; lo que se
 * gana es que un cerrador desconocido no pueda hacer una riada.
 */
static void cierra(poll_t *p)
{
    if (!p->hay) { return; }
    p->hay = 0U;
    p->ficheros++;
    p->escritos = (uint16_t)(p->escritos + p->enfichero);
    p->enfichero = 0U;
    if (p->dueno == 'W') { p->hubo = 1U; p->ult = p->ms; }
    p->dueno = 0;
}

/* gsv_cierra_si_es(): solo cierra si el fichero es del que lo pide. */
static void cierra_si_es(poll_t *p, char dueno)
{
    if (!p->hay) { return; }
    if (p->dueno != dueno) { return; }
    cierra(p);
}

/* estado = lo que diga el decodificador; guarda = el boton. */
static void poll(poll_t *p, uint8_t estado, uint8_t lineas, uint8_t guarda)
{
    uint8_t i;

    (void)estado;          /* a proposito: ya no pinta nada */
    p->ms += PASO_MS;

    if (!p->en_modo) { cierra_si_es(p, 'W'); return; }

    if (guarda) {
        if (wfx_abre_toca(p->hay, p->fallo, 1U)
            && (!p->hubo || (p->ms - p->ult) > CERROJO_MS)) {
            p->intentos++;
            if (p->disco_lleno) {
                p->fallo = 1U;
            } else {
                p->hay = 1U;
                p->dueno = 'W';
                p->enfichero = 0U;
            }
        }
    } else {
        cierra_si_es(p, 'W');
    }

    for (i = 0U; i < lineas; i++) {
        if (!p->hay) { break; }
        p->enfichero++;
        if (p->enfichero >= ALTO) {
            cierra(p);
            /*
             * Y SE SIGUE EN EL SIGUIENTE, SIN PASAR POR EL CERROJO.
             *
             * Esto faltaba en el modelo y lo acaba de destapar la prueba
             * del vecino: desde que el cerrojo lo arma el cierre, un
             * fichero lleno tambien lo arma, asi que sin esta rama la
             * carta se quedaba sin continuacion un minuto entero - el
             * modelo decia una cosa y el firmware hacia otra.
             *
             * No puede desbocarse: para llegar aqui hacen falta
             * WFX_ALTO_MAX renglones, o sea ocho minutos y medio a 120
             * lpm. El cerrojo esta para los cierres que SI pueden
             * repetirse deprisa, y este no es uno.
             */
            if (!p->fallo && guarda) {
                p->intentos++;      /* la continuacion llama a gsv_abre() igual */
                p->hay = 1U;
                p->dueno = 'W';
                p->enfichero = 0U;
            }
        }
    }
}

/*
 * EL OTRO POLL. Esto es sstv_poll(), que el bucle principal llama en la
 * MISMA vuelta que fax_poll() y SIEMPRE, este activo el SSTV o no. Es el
 * que cerraba la carta del fax.
 */
static void poll_vecino(poll_t *p) { cierra_si_es(p, 'S'); }

/* Y un cerrador que NO respeta al dueño: el que todavia no he encontrado,
 * si lo hay. Se usa para medir que el cerrojo lo aguanta. */
static void poll_vecino_malo(poll_t *p) { cierra(p); }

static void toca_boton(poll_t *p) { p->fallo = 0U; }

int main(void)
{
    char t[120];

    printf("\n== Guardar las cartas de fax: el boton manda ==\n");

    /* ---------------------------------------------------------------
     * 1. EL CASO NORMAL.
     * --------------------------------------------------------------- */
    printf("\n  boton puesto, se recibe, boton quitado\n");
    {
        poll_t p; uint16_t k;

        memset(&p, 0, sizeof p); p.en_modo = 1U;
        for (k = 0U; k < 100U; k++) { poll(&p, IMAGEN, 1U, 1U); }
        poll(&p, IMAGEN, 0U, 0U);      /* quita el boton */

        snprintf(t, sizeof t, "%u renglones en %u fichero(s)", p.escritos, p.ficheros);
        di("un fichero con todo lo recibido", p.escritos == 100U && p.ficheros == 1U, t);
    }

    /* ---------------------------------------------------------------
     * 2. EL DECODIFICADOR PARPADEANDO: NO TIENE QUE CAMBIAR NADA.
     * ---------------------------------------------------------------
     * Es el caso que destrozo el disco del usuario tres veces. Diez
     * minutos, el estado saltando entre imagen y fase cada dos por tres.
     * --------------------------------------------------------------- */
    printf("\n  diez minutos con el decodificador parpadeando\n");
    {
        poll_t p; uint16_t k;

        memset(&p, 0, sizeof p); p.en_modo = 1U;
        for (k = 0U; k < 1200U; k++) {
            uint8_t est = ((k % 7U) == 6U) ? FASE : IMAGEN;
            poll(&p, est, 1U, 1U);
        }
        poll(&p, BUSCA, 0U, 0U);

        snprintf(t, sizeof t, "%u fichero(s), %u renglones de %u por fichero",
                 p.ficheros, p.escritos, (unsigned)ALTO);
        /* La cifra sale de ALTO -o sea de WFX_ALTO_MAX, generado desde
         * main.c- y no escrita a mano: aqui ponia "600" cuando el firmware
         * llevaba dias troceando a 1024. */
        di("sigue siendo UN fichero por cada ALTO renglones",
           p.ficheros == 1200U / ALTO + 1U && p.escritos == 1200U, t);
        snprintf(t, sizeof t, "%u reservas", p.intentos);
        di("y solo se reserva sitio al abrir cada uno",
           p.intentos == p.ficheros, t);
    }

    /* ---------------------------------------------------------------
     * 3. EL BOTON APAGADO NO ABRE NADA, PASE LO QUE PASE.
     * --------------------------------------------------------------- */
    printf("\n  el boton apagado\n");
    {
        poll_t p; uint16_t k;

        memset(&p, 0, sizeof p); p.en_modo = 1U;
        for (k = 0U; k < 200U; k++) { poll(&p, IMAGEN, 1U, 0U); }

        snprintf(t, sizeof t, "%u ficheros, %u intentos", p.ficheros, p.intentos);
        di("ni un fichero ni un intento", p.ficheros == 0U && p.intentos == 0U, t);
    }

    /* ---------------------------------------------------------------
     * 4. ENCENDER A MITAD: SE GUARDA DESDE AHI.
     * --------------------------------------------------------------- */
    printf("\n  el boton encendido a mitad de carta\n");
    {
        poll_t p; uint16_t k;

        memset(&p, 0, sizeof p); p.en_modo = 1U;
        for (k = 0U; k < 50U; k++) { poll(&p, IMAGEN, 1U, 0U); }
        for (k = 0U; k < 50U; k++) { poll(&p, IMAGEN, 1U, 1U); }
        poll(&p, IMAGEN, 0U, 0U);

        snprintf(t, sizeof t, "%u renglones en %u fichero(s)", p.escritos, p.ficheros);
        di("media carta, en un fichero", p.escritos == 50U && p.ficheros == 1U, t);
    }

    /* ---------------------------------------------------------------
     * 5. SALIRSE DEL MODO CIERRA.
     * --------------------------------------------------------------- */
    printf("\n  salirse del modo\n");
    {
        poll_t p; uint16_t k;

        memset(&p, 0, sizeof p); p.en_modo = 1U;
        for (k = 0U; k < 40U; k++) { poll(&p, IMAGEN, 1U, 1U); }
        p.en_modo = 0U;
        poll(&p, IMAGEN, 0U, 1U);

        snprintf(t, sizeof t, "%u renglones en %u fichero(s)", p.escritos, p.ficheros);
        di("el fichero se cierra solo", p.hay == 0U && p.ficheros == 1U, t);
    }

    /* ---------------------------------------------------------------
     * 6. EL DISCO SIN HUECO: UN INTENTO, NO UNO POR VUELTA.
     * --------------------------------------------------------------- */
    printf("\n  sin hueco seguido para la carta\n");
    {
        poll_t p; uint16_t k;

        memset(&p, 0, sizeof p); p.en_modo = 1U; p.disco_lleno = 1U;
        for (k = 0U; k < 200U; k++) { poll(&p, IMAGEN, 1U, 1U); }

        snprintf(t, sizeof t, "%u intentos en 200 vueltas", p.intentos);
        di("se intenta una sola vez", p.intentos == 1U, t);
        toca_boton(&p);
        for (k = 0U; k < 200U; k++) { poll(&p, IMAGEN, 1U, 1U); }
        snprintf(t, sizeof t, "%u intentos en total", p.intentos);
        di("y tocar el boton da otro, y solo uno", p.intentos == 2U, t);
    }

    /* ---------------------------------------------------------------
     * 7. EL CERROJO: NUNCA MAS DE UN FICHERO POR MINUTO.
     * ---------------------------------------------------------------
     * No protege de nada que se sepa hoy. Esta porque el mismo sintoma
     * volvio tres veces por tres causas distintas, y un tope que no depende
     * de haber entendido la causa es lo unico que cubre la cuarta.
     * --------------------------------------------------------------- */
    printf("\n  el cerrojo, con el boton dando saltos\n");
    {
        poll_t p; uint16_t k;

        memset(&p, 0, sizeof p); p.en_modo = 1U;
        /* Diez minutos tocando el boton cada vuelta, que es lo peor que
         * puede hacer un usuario -o un fallo- con esto. */
        for (k = 0U; k < 1200U; k++) { poll(&p, IMAGEN, 1U, (uint8_t)(k & 1U)); }

        snprintf(t, sizeof t, "%u ficheros en diez minutos", p.ficheros);
        di("como mucho un fichero por minuto", p.ficheros <= 11U, t);
    }

    /* ---------------------------------------------------------------
     * EL VECINO. Lo que de verdad paso.
     * ---------------------------------------------------------------
     *
     * El bucle principal llama a fax_poll() y a sstv_poll() en la MISMA
     * vuelta y SIEMPRE. sstv_poll() cerraba "lo que hubiera".
     * --------------------------------------------------------------- */
    printf("\n  el otro poll, en cada vuelta del bucle\n");
    {
        poll_t p; uint16_t k;

        memset(&p, 0, sizeof p); p.en_modo = 1U;
        /* Ocho minutos: 960 vueltas de medio segundo a un renglon por
         * vuelta, que es 120 lpm. Por debajo de WFX_ALTO_MAX a proposito,
         * para que lo unico que pueda cerrar sea el vecino. */
        for (k = 0U; k < 960U; k++) {
            poll(&p, IMAGEN, 1U, 1U);
            poll_vecino(&p);        /* <- sstv_poll() */
        }
        snprintf(t, sizeof t, "%u fichero(s), %u renglones dentro",
                 (unsigned)(p.ficheros + (p.hay ? 1U : 0U)), (unsigned)p.enfichero);
        di("ocho minutos de carta siguen siendo UN fichero",
           p.ficheros == 0U && p.hay && p.enfichero == 960U, t);
    }

    printf("\n  una carta mas larga que el fichero\n");
    {
        poll_t p; uint16_t k;

        memset(&p, 0, sizeof p); p.en_modo = 1U;
        /* Dos ficheros y pico: 2400 renglones con ALTO = WFX_ALTO_MAX. */
        for (k = 0U; k < 2400U; k++) {
            poll(&p, IMAGEN, 1U, 1U);
            poll_vecino(&p);
        }
        snprintf(t, sizeof t, "%u cerrados + %u renglones en el que va",
                 (unsigned)p.ficheros, (unsigned)p.enfichero);
        di("se trocea por altura y por nada mas",
           p.ficheros == 2400U / ALTO && p.hay &&
           p.enfichero == 2400U % ALTO, t);
        snprintf(t, sizeof t, "%u renglones", (unsigned)(p.escritos + p.enfichero));
        di("y no se pierde ni un renglon por el camino",
           (uint32_t)p.escritos + p.enfichero == 2400U, t);
    }

    /* ---------------------------------------------------------------
     * Y SI SE ME HA ESCAPADO OTRO CERRADOR.
     * ---------------------------------------------------------------
     *
     * El dueño tapa el cerrador que SE CONOCE. Lo que hace falta para
     * poder prometer algo es que uno DESCONOCIDO tampoco pueda hacer una
     * riada, y de eso se encarga el cerrojo - siempre que lo arme el
     * cierre y no el que cierra.
     *
     * Esto pone el peor vecino imaginable: uno que cierra la carta en
     * CADA vuelta del bucle, sin mirar de quien es. Antes eso daba un
     * fichero por vuelta. Ahora el cerrojo lo capa a uno por minuto.
     * --------------------------------------------------------------- */
    printf("\n  y un cerrador desconocido, en cada vuelta\n");
    {
        poll_t p; uint16_t k;

        memset(&p, 0, sizeof p); p.en_modo = 1U;
        for (k = 0U; k < 1200U; k++) {
            poll(&p, IMAGEN, 2U, 1U);
            poll_vecino_malo(&p);   /* cierra sea de quien sea */
        }
        snprintf(t, sizeof t, "%u ficheros en diez minutos", (unsigned)p.ficheros);
        /* Diez minutos, un minuto de cerrojo: once como mucho -el primero
         * no espera-. Sin cerrojo armado por el cierre serian 1200. */
        di("ni asi pasa de un fichero por minuto", p.ficheros <= 11U, t);
        di("y no son los 1200 de una riada", p.ficheros < 100U, t);
    }

    printf("\n%s (%d fallos)\n\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);

    return fallos ? 1 : 0;
}
