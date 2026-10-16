/*
 * EL CERROJO DEL BUFFER PRESTADO - 25/09/2026.
 *
 * POR QUE EXISTE
 * --------------
 * La cascada y el area de trabajo de FT8 son LOS MISMOS BYTES: una union
 * (ver ft8_shared_ram.h). Eso no costo RAM y es lo unico que hizo que FT8
 * cupiera, pero deja una trampa que el compilador no puede ver: si se pinta
 * o se empuja una linea de cascada mientras FT8 esta decodificando, en la
 * pantalla salen las magnitudes de FT8 interpretadas como colores Y a FT8 se
 * le corrompen los datos. Ninguna de las dos cosas da un fallo. Las dos
 * "funcionan" mal y en silencio, que es la peor forma de fallar que hay.
 *
 * Durante un tiempo esto estuvo escrito como una REGLA en un comentario:
 * "cualquier modo que quiera pintar cascada tiene que mirar antes si FT8
 * esta en marcha". Una regla que hay que acordarse de cumplir en cada sitio
 * nuevo es una regla que un dia no se cumple -y ese dia no avisa nadie-, asi
 * que ahora es un cerrojo dentro de waterfall.c. Esto comprueba el cerrojo.
 *
 * QUE COMPRUEBA, Y QUE NO
 * -----------------------
 * Compila el User/waterfall.c DE VERDAD. Lo que mira es el estado real de
 * los bytes de la union antes y despues de cada llamada, no lo que devuelve
 * ninguna funcion: la pregunta es "¿se han movido esos bytes?", y esa solo
 * se contesta mirandolos.
 *
 * NO comprueba que ft8_modo.c pida y devuelva el buffer en su sitio; de eso
 * se encarga prestamo_check.py, que lee ese fichero. Dos preguntas
 * distintas, dos comprobaciones.
 */
#include "waterfall.h"
#include "ft8_shared_ram.h"
#include "lcd_sim.h"
#include <stdio.h>
#include <string.h>

static int fallos = 0;

static void pasa(const char *que, int bien)
{
    printf("    %-52s %s\n", que, bien ? "ok" : "*** MAL ***");
    if (!bien) { fallos++; }
}

/* Firma de TODO el bloque compartido, no solo del trozo de cascada: si FT8
 * tuviera sus tablas mas alla del final del buffer de pixeles, una escritura
 * de cascada podria pisarlas sin tocar un solo pixel. */
/*
 * Y LA FIRMA DE LA PANTALLA, que es la que faltaba.
 *
 * *** 28/09/2026. Este banco tenia aqui abajo un `pasa(..., 1)` -la
 * constante 1- con esta justificacion: "El simulador no tiene panel
 * enganchado en este banco, asi que si waterfall_blit() siguiera adelante,
 * escribiria en el LCD simulado y aqui reventaria por no haber ninguno.
 * Que vuelva sin mas es la prueba."
 *
 * Las dos mitades de esa frase se contradicen -"no tiene panel" y
 * "escribiria en el LCD simulado"- y la falsa es la primera: el banco
 * enlaza sim/lcd_sim.c, o sea que SI hay panel y el volcado puede correr
 * entero sin reventar. COMPROBADO: compilando el banco contra un
 * waterfall.c al que se le quita el cerrojo de waterfall_blit(), el banco
 * aprobaba los ocho casos y salia con codigo 0.
 *
 * Y lo que ese cerrojo impide no es poca cosa: es que, mientras FT8
 * decodifica, la cascada pinte en pantalla las MAGNITUDES de FT8
 * interpretadas como colores.
 *
 * Ahora se firma el marco entero del panel simulado. Con el buffer
 * prestado, volcar no puede cambiar ni un pixel; sin prestar, tiene que
 * cambiarlos. Las dos direcciones se comprueban, porque una firma que no
 * cambia nunca tampoco prueba nada.
 */
static unsigned long firma_pantalla(void)
{
    const uint16_t *fb = lcd_sim_fb();
    unsigned long h = 2166136261UL;
    size_t i;

    for (i = 0U; i < (size_t)LCD_W * (size_t)LCD_H; i++) {
        h ^= (unsigned long)(fb[i] & 0xFFU);
        h *= 16777619UL;
        h ^= (unsigned long)(fb[i] >> 8);
        h *= 16777619UL;
    }
    return h;
}

static unsigned long firma(void)
{
    const unsigned char *p = (const unsigned char *)&g_ft8_shared_ram;
    unsigned long h = 2166136261UL;
    size_t i;

    for (i = 0U; i < sizeof g_ft8_shared_ram; i++) {
        h ^= p[i];
        h *= 16777619UL;
    }
    return h;
}

