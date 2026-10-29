/*
 * El lector del fichero .map. El porque de cada cosa esta en mapa_bin.h.
 */
#include "mapa_bin.h"
#include "idioma.h"
#include <string.h>

/* La misma suma que usa la zona alta: FNV-1a de 32 bits. No es un CRC y no
 * pretende serlo - aqui solo hay que notar que el fichero se ha copiado
 * mal o esta a medio escribir. */
#define SUMA_INICIO 0x811C9DC5UL
static uint32_t suma(const uint8_t *b, uint32_t n, uint32_t v)
{
    uint32_t i;
    for (i = 0U; i < n; i++) {
        v ^= (uint32_t)b[i];
        v *= 16777619UL;
    }
    return v;
}

static uint8_t  s_buf[MAPA_BIN_RAM];
static uint32_t s_n;                     /* bytes de trazos en s_buf */
static uint16_t s_anillos_n;             /* de esos, los del mapa de bits */
static uint16_t s_relleno_n;             /* trazos con el bit de relleno */
static uint32_t s_col[MAPA_BIN_COLORES];
static uint8_t  s_estilo;          /* MAPA_ESTILO_*, byte 57 de la cabecera */
static uint16_t s_trazos, s_puntos;
static uint32_t s_largo;                 /* lo que mide el .map de verdad */

/*
 * LA TABLA DE NIVELES Y LA VENTANA PUESTA.
 *
 * Se guarda la tabla -no los cuerpos- porque son 16 bytes por nivel y
 * evita volver a leer la cabecera de la flash cada vez que se cambia de
 * zoom. Los cuerpos de los niveles troceados viven en la flash y de ellos
 * solo pasa a la RAM la ventana.
 */
typedef struct {
    uint16_t zoom;
    uint8_t  nx, ny;      /* 0,0 = sin trocear */
    uint32_t off, tam;    /* desde el principio del .map */
    /*
     * Bytes de MAPA DE BITS DE ANILLOS pegados al final de `tam`, detras
     * del cero que cierra la tabla de trazos. 0 = este nivel no trae.
     * Ver el comentario gordo de mapa_bin_trazo().
     */
    uint16_t anillos;
} nivel_t;

static nivel_t s_niv[MAPA_BIN_NIV_MAX];
static uint8_t s_niveles;
/*
 * LA PARIDAD DE LAS CASILLAS - 15/10/2026, para rellenar al acercarse.
 *
 * Dos bytes de la cabecera que hasta hoy eran relleno a cero:
 *   58  mascara de niveles que traen el bloque (bit i = nivel i)
 *   59  bytes por casilla
 *
 * El bloque va DENTRO del `tam` del nivel, pegado al final, asi que no se
 * ha tocado la version del formato: un firmware viejo lee la tabla de
 * trazos igual, se para en el cero que la cierra y no se entera. Ver
 * tools/mapa_pack.py y el comentario de mapa_bin_par_lee().
 */
static uint8_t s_par_masc;
static uint8_t s_par_bytes;
static uint8_t s_vis_fuera;   /* casillas visibles que no cupieron */
/*
 * Y EL PEOR DE TODA LA SESION, QUE ES EL QUE SE PUEDE MIRAR - 28/10/2026.
 *
 * *** s_vis_fuera se contaba desde el 16/10 "para que esto se pudiera ver
 * sin una foto", y NO LO LEIA NADIE: ni getter, ni fila, ni nada. Doce
 * dias contando para el forro. Es el mismo fallo que este cuaderno ya
 * tiene escrito dos veces -una funcion de reinicio que nadie dispara, un
 * contador que nadie mira- y van tres. ***
 *
 * Y NO BASTA CON SACAR s_vis_fuera TAL CUAL, porque se pone a cero en
 * cada recarga: para verlo habria que estar mirando la fila justo en la
 * ventana mala, que es pedirle al usuario que acierte. Lo que se enseña
 * es el PEOR de la sesion, que es el que contesta a "¿le pasa a mi mapa
 * en mi zona?" sin tener que cronometrar nada.
 *
 * LO QUE MIDE DE VERDAD, y la primera cuenta que hice estaba mal. Sume
 * las casillas que tocan la ventana MAS EL MARGEN -12.770 bytes al zoom
 * 8- y di por hecho que hoy se caia. No se cae: el cargador mete primero
 * las VISIBLES y solo esas cuentan como perdidas; las del margen que no
 * caben solo adelantan la siguiente recarga, que no se ve.
 *
 * Recorriendo las ventanas posibles de los cien mapas, las visibles del
 * peor caso suman 10.195 bytes contra los 10.240 que hay:
 *
 *     peor ventana, solo lo visible     10.195 B
 *     el buffer                         10.240 B
 *     margen                                45 B
 *
 * O sea que hoy NO falta ninguna, y el contador dira cero. Pero 45 bytes
 * de 10.240 no son un margen, son suerte: un .map con un poco mas de
 * detalle en una sola ventana ya empieza a perder costa. Por eso esto se
 * ve ahora, y por eso la cuenta esta escrita aqui con el numero delante.
 */
static uint8_t s_vis_fuera_max;
static uint8_t s_idx_puesto = 0xFFU;      /* que nivel esta cargado */
static uint16_t s_zoom_puesto;           /* el zoom del nivel cargado */
static int16_t  s_vx0, s_vy0, s_vx1, s_vy1;  /* la ventana, en 1/32 de grado */
static uint8_t  s_vent_vale;             /* la ventana de arriba sirve */
static uint16_t s_recargas;

