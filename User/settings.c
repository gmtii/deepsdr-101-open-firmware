#include "settings.h"
#include "touch.h"
#include "spi_flash.h"
#include "debug_uart.h"
#include "ms5351.h"
#include "backlight.h"  /* backlight_pct - read/applied directly, see settings.h's header comment */
#include "spectrum.h"   /* spectrum_style - read/applied directly, see settings.h's header comment */
#include "ft8_decoder.h" /* el localizador: clave "grid", aplicada aqui mismo */

extern volatile uint32_t g_msticks; /* same free-running ms counter touch.c/touch_calib.c/spi_flash.c already use */

#define CONFIG_FILE_NAME8 "CONFIG  "
#define CONFIG_FILE_EXT3  "CSV"

/* Wait this long after the last settings_mark_dirty() before actually
 * writing - see settings.h's comment. 3s comfortably outlasts a single
 * encoder detent's worth of tuning without feeling laggy if the user
 * powers off shortly after their last change (worst case: that last
 * change is lost, same trade-off every debounced autosave makes). */
#define SETTINGS_SAVE_DEBOUNCE_MS 3000UL

static uint8_t s_dirty = 0U;

/* ---------------------------------------------------------------------
 * CONTADORES DE GUARDADO. 25/09/2026.
 * ---------------------------------------------------------------------
 * El dueno reporta que los ajustes no se le guardan entre arranques. Se
 * reviso todo el camino leyendo -el volumen se puede escribir, el aviso de
 * "hay que guardar" llega, el buffer da de sobra, lo leido se aplica al
 * arrancar- y sobre el papel deberia funcionar. Ya ha pasado hoy dos veces
 * que lo que se lee y lo que hace la radio no coinciden, y las dos veces
 * mandaba la radio.
 *
 * Asi que esto deja de ser una deduccion: los contadores salen en la
 * ventana de informacion. Si marcan cero guardados, el que no corre es el
 * guardado; si marcan guardados y aun asi se pierde, el fallo esta en la
 * lectura del arranque. Un numero y se acabo la discusion. */
static uint16_t s_guardados;   /* terminados bien */
static uint16_t s_fallos;      /* intentados y fallidos */
static uint32_t s_ult_ms;      /* g_msticks del ultimo que salio bien */

void settings_cuentas(uint16_t *ok, uint16_t *mal, uint32_t *ultimo_ms)
{
    if (ok)        { *ok = s_guardados; }
    if (mal)       { *mal = s_fallos; }
    if (ultimo_ms) { *ultimo_ms = s_ult_ms; }
}
static uint32_t s_dirty_since_ms = 0U;

/* Buffer for the CSV being saved via the async path - must stay
 * valid/unchanged for the WHOLE async operation (many poll() calls
 * across many main loop iterations), since
 * spi_flash_async_save_start() only references it, doesn't copy it -
 * see spi_flash.h's comment. A stack-local buffer would be gone the
 * instant settings_poll() returns, so this has to be static. */
/* 23/09/2026: 1024, y hoy 1600 (ver s_async_csv_buf justo debajo; el fichero
 * en si mide CSV_TAMANO = 1536 y el buffer lleva sitio para el cero y para el
 * aviso). El "1024" de esta linea se quedo escrito cuando se subio, y a dos
 * lineas de aqui el mismo comentario ya cuenta la subida a 1536: corregido el
 * 30/09/2026. Y ya no se discute en un comentario: el tamano lo mide
 * tools/csvlen_check.py del simulador a partir de ESTE fichero, y ese banco
 * ya ha ganado su sitio: el 23/09/2026, al anadir el noise blanker, dejo el
 * peor caso en 1021 bytes contra un buffer de 1024 -TRES bytes de holgura,
 * o sea que el siguiente ajuste habria truncado el fichero en silencio-. De
 * ahi 1536. Y build_csv() avisa por UART si
 * alguna vez trunca. Las tres subidas anteriores (256 -> 384 -> 512) fueron
 * cuentas a ojo, y las tres se quedaron cortas a la siguiente clave. */
static uint8_t s_async_csv_buf[1600];
static uint8_t s_async_save_in_progress = 0U;

/* --- manual CSV building/parsing - no sprintf/strtol, same policy as
 * the rest of this project (see e.g. main.c's own itoa comment). --- */

static uint32_t append_str(uint8_t *buf, uint32_t pos, uint32_t buf_size, const char *s)
{
    while ((*s != '\0') && (pos < buf_size)) {
        buf[pos++] = (uint8_t)(*s++);
    }
    return pos;
}

static uint32_t append_u32(uint8_t *buf, uint32_t pos, uint32_t buf_size, uint32_t v)
{
    char tmp[10];
    uint8_t n = 0U;

    if (v == 0U) {
        tmp[n++] = '0';
    } else {
        while ((v > 0U) && (n < 10U)) {
            tmp[n++] = (char)('0' + (v % 10U));
            v /= 10U;
        }
    }
    while ((n > 0U) && (pos < buf_size)) {
        buf[pos++] = (uint8_t)tmp[--n];
    }
    return pos;
}

static uint32_t append_i32(uint8_t *buf, uint32_t pos, uint32_t buf_size, int32_t v)
{
    if (v < 0) {
        if (pos < buf_size) {
            buf[pos++] = '-';
        }
        v = -v;
    }
    return append_u32(buf, pos, buf_size, (uint32_t)v);
}

