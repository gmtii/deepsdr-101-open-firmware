/*
 * QUE DIVIDIR ENTRE 255 SIN DIVIDIR SEA EXACTAMENTE DIVIDIR ENTRE 255.
 *
 * POR QUE EXISTE - 08/11/2026.
 *
 * El compositor mezclaba cada pixel con cuatro divisiones entre 255, y
 * pintar una letra son unos cincuenta pixeles. En el binario de la radio
 * no eran divisiones que el compilador se hubiera quitado de en medio:
 * salian cinco `udiv` de verdad por pixel, y en este Cortex-M4 cada uno
 * cuesta entre dos y doce ciclos.
 *
 * Se sustituyeron por (x * 0x8081) >> 23. Eso es un truco, y un truco en
 * el camino que pinta TODO lo que se ve -el dial, la cascada, los menus,
 * el panel digital- no entra porque alguien diga que es exacto. Entra
 * cuando se ha comprobado uno por uno.
 *
 * Asi que este banco recorre los 65.536 valores que puede tomar x y
 * compara con la division de verdad. No una muestra: todos. Tarda unos
 * milisegundos.
 *
 * Y comprueba tambien la otra: nib*255/15 pasa a nib*17, que es exacta
 * porque 255 es 15 por 17 -y por eso vale para CUALQUIER nib, no solo
 * para los de cuatro bits-.
 *
 * SI ALGUN DIA ESTO SE PONE ROJO no hay nada que discutir: lo que hay en
 * gfx2.c no es una division entre 255 y la pantalla esta mintiendo en los
 * colores de cada mezcla.
 */
#include <stdio.h>
#include <stdint.h>

/* La MISMA linea que gfx2.c. Si alla cambia y aqui no, este banco deja de
 * medir lo que cree y por eso el numero magico va escrito en los dos
 * sitios a la vista, no escondido en una cabecera compartida. */
static uint32_t div255(uint32_t x)
{
    return (x * 0x8081u) >> 23;
}

int main(void)
{
    uint32_t x;
    unsigned long malos = 0UL;
    uint32_t primero = 0u;

    printf("== Dividir entre 255 sin dividir ==\n\n");

    for (x = 0u; x <= 65535u; x++) {
        if (div255(x) != (x / 255u)) {
            if (malos == 0UL) { primero = x; }
            malos++;
        }
    }
    printf("    (x * 0x8081) >> 23 contra x / 255, de 0 a 65535   %s",
           malos ? "MAL " : "bien");
    if (malos) {
        printf("  %lu fallan, el primero x=%u: %u contra %u\n",
               malos, primero, div255(primero), primero / 255u);
    } else {
        printf("  los 65536\n");
    }

    {
        unsigned long m2 = 0UL;
        uint32_t n;
        for (n = 0u; n < 256u; n++) {
            if ((n * 17u) != ((n * 255u) / 15u)) { m2++; }
        }
        printf("    nib * 17 contra nib * 255 / 15, de 0 a 255        %s",
               m2 ? "MAL " : "bien");
        printf("  %s\n", m2 ? "" : "los 256");
        malos += m2;
    }

    /* Y el limite escrito en gfx2.c: que la multiplicacion de dentro no
     * desborde. 65535 * 0x8081 tiene que caber en 32 bits. */
    {
        int cabe = (0xFFFFFFFFu / 0x8081u) >= 65535u;
        printf("    y 65535 * 0x8081 cabe en 32 bits                  %s\n",
               cabe ? "bien" : "MAL ");
        if (!cabe) { malos++; }
    }

    printf("\n");
    if (malos) {
        printf("  gfx2.c mezcla CADA PIXEL con esto. Si no es la misma\n");
        printf("  division, los colores de la pantalla no son los que\n");
        printf("  dice la paleta.\n");
        printf("DIV255: HAY FALLOS\n");
        return 1;
    }
    printf("TODO CORRECTO (0 fallos)\n");
    return 0;
}