static mapa_bin_lee_t s_lee;
static uint32_t       s_base;
static uint8_t  s_hay;
static mapa_bin_r_t s_estado = MAPA_BIN_NADA;

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/*
 * ¿SE VERIA EL PUNTO SOBRE ESTE MAR? - por el usuario, al cerrar el formato.
 *
 * Si alguien se hace un mapa con el punto casi del color del fondo, las
 * marcas desaparecen y desde fuera parece que la radio no decodifica nada,
 * que es el peor fallo posible: uno que miente sobre el receptor.
 *
 * Se mide con la luminancia de siempre -la de la television, 0,299 R +
 * 0,587 G + 0,114 B, la misma que usa el resto de la radio- y se pide una
 * diferencia de 48 sobre 255. Cuarenta y ocho es poco mas de un 18 % y es
 * lo que separa "oscuro sobre oscuro" de "se ve": el par mas apretado de
 * los que trae el firmware de serie -el salmon apagado de WSPR sobre el
 * azul del mar- se lleva 61.
 */
#define CONTRASTE_MINIMO 48L

static int32_t luz(uint32_t c)
{
    int32_t r = (int32_t)((c >> 16) & 0xFFUL);
    int32_t g = (int32_t)((c >> 8) & 0xFFUL);
    int32_t b = (int32_t)(c & 0xFFUL);
    return (299L * r + 587L * g + 114L * b) / 1000L;
}

static uint8_t contrasta(uint32_t marca, uint32_t mar)
{
    int32_t d = luz(marca) - luz(mar);
    if (d < 0) { d = -d; }
    return (uint8_t)(d >= CONTRASTE_MINIMO);
}

/* ------------------------------------------------------------------ */
mapa_bin_r_t mapa_bin_pon(const uint8_t *datos, uint32_t n)
{
    uint32_t i, trazos_b, dicho, calc;
    uint16_t niveles;
    const uint8_t *tr;

    s_hay = 0U; s_n = 0UL; s_anillos_n = 0U;
    s_largo = 0UL; s_trazos = 0U; s_puntos = 0U;

    if (datos == 0 || n < (MAPA_BIN_CAB + 4UL)) { return (s_estado = MAPA_BIN_VACIO); }
    if (memcmp(datos, MAPA_BIN_MARCA, 8U) != 0) { return (s_estado = MAPA_BIN_MARCA_MAL); }
    if (datos[8] != (uint8_t)MAPA_BIN_VERSION)  { return (s_estado = MAPA_BIN_VERSION_MAL); }

    niveles = datos[9];
    if (niveles == 0U || niveles > MAPA_BIN_NIV_MAX) { return (s_estado = MAPA_BIN_VACIO); }

    /*
     * DONDE ACABA EL FICHERO LO DICE EL FICHERO, NO QUIEN LO TRAE -
     * 12/10/2026.
     *
     * La primera version ponia la suma en datos[n-4] a secas, o sea
     * confiaba en que `n` fuera EXACTAMENTE el tamano del .map. Lo cazo el
     * banco nuevo: un fichero que llega con relleno detras -porque el
     * sistema de ficheros redondea a bloque, o porque la herramienta que
     * lo escribio alineo- se daba por roto con "la suma no cuadra", que
     * ademas es una pista falsa: el fichero estaba perfecto.
     *
     * Asi que el final se saca de la tabla de niveles, que es parte del
     * propio fichero. `n` pasa a ser lo que de verdad es - cuanto hay
     * disponible para leer- y no una afirmacion sobre el contenido.
     *
     * Y EL ORDEN SE MANTIENE. Mirar la tabla aqui no es creersela: lo
     * unico que se hace con esos numeros antes de la suma es ACOTARLOS
     * contra `n`, y leer los 12 bytes de cada nivel, que ya se ha
     * comprobado que estan dentro. Nada se recorre, nada se copia y nada
     * se reserva con ellos hasta que la suma cuadra.
     */
    if (n < (uint32_t)MAPA_BIN_CAB
          + (uint32_t)MAPA_BIN_NIV_MAX * MAPA_BIN_NIV_CAB + 4UL) {
        return (s_estado = MAPA_BIN_VACIO);
    }
    {
        uint32_t fin = (uint32_t)MAPA_BIN_CAB
                     + (uint32_t)MAPA_BIN_NIV_MAX * MAPA_BIN_NIV_CAB;

        for (i = 0U; i < niveles; i++) {
            const uint8_t *t = &datos[MAPA_BIN_CAB
                                      + (uint32_t)i * MAPA_BIN_NIV_CAB];
            uint32_t off = le32(&t[4]);
            uint32_t len = le32(&t[8]);

            if (off > n || len > (n - off)) { return (s_estado = MAPA_BIN_VACIO); }
            if ((off + len) > fin) { fin = off + len; }
        }
        if (fin + 4UL > n) { return (s_estado = MAPA_BIN_VACIO); }
        n = fin + 4UL;   /* a partir de aqui, `n` es el fichero y nada mas */
        s_largo = n;
    }

    /*
     * LA SUMA, ANTES DE CREERSE UN SOLO NUMERO DE DENTRO. Si se mirara
     * despues, un fichero a medio copiar ya habria dicho que tiene
     * cuarenta mil trazos y se habria leido memoria que no es suya.
     */
    dicho = le32(&datos[n - 4UL]);
    calc  = suma(datos, n - 4UL, SUMA_INICIO);
    if (dicho != calc) { return (s_estado = MAPA_BIN_SUMA_MAL); }

    for (i = 0U; i < MAPA_BIN_COLORES; i++) {
        const uint8_t *c = &datos[12U + i * 3U];
        s_col[i] = ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | (uint32_t)c[2];
    }
    /*
     * EL ESTILO, QUE VIVE EN EL BYTE 57 - 14/10/2026. Ver mapa_bin.h.
     *
     * Un valor que no se conozca se trata como contorno y NO se rechaza el
     * fichero: un mapa que no se puede rellenar sigue siendo un mapa. Lo
     * contrario -negarse a cargarlo- dejaria al usuario sin mapa por un byte
     * que no le afecta a lo que ve.
     */
    s_estilo = datos[57U];
    if (s_estilo > MAPA_ESTILO_SILUETA) { s_estilo = MAPA_ESTILO_CONTORNO; }
    s_par_masc  = datos[58U];
    s_par_bytes = datos[59U];
    if (!contrasta(s_col[MAPA_COL_MARCA], s_col[MAPA_COL_MAR])) {
        return (s_estado = MAPA_BIN_SIN_CONTRASTE);
    }

    /*
     * SE GUARDA LA TABLA DE NIVELES Y SE CARGA EL x1, QUE ES EL QUE VIVE
     * ENTERO - 12/10/2026.
     *
     * El x1 no se trocea a proposito: al zoom 1 la ventana ES el mundo,
     * asi que las casillas serian puro gasto (medido: troceado da 1,83 px
     * de error donde el entero da 1,00). Los de arriba SI, y de ellos solo
     * se carga la ventana - ver mapa_bin_ventana()-.
     */
    for (i = 0U; i < niveles; i++) {
        const uint8_t *t = &datos[MAPA_BIN_CAB + i * MAPA_BIN_NIV_CAB];

        s_niv[i].zoom = le16(&t[0]);
        s_niv[i].nx   = t[2];
        s_niv[i].ny   = t[3];
        s_niv[i].off  = le32(&t[4]);
        s_niv[i].tam  = le32(&t[8]);
        s_niv[i].anillos = le16(&t[14]);
        if (s_niv[i].off > n || s_niv[i].tam > (n - s_niv[i].off)) {
            return (s_estado = MAPA_BIN_VACIO);
        }
        if (s_niv[i].zoom == 0U) { return (s_estado = MAPA_BIN_VACIO); }
        /* El mapa de bits vive DENTRO de `tam`: si dice medir mas que el
         * nivel entero, el fichero miente y no se carga. */
        if ((uint32_t)s_niv[i].anillos > s_niv[i].tam) {
            return (s_estado = MAPA_BIN_VACIO);
        }
    }
    s_niveles = (uint8_t)niveles;

    {
        const uint8_t *t = &datos[MAPA_BIN_CAB];

        if (t[2] != 0U) {
            /* El primero TIENE que ser el entero: es el que se carga al
             * arrancar y el unico que no necesita leer de la flash. */
            return (s_estado = MAPA_BIN_VACIO);
        }
        trazos_b = s_niv[0].tam;
        if (trazos_b == 0UL) { return (s_estado = MAPA_BIN_VACIO); }
        if (trazos_b > (uint32_t)MAPA_BIN_RAM) { return (s_estado = MAPA_BIN_GRANDE); }
        tr = &datos[s_niv[0].off];
        memcpy(s_buf, tr, trazos_b);
        s_n = trazos_b;
        s_anillos_n = s_niv[0].anillos;
        s_zoom_puesto = s_niv[0].zoom;
        s_idx_puesto = 0U;
    }

    /* Y contar los puntos recorriendolo, que es la unica forma de saber
     * que la cuenta de la cabecera no miente. */
    {
        mapa_bin_iter_t it;
        int32_t lon, lat;
        uint32_t p = 0UL, tz = 0UL;

        s_hay = 1U;
        mapa_bin_primero(&it);
        while (mapa_bin_trazo(&it)) {
            tz++;
            while (mapa_bin_punto(&it, &lon, &lat)) { p++; }
        }
        if (p == 0UL) { s_hay = 0U; return (s_estado = MAPA_BIN_VACIO); }
        s_puntos = (uint16_t)((p > 65535UL) ? 65535UL : p);
        s_trazos = (uint16_t)((tz > 65535UL) ? 65535UL : tz);
    }
    return (s_estado = MAPA_BIN_OK);
}