static const char *mode_to_str(demod_mode_t mode)
{
    switch (mode) {
    case DEMOD_MODE_AM:  return "AM";
    case DEMOD_MODE_USB: return "USB";
    case DEMOD_MODE_LSB: return "LSB";
    case DEMOD_MODE_NFM: return "NFM";
    case DEMOD_MODE_WFM: return "WFM";
    case DEMOD_MODE_SAM: return "SAM"; /* 26/08/2026 fix - was missing, so saving while in SAM silently persisted "AM" instead */
    default:             return "AM"; /* unreachable in practice, but never emit garbage into the file */
    }
}

static const char *audio_bw_to_str(audio_bw_t bw)
{
    switch (bw) {
    case AUDIO_BW_4K0: return "4K0";
    case AUDIO_BW_2K3: return "2K3";
    case AUDIO_BW_1K8: return "1K8";
    default:           return "4K0"; /* unreachable in practice, same policy as mode_to_str() */
    }
}

static const char *spectrum_style_to_str(spectrum_style_t style)
{
    switch (style) {
    case SPECTRUM_STYLE_LINE:    return "LINE";
    case SPECTRUM_STYLE_OUTLINE: return "OUTLINE";
    case SPECTRUM_STYLE_HEATMAP:
    default:                     return "HEATMAP"; /* unreachable in practice, same policy as mode_to_str() */
    }
}

/*
 * 22/09/2026: esto era una copia a mano de la lista de paletas -la sexta de
 * siete- y ahora sale de la tabla de spectrum.c. Una paleta nueva ya no hay
 * que acordarse de anadirla aqui.
 */
static const char *spectrum_palette_to_str(spectrum_palette_t palette)
{
    return spectrum_palette_clave((uint8_t)palette);
}

/* Returns the byte length written (does NOT null-terminate - this is
 * flash file content, not a C string). */
/*
 * LA TABLA. Ver settings.h (settings_extra_id_t) para por que estos van por
 * indice y no con un parametro y una rama cada uno.
 *
 * El orden es EL DEL ENUM, y eso es todo lo que hay que respetar: el
 * _Static_assert de abajo caza que sobren o falten filas, y
 * tools/persist_check.py caza que el orden no se haya cruzado.
 *
 * Las claves nuevas no colisionan con ninguna de las de arriba, y key_is()
 * compara la clave ENTERA (no un prefijo), asi que anadir una no puede
 * robarle las lineas a otra.
 */
static const char *const k_extra_claves[] = {
    "agc_profile",      /* SET_X_AGC        */
    "squelch_db",       /* SET_X_SQL        */
    "nr_strength",      /* SET_X_NR         */
    "nb_level",         /* SET_X_NB         */
    "rf_agc_on",        /* SET_X_RFAGC      */
    "spec_db_min",      /* SET_X_ESCALA_LO  */
    "spec_db_max",      /* SET_X_ESCALA_HI  */
    "spec_autoscale",   /* SET_X_AUTOESC    */
    "spec_zoom",        /* SET_X_ZOOM       */
    "spec_contour",     /* SET_X_CONTORNO   */
    "touch_firmeza",    /* SET_X_TACTIL     */
    "wfm_ifbw",         /* SET_X_IFBW       */
    "rtty_shift_hz",    /* SET_X_RTTY_SHIFT */
    "rtty_baud_idx",    /* SET_X_RTTY_BAUD  */
    "rtty_inverted",    /* SET_X_RTTY_INV   */
    "cw_tone_hz",       /* SET_X_CW_TONO    */
    "cw_wpm_idx",       /* SET_X_CW_WPM     */
    "cw_autotune",      /* SET_X_CW_AUTO    */
    "wf_speed",         /* SET_X_WFVEL      */
    "audio_analyzer",   /* SET_X_ANALIZ     */
    "nco",              /* SET_X_NCO        */
    "rds",              /* SET_X_RDS        */
    "spec_bridge",      /* SET_X_PUENTE     */
    "wefax_lpm_idx",    /* SET_X_WFX_LPM    */
    "auto_notch",       /* SET_X_NOTCH      */
    /* Los dos cortes del filtro, uno por familia de modo. Van empaquetados
     * como lo*10000+hi en vez de en ocho claves: asi CONFIG.CSV sigue
     * teniendo una linea por ajuste y se lee de un vistazo -"300 2700" se
     * ve dentro de 3002700-, que es lo que se pierde al partirlo. */
    "filtro_ssb",       /* SET_X_FIL_SSB    */
    "filtro_am",        /* SET_X_FIL_AM     */
    "filtro_sam",       /* SET_X_FIL_SAM    */
    "filtro_nfm",       /* SET_X_FIL_NFM    */
    "sstv_guardar",     /* SET_X_SSTV_GUARDA */
    "wefax_guardar",    /* SET_X_WFX_GUARDA  */
    "idioma",           /* SET_X_IDIOMA      */
    "encoder_inv",      /* SET_X_ENC_INV     */
    "nr_on"             /* SET_X_NR_ON       */
};
_Static_assert(sizeof(k_extra_claves) / sizeof(k_extra_claves[0]) == (size_t)SET_X_N,
               "k_extra_claves[] y settings_extra_id_t se han desincronizado");
_Static_assert((int)SET_X_N <= 64, "settings_extra_t.presentes es un uint64_t: caben 64 claves");

