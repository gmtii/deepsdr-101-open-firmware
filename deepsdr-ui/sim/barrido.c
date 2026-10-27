/*
 * ======================================================================
 * LA MAQUINA DEL BARRIDO DE MEMORIAS - 27/10/2026
 * ======================================================================
 *
 * *** POR QUE EXISTE: el barrido se ha entregado mal CUATRO veces. ***
 *
 *   1. se paraba en la PRIMERA memoria (preguntaba si el silenciador
 *      estaba abierto, y en onda corta lo esta casi siempre);
 *   2. se paraba en la SEGUNDA, por lo mismo visto de otra manera;
 *   3. se paraba al llegar a la SEGUNDA VUELTA (habia un solo suelo para
 *      el banco entero, y el ruido no vale lo mismo en 3 MHz que en 28);
 *   4. y lo ultimo que se vio en la radio: iba tan despacio que en los
 *      canales de servicio se perdia las frases cortas.
 *
 * Las cuatro son de LOGICA, no de medida. sim/scan_nivel.c mide la
 * lectura -que es lo que las tres primeras daban por supuesto- pero no
 * toca la maquina, y la maquina es donde estaban los fallos.
 *
 * ASI QUE AQUI SE CORRE LA MAQUINA DE VERDAD. Las funciones no son una
 * copia: tools/barrido_dump.py las saca de User/main.c tal cual en cada
 * compilacion, asi que esta prueba no puede quedarse probando una version
 * vieja. Lo que se inventa es la RADIO de alrededor -un banco de
 * memorias, un reloj y un nivel por memoria que cambia con el tiempo-,
 * que es lo que permite correr media hora de barrido en un parpadeo.
 */
#include "demod_am.h"
#include "banco.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fallos = 0;

static void pasa(const char *que, int bien)
{
    printf("    %-58s %s\n", que, bien ? "ok " : "*** MAL ***");
    if (!bien) { fallos++; }
}

/* ======================================================================
 * LA RADIO DE MENTIRA
 * ====================================================================== */

volatile uint32_t g_msticks;

/* Cada memoria: el modo, el ruido de fondo de SU frecuencia -que es lo
 * que no vale lo mismo en una banda que en otra- y cuando esta ocupada. */
typedef struct {
    uint8_t      usada;
    demod_mode_t modo;
    float        piso_db;    /* el ruido de esa frecuencia */
    float        senal_db;   /* cuanto sube cuando hay alguien */
    uint32_t     desde_ms;   /* y entre que dos instantes habla */
    uint32_t     hasta_ms;
} mem_t;

static mem_t          s_mem[BANCO_MEM_N];
static uint8_t        s_puesta = 0xFFU;      /* la memoria sintonizada */
static demod_mode_t   s_modo   = DEMOD_MODE_AM;
static unsigned       s_sem    = 1u;
static unsigned       s_bips   = 0u;
static unsigned       s_nivel_on = 0u;
static unsigned       s_cerrada  = 0u;

static float ruidito(void)
{
    /* Un temblor pequeño y acotado: lo justo para que ninguna prueba
     * dependa de que dos lecturas salgan identicas al bit. */
    s_sem = s_sem * 1103515245u + 12345u;
    return (float)(((s_sem >> 16) & 0xFFFF) / 65535.0 - 0.5) * 1.5f;
}

/* ¿Habla alguien ahora mismo en la memoria sintonizada? */
static int hablando(void)
{
    const mem_t *m;

    if (s_puesta >= BANCO_MEM_N) { return 0; }
    m = &s_mem[s_puesta];
    if (m->senal_db <= 0.0f) { return 0; }
    return (g_msticks >= m->desde_ms && g_msticks < m->hasta_ms);
}

static float nivel_ahora(void)
{
    const mem_t *m;

    if (s_puesta >= BANCO_MEM_N) { return 0.0f; }
    m = &s_mem[s_puesta];
    return m->piso_db + (hablando() ? m->senal_db : 0.0f) + ruidito();
}

float demod_am_get_squelch_level_db(void) { return nivel_ahora(); }
float demod_am_get_nivel_db(void)         { return nivel_ahora(); }
/* En FM ancha la lectura va AL REVES: una emisora CALLA el ruido de
 * arriba. Los numeros son los medidos en el proyecto. */
float demod_wfm_get_sql_db(void)          { return hablando() ? 47.0f : 75.0f; }
demod_mode_t demod_am_get_mode(void)      { return s_modo; }
void demod_am_set_nivel_medir(uint8_t on) { s_nivel_on = on; }