/* ------------------------------------------------------------------ */
/*
 * LA VENTANA. Ver el comentario gordo de mapa_bin.h.
 *
 * Tres cosas y en este orden, que importa:
 *
 *   1. elegir NIVEL por la escala que pide el dibujante;
 *   2. si ese nivel es el entero, no hay nada que leer;
 *   3. si es troceado, leer de la flash las casillas cuya CAJA toca lo
 *      que se ve, una detras de otra, hasta llenar el buffer.
 *
 * Y UNA GUARDIA QUE NO SE PUEDE QUITAR: si lo que toca la pantalla no
 * cabe en el buffer, se cargan las casillas que quepan y se deja de
 * cargar. Mejor medio mundo dibujado que pasarse del buffer, que es
 * escribir en memoria de otro.
 */
static void recuenta(void);

/*
 * ABRIR EL .map DESDE LA FLASH, SIN METERLO ENTERO EN LA RAM - 12/10/2026.
 *
 * EL v2 MIDE 70.665 BYTES y la radio tiene unos 45 kB prestados para
 * leer. O sea que mapa_bin_pon() -que pide el fichero entero en memoria-
 * ya no sirve para cargar de la flash: lo caza el banco diciendo "el
 * fichero no tiene trazos", que es lo que pasa cuando se le dan los
 * primeros 64 kB de un fichero de 70.
 *
 * Esto hace lo mismo en tres pasadas y sin buffer grande:
 *
 *   1. la suma, leyendo de la flash a trozos de 256 bytes;
 *   2. la cabecera y la tabla de niveles, que son 124 bytes;
 *   3. y el nivel x1 entero al buffer, que es el unico que vive en RAM.
 *
 * mapa_bin_pon() se queda para lo que de verdad es: comprobar un fichero
 * que YA esta entero en memoria -el que acaba de llegar del disco- y los
 * bancos.
 */
