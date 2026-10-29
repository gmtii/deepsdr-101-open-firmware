/*
 * LA LINEA DE DIAGNOSTICO DE HFDL, Y QUE NO SE SALGA DE SU BUFFER.
 *
 * hfdl_modo_linea_crc() arma a mano el renglon que la radio enseña debajo
 * de la lista -"P999/999 +9999Hz  D999  BPSK 12/40  4PSK 3/9"- sobre un
 * static char l[HFDL_CRUDO_TXT], que son 58 bytes. Lo arma con
 * l[p++] = c y con aj_u2s_local(&l[p], v), y NINGUNO DE LOS DOS MIRA EL
 * TOPE: el primero escribe donde caiga, el segundo mete tantos digitos
 * como tenga el numero.
 *
 * Los contadores de CRC cuentan tramas, asi que en una tarde de escucha
 * pasan de mil sin despeinarse. Con dos modos activos y cuatro cifras en
 * cada cuenta, la linea se sale.
 *
 * POR QUE ESTE BANCO ES DE CAJA BLANCA. Para llegar ahi por el aire
 * habria que inyectar miles de tramas buenas y malas y esperar a que los
 * contadores subieran solos, que es lento y ademas no garantiza el peor
 * caso. Incluyendo el .c se ponen los contadores donde hace falta y se
 * mide lo que se quiere medir. Es el mismo reparto que ya usan otros
 * bancos de aqui: el aire para el camino completo, la caja blanca para
 * los extremos.
 *
 * SE PROBO QUE SE PONE ROJO: con el codigo de antes del 16/10/2026 este
 * banco cantaba la linea salida de madre, y AddressSanitizer la señalaba
 * como global-buffer-overflow sobre `l`.
 */
#include <stdio.h>
#include <string.h>
#include "hfdl_modo.c"

/*
 * LOS DOS DEL BLIT, Y POR QUE ESTAN AQUI.
 *
 * waterfall.c entra en el enlazado porque hfdl_modo.c usa su buffer
 * compartido, y waterfall.c llama a dos funciones del gfx ORIGINAL de la
 * radio, que vive en vendor/ y no se publica. Este banco no pinta nada
 * -mide una cadena-, asi que en vez de arrastrar vendor/ y dejar el banco
 * sin correr en cualquier clon, se ponen los dos vacios.
 *
 * Que estan vacios importa: si algun dia este banco empieza a medir algo
 * que se dibuje, estos dos hay que quitarlos y traer el gfx de verdad.
 */
void gfx_blit_arranca(void);
void gfx_blit_espera(void);
void gfx_blit_arranca(void) { }
void gfx_blit_espera(void) { }

static int fallos;

static void exige(int cond, const char *que)
{
    printf("    %-58s %s\n", que, cond ? "ok" : "MAL");
    if (!cond) { fallos++; }
}

/* El peor caso de cada contador que entra en la linea. */
static void pon_contadores(uint32_t cuenta, unsigned modos)
{
    unsigned a;

    for (a = 0U; a < 4U; a++) { s_crc_ok[a] = 0U; s_crc_tot[a] = 0U; }
    for (a = 1U; a <= modos && a < 4U; a++) {
        s_crc_ok[a]  = cuenta;
        s_crc_tot[a] = cuenta;
    }
    s_pk_desvio_hz   = -9999.0f;
    s_pk_con_rafaga  = 999999U;
    s_d1_vistos      = 999999U;
    s_sin_renglon    = 999999U;
    s_pos_huerfanas  = 999999U;
}

