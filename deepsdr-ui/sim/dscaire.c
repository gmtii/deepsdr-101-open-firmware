/*
 * Mete un .f32 de 12 kHz por el MISMO dsc.c que lleva la radio y enseña lo
 * que saca. No es un banco: es la herramienta con la que se comprueba el
 * decodificador contra señal de verdad, que es lo unico que separa "pasa mi
 * generador" de "decodifica DSC".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dsc.h"

/* La RAM prestada, igual que en helltest.c: las firmas copiadas de
 * User/hfdl_ram.h tal cual, y contando peticiones y sueltas. */
static unsigned g_cog, g_sue;
static unsigned char g_pozo[0xD4A0];
void      hfdl_ram_coge(void) { g_cog++; }
unsigned char *hfdl_ram_todo(unsigned *cap) { if (cap) { *cap = (unsigned)sizeof g_pozo; } return g_pozo; }
void      hfdl_ram_suelta(void) { g_sue++; }

int main(int argc, char **argv)
{
    FILE *f;
    static float buf[256];
    size_t n;
    dsc_info_t in;
    uint8_t i;
    unsigned long total = 0UL;

    if (argc < 2) { printf("uso: dscaire <fichero.f32>\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { printf("no puedo abrir %s\n", argv[1]); return 2; }

    if (argc > 2) { dsc_set_centro_hz((float)atof(argv[2])); }
    dsc_start(12000.0f);
    if (argc > 2) { dsc_set_centro_hz((float)atof(argv[2])); }
    while ((n = fread(buf, sizeof(float), 256, f)) > 0) {
        dsc_process(buf, (uint32_t)n);
        total += (unsigned long)n;
    }
    fclose(f);

    dsc_info(&in);
    printf("\n  === %s ===\n", argv[1]);
    printf("    %.2f s   enganchado %u   llamadas ok %lu   con fallo %lu   desvio %d Hz\n",
           (double)total / 12000.0, in.enganchado,
           (unsigned long)in.llamadas, (unsigned long)in.malas, in.desvio_hz);
    for (i = 0U; i < dsc_lineas(); i++) { printf("    %s\n", dsc_linea(i)); }
    if (dsc_lineas() == 0U) { printf("    (no ha salido ninguna llamada)\n"); }
    dsc_stop();
    printf("    RAM prestada: %u peticiones, %u sueltas\n", g_cog, g_sue);
    return 0;
}