mapa_bin_r_t mapa_bin_abre_flash(mapa_bin_lee_t lee, uint32_t base, uint32_t n)
{
    uint8_t  cab[MAPA_BIN_CAB + MAPA_BIN_NIV_MAX * MAPA_BIN_NIV_CAB];
    uint32_t i, v = SUMA_INICIO, p = 0UL, dicho = 0UL;
    uint8_t  niveles;

    s_hay = 0U; s_n = 0UL; s_anillos_n = 0U;
    s_largo = 0UL; s_trazos = 0U; s_puntos = 0U;
    if (lee == 0 || n < (uint32_t)sizeof cab + 4UL) {
        return (s_estado = MAPA_BIN_VACIO);
    }

    /*
     * 1. LA CABECERA Y LA TABLA, SOLO PARA ACOTAR. Igual que en
     *    mapa_bin_pon(): mirar la tabla aqui no es creersela - lo unico
     *    que se hace con esos numeros antes de la suma es ACOTARLOS
     *    contra `n` y sacar donde acaba el fichero-. Hace falta porque
     *    `n` viene del directorio del disco y puede traer relleno
     *    detras, y la suma esta al final del FICHERO, no del relleno.
     */
    lee(base, cab, (uint32_t)sizeof cab);
    if (memcmp(cab, MAPA_BIN_MARCA, 8U) != 0) { return (s_estado = MAPA_BIN_MARCA_MAL); }
    if (cab[8] != (uint8_t)MAPA_BIN_VERSION)  { return (s_estado = MAPA_BIN_VERSION_MAL); }
    niveles = cab[9];
    if (niveles == 0U || niveles > MAPA_BIN_NIV_MAX) { return (s_estado = MAPA_BIN_VACIO); }
    {
        uint32_t fin = (uint32_t)sizeof cab;

        for (i = 0U; i < niveles; i++) {
            const uint8_t *t = &cab[MAPA_BIN_CAB + i * MAPA_BIN_NIV_CAB];
            uint32_t off = le32(&t[4]);
            uint32_t tam = le32(&t[8]);

            if (off > n || tam > (n - off)) { return (s_estado = MAPA_BIN_VACIO); }
            if ((off + tam) > fin) { fin = off + tam; }
        }
        if (fin + 4UL > n) { return (s_estado = MAPA_BIN_VACIO); }
        n = fin + 4UL;
    }

    /* 2. LA SUMA, antes de usar un solo numero de los de arriba. */
    while (p + 4UL < n) {
        uint8_t  t[256];
        uint32_t k = (n - 4UL) - p;

        if (k > (uint32_t)sizeof t) { k = (uint32_t)sizeof t; }
        lee(base + p, t, k);
        v = suma(t, k, v);
        p += k;
    }
    {
        uint8_t t[4];

        lee(base + n - 4UL, t, 4U);
        dicho = le32(t);
    }
    if (dicho != v) { return (s_estado = MAPA_BIN_SUMA_MAL); }

    /* 3. los colores y la tabla, ya con la suma buena */
    for (i = 0U; i < MAPA_BIN_COLORES; i++) {
        const uint8_t *c = &cab[12U + i * 3U];
        s_col[i] = ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | (uint32_t)c[2];
    }
    /*
     * Y EL ESTILO, QUE ESTE CAMINO NO LEIA - 14/10/2026.
     *
     * Hay DOS puertas para cargar un mapa: mapa_bin_pon(), con el fichero
     * entero en RAM, y esta, que lo lee a trozos de la flash interna. La
     * radio entra SIEMPRE por esta -el .map se graba en la flash al
     * arrancar- y esta era la unica que no leia el byte 57. s_estilo se
     * quedaba a cero, o sea CONTORNO, y la radio no rellenaba jamas.
     *
     * Y NINGUN BANCO LO VIO porque los bancos del relleno entran todos por
     * mapa_bin_pon(), que si lo leia. Cien mapas en verde midiendo la
     * puerta que no es. Lo vigila ahora sim/mapa_dospuertas.c, que carga el
     * MISMO fichero por las dos y exige que pinten el mismo pixel.
     */
    s_estilo = cab[57U];
    if (s_estilo > MAPA_ESTILO_SILUETA) { s_estilo = MAPA_ESTILO_CONTORNO; }
    s_par_masc  = cab[58U];
    s_par_bytes = cab[59U];

    if (!contrasta(s_col[MAPA_COL_MARCA], s_col[MAPA_COL_MAR])) {
        return (s_estado = MAPA_BIN_SIN_CONTRASTE);
    }

    for (i = 0U; i < niveles; i++) {
        const uint8_t *t = &cab[MAPA_BIN_CAB + i * MAPA_BIN_NIV_CAB];

        s_niv[i].zoom = le16(&t[0]);
        s_niv[i].nx   = t[2];
        s_niv[i].ny   = t[3];
        s_niv[i].off  = le32(&t[4]);
        s_niv[i].tam  = le32(&t[8]);
        s_niv[i].anillos = le16(&t[14]);
        if (s_niv[i].zoom == 0U) { return (s_estado = MAPA_BIN_VACIO); }
        if (s_niv[i].off > n || s_niv[i].tam > (n - s_niv[i].off)) {
            return (s_estado = MAPA_BIN_VACIO);
        }
        if ((uint32_t)s_niv[i].anillos > s_niv[i].tam) {
            return (s_estado = MAPA_BIN_VACIO);
        }
    }
    s_niveles = niveles;
    if (s_niv[0].nx != 0U)  { return (s_estado = MAPA_BIN_VACIO); }
    if (s_niv[0].tam == 0UL) { return (s_estado = MAPA_BIN_VACIO); }
    if (s_niv[0].tam > (uint32_t)MAPA_BIN_RAM) { return (s_estado = MAPA_BIN_GRANDE); }

    /* 3. y el x1 al buffer */
    lee(base + s_niv[0].off, s_buf, s_niv[0].tam);
    s_n = s_niv[0].tam;
    s_anillos_n = s_niv[0].anillos;
    s_zoom_puesto = s_niv[0].zoom;
    s_idx_puesto = 0U;
    s_vent_vale = 0U;
    s_largo = n;
    s_lee = lee; s_base = base;

    s_hay = 1U;
    recuenta();
    if (s_puntos == 0U) { s_hay = 0U; return (s_estado = MAPA_BIN_VACIO); }
    return (s_estado = MAPA_BIN_OK);
}