/*
 * Lo que mide CONFIG.CSV, siempre. Tres bloques de 512 justos. El contenido
 * de verdad anda por los 700 bytes; el resto son lineas en blanco. Ver el
 * relleno al final de build_csv() para por que es fijo.
 *
 * tools/csvlen_check.py comprueba en cada compilacion que el peor caso de
 * todas las claves sigue cabiendo aqui.
 */
#define CSV_TAMANO 1536U

static uint32_t build_csv(uint8_t *buf, uint32_t buf_size,
                           const touch_calibration_t *cal, uint32_t vfo_hz, demod_mode_t mode,
                           uint32_t tune_step_hz, audio_bw_t audio_bw, int16_t volume_db_x2,
                           uint8_t nonwfm_use_48k, int16_t pga_gain_db_x2,
                           uint8_t spectrum_smooth_pct, uint8_t speaker_enabled,
                           uint8_t att_rin_level, uint8_t tema_idx,
                           const settings_extra_t *extra)
{
    uint32_t p = 0U;

    p = append_str(buf, p, buf_size, "key,value\n");
    p = append_str(buf, p, buf_size, "touch_raw_x_min,"); p = append_u32(buf, p, buf_size, cal->raw_x_min); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_raw_x_max,"); p = append_u32(buf, p, buf_size, cal->raw_x_max); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_raw_y_min,"); p = append_u32(buf, p, buf_size, cal->raw_y_min); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_raw_y_max,"); p = append_u32(buf, p, buf_size, cal->raw_y_max); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_swap_xy,");   p = append_u32(buf, p, buf_size, cal->swap_xy);   p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_invert_x,");  p = append_u32(buf, p, buf_size, cal->invert_x);  p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "touch_invert_y,");  p = append_u32(buf, p, buf_size, cal->invert_y);  p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "vfo_hz,");          p = append_u32(buf, p, buf_size, vfo_hz);         p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "mode,");            p = append_str(buf, p, buf_size, mode_to_str(mode)); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "tune_step_hz,");    p = append_u32(buf, p, buf_size, tune_step_hz);   p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "audio_bw,");        p = append_str(buf, p, buf_size, audio_bw_to_str(audio_bw)); p = append_str(buf, p, buf_size, "\n");
    p = append_str(buf, p, buf_size, "volume_db_x2,");    p = append_i32(buf, p, buf_size, volume_db_x2);   p = append_str(buf, p, buf_size, "\n");
    /* AM/USB/LSB/NFM sample rate (01/09/2026) - plain 0/1, same shape
     * as the touch_* boolean flags above (0=96kHz, the firmware
     * default; 1=48kHz). WFM is unaffected either way (always
     * 192kHz) - see s_nonwfm_use_48k's own declaration comment in
     * main.c. */
    p = append_str(buf, p, buf_size, "nonwfm_use_48k,");  p = append_u32(buf, p, buf_size, nonwfm_use_48k); p = append_str(buf, p, buf_size, "\n");
    /* MS5351 crystal reference (26/08/2026) - read directly from
     * ms5351.c, same "not threaded through every settings_poll()/
     * settings_save_now() call site" pattern as touch_get_calibration()
     * just above - see ms5351_get_xtal_hz()'s comment. */
    p = append_str(buf, p, buf_size, "ms5351_xtal_hz,");  p = append_u32(buf, p, buf_size, ms5351_get_xtal_hz()); p = append_str(buf, p, buf_size, "\n");
    /* PGA analog input gain ceiling (07/09/2026) - 0.5dB units, same
     * shape as volume_db_x2, but unsigned (0-95, no cut direction) -
     * see main.c's s_pga_gain_db_x2 declaration comment. main.c-owned,
     * so passed in like volume_db_x2/nonwfm_use_48k rather than read
     * via a getter. */
    p = append_str(buf, p, buf_size, "pga_gain_db_x2,");  p = append_i32(buf, p, buf_size, pga_gain_db_x2); p = append_str(buf, p, buf_size, "\n");
    /* Spectrum temporal smoothing (07/09/2026) - stored as the same
     * 0-95% "history weight" the UI already shows (see main.c's
     * s_spectrum_smooth_alpha comment), not the raw 0.0-0.95 float -
     * the caller converts on both ends so this file never needs to
     * know main.c's float representation. main.c-owned, passed in. */
    p = append_str(buf, p, buf_size, "spectrum_smooth_pct,"); p = append_u32(buf, p, buf_size, spectrum_smooth_pct); p = append_str(buf, p, buf_size, "\n");
    /* Speaker PA enable/mute (07/09/2026) - plain 0/1, same shape as
     * the touch_* / nonwfm_use_48k boolean flags. main.c-owned
     * (s_speaker_pa_enabled), passed in. */
    p = append_str(buf, p, buf_size, "speaker_enabled,"); p = append_u32(buf, p, buf_size, speaker_enabled); p = append_str(buf, p, buf_size, "\n");
    /* Backlight brightness (07/09/2026) - 0-100%, read straight from
     * backlight.c's own getter (backlight_get_percent()), same
     * "no threading through every call site" pattern as
     * ms5351_get_xtal_hz()/touch_get_calibration() above - see
     * settings.h's header comment for why this one is NOT a
     * build_csv() parameter despite main.c owning s_pga_gain_db_x2/
     * s_spectrum_smooth_alpha/s_speaker_pa_enabled above. */
    p = append_str(buf, p, buf_size, "backlight_pct,"); p = append_u32(buf, p, buf_size, backlight_get_percent()); p = append_str(buf, p, buf_size, "\n");
    /* Spectrum trace style (07/09/2026) - HEATMAP/LINE/OUTLINE, read
     * straight from spectrum.c's own getter, same reasoning as
     * backlight_pct just above. */
    p = append_str(buf, p, buf_size, "spectrum_style,"); p = append_str(buf, p, buf_size, spectrum_style_to_str(spectrum_get_style())); p = append_str(buf, p, buf_size, "\n");
    /* Spectrum/waterfall color palette (08/09/2026) - read straight
     * from spectrum.c's own getter, same "no ordering hazard, no
     * threading through call sites" reasoning as spectrum_style just
     * above (spectrum_init() only rebuilds the LUT from whatever
     * palette is ALREADY set - it doesn't reset the palette choice
     * itself - so applying this directly from settings_load(), before
     * spectrum_init() runs, is safe; see settings.h's header comment). */
    p = append_str(buf, p, buf_size, "spectrum_palette,"); p = append_str(buf, p, buf_size, spectrum_palette_to_str(spectrum_get_palette())); p = append_str(buf, p, buf_size, "\n");
    /*
     * El localizador Maidenhead (29/09/2026). Se guarda como TEXTO y no
     * empaquetado en un entero de los de tabla, para que CONFIG.CSV siga
     * leyendose de un vistazo en el ordenador: "grid,IN80dk" dice lo que es
     * y se puede corregir a mano; un numero no.
     *
     * Si el valor que hay puesto no validara, ft8_decoder_get_own_grid()
     * devuelve "" y aqui sale "grid," a secas - que es justo la senal
     * visible de que algo no cuadro, en vez de guardar basura.
     */
    p = append_str(buf, p, buf_size, "grid,"); p = append_str(buf, p, buf_size, ft8_decoder_get_own_grid()); p = append_str(buf, p, buf_size, "\n");
    /* HEATMAP trace white/color-matched toggle (08/09/2026) - read
     * straight from spectrum.c's own getter, same "no ordering
     * hazard, no threading through call sites" reasoning as
     * spectrum_style/spectrum_palette above (spectrum_init() doesn't
     * touch this flag either). */
    p = append_str(buf, p, buf_size, "spec_trace_white,"); p = append_u32(buf, p, buf_size, spectrum_get_heatmap_trace_white()); p = append_str(buf, p, buf_size, "\n");
    /* ATT / front-end input impedance level (08/09/2026) - 0=10k/
     * 1=20k/2=40k (aic3204_rin_t), shared by the manual ATT tile and
     * the RF-level auto-AGC's own Rin escalation - see main.c's
     * s_rf_agc_rin_level declaration comment. main.c-owned, no
     * getter, so passed in like pga_gain_db_x2/spectrum_smooth_pct/
     * speaker_enabled above rather than read directly here. */
    p = append_str(buf, p, buf_size, "att_rin_level,"); p = append_u32(buf, p, buf_size, att_rin_level); p = append_str(buf, p, buf_size, "\n");
    /* tema_idx (22/09/2026): indice en k_temas[] de main.c. Se guarda el
     * INDICE y no el nombre porque el nombre lleva tilde y este fichero
     * es ASCII plano; y porque main.c recorta el indice al cargarlo, asi
     * que un CONFIG.CSV escrito por una version con mas temas no rompe
     * nada, solo cae en el primero. */
    p = append_str(buf, p, buf_size, "tema_idx,"); p = append_u32(buf, p, buf_size, tema_idx); p = append_str(buf, p, buf_size, "\n");

    /* Y los de tabla (23/09/2026), en un bucle. Diecisiete ajustes en cinco
     * lineas: esa es toda la gracia de la tabla. */
    if (extra != 0) {
        uint32_t k;
        for (k = 0U; k < (uint32_t)SET_X_N; k++) {
            p = append_str(buf, p, buf_size, k_extra_claves[k]);
            p = append_str(buf, p, buf_size, ",");
            p = append_i32(buf, p, buf_size, extra->v[k]);
            p = append_str(buf, p, buf_size, "\n");
        }
    }

    /*
     * RELLENO HASTA UN TAMAÑO FIJO. 23/09/2026.
     *
     * El guardado rapido (spi_flash_async_save_start) solo vale si el
     * fichero sigue ocupando los MISMOS bloques de 512 bytes que ya tenia.
     * Como el tamaño depende de lo que valgan los ajustes -"0" ocupa un
     * caracter y "-120" cuatro-, el fichero crecia y encogia solo, y bastaba
     * con que un dia cruzara un multiplo de 512 para que algunos guardados
     * se fueran por el camino lento, que bloquea el bucle principal y se
     * nota como un tiron dibujando el espectro.
     *
     * Se arregla de raiz: el fichero mide SIEMPRE CSV_TAMANO. Se rellena con
     * lineas en blanco, que el lector ya se salta -no llevan coma, y ahi hay
     * un `continue`-. Asi la condicion del guardado rapido es cierta por
     * construccion y deja de depender de cuantos ajustes haya, en vez de ser
     * algo que hay que volver a comprobar cada vez que se añade uno.
     */
    while (p < CSV_TAMANO && p < buf_size - 1U) {
        buf[p++] = (uint8_t)'\n';
    }

    /* Aviso de truncado. append_*() respetan buf_size y cortan en vez de
     * pisar memoria, pero un CONFIG.CSV al que le faltan las ultimas lineas
     * es perdida de datos silenciosa: el buffer se ha subido a mano tres
     * veces con cuentas hechas a ojo en el comentario, y a la cuarta lo que
     * hay es esto, que lo dice en voz alta. El banco sim/csvlen del
     * simulador mide el peor caso de verdad. */
    if (p >= buf_size - 1U) {
        debug_print("settings: *** CONFIG.CSV TRUNCADO - el buffer se ha quedado corto ***\n");
    }
    return p;
}