static uint8_t mem_recupera(uint8_t bk, uint8_t c)
{
    uint8_t cambia;

    (void)bk;
    if (c >= BANCO_MEM_N || !s_mem[c].usada) { return 0U; }
    s_puesta = c;
    cambia = (uint8_t)(s_mem[c].modo != s_modo);
    s_modo = s_mem[c].modo;
    return cambia;
}

uint8_t banco_usadas(uint8_t bk)
{
    uint8_t i, n = 0U;

    (void)bk;
    for (i = 0U; i < BANCO_MEM_N; i++) { if (s_mem[i].usada) { n++; } }
    return n;
}

uint8_t banco_siguiente(uint8_t bk, uint8_t desde)
{
    uint8_t i, c;

    (void)bk;
    for (i = 1U; i <= BANCO_MEM_N; i++) {
        c = (uint8_t)(((desde >= BANCO_MEM_N) ? 0U
                       : (uint16_t)(desde + i)) % BANCO_MEM_N);
        if (desde >= BANCO_MEM_N) { c = (uint8_t)((i - 1U) % BANCO_MEM_N); }
        if (s_mem[c].usada) { return c; }
    }
    return 0xFFU;
}

static void bip_listo(void)  { s_bips++; }
static void bip_error(void)  { }
static void menu_screen_close(void) { s_cerrada++; }
static void mem_boton_repinta(void) { }
static void mem_rejilla_repinta(void) { }

/* Y AQUI ENTRA LA MAQUINA DE VERDAD, sacada de User/main.c. */
static uint8_t s_scan_on;
static uint8_t s_scan_banco;
static uint8_t s_scan_mem;
static uint32_t s_scan_ms;
static uint32_t s_scan_espera;
static uint8_t  s_scan_para_en;
static uint32_t s_scan_oida_ms;
static float   s_scan_pico;
static float   s_scan_valle;
static float   s_scan_piso[BANCO_MEM_N];

#include "barrido_maquina.h"

/* ======================================================================
 * CORRER EL BARRIDO
 * ====================================================================== */

/* Avanza `ms` milisegundos de reloj, llamando al latido en cada uno. */
static void corre_ms(uint32_t ms)
{
    uint32_t i;

    for (i = 0U; i < ms; i++) {
        g_msticks++;
        mem_scan_poll();
    }
}

static void pon_banco(unsigned n, demod_mode_t modo, float piso)
{
    unsigned i;

    memset(s_mem, 0, sizeof s_mem);
    for (i = 0U; i < n && i < BANCO_MEM_N; i++) {
        s_mem[i].usada   = 1U;
        s_mem[i].modo    = modo;
        s_mem[i].piso_db = piso;
    }
    s_puesta = 0xFFU;
    s_modo   = modo;
    s_bips   = 0u;
    s_cerrada = 0u;
    g_msticks = 1000U;
    s_scan_on = 0U;
    s_scan_para_en = 0U;
}

/* Cuanto tarda en dar una vuelta entera, medido de verdad: se cuentan los
 * saltos. Es EL numero del que depende cazar una frase corta. */
static uint32_t mide_vuelta(unsigned memorias)
{
    uint32_t t0;
    unsigned vueltas = 0u, i;
    uint8_t anterior;

    mem_scan_conmuta(0U);
    corre_ms(50U);               /* que arranque */
    t0 = g_msticks;
    anterior = s_scan_mem;
    for (i = 0u; i < 200000u && vueltas < memorias * 3u; i++) {
        corre_ms(1U);
        if (s_scan_mem != anterior) { anterior = s_scan_mem; vueltas++; }
    }
    mem_scan_para();
    return (uint32_t)((g_msticks - t0) / (memorias * 3u));
}

