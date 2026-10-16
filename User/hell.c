#include "hell.h"
#include "hfdl_ram.h"
#include <string.h>
#include <math.h>

/* ==========================================================================
 * ESTADO
 * ==========================================================================
 *
 * TODO EL ESTADO VIVE EN LA RAM PRESTADA, no solo las columnas.
 *
 * Primero se mudo el anillo y el enlazador bajo de 172 bytes de exceso a 68.
 * Los 68 que quedaban eran los ESCALARES -diez flotantes, dos contadores y
 * la estructura de informacion-, y en una radio con 1.960 bytes libres eso
 * tampoco sobra. Asi que se muda el conjunto: aqui solo se quedan el
 * interruptor y el puntero, cinco bytes.
 *
 * Se puede hacer porque este modulo no tiene nada que decir cuando esta
 * apagado: hell_info() con el modo parado devuelve ceros, que es la verdad.
 */
/*
 * CUATRO COLUMNAS DE ANILLO, Y NO DIECISEIS - 06/10/2026, con el enlazador
 * delante.
 *
 * La primera version pedia dieciseis "por tener colchon", y el enlazador
 * contesto "region RAM overflowed by 340 bytes". Con 1.960 bytes libres en
 * toda la radio, 224 de anillo no son un detalle.
 *
 * Y dieciseis no hacian falta. Quien vacia esto es el bucle principal, que
 * da una vuelta cada 69 ms porque lo marca el repintado del espectro, y las
 * columnas llegan a 17,5 por segundo, o sea una cada 57 ms. Con cuatro hay
 * 229 ms de colchon: cuatro vueltas.
 *
 * El fax y el SSTV si necesitan doce, pero por una razon que aqui no existe:
 * ellos ESCRIBEN la imagen en la flash del pendrive, y una escritura de
 * bloque bloquea el bucle medio segundo. Feld-Hell no guarda nada.
 */
#define COL_N 4U

typedef struct {
    float    n_punto;     /* muestras por punto */
    float    env, env_a;  /* la envolvente y su suavizado */
    float    pico, valle; /* los dos seguidores de los que sale el umbral */
    float    sig;         /* lo que persiguen por vuelta */
    float    acc, pos;    /* el reparto en puntos */
    uint32_t acc_n;
    uint32_t columnas;
    uint8_t  cab, col, fila;
    uint8_t  dat[COL_N * HELL_ALTO + HELL_ALTO];   /* anillo + la que se arma */
} hell_st_t;

static uint8_t    s_on;
static hell_st_t *s_st;

#define S_COL(i) (&s_st->dat[(uint32_t)(i) * HELL_ALTO])
#define S_CUR    (&s_st->dat[(uint32_t)COL_N * HELL_ALTO])

/* ==========================================================================
 * ARRANQUE
 * ========================================================================== */
void hell_start(float fs_hz)
{
    float fs = (fs_hz > 1000.0f) ? fs_hz : 12000.0f;
    uint8_t *b; uint32_t cap = 0U;

    s_on = 0U; s_st = 0;

    hfdl_ram_coge();
    b = hfdl_ram_todo(&cap);
    if (b == 0 || cap < (uint32_t)sizeof(hell_st_t)) {
        /* Sin prestamo no se arranca, y se dice que no: pintar con un
         * puntero nulo seria mucho peor que no pintar. */
        hfdl_ram_suelta();
        return;
    }
    s_st = (hell_st_t *)(void *)b;
    memset(s_st, 0, sizeof *s_st);

    s_st->n_punto = fs / HELL_PUNTOS_HZ;
    /* Un cuarto de punto de suavizado: 12 muestras a 12 kHz. */
    s_st->env_a = 1.0f / (0.25f * s_st->n_punto);
    if (s_st->env_a > 1.0f) { s_st->env_a = 1.0f; }
    /* Dos segundos de memoria en los seguidores. Ver su comentario. */
    s_st->sig = 1.0f / (2.0f * fs);

    s_on = 1U;
}

void hell_stop(void)
{
    /*
     * EL ORDEN: cerrar PRIMERO, soltar DESPUES - revision del 08/10/2026.
     *
     * Estaba al reves, y entre soltar y cerrar hay una rendija: la
     * interrupcion de audio ve s_on=1 con s_st todavia apuntando, y
     * escribe el anillo de columnas encima de la cascada que ya es de
     * otro. Es la misma regla que sstv_stop() deja escrita y que dsc_stop()
     * ya cumple.
     */
    {
        uint8_t estaba = s_on;
        s_on = 0U;
        s_st = 0;
        if (estaba) { hfdl_ram_suelta(); }
    }
}