/* Recorre lo que hay cargado y pone al dia trazos y puntos. */
static void recuenta(void)
{
    mapa_bin_iter_t it;
    int32_t lo, la;
    uint32_t pp = 0UL, tz = 0UL;

    s_relleno_n = 0U;
    mapa_bin_primero(&it);
    while (mapa_bin_trazo(&it)) {
        tz++;
        if (it.relleno && s_relleno_n < 0xFFFFU) { s_relleno_n++; }
        while (mapa_bin_punto(&it, &lo, &la)) { pp++; }
    }
    s_puntos = (uint16_t)((pp > 65535UL) ? 65535UL : pp);
    s_trazos = (uint16_t)((tz > 65535UL) ? 65535UL : tz);
}

void mapa_bin_fuente(mapa_bin_lee_t lee, uint32_t base)
{
    s_lee  = lee;
    s_base = base;
}

uint8_t  mapa_bin_nivel(void)    { return (uint8_t)s_zoom_puesto; }
uint16_t mapa_bin_recargas(void) { return s_recargas; }

/* De centesimas de grado a 1/32, que es como viene el fichero. */
static int32_t a32(int32_t cent)
{
    /* cent/100 grados * 32 = cent * 8 / 25, sin coma y sin perder el signo */
    return (cent * 8L) / 25L;
}

/* Cual de los niveles le viene mejor a esta escala: el de zoom mas alto
 * que no pase de `esc`. Con esc=2 y niveles 1,4,8 sale el 1, que es lo
 * que se queria: el zoom 2 se dibuja estirando el x1. */
static uint8_t elige(uint8_t esc)
{
    uint8_t i, mejor = 0U;

    for (i = 1U; i < s_niveles; i++) {
        if (s_niv[i].zoom <= (uint16_t)esc) { mejor = i; }
    }
    return mejor;
}

uint8_t mapa_bin_ventana(uint8_t esc, int32_t lon0, int32_t lat0,
                         int32_t lon1, int32_t lat1)
{
    uint8_t  idx;
    int16_t  x0, y0, x1, y1;

    if (!s_hay) { return 0U; }

    idx = elige(esc);
    x0 = (int16_t)a32(lon0); y0 = (int16_t)a32(lat0);
    x1 = (int16_t)a32(lon1); y1 = (int16_t)a32(lat1);

    /* EL NIVEL ENTERO NO TIENE VENTANA: esta todo cargado siempre. */
    if (s_niv[idx].nx == 0U) {
        /* Ya esta puesto y entero: no hay nada que hacer. `s_vent_vale`
         * distingue "el buffer tiene el nivel entero" de "tiene una
         * ventana de otro nivel", que es justo el caso que hay que
         * recargar al volver del zoom 4 al 1. */
        if (s_zoom_puesto == s_niv[idx].zoom && !s_vent_vale) { return 0U; }
        if (s_lee == 0) { return 0U; }
        if (s_niv[idx].tam > (uint32_t)MAPA_BIN_RAM) { return 0U; }
        s_lee(s_base + s_niv[idx].off, s_buf, s_niv[idx].tam);
        s_n = s_niv[idx].tam;
        s_anillos_n = s_niv[idx].anillos;
        s_zoom_puesto = s_niv[idx].zoom;
        s_idx_puesto = idx;
        s_vent_vale = 0U;
        s_recargas++;
        /*
         * Y RECONTAR TAMBIEN AQUI. La primera version solo recontaba en
         * el camino troceado, asi que al volver del zoom 8 al 1 la fila
         * de Informacion seguia enseñando los trazos de la VENTANA
         * anterior -373 en vez de 376- con los puntos ya buenos. Lo cazo
         * el banco: una cuenta que no se actualiza al cambiar lo que
         * cuenta no esta contando, esta recordando.
         */
        recuenta();
        return 1U;
    }

    /*
     * Y SI YA ESTA CARGADO LO QUE SE PIDE, no se toca nada. Esta es la
     * guardia que hace que arrastrar el mapa no sea una recarga por
     * fotograma: mientras la pantalla siga dentro de la ventana que se
     * leyo, no hay nada que leer.
     */
    if (s_zoom_puesto == s_niv[idx].zoom && s_vent_vale
     && x0 >= s_vx0 && x1 <= s_vx1 && y0 >= s_vy0 && y1 <= s_vy1) {
        return 0U;
    }
    if (s_lee == 0) { return 0U; }

    /*
     * EL MARGEN. Se carga mas de lo que se ve -media pantalla por cada
     * lado- para que un arrastre corto no dispare una recarga. Sin esto,
     * mover el dedo un pixel fuera del borde releeria la flash.
     */
    {
        int32_t mx = ((int32_t)x1 - (int32_t)x0) / 2L;
        int32_t my = ((int32_t)y1 - (int32_t)y0) / 2L;
        int32_t vx0 = (int32_t)x0 - mx, vx1 = (int32_t)x1 + mx;
        int32_t vy0 = (int32_t)y0 - my, vy1 = (int32_t)y1 + my;
        uint8_t nx = s_niv[idx].nx, ny = s_niv[idx].ny;
        uint32_t base = s_base + s_niv[idx].off;
        uint32_t puesto = 0UL;
        uint16_t c, total = (uint16_t)nx * (uint16_t)ny;
        uint8_t  pas;

        /*
         * LO QUE SE VE PRIMERO, Y EL MARGEN DESPUES - 16/10/2026.
         * ESTE ERA EL FALLO DE LAS RAYAS AL ACERCARSE.
         *
         * Aqui habia UN SOLO bucle, en el orden del fichero -de la
         * casilla de arriba a la izquierda del MUNDO a la de abajo a la
         * derecha-, y cuando el buffer se llenaba hacia `break` y a otra
         * cosa. Sin contador, sin aviso, sin nada.
         *
         * O sea que el margen de media pantalla -que no se ve- se cargaba
         * ANTES que casillas que si se ven, y cuando no cabia todo, lo
         * que se quedaba fuera era el final: la parte de abajo a la
         * derecha DE LA PANTALLA.
         *
         * Y faltar media casilla de costa no es que falte un trozo de
         * raya: el relleno va por PAR-IMPAR, asi que una costa que no
         * esta descuadra la cuenta de cruces y lo que viene detras sale
         * del color contrario hasta el siguiente borde de casilla. Esa es
         * la raya horizontal de la foto. Medido en el mapa 036 centrado
         * en Europa, de los dos niveles troceados UNO NO CUPO.
         *
         * Asi que dos pasadas: primero las casillas que tocan lo que se
         * ve, y con lo que sobre, el margen. Y lo que no quepa SE CUENTA
         * -s_vis_fuera-, que es lo que faltaba para que esto se pudiera
         * ver sin una foto.
         */
        s_vis_fuera = 0U;
        for (pas = 0U; pas < 2U; pas++) {
            for (c = 0U; c < total; c++) {
                uint8_t  e[MAPA_BIN_CAS_CAB];
                uint32_t off, tam;
                int16_t  cx0, cy0, cx1, cy1;
                uint8_t  vis;

                s_lee(base + (uint32_t)c * MAPA_BIN_CAS_CAB, e, MAPA_BIN_CAS_CAB);
                off = le32(&e[0]);
                tam = le32(&e[4]);
                cx0 = (int16_t)le16(&e[8]);  cy0 = (int16_t)le16(&e[10]);
                cx1 = (int16_t)le16(&e[12]); cy1 = (int16_t)le16(&e[14]);

                if (tam == 0UL || off == 0UL) { continue; }
                if (cx1 < cx0 || cy1 < cy0)   { continue; }   /* casilla vacia */
                if ((int32_t)cx0 > vx1 || (int32_t)cx1 < vx0) { continue; }
                if ((int32_t)cy0 > vy1 || (int32_t)cy1 < vy0) { continue; }
                if (off > s_niv[idx].tam || tam > (s_niv[idx].tam - off)) { continue; }

                /* Visible = toca el rectangulo SIN margen. */
                vis = (uint8_t)(!((int32_t)cx0 > (int32_t)x1
                                || (int32_t)cx1 < (int32_t)x0
                                || (int32_t)cy0 > (int32_t)y1
                                || (int32_t)cy1 < (int32_t)y0));
                if (pas == 0U) { if (!vis) { continue; } }
                else           { if (vis)  { continue; } }

                /*
                 * El u16 de cierre de cada cuerpo se tira: si se dejara, el
                 * recorrido pararia en la primera casilla. Se reconstruye uno
                 * solo al final.
                 */
                if (tam < 2UL) { continue; }
                if (puesto + (tam - 2UL) + 2UL > (uint32_t)MAPA_BIN_RAM) {
                    /*
                     * NO SE CORTA EL BUCLE: se sigue probando. Las
                     * casillas no miden todas lo mismo, y detras de una
                     * que no cabe puede venir otra que si. Cortar aqui
                     * dejaba fuera casillas que habrian entrado.
                     */
                    if (vis) {
                        s_vis_fuera++;
                        if (s_vis_fuera > s_vis_fuera_max) {
                            s_vis_fuera_max = s_vis_fuera;
                        }
                    }
                    continue;
                }
                s_lee(base + off, &s_buf[puesto], tam - 2UL);
                puesto += (tam - 2UL);
            }
        }
        s_buf[puesto] = 0U; s_buf[puesto + 1U] = 0U;
        s_n = puesto + 2UL;
        /* Una ventana se cose a mano con trozos de varias casillas: los
         * indices de trazo no son los del fichero, asi que AQUI NO HAY
         * mapa de bits. Si se dejara el del nivel anterior, los anillos
         * saldrian asignados a trazos que no lo son. */
        s_anillos_n = 0U;

        s_vx0 = (int16_t)vx0; s_vy0 = (int16_t)vy0;
        s_vx1 = (int16_t)vx1; s_vy1 = (int16_t)vy1;
        s_vent_vale = 1U;
        s_zoom_puesto = s_niv[idx].zoom;
        s_idx_puesto = idx;
        s_recargas++;
    }

    recuenta();
    return 1U;
}

