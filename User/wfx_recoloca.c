/*
 * El giro de una carta ya grabada. El porque, entero, en wfx_recoloca.h.
 */

#include "wfx_recoloca.h"
#include "spi_flash.h"
#include "wefax.h"
#include "idioma.h"

/* El tamaño del bloque sale de la cabecera, que es tambien donde lo mira
 * main.c para comprobar que el buffer que presta da. Ver WFX_REC_BLOQUE. */
#define BLOQUE  WFX_REC_BLOQUE

/* El nombre del fichero NO se guarda: todo lo que hace falta despues de
 * abrir el recorrido esta en s_it, y guardarlo hacia creer que este modulo
 * se queda con la identidad del fichero, que es justo lo que no hace. */
static uint8_t        *s_buf;                   /* prestado, 4096 bytes */
static uint8_t         s_fila[WEFAX_ANCHO];     /* el renglon de turno */
static uint8_t         s_carro[WEFAX_ANCHO];    /* el que cruza la frontera, YA girado */
static uint8_t         s_carro_hay;

static uint32_t        s_ancho;
static uint32_t        s_alto;
static uint32_t        s_corte;
static uint32_t        s_nb;                    /* bloques del fichero */
static uint32_t        s_b;                     /* por cual va (1 = el primero de imagen) */

static spi_bloques_t   s_it;
static uint32_t        s_a_act;                 /* direccion del bloque de turno */
static uint32_t        s_a_sig;                 /* y la del siguiente */
static uint8_t         s_sig_hay;

static wfx_rec_estado_t s_estado;
static const char      *s_porque;

/*
 * El giro de un renglon. dst[i] = src[(corte + i) % ancho].
 *
 * Leido en la pantalla: la columna `corte` pasa a ser la primera. Lo que
 * habia a su derecha se pega al borde izquierdo y lo que habia a su
 * izquierda sale por el derecho. Es un giro: no se pierde ni un punto.
 */
static void gira(uint8_t *dst, const uint8_t *src)
{
    uint32_t i, j = s_corte;

    for (i = 0UL; i < s_ancho; i++) {
        dst[i] = src[j];
        j++;
        if (j >= s_ancho) { j = 0UL; }
    }
}

static void falla(const char *porque)
{
    s_estado = WFX_REC_MAL;
    s_porque = porque;
}

uint8_t wfx_recoloca_empieza(const char name8[8], const char ext3[3],
                             uint16_t ancho, uint16_t alto, uint16_t corte,
                             uint8_t *buf4k)
{
    uint32_t nb;

    wfx_recoloca_olvida();

    if (!buf4k || ancho == 0U || alto == 0U) { return 0U; }
    if ((uint32_t)ancho > (uint32_t)WEFAX_ANCHO) { return 0U; }
    if (corte == 0U || (uint32_t)corte >= (uint32_t)ancho) { return 0U; }

    nb = spi_flash_bloques_n(name8, ext3);
    if (nb < 2UL) { return 0U; }          /* cabecera y nada mas */

    /*
     * Que el fichero tenga de verdad los renglones que se dicen. Si no los
     * tiene, el que llama se ha equivocado de fichero o de altura, y girar
     * 4 kB de lo que sea encima de un fichero es como empiezan las averias
     * que luego hay que contar.
     */
    if ((1UL + (((uint32_t)alto * (uint32_t)ancho) + BLOQUE - 1UL) / BLOQUE) > nb) {
        return 0U;
    }

    if (!spi_flash_bloques_abre(name8, ext3, &s_it)) { return 0U; }

    /* El bloque 0 es la cabecera del BMP: no se gira, solo se salta. */
    if (!spi_flash_bloques_sig(&s_it, &s_a_act)) { return 0U; }
    if (!spi_flash_bloques_sig(&s_it, &s_a_act)) { return 0U; }
    s_sig_hay = spi_flash_bloques_sig(&s_it, &s_a_sig);

    s_buf       = buf4k;
    s_ancho     = (uint32_t)ancho;
    s_alto      = (uint32_t)alto;
    s_corte     = (uint32_t)corte;
    s_nb        = nb;
    s_b         = 1UL;
    s_carro_hay = 0U;
    s_estado    = WFX_REC_ANDA;
    s_porque    = 0;
    return 1U;
}

