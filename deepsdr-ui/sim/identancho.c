/*
 * LOS NOMBRES Y LAS NOTAS DE IDENT, CONTRA LAS DOS COSAS QUE LOS CORTAN.
 *
 * POR QUE EXISTE
 * --------------
 * Pedido por el usuario, 06/10/2026.
 *
 * Cortada a mitad de palabra, y justo antes de los dos ajustes que son el
 * motivo de que esa nota exista. La escribe escribe() con snprintf() sobre
 * un buffer de IDENT_LARGO, y snprintf NO SE QUEJA: corta y sigue. Para el
 * compilador no pasa nada; para quien mira la radio, la frase se acaba a
 * mitad.
 *
 * LAS DOS COSAS QUE LA PUEDEN CORTAR SON DISTINTAS, y por eso se miden las
 * dos:
 *
 *   el buffer   IDENT_LARGO bytes. Lo corta snprintf, en silencio.
 *   la pantalla el ancho del panel en pixeles, con fuente proporcional.
 *
 * Este proyecto ya se ha dado con las dos, y por separado: en las celdas de
 * Informacion contabamos CARACTERES cuando mandaban los PIXELES, y aqui
 * teniamos pixeles de sobra -la nota mas larga mide 510 px en un panel de
 * 792- y nos quedamos cortos de BYTES. Contar una sola de las dos es no
 * medir.
 *
 * DE DONDE SALEN LOS TEXTOS
 * -------------------------
 * De ident_firmas(), o sea del ident.c de verdad, compilado aqui. No hay
 * copia que mantener: una firma nueva entra en esta prueba sola, en los dos
 * idiomas, sin que nadie se acuerde de venir a añadirla.
 */
#include "gfx2.h"
#include "palette.h"
#include "font_ui_14.h"
#include "ident.h"
#include "idioma.h"
#include <stdio.h>
#include <string.h>

const palette_t *g_pal = &k_pal_oscura;

/* El hueco de un renglon del panel digital: empieza en x=8 y la pantalla
 * mide 800, asi que quedan 792 menos el margen del otro lado. */
#define HUECO_PX 784

static int fallos = 0;

/*
 * Solo los PIXELES. El buffer ya no entra en esto: desde el 06/10/2026 los
 * nombres y las notas no se copian, se apuntan (ver s_lin en ident.c), asi
 * que IDENT_LARGO no los toca. Si alguna vez se volviera a copiar, lo que
 * fallaria seria el renglon en la radio y no esta prueba - por eso el
 * comentario de ident.h dice por que son punteros.
 */
static void mide(const char *que, const char *txt)
{
    int w = gfx2_text_w(txt, &font_ui_14);

    if (w > HUECO_PX) {
        printf("    %-6s %3u car  %4d px (tope %d)  SE SALE DE LA PANTALLA  %s\n",
               que, (unsigned)strlen(txt), w, HUECO_PX, txt);
        fallos++;
    }
}

int main(void)
{
    const ident_firma_t *f;
    uint16_t n = 0U, i;
    int idi;

    f = ident_firmas(&n);
    printf("los nombres y las notas de las %u firmas de ident.c\n", (unsigned)n);
    printf("  se pintan sin copiar, asi que lo unico que las corta es el\n");
    printf("  hueco del renglon: %d px con font_ui_14\n\n", HUECO_PX);

    /* Los dos idiomas: la traduccion puede ser mas larga que el original, y
     * de hecho en ingles varias lo son. */
    for (idi = 0; idi < 2; idi++) {
        idioma_pon((uint8_t)idi);
        printf("  %s:\n", idi == 0 ? "castellano" : "ingles");
        for (i = 0U; i < n; i++) {
            mide("nombre", ident_nombre(&f[i]));
            mide("nota",   ident_nota(&f[i]));
        }
        if (fallos == 0) { printf("    todas caben\n"); }
    }

    printf("\n%s (%d fallos)\n", fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
