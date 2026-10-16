/*
 * IMAGEN PATRON DEL ESPECTRO - 24/09/2026.
 *
 * Existe para poder reescribir el paso 3 de spectrum_draw() sin cambiar ni
 * un pixel de lo que se ve.
 *
 * EL PROBLEMA QUE LO ORIGINA. Medido en la radio: el fotograma dura unos 69
 * ms y 65 son el espectro. La cascada, que tiene 54.432 pixeles, cuesta 4;
 * el espectro, que tiene 157.248, cuesta 65. Si fuera cuestion de mover
 * pixeles al panel serian unos 11. Los otros 53 se van en DECIDIR que color
 * lleva cada pixel: el paso 3 hace, por cada uno de los 157.248, media
 * docena de comparaciones sobre siete tablas distintas, y el desensamblado
 * ensena que al compilador no le caben en registros y recarga media docena
 * de variables de la pila en cada vuelta.
 *
 * Eso tiene arreglo -decidir por COLUMNA y rellenar tramos seguidos, en vez
 * de decidir por pixel- pero es reescribir el bucle que dibuja la pantalla
 * principal de la radio. Y ahi no vale "parece que se ve igual".
 *
 * ASI QUE: esto renderiza el espectro en una bateria de casos, se queda con
 * el resumen de cada imagen, y lo compara con el guardado. Si un solo pixel
 * cambia, salta. La referencia se graba con --graba ANTES de tocar nada, y
 * a partir de ahi manda ella.
 *
 * De paso saca los contadores del bus del simulador (ventanas abiertas y
 * accesos), que es donde se vera si la reescritura cambia tambien la forma
 * de volcar y no solo la de decidir.
 *
 * Compila el User/spectrum.c DE VERDAD, no la copia de vendor/ - por la
 * misma razon que sim/puntas.c, que lo explica en su cabecera.
 */
#include "lcd_sim.h"
#include "gfx.h"
#include "gfx2.h"
#include "palette.h"
#include "spectrum.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const palette_t *g_pal = &k_pal_oscura;

#define BINS 796
#define PX   40
#define PY   108
#define PW   756
#define PH   208

static float g_db[BINS];

/* Resumen de la imagen. FNV-1a sobre el marco entero: cualquier pixel que
 * cambie cambia el numero. No es criptografia, es un detector de cambios. */
static unsigned long resumen(void)
{
    const uint16_t *fb = lcd_sim_fb();
    unsigned long h = 2166136261UL;
    int i;

    for (i = 0; i < LCD_W * LCD_H; i++) {
        h ^= (unsigned long)fb[i];
        h *= 16777619UL;
        h &= 0xFFFFFFFFUL;
    }
    return h;
}

/* Una señal con suelo, unos picos anchos y otros estrechos, y un escalon
 * muy vertical - que es donde el "puente" entre columnas vecinas decide. */
static void senal(int semilla)
{
    int i;
    unsigned s = (unsigned)semilla * 2654435761U + 12345U;

    for (i = 0; i < BINS; i++) {
        s = s * 1103515245U + 12345U;
        g_db[i] = -104.0f + (float)((s >> 16) % 7U);
    }
    for (i = 100; i < 104; i++) { g_db[i] = -30.0f; }   /* pico estrecho */
    for (i = 300; i < 340; i++) { g_db[i] = -45.0f; }   /* pico ancho */
    for (i = 500; i < 520; i++) { g_db[i] = -95.0f + (float)((i - 500) * 3); } /* rampa */
    for (i = 600; i < 610; i++) { g_db[i] = -25.0f; }   /* escalon vertical */
}

typedef struct {
    const char       *nombre;
    spectrum_style_t  estilo;
    int               suavizado;
    int               marca_px;
    unsigned char     banda;
    int               banda_lo, banda_hi;
    float             db_min, db_max;
    int               semilla;
    unsigned char     puente;   /* spectrum_set_bridge() */
    unsigned char     traza_blanca; /* spectrum_set_heatmap_trace_white() */
} caso_t;