int main(void)
{
    static const uint32_t k_cuentas[] = {
        0U, 1U, 9U, 99U, 999U, 9999U, 99999U, 999999U, 4294967295U
    };
    unsigned c, m;
    size_t peor = 0U;
    char peor_txt[128];

    printf("== LA LINEA DE CRC DE HFDL ==\n\n");
    peor_txt[0] = '\0';

    for (m = 1U; m <= 3U; m++) {
        for (c = 0U; c < sizeof k_cuentas / sizeof k_cuentas[0]; c++) {
            const char *s;
            size_t n;

            pon_contadores(k_cuentas[c], m);
            s = hfdl_modo_linea_crc();
            n = strlen(s);
            if (n > peor) {
                peor = n;
                snprintf(peor_txt, sizeof peor_txt, "%s", s);
            }
        }
    }
    printf("  el peor renglon mide %u de los %u que hay\n",
           (unsigned)peor, (unsigned)HFDL_CRUDO_TXT);
    printf("  %s\n\n", peor_txt);
    exige(peor < (size_t)HFDL_CRUDO_TXT,
          "la linea cabe en su buffer con cualquier contador");

    /* Y sin ninguna trama no se enseña nada, que es lo de siempre. */
    pon_contadores(0U, 0U);
    s_pk_con_rafaga = 0U;
    exige(hfdl_modo_linea_crc()[0] == '\0' || hfdl_modo_prekeys() != 0U,
          "sin tramas ni pre-keys la linea sale vacia");

    /*
     * ==================================================================
     * LA TABLA DE AVIONES VIVE EN LA MEMORIA PRESTADA - 28/10/2026
     * ==================================================================
     *
     * *** Pedido por el usuario: "amplia a 48 aceptando que se borre al
     * salir". ***
     *
     * Al pasar de 20 aviones a 48, la tabla y los renglones se mudaron de
     * .bss a la cabecera del prestamo (ver HFDL_RAM_AVIONES en
     * hfdl_ram.h). Eso cambia una cosa que el compilador no ve:
     * s_tabla.rec apunta DENTRO de esa region, asi que al soltarla el
     * puntero queda colgando sobre lo que sea que haya despues -pixeles
     * de cascada, o la ranura de FT8-.
     *
     * Si hfdl_modo_stop() no vaciara la tabla, hfdl_modo_aviones()
     * seguiria diciendo que hay aviones y EL MAPA los recorreria: se
     * pintarian aviones sacados de pixeles. No daria error, no se
     * colgaria, y las etiquetas serian basura verosimil, que es la peor
     * clase de fallo.
     *
     * Esto se mide aqui y no en otro sitio porque este banco corre con
     * AddressSanitizer: si alguien quita el vaciado, lo que salta no es un
     * numero raro sino el uso de la memoria en si.
     *
     * Y SE COMPRUEBA QUE CAZA: quitando las tres lineas de hfdl_modo_stop()
     * que vacian s_tabla, la primera exigencia de aqui abajo se pone roja.
     */
    printf("\n  LA TABLA DE AVIONES, AL SALIR DEL MODO\n");
    {
        hfdl_ac_event_t ev;
        uint8_t pide = 0U;

        printf("    caben %u aviones y %u renglones\n",
               (unsigned)HFDL_MODO_AVIONES, (unsigned)HFDL_MODO_LINEAS);
        exige(HFDL_MODO_AVIONES >= 48U && HFDL_MODO_LINEAS >= 48U,
              "son 48 y 48, como FT8");

        if (hfdl_modo_start()) {
            exige(s_tabla.cap >= 48U, "la tabla coge sus 48 de la cabecera");

            /* Un avion cualquiera, para que la tabla no este vacia. */
            memset(&ev, 0, sizeof ev);
            ev.flags = HFDL_AC_EV_ICAO;
            ev.icao = 0x4BB0F3UL;
            ev.ac_id = HFDL_AC_ID_NONE;
            (void)hfdl_ac_update(&s_tabla, &ev, &pide);
            exige(hfdl_modo_aviones() == 1U, "con un parte dentro, hay un avion");

            hfdl_modo_stop();
            exige(hfdl_modo_aviones() == 0U,
                  "al salir del modo la lista se borra");
            exige(s_tabla.rec == (hfdl_ac_rec_t *)0,
                  "y el puntero no se queda colgando sobre la memoria de otro");
            exige(hfdl_modo_linea(0U)[0] == '\0',
                  "y el primer renglon ya no dice nada");
        } else {
            printf("    (no se pudo coger el prestamo: no se mide)\n");
        }
    }

    printf("\n  %s\n", fallos ? "*** MAL ***" : "todo correcto");
    return fallos ? 1 : 0;
}
