#include "jtty_rx.h"
#include <string.h>
#include <math.h>

/*
 * Ver jtty_rx.h para el porque de cada numero de la cadena. Aqui esta el
 * como.
 */

/* El oscilador de 1.500 Hz a 12 kHz: ocho muestras EXACTAS por ciclo, o
 * sea una tabla y ningun error de fase acumulado. Copiado de wspr_rx.c a
 * proposito - es el mismo tono y la misma tasa-. */
#define R2 0.70710678f
static const float k_nco_c[8] = { 1.0f,  R2,  0.0f, -R2, -1.0f, -R2,  0.0f,  R2 };
static const float k_nco_s[8] = { 0.0f,  R2,  1.0f,  R2,  0.0f, -R2, -1.0f, -R2 };

static struct {
    jtty_rx_ram_t *ram;
    uint8_t  on;

    /* mezcla y diezmado */
    uint8_t  nco;
    uint8_t  cuenta;
    float    aci, acq;

    /* La ventana deslizante vive en la memoria prestada: ver jtty_rx.h. */
    uint8_t  vpos;        /* donde va la siguiente muestra */
    uint8_t  desde_fila;  /* muestras desde la ultima fila */

    /* el anillo de filas */
    uint16_t fila;        /* donde se escribio la ultima */
    uint32_t filas;       /* cuantas van en total, para saber si hay trama */

    /*
     * LOS CANDIDATOS QUE EL BUCLE PRINCIPAL TIENE QUE RESOLVER - UNA COLA.
     *
     * *** 28/09/2026, por el dueño del proyecto viendo la radio en antena:
     * "salen cosas con ...". Los puntos suspensivos quieren decir "este
     * mensaje se quedo sin final", y que salgan tantos quiere decir que se
     * pierden tramas. ***
     *
     * Aqui habia UN candidato, y mientras estuviera pendiente la busqueda
     * de sincronismo NO SE HACIA - habia un `continue` antes de ella-. Con
     * una estacion en la banda no se nota; con nueve, que es lo que hay en
     * 14.090, dos tramas que acaban con pocas filas de diferencia se
     * pisaban y la segunda se perdia entera. Y el bucle principal tarda uno
     * o dos cuadros en venir a por ella, o sea que la ventana ciega no era
     * corta.
     *
     * CUANTOS. Aqui puse seis razonando sobre el ritmo MEDIO: a 4,3
     * candidatos por segundo y sesenta resueltos por segundo sobra sitio
     * quince veces. El ritmo medio no era el problema.
     *
     * *** Lo enseño sim/jtty_cadena.c con nueve estaciones en la banda,
     * que es lo que hay en 14.090: de las nueve salieron OCHO, y la que
     * faltaba no fallo por ruido ni por el Viterbi - su candidato se tiro
     * porque la cola estaba llena-. ***
     *
     * El ritmo de PUNTA es lo que manda, y no tiene holgura ninguna: una
     * fila son 192 muestras, el bucle principal pasa cada dos filas y
     * resuelve dos, y el sincronismo puede dar un candidato en CADA fila.
     * O sea que se entra a la misma velocidad a la que se sale y cualquier
     * rafaga desborda. Con nueve estaciones cuyas tramas acaban en medio
     * segundo, la rafaga es la norma.
     *
     * Medido con ese banco, con las nueve estaciones:
     *
     *      6 candidatos tirados 22      16 -> 7
     *      8 candidatos tirados 18      24 -> 0
     *     12 candidatos tirados 11      32 -> 0
     *
     * Veinticuatro, que son 0,38 s de retraso admitido y 192 bytes. Y el
     * banco mira que se tiren CERO, no que "pasen suficientes": esa cuenta
     * no existia y por eso el modo podia perder decodificados sin que
     * ninguna medida se moviera.
     */
#define JTTY_CAND_N 24U
    uint16_t cand_fila[JTTY_CAND_N];
    /* Y el contador absoluto, que es lo que se le da al que junta los
     * mensajes: el indice del anillo da la vuelta. Ver jtty_rx_r_t.fila. */
    uint32_t cand_filas[JTTY_CAND_N];
    uint8_t  cand_base[JTTY_CAND_N];
    uint8_t  cand_q[JTTY_CAND_N];
    uint8_t  cand_cab, cand_col;   /* anillo: se mete por cab, se saca por col */

