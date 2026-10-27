#include "spec_rotulos.h"
#include "font_ui_11.h"
#include <string.h>

/* Declarada aqui porque la usan el recorte, la fila y la eleccion, y el
 * ancho de casilla tiene que ser EL MISMO en los tres sitios. */
static uint32_t rot_paso(uint32_t span_hz, uint8_t max);

/*
 * LA MISMA CUENTA QUE LA REGLA, Y POR ESO ESTA AQUI SOLA.
 *
 * Ver spec_rotulos.h: la regla de frecuencias va de center_hz - span/2 a
 * center_hz + span/2 sobre `ancho` pixeles. Esto es esa recta y nada mas.
 *
 * En int64 porque span_hz llega a 192.000 y el ancho a 756: el producto
 * se pasa de 32 bits con cualquier frecuencia de onda corta.
 */
int16_t spec_rotulos_x(uint32_t hz, uint32_t center_hz, uint32_t span_hz,
                       int16_t x0, int16_t ancho)
{
    int64_t lo, d;

    if (span_hz == 0UL || ancho <= 0) { return x0; }
    lo = (int64_t)center_hz - (int64_t)(span_hz / 2UL);
    d  = ((int64_t)hz - lo) * (int64_t)ancho / (int64_t)span_hz;
    if (d < -32000) { d = -32000; }
    if (d >  32000) { d =  32000; }
    return (int16_t)((int32_t)x0 + (int32_t)d);
}


/* ---------------------------------------------------------------- */

uint32_t spec_rotulos_hz(const spec_rotulos_t *rr, uint8_t i)
{
    if (rr == 0 || i >= SPEC_ROT_MAX) { return 0UL; }
    return rr->base_hz + (uint32_t)rr->d10[i] * 10UL;
}

/* La fila de un cartel: par arriba, impar abajo, por CASILLA. Vive aqui
 * y no dentro del dibujante para que el banco pueda comprobar la regla
 * sin mirar pixeles - y para que haya un solo sitio donde cambiarla. */
uint8_t spec_rotulos_fila(const spec_rotulos_t *rr, uint8_t i, uint32_t span_hz)
{
    if (rr == 0 || span_hz == 0UL) { return 0U; }
    return (uint8_t)((spec_rotulos_hz(rr, i) / rot_paso(span_hz, SPEC_ROT_MAX))
                     % (uint32_t)SPEC_ROT_FILAS);
}

void spec_rotulos_pon(spec_rotulos_t *rr, uint8_t i, uint32_t hz,
                      uint16_t clave)
{
    uint32_t d;

    if (rr == 0 || i >= SPEC_ROT_MAX) { return; }
    /* Por debajo de la base no se puede representar: se clava en la
     * base, que es el borde izquierdo, y el dibujante ya decide. */
    d = (hz > rr->base_hz) ? ((hz - rr->base_hz) / 10UL) : 0UL;
    if (d > 0xFFFFUL) { d = 0xFFFFUL; }
    rr->d10[i]   = (uint16_t)d;
    rr->clave[i] = clave;
}

/*
 * EL RECORTE POR PALABRA. Ver el comentario de la cabecera: cortar a
 * mitad de palabra no solo queda feo, es que se lee como un fallo.
 */
static uint8_t rot_separa(char c)
{
    return (uint8_t)((c == ' ') || (c == '/') || (c == '-'));
}

void spec_rotulos_recorta(char *dest, const char *origen)
{
    uint8_t i, corte;

    if (dest == 0) { return; }
    dest[0] = '\0';
    if (origen == 0) { return; }

    for (i = 0U; i < SPEC_ROT_TXT && origen[i] != '\0'; i++) {
        dest[i] = origen[i];
    }
    dest[i] = '\0';
    /* Si cabia entero no hay nada que pensar. */
    if (origen[i] == '\0') { return; }

    /*
     * EL CORTE QUE CAE JUSTO EN UN ESPACIO NO PARTE NINGUNA PALABRA.
     *
     * Aqui no lo miraba y por eso "Channel Marker D" -diecisiete letras-
     * se quedaba en "Channel": las catorce primeras son "Channel
     * Marker", completas, y aun asi retrocedia hasta el espacio anterior
     * y se comia la segunda palabra entera. Lo canto el barrido de
     * sim/rotulos_elige.c, donde siete balizas distintas de los 3,594
     * MHz salian todas como "Channel".
     */
    if (rot_separa(origen[i])) {
        while (i > 0U && rot_separa(dest[i - 1U])) { i--; dest[i] = '\0'; }
        return;
    }

    /*
     * Si que parte una palabra. ¿Hay por donde cortar que deje algo
     * legible? Vale el espacio y valen la barra y el guion, que es lo que
     * separa los nombres dobles de la lista: "CNR1 Jammer/Firedrake" se
     * queda en "CNR1 Jammer" en vez de en "CNR1 Jammer/Fi".
     *
     * Y EL LISTON ESTABA DEMASIADO BAJO - 15/10/2026. Lo puse en seis
     * letras y el usuario se encontro en pantalla un cartel que decia
     * "Voice of" y nada mas: "Voice of Vietnam" tiene el espacio en la
     * octava letra, ocho pasa de seis, y cortar ahi tira justo la palabra
     * que dice QUE emisora es. Vale mas "Voice of Vietn" cortado a lo
     * bruto, que al menos se identifica.
     *
     * El liston se sube a diez de catorce: solo se corta por la palabra
     * cuando lo que se tira es una cola corta, no cuando se tira media
     * frase. Con eso "SOH Xi Wang Zhi Sheng" sigue dando "SOH Xi Wang"
     * (once letras) y "Voice of Vietnam" ya no se queda en el saludo.
     */
    corte = 0U;
    while (i > 0U) {
        i--;
        if (rot_separa(dest[i])) { corte = i; break; }
    }
    if (corte >= (uint8_t)(SPEC_ROT_TXT - 4U)) { dest[corte] = '\0'; }
}

