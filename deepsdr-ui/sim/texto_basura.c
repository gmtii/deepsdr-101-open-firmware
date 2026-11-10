/*
 * BASURA POR LA PUERTA DEL TECLADO Y DEL FICHERO.
 *
 * POR QUE EXISTE - 08/11/2026, en la revision a fondo.
 * ----------------------------------------------------
 * sim/aire_basura.c le mete entradas hostiles a los dieciseis
 * decodificadores, por la puerta del AUDIO, con el detector de direcciones
 * y el de comportamiento indefinido puestos. Lleva meses haciendolo y ha
 * cazado lo que tenia que cazar.
 *
 * Pero por el audio no entra todo. Hay otras tres puertas por las que
 * entran bytes que NO ESCRIBE LA RADIO, y ninguna estaba vigilada asi:
 *
 *   CONFIG.CSV   los ajustes. Es un fichero de texto en un disco que se
 *                monta por USB y SE EDITA DESDE UN PC. Cualquiera puede
 *                abrirlo con el bloc de notas y dejarlo a medias.
 *   el fichero de bancos  lo mismo, y encima invita a tocarlo: si alguien
 *                quiere ponerle nombre a un banco, lo escribe ahi.
 *   el cable     cat_byte() se come byte a byte lo que mande el ordenador.
 *                Un programa de control a medio escribir, un puerto serie
 *                compartido o un cable con ruido mandan cualquier cosa.
 *
 * Y HAY UN MOTIVO MAS, ESPECIFICO: User/settings.c NO LO COMPILABA NINGUN
 * BANCO. Ninguno. De los 163 ficheros de User/ hay unos cuantos asi -se
 * apunto en la revision del 08/11- y este es el peor de la lista, porque
 * es el que lee lo que el usuario ha escrito a mano y el que decide con
 * que arranca la radio. Lo unico que lo miraba era csvlen_check.py, y eso
 * cuenta el LARGO del fichero que escribe, no lo que entiende al leerlo.
 *
 * QUE LE METE
 * -----------
 * Tres puertas, y cada una con su clase de basura:
 *
 *   1. el cable: bytes sueltos mezclando ';', letras de las dos cajas,
 *      cifras, retornos y bytes crudos de 0 a 255. Ordenes a medias,
 *      ordenes larguisimas, puntos y coma sueltos.
 *   2. el fichero de bancos: renglones con comas de mas y de menos,
 *      numeros gigantes, bytes crudos, finales de linea de Windows. Y
 *      despues de leerlo, se le pide que lo VUELVA A ESCRIBIR, que es
 *      donde se usa lo que quedo guardado.
 *   3. CONFIG.CSV: claves de verdad y claves inventadas, con valor, sin
 *      valor, con el valor en negativo, con la coma delante, y renglones
 *      que son basura cruda.
 *
 * QUE SE MIDE. No que entienda nada: una basura no tiene nada que
 * entender. Lo que se mide es que SOBREVIVA sin escribir fuera de sitio y
 * sin comportamiento indefinido. El veredicto no lo da este fichero: lo
 * dan -fsanitize=address y -fsanitize=undefined, que ven lo que ningun ojo
 * ve. Por eso este banco se compila SIEMPRE con los dos puestos, igual que
 * el del aire; sin ellos corre y no mira nada, y asi lo dice.
 *
 * REPETIBLE A PROPOSITO: la semilla es fija. Si un dia sale rojo, sale
 * rojo otra vez con la misma vuelta y se puede ir a ver cual era.
 *
 *     make textobasura                 (lo que corre en cada tanda)
 *     make textobasura VUELTAS=200000  (apretando, para una revision)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "banco.h"
#include "cat.h"
#include "settings.h"

#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_UNDEFINED__)
#  define SIN_DETECTORES 1
#endif

/* ------------------------------------------------------------------
 * Los tapones. settings.c habla con media radio y nada de eso es lo que
 * aqui se mide; lo unico que importa de verdad es el primero, que es por
 * donde se le cuela el fichero de mentira.
 * ------------------------------------------------------------------ */
volatile uint32_t g_msticks = 0;

static const uint8_t *g_csv;
static uint32_t       g_csv_n;

uint32_t spi_flash_read_file_by_name(const char n8[8], const char e3[3],
                                     uint8_t *buf, uint32_t cap)
{
    uint32_t n = (g_csv_n < cap) ? g_csv_n : cap;
    (void)n8; (void)e3;
    if (n != 0u) { memcpy(buf, g_csv, n); }
    return n;
}
uint8_t spi_flash_write_or_update_file(const char a[8], const char b[3],
                                       const uint8_t *d, uint32_t n)
{ (void)a; (void)b; (void)d; (void)n; return 1u; }
uint8_t spi_flash_cadena_seguida(const char a[8], const char b[3])
{ (void)a; (void)b; return 1u; }
uint8_t spi_flash_fichero_borra(const char a[8], const char b[3])
{ (void)a; (void)b; return 1u; }
uint32_t spi_flash_fichero_tam(const char a[8], const char b[3])
{ (void)a; (void)b; return 0u; }
uint8_t spi_flash_async_save_start(const char a[8], const char b[3],
                                   const uint8_t *d, uint32_t n)
{ (void)a; (void)b; (void)d; (void)n; return 1u; }
uint8_t spi_flash_async_save_poll(void) { return 0u; }

