#include "ipa_blit.h"
#include "gd32f4xx.h"

/* ---------------------------------------------------------------------
 * La tabla de 256 colores, en el formato que pide el IPA.
 * ---------------------------------------------------------------------
 * Nuestra paleta es RGB565 (uint16_t[256]) porque ese es el formato del
 * panel. El IPA, en cambio, solo carga tablas en ARGB8888 o RGB888, asi
 * que hay que traducirla. Un kilobyte, y se construye una sola vez por
 * cada paleta distinta que aparezca.
 *
 * La expansion de 5 y 6 bits a 8 no es un desplazamiento pelado: se
 * replican los bits altos en los huecos de abajo (0b11111 -> 0b11111111 y
 * no 0b11111000). Es la misma expansion que hace el propio panel, y la
 * misma que ya usa blend() en gfx2.c. Con el desplazamiento pelado el
 * blanco no llegaria a ser blanco y toda la paleta saldria un pelin
 * apagada respecto al camino de la CPU - una diferencia pequena, constante
 * y dificil de ver, que es la peor clase.
 */
static uint32_t s_lut[256];
static uint32_t s_lut_firma;
/* El puntero de la ultima paleta cargada y cuantas llamadas seguidas se
 * puede saltar la firma. Ver lut_pon(). */
#define LUT_SALTOS 8U
static const uint16_t *s_lut_ptr;
static uint8_t  s_lut_salta;      /* resumen de la paleta ya cargada */
static uint8_t  s_lut_cargada;
static uint8_t  s_on;             /* encendido por el usuario */
static uint8_t  s_vale;           /* el IPA responde */
static uint16_t s_fallos;

/* Espera maxima por una operacion del IPA. A 200 MHz una cascada entera
 * son unas decenas de miles de ciclos, asi que esto es holgadisimo; esta
 * para que un IPA colgado NO cuelgue el dibujo de la pantalla. En este
 * proyecto no se pone trabajo sin tope en un camino de interfaz. */
#define IPA_TOPE_VUELTAS  2000000UL

/* La SRAM principal, que es la unica memoria que el IPA ve. El TCM
 * (0x10000000) NO la ve: es memoria pegada al nucleo. */
#define SRAM_INI  0x20000000UL
#define SRAM_FIN  0x20030000UL

static uint8_t en_sram(const void *p, uint32_t bytes)
{
    uint32_t a = (uint32_t)p;
    return (uint8_t)((a >= SRAM_INI) && ((a + bytes) <= SRAM_FIN));
}

static uint32_t firma(const uint16_t *cmap)
{
    uint32_t h = 2166136261UL;   /* FNV-1a, que para 256 entradas sobra */
    uint16_t i;

    for (i = 0U; i < 256U; i++) {
        h ^= (uint32_t)cmap[i];
        h *= 16777619UL;
    }
    return h;
}

void ipa_blit_init(void)
{
    s_on = 0U; s_vale = 0U; s_lut_cargada = 0U; s_fallos = 0U;
    s_lut_ptr = 0; s_lut_salta = 0U;

    rcu_periph_clock_enable(RCU_IPA);
    ipa_deinit();

    /* Comprobacion de que el bloque esta vivo de verdad y no leemos ceros
     * de un periferico sin reloj: tras ipa_deinit() el registro de control
     * tiene que estar a cero y aceptar que le escribamos un modo. */
    ipa_pixel_format_convert_mode_set(IPA_FGTODE_PF_CONVERT);
    if ((IPA_CTL & IPA_CTL_PFCM) == IPA_FGTODE_PF_CONVERT) {
        s_vale = 1U;
        s_on   = 1U;
    }
}

uint8_t ipa_blit_hay(void)
{
    return (uint8_t)(s_vale && s_on);
}

void ipa_blit_pon(uint8_t on)
{
    s_on = (uint8_t)(on ? 1U : 0U);
}

uint16_t ipa_blit_fallos(void)
{
    return s_fallos;
}