    /*
     * LO ULTIMO QUE SALIO BIEN, para no darlo dos veces.
     *
     * *** 28/09/2026, de una foto de la pantalla con la radio en antena:
     * "WA4MB WA4MB DE IK DE IK 3CHK 3CHK IK3CH IK3CHK". Cada atomo sale
     * DOS VECES. ***
     *
     * La primera guarda miraba la FILA y el BIN: se descartaba un
     * candidato a menos de cuatro filas del ultimo bueno EN EL MISMO BIN.
     * Eso se lleva el duplicado en el tiempo -el sincronismo pasa el
     * umbral en la fila que toca y en la de al lado- pero no el duplicado
     * en FRECUENCIA: una señal que cae entre dos medios bines puntua igual
     * de bien en los dos, y entonces son dos bases distintas y la guarda no
     * las relaciona.
     *
     * El banco no podia enseñar esto porque manda una trama cada vez y
     * clavada en su sitio. Hizo falta la antena.
     *
     * Ahora se compara LO DECODIFICADO, que es lo que de verdad dice si es
     * la misma trama: los 34 bits de carga. Y la ventana es de un segundo
     * en vez de 64 ms, porque dos tramas seguidas de un mensaje largo van a
     * 1,888 s - o sea que un atomo identico a menos de un segundo del
     * anterior no puede ser el siguiente, tiene que ser el mismo-.
     */
    uint8_t  ult_carga[JTTY_CARGA_BITS];
    uint32_t ult_filas;    /* el contador de filas cuando salio */
    uint8_t  ult_vale;

    uint32_t n_probados;
    uint32_t n_buenos;
    /*
     * CANDIDATOS QUE NO CABEN EN LA COLA, y por que hay que contarlos.
     *
     * *** 28/09/2026: lo enseño sim/jtty_cadena.c con nueve estaciones en
     * la banda, que es lo que hay en 14.090. De las nueve salieron ocho, y
     * la que faltaba no fallo por ruido ni por el Viterbi: su candidato se
     * tiro porque la cola estaba llena. ***
     *
     * Esto no se veia en ninguna cuenta. `n_probados` cuenta los que se
     * PRUEBAN y `n_buenos` los que salen, asi que un candidato tirado antes
     * de probarse no aparecia en ningun sitio: el modo perdia
     * decodificados y las dos cuentas seguian cuadrando entre ellas.
     *
     * Es la misma clase de error que llevo persiguiendo todo el proyecto -
     * una medida que no mide lo que hace falta y por eso no puede fallar-,
     * solo que aqui no era un comentario falso sino un numero que no
     * existia.
     */
    uint32_t n_perdidos;
} s;

/* ---------------------------------------------------------------- */

/*
 * UNA VUELTA ENTERA DE 64 PUNTOS. Ver jtty_rx.h para por que basta con
 * esto en vez de la DFT precalculada entera.
 */
static void giro_tabla(void)
{
    uint8_t k;

    for (k = 0U; k < (uint8_t)JTTY_RX_BINS; k++) {
        float w = -6.28318531f * (float)k / (float)JTTY_RX_BINS;

        s.ram->giro_c[k] = cosf(w);
        s.ram->giro_s[k] = sinf(w);
    }
}

void jtty_rx_start(jtty_rx_ram_t *ram, float fs)
{
    (void)fs;   /* la tasa es fija: ver JTTY_RX_DIEZMA */
    memset(&s, 0, sizeof s);
    s.ram = ram;
    if (ram == (jtty_rx_ram_t *)0) { return; }
    memset(ram, 0, sizeof *ram);
    giro_tabla();   /* despues del memset: escribe DENTRO de `ram` */
    s.on = 1U;
}

void jtty_rx_stop(void)      { s.on = 0U; s.ram = (jtty_rx_ram_t *)0; }
uint8_t jtty_rx_activo(void) { return s.on; }
uint32_t jtty_rx_perdidos(void) { return s.n_perdidos; }

uint32_t jtty_rx_probados(void) { return s.n_probados; }
uint32_t jtty_rx_buenos(void)   { return s.n_buenos; }

/* ---------------------------------------------------------------- */

