/*
 * ======================================================================
 * CON EL MODO APAGADO, NADIE ESCRIBE EN LA CASCADA - 28/10/2026
 * ======================================================================
 *
 * *** POR QUE EXISTE ESTE BANCO. ***
 *
 * El 28/10/2026 FT8 se ensancho a la banda entera -0 a 3.200 Hz- y la
 * union de ft8_shared_ram.h crecio. Para que entrara, tres modos que no
 * pueden estar a la vez que FT8 mudaron sus buffers a esa misma memoria
 * prestada: el fax (el anillo de renglones), WSPR (el estado del
 * receptor) y el SSTV (las tres lineas de color).
 *
 * Eso cambia una cosa importante: antes esos buffers eran arrays estaticos
 * que existian siempre, y llamar a una funcion del modo con el modo
 * apagado era, como mucho, inutil. Ahora el puntero es NULO mientras el
 * modo no esta, y la misma llamada seria una escritura en memoria de otro
 * -en los pixeles de la cascada, o en los bytes de FT8-.
 *
 * El compilador no puede ver esto, y el sintoma seria de los peores: unos
 * pixeles raros en la cascada, o un mensaje de FT8 que no sale, horas
 * despues y en otro modo.
 *
 * COMO SE MIDE: se llena la union entera con un patron, se llama a TODAS
 * las entradas publicas de los tres modos con el modo parado, y se mira
 * que no se haya movido ni un byte. No se comprueba lo que devuelven -con
 * el modo apagado puede devolver lo que quiera-, solo que no escriban.
 */
#include "ft8_shared_ram.h"
#include "wefax.h"
#include "wspr_rx.h"
#include "sstv.h"
#include <stdio.h>
#include <string.h>

static int g_fallos;

static void pasa(const char *que, int bien)
{
    printf("    %-56s %s\n", que, bien ? "ok" : "*** MAL ***");
    if (!bien) { g_fallos++; }
}

/* El patron: un byte distinto por posicion, para que una escritura de un
 * solo byte se note igual que una de seis kilobytes. */
static void llena(void)
{
    uint8_t *p = (uint8_t *)&g_ft8_shared_ram;
    uint32_t i;

    for (i = 0U; i < (uint32_t)sizeof g_ft8_shared_ram; i++) {
        p[i] = (uint8_t)(i * 31U + 7U);
    }
}

/* Devuelve el primer byte movido, o -1 si no se movio ninguno. */
static long mirado(void)
{
    const uint8_t *p = (const uint8_t *)&g_ft8_shared_ram;
    uint32_t i;

    for (i = 0U; i < (uint32_t)sizeof g_ft8_shared_ram; i++) {
        if (p[i] != (uint8_t)(i * 31U + 7U)) { return (long)i; }
    }
    return -1L;
}

static void cuenta(const char *modo)
{
    long d = mirado();

    if (d < 0L) {
        pasa(modo, 1);
    } else {
        printf("      el primer byte movido es el %ld de %u\n",
               d, (unsigned)sizeof g_ft8_shared_ram);
        pasa(modo, 0);
    }
}

int main(void)
{
    static float audio[256];
    static uint8_t renglon[2048];
    int i;

    for (i = 0; i < 256; i++) { audio[i] = 0.25f; }

    printf("\nCON EL MODO APAGADO, NADIE ESCRIBE EN LA MEMORIA PRESTADA\n");
    printf("=========================================================\n\n");
    printf("  union de %u bytes, llena de un patron antes de cada tanda\n\n",
           (unsigned)sizeof g_ft8_shared_ram);

    /* --- EL FAX ------------------------------------------------------- */
    printf("  EL FAX, parado\n");
    wefax_stop();          /* por si acaso: parado de verdad */
    llena();
    wefax_stop();                       /* parar dos veces no hace nada */
    (void)wefax_activo();
    wefax_process(audio, 256U);
    (void)wefax_linea_en(renglon);
    {
        wefax_info_t inf;
        wefax_info(&inf);
    }
    (void)wefax_get_lpm();
    wefax_sincroniza();
    wefax_fuerza_imagen();
    (void)wefax_gris_de_hz(2000.0f);
    {
        float a = 0.0f, b = 0.0f;
        wefax_fase_dbg(&a, &b);
    }
    for (i = 0; i < 8; i++) { (void)wefax_lpm_opcion((uint8_t)i); }
    cuenta("el fax parado no toca la memoria prestada");

    /* Y la velocidad, que borra el anillo cuando SI hay anillo. */
    llena();
    wefax_set_lpm(120U);
    wefax_set_lpm_idx(0U);
    cuenta("ni cambiando la velocidad con el fax parado");

    /* --- WSPR --------------------------------------------------------- */
    printf("\n  WSPR, parado\n");
    wspr_rx_fin();
    llena();
    wspr_rx_fin();
    wspr_rx_reinicia();
    wspr_rx_mete(audio, 256U);
    (void)wspr_rx_pasos_pendientes();
    (void)wspr_rx_perdidos();
    (void)wspr_rx_listo();
    (void)wspr_rx_pasos();
    {
        wspr_msg_t m;
        int16_t hz = 0;
        uint8_t q = 0;
        (void)wspr_rx_decodifica(&m, &hz, &q);
    }
    {
        uint16_t bin = 0U, paso = 0U;
        int32_t punt = 0;
        wspr_rx_ultimo(&bin, &paso, &punt);
    }
    {
        uint8_t q = 0U;
        int16_t hz = 0;
        wspr_rx_diag(&q, &hz);
    }
    cuenta("WSPR parado no toca la memoria prestada");

    /* Y pedirle que arranque con una region demasiado pequeña: tampoco. */
    llena();
    wspr_rx_init((void *)0, 0U);
    wspr_rx_mete(audio, 256U);
    cuenta("ni arrancando sin region, que es lo mismo que no arrancar");

    /* --- SSTV --------------------------------------------------------- */
    printf("\n  SSTV, parado\n");
    sstv_stop();
    llena();
    sstv_stop();
    (void)sstv_activo();
    sstv_process(audio, 256U);
    {
        uint16_t y = 0U;
        (void)sstv_linea(&y);
    }
    {
        sstv_info_t inf;
        sstv_info(&inf);
    }
    cuenta("el SSTV parado no toca la memoria prestada");

    printf("\n  %s (%d fallos)\n\n",
           g_fallos ? "HAY FALLOS" : "TODO CORRECTO", g_fallos);
    return g_fallos ? 1 : 0;
}