/* Carga la paleta en el IPA si ha cambiado. Devuelve 0 si no se pudo. */
static uint8_t lut_pon(const uint16_t *cmap)
{
    uint32_t f;
    uint32_t vueltas;
    uint16_t i;

    /*
     * EL PUNTERO PRIMERO, LA FIRMA DESPUES - 05/10/2026.
     *
     * *** El dueño: "busca razones que hagan que se ralentice tanto la radio
     * completa como el espectro". ***
     *
     * La firma es un FNV-1a sobre las 256 entradas de la paleta, y se
     * calculaba ANTES de mirar si hacia falta. Esto se llama una o dos veces
     * por cada tira de la cascada -nueve tiras por fotograma-, asi que eran
     * entre 2.300 y 4.600 vueltas con una multiplicacion de 32 bits cada
     * una, por fotograma, para confirmar que la paleta sigue siendo la de
     * siempre.
     *
     * Y la paleta vive en flash, en una tabla constante: mientras el puntero
     * sea el mismo, el contenido es el mismo. Comparar el puntero es gratis.
     * La firma se queda para el caso que de verdad importa - que alguien
     * cambie el CONTENIDO sin cambiar el puntero, que es lo que hace
     * spectrum.c al construir su tabla de color en RAM-, pero solo se
     * calcula cuando el puntero no coincide... y ademas cada vez que la
     * tabla se reconstruye, porque entonces el puntero si es el mismo.
     *
     * De ahi el contador: la firma se recalcula una de cada N llamadas, lo
     * justo para que un cambio de contenido en el mismo sitio se note en el
     * fotograma siguiente, sin pagarlo nueve veces por fotograma.
     */
    if (s_lut_cargada && (cmap == s_lut_ptr)) {
        if (s_lut_salta < LUT_SALTOS) {
            s_lut_salta++;
            return 1U;
        }
        s_lut_salta = 0U;
    }

    f = firma(cmap);

    if (s_lut_cargada && (f == s_lut_firma) && (cmap == s_lut_ptr)) {
        return 1U;
    }

    for (i = 0U; i < 256U; i++) {
        uint32_t c = cmap[i];
        uint32_t r = (c >> 11) & 0x1FU;
        uint32_t g = (c >>  5) & 0x3FU;
        uint32_t b =  c        & 0x1FU;

        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        s_lut[i] = 0xFF000000UL | (r << 16) | (g << 8) | b;
    }

    /* 255 y no 256: el campo del IPA guarda "numero de entradas menos una",
     * y en 8 bits 256 no cabe. */
    ipa_foreground_lut_init(255U, IPA_LUT_PF_ARGB8888, (uint32_t)&s_lut[0]);
    ipa_flag_clear(IPA_FLAG_LLF);
    ipa_foreground_lut_loading_enable();

    for (vueltas = 0UL; vueltas < IPA_TOPE_VUELTAS; vueltas++) {
        if (ipa_flag_get(IPA_FLAG_LLF) != RESET) {
            ipa_flag_clear(IPA_FLAG_LLF);
            s_lut_firma   = f;
            s_lut_ptr     = cmap;
            s_lut_salta   = 0U;
            s_lut_cargada = 1U;
            return 1U;
        }
    }
    return 0U;
}

uint8_t ipa_blit_l8(const uint8_t *src, uint16_t src_salto,
                    uint16_t *dst, uint16_t dst_salto,
                    uint16_t w, uint16_t h, const uint16_t *cmap)
{
    ipa_foreground_parameter_struct  fg;
    ipa_destination_parameter_struct de;
    uint32_t vueltas;

    if (!s_vale || !s_on || (w == 0U) || (h == 0U)) {
        return 0U;
    }
    /* El IPA no ve el TCM. Se comprueba, no se supone. */
    if (!en_sram(src, (uint32_t)src_salto * h) ||
        !en_sram(dst, (uint32_t)dst_salto * h * 2U) ||
        !en_sram(cmap, 512U)) {
        s_fallos++;
        return 0U;
    }
    if (!lut_pon(cmap)) {
        s_fallos++;
        return 0U;
    }

    ipa_foreground_struct_para_init(&fg);
    fg.foreground_memaddr        = (uint32_t)src;
    fg.foreground_lineoff        = (uint32_t)(src_salto - w);  /* en PIXELES; aqui 1 byte = 1 pixel */
    fg.foreground_pf             = FOREGROUND_PPF_L8;
    fg.foreground_prealpha       = 0xFFU;
    fg.foreground_alpha_algorithm = IPA_FG_ALPHA_MODE_0;       /* la alfa sale de la tabla */
    fg.foreground_prered         = 0U;
    fg.foreground_pregreen       = 0U;
    fg.foreground_preblue        = 0U;
    ipa_foreground_init(&fg);

    ipa_destination_struct_para_init(&de);
    de.destination_memaddr = (uint32_t)dst;
    de.destination_lineoff = (uint32_t)(dst_salto - w);
    de.destination_pf      = IPA_DPF_RGB565;
    de.image_width         = w;
    de.image_height        = h;
    ipa_destination_init(&de);

    ipa_pixel_format_convert_mode_set(IPA_FGTODE_PF_CONVERT);
    ipa_flag_clear(IPA_FLAG_FTF);
    ipa_flag_clear(IPA_FLAG_TAE);
    ipa_flag_clear(IPA_FLAG_WCF);
    ipa_transfer_enable();

    for (vueltas = 0UL; vueltas < IPA_TOPE_VUELTAS; vueltas++) {
        if (ipa_flag_get(IPA_FLAG_FTF) != RESET) {
            ipa_flag_clear(IPA_FLAG_FTF);
            return 1U;
        }
        /* Que se queje de verdad en vez de quedarse esperando: si el IPA
         * dice que la configuracion esta mal o que no pudo leer, esperar
         * los dos millones de vueltas no lo va a arreglar. */
        if ((ipa_flag_get(IPA_FLAG_TAE) != RESET) ||
            (ipa_flag_get(IPA_FLAG_WCF) != RESET)) {
            break;
        }
    }
    ipa_flag_clear(IPA_FLAG_FTF);
    ipa_flag_clear(IPA_FLAG_TAE);
    ipa_flag_clear(IPA_FLAG_WCF);
    s_fallos++;
    return 0U;
}