/* La potencia de la ventana actual en cada uno de los 32 medios bines. */
static void fila_nueva(void)
{
    float *dst;
    uint8_t b, m;

    s.fila = (uint16_t)((s.fila + 1U) % (uint16_t)JTTY_RX_FILAS);
    dst = s.ram->espectro[s.fila];

    for (b = 0U; b < (uint8_t)JTTY_RX_BINS; b++) {
        float re = 0.0f, im = 0.0f;
        uint8_t k = s.vpos;   /* la mas vieja de la ventana */

        /*
         * EL CERO VA EN EL MEDIO, no en el bin 0.
         *
         * Primera version: el bin b era la frecuencia +b*15,625 Hz, o sea
         * que la ventana iba de 0 hacia arriba y las frecuencias por DEBAJO
         * de la de mezcla caian en los bines del final, dando la vuelta.
         * Con la señal centrada -que es donde se pone- sus cuatro tonos
         * quedaban partidos entre el principio y el final: el grupo de
         * cuatro bines seguidos que busca el sincronismo NO EXISTIA, y no
         * decodificaba ni una trama sin ruido.
         *
         * Con el cero en el medio, el bin b es (b - 32) medios bines, y
         * (b-32) modulo 64 es (b+32) modulo 64, que es el indice de giro.
         */
        for (m = 0U; m < (uint8_t)JTTY_RX_MS_SIM; m++) {
            float i = s.ram->vi[k], q = s.ram->vq[k];
            uint8_t g = (uint8_t)(((uint16_t)(b + 32U) * (uint16_t)m)
                                  & ((uint8_t)JTTY_RX_BINS - 1U));
            float gc = s.ram->giro_c[g], gs = s.ram->giro_s[g];

            re += i * gc - q * gs;
            im += i * gs + q * gc;
            k = (uint8_t)((k + 1U) & ((uint8_t)JTTY_RX_MS_SIM - 1U));
        }
        dst[b] = re * re + im * im;
    }
    s.filas++;
}

/* La fila del anillo que esta `atras` filas por detras de la ultima. */
static const float *fila_de(uint16_t atras)
{
    uint16_t i = (uint16_t)((s.fila + (uint16_t)JTTY_RX_FILAS - atras)
                            % (uint16_t)JTTY_RX_FILAS);
    return s.ram->espectro[i];
}

/*
 * LA CORRELACION DE SINCRONISMO.
 *
 * Para un arranque y una frecuencia, cuanta de la potencia de los cuatro
 * tonos esta en el tono que el vector de sincronismo dice que toca. Sale
 * 0,25 con ruido puro -uno de cuatro- y 1,0 con una señal limpia, asi que
 * el umbral se lee solo y no depende del nivel de la señal.
 *
 * Dividir por la potencia de los CUATRO tonos y no por la total de la
 * ventana es lo que la hace inmune a un portador fuerte al lado: un
 * portador que no este en estos cuatro bines no entra ni en el numerador
 * ni en el denominador.
 */
static float sync_mide(uint16_t atras_ini, uint8_t base)
{
    float bueno = 0.0f, todo = 0.0f;
    uint8_t i;

    for (i = 0U; i < (uint8_t)JTTY_SYNC_SIM; i++) {
        const float *f = fila_de((uint16_t)(atras_ini - 2U * i));
        uint8_t t;

        for (t = 0U; t < 4U; t++) {
            float p = f[base + 2U * t];

            todo += p;
            if (t == k_jtty_sync[i]) { bueno += p; }
        }
    }
    if (todo <= 0.0f) { return 0.0f; }
    return bueno / todo;
}


/*
 * EL UMBRAL. 0,45, Y DE DONDE SALE - 28/09/2026.
 *
 * *** Por el dueño del proyecto: "ve a por ello", sobre ganar
 * sensibilidad. ***
 *
 * Estaba en 0,55 "porque con ruido puro no se pasa nunca", que es verdad y
 * es la pregunta equivocada: lo que hay que maximizar no es el silencio
 * con ruido, es cuantas tramas de verdad se cogen sin que se cuele basura.
 * Y quien de verdad decide si un candidato vale no es esto: son los 12
 * bits de CRC mas el bit reservado, mas las reglas de gramatica que tiran
 * los atomos imposibles. El umbral solo esta para no llamar al Viterbi
 * cincuenta veces por segundo.
 *
 * Medido (ver /tmp/jtty_sens.c y jtty_falsos.c del dia, y la tabla del
 * CAMBIOS), con desvanecimiento de onda corta:
 *
 *     umbral   tramas a -10 dB   candidatos/s de ruido   falsos
 *      0,55         12,5 %              0,02               0
 *      0,45         32,5 %              4,3                0 en 10 min
 *      0,40         45,0 %             27,6                0,2 por minuto
 *
 * 0,45 es donde la sensibilidad sube casi tres decibelios y los falsos
 * siguen siendo cero. En 0,40 se gana otro poco y empiezan a colarse, que
 * es exactamente lo que no puede pasar en una lista de indicativos.
 *
 * SE PROBO TAMBIEN una metrica normalizada por simbolo -el cociente en
 * cada uno de los trece, promediado, en vez de la suma entre la suma-,
 * pensando que el desvanecimiento hacia que tres simbolos fuertes
 * decidieran por los trece. Daba lo mismo a igualdad de sensibilidad y con
 * el triple de candidatos. No se ha quedado: la idea era razonable y el
 * numero dijo que no.
 */