/*
 * LOS QUE ACABAN LLAMANDOSE IGUAL AUNQUE NO LO SEAN - 15/10/2026.
 *
 * spec_rotulos_filtra() quita los repetidos por CLAVE, que es el
 * identificador del nombre en la base, y lo hace antes de leer una sola
 * letra de la flash -ahi esta la gracia, en que es gratis-. Pero dos
 * claves DISTINTAS pueden acabar en el mismo texto despues de recortar a
 * catorce letras: "Shannon Aeradio I" y "Shannon Aeradio H" son dos
 * emisoras y los dos carteles dirian "Shannon Aeradi".
 *
 * Eso en pantalla no es informacion, es el borron que se veia en pantalla.
 * Asi que el que llega segundo se apaga -se le deja el texto vacio, que
 * el dibujante ya sabe saltarse-.
 *
 * SE PIERDE EL HUECO, Y SE PIERDE A PROPOSITO. Recuperarlo obligaria a
 * leer de la flash los veintiocho nombres candidatos antes de elegir, y
 * eso son 56 lecturas mas sobre las 168 del peor caso: se saldria del
 * liston medido de emisoras.h. Cuando siete balizas comparten nombre a
 * doscientos hercios unas de otras, un cartel es la respuesta correcta.
 */
void spec_rotulos_limpia(const spec_rotulos_t *rr, spec_rot_txt_t txt,
                         uint32_t span_hz)
{
    uint32_t sep;
    uint8_t  i, j;

    if (rr == 0 || txt == 0 || span_hz == 0UL) { return; }
    /*
     * UNA CASILLA, Y SE QUEDO EN UNA - 15/10/2026.
     *
     * Llegue a subir el liston a dos casillas para que las dos de Barrow
     * se fundieran en una. Vuelve a una: si en el dial hay dos señales,
     * la pantalla dice dos. Lo unico que se silencia es el repetido tan
     * cerca que se pintaria encima del otro, que no informa de nada.
     */
    sep = rot_paso(span_hz, SPEC_ROT_MAX);

    for (i = 1U; i < rr->n && i < SPEC_ROT_MAX; i++) {
        if (txt[i][0] == '\0') { continue; }
        for (j = 0U; j < i; j++) {
            uint32_t a, b, d;
            if (txt[j][0] == '\0') { continue; }
            if (strcmp(txt[i], txt[j]) != 0) { continue; }
            a = spec_rotulos_hz(rr, i);
            b = spec_rotulos_hz(rr, j);
            d = (a > b) ? (a - b) : (b - a);
            if (d < sep) { txt[i][0] = '\0'; break; }
        }
    }
}

/*
 * LA ELECCION. Ver la cabecera para el por que; aqui va el como.
 *
 * LO PRIMERO, Y LO QUE ESTABA MAL: LA ELECCION NO PUEDE DEPENDER DE DONDE
 * ESTE LA VENTANA.
 *
 * La primera version de esto partia LA VENTANA en siete trozos y cogia de
 * cada trozo la mas centrada. Reparte bien, si: en una foto. Pero al mover
 * la sintonia un clic, los siete trozos se mueven con la ventana, cada
 * emisora cae en otro trozo, y la respuesta a "quien sale" cambia entera.
 * Lo que se ve no son carteles desplazandose con el espectro -que es lo
 * que se pidio- sino siete carteles distintos cada 120 ms. Y un
 * cartel que desaparece de un sitio y aparece en otro se lee como un
 * cartel que se ha MOVIDO, y encima hacia donde no toca.
 *
 * Asi que la rejilla es ABSOLUTA: las casillas son multiplos de `paso`
 * contados desde cero hercios, no desde el borde de la pantalla. Una
 * emisora pertenece siempre a la misma casilla, pase lo que pase con la
 * sintonia, y el que gana su casilla lo gana para siempre. Mover la
 * ventana un poco ya solo puede hacer UNA cosa: que entre una casilla
 * nueva por un lado y se vaya la del otro.
 *
 * Esto es la misma idea que hace que el cartel guarde su FRECUENCIA y no
 * su pixel (ver la cabecera). Todo lo que se ancle a la pantalla se mueve
 * cuando se mueve la pantalla; todo lo que se ancle al dial, no.
 *
 * EL PASO es span/max, que a 96 kHz sobre 756 px son 108 pixeles: un pelo
 * mas que un cartel de catorce letras. Por eso una casilla = un cartel, y
 * por eso el mismo numero sirve para lo otro que hay que decidir, que es
 * cuando dos nombres iguales se pisan.
 *
 * LOS CUATRO PASOS:
 *
 * 1. Dos candidatas con el MISMO nombre a menos de un paso son un cartel y
 *    un borron: se queda la primera. Lejos se quedan las dos, que son dos
 *    señales distintas en la pantalla.
 * 2. De cada casilla, la mas centrada en SU casilla (no en la pantalla).
 *    Empate, la de menos frecuencia: un desempate estable es parte del
 *    asunto, uno arbitrario lo estropearia.
 * 3. Si salen mas casillas que huecos -la ventana mide `max` pasos, asi
 *    que a lo sumo toca una de mas por los bordes-, se cae la casilla que
 *    MENOS trozo tenga dentro de la ventana. Esa es justo la que se esta
 *    yendo, y asi se va cuando le toca y no antes.
 * 4. Y si sobran huecos porque hay casillas vacias, se rellenan con las
 *    que quedaron, por lo centradas que esten en su casilla. Sin este paso
 *    veinte emisoras apelotonadas en tres casillas darian tres carteles
 *    teniendo sitio para siete.
 */