static uint32_t manual_atou32(const uint8_t *s, uint32_t len)
{
    uint32_t v = 0U;
    uint32_t i;

    for (i = 0U; i < len; i++) {
        if ((s[i] < '0') || (s[i] > '9')) {
            break;
        }
        v = (v * 10U) + (uint32_t)(s[i] - '0');
    }
    return v;
}

/* Same as manual_atou32() but handles a leading '-' - needed for
 * volume_db_x2 (negative most of the time - 0dB is already unity, so
 * anything below that is a negative value in these 0.5dB-native
 * units). */
static int32_t manual_atoi32(const uint8_t *s, uint32_t len)
{
    uint8_t neg = 0U;
    uint32_t i = 0U;

    if ((len > 0U) && (s[0] == '-')) {
        neg = 1U;
        i = 1U;
    }
    {
        int32_t v = (int32_t)manual_atou32(&s[i], len - i);
        return neg ? -v : v;
    }
}

static uint8_t mem_eq(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    uint32_t i;

    for (i = 0U; i < len; i++) {
        if (a[i] != b[i]) {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t key_is(const uint8_t *key, uint32_t key_len, const char *literal)
{
    uint32_t lit_len = 0U;

    while (literal[lit_len] != '\0') {
        lit_len++;
    }
    return ((key_len == lit_len) && mem_eq(key, (const uint8_t *)literal, lit_len)) ? 1U : 0U;
}

uint8_t settings_load(settings_loaded_t *out)
{
    /* Ver s_async_csv_buf, arriba, para por que 1600 y no una cuenta a ojo. */
    uint8_t buf[1600];
    uint32_t n;
    uint32_t pos = 0U;
    uint8_t got_any = 0U;
    touch_calibration_t cal;
    uint8_t have_field[7] = { 0U, 0U, 0U, 0U, 0U, 0U, 0U }; /* only apply the calibration if ALL 7 showed up - see this function's comment */

    out->have_vfo_hz = 0U;
    out->have_mode = 0U;
    out->have_tune_step_hz = 0U;
    out->have_audio_bw = 0U;
    out->have_volume_db_x2 = 0U;
    out->have_nonwfm_use_48k = 0U;
    out->have_pga_gain_db_x2 = 0U;
    out->have_spectrum_smooth_pct = 0U;
    out->have_speaker_enabled = 0U;
    out->have_backlight_pct = 0U;
    out->have_att_rin_level = 0U;
    out->have_tema_idx = 0U;

    n = spi_flash_read_file_by_name(CONFIG_FILE_NAME8, CONFIG_FILE_EXT3, buf, sizeof(buf));
    if (n == 0U) {
        debug_print("settings_load: CONFIG.CSV not found or empty - using firmware defaults\n");
        return 0U;
    }

    while (pos < n) {
        uint32_t line_start = pos;
        uint32_t line_len;
        uint32_t comma;
        uint8_t found_comma = 0U;
        uint32_t k;

        while ((pos < n) && (buf[pos] != '\n')) {
            pos++;
        }
        line_len = pos - line_start;
        if (pos < n) {
            pos++; /* skip the '\n' itself */
        }
        /* 22/09/2026: quitar el '\r' de un fichero con finales de linea de
         * Windows. El firmware escribe solo '\n', pero CONFIG.CSV es un
         * fichero de texto en una tarjeta y se edita desde un PC. Antes daba
         * igual porque los valores se comparaban por prefijo; ahora que
         * spectrum_palette_de_clave() compara la clave ENTERA, un '\r'
         * pegado al final haria que la paleta guardada no se reconociera y
         * volviera a la de por defecto en silencio. */
        if (line_len > 0U && buf[line_start + line_len - 1U] == '\r') {
            line_len--;
        }

        comma = 0U;
        for (k = 0U; k < line_len; k++) {
            if (buf[line_start + k] == ',') {
                comma = k;
                found_comma = 1U;
                break;
            }
        }
        if (!found_comma) {
            continue; /* header line ("key,value") also lands here and gets skipped the same way if it somehow had no comma - harmless either way since "key" isn't a recognized key below */
        }

        {
            const uint8_t *key = &buf[line_start];
            uint32_t key_len = comma;
            const uint8_t *val = &buf[line_start + comma + 1U];
            uint32_t val_len = line_len - comma - 1U;

            if (key_is(key, key_len, "touch_raw_x_min")) { cal.raw_x_min = (uint16_t)manual_atou32(val, val_len); have_field[0] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_raw_x_max")) { cal.raw_x_max = (uint16_t)manual_atou32(val, val_len); have_field[1] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_raw_y_min")) { cal.raw_y_min = (uint16_t)manual_atou32(val, val_len); have_field[2] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_raw_y_max")) { cal.raw_y_max = (uint16_t)manual_atou32(val, val_len); have_field[3] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_swap_xy"))   { cal.swap_xy   = (uint8_t)manual_atou32(val, val_len);  have_field[4] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_invert_x"))  { cal.invert_x  = (uint8_t)manual_atou32(val, val_len);  have_field[5] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "touch_invert_y"))  { cal.invert_y  = (uint8_t)manual_atou32(val, val_len);  have_field[6] = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "vfo_hz")) { out->vfo_hz = manual_atou32(val, val_len); out->have_vfo_hz = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "tune_step_hz")) { out->tune_step_hz = manual_atou32(val, val_len); out->have_tune_step_hz = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "volume_db_x2")) { out->volume_db_x2 = (int16_t)manual_atoi32(val, val_len); out->have_volume_db_x2 = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "nonwfm_use_48k")) { out->nonwfm_use_48k = (uint8_t)manual_atou32(val, val_len); out->have_nonwfm_use_48k = 1U; got_any = 1U; }
            /* pga_gain_db_x2/spectrum_smooth_pct/speaker_enabled
             * (07/09/2026): main.c-owned, so - same as vfo_hz/mode/
             * volume_db_x2/nonwfm_use_48k above - just stashed into
             * *out with their own have_* flag for the CALLER to apply
             * once boot ordering allows (see settings.h's header
             * comment). No range clamping here - that is main.c's job
             * when it applies these, same as it already does for
             * volume_db_x2 (VOLUME_MIN_X2/MAX_X2) rather than settings.c
             * guessing at limits that live in main.c. */
            else if (key_is(key, key_len, "pga_gain_db_x2")) { out->pga_gain_db_x2 = (int16_t)manual_atoi32(val, val_len); out->have_pga_gain_db_x2 = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "spectrum_smooth_pct")) { out->spectrum_smooth_pct = (uint8_t)manual_atou32(val, val_len); out->have_spectrum_smooth_pct = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "speaker_enabled")) { out->speaker_enabled = (uint8_t)manual_atou32(val, val_len); out->have_speaker_enabled = 1U; got_any = 1U; }
            /* backlight_pct (07/09/2026): backlight.c-owned WITH a
             * getter, but still stashed into *out rather than applied
             * directly here - backlight_init() runs AFTER
             * settings_load() in main()'s boot sequence and would
             * just overwrite a direct apply with its own compiled-in
             * default (see settings.h's header comment). No clamping
             * needed on the caller's side either - backlight_set_percent()
             * already clamps both ends itself. */
            else if (key_is(key, key_len, "backlight_pct")) { out->backlight_pct = (uint8_t)manual_atou32(val, val_len); out->have_backlight_pct = 1U; got_any = 1U; }
            /* att_rin_level (08/09/2026): main.c-owned
             * (s_rf_agc_rin_level), no getter, so - same as
             * pga_gain_db_x2/spectrum_smooth_pct/speaker_enabled above
             * - just stashed into *out with its own have_* flag for
             * the caller to apply once the codec is up (needs
             * aic3204_set_input_impedance(), same boot-ordering
             * requirement as pga_gain_db_x2's rf_agc_apply_pga() call
             * - see settings.h's header comment). No clamping here -
             * that's the caller's job, same reasoning as the other
             * three main.c-owned fields. */
            else if (key_is(key, key_len, "att_rin_level")) { out->att_rin_level = (uint8_t)manual_atou32(val, val_len); out->have_att_rin_level = 1U; got_any = 1U; }
            else if (key_is(key, key_len, "tema_idx")) { out->tema_idx = (uint8_t)manual_atou32(val, val_len); out->have_tema_idx = 1U; got_any = 1U; }
            /* spectrum_style (07/09/2026): applied DIRECTLY here, no
             * have_ flag/value pair at all - unlike backlight_pct just
             * above, spectrum_init() (also called after
             * settings_load()) never touches spectrum.c's style state,
             * so there is no ordering hazard to defer around - same
             * reasoning as touch_set_calibration()/ms5351_set_xtal_hz()
             * below. Unrecognized value: silently ignored, leaves
             * whatever spectrum_init()/the s_style initializer already
             * set (SPECTRUM_STYLE_HEATMAP). */
            else if (key_is(key, key_len, "spectrum_style")) {
                if      ((val_len >= 4U) && mem_eq(val, (const uint8_t *)"LINE", 4U))    { spectrum_set_style(SPECTRUM_STYLE_LINE); got_any = 1U; }
                else if ((val_len >= 7U) && mem_eq(val, (const uint8_t *)"OUTLINE", 7U)) { spectrum_set_style(SPECTRUM_STYLE_OUTLINE); got_any = 1U; }
                else if ((val_len >= 7U) && mem_eq(val, (const uint8_t *)"HEATMAP", 7U)) { spectrum_set_style(SPECTRUM_STYLE_HEATMAP); got_any = 1U; }
                /* else: unrecognized value - leave the current style alone */
            }
            /* spectrum_palette (08/09/2026): applied DIRECTLY here,
             * same shape/reasoning as spectrum_style just above - see
             * this file's build_csv() comment for why there's no
             * ordering hazard. Unrecognized value: silently ignored,
             * leaves whatever build_lut()/the s_palette initializer
             * already set (SPECTRUM_PALETTE_CLASSIC). Checked longest-
             * name-first - REQUIRED for CLASSIC_GREEN/CLASSIC (the
             * former genuinely starts with the latter) and
             * TEMPER_COLORS (same length as CLASSIC_GREEN, no actual
             * collision with anything, kept in the same length-order
             * position for consistency). Every other name is checked
             * longest-first too, purely as a defensive habit - none
             * of the rest actually collide. */
            else if (key_is(key, key_len, "spectrum_palette")) {
                /* 22/09/2026: antes eran quince ramas ordenadas de clave
                 * mas larga a mas corta a mano, porque comparaban PREFIJOS
                 * y "CLASSIC" se habria tragado "CLASSIC_GREEN" de haber
                 * ido primero. spectrum_palette_de_clave() compara la clave
                 * entera, asi que el orden ya no puede morder. */
                int16_t i = spectrum_palette_de_clave((const char *)val, val_len);
                if (i >= 0) { spectrum_set_palette((spectrum_palette_t)i); got_any = 1U; }
                /* else: valor desconocido - se deja la paleta que haya */
            }
            /* spec_trace_white (08/09/2026): applied DIRECTLY here,
             * same shape/reasoning as spectrum_style/spectrum_palette
             * above - no ordering hazard. */
            else if (key_is(key, key_len, "grid")) {
                /* Aplicado DIRECTAMENTE aqui, como spectrum_palette: no hay
                 * dependencia de orden con el arranque de main() -el
                 * localizador no toca la radio, solo el aspa de los mapas y
                 * las distancias- asi que no necesita su par have_/valor.
                 *
                 * ft8_decoder_set_own_grid() valida por su cuenta y deja el
                 * localizador VACIO si el valor no es bueno, que es mejor
                 * que una distancia calculada desde un sitio inventado. */
                ft8_decoder_set_own_grid((const char *)val, (int)val_len);
                got_any = 1U;
            }
            else if (key_is(key, key_len, "spec_trace_white")) { spectrum_set_heatmap_trace_white((uint8_t)manual_atou32(val, val_len)); got_any = 1U; }
            else if (key_is(key, key_len, "ms5351_xtal_hz")) {
                /* Applied directly, same as touch_set_calibration()
                 * just below - no ordering dependency on the rest of
                 * main()'s boot sequence (ms5351_init()/
                 * ms5351_tune_captured() don't care what s_xtal_hz is
                 * until the first real ms5351_set_lo_freq() call,
                 * which always happens after settings_load() in
                 * main()). */
                ms5351_set_xtal_hz(manual_atou32(val, val_len));
                got_any = 1U;
            }
            else if (key_is(key, key_len, "mode")) {
                if      ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"USB", 3U)) { out->mode = DEMOD_MODE_USB; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"LSB", 3U)) { out->mode = DEMOD_MODE_LSB; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"NFM", 3U)) { out->mode = DEMOD_MODE_NFM; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"WFM", 3U)) { out->mode = DEMOD_MODE_WFM; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"SAM", 3U)) { out->mode = DEMOD_MODE_SAM; } /* 26/08/2026 fix, see mode_to_str()'s comment */
                else if ((val_len >= 2U) && mem_eq(val, (const uint8_t *)"AM", 2U))  { out->mode = DEMOD_MODE_AM; }
                else { continue; } /* unrecognized value - don't set have_mode, leave the caller's default alone */
                out->have_mode = 1U;
                got_any = 1U;
            }
            else if (key_is(key, key_len, "audio_bw")) {
                if      ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"4K0", 3U)) { out->audio_bw = AUDIO_BW_4K0; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"2K3", 3U)) { out->audio_bw = AUDIO_BW_2K3; }
                else if ((val_len >= 3U) && mem_eq(val, (const uint8_t *)"1K8", 3U)) { out->audio_bw = AUDIO_BW_1K8; }
                else { continue; } /* unrecognized value - leave the caller's default alone */
                out->have_audio_bw = 1U;
                got_any = 1U;
            }
            else {
                /* Los de tabla (23/09/2026): un bucle en vez de diecisiete
                 * ramas. Va EL ULTIMO, detras de todas las claves con nombre
                 * propio, asi que no puede robarle una linea a ninguna. */
                uint32_t k;
                for (k = 0U; k < (uint32_t)SET_X_N; k++) {
                    if (key_is(key, key_len, k_extra_claves[k])) {
                        out->extra.v[k] = manual_atoi32(val, val_len);
                        out->extra.presentes |= ((uint64_t)1U << k);
                        got_any = 1U;
                        break;
                    }
                }
            }
            /* any other unrecognized key: silently ignored - see settings.h's forward-compatibility comment */
        }
    }

    if (have_field[0] && have_field[1] && have_field[2] && have_field[3] && have_field[4] && have_field[5] && have_field[6]) {
        touch_set_calibration(&cal);
        debug_print("settings_load: touch calibration applied from CONFIG.CSV\n");
    } else if (have_field[0] || have_field[1] || have_field[2] || have_field[3] || have_field[4] || have_field[5] || have_field[6]) {
        debug_print("settings_load: CONFIG.CSV has SOME but not all touch_* fields - ignoring calibration entirely rather than applying a half-built one\n");
    }

    debug_print("settings_load: done\n");
    return got_any;
}