uint8_t mapa_bin_fuera(void)     { return s_vis_fuera; }
uint8_t mapa_bin_fuera_max(void) { return s_vis_fuera_max; }

mapa_bin_r_t mapa_bin_estado(void) { return s_estado; }
uint32_t mapa_bin_largo(void) { return s_largo; }

void mapa_bin_quita(void)
{
    s_hay    = 0U;
    s_n      = 0UL;
    s_anillos_n = 0U;
    /*
     * Y EL ESTILO TAMBIEN - 14/10/2026.
     *
     * No borrarlo dejaba el del mapa ANTERIOR puesto, y eso hizo
     * invisible un fallo de verdad: el banco de las dos puertas cargaba
     * primero por RAM -que si leia el estilo- y despues por flash -que no
     * lo leia-, y la segunda heredaba el 1 de la primera. Banco verde,
     * radio sin rellenar.
     *
     * mapa_bin_estilo() ya lo tapa con s_hay, asi que esto no se veia en
     * la radio. Da igual: un estado que sobrevive a "quita" es una mina
     * esperando a que alguien mire sin preguntar primero.
     */
    s_estilo = (uint8_t)MAPA_ESTILO_CONTORNO;
    s_largo  = 0UL;
    s_trazos = 0U;
    s_puntos = 0U;
    s_estado = MAPA_BIN_NADA;
}

uint8_t      mapa_bin_hay(void)    { return s_hay; }
uint16_t     mapa_bin_trazos(void) { return s_trazos; }
uint16_t     mapa_bin_puntos(void) { return s_puntos; }

uint8_t mapa_bin_estilo(void)
{
    return s_hay ? s_estilo : (uint8_t)MAPA_ESTILO_CONTORNO;
}

uint32_t mapa_bin_color(uint8_t cual, uint32_t porsi)
{
    if (!s_hay || cual >= (uint8_t)MAPA_BIN_COLORES) { return porsi; }
    return s_col[cual];
}