static const caso_t k_casos[] = {
    { "mapa de calor, sin banda",   SPECTRUM_STYLE_HEATMAP, 0,   0, 0U,   0,   0, -110.0f, -20.0f, 1 , 1U, 0U },
    { "mapa de calor, con banda",   SPECTRUM_STYLE_HEATMAP, 0,  20, 1U, -30,  30, -110.0f, -20.0f, 1 , 1U, 0U },
    { "mapa de calor, suavizado 2", SPECTRUM_STYLE_HEATMAP, 2,  20, 1U, -30,  30, -110.0f, -20.0f, 2 , 1U, 0U },
    { "linea, sin banda",           SPECTRUM_STYLE_LINE,    0,   0, 0U,   0,   0, -110.0f, -20.0f, 1 , 1U, 0U },
    { "linea, con banda",           SPECTRUM_STYLE_LINE,    0, -50, 1U, -80, -20, -110.0f, -20.0f, 3 , 1U, 0U },
    { "contorno, con banda",        SPECTRUM_STYLE_OUTLINE, 0,   0, 1U, -10,  10, -110.0f, -20.0f, 1 , 1U, 0U },
    { "contorno, margen estrecho",  SPECTRUM_STYLE_OUTLINE, 1,   5, 1U,  -5,   5,  -70.0f, -50.0f, 4 , 1U, 0U },
    { "linea, todo por debajo",     SPECTRUM_STYLE_LINE,    0,   0, 0U,   0,   0,  -20.0f,  -0.0f, 5 , 1U, 0U },
    { "linea, todo saturado",       SPECTRUM_STYLE_LINE,    0,   0, 0U,   0,   0,-130.0f,-120.0f, 6, 1U, 0U },
    /* Los dos ajustes que se quedaban fuera y que tocan justo el pixel de
     * la traza: el puente entre columnas vecinas y la traza blanca del mapa
     * de calor. Sin estos, la reescritura del paso 3 podria cambiarlos sin
     * que nadie se enterase. */
    { "mapa de calor, traza blanca",SPECTRUM_STYLE_HEATMAP, 0,  20, 1U, -30,  30, -110.0f, -20.0f, 1, 1U, 1U },
    { "mapa de calor, sin puente",  SPECTRUM_STYLE_HEATMAP, 0,  20, 1U, -30,  30, -110.0f, -20.0f, 1, 0U, 0U },
    { "contorno, sin puente",       SPECTRUM_STYLE_OUTLINE, 0,   0, 1U, -10,  10, -110.0f, -20.0f, 1, 0U, 0U },
};
#define N_CASOS ((int)(sizeof k_casos / sizeof k_casos[0]))

static void pinta(const caso_t *c)
{
    /* lcd_sim_reset() avisa al espectro por su cuenta - ver su comentario en
     * sim/lcd_sim.c. Sin eso, este borrado entre casos dejaba el panel en
     * negro sin que el espectro se enterase y dos casos salian MAL. */
    lcd_sim_reset(0x0000);
    lcd_sim_cost_reset();
    spectrum_set_line_smooth((uint8_t)c->suavizado);
    spectrum_set_style(c->estilo);
    spectrum_set_bridge(c->puente);
    spectrum_set_heatmap_trace_white(c->traza_blanca);
    /* Dos cuadros: el primero siembra la media temporal, el segundo ya es
     * el regimen permanente, que es lo que se ve en la radio. */
    senal(c->semilla);
    spectrum_draw(g_db, BINS, PX, PY, PW, PH, c->db_min, c->db_max,
                  (int16_t)c->marca_px, c->banda,
                  (int16_t)c->banda_lo, (int16_t)c->banda_hi);
    lcd_sim_cost_reset();
    spectrum_draw(g_db, BINS, PX, PY, PW, PH, c->db_min, c->db_max,
                  (int16_t)c->marca_px, c->banda,
                  (int16_t)c->banda_lo, (int16_t)c->banda_hi);
}

/*
 * EL CHIVATO - 05/10/2026.
 *
 * El repintado parcial depende de enterarse cuando alguien pinta dentro del
 * rectangulo del espectro. Eso lo cuenta gfx.c (ver "QUIEN HA PINTADO
 * DENTRO DE ESTE RECTANGULO" en gfx.c) y lo mira spectrum_draw().
 *
 * ESTO LO COMPRUEBA, y existe por un sitio concreto: rtty_scope_draw()
 * pinta dos rayas verticales encima del espectro y su propio comentario
 * dice que no borra la anterior porque "spectrum_draw() repinta todas las
 * columnas en cada llamada". Si el chivato dejara de funcionar, esas rayas
 * dejarian un rastro en los modos digitales y nadie se enteraria hasta
 * verlo en la radio.
 *
 * Tres pasos: un cuadro que se salta bandas, una raya encima como la del
 * osciloscopio, y otro cuadro - que tiene que salir ENTERO.
 */
static int prueba_chivato(void)
{
    const caso_t *c = &k_casos[0];
    uint32_t salt = 0U, tot = 0U;

    lcd_sim_reset(0x0000);
    senal(c->semilla);
    spectrum_draw(g_db, BINS, PX, PY, PW, PH, c->db_min, c->db_max,
                  (int16_t)c->marca_px, c->banda,
                  (int16_t)c->banda_lo, (int16_t)c->banda_hi);
    spectrum_draw(g_db, BINS, PX, PY, PW, PH, c->db_min, c->db_max,
                  (int16_t)c->marca_px, c->banda,
                  (int16_t)c->banda_lo, (int16_t)c->banda_hi);
    spectrum_parcial_diag(&salt, &tot);
    if (salt == 0U) {
        printf("  chivato: el segundo cuadro no se salto ninguna banda,"
               " la prueba no vale   MAL\n");
        return 1;
    }

    /* Una raya vertical dentro del espectro, como las del osciloscopio. */
    gfx_vline(PX + 300, PY + 10, 60, 0xF800);

    spectrum_draw(g_db, BINS, PX, PY, PW, PH, c->db_min, c->db_max,
                  (int16_t)c->marca_px, c->banda,
                  (int16_t)c->banda_lo, (int16_t)c->banda_hi);
    spectrum_parcial_diag(&salt, &tot);
    if (salt != 0U) {
        printf("  chivato: han pintado encima del espectro y el cuadro"
               " siguiente se salto %u bandas   MAL\n", (unsigned)salt);
        return 1;
    }
    printf("  chivato: pintar encima fuerza el cuadro entero  ok\n");
    return 0;
}