#define JTTY_SYNC_UMBRAL 0.45f

void jtty_rx_mete(const float *audio, uint32_t n)
{
    uint32_t k;

    if (!s.on || audio == (const float *)0) { return; }

    for (k = 0UL; k < n; k++) {
        s.aci += audio[k] * k_nco_c[s.nco];
        s.acq -= audio[k] * k_nco_s[s.nco];
        s.nco = (uint8_t)((s.nco + 1U) & 7U);

        s.cuenta++;
        if (s.cuenta < (uint8_t)JTTY_RX_DIEZMA) { continue; }
        s.cuenta = 0U;

        /* Una muestra de 500 Hz a la ventana. */
        s.ram->vi[s.vpos] = s.aci / (float)JTTY_RX_DIEZMA;
        s.ram->vq[s.vpos] = s.acq / (float)JTTY_RX_DIEZMA;
        s.vpos = (uint8_t)((s.vpos + 1U) & ((uint8_t)JTTY_RX_MS_SIM - 1U));
        s.aci = 0.0f; s.acq = 0.0f;

        s.desde_fila++;
        if (s.desde_fila < (uint8_t)(JTTY_RX_MS_SIM / 2U)) { continue; }
        s.desde_fila = 0U;

        fila_nueva();

        /*
         * Y SE BUSCA LA TRAMA QUE ACABARIA AQUI. Su primer simbolo estaria
         * 116 filas por detras. Solo se prueba esa: cada fila nueva trae su
         * propio candidato, asi que en un segundo se han probado todos los
         * arranques posibles sin repetir ninguno.
         */
        if (s.filas < (uint32_t)JTTY_RX_FILAS_TRAMA) { continue; }

        {
            uint16_t atras = (uint16_t)(JTTY_RX_FILAS_TRAMA - 1U);
            uint8_t  b, mejor_b = 0U;
            float    mejor = 0.0f;

            /* El tono 3 esta 6 medios bines por encima del 0. */
            for (b = 0U; b + 6U < (uint8_t)JTTY_RX_BINS; b++) {
                float m = sync_mide(atras, b);

                if (m > mejor) { mejor = m; mejor_b = b; }
            }
            /*
             * Aqui ya no se descarta nada: el sincronismo no sabe todavia
             * QUE trama es, y descartar por "se parece a la anterior" es
             * justo lo que dejaba pasar los duplicados en frecuencia. La
             * criba esta abajo, con los 34 bits ya decodificados.
             */
            if (mejor >= JTTY_SYNC_UMBRAL) {
                uint8_t sig = (uint8_t)((s.cand_cab + 1U) % (uint8_t)JTTY_CAND_N);

                /*
                 * Si la cola esta llena se pierde el candidato NUEVO, no el
                 * viejo: el viejo ya lleva rato esperando y su trama esta
                 * mas cerca de salirse del anillo de filas.
                 *
                 * Y SE CUENTA, que es lo que faltaba. Ver n_perdidos.
                 */
                if (sig == s.cand_col) { s.n_perdidos++; }
                if (sig != s.cand_col) {
                    s.cand_fila[s.cand_cab]  = s.fila;
                    s.cand_filas[s.cand_cab] = s.filas;
                    s.cand_base[s.cand_cab] = mejor_b;
                    s.cand_q[s.cand_cab]    = (uint8_t)(mejor * 100.0f);
                    s.cand_cab = sig;
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- */

uint8_t jtty_rx_saca(jtty_rx_r_t *out)
{
    uint8_t carga[JTTY_CARGA_BITS];
    uint16_t atras;
    uint8_t j, base, q;

    if (!s.on || s.cand_cab == s.cand_col || out == (jtty_rx_r_t *)0) {
        return 0U;
    }

    base = s.cand_base[s.cand_col];
    q    = s.cand_q[s.cand_col];
    out->fila = s.cand_filas[s.cand_col];

    /*
     * Las filas del candidato, contadas desde la ULTIMA que hay ahora - que
     * no es la que habia cuando se apunto, porque el audio no ha parado-.
     * Por eso se guardo el indice de fila y no un "cuantas atras": lo
     * segundo se habria movido debajo.
     */
    {
        uint16_t d = (uint16_t)((s.fila + (uint16_t)JTTY_RX_FILAS
                                 - s.cand_fila[s.cand_col])
                                % (uint16_t)JTTY_RX_FILAS);
        atras = (uint16_t)(d + JTTY_RX_FILAS_TRAMA - 1U);
    }
    if (atras >= (uint16_t)JTTY_RX_FILAS) {
        /* Se ha ido del anillo: el bucle principal tardo demasiado. Mejor
         * perderla que decodificar filas de otra trama. */
        s.cand_col = (uint8_t)((s.cand_col + 1U) % (uint8_t)JTTY_CAND_N);
        return 0U;
    }

    for (j = 0U; j < (uint8_t)JTTY_SIMBOLOS; j++) {
        const float *f = fila_de((uint16_t)(atras - 2U * (JTTY_SYNC_SIM + j)));
        uint8_t t;

        for (t = 0U; t < 4U; t++) { s.ram->energias[j][t] = f[base + 2U * t]; }
    }

    s.cand_col = (uint8_t)((s.cand_col + 1U) % (uint8_t)JTTY_CAND_N);
    s.n_probados++;

    /*
     * Lista 4 y no 1: el codigo es tail-biting, asi que el mejor camino que
     * cierra el circulo no es siempre el correcto. Con lista 1 se pierden
     * decodificados que estan ahi; el coste es mirar cuatro CRC en vez de
     * uno. Ver el comentario de jtty_fec_descodifica().
     */
    if (!jtty_fec_descodifica((const float (*)[4])s.ram->energias,
                              &s.ram->fec, 4U, carga)) {
        return 0U;
    }
    if (!jtty_msg_lee(carga, &out->atomo)) { return 0U; }

    /*
     * LA CRIBA DE DUPLICADOS, con los bits en la mano. Ver ult_carga.
     *
     * 60 filas son 960 ms. Dos tramas seguidas de un mensaje van a 1,888 s,
     * asi que una carga identica a menos de un segundo de la anterior es la
     * MISMA trama vista dos veces, no la siguiente.
     */
    if (s.ult_vale && (s.filas - s.ult_filas) < 60UL) {
        uint8_t k, igual = 1U;

        for (k = 0U; k < (uint8_t)JTTY_CARGA_BITS; k++) {
            if (s.ult_carga[k] != carga[k]) { igual = 0U; break; }
        }
        if (igual) { return 0U; }
    }
    {
        uint8_t k;

        for (k = 0U; k < (uint8_t)JTTY_CARGA_BITS; k++) {
            s.ult_carga[k] = carga[k];
        }
    }
    s.ult_filas = s.filas;
    s.ult_vale = 1U;

    /*
     * EL DESVIO ES EL DE LA SEÑAL, NO EL DEL TONO 0.
     *
     * `base` es donde cae el tono mas bajo, y la primera version devolvia
     * eso: con la emision perfectamente centrada salia "-46 Hz", que es
     * verdad sobre el tono 0 y mentira sobre lo que el usuario lee, que
     * es "cuanto me tengo que mover". El centro de la señal esta entre el
     * tono 1 y el 2, o sea tres medios bines por encima del tono 0.
     *
     * Y la cuenta va en enteros exactos: 15,625 Hz son 125/8, asi que
     * multiplicar por 125 y dividir por 8 no pierde nada. Con 15625/1000
     * la division truncaba y el desvio salia corto hacia el cero.
     */
    out->hz = (int16_t)((((int16_t)base + 3 - (int16_t)(JTTY_RX_BINS / 2U))
                         * 125) / 8);
    out->calidad = q;
    s.n_buenos++;
    return 1U;
}