const char *mapa_bin_porque_txt(mapa_bin_r_t r)
{
    switch (r) {
    case MAPA_BIN_NADA:           return tr("el de serie", "the built-in one");
    case MAPA_BIN_OK:             return tr("del fichero", "from the file");
    case MAPA_BIN_MARCA_MAL:      return tr("no es un .map", "not a .map");
    case MAPA_BIN_VERSION_MAL:    return tr("version que no conozco", "unknown version");
    case MAPA_BIN_SUMA_MAL:       return tr("la suma no cuadra", "checksum mismatch");
    case MAPA_BIN_GRANDE:         return tr("no cabe en la RAM", "too big for RAM");
    case MAPA_BIN_VACIO:          return tr("el fichero no tiene trazos", "no outlines in it");
    case MAPA_BIN_SIN_CONTRASTE:  return tr("el punto no se veria", "markers would not show");
    default:                      return "?";
    }
}

/* ---------------------------------------------------------- recorrido */
void mapa_bin_primero(mapa_bin_iter_t *it)
{
    if (it == 0) { return; }
    it->p = s_buf;
    /*
     * EL FINAL ES ANTES DEL MAPA DE BITS DE LOS ANILLOS, que va pegado al
     * final del nivel. El cero que cierra la tabla ya para al que recorre,
     * asi que esto es un cinturon encima de los tirantes: si algun dia un
     * fichero llega sin ese cero, el recorrido se queda en los trazos y no
     * se come el mapa de bits como si fueran puntos.
     */
    it->fin = s_buf + s_n - (uint32_t)s_anillos_n;
    it->trazos_quedan = 0U;
    it->puntos_quedan = 0U;
    it->x = 0; it->y = 0;
    it->primero = 0U;
    it->indice = 0U;
    it->anillo = 0U;
    it->frontera = 0U;
    it->relleno = 0U;
}

uint8_t mapa_bin_trazo(mapa_bin_iter_t *it)
{
    uint16_t n;

    if (it == 0 || !s_hay) { return 0U; }
    /* Lo que quede del trazo anterior se tira: quien no agota los puntos
     * no debe arrastrar su basura al siguiente. */
    while (it->puntos_quedan > 0U) {
        int32_t a, b;
        if (!mapa_bin_punto(it, &a, &b)) { break; }
    }
    if ((uint32_t)(it->fin - it->p) < 2U) { return 0U; }
    n = le16(it->p);
    it->p += 2;
    if (n == 0U) { return 0U; }          /* el cero cierra la tabla */
    /*
     * EL BIT ALTO DICE SI ES UNA FRONTERA - 12/10/2026.
     *
     * Porque el dibujante no las distinguia: todos los trazos salian con
     * el color de la costa. El .map traia un color propio para las
     * fronteras desde el principio -y los cien tipos lo tienen puesto- y
     * no lo miraba nadie.
     *
     * Va en el bit 15 de la cuenta de puntos porque ahi no estorba: el
     * trazo mas largo del mundo tiene 5.914 puntos. Cuesta cero bytes.
     */
    it->frontera = (uint8_t)((n & 0x8000U) ? 1U : 0U);
    /*
     * Y EL BIT 14, LOS ANILLOS DE RELLENO DE LA CASILLA - 16/10/2026.
     * Mismo razonamiento que el de arriba: el trazo mas largo del mundo
     * tiene 5.914 puntos, asi que de 16.384 para arriba no hay nada y el
     * bit esta libre. Un .map viejo lo trae a cero, que es "no hay", y el
     * dibujante se comporta como antes.
     */
    it->relleno = (uint8_t)((n & 0x4000U) ? 1U : 0U);
    n &= 0x3FFFU;
    /*
     * Y LOS ANILLOS DE TIERRA VIENEN DE FUERA DE LA CUENTA - 14/10/2026.
     *
     * Un anillo es un trazo CERRADO cuyo interior es tierra. Quien lo
     * decide es tools/mapa_pack.py, no la radio: un Cortex-M4 no tiene que
     * averiguar topologia cincuenta veces por segundo.
     *
     * LA PRIMERA VERSION LO PUSO EN EL BIT 14 DE ESTA CUENTA, y estaba
     * mal. El razonamiento era "un .map viejo no trae el bit, luego no hay
     * que subir la version del formato", y es cierto en esa direccion;
     * pero en la otra -un .map NUEVO en un firmware VIEJO- el lector de
     * entonces enmascaraba 0x7FFF, el bit 14 se le colaba dentro de la
     * cuenta de puntos y el primer anillo se tragaba la tabla entera. Son
     * 51 de los cien mapas, medidos, saliendo como UN trazo de 4.309
     * puntos en vez de 370 trazos. Sin reventar y sin avisar.
     *
     * Asi que la cuenta vuelve a ser la de siempre y los anillos van en un
     * MAPA DE BITS -un bit por trazo, en orden de fichero- pegado detras
     * del cero que cierra la tabla, dentro del `tam` del nivel. Un lector
     * viejo para en el cero y no lo ve jamas.
     */
    if (s_anillos_n != 0U) {
        uint16_t i = it->indice;
        uint32_t byte = (uint32_t)(i >> 3);
        const uint8_t *bits = s_buf + s_n - (uint32_t)s_anillos_n;
        it->anillo = (byte < (uint32_t)s_anillos_n)
                   ? (uint8_t)((bits[byte] >> (i & 7U)) & 1U)
                   : 0U;
    } else {
        it->anillo = 0U;
    }
    if (it->indice != 0xFFFFU) { it->indice++; }
    it->puntos_quedan = n;
    it->primero = 1U;
    return 1U;
}