int main(int argc, char **argv)
{
    int graba = 0, parcial = 1, a;

    /*
     * --sinparcial: apaga el atajo de spectrum_draw() (ver "LAS BANDAS QUE
     * NO HACE FALTA VOLCAR" en spectrum.c) y pinta todo entero.
     *
     * ASI SE COMPRUEBA EL ATAJO, y en dos EJECUCIONES y no en una: el
     * espectro arrastra estado de un cuadro al siguiente -media temporal,
     * retencion de picos-, asi que pintar el mismo caso dos veces seguidas
     * dentro del mismo proceso no da lo mismo, y el primer intento de
     * comparar atajo y camino largo asi reventó nueve casos de doce.
     *
     * Con dos ejecuciones, las dos salen del mismo estado inicial y
     * recorren los casos en el mismo orden. Si las dos cuadran con la MISMA
     * imagen patron, entonces el atajo pinta lo que pintaba el camino largo
     * antes de que el atajo existiera. make espectrooro corre las dos.
     */
    for (a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--graba") == 0)      { graba = 1; }
        else if (strcmp(argv[a], "--sinparcial") == 0) { parcial = 0; }
    }
    spectrum_parcial_set((uint8_t)parcial);
    unsigned long oro[N_CASOS];
    FILE *f;
    int i, malos = 0;
    double bus = 0.0;

    if (!graba) {
        f = fopen("sim/espectro_oro.dat", "rb");
        if (!f) {
            printf("  no encuentro sim/espectro_oro.dat\n"
                   "  (se graba una vez con: ./out/espectrooro --graba)\n");
            return 2;
        }
        if (fread(oro, sizeof oro[0], N_CASOS, f) != (size_t)N_CASOS) {
            printf("  sim/espectro_oro.dat no tiene los %d casos:"
                   " se han anadido casos sin regrabar\n", N_CASOS);
            fclose(f);
            return 2;
        }
        fclose(f);
    }

    printf("  %d casos, marco de %dx%d en (%d,%d)%s\n", N_CASOS, PW, PH, PX, PY,
           parcial ? "" : "   SIN repintado parcial");
    for (i = 0; i < N_CASOS; i++) {
        unsigned long h;
        lcd_cost_t k;

        pinta(&k_casos[i]);
        h = resumen();
        k = lcd_sim_cost();

        if (graba) {
            oro[i] = h;
            printf("    %-30s %08lx   ventanas %6llu  accesos %8llu\n",
                   k_casos[i].nombre, h,
                   (unsigned long long)k.windows,
                   (unsigned long long)k.bus_writes);
        } else if (h != oro[i]) {
            printf("    %-30s %08lx  ESPERADO %08lx   MAL\n",
                   k_casos[i].nombre, h, oro[i]);
            malos++;
        } else {
            uint32_t salt = 0U, tot = 0U;

            spectrum_parcial_diag(&salt, &tot);
            bus += (double)k.bus_writes;
            printf("    %-30s ok   ventanas %6llu  accesos %8llu"
                   "   bandas saltadas %u/%u\n",
                   k_casos[i].nombre,
                   (unsigned long long)k.windows,
                   (unsigned long long)k.bus_writes,
                   (unsigned)salt, (unsigned)tot);
        }
    }

    if (graba) {
        f = fopen("sim/espectro_oro.dat", "wb");
        if (!f) { printf("  no puedo escribir sim/espectro_oro.dat\n"); return 2; }
        fwrite(oro, sizeof oro[0], N_CASOS, f);
        fclose(f);
        printf("  grabada la referencia de %d casos\n", N_CASOS);
        return 0;
    }
    if (malos) { printf("  HAY FALLOS (%d casos distintos)\n", malos); return 1; }
    if (parcial && prueba_chivato()) { return 1; }
    printf("  %s: %.0f accesos al bus en %d cuadros, %.0f por cuadro\n",
           parcial ? "repintado parcial" : "repintado entero", bus, N_CASOS,
           bus / (double)N_CASOS);
    printf("  TODO CORRECTO: ni un pixel distinto\n");
    return 0;
}