/*
 * ¿HAY ALGO QUE HACER AQUI? - 05/10/2026.
 *
 * Existe para que el bucle principal no tenga que montar la estructura de
 * ajustes "de tabla" -treinta y cinco campos, cada uno preguntandole a su
 * modulo- en CADA VUELTA para que settings_poll() la tire en la segunda
 * linea. Ver la llamada en main.c.
 *
 * Devuelve 1 si hay un guardado en vuelo o algo marcado como sucio, o sea
 * exactamente los dos casos en los que settings_poll() hace algo.
 */
uint8_t settings_hay_faena(void)
{
    return (uint8_t)(s_async_save_in_progress || s_dirty);
}

void settings_mark_dirty(void)
{
    s_dirty = 1U;
    s_dirty_since_ms = g_msticks;
}

void settings_save_now(uint32_t vfo_hz, demod_mode_t mode, uint32_t tune_step_hz, audio_bw_t audio_bw, int16_t volume_db_x2, uint8_t nonwfm_use_48k,
                        int16_t pga_gain_db_x2, uint8_t spectrum_smooth_pct, uint8_t speaker_enabled, uint8_t att_rin_level, uint8_t tema_idx,
                        const settings_extra_t *extra)
{
    touch_calibration_t cal;
    uint8_t csv[1600]; /* ver s_async_csv_buf */
    uint32_t len;

    touch_get_calibration(&cal);
    len = build_csv(csv, sizeof(csv), &cal, vfo_hz, mode, tune_step_hz, audio_bw, volume_db_x2, nonwfm_use_48k,
                     pga_gain_db_x2, spectrum_smooth_pct, speaker_enabled, att_rin_level, tema_idx, extra);

    if (spi_flash_write_or_update_file(CONFIG_FILE_NAME8, CONFIG_FILE_EXT3, csv, len)) {
        debug_print("settings_save_now: CONFIG.CSV saved\n");
        s_guardados++; s_ult_ms = g_msticks;
    } else {
        debug_print("settings_save_now: *** CONFIG.CSV save FAILED - see spi_flash error above ***\n");
        s_fallos++;
    }
    s_dirty = 0U;
}