int main(void)
{
    char t[96];

    printf("\nLA MAQUINA DEL BARRIDO DE MEMORIAS\n\n");

    /* --------------------------------------------------------------
     * 1. UN BANCO VACIO DE EMISORAS SE RECORRE ENTERO Y NO SE PARA
     * -------------------------------------------------------------- */
    printf("  1. SIN EMISORAS, DA VUELTAS Y NO SE PARA\n");
    {
        unsigned i, vistas = 0u;
        uint8_t visto[BANCO_MEM_N];

        pon_banco(10u, DEMOD_MODE_USB, 40.0f);
        memset(visto, 0, sizeof visto);
        mem_scan_conmuta(0U);
        pasa("se enciende la medida del nivel al arrancar", s_nivel_on == 1u);
        pasa("y se vuelve a la ventana principal", s_cerrada == 1u);
        for (i = 0u; i < 30000u; i++) {
            corre_ms(1U);
            if (s_scan_mem < BANCO_MEM_N) { visto[s_scan_mem] = 1U; }
        }
        for (i = 0u; i < 10u; i++) { if (visto[i]) { vistas++; } }
        snprintf(t, sizeof t, "pasa por las diez memorias (vio %u)", vistas);
        pasa(t, vistas == 10u);
        pasa("y sigue andando a los treinta segundos", s_scan_on == 1u);
        pasa("sin haber pitado ni una vez", s_bips == 0u);
        mem_scan_para();
        pasa("al pararlo se apaga la medida del nivel", s_nivel_on == 0u);
    }

    /* --------------------------------------------------------------
     * 2. LO QUE TARDA UNA VUELTA, QUE ES EL NUMERO QUE IMPORTA
     * --------------------------------------------------------------
     * Una frase de tres segundos se caza si el barrido pasa por ese canal
     * mientras dura: la probabilidad es, mas o menos, lo que dura la
     * frase partido por lo que dura la vuelta. Por eso esto se mide y no
     * se supone.
     * -------------------------------------------------------------- */
    printf("\n  2. LO QUE CUESTA CADA MEMORIA\n");
    {
        uint32_t paso;

        pon_banco(10u, DEMOD_MODE_USB, 40.0f);
        paso = mide_vuelta(10u);
        printf("      %u ms por memoria, %u ms la vuelta de quince\n",
               paso, paso * 15u);
        snprintf(t, sizeof t, "cuadra con lo que dicen las constantes (%u ms)",
                 SCAN_ESPERA_MS + SCAN_MIRA_MS);
        /* La horquilla es de veinte milisegundos por los dos lados: el
         * reloj de mentira va de uno en uno y la cuenta se reparte entre
         * treinta saltos, asi que el ultimo digito no significa nada. */
        pasa(t, paso + 20U >= SCAN_ESPERA_MS + SCAN_MIRA_MS &&
                paso <= SCAN_ESPERA_MS + SCAN_MIRA_MS + 20U);
        /* Y el limite de lo que se pedia: que una vuelta de quince
         * memorias quepa en una frase corta de las de un canal de
         * servicio. Con el segundo de antes eran 18,75 s. */
        snprintf(t, sizeof t, "la vuelta de quince baja de 8 s (%u ms)",
                 paso * 15u);
        pasa(t, paso * 15u < 8000u);
    }

    /* --------------------------------------------------------------
     * 3. SE PARA DONDE HAY ALGUIEN
     * -------------------------------------------------------------- */
    printf("\n  3. SE PARA DONDE HAY ALGUIEN\n");
    {
        pon_banco(10u, DEMOD_MODE_USB, 40.0f);
        /* La memoria 6 habla desde el segundo 4 hasta el 20. */
        s_mem[6].senal_db = 25.0f;
        s_mem[6].desde_ms = g_msticks + 4000U;
        s_mem[6].hasta_ms = g_msticks + 20000U;

        mem_scan_conmuta(0U);
        corre_ms(12000U);
        snprintf(t, sizeof t, "a los doce segundos esta en la 6 (esta en la %u)",
                 s_scan_mem + 1U);
        pasa(t, s_scan_mem == 6U);
        pasa("y parado en ella, no de paso", s_scan_para_en == 1U);
        pasa("el barrido sigue encendido (no se ha apagado solo)",
             s_scan_on == 1U);
        snprintf(t, sizeof t, "ha pitado una sola vez (%u)", s_bips);
        pasa(t, s_bips == 1u);
    }

    /* --------------------------------------------------------------
     * 4. *** Y SIGUE SOLO CUANDO EL CANAL SE QUEDA LIBRE ***
     * --------------------------------------------------------------
     * Es lo que se pedia. Antes se paraba del todo y habia que volver a
     * arrancarlo a mano.
     * -------------------------------------------------------------- */
    printf("\n  4. Y SIGUE SOLO CUANDO EL CANAL SE QUEDA LIBRE\n");
    {
        uint32_t t_cala;

        pon_banco(10u, DEMOD_MODE_USB, 40.0f);
        s_mem[6].senal_db = 25.0f;
        s_mem[6].desde_ms = g_msticks + 4000U;
        s_mem[6].hasta_ms = g_msticks + 20000U;

        mem_scan_conmuta(0U);
        corre_ms(12000U);
        if (s_scan_mem != 6U) { pasa("(no llego a pararse en la 6)", 0); }

        /* Se calla en el segundo 20: se le dan treinta segundos y se mira
         * cuando se va. */
        t_cala = s_mem[6].hasta_ms;
        {
            unsigned i;
            uint32_t salio = 0U;

            for (i = 0u; i < 30000u && !salio; i++) {
                corre_ms(1U);
                if (s_scan_mem != 6U) { salio = g_msticks; }
            }
            if (!salio) {
                pasa("se va del canal libre", 0);
            } else {
                uint32_t tarda = salio - t_cala;

                printf("      tarda %u ms en soltar el canal\n", tarda);
                snprintf(t, sizeof t,
                         "no se va antes de los %u ms de gracia", SCAN_VUELVE_MS);
                pasa(t, tarda >= SCAN_VUELVE_MS);
                snprintf(t, sizeof t,
                         "y no tarda mucho mas (menos de %u ms)",
                         SCAN_VUELVE_MS + SCAN_MIRA_MS * 3U);
                pasa(t, tarda < SCAN_VUELVE_MS + SCAN_MIRA_MS * 3U);
            }
        }
        pasa("y despues sigue barriendo", s_scan_on == 1U);
    }

    /* --------------------------------------------------------------
     * 5. PERO UNA PAUSA ENTRE DOS FRASES NO LE MUEVE EL DIAL
     * --------------------------------------------------------------
     * Es la otra mitad de lo mismo, y la que hace que seguir solo sea
     * aceptable: si se fuera en el hueco entre una pregunta y su
     * contestacion, seguir solo seria peor que pararse del todo.
     * -------------------------------------------------------------- */
    printf("\n  5. UNA PAUSA ENTRE DOS FRASES NO LE MUEVE EL DIAL\n");
    {
        unsigned i;
        int se_fue = 0;

        pon_banco(10u, DEMOD_MODE_USB, 40.0f);
        s_mem[6].senal_db = 25.0f;
        s_mem[6].desde_ms = g_msticks + 4000U;
        s_mem[6].hasta_ms = g_msticks + 20000U;

        mem_scan_conmuta(0U);
        corre_ms(12000U);
        if (s_scan_mem != 6U) { pasa("(no llego a pararse en la 6)", 0); }

        /* Se calla dos segundos y vuelve: el hueco entre dos
         * intervenciones de la misma conversacion. */
        s_mem[6].hasta_ms = g_msticks;
        for (i = 0u; i < 2000u; i++) {
            corre_ms(1U);
            if (s_scan_mem != 6U) { se_fue = 1; }
        }
        s_mem[6].desde_ms = g_msticks;
        s_mem[6].hasta_ms = g_msticks + 10000U;
        corre_ms(3000U);

        pasa("aguanta los dos segundos de pausa", !se_fue);
        pasa("y sigue ahi cuando el otro contesta", s_scan_mem == 6U);
        snprintf(t, sizeof t, "sin volver a pitar (%u pitidos en total)",
                 s_bips);
        pasa(t, s_bips == 1u);
    }

    /* --------------------------------------------------------------
     * 6. *** Y NO SE LE ESCAPA POR LO ALTO EL SUELO DE LA MEMORIA ***
     * --------------------------------------------------------------
     * El suelo sube SCAN_PISO_SUBE_DB por visita para que una portadora
     * permanente no deje la memoria marcada para siempre. Vigilando, las
     * visitas son de un cuarto de segundo: dos decibelios por segundo. Si
     * se le dejara subir, a los pocos segundos de escuchar a alguien el
     * suelo se le pondria por encima y el barrido se iria con la emisora
     * sonando. Esto es lo que lo comprueba, y es media hora de escucha.
     * -------------------------------------------------------------- */
    printf("\n  6. ESCUCHANDO MEDIA HORA NO SE VA SOLO\n");
    {
        pon_banco(10u, DEMOD_MODE_USB, 40.0f);
        s_mem[6].senal_db = 25.0f;
        s_mem[6].desde_ms = g_msticks + 4000U;
        s_mem[6].hasta_ms = g_msticks + 1800000U;   /* media hora */

        mem_scan_conmuta(0U);
        corre_ms(12000U);
        if (s_scan_mem != 6U) { pasa("(no llego a pararse en la 6)", 0); }
        corre_ms(1700000U);
        printf("      el suelo de la 6 va por %.1f dB (la emisora, %.1f)\n",
               (double)s_scan_piso[6], (double)(40.0f + 25.0f));
        pasa("sigue en la 6 a la media hora", s_scan_mem == 6U);
        pasa("y sigue parado en ella", s_scan_para_en == 1U);
    }

    /* --------------------------------------------------------------
     * 7. *** UN BANCO QUE MEZCLA FM ANCHA CON OTRO MODO ***
     * --------------------------------------------------------------
     * Aqui habia un fallo y no se habia visto nunca: el pico se arrancaba
     * MIRANDO EL MODO ANTERIOR, porque se ponia antes de recuperar la
     * memoria. En FM ancha se busca el ruido MAS BAJO y en los demas el
     * nivel MAS ALTO, asi que al cruzar de un modo al otro el pico se
     * quedaba en su valor de arranque -9999 o -9999- y LOS DOS dan "hay
     * emisora": el barrido se paraba en TODAS las memorias que cambiaban
     * de modo, estuvieran vacias o no.
     *
     * Comprobado que esta prueba SI lo pilla: devolviendo el orden viejo
     * en mem_scan_salta(), esta seccion se pone roja. Una prueba que no
     * sabe fallar no prueba nada.
     * -------------------------------------------------------------- */
    printf("\n  7. UN BANCO QUE MEZCLA FM ANCHA CON BANDA LATERAL\n");
    {
        unsigned i;

        pon_banco(6u, DEMOD_MODE_USB, 40.0f);
        /* Las pares en FM ancha, las impares en banda lateral; ninguna
         * con nadie dentro. */
        for (i = 0u; i < 6u; i++) {
            s_mem[i].modo = (i % 2u) ? DEMOD_MODE_USB : DEMOD_MODE_WFM;
        }
        mem_scan_conmuta(0U);
        corre_ms(20000U);
        snprintf(t, sizeof t, "no se para en ninguna (pitidos: %u)", s_bips);
        pasa(t, s_bips == 0u && s_scan_para_en == 0U);
        pasa("y sigue barriendo", s_scan_on == 1U);
        mem_scan_para();
    }

    /* --------------------------------------------------------------
     * 8. Y EN FM ANCHA SI HAY EMISORA, SE PARA
     * --------------------------------------------------------------
     * Lo de arriba comprueba que no para de mas; esto, que para. Las dos
     * hacen falta: un barrido que no para nunca pasaria la de arriba.
     * -------------------------------------------------------------- */
    printf("\n  8. EN FM ANCHA, DONDE HAY EMISORA SI SE PARA\n");
    {
        unsigned i;

        pon_banco(6u, DEMOD_MODE_WFM, 40.0f);
        for (i = 0u; i < 6u; i++) { s_mem[i].modo = DEMOD_MODE_WFM; }
        s_mem[3].senal_db = 25.0f;
        s_mem[3].desde_ms = 0U;
        s_mem[3].hasta_ms = 0xFFFFFFFFU;

        mem_scan_conmuta(0U);
        corre_ms(20000U);
        snprintf(t, sizeof t, "se para en la 4 (esta en la %u)",
                 s_scan_mem + 1U);
        pasa(t, s_scan_mem == 3U && s_scan_para_en == 1U);
        mem_scan_para();
    }

    /* --------------------------------------------------------------
     * 9. BANDAS DISTINTAS: EL SUELO ES DE CADA MEMORIA
     * --------------------------------------------------------------
     * El fallo de la tercera entrega: un solo suelo para el banco y el
     * ruido no vale lo mismo en 3 MHz que en 28. Aqui hay quince
     * decibelios entre la memoria mas tranquila y la mas ruidosa, todas
     * vacias.
     * -------------------------------------------------------------- */
    printf("\n  9. QUINCE DECIBELIOS ENTRE BANDAS, Y NINGUNA OCUPADA\n");
    {
        unsigned i;

        pon_banco(8u, DEMOD_MODE_USB, 40.0f);
        for (i = 0u; i < 8u; i++) {
            s_mem[i].piso_db = 35.0f + (float)(i % 4u) * 5.0f;
        }
        mem_scan_conmuta(0U);
        corre_ms(40000U);    /* varias vueltas, que es cuando fallaba */
        snprintf(t, sizeof t, "no para en ninguna en diez vueltas (%u pitidos)",
                 s_bips);
        pasa(t, s_bips == 0u);
        mem_scan_para();
    }

    printf("\n%s (%d fallos)\n\n",
           fallos ? "HAY FALLOS" : "TODO CORRECTO", fallos);
    return fallos ? 1 : 0;
}