uint8_t hell_activo(void) { return s_on; }

void hell_info(hell_info_t *out)
{
    if (!out) { return; }
    memset(out, 0, sizeof *out);
    if (!s_on || s_st == 0) { return; }
    out->activo   = 1U;
    out->columnas = s_st->columnas;
    out->nivel    = s_st->pico - s_st->valle;
    out->umbral   = 0.5f * (s_st->pico + s_st->valle);
}

/* ==========================================================================
 * UNA COLUMNA TERMINADA
 * ========================================================================== */
static void columna_cierra(void)
{
    uint8_t sig = (uint8_t)((s_st->cab + 1U) % COL_N);

    /*
     * Si el que pinta no ha venido a por la anterior, se pisa la MAS VIEJA y
     * no la nueva: con Hell, perder una columna de hace un segundo es perder
     * un trozo de letra que ya ha pasado, y perder la de ahora es partir la
     * letra que se esta formando.
     */
    if (sig == s_st->col) { s_st->col = (uint8_t)((s_st->col + 1U) % COL_N); }

    memcpy(S_COL(s_st->cab), S_CUR, HELL_ALTO);
    s_st->cab = sig;
    s_st->columnas++;
    s_st->fila = 0U;
}

/* ==========================================================================
 * LA ENTRADA
 * ========================================================================== */
void hell_process(const float *audio, uint32_t n)
{
    uint32_t k;

    if (!s_on || s_st == 0 || !audio) { return; }

    for (k = 0U; k < n; k++) {
        float a = audio[k];
        if (a < 0.0f) { a = -a; }

        s_st->env += s_st->env_a * (a - s_st->env);

        /* Los dos seguidores. Suben de golpe y bajan despacio, o al reves. */
        if (s_st->env > s_st->pico) { s_st->pico = s_st->env; }
        else                { s_st->pico += s_st->sig * (s_st->env - s_st->pico); }
        if (s_st->env < s_st->valle) { s_st->valle = s_st->env; }
        else                 { s_st->valle += s_st->sig * (s_st->env - s_st->valle); }

        s_st->acc += s_st->env;
        s_st->acc_n++;
        s_st->pos += 1.0f;

        if (s_st->pos >= s_st->n_punto) {
            float med = (s_st->acc_n > 0UL) ? (s_st->acc / (float)s_st->acc_n) : 0.0f;
            float lo  = s_st->valle;
            float hi  = s_st->pico;
            float t;
            uint8_t v;

            s_st->pos -= s_st->n_punto;
            s_st->acc = 0.0f; s_st->acc_n = 0UL;

            /*
             * A gris, estirando entre el valle y el pico MEDIDOS. Si los dos
             * estan pegados -no hay señal, o es toda del mismo nivel- se
             * saca negro en vez de amplificar el ruido hasta llenar la
             * pantalla de puntos: un panel lleno de basura se lee como "esto
             * no funciona", y un panel vacio se lee como "aqui no hay nada",
             * que es la verdad.
             */
            if ((hi - lo) < 1.0e-4f) {
                v = 0U;
            } else {
                t = (med - lo) / (hi - lo);
                if (t <= 0.0f)      { v = 0U; }
                else if (t >= 1.0f) { v = 255U; }
                else                { v = (uint8_t)(t * 255.0f + 0.5f); }
            }

            /*
             * DE ABAJO ARRIBA. El barrido de Hell empieza por el punto de
             * ABAJO de cada columna, asi que el punto numero s_fila va en la
             * fila (HELL_ALTO - 1 - s_fila) de la pantalla. Darle la vuelta
             * aqui y no en el dibujante es lo correcto: quien sabe como se
             * barre es este fichero, y el dibujante solo recibe "de arriba a
             * abajo", que es como se pinta todo lo demas.
             */
            S_CUR[HELL_ALTO - 1U - s_st->fila] = v;
            s_st->fila++;
            if (s_st->fila >= (uint8_t)HELL_ALTO) { columna_cierra(); }
        }
    }
}

const uint8_t *hell_columna(void)
{
    const uint8_t *p;

    if (!s_on || s_st == 0 || s_st->col == s_st->cab) { return 0; }
    p = S_COL(s_st->col);
    s_st->col = (uint8_t)((s_st->col + 1U) % COL_N);
    return p;
}