void settings_poll(uint32_t vfo_hz, demod_mode_t mode, uint32_t tune_step_hz, audio_bw_t audio_bw, int16_t volume_db_x2, uint8_t nonwfm_use_48k,
                    int16_t pga_gain_db_x2, uint8_t spectrum_smooth_pct, uint8_t speaker_enabled, uint8_t att_rin_level, uint8_t tema_idx,
                    const settings_extra_t *extra)
{
    if (s_async_save_in_progress) {
        spi_flash_async_status_t st = spi_flash_async_save_poll();

        if (st == SPI_FLASH_ASYNC_DONE) {
            debug_print("settings_poll: async CONFIG.CSV save done\n");
            s_async_save_in_progress = 0U;
            s_guardados++; s_ult_ms = g_msticks;
        } else if (st == SPI_FLASH_ASYNC_ERROR) {
            debug_print("settings_poll: *** async CONFIG.CSV save FAILED - see spi_flash error above ***\n");
            s_async_save_in_progress = 0U;
            s_fallos++;
        }
        /* SPI_FLASH_ASYNC_BUSY: nothing else to do this tick - don't
         * re-check dirty/debounce or start a second save while one is
         * still in flight. */
        return;
    }

    if (!s_dirty) {
        return;
    }
    if ((g_msticks - s_dirty_since_ms) < SETTINGS_SAVE_DEBOUNCE_MS) {
        return;
    }

    /*
     * Non-blocking path - see spi_flash.h's comment on why this
     * matters (a blocking save here would freeze the spectrum/
     * waterfall, which share the main loop with this call, for
     * however long the flash chip's own erase/program circuitry
     * takes - noticed by the project owner 18/08/2026 while tuning).
     * spi_flash_async_save_start() only succeeds if CONFIG.CSV
     * already exists AND the new content needs the exact same
     * cluster count as before - true on essentially every save in
     * practice, since CSV_TAMANO is fixed. (It is 1536 bytes, o sea
     * TRES clusters de 512, no uno: aqui ponia "one 512-byte cluster"
     * hasta el 30/09/2026, y esa frase era justo la razon por la que
     * nadie se preocupaba de los vecinos del bloque de 4 kB - que es
     * el fallo que se arreglo ese dia, ver escribe_datos_fichero() en
     * spi_flash.c.) If it can't (the very first save ever, before
     * CONFIG.CSV exists yet), fall back to the old blocking
     * settings_save_now() just this once - correct either way, just
     * not smooth that one time.
     */
    {
        touch_calibration_t cal;
        uint32_t len;

        touch_get_calibration(&cal);
        len = build_csv(s_async_csv_buf, sizeof(s_async_csv_buf), &cal, vfo_hz, mode, tune_step_hz, audio_bw, volume_db_x2, nonwfm_use_48k,
                         pga_gain_db_x2, spectrum_smooth_pct, speaker_enabled, att_rin_level, tema_idx, extra);

        if (spi_flash_async_save_start(CONFIG_FILE_NAME8, CONFIG_FILE_EXT3, s_async_csv_buf, len)) {
            s_async_save_in_progress = 1U;
            s_dirty = 0U;
        } else {
            debug_print("settings_poll: async fast path unavailable (first save?) - falling back to a blocking save\n");
            settings_save_now(vfo_hz, mode, tune_step_hz, audio_bw, volume_db_x2, nonwfm_use_48k,
                               pga_gain_db_x2, spectrum_smooth_pct, speaker_enabled, att_rin_level, tema_idx, extra);
        }
    }
}