uint8_t  backlight_get_percent(void) { return 50u; }
void     ft8_decoder_set_own_grid(const char *g) { (void)g; }
const char *ft8_decoder_get_own_grid(void) { return "IN80"; }
uint32_t ms5351_get_xtal_hz(void) { return 25000000u; }
void     ms5351_set_xtal_hz(uint32_t h) { (void)h; }
uint8_t  spectrum_get_heatmap_trace_white(void) { return 0u; }
void     spectrum_set_heatmap_trace_white(uint8_t v) { (void)v; }
uint8_t  spectrum_get_palette(void) { return 0u; }
uint8_t  spectrum_get_style(void) { return 0u; }
void     spectrum_set_style(uint8_t v) { (void)v; }
const char *spectrum_palette_clave(uint8_t i) { (void)i; return "clasica"; }
uint8_t  spectrum_palette_de_clave(const char *c, uint8_t *fuera)
{ (void)c; if (fuera != 0) { *fuera = 0u; } return 0u; }
void     touch_get_calibration(int32_t *a, int32_t *b, int32_t *c, int32_t *d)
{ if (a) { *a = 0; } if (b) { *b = 0; } if (c) { *c = 0; } if (d) { *d = 0; } }
void     touch_set_calibration(int32_t a, int32_t b, int32_t c, int32_t d)
{ (void)a; (void)b; (void)c; (void)d; }

/* Lo poco que cat.c pide de la radio. Contesta siempre lo mismo: lo que se
 * prueba es el analizador de ordenes, no lo que contestan. */
static uint32_t f_lee(void)            { return 7110000u; }
static void     f_pon(uint32_t h)      { (void)h; }
static uint8_t  m_lee(void)            { return 1u; }
static void     m_pon(uint8_t m)       { (void)m; }
static uint8_t  sm_lee(void)           { return 5u; }
static uint8_t  v_lee(void)            { return 66u; }
static void     v_pon(uint8_t v)       { (void)v; }
static uint16_t an_lee(void)           { return 2300u; }
static void     dg_lee(cat_diag_t *d)  { memset(d, 0, sizeof *d); }
static void     dd_lee(cat_dibujo_t *d){ memset(d, 0, sizeof *d); }
static void     da_lee(cat_audio_t *a) { memset(a, 0, sizeof *a); }
static const cat_radio_t k_radio = {
    f_lee, f_pon, m_lee, m_pon, sm_lee, v_lee, v_pon, an_lee,
    dg_lee, dd_lee, da_lee
};

/* ------------------------------------------------------------------ */

static uint32_t s_rnd = 20261108u;   /* semilla fija: ver la cabecera */
static uint32_t rnd(void)
{
    s_rnd = s_rnd * 1664525u + 1013904223u;
    return s_rnd >> 8;
}

static void por_el_cable(void)
{
    char     sal[CAT_SALIDA_MAX];
    uint32_t n = rnd() % 300u, k;

    for (k = 0u; k < n; k++) {
        char c;
        switch (rnd() % 6u) {
        case 0:  c = ';'; break;
        case 1:  c = (char)('A' + (rnd() % 26u)); break;
        case 2:  c = (char)('a' + (rnd() % 26u)); break;
        case 3:  c = (char)('0' + (rnd() % 10u)); break;
        case 4:  c = (char)(rnd() & 0xFFu); break;
        default: c = (rnd() % 2u) ? '\r' : '\n'; break;
        }
        (void)cat_byte(c, sal);
    }
    cat_reset();
}

static void por_el_fichero_de_bancos(void)
{
    static char csv[4096];
    static char fuera[8192];
    uint32_t n = rnd() % (uint32_t)sizeof csv, k;

    for (k = 0u; k < n; k++) {
        switch (rnd() % 8u) {
        case 0:  csv[k] = '\n'; break;
        case 1:  csv[k] = ','; break;
        case 2:  csv[k] = (char)('0' + (rnd() % 10u)); break;
        case 3:  csv[k] = (char)(rnd() & 0xFFu); break;
        case 4:  csv[k] = '\r'; break;
        case 5:  csv[k] = (char)('a' + (rnd() % 26u)); break;
        default: csv[k] = 'n'; break;
        }
    }
    banco_init();
    (void)banco_de_csv(csv, n);
    /* Y QUE SE PUEDA VOLVER A ESCRIBIR. Media gracia de este caso esta
     * aqui: lo que entro raro se queda guardado, y el que lo usa de verdad
     * es el que escribe el fichero de vuelta. */
    (void)banco_a_csv(fuera, (uint32_t)sizeof fuera);
}