wfx_rec_estado_t wfx_recoloca_paso(void)
{
    uint32_t p0, k, r, nf, pix;

    if (s_estado != WFX_REC_ANDA) { return s_estado; }

    nf  = s_ancho;
    pix = s_alto * nf;                      /* bytes de imagen de verdad */
    p0  = (s_b - 1UL) * BLOQUE;             /* primer byte de imagen de este bloque */

    if (p0 >= pix) {                        /* ya no queda imagen que girar */
        s_estado = WFX_REC_BIEN;
        return s_estado;
    }

    spi_flash_read(s_a_act, s_buf, BLOQUE);

    /*
     * 1. La cola del renglon que venia del bloque anterior. Su mitad
     *    izquierda ya se escribio girada; aqui va el resto, que esta
     *    guardado en s_carro desde entonces.
     */
    k = 0UL;
    r = p0 / nf;
    if ((p0 % nf) != 0UL) {
        uint32_t dentro = p0 % nf;          /* lo suyo que iba en el anterior */
        uint32_t queda  = nf - dentro;      /* lo suyo que cae aqui */

        /*
         * Y SI NO HAY CARRO, ESTO NO PUEDE SEGUIR - 21/10/2026.
         *
         * Que este bloque empiece a mitad de renglon y no haya carro
         * significa que el paso anterior no lo guardo, o sea que el
         * recorrido se ha descolocado. Antes se seguia adelante dejando
         * esos bytes sin girar y en silencio: medio renglon crudo en medio
         * de la carta, que es justo la costura que esto viene a quitar.
         *
         * El `if (queda > BLOQUE)` que habia aqui era codigo muerto: un
         * renglon mide 756 y un bloque 4096.
         */
        if (r < s_alto) {
            uint32_t i;
            if (!s_carro_hay) {
                falla(tr("el giro se ha descolocado",
                         "the rotation lost its place"));
                return s_estado;
            }
            for (i = 0UL; i < queda; i++) { s_buf[i] = s_carro[dentro + i]; }
        }
        k = queda;
        r++;
    }
    s_carro_hay = 0U;

    /* 2. Los renglones enteros que caben en este bloque. */
    while ((k + nf) <= BLOQUE) {
        if (r >= s_alto) { break; }         /* lo de detras del ultimo no es imagen */
        gira(s_fila, &s_buf[k]);
        {
            uint32_t i;
            for (i = 0UL; i < nf; i++) { s_buf[k + i] = s_fila[i]; }
        }
        k += nf;
        r++;
    }

    /*
     * 3. Y el que se queda a caballo. Su cabeza esta aqui y su cola en el
     *    bloque siguiente, que todavia no se ha tocado: se lee del chip,
     *    se gira el renglon ENTERO, la cabeza se deja aqui y el resultado
     *    se guarda para el paso siguiente.
     */
    if (k < BLOQUE && r < s_alto) {
        uint32_t cabeza = BLOQUE - k;
        uint32_t cola   = nf - cabeza;
        uint32_t i;

        if (!s_sig_hay) { falla(tr("el fichero se acaba antes de tiempo",
                                   "the file ends too soon")); return s_estado; }
        for (i = 0UL; i < cabeza; i++) { s_fila[i] = s_buf[k + i]; }
        spi_flash_read(s_a_sig, &s_fila[cabeza], cola);
        gira(s_carro, s_fila);
        s_carro_hay = 1U;
        for (i = 0UL; i < cabeza; i++) { s_buf[k + i] = s_carro[i]; }
    }

    /* 4. Y al chip. */
    spi_flash_write_block_4k(s_a_act, s_buf);

    /* 5. Un bloque mas. */
    s_b++;
    if (s_b >= s_nb || ((s_b - 1UL) * BLOQUE) >= pix) {
        s_estado = WFX_REC_BIEN;
        return s_estado;
    }
    if (!s_sig_hay) { falla(tr("la cadena del fichero se rompe",
                               "the file's chain is broken")); return s_estado; }
    s_a_act   = s_a_sig;
    s_sig_hay = spi_flash_bloques_sig(&s_it, &s_a_sig);
    return s_estado;
}

uint8_t wfx_recoloca_pct(void)
{
    uint32_t hechos, total;

    if (s_estado == WFX_REC_PARADO) { return 0U; }
    if (s_estado != WFX_REC_ANDA)   { return 100U; }
    total = (s_alto * s_ancho + BLOQUE - 1UL) / BLOQUE;
    if (total == 0UL) { return 100U; }
    hechos = s_b - 1UL;
    if (hechos > total) { hechos = total; }
    return (uint8_t)((hechos * 100UL) / total);
}

wfx_rec_estado_t wfx_recoloca_estado(void) { return s_estado; }
const char      *wfx_recoloca_porque(void) { return (s_estado == WFX_REC_MAL) ? s_porque : 0; }

void wfx_recoloca_olvida(void)
{
    s_estado    = WFX_REC_PARADO;
    s_porque    = 0;
    s_buf       = 0;
    s_carro_hay = 0U;
    s_sig_hay   = 0U;
    s_b         = 0UL;
    s_nb        = 0UL;
    s_it.vale   = 0U;
}
