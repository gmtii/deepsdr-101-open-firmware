/* GENERADO por tools/gen_temas.py - no editar a mano. */
#ifndef TEMAS_TABLA_H
#define TEMAS_TABLA_H
#include "palette.h"
#include "spectrum.h"

typedef struct { const char *nombre; const char *desc;
                 const palette_t *pal; spectrum_palette_t wf; } sim_tema_t;

static const sim_tema_t k_sim_temas[] = {
    { "Oscura", "La de siempre, con el waterfall clásico", &k_pal_oscura, SPECTRUM_PALETTE_CLASSIC },
    { "Contrastada", "Negro real y saltos mayores, para pleno sol", &k_pal_contraste, SPECTRUM_PALETTE_TURBO },
    { "Ámbar", "Grises cálidos, sin azul: para la noche", &k_pal_ambar, SPECTRUM_PALETTE_INFERNO },
    { "Fría", "Grises azulados, para luz de día", &k_pal_fria, SPECTRUM_PALETTE_VIRIDIS },
    { "Oliva", "Verde oliva con tinta negra, tema claro", &k_pal_oliva, SPECTRUM_PALETTE_SMOKE },
    { "Naranja", "Naranja fuerte con tinta negra, tema claro", &k_pal_naranja, SPECTRUM_PALETTE_FIRE },
    { "Fósforo", "Verde fósforo sobre negro, como un terminal", &k_pal_fosforo, SPECTRUM_PALETTE_PHOSPHOR },
    { "Chemistry", "Papel blanco con tinta negra, para pantallas flojas", &k_pal_chemistry, SPECTRUM_PALETTE_SMOKE },
    { "Lino", "Blanco roto cálido, tema claro", &k_pal_lino, SPECTRUM_PALETTE_SMOKE },
    { "Marfil", "Crema amarillento, tema claro", &k_pal_marfil, SPECTRUM_PALETTE_SMOKE },
    { "Arena", "Beige tostado, tema claro", &k_pal_arena, SPECTRUM_PALETTE_SMOKE },
    { "Pergamino", "Crema envejecido, tema claro", &k_pal_pergamino, SPECTRUM_PALETTE_SMOKE },
    { "Niebla", "Gris azulado muy claro, tema claro", &k_pal_niebla, SPECTRUM_PALETTE_SMOKE },
    { "Almendra", "Crema rosado, tema claro", &k_pal_almendra, SPECTRUM_PALETTE_SMOKE },
    { "Menta", "Verde muy claro, tema claro", &k_pal_menta, SPECTRUM_PALETTE_SMOKE },
    { "Cielo", "Azul muy claro, tema claro", &k_pal_cielo, SPECTRUM_PALETTE_SMOKE },
    { "Trigo", "Dorado pálido, tema claro", &k_pal_trigo, SPECTRUM_PALETTE_SMOKE },
    { "Porcelana", "Blanco azulado, tema claro", &k_pal_porcelana, SPECTRUM_PALETTE_SMOKE },
};
#define SIM_TEMA_COUNT (int)(sizeof(k_sim_temas)/sizeof(k_sim_temas[0]))
#endif