static void nombres_de_banco_largos(void)
{
    static char csv[512];
    uint32_t largo = rnd() % 400u, k;
    int p = snprintf(csv, sizeof csv, "nombre,%u,", 1u + (rnd() % 6u));

    if (p < 0) { return; }
    for (k = 0u; k < largo && ((uint32_t)p + k + 2u) < sizeof csv; k++) {
        csv[(uint32_t)p + k] = (char)('A' + (rnd() % 26u));
    }
    csv[(uint32_t)p + k]      = '\n';
    csv[(uint32_t)p + k + 1u] = '\0';
    banco_init();
    (void)banco_de_csv(csv, (uint32_t)strlen(csv));
}

static const char *const k_claves[] = {
    "vfo_hz", "mode", "step_hz", "audio_bw", "vol", "grid", "tz", "pal",
    "tema", "backlight", "att", "pga", "smooth", "speaker", "xtal", "cal",
    "filtro", "paleta", "estilo", "blanco", "toque", "nada_de_nada", ""
};

static void un_config_csv_de_mentira(void)
{
    static uint8_t csv[4096];
    settings_loaded_t out;
    uint32_t n = 0u, lineas = rnd() % 40u, L;

    for (L = 0u; L < lineas && (n + 80u) < sizeof csv; L++) {
        const char *k = k_claves[rnd() % (sizeof k_claves / sizeof k_claves[0])];
        int p;
        switch (rnd() % 5u) {
        case 0:
            p = snprintf((char *)csv + n, 80, "%s,%u\n", k, rnd());
            break;
        case 1:
            p = snprintf((char *)csv + n, 80, "%s,%d\r\n", k, (int)rnd() - 1000);
            break;
        case 2:
            p = snprintf((char *)csv + n, 80, "%s\n", k);
            break;
        case 3:
            p = snprintf((char *)csv + n, 80, ",%u\n", rnd());
            break;
        default: {
            uint32_t q, m = rnd() % 60u;
            for (q = 0u; q < m; q++) { csv[n + q] = (uint8_t)(rnd() & 0xFFu); }
            csv[n + m] = '\n';
            p = (int)m + 1;
            break; }
        }
        if (p > 0) { n += (uint32_t)p; }
    }
    g_csv = csv;
    g_csv_n = n;
    memset(&out, 0, sizeof out);
    (void)settings_load(&out);
}

int main(int argc, char **argv)
{
    static uint32_t k_pasos[] = {
        10u, 100u, 500u, 1000u, 5000u, 10000u, 12500u, 25000u, 100000u, 1000000u
    };
    long vueltas = (argc > 1) ? atol(argv[1]) : 20000;
    long i;

    printf("\nBASURA POR LA PUERTA DEL TECLADO Y DEL FICHERO\n");
    printf("=============================================\n\n");

#ifdef SIN_DETECTORES
    /*
     * Y SI NO LLEVA LOS DETECTORES, NO DICE QUE TODO ESTA BIEN.
     *
     * Este banco no comprueba ni un valor: lo unico que puede fallar aqui
     * lo ven -fsanitize=address y -fsanitize=undefined. Compilado sin
     * ellos corre, no mira nada y terminaria en verde, que es la peor
     * forma de fallar que tiene un guardian. La misma leccion que
     * copias_check.py y los demas.
     */
    printf("  *** COMPILADO SIN LOS DETECTORES: este banco no mira nada ***\n");
    printf("      Tiene que llevar -fsanitize=address y -fsanitize=undefined.\n");
    printf("      Ver la regla de local.mk.\n");
    (void)vueltas; (void)i; (void)k_pasos;
    return 1;
#else
    if (vueltas <= 0) {
        printf("  *** CERO VUELTAS: no he metido ni una basura ***\n");
        return 1;
    }

    banco_pasos_pon(k_pasos, (uint8_t)(sizeof k_pasos / sizeof k_pasos[0]));
    cat_init(&k_radio);

    for (i = 0; i < vueltas; i++) {
        por_el_cable();
        por_el_fichero_de_bancos();
        nombres_de_banco_largos();
        un_config_csv_de_mentira();
    }

    printf("  %ld vueltas por las tres puertas:\n", vueltas);
    printf("    el cable            cat_byte()\n");
    printf("    el de bancos        banco_de_csv() y banco_a_csv()\n");
    printf("    CONFIG.CSV          settings_load()\n");
    printf("\n  ni una escritura fuera de sitio, ni un comportamiento"
           " indefinido\n");
    printf("\nTEXTO BASURA: todo correcto\n");
    return 0;
#endif
}
