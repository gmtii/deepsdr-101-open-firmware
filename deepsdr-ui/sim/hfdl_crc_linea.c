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

    printf("\n  %s\n", fallos ? "*** MAL ***" : "todo correcto");
    return fallos ? 1 : 0;
}