uint8_t mapa_bin_punto(mapa_bin_iter_t *it, int32_t *lon_c, int32_t *lat_c)
{
    if (it == 0 || it->puntos_quedan == 0U) { return 0U; }

    if (it->primero) {
        if ((uint32_t)(it->fin - it->p) < 4U) { it->puntos_quedan = 0U; return 0U; }
        it->x = (int32_t)(int16_t)le16(it->p);
        it->y = (int32_t)(int16_t)le16(it->p + 2);
        it->p += 4;
        it->primero = 0U;
    } else {
        if ((uint32_t)(it->fin - it->p) < 1U) { it->puntos_quedan = 0U; return 0U; }
        if (it->p[0] == 0x80U) {
            /* el que no cabia en un byte */
            if ((uint32_t)(it->fin - it->p) < 5U) { it->puntos_quedan = 0U; return 0U; }
            it->x += (int32_t)(int16_t)le16(it->p + 1);
            it->y += (int32_t)(int16_t)le16(it->p + 3);
            it->p += 5;
        } else {
            if ((uint32_t)(it->fin - it->p) < 2U) { it->puntos_quedan = 0U; return 0U; }
            it->x += (int32_t)(int8_t)it->p[0];
            it->y += (int32_t)(int8_t)it->p[1];
            it->p += 2;
        }
    }
    it->puntos_quedan--;

    /*
     * De 1/32 de grado a centesimas, que es lo que habla el resto del
     * mapa. 100/32 es 25/8, y asi la conversion son un producto y un
     * desplazamiento: ni division ni coma flotante en un camino que se
     * recorre miles de veces por fotograma.
     */
    if (lon_c) { *lon_c = (it->x * 25L) / 8L; }
    if (lat_c) { *lat_c = (it->y * 25L) / 8L; }
    return 1U;
}

/* ------------------------------------------------ LA PARIDAD POR CASILLA
 *
 * PARA QUE LA TIERRA SE RELLENE TAMBIEN AL ACERCARSE - 15/10/2026.
 *
 * El relleno es par-impar sobre los cruces de cada fila. Al zoom 1 basta
 * porque la ventana ES el mundo y toda fila empieza en el borde, que es
 * mar. Al acercarse no: una ventana metida dentro de un continente no
 * tiene ninguna costa que cruzar y saldria toda de mar.
 *
 * Lo que falta no es el relleno: es LA PARIDAD DE PARTIDA. Un bit por
 * fila y casilla que diga si el borde IZQUIERDO de esa casilla cae en
 * tierra o en agua. Con eso, el par-impar de siempre hace el resto.
 *
 * VA UN BIT EN CADA CASILLA Y NO UNO POR VENTANA, y esa es la decision
 * que lo hace defendible: sembrar solo en el borde de lo que se ve deja
 * que un error se propague hasta la otra punta de la pantalla -basta una
 * isla que la simplificacion se haya comido para invertir todo lo que
 * tenga a su derecha-. Sembrando en cada casilla, el dibujante vuelve a
 * sincronizar la paridad en cada borde y el daño de una isla perdida no
 * sale de su casilla.
 *
 * NO SE CARGA CON LA VENTANA. Son 45 bytes de la casilla que toque, y se
 * leen de la flash cuando se van a usar: meterlos en el buffer obligaria
 * a quitarle sitio a los trazos, que es lo que se ve.
 */
uint8_t mapa_bin_par_hay(void)
{
    if (!s_hay || s_idx_puesto >= s_niveles) { return 0U; }
    if (s_par_bytes == 0U) { return 0U; }
    if (s_niv[s_idx_puesto].nx == 0U) { return 0U; }
    return ((s_par_masc >> s_idx_puesto) & 1U);
}

uint8_t mapa_bin_par_bytes(void) { return s_par_bytes; }

/*
 * CUANTOS ANILLOS DE RELLENO TRAE LO QUE ESTA CARGADO - 16/10/2026.
 *
 * Cero quiere decir dos cosas distintas y las dos llevan al mismo sitio:
 * o es el nivel entero -que rellena con sus propios anillos cerrados-, o
 * es un .map de antes de hoy, que no los trae. En los dos casos el
 * dibujante tira por el camino de siempre.
 */
uint16_t mapa_bin_relleno_n(void) { return s_hay ? s_relleno_n : 0U; }
uint8_t mapa_bin_nx(void)
{ return (s_idx_puesto < s_niveles) ? s_niv[s_idx_puesto].nx : 0U; }
uint8_t mapa_bin_ny(void)
{ return (s_idx_puesto < s_niveles) ? s_niv[s_idx_puesto].ny : 0U; }

/*
 * SE LEE UN TROZO Y NO LA CASILLA ENTERA, y eso no es tacañeria.
 *
 * El dibujante va por BANDAS de dieciseis filas, asi que de los 360 bits
 * de una casilla le hacen falta dieciseis seguidos: tres bytes, no
 * cuarenta y cinco. Leer la casilla entera obligaria a tener 45 bytes en
 * algun sitio, y en esta radio quedan 72 libres en toda la SRAM.
 */
uint8_t mapa_bin_par_lee(uint16_t cas, uint8_t desde, uint8_t *buf, uint8_t n)
{
    const nivel_t *v;
    uint32_t total, ini;

    if (!mapa_bin_par_hay() || buf == 0 || s_lee == 0 || n == 0U) { return 0U; }
    if ((uint16_t)desde + (uint16_t)n > (uint16_t)s_par_bytes) { return 0U; }
    v = &s_niv[s_idx_puesto];
    total = (uint32_t)v->nx * (uint32_t)v->ny * (uint32_t)s_par_bytes;
    if (cas >= (uint16_t)((uint16_t)v->nx * (uint16_t)v->ny)) { return 0U; }
    /* El bloque esta pegado al FINAL del nivel. Si el fichero dice que el
     * nivel mide menos que su propio bloque, es que miente: no se lee. */
    if (total > v->tam) { return 0U; }
    ini = v->off + v->tam - total
        + (uint32_t)cas * (uint32_t)s_par_bytes + (uint32_t)desde;
    s_lee(s_base + ini, buf, (uint32_t)n);
    return 1U;
}