int main(void)
{
    static uint8_t linea[WATERFALL_WIDTH];
    unsigned long antes, despues;
    uint16_t i;

    printf("CERROJO DEL BUFFER QUE LA CASCADA LE PRESTA A FT8\n");
    printf("  el bloque compartido mide %lu bytes\n\n",
           (unsigned long)sizeof g_ft8_shared_ram);

    for (i = 0U; i < (uint16_t)WATERFALL_WIDTH; i++) {
        linea[i] = (uint8_t)(i & 0xFFU);
    }

    /* --- 1. Sin prestar: la cascada funciona como siempre ------------- */
    printf("  SIN PRESTAR (lo de todos los dias)\n");
    waterfall_init();
    pasa("arranca sin prestar", waterfall_prestada() == 0U);

    antes = firma();
    waterfall_push_line(linea);
    despues = firma();
    pasa("empujar una linea SI cambia el buffer", antes != despues);

    /* --- 2. Prestado: nada se mueve ----------------------------------- */
    printf("\n  PRESTADO A FT8 (mientras decodifica)\n");
    waterfall_presta(1U);
    pasa("queda marcado como prestado", waterfall_prestada() == 1U);

    antes = firma();
    for (i = 0U; i < 40U; i++) { waterfall_push_line(linea); }
    despues = firma();
    /* ESTA es la comprobacion que importa: cuarenta lineas de cascada
     * contra el area de trabajo de FT8 y ni un byte movido. Sin el cerrojo,
     * cada una de esas cuarenta le sobreescribe 756 bytes de magnitudes. */
    pasa("40 lineas empujadas NO tocan ni un byte", antes == despues);

    {
        /* Y EL VOLCADO A PANTALLA TAMPOCO. Ver firma_pantalla(): esto era
         * un `pasa(..., 1)` y aprobaba con el cerrojo quitado. */
        static uint16_t lut[256];
        unsigned long p_antes, p_despues;
        uint16_t k;

        /* Una paleta que NO sea todo ceros: con la tabla a cero, volcar
         * escribiria el mismo negro que ya hay y la firma no se moveria
         * aunque el cerrojo faltara. La prueba tiene que poder fallar. */
        for (k = 0U; k < 256U; k++) { lut[k] = (uint16_t)(k | 0x0800U); }

        p_antes = firma_pantalla();
        waterfall_blit(0U, 0U, lut);
        p_despues = firma_pantalla();
        pasa("volcar NO pinta un solo pixel mientras esta prestado",
             p_antes == p_despues);
    }

    /* --- 3. Devuelto: vuelve a ser cascada ---------------------------- */
    printf("\n  DEVUELTO (al salir de FT8)\n");
    waterfall_presta(0U);
    waterfall_init();
    pasa("ya no esta prestado", waterfall_prestada() == 0U);

    {
        /* waterfall_init() tiene que dejarlo a cero: lo que habia ahi dentro
         * eran magnitudes de FT8, y si no se borran lo primero que se ve al
         * volver de FT8 es esa tabla pintada como colores. */
        const unsigned char *p = (const unsigned char *)g_ft8_shared_ram.waterfall_buf;
        size_t n = sizeof g_ft8_shared_ram.waterfall_buf;
        size_t k;
        int limpio = 1;

        for (k = 0U; k < n; k++) { if (p[k] != 0U) { limpio = 0; break; } }
        pasa("al devolverlo el buffer queda a cero", limpio);
    }

    antes = firma();
    waterfall_push_line(linea);
    despues = firma();
    pasa("y vuelve a aceptar lineas", antes != despues);

        /*
     * Y LA DIRECCION CONTRARIA, que es lo que hace que la de arriba valga.
     *
     * Una firma que no cambia nunca no prueba nada: si `waterfall_blit()`
     * no pintara jamas -por un fallo suyo, o porque el banco le pasa una
     * paleta que da el mismo color que ya hay- la comprobacion de "no
     * pinta mientras esta prestado" aprobaria igual. Asi que aqui, SIN
     * prestar, se exige que pinte.
     */
    {
        static uint16_t lut[256];
        unsigned long p_antes, p_despues;
        uint16_t k;

        for (k = 0U; k < 256U; k++) { lut[k] = (uint16_t)(k | 0x0800U); }
        for (k = 0U; k < 40U; k++) { waterfall_push_line(linea); }

        p_antes = firma_pantalla();
        waterfall_blit(0U, 0U, lut);
        p_despues = firma_pantalla();
        pasa("y sin prestar SI pinta (si no, lo de arriba no probaria nada)",
             p_antes != p_despues);
    }

printf("\n  %s\n", fallos ? "HAY FALLOS" : "TODO CORRECTO (0 fallos)");
    return fallos ? 1 : 0;
}