static uint32_t rot_dif(uint32_t a, uint32_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

/* El ancho de una casilla, que es tambien lo que mide un cartel. Un solo
 * sitio: si esto y el reparto de filas discreparan, los carteles se
 * elegirian con una medida y se pintarian con otra. */
/*
 * EL ANCHO DE CASILLA, Y POR QUE ES span/(max-1) Y NO span/max.
 *
 * Con span/max la ventana mide EXACTAMENTE max casillas, y eso solo pasa
 * si los bordes caen clavados en la rejilla. En cuanto no caen -o sea
 * casi siempre- la ventana toca max+1 casillas: una entera menos y dos
 * mordidas por los bordes. Una sobra, y habia que tirar una.
 *
 * Y "cual se tira" era una carrera entre las dos mordidas: la de la
 * izquierda se va haciendo pequeña y la de la derecha grande, y en el
 * kilohercio en que se cruzan, el cartel de la derecha APARECE y el de
 * la izquierda DESAPARECE a la vez. Dos cambios de golpe, y uno de ellos
 * en el lado hacia el que estas avanzando, que es justo donde nadie
 * espera que desaparezca nada.
 *
 * Con span/(max-1) la cuenta se cierra: la ventana mide max-1 casillas,
 * asi que toca max como mucho, pasen los bordes por donde pasen. NUNCA
 * sobra ninguna y NUNCA hay que tirar nada. Un cartel aparece cuando su
 * casilla entra por un borde y se va cuando sale por el otro, y no por
 * ningun otro motivo.
 *
 * Se paga en densidad: a 96 kHz la casilla pasa de 13,7 a 16 kHz, o sea
 * de 108 a 126 pixeles. Siete carteles en vez de ocho en el mejor caso.
 * Es un cartel menos a cambio de que ninguno aparezca ni desaparezca sin
 * motivo, y despues de la tarde de hoy ese cambio sale a cuenta.
 */
static uint32_t rot_paso(uint32_t span_hz, uint8_t max)
{
    uint32_t p;
    if (max < 2U) { max = SPEC_ROT_MAX; }
    p = span_hz / (uint32_t)(max - 1U);
    return (p == 0UL) ? 1UL : p;
}

/* Lo lejos que esta del centro de SU casilla. Es el criterio de "quien
 * gana la casilla" y el de "quien entra a rellenar", y los dos tienen que
 * ser el mismo para que rellenar no deshaga lo que repartio. */
static uint32_t rot_descentre(uint32_t hz, uint32_t paso)
{
    uint32_t casilla = hz / paso;
    uint32_t centro  = casilla * paso + paso / 2UL;
    return rot_dif(hz, centro);
}

uint8_t spec_rotulos_filtra(spec_rot_cand_t *c, uint8_t n,
                            uint32_t center_hz, uint32_t span_hz,
                            uint32_t tune_hz, uint8_t max)
{
    uint32_t lo, hi, paso;
    uint8_t  i, j, k = 0U;

    if (c == 0 || n == 0U || max == 0U || span_hz == 0UL) { return 0U; }
    if (max > SPEC_ROT_MAX) { max = SPEC_ROT_MAX; }
    /* Los mapas de bits de abajo son de 32: mas candidatas que eso no
     * llegan nunca (SPEC_ROT_CAND son 28) pero si llegaran, se miran las
     * 32 primeras y no se lee fuera del array. */
    if (n > 32U) { n = 32U; }
    paso = rot_paso(span_hz, max);
    lo   = (center_hz > span_hz / 2UL) ? (center_hz - span_hz / 2UL) : 0UL;
    hi   = lo + span_hz;

    /* 1. el mismo nombre, tan cerca que se pisaria */
    for (i = 0U; i < n; i++) {
        uint8_t repe = 0U;
        for (j = 0U; j < k; j++) {
            if (c[j].clave != c[i].clave) { continue; }
            if (rot_dif(c[i].hz, c[j].hz) < paso) { repe = 1U; break; }
        }
        if (!repe) { c[k] = c[i]; k++; }
    }


    /*
     * AQUI HABIA UN ATAJO: "si caben todas, se devuelven todas". Y era un
     * precipicio - 15/10/2026.
     *
     * Con siete candidatas o menos salian las siete tal cual, dos en la
     * misma casilla incluidas; con ocho, entraba el reparto por casillas
     * y la respuesta cambiaba ENTERA. O sea que en el kilohercio en que
     * entraba la octava se reorganizaba la pantalla, y eso es justo lo
     * que el usuario lleva toda la tarde viendo.
     *
     * Un atajo que hace que una funcion conteste de dos maneras distintas
     * segun cuantos datos le des es un precipicio escondido. Se quita: el
     * reparto por casillas se aplica SIEMPRE, haya dos candidatas o
     * veintiocho. Cuesta algun cartel de menos en una banda floja, y a
     * cambio la regla es una sola y no cambia nunca.
     */

    {
        uint32_t gana = 0UL;         /* el que manda en su casilla */
        uint8_t  sal[SPEC_ROT_MAX];
        spec_rot_cand_t tmp[SPEC_ROT_MAX];
        uint8_t  nsal = 0U, ngana = 0U;
        (void)ngana; (void)lo; (void)hi;

        /* 2. el que manda en cada casilla */
        for (i = 0U; i < k; i++) {
            uint32_t ci = c[i].hz / paso;
            uint8_t  mejor = i;
            for (j = 0U; j < k; j++) {
                if (j == i) { continue; }
                if (c[j].hz / paso != ci) { continue; }
                if (rot_descentre(c[j].hz, paso) < rot_descentre(c[mejor].hz, paso)
                    || (rot_descentre(c[j].hz, paso) == rot_descentre(c[mejor].hz, paso)
                        && c[j].hz < c[mejor].hz)) {
                    mejor = j;
                }
            }
            if (mejor == i) { gana |= (1UL << i); ngana++; }
        }

        /*
         * 2b. Y SI DOS CASILLAS GANAN CON EL MISMO NOMBRE, la menos
         * centrada cede el sitio a la segunda de SU casilla.
         *
         * Sin esto, en 7,260 MHz salen tres "PBS Xinjiang" y dos "China
         * National" y se van cinco de los siete huecos en dos emisoras,
         * dejando fuera a otras seis que nadie ha nombrado. Repetir un
         * nombre no es un fallo -son dos señales de verdad, cada una con
         * su portadora- pero vale MENOS que nombrar a una que todavia no
         * esta.
         *
         * Quien cede se decide con numeros que no dependen de donde este
         * la ventana: manda la mas centrada en su casilla, y si empatan,
         * la de menos frecuencia. Si la casilla no tiene segunda, se
         * queda el repetido: un cartel repetido informa mas que un hueco
         * vacio.
         */
        {
            uint8_t vuelta;
            for (vuelta = 0U; vuelta < 2U; vuelta++) {
                for (i = 0U; i < k; i++) {
                    uint32_t ci, di;
                    uint8_t  cede = 0U, nuevo = 0xFFU;

                    if ((gana & (1UL << i)) == 0UL) { continue; }
                    di = rot_descentre(c[i].hz, paso);
                    for (j = 0U; j < k; j++) {
                        uint32_t dj;
                        if (j == i || (gana & (1UL << j)) == 0UL) { continue; }
                        if (c[j].clave != c[i].clave) { continue; }
                        dj = rot_descentre(c[j].hz, paso);
                        if (dj < di || (dj == di && c[j].hz < c[i].hz)) {
                            cede = 1U; break;
                        }
                    }
                    if (!cede) { continue; }

                    /* La segunda de su casilla, con un nombre que no este
                     * ya puesto. */
                    ci = c[i].hz / paso;
                    for (j = 0U; j < k; j++) {
                        uint8_t t, repetido = 0U;
                        if (j == i || (c[j].hz / paso) != ci) { continue; }
                        if ((gana & (1UL << j)) != 0UL) { continue; }
                        for (t = 0U; t < k; t++) {
                            if ((gana & (1UL << t)) == 0UL) { continue; }
                            if (c[t].clave == c[j].clave) { repetido = 1U; break; }
                        }
                        if (repetido) { continue; }
                        if (nuevo == 0xFFU
                            || rot_descentre(c[j].hz, paso)
                               < rot_descentre(c[nuevo].hz, paso)) {
                            nuevo = j;
                        }
                    }
                    if (nuevo != 0xFFU) {
                        gana &= ~(1UL << i);
                        gana |=  (1UL << nuevo);
                    }
                }
            }
        }

        /*
         * 2c. Y LA QUE SUENA MANDA EN SU CASILLA - 15/10/2026.
         *
         * Porque 5.544.000 es multiplo exacto del paso y cae en el borde
         * de su casilla, que es el peor sitio posible. El porque largo
         * -y por que con tolerancia si y sin tolerancia no- esta en
         * spec_rotulos.h, encima de la declaracion.
         *
         * Va EL ULTIMO a proposito: primero se reparte la ventana con
         * numeros que no dependen del dial, y solo despues la sintonia
         * reclama SU casilla. Asi lo que hace el dial es siempre una
         * sola cosa y en un solo sitio, y si algun dia hay que quitarlo
         * se quita este bloque entero y lo de arriba sigue igual.
         */
        if (tune_hz != 0UL) {
            uint8_t sinto = 0xFFU;
            uint32_t dmin = 0UL;

            for (i = 0U; i < k; i++) {
                uint32_t d = rot_dif(c[i].hz, tune_hz);
                if (d > SPEC_ROT_SINTO) { continue; }
                if (sinto == 0xFFU || d < dmin
                    || (d == dmin && c[i].hz < c[sinto].hz)) {
                    sinto = i; dmin = d;
                }
            }
            if (sinto != 0xFFU && (gana & (1UL << sinto)) == 0UL) {
                uint32_t cs = c[sinto].hz / paso;
                for (j = 0U; j < k; j++) {
                    if (c[j].hz / paso == cs) { gana &= ~(1UL << j); }
                }
                gana |= (1UL << sinto);
            }
        }

        /*
         * NO HAY PASO 3 NI PASO 4, Y ESO ES EL ARREGLO - 15/10/2026.
         *
         * Aqui habia dos cosas mas: tirar casillas cuando sobraban, y
         * rellenar huecos con segundas de casillas ya puestas. Las dos
         * eran rankings, y un ranking entre cosas que se mueven cambia de
         * ganador sin que se mueva nadie importante. De ahi salian los
         * carteles que aparecian y desaparecian solos.
         *
         * Con el paso en span/(max-1) ya no sobra ninguna casilla, asi
         * que no hay nada que tirar; y rellenar con una segunda de una
         * casilla ya representada era meter dos nombres donde cabe uno.
         * Lo que queda es una regla sin rankings: UNA por casilla, la que
         * mande en ella, y punto.
         */
        for (i = 0U; i < k && nsal < max; i++) {
            if ((gana & (1UL << i)) == 0UL) { continue; }
            sal[nsal] = i; nsal++;
        }

        /* De vuelta al orden de frecuencia, que es el que espera el
         * dibujante para repartir filas de izquierda a derecha. */
        for (i = 1U; i < nsal; i++) {
            uint8_t v = sal[i];
            j = i;
            while (j > 0U && sal[j - 1U] > v) { sal[j] = sal[j - 1U]; j--; }
            sal[j] = v;
        }
        for (i = 0U; i < nsal; i++) { tmp[i] = c[sal[i]]; }
        for (i = 0U; i < nsal; i++) { c[i] = tmp[i]; }
        return nsal;
    }
}

/*
 * EL REPARTO, Y POR QUE EN DOS FILAS.
 *
 * Con treinta y dos emisoras en la peor ventana de 96 kHz y 756 pixeles,
 * los nombres se pisan. En la captura de referencia -un receptor de
 * esos de web- se ve el mismo problema sin resolver: "BSKSA Holy Quran"
 * y "All India Radio GOS" montados uno encima de otro.
 *
 * Aqui se hace lo que esa foto no hace: si un cartel se pisa con el
 * anterior de su fila, se prueba en la otra; si tambien se pisa, NO SE
 * PINTA. Un nombre a medias debajo de otro no informa, estorba, y encima
 * hace dudar del que si esta bien.
 *
 * LA RAYITA HASTA LA TRAZA. Sin ella un cartel desplazado para no pisar
 * al vecino señala una frecuencia que no es la suya, y eso es peor que no
 * ponerlo: la raya sale del punto exacto y el texto se va donde cabe.
 */
/*
 * ======================================================================
 * AQUI HUBO UN CARTEL PARA LA EMISORA SINTONIZADA, Y SE FUE - 15/10/2026
 * ======================================================================
 *
 * Lo meti porque el usuario encontro que el cartel de al lado del reloj
 * decia "BAF Beijing" y en el espectro no habia ninguno en esa
 * frecuencia. Parecia una falta evidente. Y durante una hora lo probe de
 * las tres formas posibles, y las tres fallaron por el mismo sitio:
 *
 *   - METIDO EN LA ELECCION, ganando su casilla: al cruzar el dial de
 *     una casilla a otra cambiaban DOS carteles de golpe: desaparecia uno
 *     y salia otro distinto con mover el dial un kilohercio.
 *   - PINTADO EL PRIMERO, para que los demas se apartaran: dejaba su
 *     marca de agua en el centro de su fila y se comia todo lo que tenia
 *     a su izquierda. Media fila desaparecia.
 *   - PINTADO EL ULTIMO y en otra fila, sumando en vez de sustituyendo:
 *     el cartel cambiaba de sitio al mover un kilohercio, y
 *     "porque hay dos san francisco uno debajo del otro en la misma
 *     frecuencia" cuando el de la lista decia lo mismo.
 *
 * Las tres son la misma cosa: ESTE CARTEL DEPENDE DEL DIAL, y todo lo
 * demas de la pantalla depende de la frecuencia. Mezclarlos hace que la
 * pantalla cambie al mover el mando, que es justo lo que el usuario llevaba
 * toda la tarde pidiendo que no pasara: que cada cosa tenga su posicion
 * fija y que el orden sea siempre el mismo.
 *
 * Asi que no esta. El nombre de lo que suena sale al lado del reloj, que
 * es su sitio y donde siempre estuvo. En el espectro hay una sola regla y
 * ninguna excepcion: un cartel por casilla, en la fila de su casilla, y
 * solo cambia cuando su casilla entra o sale por un borde.
 *
 * Dos requisitos que se contradicen no se resuelven a medias: se elige,
 * y se dice cual se ha elegido y por que.
 */
uint8_t spec_rotulos_draw(gfx2_surf_t *s, const spec_rotulos_t *rr,
                          const spec_rot_txt_t txt,
                          uint32_t center_hz, uint32_t span_hz,
                          int16_t x0, int16_t ancho, int16_t y, int16_t alto,
                          uint8_t *filas,
                          gfx2_rgba_t color, gfx2_rgba_t color_linea)
{
    /* La misma fuente que la regla de frecuencias (ver spec_chrome.c):
     * si los carteles fueran de otro tamaño, la pantalla pareceria dos
     * pantallas pegadas. */
    const gfx2_font_t *f = &font_ui_11;
    int16_t fin[SPEC_ROT_FILAS];   /* hasta donde llega lo pintado, por fila */
    int16_t fila_y[SPEC_ROT_FILAS];
    uint8_t nfilas;
    int16_t alto_fila;
    uint8_t i, puestos = 0U;

    if (filas != 0) {
        for (i = 0U; i < SPEC_ROT_MAX; i++) { filas[i] = 0xFFU; }
    }
    if (s == 0 || rr == 0 || txt == 0 || ancho <= 0 || alto <= 0) { return 0U; }

    /*
     * ======================================================================
     * Y SI ESTA BANDA NO TOCA LA FRANJA, NO HAY NADA QUE HACER - 24/10/2026.
     * ======================================================================
     *
     * Encontrado en la revision a fondo pedida por el usuario.
     *
     * Esto corre UNA VEZ POR BANDA del espectro, que con bandas de ocho
     * filas y un panel de 208 son veintiseis. La franja de carteles ocupa
     * las primeras 42 filas, o sea cinco bandas: en las otras veintiuna no
     * se pinta ni un pixel... pero se recorria la lista entera igual, y cada
     * cartel cuesta una DIVISION DE 64 BITS -spec_rotulos_x()- y un recorrido
     * del UTF-8 buscando glifos -gfx2_text_w()-.
     *
     * Y no lo tapaba el salto de bandas: las que se saltan por no haber
     * cambiado son justo las de arriba, donde no hay traza, que son las que
     * SI llevan carteles. Las que se montan son las de abajo, donde esto no
     * pinta nada.
     *
     * gfx2_band_hits() ya existia y lo usan las primitivas de gfx2 por este
     * mismo motivo, escrito en su propio comentario: "el callback de dibujo
     * se ejecuta una vez POR BANDA, asi que todo lo que no se corte pronto
     * se paga veinte veces". Aqui faltaba.
     *
     * Las cuatro filas de aire son por los glifos: el texto del ultimo
     * renglon puede bajar un pixel o dos por debajo de `alto` segun la
     * fuente, y cortar por lo justo seria cambiar lo que se ve.
     */
    if (!gfx2_band_hits(s, y, (int16_t)(alto + 4))) { return 0U; }

    /*
     * LOS RENGLONES SE REPARTEN EL ALTO QUE HAY, no al reves.
     *
     * Antes la altura de renglon era la de la fuente y el que llamaba
     * pedia tres por quince, o sea 46. Pero el sitio que hay no es un
     * numero que se pida: es el que haya hasta la primera raya de la
     * rejilla, que en este panel son 41 pixeles. Pedir 46 se saltaba esa
     * raya y la franja se comia dos huecos del espectro.
     *
     * Asi que se da la vuelta: llega el alto que hay y los renglones se
     * lo reparten. Con 42 y tres filas salen catorce por renglon, que es
     * justo la altura de linea de font_ui_11. Y si algun dia el panel
     * encoge, lo que pasa es que caben dos renglones en vez de tres - no
     * que la franja se salga.
     */
    nfilas = SPEC_ROT_FILAS;
    while (nfilas > 1U
           && (int16_t)(alto / (int16_t)nfilas) < (int16_t)(f->line_h - 1)) {
        nfilas--;
    }
    alto_fila = (int16_t)(alto / (int16_t)nfilas);
    if (alto_fila < 1) { return 0U; }
    for (i = 0U; i < SPEC_ROT_FILAS; i++) {
        uint8_t fl = (i < nfilas) ? i : (uint8_t)(nfilas - 1U);
        fin[i] = (int16_t)-32000;
        fila_y[i] = (int16_t)(y + (int16_t)fl * alto_fila);
    }

    for (i = 0U; i < rr->n && i < SPEC_ROT_MAX; i++) {
        int16_t px, w, tx;
        uint8_t fila;

        if (txt[i][0] == '\0') { continue; }

        px = spec_rotulos_x(spec_rotulos_hz(rr, i), center_hz, span_hz,
                            x0, ancho);
        /*
         * EL BORDE DERECHO, CLAVADO Y NO TIRADO - 15/10/2026.
         *
         * La ventana que se le pide a la base es CERRADA por los dos
         * lados: de center-span/2 a center+span/2, los dos incluidos. Y
         * la frecuencia de arriba del todo cae, por la propia recta, en
         * x0+ancho: un pixel mas alla del ultimo, que es x0+ancho-1.
         *
         * Tirarla seria perder una emisora que SI esta en la ventana por
         * medio pixel. Se clava en el ultimo. Lo que si se tira es lo que
         * esta de verdad fuera, que es otra cosa.
         */
        if (px == (int16_t)(x0 + ancho)) { px = (int16_t)(x0 + ancho - 1); }
        if (px < x0 || px >= (int16_t)(x0 + ancho)) { continue; }

        w  = gfx2_text_w(txt[i], f);
        /* Centrado sobre su frecuencia, y metido dentro de la traza: un
         * cartel a medio salir no se lee. */
        tx = (int16_t)(px - w / 2);
        if (tx < x0) { tx = x0; }
        if ((int16_t)(tx + w) > (int16_t)(x0 + ancho)) {
            tx = (int16_t)(x0 + ancho - w);
        }
        if (tx < x0) { continue; }      /* mas ancho que la traza entera */

        /*
         * LA FILA SALE DE LA FRECUENCIA, Y NO SE MUEVE DE AHI.
         *
         * PRIMER INTENTO: alternar por el INDICE en la lista. Zigzag
         * perfecto en una foto, y saltos en cuanto te mueves: entra un
         * nombre nuevo por la izquierda, todos los de detras corren un
         * sitio, y los siete cambian de fila a la vez.
         *
         * SEGUNDO: la fila sale de la CASILLA -la misma rejilla absoluta
         * con la que se eligen-, asi que una emisora no cambia de casilla
         * nunca y por tanto no cambia de fila nunca. Y como las casillas
         * van seguidas, el zigzag sale solo. Eso arreglo la REGLA...
         *
         * ...y el usuario siguio viendo carteles cambiar de fila, porque
         * aqui abajo quedaba una REPESCA: "si en tu fila no cabe, prueba
         * las otras". Un vecino nuevo que entra por un borde cambia lo
         * que hay pintado en una fila, y el de al lado se mudaba. O sea
         * que la fila volvia a depender de QUIEN MAS HAY. El mismo error
         * -atar algo a la lista en vez de a la emisora- tres veces en la
         * misma tarde, y las dos ultimas a tres lineas de distancia.
         *
         * Ya no hay repesca: su fila o ninguna. Con tres filas y una
         * casilla de 8 kHz -63 px- dos carteles de la misma fila estan a
         * 24 kHz, o sea 189 px, y un nombre de catorce letras mide
         * setenta: no caber solo puede pasarle a quien se cruce con el
         * cartel de la sintonia, que se pinta antes.
         *
         * Dos pixeles de aire entre carteles: pegados se leen como uno.
         */
        fila = spec_rotulos_fila(rr, i, span_hz);
        if (tx <= fin[fila]) { continue; }

        gfx2_line(s, px, (int16_t)(fila_y[fila] + alto_fila - 1),
                  px, (int16_t)(y + alto - 1), color_linea);
        gfx2_text(s, tx, fila_y[fila], txt[i], f, color);

        fin[fila] = (int16_t)(tx + w + 2);
        if (filas != 0) { filas[i] = fila; }
        puestos++;
    }
    return puestos;
}

/*
 * LA AUTOPRUEBA DE LA CUENTA, que es lo unico que se puede comprobar sin
 * mirar la pantalla. El reparto se mira en sim/rotulos.c, con un render.
 */
uint8_t spec_rotulos_selftest(void)
{
    const uint32_t C = 9600000UL, S = 96000UL;
    const int16_t X0 = 40, W = 756;

    /* El centro cae en el centro. */
    if (spec_rotulos_x(C, C, S, X0, W) != (int16_t)(X0 + W / 2)) { return 0U; }
    /* Los dos bordes, en los dos bordes. */
    if (spec_rotulos_x(C - S / 2UL, C, S, X0, W) != X0) { return 0U; }
    if (spec_rotulos_x(C + S / 2UL, C, S, X0, W) != (int16_t)(X0 + W)) { return 0U; }
    /* Y la mitad de la mitad, para cazar un factor de dos. */
    if (spec_rotulos_x(C - S / 4UL, C, S, X0, W) != (int16_t)(X0 + W / 4)) { return 0U; }
    /* Span cero no divide por cero. */
    if (spec_rotulos_x(C, C, 0UL, X0, W) != X0) { return 0U; }
    /*
     * Y UNA FRECUENCIA ALTA, que es donde se desborda un int32: 30 MHz
     * por 756 son 2,3e10. Si alguien quita el int64 de arriba, esto se da
     * la vuelta y el cartel se va al otro lado de la pantalla.
     */
    if (spec_rotulos_x(30000000UL, 30000000UL, S, X0, W)
        != (int16_t)(X0 + W / 2)) { return 0U; }
    return 1U;
}

/* ------------------------------------------- DE QUE COLOR VAN LOS CARTELES
 *
 * Y es verdad, y la causa es de SITIO. Los carteles se pintan DENTRO del
 * panel de la traza, que es casi negro en los ocho temas
 * (SPEC_LINE_BG = #000818, luz 7), pero el color salia de PAL_INK, que es
 * la tinta del CROMO. En un tema claro la tinta del cromo es NEGRA, porque
 * va sobre barras claras. Negro sobre negro.
 *
 * MEDIDO sobre las ocho paletas, diferencia de luminancia contra el fondo
 * del panel (el minimo que se exige, 48, es el mismo que usa mapa_bin.c
 * para el contraste del punto sobre el mar):
 *
 *     tema           ink   ink_dim   trace
 *     Oscura         234     156      112
 *     Contrastada    237     173      112
 *     Ambar          235     165      112
 *     Fria           236     161      112
 *     Oliva            7      32       59   <- tinta negra
 *     Naranja          7      25       61   <- tinta negra
 *     Fosforo        172     103      104
 *     Chemistry        7       7      138   <- tinta negra
 *
 * Tres temas de ocho con la tinta invisible, y el ink_dim de otros dos muy
 * justo. El color que SIEMPRE vale es `trace`: el de la traza, que es el
 * color que ese tema eligio para dibujar precisamente sobre ese panel. Por
 * eso es el suplente y no un blanco fijo: un blanco fijo seria ignorar el
 * tema, y lo que se pide es lo contrario.
 *
 * Y el ink se conserva donde se ve, asi que los cinco temas en los que
 * esto ya funcionaba no cambian ni un pixel.
 */
static int32_t rot_luz(uint32_t c)
{
    return (int32_t)((299UL * ((c >> 16) & 0xFFUL)
                    + 587UL * ((c >> 8) & 0xFFUL)
                    + 114UL * (c & 0xFFUL)) / 1000UL);
}

static uint8_t rot_contrasta(uint32_t a, uint32_t b)
{
    int32_t d = rot_luz(a) - rot_luz(b);

    if (d < 0) { d = -d; }
    return (d >= (int32_t)SPEC_ROT_CONTRASTE) ? 1U : 0U;
}

/* Mezcla: `cuanto` de 0 a 255, 0 = todo `b`, 255 = todo `a`. */
static uint32_t rot_mezcla(uint32_t a, uint32_t b, uint8_t cuanto)
{
    uint32_t r = 0UL;
    uint8_t  i;

    for (i = 0U; i < 3U; i++) {
        uint8_t desp = (uint8_t)(i * 8U);
        uint32_t ca = (a >> desp) & 0xFFUL;
        uint32_t cb = (b >> desp) & 0xFFUL;
        uint32_t cc = (ca * (uint32_t)cuanto + cb * (255UL - cuanto)) / 255UL;

        r |= (cc & 0xFFUL) << desp;
    }
    return r;
}

/*
 * SUBE `c` HACIA EL BLANCO -o lo baja hacia el negro, segun el fondo-
 * hasta que se separe de `fondo` por lo menos `objetivo`.
 *
 * El tono se conserva porque la mezcla es con blanco o con negro puros:
 * el verde oliva sigue siendo verde, solo que claro. Un tema no se
 * respeta usando su color exacto aunque no se lea; se respeta usando SU
 * color en una claridad en la que se lee.
 */
static uint32_t rot_sube(uint32_t c, uint32_t fondo, int32_t objetivo)
{
    int32_t lf = rot_luz(fondo), lc = rot_luz(c);
    int32_t falta, margen, k;

    if (lf < 128) {                      /* fondo oscuro: hacia el blanco */
        falta  = objetivo - (lc - lf);
        margen = 255 - lc;
        if (falta <= 0 || margen <= 0) { return c; }
        k = (falta * 255 + margen - 1) / margen;
        if (k > 255) { k = 255; }
        return rot_mezcla(0xFFFFFFUL, c, (uint8_t)k);
    }
    falta  = objetivo - (lf - lc);       /* fondo claro: hacia el negro */
    margen = lc;
    if (falta <= 0 || margen <= 0) { return c; }
    k = (falta * 255 + margen - 1) / margen;
    if (k > 255) { k = 255; }
    return rot_mezcla(0x000000UL, c, (uint8_t)k);
}

void spec_rotulos_tinta(uint32_t fondo, uint32_t ink, uint32_t ink_dim,
                        uint32_t traza, uint32_t *texto, uint32_t *raya)
{
    uint32_t t, l;

    /*
     * EL SUPLENTE NO ES LA TRAZA TAL CUAL, ES LA TRAZA SUBIDA.
     *
     * Medido: con la traza a secas, Oliva da 59 de contraste y Naranja 61.
     * Eso pasa el minimo de 48 -o sea, se ve- pero es un verde oscuro
     * sobre negro con letra de once pixeles, y "se ve" no es lo que pidio
     * el usuario. Subiendola a SPEC_ROT_CONTRASTE_BUENO se lee igual que en
     * los temas oscuros y sigue siendo el color del tema.
     */
    t = rot_contrasta(ink, fondo) ? ink
                                  : rot_sube(traza, fondo,
                                             (int32_t)SPEC_ROT_CONTRASTE_BUENO);
    if (!rot_contrasta(t, fondo)) {
        /* Ni la tinta ni la traza: no queda mas remedio que irse al blanco
         * o al negro, el que se vea. Hoy no pasa con ninguna de las ocho
         * paletas; esta para que un tema nuevo mal calculado deje los
         * nombres FEOS y no INVISIBLES. */
        t = (rot_luz(fondo) < 128) ? 0xFFFFFFUL : 0x000000UL;
    }
    /*
     * LA RAYITA de cada cartel va mas apagada que el texto -marca la
     * frecuencia exacta y no debe competir con el nombre- pero tiene que
     * verse igual. Se prueba el ink_dim del tema; si no llega, se baja el
     * texto hacia el fondo hasta donde el contraste aguante, y si tampoco,
     * se queda del color del texto. Un cartel sin rayita flota.
     */
    l = ink_dim;
    if (!rot_contrasta(l, fondo)) { l = rot_mezcla(t, fondo, 170U); }
    if (!rot_contrasta(l, fondo)) { l = t; }

    if (texto != 0) { *texto = t; }
    if (raya  != 0) { *raya  = l; }
}
