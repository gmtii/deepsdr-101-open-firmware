#include "gd32f4xx.h"
#include "rm68120_exmc.h"
#include "debug_uart.h"
#include "gfx.h"

/* --- ETAPA 1 DEL PORT A gfx2 ---------------------------------------------
 * El nucleo grafico nuevo entra aqui junto al antiguo, no en su lugar. Los
 * dos conviven: gfx.c sigue pintando todo menos el reloj de la barra
 * superior, que es el unico elemento que pasa a gfx2 en esta etapa.
 *
 * Se elige el reloj porque es lo mas aislado que hay: no tiene interaccion,
 * se repinta solo, y si algo sale mal se revierte cambiando una funcion.
 * El objetivo de la etapa no es que se vea mejor (aunque se vea), sino
 * comprobar EN LA RADIO que gfx2 enlaza, compone y vuelca bien conviviendo
 * con el resto del firmware, y cuanta RAM y flash cuesta de verdad.
 */
#include "gfx2.h"
#include "palette.h"
#include "font_num_20.h"
#include "ui_top.h"   /* ETAPA 2: cabecera y barra de estado */
#include "ui.h"
#include "waterfall.h"
#include "spec_chrome.h"
#include "ui_act.h"
#include "ui_grid.h"
#include "ui_kbd.h"
#include "ui_digi.h"
#include "spec_snap.h"
#include "font_ui_14b.h"
#include "ui_det.h"
#include "ui_cfg.h"
#include "palette.h"
#include "touch.h"
#include "touch_calib.h"
#include "spi_flash.h"
#include "settings.h"
#include "aic3204.h"
#include "config.h"
#include "rtty.h"
#include "cw.h" /* decodificador de CW - ver k_demod_modes[] y cw_poll() */
#include "rtty_scope.h"
#include "ms5351.h"
#include "lo_gen_gd32.h"
#include "rf_lpf.h"
#include "encoder.h"
#include "battery.h"
#include "backlight.h"
#include "arm_math.h" /* arm_fir_decimate_* - spectrum ZOOM, see spec_zoom_t's comment */
#include "demod_am.h"
#include "gd32_i2s.h"
#include "sdr_rx.h"
#include "fft.h"
#include "spectrum.h"
#include "spec_agc.h" /* autoescala del espectro, ver su cabecera */
#include "nr_ss.h" /* NR strength control (RADIO page tile) - see ENCODER_TARGET_NR */
#include "splash_screen.h"
#include "splash_screen.h"

static void led_gpio_init(void);
static void speaker_pa_gpio_init(void);
static void speaker_pa_set_enabled(uint8_t on);
static void systick_delay_init(void);
static void radio_screen_draw(void);
static void sdr_spectrum_waterfall_tick(void);
static void demo_touch_poll(void);
static void freq_display_draw(void);
static void step_display_draw(void);
static void aux_row_display_draw(void);
static void mode_display_draw(void);
static void sam_calib_display_draw(void);
static void time_display_draw(void);
static void battery_display_draw(void);
static void badges_draw(void);
static void smeter_draw(uint8_t segs);
static uint8_t smeter_segments_from_peak(float peak);
static void smeter_dbm_update_and_draw(float peak);

/* spec_zoom_t / s_spec_zoom - moved up from their original spot further
 * down (see spec_zoom_t's own design-comment block, still down there,
 * right before spec_zoom_full_span_hz()) - originally done so the old
 * snr_update_and_draw() (defined earlier in the file than that block)
 * could see s_spec_zoom for its own low-IF center-bin correction; that
 * function was replaced 01/09/2026 by smeter_dbm_update_and_draw()
 * (see its own comment), which doesn't need s_spec_zoom at all - left
 * moved up anyway since spec_zoom_full_span_hz() and others still need
 * it here regardless, and moving it back down would be pure churn for
 * no behavior change. Same SPEC_ZOOM_1X-only
 * condition center_mark_offset_px already uses. No behavior change,
 * just an earlier declaration point for the same single definition. */
typedef enum {
    SPEC_ZOOM_1X = 0,
    SPEC_ZOOM_2X,
    SPEC_ZOOM_4X,
    SPEC_ZOOM_8X
} spec_zoom_t;

static spec_zoom_t s_spec_zoom = SPEC_ZOOM_1X;
static void tune_encoder_poll(void);
static void menu_screen_close(void);
static void screen_sleep_enter(void);
static void screen_wake(void);
static void zoom_decimators_init(void);
static void menu_grid_show(void);
static void menu_bands_show(void);
static void menu_freq_keypad_show(void);
static void menu_time_keypad_show(void);
static void rf_agc_apply_pga(void);
static void rf_agc_mute_for_transition(void); /* forward-declared here too (08/09/2026) - main()'s new att_rin_level boot-apply block needs it before its real definition further down, same reasoning as rf_agc_apply_pga() just above */
/* Forward declarations for main.c-owned CONFIG.CSV fields added
 * 07/09/2026 (PGA/spectrum smoothing/speaker) - their real
 * declarations/definitions sit later in the file, in the same
 * variable-plus-#define blocks as before (s_pga_gain_db_x2 next to
 * PGA_MIN_X2/PGA_MAX_X2, etc.), but main()'s settings-apply block
 * (see settings_load()'s call site) needs to reference them earlier
 * in the file than that. Tentative static declarations here (no
 * initializer) plus identical macro values, merged by the compiler
 * with the real ones further down - same idea as this file's existing
 * forward-declared static FUNCTIONS just above, applied to statics/
 * macros instead. */
static int16_t s_pga_gain_db_x2;
#define PGA_MIN_X2  0   /* 0.0dB - kept identical to the canonical #define next to s_pga_gain_db_x2's real declaration */
#define PGA_MAX_X2  95  /* 47.5dB - see aic3204_set_pga_gain_db()'s field-range note */
static float s_spectrum_smooth_alpha;
#define SPECTRUM_SMOOTH_MIN 0.0f
#define SPECTRUM_SMOOTH_MAX 0.95f
static uint8_t s_speaker_pa_enabled;
/* s_rf_agc_rin_level (ATT/Rin level, 0=10k/1=20k/2=40k) - same
 * tentative-forward-declaration treatment as the three statics just
 * above, for the exact same reason (real declaration sits after
 * main() in the file, at line ~1770, but main()'s settings-apply
 * block needs it earlier - see this block's own header comment).
 * Added 08/09/2026 alongside CONFIG.CSV's new att_rin_level field. */
static uint8_t s_rf_agc_rin_level;
/* s_att_suelo (lo que el usuario ha pedido en el tile ATT, frente a lo que
 * el codec tiene puesto ahora mismo) - mismo tratamiento y misma razon: el
 * bloque de aplicar ajustes de main() lo necesita antes de su declaracion
 * real. Ver ahi el porque de separarlo de s_rf_agc_rin_level. */
static uint8_t s_att_suelo;
/* s_pga_hf_x2: la ganancia de entrada que has elegido TU, frente a la que
 * la radio se pone sola en WFM. Declaracion tentativa aqui por lo mismo que
 * las de al lado - el autoguardado del bucle principal la necesita antes.
 * Ver apply_demod_mode() para el porque. */
static int16_t s_pga_hf_x2;
static uint8_t spectrum_smooth_pct_for_save(void);
static void rf_agc_poll(void);
static void rtty_poll(void);
static void cw_poll(void);
static uint8_t digi_panel_active(void);
static void rtty_scope_panel_reset(void);
static void rtty_text_panel_reset(void);
static void rtty_text_force_redraw(void);
static void rtty_scope_draw(void);
static int64_t tune_mueve_pasos(int64_t f, int32_t pasos, int64_t paso_hz);
static void apply_lo_tune(uint32_t freq_hz);
static void apply_demod_mode(demod_mode_t mode);
static void menu_detail_value_redraw(void);

/* =======================================================================
 * ETAPA 2 DEL PORT: cabecera y barra de estado con gfx2
 * -----------------------------------------------------------------------
 * El dibujo vive en User/ui_top.c, que no depende de nada del firmware
 * salvo gfx2: recibe una estructura con el estado y pinta. Aqui solo se
 * rellena esa estructura desde las variables vivas de la radio.
 *
 * Esa separacion es lo que permite que el simulador de host compile el
 * MISMO ui_top.c con valores inventados y saque un PNG de lo que va a
 * salir en el panel - incluida la comprobacion del peor caso de anchura,
 * que ya pillo que la pastilla del mando se salia por 14 px.
 *
 * DECLARACIONES aqui arriba; la definicion de top_sync() va mas abajo,
 * despues de k_agc_profile_labels y demas estado que necesita leer.
 * ======================================================================= */
static float   s_db_frame[FFT_BINS_IQ]; /* lo ULTIMO dibujado, promediado por cuadro */
static uint8_t s_db_frame_listo = 0U;
static ui_top_state_t s_top;
static char s_top_freq[16];   /* >= FREQ_FIELD_CHARS+1, que se define mas abajo */
static char s_top_knobval[16];
static char s_top_volts[10];
static char s_top_sam[16] = "";
static int16_t s_top_dbm = 0;
static uint8_t s_top_dbm_valid = 0U;
static uint8_t s_top_segs = 0U;
static const char *s_time_text = "--:--";
static char s_time_buf[6] = "--:--";
static char s_top_bw[8];   /* "4,0 k" para el chip de ancho de filtro */

/* Declaradas mas abajo (junto a las etiquetas del menu); top_sync() necesita
 * los Hz aqui arriba y duplicar la tabla seria justo como se consigue que dos
 * sitios de la pantalla digan anchos distintos. */
static const uint32_t k_audio_bw_hz[3];
static const uint32_t k_cw_bw_hz[3];   /* lo mismo, para el filtro de CW */

static void top_sync(void);


static void settings_value_redraw(void);
static void tema_cambiar(void);
static void paleta_cambiar(void);
typedef enum { GRID_NADA = 0, GRID_AJUSTES, GRID_BANDAS, GRID_MODO, GRID_PASOS, GRID_INFO } grid_pant_t;
static void grid_show(grid_pant_t p);
/* Teclado numerico (etapa 19). El tipo y el estado suben aqui porque
 * menu_screen_close() y cfg_show(), que estan muy por delante, tienen que
 * poder apagarlo; el cuerpo vive junto a las funciones de entrada, que es
 * donde se entiende. */
typedef enum { KBD_NADA = 0, KBD_FREQ, KBD_HORA } kbd_modo_t;
static kbd_modo_t s_kbd_modo;
static int8_t     s_kbd_press;
static uint8_t    kbd_activa(void);
static void       kbd_touch(uint16_t x, uint16_t y, uint8_t pressed);
static void       kbd_lectura_draw(void);
static void       kbd_show(kbd_modo_t modo);
/* El indice del tema y la funcion que lo aplica se declaran aqui arriba
 * porque los necesitan dos sitios que estan muy por delante de la tabla:
 * el guardado de ajustes del bucle principal y el arranque. La TABLA
 * sigue junto a g_pal, que es donde se entiende. */
static uint8_t s_tema_idx;
static void tema_aplicar(uint8_t i);

/* Set to 0 to go back to the normal demo once the real panel height is calibrated. */
#define CALIB_HEIGHT_TEST 0
#if CALIB_HEIGHT_TEST
static void calib_height_ruler_draw(void);
#endif

/* Set to 1 to stream raw+calibrated touch coordinates to the debug
 * UART (throttled, ~150ms) while a finger is held down anywhere on
 * screen - see touch_debug_stream_poll()'s comment. Temporary
 * diagnostic for the "screen edges don't respond" report, 18/08/2026 -
 * back to 0 once that's tracked down. */
#define TOUCH_EDGE_DEBUG 1

/* Set to 1 to run spi_flash_probe_dump() once at boot (needs
 * DEBUG_UART_ENABLED=1 to actually see anything - see spi_flash.h's
 * comment). ONE-TIME bring-up step to confirm the external SPI flash
 * chip's identity and whether LBA 0 really holds a FAT boot sector,
 * BEFORE any write/erase support gets added to spi_flash.c - back to
 * 0 for normal use once that's been confirmed on real hardware. */
#define SPI_FLASH_PROBE_TEST 1

/* Set to 1 to override the ADC's M-terminal (negative input) routing
 * to common-mode right after aic3204_phase2_init(), for HFDL bench
 * testing with the RF front-end (QSD) disconnected and a single-ended
 * line-level jack feeding the codec directly instead - see
 * aic3204_set_input_single_ended_test()'s comment in aic3204.h for
 * the full "why" and its own hardware-validation caveat. Back to 0
 * for normal QSD-fed operation - same "opt-in bench diagnostic, off
 * by default" shape as SPI_FLASH_PROBE_TEST just above. */
#define AIC3204_SINGLE_ENDED_TEST 0

/*
 * TUNE_START_HZ moved to config.h (CONFIG_TUNE_START_HZ) 07/08/2026,
 * per the project owner - kept as a local alias so the many
 * TUNE_START_HZ references below don't all need renaming. Still a
 * #define, not just s_tune_hz's initializer, for the same ordering
 * reason as before: main() needs this BEFORE s_tune_hz's own
 * declaration (much further down this file) is visible - see main()'s
 * apply_lo_tune() call right after ms5351_tune_captured() for why.
 */
#define TUNE_START_HZ CONFIG_TUNE_START_HZ

/* Moved up from its old spot alongside s_menu_screen (much further
 * down this file) - 08/08/2026, same reasoning as TUNE_START_HZ just
 * above: main()'s own loop now reads this directly (the RTTY-scope
 * menu-open guard, see main()'s comment there), and C needs the real
 * declaration, not just a prototype, visible before that first use. */
static uint8_t s_menu_open = 0U;

/*
 * Set right before main()'s loop starts (after the boot-time
 * settings_load()/apply_lo_tune()/apply_demod_mode() calls have all
 * run) - apply_lo_tune()/apply_demod_mode() call settings_mark_dirty()
 * at their end ONLY while this is 1, so the boot sequence's own
 * initial calls (which just re-apply whatever settings_load() already
 * loaded, or the firmware defaults if there was nothing to load)
 * don't immediately trigger a pointless "save what we just loaded
 * right back" a few seconds into every single boot - see settings.h's
 * write-cycle-wear comment for why that's worth avoiding, not just
 * wasted time.
 */
static uint8_t s_settings_ready_for_autosave = 0U;

/* Moved up from its old spot alongside the rest of the encoder-tuning
 * state (much further down this file, where its full explanatory
 * comment still lives) - 17/08/2026, same reasoning as s_menu_open
 * above: main()'s boot sequence (settings_load()/apply_lo_tune())
 * now reads/writes this directly before the main loop starts, so the
 * real declaration has to be visible there too, not just at the
 * later apply_lo_tune(s_tune_hz) call sites. */
static uint32_t s_tune_hz = TUNE_START_HZ;

/* Moved up alongside s_tune_hz, same reasoning as it and
 * s_tune_step_idx - main()'s boot sequence needs to apply a loaded
 * volume before entering the main loop (right after the AIC3204 codec
 * is confirmed up, same point mode/step/vfo already get applied - see
 * there). s_volume_db_x2 is kept directly in the hardware's native
 * 0.5dB units (see its own comment further down, still in place) -
 * avoids float rounding drift across repeated encoder adjustments. */
static int16_t s_volume_db_x2 = 0;
#define VOLUME_STEP_X2 2 /* 2 * 0.5dB = 1.0dB per encoder detent */
#define VOLUME_MIN_X2  (-127)  /* -63.5dB */
#define VOLUME_MAX_X2  48      /* +24.0dB */

/* Single choke point for every REAL s_volume_db_x2 change (encoder
 * only, currently) - added 18/08/2026 alongside CONFIG.CSV
 * persistence, same "one place, not one settings_mark_dirty() per
 * call site" reasoning as set_tune_step_idx(). Applies the new value
 * to the codec too (aic3204_set_volume_db()) - the encoder call site
 * already did this itself before, folded in here instead so loading a
 * saved volume at boot can go through the same function. Does NOT
 * redraw anything - callers that show the volume on screen still do
 * that themselves, same as set_tune_step_idx() leaving its own
 * redraws to its callers. */
static void set_volume_db_x2(int16_t v)
{
    s_volume_db_x2 = v;
    aic3204_set_volume_db((float)s_volume_db_x2 * 0.5f);
    if (s_settings_ready_for_autosave) {
        settings_mark_dirty();
    }
}

/* Moved up alongside s_tune_hz, same reasoning - see config.h for
 * why CONFIG_TUNE_START_STEP_IDX is what it is (was BAND_STEP_100K,
 * fine for the old FM-broadcast startup frequency, useless for HF
 * voice tuning - a single click would jump clean past a QSO); changed
 * 07/08/2026 alongside CONFIG_TUNE_START_HZ. */
static uint8_t s_tune_step_idx = CONFIG_TUNE_START_STEP_IDX;

/* Extended 31/07/2026 (100/1K/10K/100K/1M -> 8 steps) to cover the
 * channel spacings real bands actually use, needed for the BANDS
 * presets further down (each preset picks an INDEX into this array,
 * see band_preset_t) - 5K for SW AM broadcast, 12K5/25K for VHF voice
 * channels (2m repeaters, airband). Order matters: the BAND_STEP_*
 * defines further down are literal indices into this array, so
 * inserting/removing/reordering an entry means updating those too. */
static const uint32_t k_tune_steps[] = {
    100UL, 1000UL, 5000UL, 10000UL, 12500UL, 25000UL, 100000UL, 1000000UL
};
#define TUNE_STEP_COUNT (sizeof(k_tune_steps) / sizeof(k_tune_steps[0]))

/* Single choke point for every s_tune_step_idx change (menu tile,
 * step-list picker, encoder-driven BW/step cycling at every band's
 * default-step reset...) - added 18/08/2026 alongside CONFIG.CSV
 * persistence so all of those call sites mark settings dirty through
 * ONE place instead of needing settings_mark_dirty() added at each of
 * the (many) individual assignment sites by hand and risking one
 * getting missed. */
static void set_tune_step_idx(uint8_t idx)
{
    s_tune_step_idx = idx;
    if (s_settings_ready_for_autosave) {
        settings_mark_dirty();
    }
}

/* Filled by settings_load() in main()'s boot sequence - see
 * settings_loaded_t's comment for why this is one struct rather than
 * a scalar per setting. Consumed once, right before entering the main
 * loop (mode/audio_bw need demod_am_init() to have run first, vfo_hz
 * needs to land before the real apply_lo_tune() call - see there for
 * exactly where/why each field gets applied), then not touched again -
 * settings_poll()'s own SAVE path reads the LIVE values instead
 * (s_tune_hz, demod_am_get_mode(), etc.), not this struct. */
static settings_loaded_t s_loaded_settings;

/*
 * Screen SLEEP (HW page's SLEEP tile) - added 10/08/2026, per the
 * project owner, for long unattended listening sessions: kills the
 * backlight and stops every bit of display-side work (spectrum/
 * waterfall redraw, the RTTY scope, touch polling) to save battery
 * and cut the EXMC bus traffic next to the RF front-end - see
 * screen_sleep_enter()'s comment for the full reasoning and main()'s
 * loop for exactly what does/doesn't keep running while this is set.
 * Same "moved up, main() reads it directly" reasoning as s_menu_open
 * just above.
 */
static uint8_t s_screen_asleep = 0U;

volatile uint32_t g_msticks = 0; /* incremented in SysTick_Handler, 1 tick = 1ms real time */
volatile uint16_t g_last_rddpm  = 0; /* last value read from RDDPM (0x0A00) */
volatile uint16_t g_last_rddsdr = 0; /* last value read from RDDSDR (0x0F00) */
volatile uint32_t g_fill_count  = 0; /* how many full fills have been done */
volatile uint16_t g_panel_id_check1 = 0; /* response to panel command 0x000A */
volatile uint16_t g_panel_id_check2 = 0; /* response to panel command 0x3A00 */
volatile uint32_t g_system_clock_snapshot = 0; /* copy of SystemCoreClock, to verify clock startup */

/*
 * s_nonwfm_use_48k / s_current_rate: added 01/09/2026, per the project
 * owner - a real birdie was confirmed tied exactly to N*Fs (287*96kHz
 * = 27.552MHz, landing right in the 11m/CB band, present in a
 * modified AND an unmodified board alike - ruled out as a power-rail
 * issue), giving 48kHz (this project's OWN original AM/USB/LSB/NFM
 * rate, before the later move to 96kHz for wider RF coverage) as a
 * user-selectable escape hatch: moving Fs relocates the same class of
 * birdie to a different, hopefully less troublesome spot. See
 * aic3204_configure_rate()'s DAC-side comment in aic3204.c for the
 * restored 48kHz register values (NDAC=2/MDAC=7/DOSR=128 and
 * NADC=1/MADC=28/AOSR=64, both giving exactly 48000Hz from the same
 * fixed 86.016MHz CODEC_CLKIN - verified numerically, matching the
 * project's own historical 48kHz values before they were overwritten
 * by the 96kHz migration).
 *
 * *** 01/09/2026: now PERSISTED to CONFIG.CSV *** - per the project
 * owner, once 48kHz settled into being an everyday preference rather
 * than just a bench A/B toggle against the birdie above (see
 * settings.h's own schema comment and settings.c's "nonwfm_use_48k"
 * key). Applied at boot from s_loaded_settings, right before the
 * loaded mode is applied (see main()'s own boot sequence) so
 * apply_demod_mode()'s rate-selection logic already sees the right
 * value on its very first real call - same ordering main() already
 * uses for every other loaded setting with a dependency like this.
 *
 * s_current_rate tracks whatever rate is ACTUALLY configured on the
 * codec right now (starts matching cold boot's own default, 96K - see
 * main()'s own boot sequence below) - apply_demod_mode() compares the
 * newly-DESIRED rate against this (not just was_wfm!=will_be_wfm) so
 * tapping the RATE tile while already in a non-WFM mode still forces
 * the real reconfigure, which a was_wfm/will_be_wfm-only check would
 * have missed (both stay "non-WFM" across a 48K<->96K change with no
 * mode change at all). Declared here (not next to apply_demod_mode())
 * since main()'s own boot sequence, further down, also needs to read
 * s_nonwfm_use_48k before apply_demod_mode() is ever even declared in
 * this file's own top-to-bottom order.
 */
static uint8_t s_nonwfm_use_48k = 0U;
static aic3204_rate_t s_current_rate = AIC3204_RATE_96K;

/*
 * demod_if_offset_hz() - added 01/09/2026 alongside the 48kHz rate
 * option. Returns whichever of DEMOD_IF_OFFSET_HZ (24000, @ 96kHz) or
 * DEMOD_IF_OFFSET_HZ_48K (12000, @ 48kHz) matches s_nonwfm_use_48k -
 * demod_am.c's Fs/4 down-mix rotation structurally shifts by exactly
 * Fs/4, whatever Fs actually is (see demod_am.h's own comment on why
 * it's not a generic NCO), so this offset MUST always track the
 * REAL active rate or the LO ends up mistuned relative to what the
 * down-mix actually does - every call site that used to read
 * DEMOD_IF_OFFSET_HZ directly (a single, rate-blind constant) now
 * calls this instead. WFM is unaffected either way - it never applies
 * this offset at all (see is_wfm checks at each call site).
 */
static uint32_t demod_if_offset_hz(void)
{
    return s_nonwfm_use_48k ? DEMOD_IF_OFFSET_HZ_48K : DEMOD_IF_OFFSET_HZ;
}

int main(void)
{
    /* Critical when chained after a bootloader: our vector table is no
     * longer at 0x08000000 (that's the bootloader's), but at
     * 0x08020000. Without this, any interrupt (including our own
     * SysTick) would look up its handler in the BOOTLOADER's vector
     * table, not ours - this must be the FIRST thing we do. */
    SCB->VTOR = 0x08020000;

    /*
     * Clear the NVIC state inherited from the bootloader BEFORE
     * re-enabling global interrupts. The bootloader may have left some
     * of its own interrupts enabled at the NVIC level (its own
     * SysTick, some DMA, etc.) which, with VTOR already pointing at
     * OUR vector table (unused entries = Default_Handler, a silent
     * infinite loop), would jump into a silent hang the moment they
     * got unmasked. Disabling everything and clearing pending flags
     * gives a clean slate; our own code then selectively re-enables
     * only what it configures (SysTick_Config below, EXTI2 later in
     * touch_init()).
     */
    {
        uint8_t i;
        for (i = 0; i < 8U; i++) {
            NVIC->ICER[i] = 0xFFFFFFFFU;
            NVIC->ICPR[i] = 0xFFFFFFFFU;
        }
    }

    /*
     * Also critical when chained after a bootloader: it's common for a
     * bootloader to leave interrupts globally disabled (PRIMASK)
     * before jumping to the application, to avoid handing off IRQ
     * state half-configured. Without this __enable_irq() here, NO
     * interrupt (SysTick included, touch.c's EXTI later on) would ever
     * fire, even with fully correct NVIC/EXTI configuration.
     */
    __enable_irq();

    /*
     * SystemInit() has already been called from the startup code
     * before main(): it configures PLL/HXTAL per system_gd32f4xx.c.
     * See that file for the active clock configuration.
     *
     * HXTAL_VALUE is set correctly via -D in the Makefile, matching
     * this board's real 12.288MHz crystal. SystemCoreClockUpdate()
     * re-reads the live PLL registers and recalculates
     * SystemCoreClock using that value - without calling it,
     * SystemCoreClock stays at the incorrect literal the vendor
     * startup file initializes it to.
     */
    SystemCoreClockUpdate();

    systick_delay_init();
    led_gpio_init();
    debug_uart_init();

    debug_print("\n\n=== STARTUP (chained after bootloader) ===\n");
    debug_print_hex32("VTOR read back", SCB->VTOR);

    /* Snapshot of SystemCoreClock as early as possible, to verify via
     * debugger whether the clock came up at the expected frequency or
     * whether the HXTAL crystal failed and silently settled elsewhere. */
    g_system_clock_snapshot = SystemCoreClock;
    debug_print_hex32("SystemCoreClock", g_system_clock_snapshot);

    debug_print_hex32("RCU_PLLI2S as early as possible in main()", RCU_PLLI2S);

    /*
     * REMOVED (30/07/2026): rm68120_init() itself - specifically its
     * rm68120_hw_reset() + fresh panel_init_sequence() - turned out to
     * be exactly what painted the panel red on boot, not a missing
     * clear. The bootloader already brings the panel up correctly
     * before handing off to this firmware (EXMC bus config included),
     * so re-running our own init just interrupts that known-good
     * state. Now relying entirely on the bootloader's init: no
     * rm68120_init() call here, and no gfx_fill_screen() either - the
     * panel is already black from the bootloader by the time we get
     * here, so there's nothing to blank.
     */
    debug_print_hex32("RCU_PLLI2S (rm68120_init skipped, using bootloader's panel init)",
                       RCU_PLLI2S);

    waterfall_init();
    touch_init();
    debug_print_hex32("RCU_PLLI2S after touch_init", RCU_PLLI2S);

    spi_flash_init(); /* unconditional - settings_load()/settings_poll() need this every boot, not just under the SPI_FLASH_PROBE_TEST diagnostics below */

#if SPI_FLASH_PROBE_TEST
    /* Bring-up ONLY - see SPI_FLASH_PROBE_TEST's and
     * spi_flash_probe_dump()'s comments. Read-only up through
     * spi_flash_probe_fat_scan(); spi_flash_probe_write_selftest()
     * writes a throwaway pattern into a confirmed-free scratch block
     * to prove the erase+program path before anything real depends on
     * it - already done and confirmed on real hardware, 17/08/2026
     * (Winbond W25Q16, genuine FAT12, same volume the bootloader's
     * USB-MSC mode uses for update4.bin). Kept here, gated off by
     * default, purely as a re-runnable diagnostic if the flash chip
     * or filesystem is ever in question again.
     */
    spi_flash_probe_dump();
    spi_flash_probe_root_dir();
    {
        spi_flash_fat_scan_t fat_scan;
        spi_flash_probe_fat_scan(&fat_scan);
        spi_flash_probe_write_selftest(&fat_scan);
    }
#endif

    /*
     * Real settings load - see settings.h's comment for the schema and
     * settings_load()'s comment for exactly what it does/doesn't
     * apply directly. Touch calibration gets applied right here
     * (touch_set_calibration() has no other side effects to sequence
     * around). Everything else is only STORED in s_loaded_settings
     * here - actually applying vfo_hz/mode/tune_step_hz/audio_bw needs
     * to wait for demod_am_init() (mode/audio_bw) and the real LO tune
     * (further down, see the apply_demod_mode()/apply_lo_tune() calls
     * right before radio_screen_draw()) rather than reaching ahead of
     * this project's own established boot ordering.
     */
    (void)settings_load(&s_loaded_settings);

    /*
     * *** 01/09/2026, rate-aware cold boot TRIED then REVERTED same
     * day *** - an attempt to boot straight into AIC3204_RATE_192K
     * when the saved mode was WFM (avoiding a cold-96K-then-warm-192K
     * double reset, on the theory that a warm nRESET might not
     * resettle some analog bias/reference circuit the way a true
     * power-on does) made WFM audio sound "robotizado" on a cold boot
     * - confirmed reproducible, and NOT fixed by also arming WFM's
     * settle-mute (demod_wfm_reset_diag()) the way a live switch does.
     * Two independent fixes failing to resolve a newly-introduced,
     * clearly audible regression means the "double reset" theory (or
     * at least this project's understanding of what's actually
     * different about a genuine cold 192K bring-up vs this codebase's
     * OTHER boot machinery) isn't solid enough to keep chasing blind,
     * without live hardware access to instrument it further. Reverted
     * to the plain, always-96K-first cold boot below, unconditionally
     * - restoring the exact behavior this project had before today's
     * attempt, which is known-good: WFM is only ever entered via
     * apply_demod_mode()'s live-switch path, thoroughly validated on
     * real hardware (see that function's own comment) and confirmed
     * clean by the project owner. The WFM sensitivity investigation
     * itself (needing more PGA gain than before) remains open - see
     * gd32f450-sdr-firmware notes - but chasing it via the cold-boot
     * rate is set aside for now in favor of not trading a mild
     * sensitivity issue for an audible audio corruption bug.
     */
    debug_print("\n--- AIC3204: phase 1 (I2C communication only) ---\n");
    aic3204_init(AIC3204_ADDR_DEFAULT);
    if (!aic3204_probe_and_reset()) {
        debug_print("aic3204: trying 0x19 (MODE pin tied to VDD)...\n");
        aic3204_init(0x19);
        if (!aic3204_probe_and_reset()) {
            aic3204_scan_bus();
        }
    }
    debug_print_hex32("RCU_PLLI2S after aic3204", RCU_PLLI2S);

    /*
     * MS5351 base configuration (PLLA 832MHz, 8pF load, CLK2 8MHz
     * prepared but off, OE = CLK0|CLK1). In the original firmware's
     * I2C capture this is the very FIRST traffic on the bus, before
     * any codec write; here it runs right after the codec probe only
     * because i2c_bitbang_init() lives inside aic3204_init(). The two
     * chips are independent, so the relative order between them does
     * not matter - what does match the capture is base-init BEFORE
     * codec config and the quadrature tune AFTER it (see below).
     */
    rf_lpf_init(); /* front-end LPF GPIOs ready before the first tune */
    encoder_init(); /* tuning knob: PD13(A)/PD12(B)/PC9(button, active high) */
    battery_init(); /* VBAT via ADC0 channel 18, see battery.h - independent
                      * of everything else in this sequence, so it just
                      * needs to run before the first battery_display_draw() */
    backlight_init(); /* PWM brightness on PA3 (TIMER1_CH3), see backlight.h -
                        * likewise independent; run before the panel is first
                        * drawn so it's never dark/at some undefined duty
                        * cycle for even one frame. */
    speaker_pa_gpio_init(); /* speaker PA enable/mute, see its own comment -
                              * independent GPIO, same reasoning as the other
                              * inits in this block; run before audio starts
                              * flowing so the speaker is never briefly at an
                              * undefined level for even one frame. */

    /* PA6/PA7 explicit Hi-Z (21/08/2026) - CLK0/CLK1 from the MS5351
     * route through these pins via 100-ohm series resistors (found by
     * inspection of the real board, not from any schematic on file).
     * Was never configured anywhere in this codebase before that date
     * - left at the GD32F4's power-on-reset default (floating input),
     * which SHOULD already be high-impedance, but doing it explicitly
     * here removes any doubt (no other init code accidentally claims
     * these pins for something else later, and it documents the
     * intent). Run before ms5351_init() so the LO never sees anything
     * other than Hi-Z on these lines from the moment it starts up.
     *
     * *** 01/09/2026: these same two pins are now ALSO the GD32-
     * generated quadrature LO for the lowest tuning range - see
     * lo_gen_gd32.h's big comment for why. This boot-time Hi-Z is
     * still the correct starting state either way (whichever source
     * ends up driving the net gets decided per-tune, the first time
     * apply_lo_tune() runs, by s_lo_source_is_gd32's sentinel) - only
     * now there's a second piece of code (lo_gen_gd32_set_freq()/
     * lo_gen_gd32_stop()) that also reconfigures these same two pins
     * later, switching them between this Hi-Z input mode and TIMER2
     * AF2 output mode as tuning crosses LO_GEN_CROSSOVER_HZ. */
    rcu_periph_clock_enable(RCU_GPIOA);
    gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO_PIN_6 | GPIO_PIN_7);

    ms5351_init();
    lo_gen_gd32_init(); /* GPIOA/TIMER2 clock enable only - PA6/PA7 stay
                          * in the Hi-Z input mode just set above until
                          * the first low-band tune actually needs them -
                          * see lo_gen_gd32.h's comment. */

    /*
     * *** 01/09/2026, full history: disabled, reverted, then properly
     * resolved same day *** - MCLK was briefly disabled entirely as a
     * real-hardware experiment (per the project owner) to free TIMER2
     * for lo_gen_gd32.c's PA6/PA7 quadrature generator, on the theory
     * that aic3204.c's own (then-uncorrected) comment made it sound
     * vestigial. That caused a real regression (frequencies off,
     * degraded WFM reception) and was reverted - MCLK is genuinely
     * required (see aic3204.c's corrected clock-chain comment: the
     * codec's PLL takes MCLK, not BCLK, as its reference - the
     * project owner's own objection that a BCLK-sourced PLL made no
     * sense for an I2S-master codec is what prompted re-decoding the
     * real register bits and catching this).
     *
     * With MCLK confirmed genuinely necessary, the real fix was
     * finding it a DIFFERENT timer rather than removing it: the
     * project owner's own real datasheet pinout table showed PC6 also
     * has TIMER7_CH0 available (alongside the TIMER2_CH0 this
     * function used before) - gd32_i2s_mclk_timer_start() (gd32_i2s.c)
     * now uses that instead, freeing TIMER2 for real. See that
     * function's own comment for the exact frequency math and its AF
     * number (AF3, confirmed 01/09/2026 against the real datasheet's
     * Port C AF table, same source as the pin mapping itself).
     */
    debug_print("\n--- MCLK: TIMER7_CH0/PC6, 1.536MHz ---\n");
    gd32_i2s_mclk_timer_start();

    debug_print("\n--- I2S1: phase 3 (clocks + circular DMA, test tone) ---\n");
    /* Cold boot always starts at s_current_rate's own static-initializer
     * value (AIC3204_RATE_96K) - matches s_nonwfm_use_48k's own
     * un-persisted default (0/96K, see its declaration comment) by
     * construction, not by coincidence: neither is ever loaded from
     * settings, so both simply start at the values written here in
     * the source. */
    gd32_i2s_init_slave(s_current_rate);

    /*
     * REORDERED (28/07/2026): sdr_rx_init() moved to run IMMEDIATELY
     * after gd32_i2s_init_slave_192k() enables I2S1_ADD's receive
     * path, instead of after aic3204_phase2_init(). The bit-banged I2C
     * codec configuration takes a non-trivial amount of time (many
     * register writes), and with real bits already arriving on PB14
     * every ~10us the moment I2S1_ADD is enabled, that whole gap was
     * long enough for SPI_STAT_RXORERR (receive overrun) to fire
     * continuously with nothing servicing it - by the time DMA was
     * finally armed, the overrun was mid-storm and never actually
     * cleared, which is a strong candidate for why captured samples
     * stayed pinned at a fixed value. Arming DMA first minimizes that
     * unserviced gap to just the few lines of DMA setup itself.
     */
    fft_init();
    spectrum_init(); /* palette LUT for spectrum + waterfall */
    zoom_decimators_init(); /* spectrum ZOOM cascaded decimators - see spec_zoom_t's comment */
    sdr_rx_init();

    debug_print("\n--- AIC3204: phase 2 (clock + single-ended ADC baseline + power-up) ---\n");
    aic3204_phase2_init(s_current_rate);

#if AIC3204_SINGLE_ENDED_TEST
    /* Bench test ONLY - see AIC3204_SINGLE_ENDED_TEST's and
     * aic3204_set_input_single_ended_test()'s comments. Must run
     * AFTER aic3204_phase2_init() (which sets up the normal
     * differential baseline this call then partially overrides), and
     * before anything starts actually reading real audio through the
     * ADC. */
    aic3204_set_input_single_ended_test();
#endif

    /*
     * Audio out: switch DMA0/CH4 from the bring-up test tone to the
     * ping-pong TX stream, then register the AM demodulator as the
     * per-block RX hook (runs in the RX DMA interrupt - see
     * demod_am.h for why it cannot live in this loop). Order matters:
     * the stream transport must exist before the hook can write into
     * it. WFM is reached only via apply_demod_mode()'s live-switch
     * path further down (if s_loaded_settings.mode is WFM) - see this
     * block's own header comment for why a rate-aware cold boot was
     * tried and reverted.
     */
    debug_print("\n--- Audio: TX stream + AM demodulator hook ---\n");
    gd32_i2s_dma_start_stream();
    demod_am_init();
    sdr_rx_set_block_hook(demod_am_process_raw);

    /*
     * Quadrature LO for the QSD, same point in the sequence as the
     * capture (right after the codec is fully configured). For
     * bring-up we replay the hardware-proven captured bytes
     * (90.800MHz, FM broadcast - whatever station is around should
     * show up in the spectrum, which makes for a great smoke test).
     * Once validated, switch to ms5351_set_lo_freq(hz): calling it
     * with MS5351_CAPTURED_LO_HZ emits these exact same bytes.
     *
     * (A low-IF offset scheme - LO tuned below the selected station
     * plus a digital down-mix in demod_am.c, to move the demodulated
     * signal off the QSD's DC-centered LO-leakage artifact - was
     * tried and fully reverted on 30/07/2026: it caused a broadband
     * noise floor / jumping spectrum on hardware. The person
     * confirmed the I2S signals and the MS5351's LO were both fine on
     * their own, so the actual cause wasn't the LO retuning itself
     * and is still unidentified - see demod_am.h for the full note if
     * picking this back up.)
     */
    debug_print("\n--- MS5351: quadrature LO tune ---\n");
    ms5351_tune_captured();

    /*
     * ms5351_tune_captured() above ALWAYS replays the hardware-proven
     * 90.8MHz capture, unconditionally - that's the whole point of a
     * byte-exact smoke test, and it stays that way. But it means the
     * LO is now sitting at 90.8MHz regardless of what s_tune_hz
     * actually starts at (7.150.000MHz as of 07/08/2026 - see its
     * declaration comment) - the two used to always match by
     * construction (s_tune_hz's initializer WAS
     * MS5351_CAPTURED_LO_HZ), but now that they're different values,
     * something has to actually move the LO the rest of the way.
     * That's this call: the normal computed-tuning path
     * (ms5351_set_lo_freq(), same as every retune from the encoder/
     * BANDS/keypad), run once here so the radio boots up actually
     * listening where the frequency readout says it is, not just
     * displaying the right number over a still-90.8MHz LO.
     *
     * Uses s_tune_hz (not the TUNE_START_HZ macro directly) as of
     * 17/08/2026, so a VFO frequency loaded from CONFIG.CSV by
     * settings_load() further up actually takes effect - s_tune_hz's
     * own initializer IS TUNE_START_HZ, so this is unchanged when
     * there's nothing to load (first boot, no CONFIG.CSV yet).
     * Demod mode/audio_bw/step go first (same order every other retune
     * call site in this file already uses - mode then frequency),
     * applied only for whichever fields settings_load() actually
     * found (see s_loaded_settings' comment) - tune_step_hz needs an
     * extra step, looking up which k_tune_steps[] INDEX matches the
     * loaded Hz value (by value, not a raw saved index - see
     * settings.h's comment on why), falling back to leaving
     * s_tune_step_idx at its firmware default if no exact match is
     * found (e.g. a CONFIG.CSV saved by a build with a different step
     * table).
     */
    if (s_loaded_settings.have_nonwfm_use_48k) {
        s_nonwfm_use_48k = s_loaded_settings.nonwfm_use_48k;
    }
    if (s_loaded_settings.have_mode) {
        apply_demod_mode(s_loaded_settings.mode);
    }
    if (s_loaded_settings.have_audio_bw) {
        demod_am_set_audio_bw(s_loaded_settings.audio_bw);
        /* El mismo ajuste guardado vale para los dos caminos: el filtro de
         * audio de AM/BLU y el de CW. Sin esto, al encender la radio en CW
         * el filtro arrancaba en su valor por defecto y no en el que
         * dejaste puesto. */
        demod_am_set_cw_bw_hz((float)k_cw_bw_hz[(uint8_t)s_loaded_settings.audio_bw]);
    }
    if (s_loaded_settings.have_volume_db_x2) {
        int32_t v = s_loaded_settings.volume_db_x2;

        if (v < VOLUME_MIN_X2) { v = VOLUME_MIN_X2; }
        if (v > VOLUME_MAX_X2) { v = VOLUME_MAX_X2; }
        set_volume_db_x2((int16_t)v); /* codec already up by this point in the boot sequence - see AIC3204 phase 1/2 further up - safe to apply here */
    }
    /* PGA/spectrum smoothing/speaker/backlight (07/09/2026) - same
     * "wait for the caller, don't apply straight from settings_load()"
     * shape as volume_db_x2 just above, for the reasons in settings.h's
     * header comment. This point in main()'s boot sequence is already
     * past both backlight_init() and speaker_pa_gpio_init() (see their
     * own call sites further up), and the codec is already up (same
     * "safe to apply here" reasoning as volume_db_x2), so all four are
     * safe to apply right here. */
    if (s_loaded_settings.have_pga_gain_db_x2) {
        int32_t v = s_loaded_settings.pga_gain_db_x2;

        if (v < PGA_MIN_X2) { v = PGA_MIN_X2; }
        if (v > PGA_MAX_X2) { v = PGA_MAX_X2; }
        s_pga_gain_db_x2 = (int16_t)v;
        rf_agc_apply_pga(); /* the only allowed call site for aic3204_set_pga_gain_db() - see its own comment */
    }
    if (s_loaded_settings.have_spectrum_smooth_pct) {
        float alpha = (float)s_loaded_settings.spectrum_smooth_pct * 0.01f; /* inverse of spectrum_smooth_pct_for_save()'s rounding */

        if (alpha < SPECTRUM_SMOOTH_MIN) { alpha = SPECTRUM_SMOOTH_MIN; }
        if (alpha > SPECTRUM_SMOOTH_MAX) { alpha = SPECTRUM_SMOOTH_MAX; }
        s_spectrum_smooth_alpha = alpha;
    }
    if (s_loaded_settings.have_speaker_enabled) {
        speaker_pa_set_enabled(s_loaded_settings.speaker_enabled);
    }
    if (s_loaded_settings.have_backlight_pct) {
        backlight_set_percent(s_loaded_settings.backlight_pct); /* clamps both ends itself, see backlight.h's comment */
    }
    /* Tema: se aplica ANTES de que se pinte nada, para que el arranque ya
     * salga con los colores buenos en vez de pintarse oscuro y cambiar. */
    if (s_loaded_settings.have_tema_idx) {
        tema_aplicar(s_loaded_settings.tema_idx);
    }
    if (s_loaded_settings.have_att_rin_level) {
        uint8_t v = s_loaded_settings.att_rin_level;

        if (v > (uint8_t)AIC3204_RIN_40K) { v = (uint8_t)AIC3204_RIN_40K; }
        s_att_suelo = v;          /* lo guardado es lo que pidio el usuario */
        s_rf_agc_rin_level = v;   /* y el codec arranca justo ahi */
        aic3204_set_input_impedance((aic3204_rin_t)s_rf_agc_rin_level); /* same call the manual ATT tile and rf_agc_escalate_rin()/deescalate_rin() make */
        rf_agc_mute_for_transition(); /* Rin isn't soft-stepped, unlike PGA - see this function's own comment; harmless/no-op this early in boot, kept for consistency with every other Rin-changing call site */
    }
    if (s_loaded_settings.have_tune_step_hz) {
        uint32_t i;
        for (i = 0U; i < TUNE_STEP_COUNT; i++) {
            if (k_tune_steps[i] == s_loaded_settings.tune_step_hz) {
                s_tune_step_idx = (uint8_t)i; /* direct assignment, not set_tune_step_idx() - this is the boot-time LOAD applying what was saved, not a new user change to save again */
                break;
            }
        }
    }
    if (s_loaded_settings.have_vfo_hz) {
        s_tune_hz = s_loaded_settings.vfo_hz;
    }
    apply_lo_tune(s_tune_hz);

#if CALIB_HEIGHT_TEST
    calib_height_ruler_draw();
#else
    splash_screen_draw(); /* personalizable splash screen, see splash_screen.c */
    radio_screen_draw(); /* full radio UI, all readouts included */
#endif

    /* From here on, apply_lo_tune()/apply_demod_mode() calls mean a
     * REAL user-driven change (encoder, BANDS, keypad, mode menu) -
     * see s_settings_ready_for_autosave's own comment for why this
     * has to wait until after the two boot-time calls just above. */
    s_settings_ready_for_autosave = 1U;

    debug_print("main: entering the main loop\n");

    while (1) {
#if !CALIB_HEIGHT_TEST
        if (s_screen_asleep) {
            /*
             * Screen asleep (see screen_sleep_enter()'s and
             * s_screen_asleep's comments) - deliberately does NOT call
             * rtty_scope_poll()/rtty_scope_draw()/sdr_spectrum_
             * waterfall_tick()/demo_touch_poll()/tune_encoder_poll()
             * at all while this is set: that's exactly the EXMC
             * display traffic and touch polling this mode exists to
             * stop. rf_agc_poll()/rtty_poll() below still run every
             * pass regardless (neither touches the display - see
             * their own comments), same as the radio's actual
             * demodulation/audio, which runs straight off the DMA ISR
             * and was never routed through this loop to begin with -
             * listening continues uninterrupted with the screen dark.
             *
             * Rotation and long-press are explicitly DISCARDED (read
             * and thrown away, not left to accumulate) rather than
             * simply not read - encoder_tick() keeps sampling at 1kHz
             * from SysTick regardless of what this loop does, so an
             * un-drained rotation would otherwise pile up sub-detent
             * counts while asleep and suddenly apply as one big jump
             * the instant tune_encoder_poll() resumes after waking.
             * Only a SHORT press (encoder_take_press()) wakes - a
             * long press does nothing here (not even the button's
             * usual "reset from wherever it was" trick, since a
             * long-press-while-asleep has no accumulated menu/detail
             * state to reset in the first place).
             */
            (void)encoder_take_delta();
            (void)encoder_take_long_press();
            if (encoder_take_press()) {
                screen_wake();
            }
        } else if (touch_calib_active()) {
            /*
             * Touch calibration wizard (HW page's CAL tile, see
             * menu_tile_cal_callback()/touch_calib_done_callback())
             * owns the WHOLE screen and touch input while active -
             * same "skip everything display/touch-related" reasoning
             * as s_screen_asleep just above, except the radio itself
             * keeps running exactly the same way (DMA-driven, never
             * routed through this loop - see s_screen_asleep's
             * comment). Rotation is discarded same as while asleep;
             * a SHORT press cancels (touch_calib_cancel() leaves
             * whatever calibration was active before untouched, then
             * this repaints the radio screen the wizard drew over -
             * same "wizard doesn't know what it interrupted" reasoning
             * as touch_calib_done_callback() needing to do the same
             * thing on a successful finish, not just here). A long
             * press does nothing, matching screen_wake()'s treatment
             * of it while asleep.
             */
            (void)encoder_take_delta();
            (void)encoder_take_long_press();
            if (encoder_take_press()) {
                touch_calib_cancel();
                if (s_menu_open) {
                    menu_screen_close();
                }
                radio_screen_draw();
                debug_print("touch_calib: cancelled via encoder press\n");
            } else {
                touch_calib_poll();
            }
        } else
        {
            static uint8_t s_rtty_scope_was_active = 0U;
            static uint8_t s_rtty_mode_was_active = 0U; /* tracks active_now, NOT drawing_now - see below */
            uint8_t active_now = digi_panel_active();
            /* Only actually DRAW the scope when the settings menu
             * isn't covering the panel - added 08/08/2026, per the
             * project owner: the scope never checked s_menu_open at
             * all, so opening MENU/MODE while in RTTY-L/RTTY-U kept
             * painting scope bars right over the menu underneath.
             * sdr_spectrum_waterfall_tick() already has this exact
             * same guard internally (see its own "Settings menu
             * covers the spectrum+waterfall panel while open"
             * comment) - this mirrors it for the scope's panel,
             * which occupies the identical screen region. */
            uint8_t drawing_now = (uint8_t)(active_now && !s_menu_open);

            rtty_scope_poll(); /* keep the FFT data fresh regardless - cheap, and matches
                                 * sdr_spectrum_waterfall_tick()'s own "accumulate even while
                                 * hidden" behavior, so there's no stale-data jolt on reopen. */
            if (drawing_now && !s_rtty_scope_was_active) {
                /* Fires on EITHER transition into showing the scope:
                 * switching into RTTY-L/RTTY-U from elsewhere, OR the
                 * menu just closing while RTTY was already the active
                 * mode the whole time it was open (active_now stayed
                 * true throughout, only drawing_now flips) - the TRACE
                 * always gets a fresh paint either way, since whatever
                 * was on screen right now isn't a valid diff baseline
                 * for the bars/markers. */
                rtty_scope_panel_reset();
                if (active_now && !s_rtty_mode_was_active) {
                    /* Genuinely JUST switched into RTTY-L/RTTY-U from
                     * a different mode - actually clear the text
                     * grid's CONTENT, a fresh decode session starting
                     * from nothing (see rtty_text_panel_reset()'s
                     * comment), and discard any partial multi-window
                     * average left over from before the switch (see
                     * rtty_scope_avg_reset()'s comment) so the first
                     * displayed trace is a clean average, not a blend
                     * that includes windows from whatever was tuned
                     * in before. */
                    rtty_text_panel_reset();
                    rtty_scope_avg_reset();
                } else {
                    /* Was already in RTTY mode the whole time the menu
                     * was open (active_now never flipped) - the
                     * SCROLLBACK TEXT is still exactly right, only the
                     * physical pixels under the menu went stale.
                     * rtty_text_panel_reset() would wrongly wipe every
                     * decoded line just because the person checked
                     * MODE/SHIFT/BAUD - repaint only, via
                     * rtty_text_force_redraw() (added 10/08/2026, per
                     * the project owner, fixing exactly this). */
                    rtty_text_force_redraw();
                }
            }
            s_rtty_scope_was_active = drawing_now;
            s_rtty_mode_was_active = active_now;

            if (drawing_now) {
                rtty_scope_draw();
            } else {
                /* Covers BOTH "not in RTTY mode" and "menu is open" -
                 * sdr_spectrum_waterfall_tick() already skips its own
                 * drawing internally while s_menu_open, so it's always
                 * safe to call here regardless of which of those two
                 * reasons drawing_now was false for. */
                sdr_spectrum_waterfall_tick();
            }
            demo_touch_poll();
            tune_encoder_poll();
        }
        rf_agc_poll(); /* RF-level (analog PGA) auto-AGC - see its own comment */
        rtty_poll(); /* drains rtty.c's decoded text to debug UART - see its own comment */
        cw_poll();   /* lo mismo para el CW, al mismo panel - ver su comentario */
        settings_poll(s_tune_hz, demod_am_get_mode(), k_tune_steps[s_tune_step_idx], demod_am_get_audio_bw(), s_volume_db_x2, s_nonwfm_use_48k,
                      ((s_pga_hf_x2 >= 0) ? s_pga_hf_x2 : s_pga_gain_db_x2), spectrum_smooth_pct_for_save(), s_speaker_pa_enabled, s_att_suelo, s_tema_idx); /* debounced CONFIG.CSV autosave - see settings.h's comment; cheap no-op most iterations */
#if TOUCH_EDGE_DEBUG
        touch_debug_stream_poll(); /* see TOUCH_EDGE_DEBUG's comment */
#endif
#endif

        g_fill_count++;

        if ((g_fill_count % 50) == 0
            /* Suppressed while the RTTY scope is showing - added
             * 08/08/2026, per the project owner: this ISR/waterfall
             * timing dump fires every ~50 loop passes REGARDLESS of
             * what's being tested, and during an RTTY session it
             * drowns out the sparse "rtty: <decoded text>" lines
             * (rtty_poll()'s output) that actually matter right now.
             * Not gated on DEBUG_UART_ENABLED alone because this
             * block's own content (ISR cycle counts, block budget)
             * is irrelevant to an RTTY tuning session either way -
             * this isn't about reducing UART traffic, it's about
             * signal-to-noise in the log. */
            && !digi_panel_active()
           ) {
            debug_print_dec("waterfall ticks", g_fill_count);
            /* ISR timing check (see demod_am.h's comment above
             * demod_am_get_last_cycles()): one block's real-time
             * budget is SDR_RX_BLOCK_SAMPLES samples at 96kHz (was
             * 48kHz, and 192kHz before that - see sdr_rx.h's
             * SDR_RX_BLOCK_SAMPLES comment; SAME ~2.667ms/block either
             * way, by design). If "demod ISR cycles" gets close to or
             * over "block budget cycles", the demod ISR doesn't fit in
             * real time - exactly the situation suspected in the
             * USB/LSB hang report.
             *
             * *** 05/08/2026 fix ***: this used to call
             * demod_am_get_last_cycles() UNCONDITIONALLY, even while
             * WFM (which runs an entirely separate ISR,
             * demod_wfm_process_raw(), at 192kHz/512 samples per
             * block) was the active mode - meaning WFM's own ISR
             * timing has never actually been checked, not once, since
             * the dual-rate split was introduced. demod_am_get_last_
             * cycles() just kept reporting whatever AM/SSB/LSB/NFM's
             * ISR last measured (stale, from before the switch into
             * WFM, since demod_am_process_raw() stops being called at
             * all while WFM is active). Branch on the live mode so
             * each path's real ISR gets checked against its own real
             * budget - suspected relevant to the "ruido de fondo"
             * WFM report: atan2f() runs once per sample (512x/block)
             * in the WFM discriminator, far more expensive than AM's
             * plain envelope detection, making an occasional real-time
             * overrun plausible and previously invisible. */
            if (demod_am_get_mode() == DEMOD_MODE_WFM) {
                debug_print_dec("WFM ISR cycles (last block)", demod_wfm_get_last_cycles());
                debug_print_dec("block budget cycles (192kHz, for reference)",
                                 (SystemCoreClock / 192000UL) * SDR_RX_BLOCK_SAMPLES_WFM);
            } else {
                debug_print_dec("demod ISR cycles (last block)", demod_am_get_last_cycles());
                debug_print_dec(s_nonwfm_use_48k ? "block budget cycles (48kHz, for reference)"
                                                   : "block budget cycles (96kHz, for reference)",
                                 (SystemCoreClock / (s_nonwfm_use_48k ? 48000UL : 96000UL)) * SDR_RX_BLOCK_SAMPLES);
                {
                    /* Per-stage breakdown (31/07/2026, see
                     * demod_am_get_last_cycles_breakdown()'s comment) -
                     * pins down which stage a total-cycles jump actually
                     * comes from, instead of guessing. WFM has no
                     * equivalent breakdown getter yet (only the AM/SSB/
                     * LSB/NFM path had per-stage instrumentation added) -
                     * if the total above points at a WFM overrun, that's
                     * the next thing worth adding, not assumed here. */
                    demod_am_cycles_breakdown_t bd = demod_am_get_last_cycles_breakdown();
                    (void)bd;
                    debug_print_dec("  frontend (deinterleave/down-mix/CHF)", bd.frontend);
                    debug_print_dec("  extract  (mode-specific: AM/WFM/SSB)", bd.extract);
                    debug_print_dec("  audio    (DC block + audio LPF)", bd.audio);
                    debug_print_dec("  nr       (Spectral Subtraction, AM/SSB only, 0 otherwise)", bd.nr);
                    debug_print_dec("  agc_out  (AGC + I2S write)", bd.agc_out);
                }
            }
        }
    }
}

/*
 * Calibration build: draws a horizontal ruler (tick + text label with
 * the Y value) every 40px from 0 to GFX_SCREEN_HEIGHT-1, plus an exact
 * border at (0,0,GFX_SCREEN_WIDTH-1,GFX_SCREEN_HEIGHT-1). Photograph
 * the panel and compare: the last label that reads COMPLETE (not cut
 * off) before the panel's real bottom edge indicates the true usable
 * height. If the drawn bottom border isn't visible at all,
 * GFX_SCREEN_HEIGHT is still larger than the real physical height.
 */
#if CALIB_HEIGHT_TEST
static void calib_height_ruler_draw(void)
{
    char label[8];
    uint16_t y;

    gfx_fill_screen(GFX_COLOR_BLACK);

    /* Exact border at the limits we currently assume */
    gfx_rect(0, 0, GFX_SCREEN_WIDTH, GFX_SCREEN_HEIGHT, GFX_COLOR_RED);

    for (y = 0; y < GFX_SCREEN_HEIGHT; y += 40) {
        uint8_t i = 0;
        uint16_t v = y;
        char tmp[8];
        uint8_t n = 0;

        /* Manual itoa (no sprintf, to avoid pulling in more of newlib) */
        if (v == 0) {
            tmp[n++] = '0';
        } else {
            while (v > 0 && n < sizeof(tmp)) {
                tmp[n++] = (char)('0' + (v % 10));
                v /= 10;
            }
        }
        while (n > 0) {
            label[i++] = tmp[--n];
        }
        label[i] = '\0';

        gfx_hline(0, y, 20, GFX_COLOR_YELLOW);
        gfx_text(24, (uint16_t)((y >= 3) ? (y - 3) : 0), label,
                  GFX_COLOR_CYAN, GFX_COLOR_BLACK, 1);
    }
}
#endif /* CALIB_HEIGHT_TEST */

/*
 * RADIO UI LAYOUT (30/07/2026 redesign - replaces the original 3-button
 * demo screen; still doubles as the gfx.c/ui.c validation surface).
 * Landscape 800x480 (confirmed on real hardware, see the
 * GFX_SCREEN_WIDTH/HEIGHT comment in gfx.h). Zones:
 *
 *   +--------------------------------------------------------------+
 *   | TOP BAR (h=64): freq (big) | mode | step+vol | time | batt   |
 *   +--------------------------------------------------------------+
 *   | STATUS STRIP (h=40): S-meter | SNR | NR SPT AGC [..] OVR      |
 *   +--------------------------------------------------------------+
 *   | SPECTRUM (796 wide, 240 tall)                                |
 *   +--------------------------------------------------------------+
 *   | WATERFALL (796 x 72 rows)                                    |
 *   +--------------------------------------------------------------+
 *   | BOTTOM BAR: 6 buttons (MODE VOL STEP NR BANDS MENU)          |
 *   +--------------------------------------------------------------+
 *
 * (01/09/2026: the old right-hand column - S-meter + badges next to
 * the spectrum/waterfall - was removed; see STATUS_STRIP_Y/H's own
 * declaration comment for the full story. Spectrum/waterfall now span
 * the full screen width.)
 *
 * Every coordinate is an internally-linked constant (static const, not
 * a macro) so sdr_spectrum_waterfall_tick() uses exactly the same
 * values as radio_screen_draw() without duplicating arithmetic by
 * hand.
 */
static const uint16_t TOP_H        = 64;

/*
 * *** 01/09/2026, right-hand column REMOVED, per the project owner:
 * "la parte derecha es de poco uso y desaprovecha mucho espacio" ***
 * - S-meter, SNR readout, and the status badges all move into a new
 * horizontal STATUS_STRIP row directly under the top bar (see its own
 * comment below), freeing the old RCOL_X..799 width entirely for the
 * spectrum/waterfall panel, which now spans the full screen width.
 * SPEC_H shrinks by exactly STATUS_STRIP_H (280->240) and SPEC_Y grows
 * by the same amount (64->104) - their SUM is unchanged (344 either
 * way), which is why WF_PANEL_Y/WF_Y below still come out to the same
 * numbers as before: the waterfall's own position and height are
 * completely untouched by this change, only the spectrum panel above
 * it shrinks vertically to make room, and both panels now stretch
 * across the full width instead of stopping at the old 676px RCOL
 * boundary. This was only feasible RAM-wise after moving a whole set
 * of CPU-only DSP/FFT/spectrum working buffers into TCM RAM (see
 * fft.c's TCMRAM_BSS comment) - the waterfall's own history buffer
 * (waterfall.h's WATERFALL_WIDTH) alone needed ~18KB more main RAM at
 * this new width, which the freed TCM headroom now comfortably covers.
 */
#define STATUS_STRIP_Y TOP_H
#define STATUS_STRIP_H 40

/* Main (left) display column: spectrum over waterfall - now the ONLY
 * column, full screen width. */
static const uint16_t MAIN_W       = 800;             /* panel width, border included - was 676 before the RCOL removal above */
static const uint16_t SPEC_Y       = 104;             /* = TOP_H(64) + STATUS_STRIP_H(40) - was 64 */
static const uint16_t SPEC_H       = 240;             /* was 280 - see this block's header comment: SPEC_Y+SPEC_H unchanged at 344 */
/* ETAPA 3b: la traza deja los 40 px de la izquierda para la canaleta de los
 * ejes (numeros de dB arriba, escala de color del waterfall abajo) - ver
 * spec_chrome.h. Los valores salen de ahi y no se repiten aqui: que la regla
 * y la traza calculen la misma geometria por caminos distintos es justo como
 * se consigue que el eje mienta. */
static const uint16_t SPEC_TRACE_X = SPC_TRACE_X;     /* = 40, tras la canaleta */
static const uint16_t SPEC_TRACE_W = SPC_TRACE_W;     /* = WATERFALL_WIDTH; /4 exacto para el marcador de Fs/4 */
static const uint16_t WF_PANEL_Y   = 104 + 240 + 2;   /* = 346, same value as before (see header comment) */
static const uint16_t WF_Y         = 104 + 240 + 4;   /* first waterfall row - same value as before */

/* Barra de acciones: la geometria la manda ahora ui_act.h (UI_ACT_Y/H/
 * BTN_W/GAP), que es quien la dibuja y quien resuelve los toques. Las cuatro
 * constantes BTNBAR_* que habia aqui se han eliminado en vez de dejarlas
 * apuntando a los valores nuevos: dos sitios con la misma geometria es
 * exactamente como se consigue que el dibujo y la zona de toque se separen.
 * La barra crece de 46 a 54 px de alto (9,2 mm) aprovechando el hueco que
 * quedaba libre entre el waterfall (acaba en 420) y el borde de la pantalla. */

/*
 * IMPORTANT: these widgets are static (not local to
 * radio_screen_draw()) on purpose. ui_screen_t only stores POINTERS to
 * them (to avoid duplicating data or depending on malloc), so they
 * must stay alive for as long as the screen exists - if they were
 * stack variables of a function that already returned,
 * ui_screen_touch() would be reading stack memory already reused by
 * another call. This is exactly the kind of bug that doesn't produce
 * a compile error but silently corrupts memory at runtime.
 */
static ui_panel_t  s_title_panel;
static ui_panel_t  s_spectrum_panel;
static ui_panel_t  s_waterfall_panel;
/* s_rcol_panel REMOVED 01/09/2026 - the right-hand status column it
 * anchored no longer exists, see STATUS_STRIP_Y/H's declaration
 * comment. Replaced by s_status_strip_panel, the new horizontal
 * strip's own background panel. */
static ui_panel_t  s_status_strip_panel;
static ui_button_t s_btn_mode;
static ui_button_t s_btn_vol;
static ui_button_t s_btn_step;
static ui_button_t s_btn_nr;
static ui_button_t s_btn_bands; /* bottom-bar shortcut to menu_bands_show() - repurposed 02/08/2026 from the SPT smoothing-cycle shortcut, see demo_button_callback()'s comment */
static ui_button_t s_btn_menu;
/* Added 31/07/2026: unlike the 6 bottom-bar buttons above, this one
 * lives in the badge grid (see badges_draw()) - a real, touchable
 * ui_button_t standing in for what used to be a plain badge_draw()
 * call, so tapping it can cycle the AGC profile directly instead of
 * needing yet another bottom-bar slot (all 6 are already spoken for -
 * see demo_button_callback()'s header comment) or another MENU cycle
 * position. ui_button_draw()'s rendering (fill+border+centered label)
 * already matches badge_draw()'s look, so no visual seam. */
/*
 * encoder_target_t - hoisted up here (was originally declared further
 * down, right before s_encoder_target - see that declaration's full
 * comment for what this enum means and how each value is reached) so
 * it's available for s_menu_detail_target below, which needs the type
 * before its own declaration point. Fixes a real build error: the
 * settings-menu block was declared before this enum existed in the
 * file, even though C only cares about textual order, not "logical"
 * grouping.
 */
typedef enum {
    ENCODER_TARGET_TUNE = 0,
    ENCODER_TARGET_VOLUME,
    ENCODER_TARGET_BACKLIGHT,
    ENCODER_TARGET_SCALE,
    ENCODER_TARGET_SQUELCH,
    ENCODER_TARGET_SMOOTH,
    ENCODER_TARGET_PGA,
    ENCODER_TARGET_NR,
    ENCODER_TARGET_RTTY_SHIFT,
    ENCODER_TARGET_CW_TONE
} encoder_target_t;

/*
 * --- Settings menu screen (first pass, extended with real detail
 *     views 31/07/2026) -----------------------------------------------
 *
 * Added 31/07/2026: a SEPARATE ui_screen_t, not more widgets crammed
 * into s_demo_screen - the framework already supports this (ui_screen_t
 * is just a widget registry + dispatcher, see ui.h, no assumption
 * anywhere that only one screen exists). MENU now OPENS this screen
 * (menu_screen_open()) instead of cycling s_encoder_target through
 * BACKLIGHT/SCALE/SQUELCH - see demo_button_callback()'s MENU branch
 * for what that replaces.
 *
 * TWO LEVELS inside this one screen:
 *   - GRID (menu_grid_show()): the 4x2 tile overview - tapping
 *     AGC/SPT acts immediately (cycles/toggles in place, same as
 *     before); tapping SQUELCH/BACKLIGHT/SCALE/VOLUME/SMOOTH now
 *     opens...
 *   - DETAIL (menu_detail_show()): a single big value + a BACK tile,
 *     replacing what used to be "select the target and bounce back to
 *     the main screen to see it change" - per the project owner, that
 *     wasn't REAL interaction (you couldn't see the value change from
 *     inside the menu at all). Turning the knob now updates the big
 *     value live, right here - see settings_value_redraw()'s comment
 *     for how tune_encoder_poll() knows which view to repaint.
 *
 * CONFINED to the spectrum+waterfall panel (MENU_AREA_*, see
 * menu_grid_show()), not a full-screen takeover - changed 31/07/2026,
 * again per the project owner: the top bar, right column, and bottom
 * button bar now stay live and touchable the ENTIRE time the menu is
 * open, only the graph area gets replaced by the tile grid (or a
 * detail view). This means TWO screens are effectively active at
 * once, split by SCREEN REGION rather than by which one is "current":
 *   - demo_touch_poll() routes each touch sample to s_menu_screen if
 *     it lands inside MENU_AREA while s_menu_open, otherwise always to
 *     s_demo_screen (see its own comment for the accepted drag-across-
 *     the-boundary edge case).
 *   - sdr_spectrum_waterfall_tick() keeps the S-meter and time readout
 *     updating every frame regardless of s_menu_open (both live in the
 *     still-visible top bar/right column) but skips the FFT-averaging/
 *     smoothing/spectrum_draw()/waterfall work while the menu covers
 *     that panel - see its own comment for exactly where that split
 *     happens.
 *   - menu_screen_close() only needs to restore the spectrum+waterfall
 *     PANEL BORDERS (ui_panel_draw() on s_spectrum_panel/
 *     s_waterfall_panel) - everything else was never touched, so a
 *     full radio_screen_draw() would just be wasted EXMC bandwidth and
 *     a visible flash of things that never changed. The actual trace/
 *     waterfall CONTENT follows on the next tick's frame, ~33ms later
 *     at most - imperceptible.
 */
/* s_menu_detail_active: 0 = grid showing, 1 = a detail view showing.
 * s_menu_detail_target: WHICH detail view, when active - reuses
 * encoder_target_t rather than inventing a parallel enum, since the
 * detail view IS "adjust whatever the encoder currently targets". */
static uint8_t s_menu_detail_active = 0U;
static encoder_target_t s_menu_detail_target = ENCODER_TARGET_TUNE;
/* s_menu_bands_active: same idea as s_menu_detail_active, one level
 * up - 0 = grid (or a detail view) showing, 1 = the BANDS preset list
 * showing. Mutually exclusive with s_menu_detail_active in practice
 * (menu_grid_show() clears both whenever it runs), but kept as its
 * own flag rather than folded into a 3-state enum - simplest thing
 * that reads clearly at each of the few call sites that check it. */
static uint8_t s_menu_cfg_active = 0U; /* la pantalla de ajustes con columna esta abierta */
static uint8_t s_menu_bands_active = 0U;
/* s_menu_step_active / s_menu_mode_active: same bookkeeping idea as
 * s_menu_bands_active, for the two picker lists reachable directly
 * from the bottom bar (menu_step_list_show()/menu_mode_list_show()) -
 * see those functions' comments. Unlike BANDS these have no parent
 * grid to belong to (they're opened straight from STEP/MODE, not from
 * within the settings menu), but they still need SOME flag set while
 * open for the same "which sub-view of s_menu_screen is this" reason,
 * even though nothing branches on them today beyond set/reset. */
static uint8_t s_menu_step_active = 0U;
static uint8_t s_menu_mode_active = 0U;
/* s_menu_freq_active: same bookkeeping idea as s_menu_step_active/
 * s_menu_mode_active above, for the frequency-entry keypad (added
 * 07/08/2026, per the project owner: tap the frequency readout in the
 * top bar to type a new one instead of only being able to spin the
 * encoder or use a band preset). Opened straight from
 * demo_touch_poll()'s top-bar tap check, not from the settings grid -
 * same "no parent grid" situation as STEP/MODE. */
static uint8_t s_menu_freq_active = 0U;
/* s_menu_time_active: same bookkeeping idea, for the clock-setting
 * keypad (added 08/09/2026 - see menu_time_keypad_show()'s comment).
 * Opened straight from demo_touch_poll()'s top-bar TIME_TAP zone
 * check, same "no parent grid" situation as FREQ. */
static uint8_t s_menu_time_active = 0U;
/*
 * s_freq_entry_value/s_freq_entry_digits: the digits typed so far on
 * the keypad, plain integer accumulation (value = value*10 + digit),
 * position of the decimal point tracked SEPARATELY (s_freq_entry_point_pos
 * below) rather than as a float - see menu_freq_keypad_show()'s
 * comment for why: no float parsing needed on a bare-metal target,
 * matching the kHz/MHz accept buttons' own existing uint64_t-then-
 * divide approach. Capped at FREQ_ENTRY_MAX_DIGITS so the value
 * itself never risks overflowing uint32_t (999,999,999 fits easily);
 * the SEPARATE overflow risk - value*1000000 for the MHz button -
 * is handled in the accept callback via a uint64_t intermediate, not
 * here. Reset to 0/0 every time the keypad opens (menu_freq_keypad_
 * show()), never pre-filled with the current frequency - typing a
 * fresh number is the whole point, and starting blank avoids any
 * "do I need to clear this first" confusion. */
#define FREQ_ENTRY_MAX_DIGITS 9U
static uint32_t s_freq_entry_value = 0U;
static uint8_t  s_freq_entry_digits = 0U;
/*
 * s_freq_entry_point_pos: how many digits had been typed BEFORE the
 * decimal point was pressed - FREQ_ENTRY_NO_POINT (0xFF) if no point
 * has been entered yet. Deliberately a digit COUNT, not a flag plus a
 * separately-tracked fractional value: this survives the DEL key
 * cleanly (deleting back past the point just needs comparing this
 * count against the current s_freq_entry_digits, see
 * menu_freq_keypad_del_callback()) and survives leading zeros
 * correctly (typing "0" "." "6" "2" "1" for 0.621 records point_pos=1
 * regardless of the fact that a leading zero contributes nothing
 * numerically to s_freq_entry_value - the fractional digit COUNT at
 * accept time is (s_freq_entry_digits - s_freq_entry_point_pos)
 * either way). Added 01/09/2026, replacing the plain HZ accept button
 * - see menu_freq_keypad_show()'s comment for why: typing a
 * frequency out to bare-Hz precision digit-by-digit had no practical
 * use once kHz/MHz entry existed, so that keypad slot became a
 * decimal point instead, letting a frequency be typed exactly the way
 * it's normally written (e.g. "14.200" + MHZ, or "0.621" + MHZ)
 * rather than only as a bare integer count of the chosen unit. */
#define FREQ_ENTRY_NO_POINT 0xFFU
static uint8_t  s_freq_entry_point_pos = FREQ_ENTRY_NO_POINT;
/*
 * --- Settings grid PAGES (RADIO / UI / HW) -------------------------------
 *
 * Added 03/08/2026, per the project owner: the settings grid's left
 * column is now a fixed 3-tile PAGE SELECTOR (RADIO/UI/HW, one per
 * row), and the remaining 3x3 tiles on the right show whichever
 * page's options are currently selected. Only ONE flag is needed
 * (s_menu_page) - unlike s_menu_detail_active/s_menu_bands_active/etc.
 * above, the page selector doesn't leave the grid (it just changes
 * WHAT the grid shows), so there's no separate "is a page open"
 * boolean, just which page.
 *
 * menu_page_step_callback() is column 0's shared PREV/NEXT callback -
 * steps s_menu_page (wrapping) and re-runs menu_grid_show() to repaint
 * both the pager (new current-page name) and the new page's options in
 * one go - see this file's "Settings grid PAGES" / PAGINATION comment
 * for the full column-0 layout this replaced menu_page_select_callback()
 * with on 09/08/2026.
 */
typedef enum {
    MENU_PAGE_RADIO = 0,
    MENU_PAGE_UI,
    MENU_PAGE_HW,
    MENU_PAGE_DIG, /* digital-mode (RTTY) params - added 09/08/2026, see this file's PAGINATION comment */
    MENU_PAGE_COUNT
} menu_page_t;

static menu_page_t s_menu_page = MENU_PAGE_RADIO;

 /* was the reserved/empty slot - see menu_grid_show() */
/* s_speaker_pa_enabled: backs BOTH the tile's label (menu_tile_speaker_pa_refresh())
 * and the actual GPIO level (speaker_pa_set_enabled(), defined down
 * with the rest of the GPIO drivers near led_gpio_init() - declared
 * here instead, alongside the tile, since it's used well before that
 * point in the file). */
static uint8_t s_speaker_pa_enabled = 1U;
/* 22/09/2026: aqui vivia s_menu_detail_back, el boton "BACK" que compartian
 * las pantallas de detalle y los dos teclados. Ya no lo usa nadie: las de
 * detalle se dibujan con ui_det.c y los teclados con ui_kbd.c. */
/* Backing buffers for the tiles whose label needs to show a live value
 * (AGC/SQUELCH/BACKLIGHT/VOLUME/SPT/SMOOTH/SPEC/ZOOM/PGA/NR) - ui_button_t.label
 * is just a const char*, so whatever it points at must outlive the
 * button. SCALE and EXIT use plain string literals instead (SCALE
 * still shows no live value on its GRID tile - see menu_grid_show()'s
 * comment; its DETAIL view does show both LO and HI, see
 * menu_detail_value_redraw()). Sized generously; actual content is
 * always much shorter. */
/* s_menu_tile_rtty_inv needs no buffer - only two possible strings
 * ("INV NORM"/"INV REV"), same "point straight at a literal" shape as
 * s_menu_tile_speaker_pa's SPK ON/OFF. */

/* NR master on/off (Spectral Subtraction - see nr_ss.h), mirrored into
 * nr_ss_set_enabled() on every change - toggled by the bottom bar's NR
 * button (s_btn_nr's callback) and shown live on the row0 badge (see
 * badges_draw()). Was VESTIGIAL from 31/07/2026 to 03/08/2026 (see
 * this file's git history if you need the old comment) while s_btn_nr
 * was temporarily repurposed to cycle the AGC profile instead, ahead
 * of the actual NR DSP existing - restored to its real job now that
 * nr_ss.h does. */
static uint8_t s_nr_on = 0U;

/* Spatial line-smoothing pass count (0-3), fed straight into
 * spectrum_set_line_smooth() - see its comment in spectrum.h for what
 * it actually does to the trace. REPURPOSED 01/08/2026 from the old
 * NB (noise blanker) flag: NB never drove any real DSP, it just
 * flipped a badge/button/tile with a debug_print() behind it (see the
 * git history if that stub is ever needed again), so its three UI
 * slots - bottom-bar button, right-column badge, and settings-menu
 * tile - were free real estate for a control that actually does
 * something. Reachable via the SPT badge (indicator only) and the
 * settings-menu grid tile (menu_tile_nb_callback() - cycles
 * 0->1->2->3->0 on tap); the bottom-bar button that used to be a third
 * shortcut for this was itself repurposed 02/08/2026 to open the
 * BANDS list instead (see s_btn_bands' comment in
 * demo_button_callback()) - the smoothing pass count is still fully
 * reachable, just one tap deeper now. Default 0 (matches NB's old
 * default-off state); the project owner settled on 2-3 as the sweet
 * spot after testing by hand. */
static uint8_t s_spec_smooth_passes = 0U;

/*
 * --- Encoder-driven tuning state -----------------------------------
 *
 * s_tune_hz starts at 7.150.000MHz (40m band, AM/general-coverage
 * starting point) - CHANGED 07/08/2026 from MS5351_CAPTURED_LO_HZ
 * (90.8MHz), per the project owner: the radio boots into AM mode
 * (see demod_am.c's s_mode initializer), and 90.8MHz - deep in FM
 * broadcast - made no sense to land on with AM selected. See
 * main()'s call to apply_lo_tune(s_tune_hz) right after
 * ms5351_tune_captured() for how the LO actually gets moved off the
 * captured-replay frequency to this one at boot - the two are
 * decoupled on purpose now, see that comment.
 *
 * The button cycles the tuning step. Limits: 30kHz (lowered
 * 01/09/2026 from 100kHz, per the project owner, specifically to
 * reach DCF77/similar LF time-signal stations - 77.5kHz - with
 * comfortable margin) - no longer tied to ms5351.c's own LOWF_FLOOR_HZ
 * the way the old 100kHz value was: anything this low routes through
 * lo_gen_gd32.c instead (LO_GEN_CROSSOVER_HZ=300kHz), whose own
 * achievable range comfortably covers this with margin to spare (see
 * that module's own comment). Kept comfortably above
 * DEMOD_IF_OFFSET_HZ (24kHz) - see apply_lo_tune()'s own comment and
 * the "TUNE_MIN_HZ > DEMOD_IF_OFFSET_HZ" invariant a few other
 * comments in this file rely on to avoid a uint32_t underflow - going
 * any lower than this would need re-checking those too. 180MHz is the
 * top of the front-end LPF bank (rf_lpf.c).
 */
#define TUNE_MIN_HZ 30000UL
#define TUNE_MAX_HZ 180000000UL

/* s_tune_hz's actual declaration moved up near s_menu_open, 17/08/2026
 * - see the comment there. This comment block (the "why 7.150MHz",
 * "why apply_lo_tune(s_tune_hz) exists separately from
 * ms5351_tune_captured()" reasoning above) still applies unchanged. */
/* k_tune_steps[]/TUNE_STEP_COUNT's actual declarations moved up near
 * s_tune_step_idx, 18/08/2026, for the same reason s_tune_hz was:
 * main()'s boot sequence now needs both (to look up which INDEX
 * matches a "tune_step_hz" value loaded from CONFIG.CSV, by VALUE
 * rather than by raw index - see set_tune_step_idx()'s neighborhood
 * up there for the full comment) before entering the main loop. */

#define BAND_STEP_100HZ 0U
#define BAND_STEP_1K    1U
#define BAND_STEP_5K    2U
#define BAND_STEP_10K   3U
#define BAND_STEP_12K5  4U
#define BAND_STEP_25K   5U
#define BAND_STEP_100K  6U
#define BAND_STEP_1M    7U
/* Las mismas, escritas de verdad, para la cabecera y la barra de acciones,
 * que ya usan fuente proporcional. Las de arriba se quedan para las
 * pantallas que siguen dibujando con la 5x7 de ancho fijo, donde "12,5 kHz"
 * ni cabe ni se lee. Mismo orden e indices: si una crece, la otra tambien. */
static const char *k_tune_step_labels_ui[] = {
    "100 Hz", "1 kHz", "5 kHz", "10 kHz", "12,5 kHz", "25 kHz", "100 kHz", "1 MHz"
};
/* s_tune_step_idx's actual declaration moved up near s_menu_open/
 * s_tune_hz, 18/08/2026 - see the comment there. This comment block
 * (why CONFIG_TUNE_START_STEP_IDX is what it is) still applies
 * unchanged. */

/*
 * MODE picker list - added 01/08/2026, same treatment as STEP above:
 * replaces the old "MODE button cycles AM->USB->LSB->NFM->WFM->AM" with
 * a pick screen (see menu_mode_list_show()). label+demod_mode_t pairs
 * in that same order, purely so the grid fills left-to-right/top-to-
 * bottom in the order people expect from the old cycle.
 *
 * RTTY-L/RTTY-U added 08/08/2026, per the project owner, once the
 * RTTY decoder + tuning scope were validated against a real signal
 * and graduated from a debug-build-only tool to a real mode. Both map
 * to a REAL underlying demod_mode_t (LSB/USB respectively) - RTTY
 * isn't its own demodulator, it's two audio tones inside an SSB
 * passband, so the actual RF demod stays plain LSB/USB; what these
 * two entries ADD on top is switching rtty_get_enabled() on and
 * setting mark/space for the correct polarity (see
 * menu_mode_preset_callback()'s rtty_variant handling right below).
 *
 * The polarity difference is real, not a firmware quirk: USB and LSB
 * are mirror images of each other in frequency for the same pair of
 * RF tones, so whichever audio tone is "mark" in one becomes "space"
 * in the other at the same nominal Hz - confirmed by the project
 * owner needing to flip the transmitter's own inversion setting to
 * get a clean decode in USB after LSB worked normally. RTTY_VARIANT_
 * INVERTED swaps CONFIG_RTTY_MARK_HZ/SPACE_HZ's roles for exactly
 * this reason, so the person doesn't have to remember to touch their
 * transmitter (or config.h) when switching which sideband they're
 * listening on.
 */
typedef enum {
    RTTY_VARIANT_NONE = 0,     /* plain mode - selecting this turns RTTY OFF if it was on */
    RTTY_VARIANT_NORMAL,       /* RTTY on, mark/space as config.h's CONFIG_RTTY_MARK_HZ/SPACE_HZ */
    RTTY_VARIANT_INVERTED      /* RTTY on, mark/space SWAPPED - see this block's comment above */
} rtty_variant_t;

/*
 * CW anadido 21/09/2026. Encaja igual que RTTY-L/RTTY-U: no es un
 * demodulador propio sino un oyente colgado de USB o LSB, asi que lleva
 * su modo real debajo y un interruptor encima.
 *
 * Va sobre USB porque es la convencion en las bandas de aficionado. Para
 * decodificar da exactamente igual -un tono es un tono en cualquiera de
 * las dos bandas laterales-; lo unico que cambia es hacia que lado hay
 * que girar el mando para que suba el pitido, y con USB gira como espera
 * casi todo el mundo.
 */
/*
 * La DESCRIPCION va aqui dentro, 22/09/2026, y no en un array aparte
 * indexado por el mismo indice.
 *
 * Estaba en un k_modo_desc[] paralelo, y al anadir CW en medio paso lo
 * que tenia que pasar: las descripciones se corrieron una posicion y la
 * ultima entrada leia FUERA del array. Lo que sale de ahi no es un texto
 * sino cuatro bytes cualesquiera tratados como puntero, y el dibujador
 * de texto se va a recorrer memoria arbitraria buscando un cero. La
 * pantalla de modos dejaba de aparecer entera.
 *
 * Y lo peor es que el compilador no puede avisar: el array paralelo
 * estaba bien formado, solo era mas corto. Dos tablas que hay que
 * mantener alineadas a mano son una tabla con un fallo pendiente. Ahora
 * anadir un modo es anadir una linea, y no hay ninguna otra que se
 * pueda olvidar.
 */
typedef struct {
    const char *label;
    const char *desc;
    demod_mode_t mode;
    rtty_variant_t rtty_variant;
    uint8_t cw;                 /* 1 = ademas enciende el decodificador de CW */
} demod_mode_entry_t;

static const demod_mode_entry_t k_demod_modes[] = {
    { "AM",     "Amplitud modulada",   DEMOD_MODE_AM,  RTTY_VARIANT_NONE,     0U },
    { "SAM",    "AM síncrona",         DEMOD_MODE_SAM, RTTY_VARIANT_NONE,     0U }, /* synchronous AM, 21/08/2026 - see sam.h */
    { "USB",    "Banda lateral alta",  DEMOD_MODE_USB, RTTY_VARIANT_NONE,     0U },
    { "LSB",    "Banda lateral baja",  DEMOD_MODE_LSB, RTTY_VARIANT_NONE,     0U },
    { "NFM",    "FM estrecha",         DEMOD_MODE_NFM, RTTY_VARIANT_NONE,     0U },
    { "WFM",    "FM ancha",            DEMOD_MODE_WFM, RTTY_VARIANT_NONE,     0U },
    { "CW",     "Telegrafía Morse",    DEMOD_MODE_USB, RTTY_VARIANT_NONE,     1U }, /* ver el comentario de arriba */
    { "RTTY-L", "Teletipo en LSB",     DEMOD_MODE_LSB, RTTY_VARIANT_NORMAL,   0U }, /* confirmed correct polarity on LSB, 08/08/2026 */
    { "RTTY-U", "Teletipo en USB",     DEMOD_MODE_USB, RTTY_VARIANT_INVERTED, 0U }  /* USB mirrors LSB - see this block's comment */
};
#define DEMOD_MODE_ENTRY_COUNT (sizeof(k_demod_modes) / sizeof(k_demod_modes[0]))
/*
 * --- Frequency-entry keypad ----------------------------------------------
 *
 * 15 of the 16 grid cells (see FREQ_KEYPAD_* geometry above) - the
 * 16th (BACK) reuses the shared s_menu_detail_back widget/callback,
 * same as STEP/MODE's picker lists do, rather than allocating a
 * second BACK button that would do the exact same thing.
 * Index layout, 4 cols x 4 rows, row-major (matches
 * menu_freq_keypad_show()'s loop):
 *   row0: 1 2 3 DEL
 *   row1: 4 5 6 CLR
 *   row2: 7 8 9 (BACK lives here, col 3 - shared widget, not in this array)
 *   row3: Hz 0 kHz MHz
 */

/*
 * --- BANDS presets -------------------------------------------------------
 *
 * Added 31/07/2026, per the project owner: quick-jump tiles for the
 * most commonly used bands within this board's 4.8-180MHz tuning
 * range (TUNE_MIN_HZ/MAX_HZ above), each bundling a starting
 * frequency + demod mode + tuning step into one tap. Frequencies are
 * reasonable STARTING POINTS inside each band, not band-EDGE
 * markers - picked to land on something likely to have activity, not
 * necessarily the technical bottom of the allocation.
 *
 * SW BROADCAST (49m/41m/31m/19m): AM, 5kHz step (standard SW
 * broadcast channel spacing). Only 4 of the many SW broadcast bands -
 * a reasonably representative spread across the HF spectrum, not an
 * exhaustive list; easy to add more entries the same way.
 * FM BCST (commercial FM broadcast): WFM, 100kHz step, 88.0MHz - the
 * bottom of the commercial FM band almost everywhere.
 * AIRBAND (civil aviation voice): AM - aviation voice is AM, not FM,
 * unlike everything else in this table - 25kHz step (the simpler,
 * widely-used channel spacing; some regions use 8.33kHz instead,
 * not offered here as a separate step yet).
 * 2M (amateur 2 meter band): NFM, 12.5kHz step (standard repeater/
 * simplex channel spacing in most of Europe).
 * VHF HI: NFM, 12.5kHz step, 150.0MHz - a general "above 2m, below
 * the 180MHz ceiling" catch-all (marine VHF, PMR, and similar narrow-
 * band VHF traffic live in this general region) rather than one
 * specific named allocation - the vaguest entry here, worth revisiting
 * once you know what you actually want to listen to up there.
 *
 * 80M/40M/20M (amateur HF, added 02/08/2026): LSB below 10MHz, USB
 * above - the standard amateur-radio sideband convention, not an
 * arbitrary choice per band. 1kHz step (BAND_STEP_1K) for SSB tuning
 * precision - the 5/25kHz broadcast/aviation steps above would be far
 * too coarse to sit on a voice QSO here. Frequencies are common phone-
 * segment calling/activity spots, not band edges: 80M 3.750MHz (IARU
 * R1 SSB calling area), 40M 7.150MHz (R1 voice segment, comfortably
 * inside 7.130-7.200), 20M 14.250MHz (a generally busy R1/R2 SSB
 * spot).
 * 11M (added 02/08/2026): technically CB, NOT an amateur allocation
 * anywhere today (it was, decades ago, in some countries - hence
 * still being lumped in with "ham bands" colloquially) - included
 * anyway per the project owner's request. 27.185MHz is CB channel 19,
 * historically the most active AM calling channel (truckers). AM,
 * 10kHz step (BAND_STEP_10K) - the actual 40-channel CB spacing.
 */
/*
 * *** 22/09/2026, LISTA REHECHA ENTERA, por el dueno del proyecto: "la
 * ventana de bandas... en la que hay ahora es un desproposito, quiero todas
 * las bandas ham, la fm, las sw vhf y lo que veas que falte ahi" ***
 *
 * Lo que habia eran doce entradas sueltas con una sigla cada una. Ahora:
 *
 * - Estan TODAS las bandas de aficionado de la Region 1 que caben en el
 *   rango del aparato (30 kHz a 180 MHz), de 2200 m a 2 m. Fuera queda 70 cm
 *   (430 MHz), que el hardware no alcanza.
 * - Estan todas las bandas de radiodifusion de onda corta por metros, de
 *   120 m a 11 m, mas onda media y onda larga.
 * - Estan las de utilidad que se escuchan de verdad con esto: FM comercial,
 *   aeronautica, marina VHF, CB, NDB, NAVTEX, emisoras horarias y la banda
 *   de los satelites meteorologicos de 137 MHz.
 *
 * Cada entrada lleva su RANGO escrito, que es lo que faltaba para poder
 * elegir sin tener que pulsar y mirar a ver donde caes.
 *
 * SOBRE EL MODO Y EL PASO de cada una: el modo es el que se usa de verdad en
 * esa banda (LSB por debajo de 10 MHz y USB por encima en aficionados, que
 * es convencion y no capricho; AM en radiodifusion y aeronautica; NFM en
 * marina y 2 m; WFM solo en FM comercial). El paso es el espaciado real del
 * servicio donde lo hay (10 kHz en CB, 12,5 kHz en marina y 2 m, 25 kHz en
 * aeronautica, 100 kHz en FM) y 1 kHz en lo demas, que es lo fino que se
 * necesita para SSB. La frecuencia de entrada no es el borde de la banda
 * sino un punto donde suele haber algo.
 *
 * NOTA sobre el paso en onda media y larga: el canalizado real de la Region 1
 * es de 9 kHz, que no esta entre los pasos del aparato (ver k_tune_steps).
 * Se entra con 1 kHz, que permite caer en cualquier canal aunque haga falta
 * girar mas.
 */
typedef struct {
    const char *label;
    const char *rango;    /* escrito tal cual se pinta: "5.800-6.200 kHz" */
    uint32_t freq_hz;
    demod_mode_t mode;
    uint8_t step_idx; /* index into k_tune_steps[]/k_tune_step_labels[] - see BAND_STEP_* above */
    uint8_t familia;  /* BAND_FAM_* */
} band_preset_t;

/* El NUMERO manda: es el orden en que salen las familias en la columna de la
 * izquierda. Aficionados primero, por el dueno del proyecto. */
#define BAND_FAM_HAM   0U   /* aficionados */
#define BAND_FAM_BCST  1U   /* radiodifusion */
#define BAND_FAM_UTIL  2U   /* utilidades */
#define BAND_FAM_COUNT 3U

static const band_preset_t k_band_presets[] = {
    /* ---- aficionados, Region 1 ---------------------------------------- */
    { "2200 m", "135,7-137,8 kHz",    136500UL,    DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_HAM },
    { "630 m",  "472-479 kHz",        475500UL,    DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_HAM },
    { "160 m",  "1.810-2.000 kHz",    1840000UL,   DEMOD_MODE_LSB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "80 m",   "3.500-3.800 kHz",    3750000UL,   DEMOD_MODE_LSB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "60 m",   "5.351-5.367 kHz",    5357000UL,   DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "40 m",   "7.000-7.200 kHz",    7150000UL,   DEMOD_MODE_LSB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "30 m",   "10.100-10.150 kHz",  10120000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "20 m",   "14.000-14.350 kHz",  14250000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "17 m",   "18.068-18.168 kHz",  18130000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "15 m",   "21.000-21.450 kHz",  21250000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "12 m",   "24.890-24.990 kHz",  24950000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "10 m",   "28,0-29,7 MHz",      28400000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "6 m",    "50-52 MHz",          50150000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { "2 m",    "144-146 MHz",        145500000UL, DEMOD_MODE_NFM, BAND_STEP_12K5, BAND_FAM_HAM },

    /* ---- radiodifusion ------------------------------------------------ */
    { "OL",     "148-284 kHz",        198000UL,    DEMOD_MODE_AM,  BAND_STEP_1K,   BAND_FAM_BCST },
    { "OM",     "526-1.606 kHz",      1000000UL,   DEMOD_MODE_AM,  BAND_STEP_1K,   BAND_FAM_BCST },
    { "120 m",  "2.300-2.495 kHz",    2400000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "90 m",   "3.200-3.400 kHz",    3300000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "75 m",   "3.900-4.000 kHz",    3950000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "60 m",   "4.750-5.060 kHz",    4900000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "49 m",   "5.800-6.200 kHz",    6000000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "41 m",   "7.200-7.450 kHz",    7300000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "31 m",   "9.400-9.900 kHz",    9600000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "25 m",   "11.600-12.100 kHz",  11800000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "22 m",   "13.570-13.870 kHz",  13700000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "19 m",   "15.100-15.830 kHz",  15400000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "16 m",   "17.480-17.900 kHz",  17650000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "15 m",   "18.900-19.020 kHz",  18950000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "13 m",   "21.450-21.850 kHz",  21600000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { "11 m",   "25.670-26.100 kHz",  25800000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },

    /* ---- utilidades ---------------------------------------------------- */
    { "NDB",    "190-535 kHz",        350000UL,    DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_UTIL },
    { "NAVTEX", "518 kHz",            518000UL,    DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_UTIL },
    { "Horaria","2,5 / 5 / 10 MHz",   10000000UL,  DEMOD_MODE_AM,  BAND_STEP_1K,   BAND_FAM_UTIL },
    { "CB",     "26,965-27,405 MHz",  27185000UL,  DEMOD_MODE_AM,  BAND_STEP_10K,  BAND_FAM_UTIL },
    { "FM",     "87,5-108 MHz",       100000000UL, DEMOD_MODE_WFM, BAND_STEP_100K, BAND_FAM_UTIL },
    { "Aérea",  "108-137 MHz",        118000000UL, DEMOD_MODE_AM,  BAND_STEP_25K,  BAND_FAM_UTIL },
    { "Meteo",  "137-138 MHz",        137500000UL, DEMOD_MODE_WFM, BAND_STEP_12K5, BAND_FAM_UTIL },
    { "Marina", "156-162 MHz",        156800000UL, DEMOD_MODE_NFM, BAND_STEP_12K5, BAND_FAM_UTIL },
    { "VHF alta","162-174 MHz",       165000000UL, DEMOD_MODE_NFM, BAND_STEP_12K5, BAND_FAM_UTIL }
};

static const char *const k_band_familias[BAND_FAM_COUNT] = {
    "Aficionados", "Radiodifusión", "Utilidades"
};
#define BAND_PRESET_COUNT (sizeof(k_band_presets) / sizeof(k_band_presets[0]))
/* s_menu_band_tiles ELIMINADO 22/09/2026: la pantalla de bandas ya no usa
 * widgets de ui.c, la dibuja ui_bands.c y resuelve sus toques ui_grid_hit().
 * Con la lista nueva habrian sido 39 ui_button_t de los que solo 12 se ven a
 * la vez. */


/*
 * --- Encoder-driven volume/backlight/scale state ------------------------
 *
 * s_encoder_target selects what the tuning encoder currently controls:
 * TUNE (default, existing behavior, untouched), VOLUME (DAC digital
 * volume via aic3204_set_volume_db()), BACKLIGHT (PWM brightness via
 * backlight_set_percent() - see backlight.h), or SCALE (the
 * spectrum/waterfall vertical dB range, added 31/07/2026 alongside
 * BACKLIGHT's own addition - see the SPECTRUM SCALE block below).
 *
 * Was a plain uint8_t boolean (s_volume_mode) before BACKLIGHT
 * existed - an enum instead of stacking independent flags keeps "what
 * does the knob do right now" a single, unambiguous piece of state
 * (no risk of two targets ending up "on" at once). The VOL button
 * jumps straight to TUNE<->VOLUME (unchanged). BACKLIGHT/SCALE/
 * SQUELCH are now selected by tapping their tile in the settings menu
 * screen (menu_screen_open(), reached via MENU) instead of cycling
 * MENU repeatedly - see s_menu_screen's declaration comment for why
 * that changed 31/07/2026. VOL still jumps straight to VOLUME/TUNE
 * regardless of what's selected in the menu, same as before.
 *
 * s_volume_db_x2's actual declaration (and the VOLUME_STEP_X2/MIN/MAX
 * macros) moved up near s_tune_hz, 18/08/2026 - see the comment
 * there. This comment block (0.5dB-native-units reasoning, starting
 * at 0dB to match the captured baseline) still applies unchanged.
 *
 * (encoder_target_t itself is now declared earlier in this file -
 * see the comment right before the settings-menu block above - since
 * s_menu_detail_target needed the type before this point.)
 */

static void menu_detail_show(encoder_target_t target);
/* Estado de la pantalla de un ajuste - ver su bloque mas abajo. Declarado
 * aqui porque menu_detail_value_redraw() esta antes en el fichero. */
static ui_det_state_t s_det;
static char           s_det_val[20];
static int8_t         s_det_press = -1;
static void det_sync(void);
static void menu_step_preset_callback(void *widget, ui_event_t event, void *user_data);
static void menu_mode_preset_callback(void *widget, ui_event_t event, void *user_data);
static void encoder_inject_detents(int32_t d);
static void encoder_inject_press(void);

/*
 * Geometria. El valor se pinta centrado a escala 6 en la banda
 * MENU_DETAIL_VALUE_CLEAR_Y..+H; los dos botones van a los lados, fuera de
 * esa banda por la izquierda y la derecha, asi que no hace falta tocar el
 * redibujado del valor.
 */
#define MENU_DETAIL_PM_W   150
#define MENU_DETAIL_PM_H   120
#define MENU_DETAIL_PM_Y   180
#define MENU_DETAIL_PM_X0   16
#define MENU_DETAIL_PM_X1  (uint16_t)(MENU_AREA_W - MENU_DETAIL_PM_W - 16)
#define MENU_DETAIL_ALT_W  180
#define MENU_DETAIL_ALT_H   44
#define MENU_DETAIL_ALT_X  (uint16_t)((MENU_AREA_W - MENU_DETAIL_ALT_W) / 2)
#define MENU_DETAIL_ALT_Y  306


static encoder_target_t s_encoder_target = ENCODER_TARGET_TUNE;

/* s_volume_target_last_ms - added 26/08/2026, project owner report:
 * VOL is a bottom-bar TOGGLE (demo_button_callback(), s_btn_vol), not
 * a menu detail view - it has no EXIT tile to end the adjustment, so
 * without this it stayed "hot" (encoder still adjusting volume)
 * indefinitely until the user pressed VOL again or long-pressed the
 * knob. g_msticks timestamp of the last VOLUME-target activity
 * (entering the mode, or turning the knob while in it) -
 * tune_encoder_poll()'s ENCODER_TARGET_VOLUME branch reverts to TUNE
 * on its own once VOLUME_TARGET_TIMEOUT_MS passes with no further
 * activity. Deliberately does NOT apply to VOLUME reached via its own
 * menu tile (menu_detail_show(ENCODER_TARGET_VOLUME)) - that's a menu
 * detail like PGA/SCALE/etc and ends via EXIT (menu_screen_close(),
 * see its own comment) same as those, not a timeout; the
 * !s_menu_open guard at the check site is what keeps the two paths
 * from interfering with each other. */
static uint32_t s_volume_target_last_ms = 0U;
#define VOLUME_TARGET_TIMEOUT_MS 4000UL /* "a few seconds" per the project owner - comfortably longer than the pause between two encoder detents while actually adjusting */
/* PGA (analog input gain, MIC_PGA_L/R - see aic3204_set_pga_gain_db())
 * - same 0.5dB-native-units reasoning as s_volume_db_x2 above, just
 * unsigned (0-95, matching the register's 0-47.5dB range, no cut
 * direction). Starts at 40 (20.0dB) - the byte-exact captured
 * baseline aic3204_phase2_init() leaves the chip at (0x28), so
 * turning the encoder for the first time doesn't jump the gain. */
static int16_t s_pga_gain_db_x2 = CONFIG_PGA_START_DB_X2; /* see config.h */
#define PGA_STEP_X2 2   /* 2 * 0.5dB = 1.0dB per encoder detent */
#define PGA_MIN_X2  0   /* 0.0dB */
#define PGA_MAX_X2  95  /* 47.5dB - see aic3204_set_pga_gain_db()'s field-range note */

/*
 * --- RF-level (analog PGA) auto-AGC -------------------------------------
 *
 * Added 07/08/2026, per the project owner, after ruling out the
 * digital AM/SSB AGC's own math as the cause of "señales muy fuertes
 * saturan la radio del todo" (see demod_am.c's AGC comment - instant
 * attack, no lower gain clamp, mathematically fine for any input
 * short of the ADC itself having already clipped). If the front end
 * clips before ANY of that digital chain runs, no amount of correct
 * downstream gain math can undo it - the fix has to happen at the
 * PGA, upstream of the ADC, which is what this does automatically
 * instead of requiring a manual PGA tweak every time a strong station
 * shows up.
 *
 * s_pga_gain_db_x2 above KEEPS its existing meaning unchanged: it's
 * the user's manual setting via the encoder/PGA menu tile - now
 * treated as a CEILING this auto-AGC never exceeds, not the literal
 * applied gain anymore. The actual applied gain is always
 * (s_pga_gain_db_x2 - s_rf_agc_backoff_x2), computed and pushed to
 * the codec by rf_agc_apply_pga() - the ONLY place that's allowed to
 * call aic3204_set_pga_gain_db() now (see that function's other two
 * former call sites, both switched over to it below).
 *
 * s_rf_agc_enabled is a genuine master on/off switch, independent of
 * the backoff amount - same "toggle separate from the value" shape as
 * s_nr_on/nr_ss_get_enabled(), per the project owner ("como el botón
 * NR"). Defaults OFF: this changes what gets written to the codec
 * autonomously, based on a heuristic (rf_clip_scan()'s threshold/
 * count in demod_am.c) - opt-in like every other automatic-behavior
 * feature in this codebase (NR, NB historically), not a surprise for
 * someone who hasn't asked for it. While off, rf_agc_apply_pga()
 * still runs (from the two "settings changed" call sites) but always
 * computes effective gain = ceiling - 0 = ceiling, i.e. plain manual
 * PGA exactly as before this feature existed.
 *
 * Backoff/recovery ballistics mirror the digital AGC's own philosophy
 * (fast to protect against clipping, slow to back off from that
 * protection) but on a MUCH coarser timescale, because unlike the
 * digital AGC's per-sample math this drives a bit-banged I2C
 * transaction - RF_AGC_ATTACK_COOLDOWN_MS keeps repeated clip
 * detections from spamming the I2C bus faster than the codec/bus can
 * sanely keep up with, and RF_AGC_RELEASE_COOLDOWN_MS is deliberately
 * many seconds (not milliseconds) so recovery only happens once the
 * signal has ACTUALLY dropped for a while, not during the natural
 * peaks/troughs of a single strong signal's own modulation - a fast
 * release here would just pump the gain up and down audibly in sync
 * with the strong station's own audio envelope, the same "release too
 * fast" pumping problem the digital AGC's profile choices already
 * exist to avoid, just one level up the chain.
 *
 * Rin escalation - added same day, per the datasheet's "Analog PGA
 * versus Input Configuration" table (2.3.2.1): PGA backoff alone tops
 * out at RF_AGC_BACKOFF_MAX_X2 (the PGA register's own 0dB floor,
 * relative to whatever Rin is active). If a signal is STILL clipping
 * once backoff is maxed, there's no more PGA-register room - the next
 * escalation step instead switches aic3204_set_input_impedance() up
 * one level (10k->20k->40k), which shifts the WHOLE gain range down
 * another 6dB, and simultaneously gives back RF_AGC_RIN_STEP_X2 of
 * PGA backoff (since the Rin step itself just provided that same 6dB
 * of attenuation) so the transition is a smooth net 6dB step down,
 * not an abrupt 12dB jump, and so there's PGA-register headroom again
 * to keep fine-tuning within the new range. De-escalation mirrors
 * this in reverse once backoff would go negative at the current Rin
 * level. See rf_agc_escalate_rin()/rf_agc_deescalate_rin() and
 * aic3204_set_input_impedance()'s own comment for the
 * *** IMPORTANT UNVERIFIED ASSUMPTION *** about the 20k/40k register
 * values - worth confirming on real hardware before trusting this
 * escalation path blindly.
 */
static uint8_t  s_rf_agc_enabled = 0U;
static int16_t  s_rf_agc_backoff_x2 = 0;      /* 0..RF_AGC_BACKOFF_MAX_X2, in 0.5dB units */
static uint8_t  s_rf_agc_rin_level = 0U;      /* aic3204_rin_t - 0=10k/1=20k/2=40k, see rf_agc_escalate_rin() */

/*
 * SUELO MANUAL DEL ATENUADOR - 22/09/2026, por el dueno del proyecto: "el
 * atenuador en usb 20m no me deja poner nada mas que -6".
 *
 * No le dejaba, y el comentario de menu_tile_att_callback() ya lo decia sin
 * llamarlo fallo: el tile manual y el AGC de RF compartian
 * s_rf_agc_rin_level, asi que en una banda con senales fuertes el automatico
 * se lo volvia a llevar a donde el creyera. Pones 0 dB y al primer recorte
 * sube a -6; pones -12 y en cuanto hay tres segundos de calma baja a -6. La
 * celda se quedaba clavada en -6 sin que el usuario tocara nada.
 *
 * La causa de fondo es una variable contestando a dos preguntas distintas:
 *   - "que ha pedido el usuario"            -> eso es esto, s_att_suelo
 *   - "que tiene el codec puesto AHORA"     -> eso es s_rf_agc_rin_level
 * Separadas, cada una tiene un solo dueno y el automatico deja de pisar al
 * manual.
 *
 * Que significa suelo: el AGC puede atenuar MAS que lo que has pedido -para
 * eso esta, para salvarte de un recorte- pero nunca menos. Al calmarse la
 * banda vuelve a tu ajuste, no a cero. Con el AGC de RF apagado el suelo es
 * sencillamente el atenuador, igual que antes.
 *
 * Es ademas lo que se guarda en CONFIG.CSV (clave att_rin_level, la misma de
 * siempre). Antes se guardaba el valor VIVO, o sea que lo que te encontrabas
 * al encender era donde hubiera dejado el automatico la atenuacion el
 * instante en que toco autoguardar - no lo que tu elegiste.
 */
static uint8_t  s_att_suelo = 0U;
static uint32_t s_rf_agc_last_action_ms = 0U; /* g_msticks at the last backoff/recovery/Rin step */
static uint32_t s_rf_agc_last_clip_ms = 0U;   /* g_msticks at the last DETECTED clip - the release timer's reference point */
#define RF_AGC_STEP_X2              CONFIG_RF_AGC_STEP_X2             /* see config.h */
#define RF_AGC_BACKOFF_MAX_X2       CONFIG_RF_AGC_BACKOFF_MAX_X2      /* see config.h */
#define RF_AGC_RIN_STEP_X2          CONFIG_RF_AGC_RIN_STEP_X2         /* see config.h */
#define RF_AGC_ATTACK_COOLDOWN_MS   CONFIG_RF_AGC_ATTACK_COOLDOWN_MS  /* see config.h */
#define RF_AGC_RELEASE_COOLDOWN_MS  CONFIG_RF_AGC_RELEASE_COOLDOWN_MS /* see config.h */

/* NR strength (Spectral Subtraction, AM/USB/LSB only - see nr_ss.h and
 * demod_am.c's NR INTEGRATION comment). RAW 0-4095, same native units
 * as nr_ss_set_strength() itself (was 0-100% mapped internally until
 * 03/08/2026 - the project owner asked for the raw range directly,
 * once the on/off switch moved to its own separate control (s_nr_on)
 * and this value no longer needed to double as an implicit bypass at
 * its minimum). Starts at 0 - matches nr_ss_init()'s own default. */
static uint16_t s_nr_strength = 50U;
#define NR_STRENGTH_STEP 10U /* per encoder detent - ~32 detents edge
                                 * to edge across the full 0-4095 range,
                                 * similar turn-count feel to PGA/VOLUME's
                                 * own step sizes over their own ranges */
#define NR_STRENGTH_MAX 4095U
/* Backlight step per encoder detent - 5% keeps the full 0-100% range
 * reachable in ~20 detents, coarse enough to actually SEE each step
 * change on the panel while turning the knob (unlike volume's finer
 * 1dB/detent, brightness differences under ~5% are hard to perceive
 * at all - no point spending detents on something invisible). */
#define BACKLIGHT_STEP 5U

/*
 * --- Spectrum/waterfall vertical scale (dB) -----------------------------
 *
 * s_db_min/s_db_max replace what used to be the compile-time
 * constants SDR_DB_MIN/SDR_DB_MAX (still UNCALIBRATED - the "dB"
 * value comes from a bit-manipulation log2 approximation in fft.c,
 * not a referenced measurement - that caveat still applies, only the
 * "compile-time constant" part changed) with live, encoder-adjustable
 * state - 31/07/2026, per the project owner: with the original
 * -10..90dB (100dB) default range, most of a typical signal's
 * dynamic range sits flat near the floor, wasting vertical screen
 * real estate that narrowing the range makes available to the part
 * that actually moves.
 *
 * Reached by tapping the SCALE tile in the settings menu screen
 * (menu_screen_open(), see s_menu_screen's declaration comment - this
 * used to be a MENU-button cycle position, replaced 31/07/2026). In
 * SCALE mode the encoder
 * adjusts EITHER db_min OR db_max - s_scale_adjust_max selects which,
 * TOGGLED BY THE ENCODER BUTTON itself (repurposed here exactly the
 * way it's already repurposed to cycle the tune step in VOLUME/
 * BACKLIGHT mode - see tune_encoder_poll()). SPECTRUM_DB_MIN_GAP
 * stops the two bounds from crossing or collapsing the visible range
 * to something degenerate.
 */
static float s_db_min = 0.0f; /* same starting point as the old SDR_DB_MIN */
static float s_db_max = 90.0f;  /* same starting point as the old SDR_DB_MAX */
static uint8_t s_scale_adjust_max = 0U; /* 0 = knob moves db_min, 1 = moves db_max */
#define SPECTRUM_DB_STEP     2.0f   /* dB per encoder detent */
/* El suelo, el techo y el hueco minimo de la escala vienen de
 * spec_agc.h, con la autoescala que los usa. Estuvieron aqui duplicados
 * hasta el 22/09/2026; lo que pasa con un numero repetido en dos sitios
 * ya lo ha pagado este fichero hoy con la tabla de modos. */
#define SPECTRUM_DB_FLOOR   SPEC_AGC_DB_FLOOR
#define SPECTRUM_DB_CEIL    SPEC_AGC_DB_CEIL
#define SPECTRUM_DB_MIN_GAP SPEC_AGC_DB_MIN_GAP

/*
 * --- Spectrum AGC, added 01/09/2026 -------------------------------------
 *
 * Per the project owner: auto-track s_db_min/s_db_max from the actual
 * spectrum instead of only ever setting them by hand. Manual SCALE
 * adjustment (tune_encoder_poll()'s ENCODER_TARGET_SCALE handler)
 * still works exactly as before, and auto-disables this if it was on,
 * so turning the knob always means "I'm taking over," never "fight
 * the auto-tracker."
 *
 * Reuses s_db_frame[] - the exact same per-frame FFT data already
 * computed for the panadapter/waterfall and the SNR readout, no new
 * signal path. Each frame (same "!s_menu_open" gate as s_db_frame
 * itself - see sdr_spectrum_waterfall_tick()'s own comment on why):
 * take the CURRENT frame's own min across all FFT_BINS_IQ bins as the
 * floor reference, pad it down by SPEC_AGC_FLOOR_MARGIN_DB (so the
 * noise floor itself doesn't sit clipped right at the bottom edge),
 * clamp to the same SPECTRUM_DB_FLOOR/CEIL/MIN_GAP bounds manual
 * adjustment already respects, then move s_db_min/s_db_max toward
 * that target with a SLOW exponential smoothing factor
 * (SPEC_AGC_SMOOTH_ALPHA) rather than snapping straight to it - a
 * strong signal appearing or fading should widen or narrow the
 * visible range gradually, not visibly jump the whole waterfall's
 * color mapping every single frame.
 *
 * *** 01/09/2026, CEILING FIXED same day - real hardware feedback ***
 * - the first version set the ceiling to frame_max + a small fixed
 * margin (SPEC_AGC_CEIL_MARGIN_DB), which looked fine with a strong
 * signal present but rode up uncomfortably close to the very top of
 * the display with no strong signal at all: plain background noise
 * has very little spread between its own min and max, so "max + a
 * few dB" ends up hugging just above the noise floor itself, leaving
 * almost no visible headroom even though the display is nowhere near
 * SPECTRUM_DB_CEIL. Fixed by giving the ceiling a GUARANTEED minimum
 * distance above the floor (SPEC_AGC_MIN_SPAN_DB) regardless of
 * frame_max, only widening further than that when an actual signal
 * needs the room: target_max = max(target_min + MIN_SPAN_DB,
 * frame_max + CEIL_MARGIN_DB). With no strong signals, the display now
 * always keeps at least MIN_SPAN_DB of headroom above the floor.
 * *** 01/09/2026, DEFAULT FLIPPED TO ON, same day *** - per the
 * project owner, after bench-testing the ceiling fix above: "me gusta"
 * - on by default now, departing from this project's usual "new
 * control defaults to the old behavior" rule for once, since the
 * owner explicitly asked for it after confirming the behavior on real
 * hardware. Manual SCALE adjustment (tune_encoder_poll()'s
 * ENCODER_TARGET_SCALE handler) still auto-disables this the same way
 * regardless of the default, so turning the knob always means "I'm
 * taking over," never "fight the auto-tracker."
 */
static uint8_t s_spec_agc_enabled = 1U;
/* Los cuatro numeros de la autoescala vivian aqui y ahora estan en
 * spec_agc.h, con el codigo que los usa. Dejarlos duplicados en los dos
 * sitios es como se acaba con dos versiones distintas del mismo ajuste;
 * este fichero ya ha pagado ese precio hoy con la tabla de modos. */

/*
 * --- Squelch (AM + NFM, encoder target) -------------------------------
 *
 * Reached by tapping the SQUELCH tile in the settings menu screen
 * (menu_screen_open() - this used to be a 4th MENU-button cycle
 * position, replaced 31/07/2026, same as SCALE's - see
 * s_menu_screen's declaration comment). The encoder just adjusts
 * demod_am's threshold directly via
 * demod_am_set_squelch_db()/get - no separate mirrored state
 * needed here, unlike s_db_min/s_db_max (which don't have a demod_am
 * equivalent to read from). The encoder BUTTON does the same "still
 * cycles the tune step" courtesy as VOLUME/BACKLIGHT (see
 * tune_encoder_poll()) - there's no second sub-value to toggle here
 * the way SCALE has LO/HI.
 */
#define SQUELCH_DB_STEP 2.0f /* dB per encoder detent - same granularity as SCALE */
#define SQUELCH_DB_FLOOR (-10.0f) /* matches demod_am.h's "OFF" default exactly - turning
                                     * all the way down returns to today's un-squelched
                                     * NFM behavior, not some arbitrary very-quiet floor. */
#define SQUELCH_DB_CEIL 60.0f /* generous headroom above any realistic in-channel signal
                                * level this metric could read - see demod_am.h's
                                * UNCALIBRATED note; this is a display/encoder-range
                                * bound, not a claim about what's physically meaningful. */

/*
 * --- Spectrum temporal smoothing (encoder target) -----------------------
 *
 * s_spectrum_smooth_alpha used to be a #define local to
 * sdr_spectrum_waterfall_tick() - promoted to file-scope state
 * 31/07/2026 per the project owner, so it can be adjusted live via
 * ENCODER_TARGET_SMOOTH (reached through the settings menu screen's
 * SMOOTH tile, see menu_screen_open()) instead of only at compile
 * time. See sdr_spectrum_waterfall_tick()'s TEMPORAL smoothing comment
 * for the full explanation of what this value does.
 *
 * Presented to the user as a 0-95% "history weight" (see
 * menu_detail_value_redraw()) rather than the raw 0.0-0.95 float -
 * more intuitive than a bare decimal, and matches how the other
 * percent-style controls (BACKLIGHT) already read. SPECTRUM_SMOOTH_MAX
 * stops short of 1.0 deliberately - true 1.0 would freeze the display
 * forever, never blending in a new frame at all.
 */
static float s_spectrum_smooth_alpha = 0.75f; /* same default the #define always used */
#define SPECTRUM_SMOOTH_STEP 0.05f /* 5 percentage points per encoder detent */
#define SPECTRUM_SMOOTH_MIN 0.0f
#define SPECTRUM_SMOOTH_MAX 0.95f

/* CONFIG.CSV's spectrum_smooth_pct field (07/09/2026, settings.c)
 * stores the same 0-95% "history weight" already shown to the user,
 * not the raw 0.0-0.95 float - this is the one conversion point both
 * settings_poll()/settings_save_now() call sites and the boot-time
 * apply block use, so the rounding rule only lives in one place. */
static uint8_t spectrum_smooth_pct_for_save(void)
{
    return (uint8_t)((s_spectrum_smooth_alpha * 100.0f) + 0.5f);
}

/* TOP BAR readout placement (see the RADIO UI LAYOUT block). The
 * frequency is the star: scale 5 (30px wide x 35px tall per char),
 * 11-char field "XXX.XXX.XXX" = 330px, left-anchored. Mode sits to
 * its right at scale 3; step and volume stack in two scale-2 rows
 * next; time (scale 3) and the battery gauge live over the right
 * status column. */
#define FREQ_FIELD_CHARS 11
#define STEP_Y 8
#define TIME_X 690
#define TIME_Y 8
#define BATT_Y 40
#define BATT_H 16

/* Speaker-enabled indicator (07/09/2026, per the project owner) -
 * sits in the gap between the badge row (ends at BADGE_COL(6)+BADGE_W
 * = 649, see BADGE_COL's own comment) and the battery gauge (BATT_X =
 * 690) - 41px available. Proportions (width:height, box width:box
 * height:horn width) match the real, widely-used "speaker/volume"
 * glyph (e.g. Feather icons' "volume-1": box 4x6, horn 5 wide, full
 * height 14, all in a 24-tall viewBox - a 9:14 width:height ratio,
 * TALLER than wide) rather than an invented shape - see
 * speaker_icon_draw()'s own comment for why that matters. Vertically
 * centered in the status strip (like BADGE_Y0), not pinned to the
 * battery's shorter BATT_Y/H, since this icon is taller than the
 * battery gauge by design. */
#define SPK_ICON_W 16
#define SPK_ICON_H 24
#define SPK_ICON_X 661
#define SPK_ICON_Y (uint16_t)(STATUS_STRIP_Y + (STATUS_STRIP_H - SPK_ICON_H) / 2U)

/*
 * Renders `hz` as a fixed 11-char field "XXX.XXX.XXX" with thousands
 * separators, right-aligned, space-padded (e.g. " 90.800.000",
 * "  7.100.000"). Fixed width keeps gfx_text() repainting the whole
 * field every time, so no ghost digits survive a change in magnitude.
 * Manual formatting - same policy as the itoa above, no sprintf.
 * `buf` must hold FREQ_FIELD_CHARS + 1 bytes.
 */
/*
 * Renders db_x2 (native 0.5dB units) as a fixed 11-char field,
 * right-aligned, same geometry/width as tune_freq_format() so it can
 * reuse the frequency readout's position: e.g. "   +12.0dB",
 * "   -6.5dB", "    +0.0dB". Manual formatting, no sprintf, same
 * convention as tune_freq_format(). `buf` must hold
 * FREQ_FIELD_CHARS + 1 bytes.
 */
/*
 * Igual que volume_format(), pero para los sitios que ya dibuja gfx2 (la
 * pastilla del mando y la barra de acciones): "-25,0 dB" en vez de
 * "-25.0DB".
 *
 * Las mayusculas y el punto de volume_format() no eran una eleccion de
 * estilo: la fuente 5x7 del firmware tiene las minusculas practicamente
 * ilegibles (hay nueve pares que se diferencian en un pixel), asi que todo
 * se escribia en mayusculas. Las pantallas que siguen usando esa fuente
 * mantienen volume_format() tal cual; las que ya no, usan esta.
 */
static void volume_format_ui(int16_t db_x2, char *buf)
{
    uint16_t mag = (db_x2 < 0) ? (uint16_t)(-(int32_t)db_x2) : (uint16_t)db_x2;
    uint16_t whole = mag / 2U;
    uint16_t tenth = (mag % 2U) * 5U;
    uint8_t i = 0;

    if (db_x2 < 0) { buf[i++] = '-'; }
    if (whole >= 10U) { buf[i++] = (char)('0' + (whole / 10U)); }
    buf[i++] = (char)('0' + (whole % 10U));
    buf[i++] = ',';
    buf[i++] = (char)('0' + tenth);
    buf[i++] = ' ';
    buf[i++] = 'd';
    buf[i++] = 'B';
    buf[i] = '\0';
}

/* Same fixed-7-char-field, sign-then-digits-then-unit style as
 * volume_format() above, but for the spectrum SCALE readout: whole
 * dB only (SPECTRUM_DB_STEP is a whole number, no decimal needed),
 * range -40..120 so up to 3 digits. `buf` must hold FREQ_FIELD_CHARS+1
 * bytes, same as volume_format(). */
static void spectrum_db_format(int16_t db_i, char *buf)
{
    int8_t pos = FREQ_FIELD_CHARS;
    uint16_t mag;
    uint8_t negative = (db_i < 0) ? 1U : 0U;

    mag = negative ? (uint16_t)(-(int32_t)db_i) : (uint16_t)db_i;

    buf[pos] = '\0';
    buf[--pos] = 'B';
    buf[--pos] = 'D';
    do {
        if (pos > 0) {
            buf[--pos] = (char)('0' + (mag % 10U));
        }
        mag /= 10U;
    } while (mag > 0U && pos > 0);
    if (pos > 0) {
        buf[--pos] = negative ? '-' : '+';
    }
    while (pos > 0) {
        buf[--pos] = ' ';
    }
}

/*
 * --- Top-bar / right-column readout drawing ---------------------------
 *
 * Each readout has its own draw function, repainting a fixed-width
 * field at a fixed position (no ghost chars). All draw over the
 * DARKGRAY top bar / BLACK right column.
 */
/* ETAPA 2: este readout pasa a ui_top.c. Se conserva el nombre y todas
 * las llamadas existentes, y solo cambia lo que hace por dentro - asi el
 * cambio se revierte tocando una funcion, sin perseguir call sites. */
static void mode_display_draw(void)
{
    top_sync();
    ui_top_draw(&s_top);
}

/*
 * sam_current_ppm_error() - 26/08/2026, replaces the old raw-Hz
 * readout (see sam_calib_display_draw()'s history below): a Hz offset
 * on its own isn't practical to act on - it means something different
 * at every tuned frequency, so you had to do the ppm = offset_hz /
 * tuned_hz * 1e6 division by hand before it meant anything. This
 * does that division once, here, so both the live on-screen readout
 * AND the CAL PPM tile's actual correction (menu_tile_cal_ppm_callback()
 * below) read from exactly the same number - no risk of the display
 * and the applied correction ever disagreeing.
 *
 * Valid only while DEMOD_MODE_SAM is selected and tuned to a station
 * of known carrier frequency (SAM's PLL locks onto whatever carrier
 * is strongest in-band, known or not) - callers must check
 * demod_am_get_mode() themselves, same as the old Hz readout did.
 * s_tune_hz is always > 0 (TUNE_MIN_HZ enforces that), so no
 * divide-by-zero guard needed.
 */
static float sam_current_ppm_error(void)
{
    return (demod_am_get_sam_carrier_hz() / (float)s_tune_hz) * 1.0e6f;
}

/*
 * MS5351 PPM calibration readout (21/08/2026, reworked 26/08/2026 to
 * show ppm instead of raw Hz - see sam_current_ppm_error()'s comment
 * for why). Shown right under the mode label, only while SAM is
 * selected (blanked otherwise, so switching away from SAM doesn't
 * leave a stale reading on screen). Manual formatting, no sprintf -
 * same convention as volume_format() above.
 */
/*
 * X/Y (26/08/2026, moved from under MODE - see this function's own
 * comment for the ppm math, unchanged here) - the MODE_X position
 * put this readout's 140px-wide field right on top of VOL_X/VOL_Y's
 * own text (both around x=430-490, this one at y=47-63 landing
 * inside VOL_Y=38's row), so the two overwrote each other on the
 * real hardware even though they never collided during review. Moved
 * to the free gap between STEP's field (ends around x=550) and
 * TIME_X (690) on the SAME row as STEP (y=STEP_Y) instead - nothing
 * else occupies that span, and keeping the row parallels how VOL
 * sits right under STEP: this now sits right of STEP the same way
 * VOL sits under it. */
#define SAM_CALIB_X (594)
#define SAM_CALIB_Y STEP_Y
/* ETAPA 2: ya no dibuja. Actualiza el texto y repinta la cabecera SOLO si
 * cambio - esta funcion se llama a cada tick del espectro (30/s) y un
 * repintado completo de cabecera en cada uno seria ~20% del tiempo de bus
 * para un numero que se mueve despacio.
 *
 * Y sobre todo: en los modos que NO son SAM ya no rellena nada. La version
 * anterior pintaba ahi un rectangulo de 70x16 con el gris de la barra para
 * "limpiar su hueco", lo que era invisible sobre un fondo plano pero dejaba
 * un cuadrado gris a la izquierda del reloj en cuanto la cabecera paso a
 * tener degradado. */
static void sam_calib_display_draw(void)
{
    char nuevo[16];
    uint8_t i;

    if (demod_am_get_mode() != DEMOD_MODE_SAM) {
        nuevo[0] = '\0';
    } else {
        float ppm_f = sam_current_ppm_error();
        uint8_t negative = (ppm_f < 0.0f) ? 1U : 0U;
        float mag_f = negative ? -ppm_f : ppm_f;
        uint16_t whole = (uint16_t)mag_f;
        uint16_t tenth = (uint16_t)((mag_f - (float)whole) * 10.0f + 0.5f);
        int8_t pos = 15;

        if (tenth >= 10U) { tenth = 0U; whole++; }
        nuevo[pos] = '\0';
        nuevo[--pos] = 'M'; nuevo[--pos] = 'P'; nuevo[--pos] = 'P';
        nuevo[--pos] = ' ';
        nuevo[--pos] = (char)('0' + tenth);
        nuevo[--pos] = ',';
        do {
            if (pos > 0) { nuevo[--pos] = (char)('0' + (whole % 10U)); }
            whole /= 10U;
        } while (whole > 0U && pos > 0);
        if (pos > 0) { nuevo[--pos] = negative ? '-' : '+'; }
        /* compactar al principio del buffer */
        for (i = 0U; nuevo[pos + i] != '\0'; i++) { nuevo[i] = nuevo[pos + i]; }
        nuevo[i] = '\0';
    }

    for (i = 0U; i < sizeof(s_top_sam); i++) {
        if (s_top_sam[i] != nuevo[i]) { break; }
        if (nuevo[i] == '\0') { return; }      /* identico: nada que hacer */
    }
    for (i = 0U; i < sizeof(s_top_sam); i++) {
        s_top_sam[i] = nuevo[i];
        if (nuevo[i] == '\0') { break; }
    }
    top_sync();
    ui_top_draw(&s_top);
}

/*
 * Not a ui_button_t on purpose, even though it's tappable (see
 * demo_touch_poll()'s s_freq_tap_active) - ui_button_draw() always
 * fills its whole rect with btn->bg on every press/release, which
 * would blank these digits on every touch and need patching right
 * back in the callback. Easier and glitch-free to keep this a plain
 * gfx_text() readout and do the hit-test as a raw coordinate check
 * outside the ui_screen framework instead, same treatment the
 * spectrum drag-to-tune zone already gets.
 */
/* ETAPA 2: este readout pasa a ui_top.c. Se conserva el nombre y todas
 * las llamadas existentes, y solo cambia lo que hace por dentro - asi el
 * cambio se revierte tocando una funcion, sin perseguir call sites. */
static void freq_display_draw(void)
{
    top_sync();
    ui_top_draw(&s_top);
}

/* ETAPA 2: este readout pasa a ui_top.c. Se conserva el nombre y todas
 * las llamadas existentes, y solo cambia lo que hace por dentro - asi el
 * cambio se revierte tocando una funcion, sin perseguir call sites. */
static void step_display_draw(void)
{
    top_sync();
    ui_top_draw(&s_top);
}

/*
 * Shared "what does the knob control right now" readout at
 * VOL_X/VOL_Y - shows either the volume or the backlight percent
 * depending on s_encoder_target, inverted colors while that target is
 * the active one (same visual language as step_display_draw()'s
 * highlight, just per-target instead of per-button). Renamed
 * 31/07/2026 from volume_display_draw() now that this row shows more
 * than just volume - same call sites, same row.
 *
 * Both branches format into a FIXED 7-char value field (same
 * reasoning as tune_freq_format()'s comment: fixed width means
 * gfx_text() repaints the whole field every time, so switching
 * VOL<->BACKLIGHT never leaves a ghost digit from whichever one was
 * showing before - a 4-char "100%" over a stale 7-char "+00.0DB"
 * would otherwise leave 3 uncleared pixels' worth of the old text).
 */
/* ETAPA 2: lo sustituye la pastilla del mando de la barra de estado.
 *
 * Esta funcion pintaba en VOL_X/VOL_Y (478, 38) con la fuente 5x7 a escala 2
 * y colores invertidos - encima de la cabecera nueva, que ahi tiene degradado.
 * Ademas era redundante: mostraba BL/HI-LO/SQL/SMH/PGA/NR/VOL con su valor,
 * que es exactamente lo que dice la pastilla, y con las palabras enteras en
 * vez de abreviaturas de tres letras.
 *
 * Mismo patron que sam_calib_display_draw(): se conserva el nombre y las
 * llamadas, y solo cambia lo que hace por dentro. */
static void aux_row_display_draw(void)
{
    top_sync();
    ui_top_draw(&s_top);
}

/*
 * Time-of-day slot. There is NO RTC configured in this project (and
 * no battery-backed clock domain has been brought up) - see
 * s_time_offset_min's own comment for exactly what this shows instead
 * and its one real limitation (doesn't survive a power cycle). When/
 * if the GD32F450's RTC gets configured (needs LXTAL bring-up +
 * calendar init), swap the source here for the real one and drop
 * s_time_offset_min/the SET keypad entirely - nothing else about this
 * function's shape needs to change.
 *
 * s_time_offset_min: minutes added to uptime (g_msticks/60000) before
 * wrapping to a 24h wall-clock read, i.e. displayed = (uptime_min +
 * s_time_offset_min) % 1440 - added 08/09/2026, per the project
 * owner: tap the clock readout to type in the actual HH:MM (see
 * menu_time_keypad_show()) instead of only ever showing raw uptime.
 * This is a SOFTWARE offset only, not a real clock - it does not tick
 * while powered off and is NOT persisted to CONFIG.CSV (persisting it
 * would just silently show the wrong time after any real-world delay
 * across a power cycle, which is worse than plainly resetting to
 * uptime=0's offset and needing a re-set - see the project's own
 * settings.h precedent for "don't persist something that would be
 * actively misleading"). Starts at 0 (matches the old raw-uptime
 * behavior) until the first SET.
 */
static uint32_t s_time_offset_min = 0U;

/*
 * Geometria del reloj para gfx2. Cubre la zona que ocupaba el texto a escala
 * 3 (TIME_X=690, TIME_Y=8, 5 caracteres) con algo de margen, y se queda por
 * encima de TIME_TAP_Y2 (36) para no invadir el indicador de bateria, que
 * vive mas abajo en esta misma barra.
 */
#define TIME2_X 684
#define TIME2_Y   4
#define TIME2_W (GFX2_W - TIME2_X)
#define TIME2_H  30

/* Fondo de la barra superior. GFX_COLOR_DARKGRAY es 0x4208 en RGB565, que
 * expandido a 8 bits por canal es 0x424242: hay que rellenar con EXACTAMENTE
 * ese valor para que la franja recompuesta no se note contra el resto. */
#define TOPBAR_BG 0x424242u


static void time_display_draw(void)
{
    extern volatile uint32_t g_msticks;
    uint32_t total_min = ((g_msticks / 60000UL) + s_time_offset_min) % 1440UL; /* 1440 = minutes/day - wraps the wall-clock read at 24h */
    uint32_t hh = total_min / 60UL;
    uint32_t mm = total_min % 60UL;
    char buf[6];

    buf[0] = (char)('0' + (hh / 10UL));
    buf[1] = (char)('0' + (hh % 10UL));
    buf[2] = ':';
    buf[3] = (char)('0' + (mm / 10UL));
    buf[4] = (char)('0' + (mm % 10UL));
    buf[5] = '\0';

    /* ETAPA 2: el reloj ya no se pinta solo - forma parte de la cabecera.
     * Se guarda el texto y se repinta la cabecera entera, que con la banda
     * compositora cuesta un unico volcado por banda. */
    {
        uint8_t i;
        for (i = 0U; i < sizeof(s_time_buf); i++) { s_time_buf[i] = buf[i]; }
    }
    s_time_text = s_time_buf;
    top_sync();
    ui_top_draw(&s_top);
}

/*
 * Battery gauge, backed by VBAT/ADC0 channel 18 (see battery.h/.c) -
 * 31/07/2026, replacing the earlier "no ADC channel identified"
 * placeholder now that the VBAT pin + diode-drop compensation is
 * known. battery_get_percent() never returns >100, so the "--"
 * unknown-placeholder path below is now dead in practice - left in
 * place rather than deleted, in case battery_init() ever needs a
 * "not wired / read failed" escape hatch again (e.g. a future board
 * revision that removes the divider).
 */
/* Formats millivolts as a fixed 5-char "XX.XV" field (e.g. " 3.9V",
 * "12.4V") - always the same width regardless of magnitude, so
 * gfx_text() fully repaints it every refresh with no ghost digits
 * (same fixed-width reasoning as tune_freq_format()/volume_format()).
 * Deliberately simpler than those two: no sign, and the whole-volt
 * part is capped at 2 digits (99V) - plenty for any battery pack this
 * board is realistically going to have - so straight positional
 * indexing reads clearer here than their right-to-left digit-peeling
 * loops. `buf` must hold 6 bytes. */
/*
 * Battery gauge + voltage readout. The voltage text sits just right
 * of the icon (BATT_W was narrowed to make room - see its comment)
 * and shares the SAME green/orange/red threshold color as the fill
 * bar, so a glance at the color alone still tells the story even
 * before reading the number. Uses battery_get_millivolts() directly
 * (not the already-floor/ceiling-clamped percent) since the raw
 * voltage is the more useful number to actually read off, and it's
 * a second, independent ADC conversion each redraw rather than a
 * derived value - harmless, this is a housekeeping readout polled a
 * few times a second at most, not the demod ISR.
 */
/* ETAPA 2: este readout pasa a ui_top.c. Se conserva el nombre y todas
 * las llamadas existentes, y solo cambia lo que hace por dentro - asi el
 * cambio se revierte tocando una funcion, sin perseguir call sites. */
static void battery_display_draw(void)
{
    top_sync();
    ui_top_draw(&s_top);
}

/*
 * Simple speaker glyph - box (the driver) + a flat-right-edge
 * trapezoid "horn", no sound-wave arcs (per the project owner:
 * "hazlo mas facil" after seeing a fancier waves version).
 *
 * Proportions (box_w=7, box_h=10, full W=16, full H=24) are scaled
 * from the real, standard "speaker/volume" glyph most icon sets use
 * (e.g. Feather icons' "volume-1": box 4x6, horn width 5, full height
 * 14 in a 24-tall viewBox - a 9:14, TALLER-than-wide silhouette) -
 * NOT an invented shape. An earlier version of this icon used a
 * wider-than-tall box+horn of its own invention and looked wrong for
 * exactly that reason: matching a real icon's proportions is what
 * makes this read as "speaker" rather than an arbitrary polygon.
 *
 * gfx.h has no filled-triangle/polygon primitive, only
 * rect/line/hline/vline (see gfx_line()'s comment for the one
 * arbitrary-diagonal primitive that DOES exist, unused here), so the
 * horn is built one row at a time via gfx_hline(): each row's LEFT
 * edge is computed from its distance off vertical center, rounded to
 * the nearest pixel (not truncated) to keep the staircase as even as
 * possible - rows within the box's own height sit at the horn's
 * widest point (flush with the box); rows further out taper the LEFT
 * edge rightward the closer they get to the icon's very top/bottom
 * row, closing completely there. Some staircase-stepping on the
 * diagonal is unavoidable without an anti-aliased fill (this display
 * only takes solid RGB565 colors - no alpha blending in gfx.c) - the
 * same stepping any small bitmap icon has; it reads fine at actual
 * screen size and viewing distance even though it looks rough
 * zoomed-in.
 *
 * Lit GFX_COLOR_WHITE when the speaker PA is enabled
 * (s_speaker_pa_enabled - see speaker_pa_set_enabled()), dim
 * GFX_COLOR_DARKGRAY when muted (headphones-only) - white rather than
 * badge_draw()'s usual on-color-per-badge convention, to read as part
 * of the same neutral white-icon language as the battery gauge right
 * next to it, not as a third distinct status color competing with the
 * badge row's green/red/yellow.
 *
 * Fully repaints its own shape (box + every horn row) each call, so
 * toggling states needs no separate background clear first.
 */
/* ETAPA 2: el icono lo sustituye el chip "SIN ALTAVOZ" de la barra de
 * estado, que ademas lleva la palabra y no depende de reconocer un dibujo
 * de 16x24. Esta funcion solo repinta la barra. */
static void speaker_icon_draw(void)
{
    top_sync();
    ui_top_draw_status(&s_top);
}

/*
 * --- Right column: S-meter + status badges ----------------------------
 *
 * S-METER: 12 segments driven by the demodulator's pre-AGC envelope
 * peak (demod_am_get_signal_peak() - instant attack / slow release,
 * proper meter ballistics for free). Mapped from dBFS, NOT calibrated
 * S-units: without a known antenna/frontend gain figure, painting
 * "S9" on it would be an invented number. Segment thresholds span
 * -90..-6dBFS (7dB/segment); the last 3 segments draw red like the
 * classic "over S9" zone. Good relative meter now; calibration (an
 * offset + label change here) can come once a signal generator has
 * been on the antenna jack.
 */
#define SMETER_X    8
#define SMETER_Y    (uint16_t)(STATUS_STRIP_Y + (STATUS_STRIP_H - SMETER_SEG_H) / 2) /* vertically centered in the strip */
#define SMETER_SEGS 12
#define SMETER_SEG_W 8
#define SMETER_SEG_H 20

static uint8_t s_smeter_segs_last = 0xFFU; /* force first draw */

/* ETAPA 2: pasa a ui_top.c. Se conserva el filtro de "si no cambio, no
 * repintes" del original, que ahorra un repintado completo de la franja
 * en la mayoria de los ticks. */
static void smeter_draw(uint8_t segs)
{
    if (segs == s_smeter_segs_last) {
        return;
    }
    s_smeter_segs_last = segs;
    s_top_segs = segs;
    top_sync();
    ui_top_draw_status(&s_top);
}

/* Convert the demod's peak (int16 scale) to lit segments. Uses the
 * same IEEE754 bit-trick log2 approximation as fft.c instead of
 * libm's log10f: this project links without syscall stubs and
 * newlib's log10f drags in __errno (link error) - and a few percent
 * of log error is invisible on a 12-segment meter anyway. Main loop
 * only - never the ISR. */
static float smeter_log2_approx(float x)
{
    union { float f; uint32_t u; } v;
    float y;

    v.f = x;
    y = (float)v.u;
    y *= 1.1920928955078125e-7f; /* 1/2^23 */
    return y - 126.94269504f;
}

/*
 * smeter_dbfs_from_peak() - factored out 01/09/2026 so the raw,
 * unrounded dBFS value (before quantizing into the 12-segment meter)
 * is available on its own - see smeter_dbfs_uart_report()'s comment
 * for why (S-meter/SNR calibration against a real signal generator).
 */
static float smeter_dbfs_from_peak(float peak)
{
    if (peak < 1.0f) {
        peak = 1.0f;
    }
    /* 20*log10(x) = 6.0206*log2(x); dBFS relative to int16 full scale. */
    return 6.0206f * (smeter_log2_approx(peak) - smeter_log2_approx(32767.0f));
}

static uint8_t smeter_segments_from_peak(float peak)
{
    float dbfs = smeter_dbfs_from_peak(peak);
    int32_t segs;

    /* -90dBFS -> 0 segs, 7dB per segment, saturating at 12. */
    segs = (int32_t)((dbfs + 90.0f) / 7.0f);
    if (segs < 0)  { segs = 0; }
    if (segs > SMETER_SEGS) { segs = SMETER_SEGS; }
    return (uint8_t)segs;
}

/*
 * smeter_dbfs_uart_report() - added 01/09/2026, per the project owner:
 * they now have a signal generator with a KNOWN, calibrated dBm
 * output, resolving the earlier objection to calibrating the S-meter/
 * SNR (too many uncontrolled variables chained together with no real
 * reference point - see the conversation this was discussed in). This
 * prints the RAW, unrounded dBFS value the 12-segment meter itself is
 * quantized from, throttled to 1Hz so it's readable on a terminal
 * without flooding it - the 12-segment display alone is far too
 * coarse (7dB per segment) for a real calibration procedure.
 *
 * Calibration procedure this enables: with AGC OFF (a genuine unity-
 * gain bypass, not just a slow setting - see AGC_PROFILE_MANUAL's own
 * comment) and ATT/PGA fixed at a known, noted setting, feed a clean
 * CW/AM tone at a known dBm from the generator into the antenna port,
 * read the settled dBFS value here, and compute offset_db = known_dBm
 * - dbfs. Repeat at a few different levels/frequencies within the
 * same band to check linearity before trusting a single offset value
 * project-wide - do NOT assume linearity holds from one data point.
 * Always called from the same place smeter_draw() already reads
 * demod_am_get_signal_peak() from - the exact same peak value both
 * the visible S-meter and this printout are computed from, so what
 * shows on screen and what's printed here are asking about the
 * identical measurement.
 */
static void smeter_dbfs_uart_report(float peak)
{
    static uint32_t s_next_report_ms = 0U;
    float dbfs;

    if ((int32_t)(g_msticks - s_next_report_ms) < 0) {
        return;
    }
    s_next_report_ms = g_msticks + 1000U;

    dbfs = smeter_dbfs_from_peak(peak);
    /* debug_print_dec() takes uint32_t, no sign handling - and dBFS is
     * essentially always negative for a real signal, so a naive cast
     * would print a huge bogus positive number instead. Sign folded
     * into the label text itself, magnitude via debug_print_dec()'s
     * own "label = value" format - reads as e.g.
     * "smeter dBFS x100 (-, /100 for real dBFS) = 3456". */
    if (dbfs < 0.0f) {
        debug_print_dec("smeter dBFS x100 (-, /100 for real dBFS)", (uint32_t)(-(dbfs * 100.0f)));
    } else {
        debug_print_dec("smeter dBFS x100 (+, /100 for real dBFS)", (uint32_t)(dbfs * 100.0f));
    }
}

/*
 * SMETER_CAL_OFFSET_DB - added 01/09/2026, per the project owner:
 * derived from a real calibration session against an external signal
 * generator with a KNOWN, calibrated dBm output (see the conversation
 * this was worked out in for the full data table). Procedure: AGC
 * OFF (genuine unity-gain bypass), ATT=0dB, PGA=20 (see aic3204_
 * set_input_impedance()/PGA gain settings - THIS OFFSET ONLY APPLIES
 * AT THOSE EXACT SETTINGS, not verified or expected to hold at any
 * other ATT/PGA combination, or with AGC on), fed a clean CW/AM tone
 * at a known dBm across several frequencies and levels, read the raw
 * dBFS via smeter_dbfs_uart_report(), computed offset = dBm - dBFS
 * per point.
 *
 * Result: 6 of 11 tested points clustered tightly around -38.2dB
 * (range -37.62 to -39.60, i.e. within about 1dB of each other) -
 * spanning very different conditions (both LO generation mechanisms
 * below 5MHz, high-band above it, -80/-60dBm input levels) - good
 * evidence this figure is genuinely representative, not a one-off
 * coincidence. -38.2 (the rounded, documented value) is the average
 * of that consistent cluster.
 *
 * KNOWN GAPS, not covered by this single offset - readings in these
 * ranges will be WRONG by a large, inconsistent amount, not just
 * imprecise:
 *   - Above ~-50dBm input (roughly): the receiver visibly compresses
 *     at this PGA/ATT setting (a -40dBm point in the same calibration
 *     session read barely different from the -60dBm point - a 20dB
 *     input change producing ~1dB of output change is saturation, not
 *     signal). This offset assumes small-signal linear behavior.
 *   - 37-60MHz (rf_lpf.c's own "range 2" filter band): both edges
 *     tested (37MHz, 59MHz) read roughly 30dB worse than the general
 *     cluster - a real, still-uninvestigated loss (or a relay/filter
 *     fault) specific to that filter path, not something this offset
 *     can paper over.
 *   - Right at the low-band/high-band LO handoff (~4.8MHz): 4.5MHz
 *     (low-band) matched the good cluster, but 5.5MHz (high-band, only
 *     1MHz higher) read about 20dB worse - suggests a real LO-level
 *     difference between the two generation methods that hasn't been
 *     tracked down yet, independent of this offset.
 * Applied UNCONDITIONALLY anyway ("adelante con el resto lo mas
 * simple", per the project owner) rather than trying to detect and
 * special-case any of the above - the tradeoff being: expect this
 * reading to be genuinely wrong (not just imprecise) in those specific
 * conditions, until each is separately investigated and, if needed,
 * given its own correction.
 */
#define SMETER_CAL_OFFSET_DB (-38.2f)
#define SMETER_CAL_ATT_DB     0.0f  /* Rin=10k (index 0 of k_att_labels) - the ATT setting active during calibration */
#define SMETER_CAL_PGA_DB    20.0f  /* the PGA setting active during calibration - see SMETER_CAL_OFFSET_DB's own comment */

/* Same 0/-6/-12dB values as k_att_labels' own indices (AIC3204_RIN_10K/
 * 20K/40K) - duplicated here rather than derived from k_att_labels
 * itself (a display STRING, not a number) since this needs the actual
 * dB value to compute with, not just a label to print. */
static const float k_att_db_values[3] = { 0.0f, -6.0f, -12.0f };

#define SNR_X (uint16_t)(SMETER_X + SMETER_SEGS * (SMETER_SEG_W + 1) + 12) /* right after the meter, plus a gap - name kept as "SNR_X/Y" even though this slot no longer shows SNR, since BADGE_ROW_X0 and others already reference it by this name */

/*
 * smeter_dbm_update_and_draw() - added 01/09/2026, per the project
 * owner, REPLACING the earlier SNR readout that used to live in this
 * exact screen spot (SNR_X/SNR_Y, in the status strip next to the
 * S-meter bar) - "ponemos el valor numerico del smeter al lado en
 * lugar del SNR actual". The 12-segment S-meter bar is coarse (7dB/
 * segment); this gives the same underlying peak reading (demod_am_
 * get_signal_peak(), the exact same value smeter_draw() and smeter_
 * dbfs_uart_report() already use) as a precise, calibrated dBm number
 * instead. See SMETER_CAL_OFFSET_DB's own comment for the calibration
 * this is based on and its real, documented limitations.
 *
 * *** 01/09/2026: dynamically compensated for the CURRENT ATT/PGA
 * setting, not just valid at the one exact combination calibrated
 * against *** - per the project owner ("no una relacion lineal entre
 * el valor PGA y esta medida de dbm?"). Both ATT (the Rin selector,
 * k_att_db_values - a real, physics-derived 0/-6/-12dB signal drop,
 * not an approximation) and PGA (aic3204_set_pga_gain_db(), a real
 * 0.5dB-precision hardware gain stage per the AIC3204 datasheet) are
 * genuine, precisely-known linear gain elements - so unlike AGC (a
 * DYNAMIC, not-simply-trackable gain the calibration explicitly
 * required OFF), their effect on the reading can be computed and
 * subtracted out exactly, rather than requiring the exact calibrated
 * setting to still be active. Uses the EFFECTIVE PGA (s_pga_gain_db_x2
 * - s_rf_agc_backoff_x2, matching what rf_agc_apply_pga() actually
 * pushes to the codec - see its own comment), so RF-AGC's own
 * automatic backoff is compensated too, not just the user's PGA
 * target. NOT compensated for: the MAIN receive AGC (AGC_PROFILE_*) -
 * still must be OFF for this reading to mean anything, exactly as
 * before, since that gain is genuinely dynamic and not a simple
 * trackable number the way ATT/PGA are. Also assumes RF-AGC's own
 * backoff was truly 0 during the original calibration session (not
 * independently re-verified - the calibration levels used were modest
 * enough that RF-AGC likely never triggered, but this is an
 * assumption, not a confirmed fact).
 */
/* ETAPA 2: ya no dibuja - calcula y entrega el valor a ui_top.c.
 *
 * dbm_valid solo se pone a 1 con el AGC principal DESACTIVADO: con el AGC
 * actuando, la ganancia es dinamica y la lectura en dBm no significa nada.
 * El firmware la mostraba igual; ahora la barra pone "n/d con AGC" en vez
 * de un numero que parece bueno y no lo es (ver el apartado 3.8 del README
 * sobre la calibracion y sus limites). */
static void smeter_dbm_update_and_draw(float peak)
{
    float dbfs = smeter_dbfs_from_peak(peak);
    float current_att_db = k_att_db_values[s_rf_agc_rin_level];
    float current_pga_db = (float)((int32_t)s_pga_gain_db_x2 - (int32_t)s_rf_agc_backoff_x2) * 0.5f;
    float current_gain_db = current_att_db + current_pga_db;
    float cal_gain_db = SMETER_CAL_ATT_DB + SMETER_CAL_PGA_DB;
    float dbm_f = dbfs + SMETER_CAL_OFFSET_DB - (current_gain_db - cal_gain_db);
    int32_t dbm_rounded = (dbm_f >= 0.0f) ? (int32_t)(dbm_f + 0.5f)
                                          : (int32_t)(dbm_f - 0.5f);

    if (dbm_rounded > 32767) { dbm_rounded = 32767; }
    if (dbm_rounded < -32768) { dbm_rounded = -32768; }
    s_top_dbm = (int16_t)dbm_rounded;
    s_top_dbm_valid = (uint8_t)(demod_am_get_agc_profile() == AGC_PROFILE_MANUAL);
}

/*
 * Auto-tracks s_db_min/s_db_max from the current frame - see
 * s_spec_agc_enabled's declaration comment for the full design. A
 * no-op unless the SAGC tile has turned this on (s_spec_agc_enabled),
 * checked by the caller, not in here.
 */
/*
 * La autoescala se mudo a User/spec_agc.c el 22/09/2026, y no fue por
 * ordenar: fue porque estaba mal y aqui dentro no habia forma de
 * demostrarlo.
 *
 * Miraba el minimo y el maximo ABSOLUTOS del cuadro. Delante de la radio
 * eso significa que el pico de continua del centro y los bins hundidos
 * de los bordes -dos y cuatro muestras de quinientas doce- decidian la
 * ventana entera: salia de -1 a 120 dB, y con 121 dB de recorrido una
 * senal que ocupa veinte se queda plana y sin relieve. Desde fuera se
 * veia como "la autoescala no hace nada", cuando estaba haciendo
 * exactamente lo que se le pidio.
 *
 * Ahora usa percentiles y se puede alimentar con cuadros sinteticos
 * desde el simulador: sim/autoesc.c reproduce ese caso y compara las dos
 * versiones. Esa comprobacion es la razon de que esto sea un modulo.
 */

/*
 * STATUS BADGES: up to 6, in a 2x3 grid under the S-meter. Each shows
 * a radio state at a glance:
 *   NR / SPT  - NR (03/08/2026) is real again: lit whenever the
 *               bottom bar's NR button has Spectral Subtraction
 *               switched on (s_nr_on/nr_ss_get_enabled() - see
 *               nr_ss.h). Whether it's actually DOING anything right
 *               now also depends on demod mode (AM/USB/LSB only, see
 *               demod_am.c's NR INTEGRATION comment) - this badge
 *               only reflects the on/off switch, not that mode check.
 *               SPT lights up whenever the spectrum's spatial line
 *               smoothing is active (passes > 0) - see
 *               s_spec_smooth_passes' comment; it used to be the NB
 *               (noise blanker) badge, which never drove any real DSP.
 *   AGC       - lit: the demod AGC loop really is always active (even
 *               in MANUAL profile - i.e. the user-facing "AGC off" -
 *               the loop still runs, it just skips the peak-tracking
 *               math and outputs fixed unity gain - see
 *               agc_profile_t's MANUAL note in demod_am.h). This
 *               badge reflects the loop running, not whether it's
 *               actively adjusting gain right now.
 *   [profile] - s_btn_agc_profile, a REAL touchable button (not a
 *               plain badge_draw() call) showing OFF/SLW/MED/FST -
 *               tap it to cycle (MANUAL included again as of
 *               01/09/2026 - see agc_profile_cycle()'s comment), see
 *               agc_profile_button_callback()
 *               below. Moved here from where BW used to live
 *               31/07/2026, per the project owner: this slot gets
 *               more use as an interactive control than BW did as a
 *               static readout.
 *   BW        - now a REAL touchable button too (s_btn_audio_bw, added
 *               02/08/2026, same "not a plain badge_draw() call"
 *               treatment as [profile] above): in AM/USB/LSB it shows
 *               the active audio-filter width and tapping it cycles
 *               4K0 -> 2K3 -> 1K8 -> 4K0 (see audio_bw_cycle() and
 *               demod_am_set_audio_bw()'s comment in demod_am.h). In
 *               NFM/WFM it goes back to being purely informative
 *               (channel-filter -3dB width, "6K3"/"96K") and tapping
 *               it does nothing - those modes have their own fixed
 *               filters, unrelated to this selector, see
 *               audio_bw_button_callback()'s comment.
 *   OVR       - RF-level auto-AGC overload indicator (added
 *               07/08/2026), reusing this exact slot - was PRE
 *               (preamp: no such hardware control was ever identified
 *               on this board, shown dark as a placeholder). Lit RED
 *               whenever s_rf_agc_backoff_x2 > 0, i.e. the RF-AGC
 *               (main.c's rf_agc_poll()/s_rf_agc_enabled) is actively
 *               holding the PGA below the user's manual ceiling right
 *               now because it detected front-end clipping - see that
 *               feature's own declaration comment. Deliberately a
 *               BADGE, not the RFAGC grid tile's own label (which
 *               only shows plain ON/OFF now) - a badge lives in the
 *               always-visible right column, outside MENU_AREA, so
 *               rf_agc_poll() can safely redraw it from the
 *               background at any time, no matter what's showing in
 *               the menu. The grid tile used to show the live backoff
 *               amount directly on itself; that redraw fired from the
 *               same background poll and could land while some OTHER
 *               MENU_AREA sub-screen (e.g. the PGA detail view) was
 *               on screen, painting stale tile graphics over live
 *               content - exactly the corruption class
 *               menu_tile_bw_callback()'s own guard comment already
 *               warned about, just triggered from a background timer
 *               instead of a one-off user action. ATT (attenuator,
 *               same "no real placeholder" story as the old PRE) was
 *               dropped from this grid earlier to make room for BW's
 *               move - if a real attenuator control ever gets added,
 *               it'll need a new home rather than reclaiming this
 *               slot too.
 */
#define BADGE_W 55
#define BADGE_H 26
#define BADGE_GAP 6
/* *** 01/09/2026: reflowed from a 2-column x 3-row grid into a single
 * row of 6, per the project owner's status-strip redesign - see
 * STATUS_STRIP_Y/H's declaration comment. BADGE_COL(n) replaces the
 * old BADGE_X0/BADGE_X1 pair; there's no more BADGE_ROW_STEP since
 * everything's on one row now. Starts right after the SNR text with a
 * clear gap, vertically centered in the strip like every other
 * element in it. */
#define BADGE_ROW_X0 (uint16_t)(SNR_X + 100U) /* 01/09/2026: widened from +90 to +100 - the dBm readout that replaced SNR here can reach 3-digit magnitudes ("-128dBm", 7 chars, 84px at this font/scale) wider than SNR's old 2-digit-typical range ever needed */

/* Indexed directly by agc_profile_t (demod_am.h) - MANUAL, SLOW,
 * MEDIUM, FAST in that order. Originally "MAN, SLW, MED, FST" per the
 * project owner's verbatim request; MAN relabeled to OFF on
 * 01/09/2026 (also per the project owner) once MANUAL was re-added to
 * the cycle as an explicit "AGC off" rung - OFF reads more clearly
 * than MAN for that purpose. */
static const char *k_agc_profile_labels[4] = { "OFF", "SLW", "MED", "FST" };

/*
 * Hz -> "4,0 k" / "0,5 k". Una funcion y no cuatro copias.
 *
 * Estaba escrito cuatro veces -dos en la chapa de la barra y dos en la celda
 * de Ajustes-, una por cada combinacion de modo. Cuatro sitios que dicen el
 * mismo numero es exactamente como se consigue que digan numeros distintos:
 * de hecho paso, la celda de Ajustes se quedo devolviendo "0,5 kHz de CW"
 * fijo mientras la chapa ya decia el ancho de verdad.
 *
 * `cola` es lo que va detras ("k", "kHz CW", ...) o 0 para nada. El buffer
 * tiene que dar para 4 caracteres mas la cola mas el cierre.
 */
static void bw_format(char *buf, uint32_t hz, const char *cola)
{
    uint8_t i = 0U;

    buf[i++] = (char)('0' + (char)((hz / 1000U) % 10U));
    buf[i++] = ',';
    buf[i++] = (char)('0' + (char)((hz % 1000U) / 100U));
    if (cola) {
        buf[i++] = ' ';
        while (*cola != '\0') { buf[i++] = *cola++; }
    }
    buf[i] = '\0';
}

/*
 * ETAPA 4: la cabecera en kHz con dos decimales.
 *
 * FALLO QUE ESTO ARREGLA: la cabecera mostraba los Hz agrupados de tres en
 * tres ("7.200.000") y al lado ponia "MHz". Eso no era una etiqueta poco
 * afortunada, era falso: 7.200.000 no son 7 millones de MHz. Lo puse yo en
 * la etapa 2 y no lo vi hasta montar el render de la pantalla entera, con la
 * regla del espectro debajo diciendo "kHz" y numeros que no se parecian a
 * los de arriba.
 *
 * En kHz porque es la unidad que cubre bien todo el rango del aparato, de
 * 30 kHz a 108 MHz: en MHz, onda larga saldria como "0,153000", y en Hz hay
 * que contar grupos para saber en que banda estas. Dos decimales dan
 * resolucion de 10 Hz, y el paso mas fino del aparato son 100 Hz, asi que no
 * se pierde nada. Y ahora la cabecera y la regla del espectro hablan la
 * misma unidad, que es como se comparan de un vistazo.
 */
static void tune_freq_format_khz(uint32_t hz, char *buf)
{
    uint32_t khz  = hz / 1000U;
    uint32_t cent = (hz % 1000U) / 10U;   /* centesimas de kHz = decenas de Hz */
    char num[12];
    int8_t n = 0, i = 0, j;
    uint32_t v = khz;

    do { num[n++] = (char)('0' + (v % 10U)); v /= 10U; } while (v > 0U);

    for (j = (int8_t)(n - 1); j >= 0; j--) {
        buf[i++] = num[j];
        if (j != 0 && (j % 3) == 0) { buf[i++] = '.'; }
    }
    buf[i++] = ',';
    buf[i++] = (char)('0' + (cent / 10U));
    buf[i++] = (char)('0' + (cent % 10U));
    buf[i] = '\0';
}

/*
 * Indice del caracter que el mando va a mover, dentro de la frecuencia ya
 * formateada. El texto lleva puntos de millar y una coma decimal, asi que no
 * vale contar posiciones a secas: hay que contar DIGITOS desde la derecha,
 * saltandose los separadores.
 *
 * Para pasos que no son potencia de diez (12,5 kHz y 25 kHz) se subraya la
 * decada mas significativa que tocan - la de 10 kHz -, que es la que el ojo
 * espera ver moverse.
 */
static int8_t freq_highlight_index(const char *txt, uint32_t step_hz)
{
    int8_t n = -1;
    int8_t i, cnt = 0;
    uint32_t v = step_hz;

    while (v >= 10U) { v /= 10U; n++; }
    /* Tras el bucle n = digitos(paso) - 2, que es justo el indice del digito
     * contando desde la derecha del texto: el texto empieza en las DECENAS
     * de Hz (dos decimales de kHz), no en los Hz. Comprobado para los ocho
     * pasos: 100 Hz -> el 100 Hz, 1 kHz -> el 1 kHz, 12,5 kHz -> la decada
     * de 10 kHz, 1 MHz -> el 1 MHz. */

    for (i = 0; txt[i] != '\0'; i++) { }
    for (i = (int8_t)(i - 1); i >= 0; i--) {
        if (txt[i] != '.' && txt[i] != ',' && txt[i] != ' ') {
            if (cnt == n) { return i; }
            cnt++;
        }
    }
    return -1;
}

/*
 * 1 si la entrada i de k_demod_modes[] es la que esta activa ahora
 * mismo.
 *
 * Hace falta una funcion para esto porque el modo real NO identifica la
 * entrada: USB, CW y RTTY-U son las tres USB por debajo, y LSB y RTTY-L
 * las dos LSB. Lo que las distingue son los interruptores de encima, asi
 * que la unica respuesta correcta mira los tres campos.
 *
 * Y esta en un sitio solo porque hacen falta dos: la etiqueta del boton
 * de modo y la celda marcada en la pantalla de modos. Cuando la
 * condicion vivia suelta en cada uno, la primera se quedo con la version
 * corta -solo el modo- y por eso al elegir RTTY-L el boton seguia
 * poniendo "LSB", que es el demodulador de debajo y no el modo que se
 * acaba de elegir.
 */
static uint8_t demod_mode_entry_active(uint8_t i)
{
    uint8_t es_rtty = (uint8_t)(k_demod_modes[i].rtty_variant != RTTY_VARIANT_NONE);

    return (uint8_t)(k_demod_modes[i].mode == demod_am_get_mode() &&
                     es_rtty == rtty_get_enabled() &&
                     k_demod_modes[i].cw == cw_get_enabled());
}

static const char *demod_mode_label(void)
{
    uint8_t i;
    for (i = 0U; i < (uint8_t)DEMOD_MODE_ENTRY_COUNT; i++) {
        if (demod_mode_entry_active(i)) { return k_demod_modes[i].label; }
    }
    return "?";
}

/* encoder_target_t y ui_knob_t llevan hoy el mismo orden, pero se traduce
 * con un switch explicito y no con un cast: son dos enumeraciones de dos
 * modulos distintos, y el dia que una crezca el cast fallaria en silencio. */
static ui_knob_t top_knob_from_target(encoder_target_t t)
{
    switch (t) {
    case ENCODER_TARGET_VOLUME:     return UI_KNOB_VOLUME;
    case ENCODER_TARGET_BACKLIGHT:  return UI_KNOB_BACKLIGHT;
    case ENCODER_TARGET_SCALE:      return s_scale_adjust_max ? UI_KNOB_SCALE_HI
                                                              : UI_KNOB_SCALE_LO;
    case ENCODER_TARGET_SQUELCH:    return UI_KNOB_SQUELCH;
    case ENCODER_TARGET_SMOOTH:     return UI_KNOB_SMOOTH;
    case ENCODER_TARGET_PGA:        return UI_KNOB_PGA;
    case ENCODER_TARGET_NR:         return UI_KNOB_NR;
    case ENCODER_TARGET_RTTY_SHIFT: return UI_KNOB_RTTY_SHIFT;
    case ENCODER_TARGET_CW_TONE:    return UI_KNOB_CW_TONE;
    case ENCODER_TARGET_TUNE:
    default:                        return UI_KNOB_TUNE;
    }
}

static char *top_u2s(char *b, uint32_t v)
{
    char t[12];
    int n = 0, i = 0;
    do { t[n++] = (char)('0' + (v % 10U)); v /= 10U; } while (v);
    while (n) { b[i++] = t[--n]; }
    b[i] = '\0';
    return b;
}

static void top_sync(void)
{
    uint8_t first = 0U;
    uint16_t mv;

    tune_freq_format_khz(s_tune_hz, s_top_freq);
    while (s_top_freq[first] == ' ') { first++; }   /* por si acaso: ya no rellena */
    s_top.freq = &s_top_freq[first];
    s_top.freq_digit = freq_highlight_index(s_top.freq,
                                            k_tune_steps[s_tune_step_idx]);

    s_top.mode  = demod_mode_label();
    s_top.clock = s_time_text;

    s_top.batt_pct = battery_get_percent();
    mv = battery_get_millivolts();
    {
        int i = 0;
        top_u2s(s_top_volts, mv / 1000U);
        i = (s_top_volts[1] != '\0') ? 2 : 1;
        s_top_volts[i++] = ',';
        s_top_volts[i++] = (char)('0' + ((mv / 100U) % 10U));
        s_top_volts[i++] = (char)('0' + ((mv / 10U) % 10U));
        s_top_volts[i++] = ' ';
        s_top_volts[i++] = 'V';
        s_top_volts[i]   = '\0';
    }
    s_top.batt_volts = s_top_volts;

    /* El S-meter del firmware da 0..12 segmentos; aqui hace falta la lectura
     * en unidades S. Los 9 primeros segmentos son S1..S9 y el resto son dB
     * por encima, a ~7 dB por segmento (ver smeter_draw()). */
    if (s_top_segs > 9U) {
        s_top.s_units = 10U;
        s_top.over_db = (uint8_t)((s_top_segs - 9U) * 7U);
    } else {
        s_top.s_units = s_top_segs;
        s_top.over_db = 0U;
    }
    s_top.dbm = s_top_dbm;
    s_top.dbm_valid = s_top_dbm_valid;

    /* El valor de CADA destino del mando, leido de las mismas variables que
     * usaba aux_row_display_draw() antes de quedarse sin trabajo. */
    s_top.knob = top_knob_from_target(s_encoder_target);
    s_top.knob_value = s_top_knobval;
    switch (s_encoder_target) {
    case ENCODER_TARGET_TUNE:
        s_top.knob_value = k_tune_step_labels_ui[s_tune_step_idx];
        break;
    case ENCODER_TARGET_VOLUME:
        volume_format_ui(s_volume_db_x2, s_top_knobval);
        break;
    case ENCODER_TARGET_PGA:
        volume_format_ui(s_pga_gain_db_x2, s_top_knobval);
        break;
    case ENCODER_TARGET_BACKLIGHT:
        top_u2s(s_top_knobval, backlight_get_percent());
        {
            uint8_t i = 0U;
            while (s_top_knobval[i] != '\0') { i++; }
            s_top_knobval[i++] = ' ';
            s_top_knobval[i++] = '%';
            s_top_knobval[i]   = '\0';
        }
        break;
    case ENCODER_TARGET_SCALE:
        spectrum_db_format((int16_t)(s_scale_adjust_max ? s_db_max : s_db_min),
                           s_top_knobval);
        break;
    case ENCODER_TARGET_SQUELCH:
        spectrum_db_format((int16_t)demod_am_get_squelch_db(), s_top_knobval);
        break;
    case ENCODER_TARGET_SMOOTH:
        top_u2s(s_top_knobval, (uint32_t)spectrum_smooth_pct_for_save());
        {
            uint8_t i = 0U;
            while (s_top_knobval[i] != '\0') { i++; }
            s_top_knobval[i++] = ' ';
            s_top_knobval[i++] = '%';
            s_top_knobval[i]   = '\0';
        }
        break;
    case ENCODER_TARGET_NR:
        top_u2s(s_top_knobval, (uint32_t)s_nr_strength);
        break;
    default:
        s_top.knob_value = 0;
        break;
    }

    s_top.agc   = k_agc_profile_labels[(uint8_t)demod_am_get_agc_profile()];
    /* ETAPA 4: el ancho del filtro vuelve a la pantalla (ver ui_top_state_t::bw).
     * Se formatea desde los Hz, no desde las siglas "4K0"/"2K3": "4,0 k" se lee
     * igual de rapido y no hay que saberse una convencion. Solo en los modos
     * donde lo elige el usuario; en NFM/WFM el ancho es fijo y anunciarlo como
     * si fuera un ajuste seria mentir. */
    {
        demod_mode_t m = demod_am_get_mode();
        if (cw_get_enabled()) {
            /* El ancho de VERDAD del filtro de CW, que desde el 22/09/2026 lo
             * elige el mismo selector que en los demas modos. Se pregunta al
             * demodulador en vez de mirar la tabla: asi la chapa no puede
             * decir una cosa distinta de la que esta puesta. */
            bw_format(s_top_bw, (uint32_t)(demod_am_get_cw_bw_hz() + 0.5f), "k");
            s_top.bw = s_top_bw;
        } else if (m == DEMOD_MODE_AM || m == DEMOD_MODE_USB || m == DEMOD_MODE_LSB) {
            bw_format(s_top_bw, k_audio_bw_hz[(uint8_t)demod_am_get_audio_bw()], "k");
            s_top.bw = s_top_bw;
        } else {
            s_top.bw = 0;
        }
    }
    s_top.nr_on = s_nr_on;
    s_top.ovr   = (uint8_t)(s_rf_agc_enabled && (s_rf_agc_backoff_x2 > 0));
    s_top.spk_muted = (uint8_t)(!s_speaker_pa_enabled);
    s_top.sam_ppm = (s_top_sam[0] != '\0') ? s_top_sam : 0;
}



/* ===========================================================================
 * ETAPA 4: barra de acciones
 * =========================================================================== */
/* Estado de las pantallas de menu, declarado aqui porque act_sync() -que
 * decide que boton de la barra va iluminado- esta antes en el fichero. */
typedef enum { CFG_AJUSTES = 0, CFG_BANDAS } cfg_pant_t;
static cfg_pant_t  s_cfg_pant;
static grid_pant_t s_grid_pant;

/*
 * La paleta de la interfaz. Vivia en User/gfx2.c, que es un fichero
 * GENERADO desde el simulador; al regenerarlo se perdio la definicion y el
 * enlazado se cayo. Su sitio es este: gfx2.c dibuja, no decide de que color.
 *
 * k_pal_oscura es la revision 1, la que se validó en la placa con la carta
 * de grises (ver el historial: la "revision 2" salia de suponer que las
 * proporciones de luminancia de una foto son fiables, y no lo son).
 */
const palette_t *g_pal = &k_pal_oscura;

/*
 * TEMAS - 22/09/2026, a peticion del dueno del proyecto.
 *
 * Un tema NO es solo la paleta de la interfaz: es la interfaz Y la del
 * waterfall, que es medio panel. Tenerlas sueltas dejaba combinaciones
 * que no pegan -una interfaz de grises calidos con un waterfall azul- y
 * obligaba a acertar dos ajustes para que la pantalla fuera de una
 * pieza. Aqui van emparejadas.
 *
 * "Paleta", en la pagina de Pantalla, sigue existiendo y sigue mandando:
 * elegir un tema pone su waterfall, y despues se puede cambiar solo el
 * waterfall si a uno le apetece otra cosa. El tema propone, no encierra.
 *
 * PENDIENTE: cual va con cada tema esta sin cerrar. La del rediseno se
 * quito de aqui el 22/09/2026 mientras se buscaba una averia de audio en
 * FM, para dejar la etapa 11 partida en dos y ver de que mitad venia. No
 * se ha vuelto a meter todavia.
 */
typedef struct {
    const char         *nombre;
    const char         *desc;
    const palette_t    *pal;
    spectrum_palette_t  wf;
} tema_t;

static const tema_t k_temas[] = {
    { "Oscura",      "La de siempre, con el waterfall clásico",
      &k_pal_oscura,    SPECTRUM_PALETTE_CLASSIC },
    { "Contrastada", "Negro real y saltos mayores, para pleno sol",
      &k_pal_contraste, SPECTRUM_PALETTE_TURBO },
    { "Ámbar",       "Grises cálidos, sin azul: para la noche",
      &k_pal_ambar,     SPECTRUM_PALETTE_INFERNO },
    { "Fría",        "Grises azulados, para luz de día",
      &k_pal_fria,      SPECTRUM_PALETTE_VIRIDIS }
};
#define TEMA_COUNT (sizeof(k_temas) / sizeof(k_temas[0]))

static ui_act_state_t s_act;
static char           s_act_vol[10];
static int8_t         s_act_press = -1;       /* boton bajo el dedo desde la pulsacion */

/*
 * EL ORDEN DE LA BARRA, EN UN SOLO SITIO.
 *
 * Cada entrada ata la casilla al widget que ya existe (el callback se elige
 * comparando ese puntero, ver demo_button_callback) y al nombre que se pinta.
 * Cambiar el orden es mover filas de esta tabla y nada mas: el valor que
 * muestra cada casilla lo decide act_sync() mirando QUE BOTON es, no en que
 * posicion esta, asi que no hay una segunda lista de la que acordarse.
 *
 * Orden actual, pedido por el dueno del proyecto: Bandas, Modo, Paso,
 * Volumen, Ruido, Ajustes.
 */
static const struct {
    ui_button_t *btn;
    const char  *nombre;
} k_act_slots[UI_ACT_N] = {
    { &s_btn_bands, "Bandas"  },
    { &s_btn_mode,  "Modo"    },
    { &s_btn_step,  "Paso"    },
    { &s_btn_vol,   "Volumen" },
    { &s_btn_nr,    "Ruido"   },
    { &s_btn_menu,  "Ajustes" },
};

/*
 * Cada boton dice su valor actual. Se lee de las MISMAS variables que la
 * pastilla del mando y la cabecera - no de una copia - para que no puedan
 * discrepar.
 */
static void act_sync(void)
{
    uint8_t i;

    volume_format_ui(s_volume_db_x2, s_act_vol);

    for (i = 0U; i < UI_ACT_N; i++) {
        const ui_button_t *b = k_act_slots[i].btn;

        s_act.name[i]   = k_act_slots[i].nombre;
        s_act.value[i]  = 0;
        s_act.active[i] = 0U;

        if (b == &s_btn_mode) {
            s_act.value[i]  = demod_mode_label();
            s_act.active[i] = (uint8_t)(s_menu_open && s_grid_pant == GRID_MODO);
        } else if (b == &s_btn_step) {
            s_act.value[i] = k_tune_step_labels_ui[s_tune_step_idx];
        } else if (b == &s_btn_vol) {
            s_act.value[i]  = s_act_vol;
            s_act.active[i] = (uint8_t)(s_encoder_target == ENCODER_TARGET_VOLUME);
        } else if (b == &s_btn_nr) {
            s_act.value[i]  = s_nr_on ? "activo" : "apagado";
            s_act.active[i] = s_nr_on;
        } else if (b == &s_btn_menu) {
            /* Se ilumina el boton de LA pantalla que esta abierta, no el de
             * Ajustes siempre que haya algun menu: estando en Bandas se
             * encendia Ajustes, que es justo decir donde NO estas. El detalle
             * de un ajuste cuenta como Ajustes, que es de donde se llega. */
            s_act.active[i] = (uint8_t)((s_menu_cfg_active && s_cfg_pant == CFG_AJUSTES)
                                        || s_menu_detail_active);
        } else if (b == &s_btn_bands) {
            s_act.active[i] = (uint8_t)(s_menu_cfg_active && s_cfg_pant == CFG_BANDAS);
        } else if (b == &s_btn_step) {
            s_act.active[i] = (uint8_t)(s_menu_open && s_grid_pant == GRID_PASOS);
        }
        /* Bandas no tiene un valor que quepa en una linea: la banda ya esta
         * implicita en la frecuencia de la cabecera. Se queda a una linea. */
    }
}

static void act_draw(void)
{
    act_sync();
    s_act.pressed = s_act_press;
    ui_act_draw(&s_act);
}

/* Indexed directly by audio_bw_t (demod_am.h) - AUDIO_BW_4K0,
 * AUDIO_BW_2K3, AUDIO_BW_1K8 in that order. */
/* Indexed the same way (AUDIO_BW_4K0/2K3/1K8), but a completely
 * different set of filters, shown only while mode==WFM - see
 * demod_am_set_audio_bw()'s comment in demod_am.h for why the same
 * enum/tile now means two different things depending on mode. */

/* Indexed directly by aic3204_rin_t (aic3204.h) - AIC3204_RIN_10K,
 * AIC3204_RIN_20K, AIC3204_RIN_40K in that order, same "index into a
 * table, don't try to derive a string from the raw value" shape as
 * k_agc_profile_labels/k_audio_bw_labels above. 10k/20k/40k are the
 * AIC3204 MIC_PGA differential input impedances - see
 * aic3204_set_input_impedance()'s comment in aic3204.c - which work
 * out to 0/-6/-12dB of relative input attenuation (each doubling of
 * Rin is a 6dB drop in the signal presented to the PGA), hence "ATT"
 * as the manual tile's name rather than "RIN". */
/* k_att_labels se fue con los tiles viejos (22/09/2026). Los rotulos que se
 * ven ahora son k_att_nombres ("0 dB", "-6 dB", "-12 dB"), en la celda de
 * Ajustes; estos eran las siglas en mayusculas del tile. */

/* RTTY BAUD tile table (DIG page) - added 09/08/2026, per the project
 * owner. Cycles the bit rate through the common ham/commercial rates,
 * same "index into a table, don't try to read the float back and
 * match it" shape as k_audio_bw_labels/agc_profile_t already use for
 * their own cycles (float equality after a round-trip through
 * rtty_get_baud() would be fragile - the index is the source of
 * truth here, rtty_set_baud() just gets told the resulting value).
 * Starts at index 1 (50 baud) to match config.h's CONFIG_RTTY_BAUD
 * default - see menu_tile_rtty_baud_callback()'s comment. */
static const float       k_rtty_baud_values[4] = { 45.45f, 50.0f, 75.0f, 100.0f };
/*
 * Siembras de velocidad ofrecidas. No es el rango que admite el
 * decodificador -ese va de 5 a 45 PPM- sino los sitios razonables por
 * donde empezar a buscar. 20 es el de por defecto y sirve para casi
 * todo.
 */
static const uint8_t k_cw_wpm[] = { 12U, 16U, 20U, 25U, 30U, 36U };
#define CW_WPM_OPCIONES (sizeof(k_cw_wpm) / sizeof(k_cw_wpm[0]))
static uint8_t s_cw_wpm_idx = 2U;   /* 20 PPM, igual que CONFIG_CW_WPM_HINT */

static const char *const k_rtty_baud_labels[4] = { "45.45", "50", "75", "100" };
#define RTTY_BAUD_COUNT 4U
static uint8_t s_rtty_baud_idx = 1U;
/* Same indexing, in Hz - the nominal -3dB corner each ALPF_*_COEFFS
 * table was designed for (see their comments in demod_am.c). Added
 * 03/08/2026 for the spectrum panadapter's demodulated-bandwidth tint
 * (see sdr_spectrum_waterfall_tick()'s call to spectrum_draw()) - the
 * labels above are for display only and aren't parseable back into a
 * number, so this is a second small table rather than deriving one
 * from the other. */
static const uint32_t k_audio_bw_hz[3] = { 4000UL, 2300UL, 1800UL };

/*
 * Lo que significa el mismo selector de ancho CUANDO ESTAS EN CW - 22/09/2026.
 *
 * Antes en CW el selector no hacia nada: el filtro de CW estaba clavado en
 * 500 Hz y la chapa de la barra mentia menos que las otras, pero el mando no
 * servia para nada. Ahora las tres posiciones son tres anchos de CW.
 *
 * 1000 Hz para buscar por la banda, 500 para escuchar y 250 para sacar a
 * alguien de debajo de otro. Mas estrecho de 250 con este numero de etapas
 * empieza a alargar los puntos, que es peor que oir de mas.
 *
 * Mismo indice que k_audio_bw_hz: la posicion 0 es la mas ancha en los dos.
 */
static const uint32_t k_cw_bw_hz[3] = { 1000UL, 500UL, 250UL };

/*
 * Las dos tablas de ancho van indexadas por el MISMO audio_bw_t, asi que
 * tienen que medir lo mismo y lo mismo que el enum. Si alguien anade un
 * ancho a una y se olvida de la otra, esto no compila - que es justo lo que
 * NO pasaba con la lista de paletas ni con la de modos, y las dos veces
 * acabo en pantalla.
 */
_Static_assert(sizeof(k_cw_bw_hz) / sizeof(k_cw_bw_hz[0])
               == sizeof(k_audio_bw_hz) / sizeof(k_audio_bw_hz[0]),
               "k_cw_bw_hz y k_audio_bw_hz tienen que medir lo mismo");
_Static_assert(sizeof(k_audio_bw_hz) / sizeof(k_audio_bw_hz[0])
               == (size_t)AUDIO_BW_1K8 + 1U,
               "las tablas de ancho no cubren todo audio_bw_t");

/*
 * Cycles SLOW -> MEDIUM -> FAST -> SLOW and redraws s_btn_agc_profile
 * - shared by BOTH ways to trigger this now: tapping the badge/button
 * itself (agc_profile_button_callback() below) and the NR bottom-bar
 * button (see demo_button_callback()'s NR branch, repurposed
 * 31/07/2026 - a quick, deliberately temporary stopgap since NR's
 * noise-reduction toggle was never wired to real DSP anyway, ahead of
 * a proper settings-menu redesign the project owner is planning for
 * later the same day, once the badge grid ran out of comfortable room
 * for more controls). Factored out so both entry points can't drift
 * out of sync with each other.
 *
 * MANUAL was removed from the cycle 07/08/2026, per the project owner
 * ("no tiene sentido por ahora"), then RE-ADDED 01/09/2026 (also per
 * the project owner: an explicit "AGC off" option is wanted after
 * all) - the enum value, the demod_am.c MANUAL branch (fixed unity
 * gain, peak-tracking bypassed), and k_agc_profile_labels[0]="OFF"
 * were never actually removed in between, so reviving it is just this
 * one switch case. Cycle order is now MANUAL -> SLOW -> MEDIUM ->
 * FAST -> MANUAL, i.e. "MAN" reads as the explicit AGC-off rung at
 * one end of the cycle rather than a hidden extra state - tapping the
 * tile four times returns to wherever you started, same as before.
 */
static void agc_profile_cycle(void)
{
    agc_profile_t p = demod_am_get_agc_profile();

    switch (p) {
    case AGC_PROFILE_MANUAL: p = AGC_PROFILE_SLOW;   break;
    case AGC_PROFILE_SLOW:   p = AGC_PROFILE_MEDIUM; break;
    case AGC_PROFILE_MEDIUM: p = AGC_PROFILE_FAST;   break;
    case AGC_PROFILE_FAST:
    default:                  p = AGC_PROFILE_MANUAL; break;
    }
    demod_am_set_agc_profile(p);
    debug_print("agc: profile now ");
    debug_print(k_agc_profile_labels[(uint8_t)p]);
    debug_print("\n");
    /* ETAPA 2/3: el aspecto lo lleva el chip de ui_top.c. Pintar el boton
     * aqui era lo que soltaba un recuadro cian sobre la barra de estado al
     * cambiar el AGC desde el menu. */
    top_sync();
    ui_top_draw_status(&s_top);
}

/* Unlike the bottom-bar buttons (which toggle or jump straight to a
 * target), this only ever needs to STEP forward one at a time - four
 * short taps to get anywhere, and there's no "off" state worth
 * jumping straight to the way VOL/MENU jump straight to their
 * targets. */
static void agc_profile_button_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;

    if (event == UI_EVENT_RELEASE) {
        agc_profile_cycle();
    }
}

/* ETAPA 2: este readout pasa a ui_top.c. Se conserva el nombre y todas
 * las llamadas existentes, y solo cambia lo que hace por dentro - asi el
 * cambio se revierte tocando una funcion, sin perseguir call sites. */
static void badges_draw(void)
{
    top_sync();
    ui_top_draw_status(&s_top);
}

/*
 * --- Settings menu screen: tile geometry + callbacks --------------------
 *
 * CONFINED to the spectrum+waterfall area (MENU_AREA_*) rather than
 * the whole 800x480 screen - changed 31/07/2026 per the project
 * owner: the top bar (frequency/mode/time/battery) and the right
 * column (S-meter/badges) and the bottom button bar all stay live and
 * visible while the menu is open, only the graph itself gets replaced
 * by the tile grid. See s_menu_screen's declaration comment for how
 * touch routing and the still-running S-meter/time updates work with
 * two screens effectively active in different SCREEN REGIONS at once.
 *
 * Grid: 4 columns x 3 rows, 159x108px tiles, 8px gaps, inside
 * MENU_AREA (676x358, starting at (0,SPEC_Y)).
 *
 * REDESIGNED 03/08/2026, per the project owner, from one flat 4x3
 * grid of 12 interchangeable tiles into a PAGED layout:
 *
 *   - Column 0 (all 3 rows): page NAVIGATION, always present and
 *     never changing shape - see the PAGINATION note below for what
 *     lives in it now.
 *   - Columns 1-3 (all 3 rows = 9 slots): the selected page's
 *     OPTIONS, laid out row-major (slot 0 = row0/col1, slot 1 =
 *     row0/col2, ..., slot 7 = row2/col2). Slot 8 (row2/col3,
 *     bottom-right) is ALWAYS EXIT, regardless of which page is
 *     selected - it closes the whole menu (menu_screen_close()), same
 *     as the long-press-the-knob gesture does from anywhere (see
 *     tune_encoder_poll()'s comment). A page that doesn't fill all 8
 *     option slots just leaves the rest empty (black).
 *
 * PAGINATION, added 09/08/2026, per the project owner: column 0 was
 * originally a fixed 3-tile PAGE SELECTOR, one tile per row, one page
 * per tile (RADIO/UI/HW - fit exactly because there were exactly 3
 * pages). That doesn't scale - MENU_AREA_H only has room for 3 tile
 * rows total (see the height-budget comment on FREQ_KEYPAD's geometry
 * just below for how tight that already is), so a 4th page (DIG, see
 * below) has nowhere to add a 4th row. Column 0 is now a PAGER
 * instead, reusing the exact same 3 slots regardless of how many
 * pages exist:
 *   - Row 0: "PREV" - steps s_menu_page back one, wrapping from page 0
 *     to the last page.
 *   - Row 1: the CURRENT page's name (k_menu_page_names[]), solid
 *     ORANGE-on-BLACK same as the old "selected" look - purely
 *     informational (enabled=0, see ui_screen_add_button()'s comment
 *     in ui.c for what that does: still painted, never reacts to
 *     touch) since it can't mean "switch to this page", it already IS
 *     this page.
 *   - Row 2: "NEXT" - steps s_menu_page forward one, wrapping from the
 *     last page back to 0.
 * Both PREV/NEXT share menu_page_step_callback() (direction via
 * user_data, same (void*)(uintptr_t) pattern the old per-page
 * callback used), styled ORANGE-outlined (BLACK-on-BLACK fill, ORANGE
 * text/border) same as the old unselected page tiles - visually
 * distinct from the CYAN/DARKGRAY/YELLOW palette the option tiles use,
 * so "this changes pages" still reads differently from "this is a
 * page's option" at a glance. Adding a 5th, 6th, ... page later is
 * just another menu_page_t enum value + k_menu_page_names[] entry + a
 * switch case in menu_grid_show() below - column 0 itself never needs
 * to change again.
 *
 * Per-page option assignment (all pre-existing tiles, just
 * relocated - no settings were dropped):
 *   RADIO (slots 0-7): AGC, SQL (squelch), VOL, BW, PGA, NR (Spectral
 *                       Subtraction strength, AM/USB/LSB only), RFAGC
 *                       (RF-level auto-AGC toggle, slot 6, added
 *                       07/08/2026), ATT (manual codec input
 *                       attenuator, slot 7, added 01/09/2026 - see
 *                       menu_tile_att_callback()'s comment).
 *   UI    (slots 0-5): BL (backlight), SCALE, SPT, SMH (smooth),
 *                       SPC (spectrum trace style, HEATMAP<->LINE),
 *                       ZOOM.
 *   HW    (slots 0-1, 4-6): SPK - speaker PA enable/mute (PB7, see
 *                       speaker_pa_set_enabled()'s comment - pin/
 *                       polarity UNCONFIRMED as of 03/08/2026). IFBW -
 *                       WFM pre-discriminator channel filter width
 *                       (96K/80K, slot 4, added 01/09/2026 - see
 *                       menu_tile_ifbw_callback()'s comment). SAGC -
 *                       spectrum/waterfall auto-scale toggle (slot 5,
 *                       added 01/09/2026 - see
 *                       menu_tile_specagc_callback()'s comment). RATE -
 *                       AM/USB/LSB/NFM sample rate 96K/48K toggle
 *                       (slot 6, added 01/09/2026 - see
 *                       menu_tile_rate_callback()'s comment).
 *                       Slot 7 reserved for future hardware settings.
 *   DIG   (slots 0-2), added 09/08/2026: digital-mode (currently just
 *                       RTTY) parameters that no longer fit on RADIO
 *                       once it hit 8/8 - see the SHIFT tile's
 *                       declaration comment for why it moved here
 *                       rather than staying put. SHIFT (mark/space
 *                       separation), BAUD (bit rate), INV (station
 *                       NORMAL/REVERSE convention, independent of the
 *                       USB/LSB sideband mirror RTTY-L/RTTY-U already
 *                       handle - see rtty_set_station_inverted()'s
 *                       comment in rtty.h for the DDK9 field finding
 *                       that prompted this). Slots 3-7 reserved for
 *                       future digital modes (PSK31 and similar).
 *
 * Tile behavior is unchanged from before the redesign, just
 * regrouped by page:
 *   - AGC, SPT, SPC, BW, ZOOM, and BAUD CYCLE/TOGGLE DIRECTLY on tap
 *     (same as their existing bottom-bar-button/badge equivalents
 *     where they have one) and stay on this screen - you can tap
 *     several of these in a row. INV is the same shape as RFAGC/SPK:
 *     a plain two-state toggle, not a multi-way cycle.
 *   - SQL/BL/SCALE/VOL/SMH/PGA/SHIFT instead open a DETAIL view
 *     (menu_detail_show()) for that target - see its own comment.
 *
 * menu_grid_show() itself always redraws the WHOLE MENU_AREA (page
 * column + options + EXIT) in one pass, including on a page switch -
 * simplest correct thing (no separate "clear just the options area"
 * path to keep in sync), and cheap enough for a menu tap.
 */
#define MENU_AREA_X 0
#define MENU_AREA_W MAIN_W
#define MENU_AREA_Y SPEC_Y
#define MENU_AREA_H (uint16_t)(WF_PANEL_Y + WATERFALL_ROWS + 4U - SPEC_Y) /* 346+72+4-104=318 - was 358 before the status-strip redesign (SPEC_Y grew from 64 to 104) */

/* *** 01/09/2026, RECOMPUTED for the status-strip redesign - real bug
 * fix, per the project owner *** - MENU_TILE_W/H used to be sized
 * (159x108) to fit exactly within the OLD 676x358 MENU_AREA. Stealing
 * 40px of height for the new status strip (MENU_AREA_H: 358->318)
 * without ALSO shrinking these meant the 3-row tile grid (3*108 +
 * 2*GAP + 2*margin = 356) no longer fit inside the new, shorter area
 * (318) - it overflowed by 38px, running the bottom row (which
 * includes EXIT) 24px into the bottom button bar. Recomputed from
 * scratch for the new MENU_AREA_W(800)/H(318), same 8px margin/gap
 * convention as before: width divides EXACTLY (4*190 + 3*8 + 2*8 =
 * 800); height leaves a harmless 1px of slack (3*95 + 2*8 + 2*8 =
 * 317, vs 318 available) rather than force an ugly fractional tile
 * height for the sake of a pixel nobody would ever notice missing. */
#define MENU_TILE_W 190
#define MENU_TILE_H 95
#define MENU_TILE_GAP 8
#define MENU_TILE_X0 (uint16_t)(MENU_AREA_X + 8)
#define MENU_TILE_Y0 (uint16_t)(MENU_AREA_Y + 8)
#define MENU_TILE_COL(i) (uint16_t)(MENU_TILE_X0 + (i) * (MENU_TILE_W + MENU_TILE_GAP))
#define MENU_TILE_ROW(i) (uint16_t)(MENU_TILE_Y0 + (i) * (MENU_TILE_H + MENU_TILE_GAP))

/*
 * Frequency-entry keypad geometry (added 07/08/2026) - a denser 4x4
 * grid than the MENU_TILE_* one above, since a phone-style keypad
 * needs 16 keys (10 digits + DEL/CLR/BACK + 3 unit-accept buttons)
 * where the settings grid only ever needed up to 3 rows of 4. Reuses
 * MENU_TILE_COL() as-is for the x positions - the column math (4
 * cols, same MENU_AREA_W, same MENU_TILE_GAP) works out identical
 * regardless of row height, so there's no reason to duplicate it.
 * Only the row pitch differs (shorter tiles, 4 rows instead of 3),
 * and rows start further down to leave room for the entry readout
 * (see FREQ_KEYPAD_READOUT_H below) between the top border and the
 * first row of keys.
 */
/*
 * Height budget, checked to actually fit MENU_AREA_H (318, was 358
 * before the status-strip redesign shrank SPEC_H/grew SPEC_Y - see
 * MENU_AREA_H's own comment) - this bit the project owner twice now
 * with the exact same failure mode, both times from changing the
 * available height without re-checking this budget: first 07/08/2026
 * (the FIRST version of this budget, 56 + 4*68 + gaps, came out to
 * 368, ten pixels TALLER than MENU_AREA_H at the time), then again
 * 01/09/2026 when MENU_AREA_H shrank by 40 out from under the OLD
 * 65px row height (356 vs the new 318 - a 38px overrun) without this
 * budget being revisited. Both times: row 3 spilled past the bottom
 * of MENU_AREA and into the bottom bar underneath, which
 * menu_screen_close() never repaints (see its comment: "only the
 * spectrum+waterfall panels need restoring"), so the overrun stayed
 * corrupted on screen after closing the keypad. Recomputed budget,
 * top to bottom: 8 (top margin) + 48 (readout) + 8 (gap) + 4*55
 * (rows) + 3*8 (inter-row gaps) + 8 (bottom margin) = 316,
 * comfortably inside 318 this time - VERIFY THE ARITHMETIC AGAIN
 * before ever touching MENU_AREA_H, FREQ_KEYPAD_READOUT_H, or
 * FREQ_KEYPAD_TILE_H again - this is now a two-time repeat offender.
 */
#define FREQ_KEYPAD_READOUT_H 48  /* readout strip height, MENU_AREA_Y+8 downward */
#define FREQ_KEYPAD_TILE_W    MENU_TILE_W /* same 4-column pitch as MENU_TILE_COL() */
#define FREQ_KEYPAD_TILE_H    55
#define FREQ_KEYPAD_Y0        (uint16_t)(MENU_AREA_Y + 8 + FREQ_KEYPAD_READOUT_H + MENU_TILE_GAP)
#define FREQ_KEYPAD_COL(i)    MENU_TILE_COL(i)
#define FREQ_KEYPAD_ROW(i)    (uint16_t)(FREQ_KEYPAD_Y0 + (i) * (FREQ_KEYPAD_TILE_H + MENU_TILE_GAP))

/*
 * Top-bar tap zone for opening the keypad - the whole left portion of
 * the top bar (x: 0..MODE_X, y: 0..TOP_H), generous on purpose for a
 * resistive touchscreen rather than tight around freq_display_draw()'s
 * exact text bounds. Safe to be this generous: nothing else in the
 * top bar is interactive to the left of MODE_X (MODE/STEP/VOL's own
 * readouts at MODE_X/STEP_X/VOL_X are all >= MODE_X), so there's
 * nothing this zone could steal a touch from.
 */
/* 22/09/2026: FREQ_TAP_X1/Y1 ya no existen - la zona la da
 * ui_top_freq_hit(), ver su llamada en demo_touch_poll(). Se quedaba
 * anclada a MODE_X, que es geometria de la cabecera vieja. */

/*
 * Top-bar tap zone for the clock-setting keypad (08/09/2026) - the
 * clock readout's own small area (TIME_X/Y, scale-3 "HH:MM" text),
 * NOT reusing FREQ_TAP's generous "whole left portion of the bar"
 * shape: TIME_X/Y sits in the SAME top bar but in its own column,
 * directly ABOVE the battery gauge/speaker icon (BATT_Y=40,
 * SPK_ICON_Y) which live lower in this same bar - so unlike FREQ_TAP,
 * this needs a real Y ceiling (TIME_TAP_Y2) to stay clear of them,
 * not just TOP_H. X1 has a small left margin past TIME_X itself for
 * a forgivable resistive-panel tap; X2 runs to the screen edge since
 * nothing else sits right of the clock in this bar.
 */
#define TIME_TAP_X1 (uint16_t)(TIME_X - 10)


/*
 * Option-slot geometry helpers: slot 0-8 -> (row, col) within the
 * RIGHT-hand 3x3 area (columns 1-3, since column 0 is the page
 * selector). Row-major: slot / 3 = row, slot % 3 = column-within-3,
 * offset by +1 to skip the page-selector column. Slot 8 always lands
 * on row 2 / column 3 (bottom-right) - the fixed EXIT position - see
 * this block's comment above for why that's deliberate, not a
 * coincidence of the arithmetic.
 */
#define MENU_OPT_COL(slot) MENU_TILE_COL(1 + ((slot) % 3))
#define MENU_OPT_ROW(slot) MENU_TILE_ROW((slot) / 3)
#define MENU_OPT_EXIT_SLOT 8

/*
 * --- Spectrum/waterfall ZOOM --------------------------------------------
 *
 * Added 31/07/2026, per the project owner - NOT a codec/sample-rate
 * change (see the earlier discussion on why that's both risky, with
 * no reference capture to replay, and not actually what was needed).
 * This is a purely DIGITAL zoom: the codec/ADC/I2S keep running at
 * 48kHz exactly as always, untouched (was 192kHz before 04/08/2026 -
 * see sdr_rx.h's SDR_RX_BLOCK_SAMPLES comment for THAT separate,
 * actual sample-rate change - this zoom mechanism itself needed no
 * code changes at all for it, being generic decimate-by-2 regardless
 * of what Fs it's fed; only the absolute Hz spans below moved).
 *
 * How it works: cascade 1-3 stages of a generic decimate-by-2 FIR
 * (ZOOM_DECIM2_COEFFS, see its own comment) on a COPY of the raw I/Q,
 * re-centered on the actual tuned frequency first (same delay-free
 * sign-flip rotation demod_am.c's low-IF down-mix uses, applied here
 * only when demod_am_get_if_offset_active() is set - see
 * zoom_process_block()). Enough decimated samples accumulate across
 * however many raw 96kHz blocks it takes to fill one FFT_SIZE window,
 * then that window feeds the SAME fft_compute_db_iq() the unzoomed
 * view already uses - no change to the FFT itself, just what feeds it.
 *
 *   SPEC_ZOOM_1X - unchanged existing behavior: FFT runs directly on
 *                  the raw 96kHz block, every block, +/-48kHz span.
 *                  ZERO extra cost - the whole zoom pipeline below is
 *                  skipped entirely at this setting.
 *   SPEC_ZOOM_2X - one decimate-by-2 stage, +/-12kHz span. Needs 2 raw
 *                  blocks (~5.3ms) per FFT window.
 *   SPEC_ZOOM_4X - two cascaded stages, +/-6kHz span, 4 raw blocks
 *                  (~10.7ms) per window.
 *   SPEC_ZOOM_8X - three cascaded stages, +/-3kHz span, 8 raw blocks
 *                  (~21.3ms) per window.
 *
 * REFRESH RATE TRADEOFF, inherent to any zoom-FFT (not a bug to fix):
 * higher zoom means each window takes longer to fill, so the spectrum/
 * waterfall update less often - see sdr_spectrum_waterfall_tick()'s
 * comment for exactly how that's handled (skip drawing, don't block,
 * when a window isn't ready yet).
 *
 * spec_zoom_t itself and s_spec_zoom's definition now live earlier in
 * this file (right after the forward-declarations block) - see the
 * comment there for why (originally for the old snr_update_and_draw(),
 * replaced 01/09/2026, but still needed there today by other users of
 * s_spec_zoom). Nothing below this point changed behavior.
 */

/*
 * spec_zoom_full_span_hz() - added 01/09/2026 alongside the 48kHz
 * rate option, factoring out a "switch on s_spec_zoom -> full_span_hz"
 * pattern that had been copy-pasted at THREE separate call sites
 * (spec_span_labels_draw()'s tick ruler, spec_drag_tune_apply()'s
 * Hz-per-pixel, and the AM/USB/LSB audio-bandwidth tint's width) -
 * exactly the kind of unlabeled duplicated assumption that caused the
 * bug this function fixes in the first place (all three had 96000
 * hardcoded for SPEC_ZOOM_1X, silently wrong the moment AM/USB/LSB/
 * NFM's real rate became selectable). One function, one place to get
 * it right, no fourth copy to eventually drift out of sync.
 *
 * NOTE - does NOT account for WFM's own fixed 192kHz rate; none of
 * the three call sites checked for WFM mode before this change
 * either, so this doesn't newly introduce that gap, just makes it
 * easier to close later in one place instead of three.
 */
static uint32_t spec_zoom_full_span_hz(void)
{
    uint32_t base_span_hz = s_nonwfm_use_48k ? 48000UL : 96000UL;
    switch (s_spec_zoom) {
    case SPEC_ZOOM_2X: return base_span_hz / 2U;
    case SPEC_ZOOM_4X: return base_span_hz / 4U;
    case SPEC_ZOOM_8X: return base_span_hz / 8U;
    case SPEC_ZOOM_1X:
    default:           return base_span_hz;
    }
}

/*
 * Panadapter frequency scale under the spectrum trace - 5 reference
 * points (both edges, both quarter-points, and center), each showing
 * the ACTUAL absolute frequency at that point, TRUNCATED to kHz (last
 * 3 digits dropped - "quitando los 3 ultimos digitos", per the
 * project owner, once 3 points at full Hz precision didn't leave room
 * for more of them). Same grouped-thousands look as
 * freq_display_draw()'s title-bar readout (via tune_freq_format() -
 * already defined above, reused here on the truncated kHz value
 * rather than the raw Hz one - e.g. 180096345 Hz -> 180096 -> shown as
 * "180.096", not "180.096.345").
 *
 * PANEL-CENTER FREQUENCY - the one thing this function has to get
 * right that a naive "s_tune_hz +/- half_span" wouldn't: the pixel
 * grid's true center is NOT always s_tune_hz. Whenever the low-IF
 * down-mix is active (demod_am_get_if_offset_active(), AM/USB/LSB/NFM
 * at 1X zoom - see demod_am.h's LOW-IF TUNING note), the LO itself
 * sits DEMOD_IF_OFFSET_HZ BELOW the selected/displayed station, and
 * the FFT (hence this whole panel) is centered on the LO, not the
 * station - s_tune_hz actually shows up center_mark_offset_px pixels
 * to the right of center (see sdr_spectrum_waterfall_tick()'s comment
 * and the red marker it draws there). Under ZOOM, on the other hand,
 * zoom_process_block() re-centers on s_tune_hz BEFORE decimating, so
 * the panel center genuinely IS s_tune_hz there. panel_center_hz
 * below picks the right one of those two, the EXACT same condition
 * center_mark_offset_px uses - so this scale, the red center marker,
 * and the demodulated-bandwidth tint all agree on which frequency
 * sits at which pixel. Getting this wrong would silently mislabel
 * every non-WFM reading by 48kHz (the whole 1X span) at 1X zoom - worth the extra
 * conditional to avoid.
 *
 * The two edges are the exact +/-half_span_hz boundary of whatever
 * the CURRENT zoom shows (96/48/24/12kHz - see the switch below), so
 * they track ZOOM automatically, same as the marker/tint do.
 * panel_center_hz +/- half_span_hz can, in principle, run outside
 * TUNE_MIN_HZ/MAX_HZ (e.g. tuned near the tunable floor, minus 96kHz
 * of span) - int64_t math + a floor-at-0 clamp keeps that from
 * wrapping a uint32_t negative into a huge bogus frequency; showing
 * an edge label below the tunable floor is harmless (there's no
 * corresponding "real" edge case to get wrong here, it's just a
 * label), unlike clamping the LO itself would be.
 *
 * MUST be redrawn on every actual frequency change, not just at
 * init/menu-close - "esta escala tiene que variar con la frecuencia".
 * Call sites: radio_screen_draw() (initial paint), menu_screen_close()
 * (also covers band-preset/mode changes applied from the menu, which
 * both close it on their way out - see menu_band_preset_callback()'s
 * and menu_mode_preset_callback()'s comments), tune_encoder_poll()'s
 * TUNE branch (knob retune) and spec_drag_tune_apply() (touch drag-
 * to-tune) - the two places s_tune_hz changes WITHOUT going through
 * the menu. Cheap enough (a handful of small draws, same cost class
 * as freq_display_draw() it's always paired with) to just redraw on
 * every change rather than diffing against the last-drawn value.
 */
#define SPEC_SCALE_TEXT_SIZE 2 /* was 1 - bumped 03/08/2026, per the
                                 * project owner: "casi no se ve" */

static spec_chrome_t s_chrome;      /* lo ultimo que se dibujo */
static uint8_t       s_chrome_init = 0U;

/*
 * ETAPA 3b: recoge de la radio lo que necesita el chrome del espectro.
 * Mismo patron que top_sync(): el dibujo no consulta el estado de la radio,
 * se le pasa ya resuelto, para que spec_chrome.c compile igual en el
 * simulador de host.
 */
static void chrome_sync(spec_chrome_t *c)
{
    uint32_t full_span_hz = spec_zoom_full_span_hz();
    uint32_t panel_center_hz = s_tune_hz;
    demod_mode_t mode = demod_am_get_mode();

    /* Misma condicion que usa sdr_spectrum_waterfall_tick() para
     * center_mark_offset_px: con low-IF activo el centro del PANEL no es la
     * frecuencia sintonizada, esta Fs/4 por debajo. */
    if (s_spec_zoom == SPEC_ZOOM_1X && demod_am_get_if_offset_active()) {
        panel_center_hz = s_tune_hz - demod_if_offset_hz();
        c->demod_px = (int16_t)(SPC_TRACE_W / 4U);
    } else {
        c->demod_px = 0;
    }

    c->db_min    = s_db_min;
    c->db_max    = s_db_max;
    c->center_hz = panel_center_hz;
    c->span_hz   = full_span_hz;
    c->cmap      = spectrum_colormap_lut();

    if (mode == DEMOD_MODE_USB) {
        c->band = SPC_BAND_UPPER;
    } else if (mode == DEMOD_MODE_LSB) {
        c->band = SPC_BAND_LOWER;
    } else if (mode == DEMOD_MODE_AM) {
        c->band = SPC_BAND_BOTH;
    } else {
        c->band = SPC_BAND_NONE;   /* NFM/WFM: el ancho no lo elige el usuario */
    }
    c->band_hz = k_audio_bw_hz[(uint8_t)demod_am_get_audio_bw()];
}

/*
 * ETAPA 3b: se conserva el nombre y las once llamadas que ya existian, y
 * solo cambia lo que hace por dentro - la regla la dibuja ahora
 * spec_chrome.c con las fuentes proporcionales, en su propia franja, y sin
 * pisar la traza (antes la franja empezaba en SPEC_Y+SPEC_H-24 = 320 y la
 * traza acababa en 321, asi que la traza le comia dos filas en cada frame).
 */
/*
 * QUIEN PUEDE PINTAR EN LA ZONA DEL ESPECTRO - 22/09/2026.
 *
 * Por el dueno del proyecto: "si entro a ajustes y salgo se ve bien hasta
 * que vuelve a salir la regla... basicamente sale cuando toco el encoder o
 * la pantalla".
 *
 * Y era exactamente eso. La regla de frecuencias se repinta cada vez que
 * cambia la sintonia -girar el mando, tocar el espectro para sintonizar-, y
 * esas dos llamadas no miraban QUE hay dibujado ahi ahora mismo. En CW o en
 * RTTY la zona del espectro la ocupa el panel digital, asi que cada toque
 * estampaba la regla de 96 kHz de RF encima del texto decodificado. Con el
 * menu abierto pasaba lo mismo.
 *
 * El guardia va AQUI DENTRO y no en las llamadas, por el mismo motivo que ya
 * explica spec_chrome_tick(): son mas de diez sitios y olvidar uno no da
 * error de compilacion, da una regla encima de otra cosa.
 *
 * Al no poder pintar se apunta que la chapa esta sucia (s_chrome_init = 0),
 * y asi el primer frame del espectro normal la repinta entera en vez de
 * comparar contra un estado que nunca llego a la pantalla.
 */
static uint8_t chrome_puede_pintar(void)
{
    if (digi_panel_active() || s_menu_open) {
        s_chrome_init = 0U;
        return 0U;
    }
    return 1U;
}

static void spec_span_labels_draw(void)
{
    if (!chrome_puede_pintar()) { return; }
    chrome_sync(&s_chrome);
    s_chrome_init = 1U;
    spec_chrome_draw_ruler(&s_chrome);
}

/* Repintado completo: canaleta de dB, regla y leyenda de color. */
static void spec_chrome_full_draw(void)
{
    if (!chrome_puede_pintar()) { return; }
    chrome_sync(&s_chrome);
    s_chrome_init = 1U;
    spec_chrome_draw(&s_chrome);
}

/*
 * Repintado POR COMPARACION, una vez por frame.
 *
 * La alternativa era llamar al repintado desde cada sitio que cambia la
 * escala, el zoom, la sintonia, el modo o la paleta. Eso son mas de diez
 * sitios y olvidar uno no da error de compilacion: da un eje que dice -80
 * donde pone -60. Comparar cuesta seis comparaciones por frame y no se
 * puede olvidar.
 */
static void spec_chrome_tick(void)
{
    spec_chrome_t nueva;

    /* Hoy este no llega a correr con el panel digital delante -el bucle
     * principal ya elige entre una cosa y la otra-, pero se comprueba igual:
     * asi "quien puede pintar aqui" se responde en un solo sitio y deja de
     * depender de que el reparto de mas arriba siga siendo el que es. */
    if (!chrome_puede_pintar()) { return; }

    chrome_sync(&nueva);
    if (!s_chrome_init) {
        s_chrome = nueva;
        s_chrome_init = 1U;
        spec_chrome_draw(&s_chrome);
        return;
    }
    if (spec_chrome_axis_changed(&s_chrome, &nueva)) {
        s_chrome = nueva;
        spec_chrome_draw_axis(&s_chrome);
    }
    if (spec_chrome_ruler_changed(&s_chrome, &nueva)) {
        s_chrome = nueva;
        spec_chrome_draw_ruler(&s_chrome);
    }
    s_chrome = nueva;
}


/*
 * Applies the CURRENT effective PGA gain (ceiling - backoff) to the
 * codec - the ONLY function allowed to call aic3204_set_pga_gain_db()
 * now (see s_rf_agc_enabled's declaration comment for why the two
 * former direct call sites - the encoder-driven PGA change, and the
 * WFM-boundary gain restore - both route through this instead now).
 *
 * Deliberately does NO UI redraw of its own (07/08/2026, fixed per
 * the project owner after testing - see menu_tile_rfagc_refresh()'s
 * comment for the corruption this used to cause): this gets called
 * from rf_agc_poll(), a BACKGROUND poll with no idea what's on screen
 * right now, so it must never touch anything inside MENU_AREA itself.
 * Callers that need a UI update after calling this do it themselves,
 * in whatever way is actually safe for their own context - see
 * rf_agc_poll()'s badges_draw() call (the right-column badge grid is
 * always visible, outside MENU_AREA, safe from any background
 * context) and the encoder-driven PGA handler's existing
 * settings_value_redraw() call (safe because being IN that handler
 * means the person is provably looking at the PGA detail view right
 * now).
 */
static void rf_agc_apply_pga(void)
{
    int32_t eff = (int32_t)s_pga_gain_db_x2 - (int32_t)s_rf_agc_backoff_x2;

    if (eff < PGA_MIN_X2) { eff = PGA_MIN_X2; }
    aic3204_set_pga_gain_db((float)eff * 0.5f);
}

/*
 * Brief mute around an Rin (input impedance) switch - see
 * aic3204_set_input_impedance()'s comment for why this is needed
 * (unlike the PGA gain register, Rin switching isn't soft-stepped, so
 * it's an abrupt analog reconnection that would otherwise pop).
 * Reuses whichever settle-mute already exists for the ACTIVE demod
 * mode (demod_am_reset_diag() covers AM/USB/LSB/NFM via
 * s_am_mute_remaining, demod_wfm_reset_diag() covers WFM via
 * s_wfm_mute_remaining - see both their comments in demod_am.c) -
 * same mechanism already proven to hide the mode-switch transient,
 * repurposed here for a different transient of the same general
 * shape (a sudden front-end level change the AGC needs a moment to
 * re-settle around before it's safe to listen to again).
 */
static void rf_agc_mute_for_transition(void)
{
    if (demod_am_get_mode() == DEMOD_MODE_WFM) {
        demod_wfm_reset_diag();
    } else {
        demod_am_reset_diag();
    }
}

/*
 * One step UP in Rin (10k->20k->40k) - see s_rf_agc_enabled's
 * declaration comment for the "why" and the net-6dB-step reasoning.
 * Only called from rf_agc_poll() once PGA backoff is already maxed
 * AND a new clip still came in - i.e. this is the LAST resort, not a
 * routine step.
 */
static void rf_agc_escalate_rin(void)
{
    s_rf_agc_rin_level++;
    s_rf_agc_backoff_x2 = (int16_t)(s_rf_agc_backoff_x2 - (int16_t)RF_AGC_RIN_STEP_X2);
    if (s_rf_agc_backoff_x2 < 0) { s_rf_agc_backoff_x2 = 0; }

    aic3204_set_input_impedance((aic3204_rin_t)s_rf_agc_rin_level);
    rf_agc_mute_for_transition();
    rf_agc_apply_pga();
    badges_draw();
    debug_print_dec("rf_agc: escalated Rin, level now (0=10k/1=20k/2=40k)", (uint32_t)s_rf_agc_rin_level);
}

/* One step DOWN in Rin - mirrors rf_agc_escalate_rin(), only called
 * once PGA backoff has fully recovered to 0 at the current Rin level
 * and the release cooldown has ALSO elapsed since the last clip. */
static void rf_agc_deescalate_rin(void)
{
    s_rf_agc_rin_level--;
    s_rf_agc_backoff_x2 = (int16_t)(s_rf_agc_backoff_x2 + (int16_t)RF_AGC_RIN_STEP_X2);
    if (s_rf_agc_backoff_x2 > (int16_t)RF_AGC_BACKOFF_MAX_X2) {
        s_rf_agc_backoff_x2 = (int16_t)RF_AGC_BACKOFF_MAX_X2;
    }

    aic3204_set_input_impedance((aic3204_rin_t)s_rf_agc_rin_level);
    rf_agc_mute_for_transition();
    rf_agc_apply_pga();
    badges_draw();
    debug_print_dec("rf_agc: de-escalated Rin, level now (0=10k/1=20k/2=40k)", (uint32_t)s_rf_agc_rin_level);
}





static void menu_tile_trace_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        uint8_t next = spectrum_get_heatmap_trace_white() ? 0U : 1U;

        spectrum_set_heatmap_trace_white(next);
        debug_print("spectrum: heatmap trace now ");
        debug_print(next ? "WHITE\n" : "color-matched\n");
        if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
    }
}

static void menu_tile_agc_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        agc_profile_cycle(); /* updates s_btn_agc_profile too - harmless, it's just not visible right now */
    }
}

static void menu_tile_nb_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        s_spec_smooth_passes = (uint8_t)((s_spec_smooth_passes + 1U) % (SPECTRUM_LINE_SMOOTH_MAX + 1U));
        spectrum_set_line_smooth(s_spec_smooth_passes); /* live - no re-init needed */
        debug_print_dec("spectrum line smooth passes", s_spec_smooth_passes);
    }
}

/* SPEC (trace style) behaves like AGC/SPT above, not like the DETAIL-
 * view group below - it's a 3-way cycle (HEATMAP->LINE->OUTLINE->
 * HEATMAP, see spectrum_set_style()'s comment in spectrum.h), so
 * cycling it directly on tap and staying on the grid is simpler and
 * just as clear as a dedicated detail view would be for only three
 * states. */
static void menu_tile_spec_style_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        spectrum_style_t next;
        const char *name;
        (void)name;

        switch (spectrum_get_style()) {
        case SPECTRUM_STYLE_HEATMAP: next = SPECTRUM_STYLE_LINE;    name = "LINE\n";    break;
        case SPECTRUM_STYLE_LINE:    next = SPECTRUM_STYLE_OUTLINE; name = "OUTLINE\n"; break;
        case SPECTRUM_STYLE_OUTLINE:
        default:                     next = SPECTRUM_STYLE_HEATMAP; name = "HEATMAP\n"; break;
        }
        spectrum_set_style(next);
        debug_print("spectrum: style now ");
        debug_print(name);
        if (s_settings_ready_for_autosave) { settings_mark_dirty(); } /* 07/09/2026 - was missing, see settings.h's comment */
    }
}

/* BW (AM/SSB audio filter width) - added 02/08/2026, replacing the
 * grid's BANDS tile (see s_menu_tile_bw's declaration comment).
 * Extended from a plain WIDE/NARROW toggle to a 3-way cycle
 * (4K0->2K3->1K8->4K0) the same day per the project owner, once the
 * main-screen BW badge itself became a second, real entry point (see
 * s_btn_audio_bw's declaration and audio_bw_button_callback() below) -
 * a cycle handles 3+ states as cleanly as a toggle handles 2, so this
 * keeps the same "cycle directly on tap, stay on the grid" treatment
 * as SPEC's HEATMAP<->LINE toggle rather than a DETAIL view - see
 * menu_tile_spec_style_callback()'s comment for that reasoning.
 * Applies live, same as SPEC - demod_am_process_raw() picks up the new
 * setting on its very next block.
 *
 * audio_bw_cycle() is the single shared step - both this grid tile's
 * callback AND s_btn_audio_bw's callback go through it, so the two
 * entry points can't drift out of sync with each other (same reasoning
 * agc_profile_cycle() already established for AGC's two entry points).
 */
static void audio_bw_cycle(void)
{
    audio_bw_t bw = demod_am_get_audio_bw();

    switch (bw) {
    case AUDIO_BW_4K0: bw = AUDIO_BW_2K3; break;
    case AUDIO_BW_2K3: bw = AUDIO_BW_1K8; break;
    case AUDIO_BW_1K8:
    default:            bw = AUDIO_BW_4K0; break;
    }
    demod_am_set_audio_bw(bw);
    /* En CW el que manda es el filtro de CW, que es otro camino (ver
     * ALPF_CW_STAGES en demod_am.c). El selector es el mismo para que no
     * haya dos mandos de ancho, pero lo que aplica es distinto. */
    demod_am_set_cw_bw_hz((float)k_cw_bw_hz[(uint8_t)bw]);
    debug_print("audio filter: now ");
    debug_print(k_audio_bw_labels[(uint8_t)bw]);
    debug_print("\n");
    if (s_settings_ready_for_autosave) {
        settings_mark_dirty();
    }

    /* badges_draw() (not just a direct s_btn_audio_bw update) because
     * it's the function that already knows how to compute bw_label
     * correctly for the CURRENT mode - keeps this in one place rather
     * than duplicating that switch here too. Cheap: a handful of small
     * rect+text draws, same cost class as the mode-change call sites
     * that already call the whole thing (e.g.
     * menu_band_preset_callback()). */
    badges_draw();
    /* s_menu_tile_bw only actually exists on screen while the grid is
     * showing AND the RADIO page (BW's home page - see the "Settings
     * grid PAGES" comment) is the one currently selected - redrawing
     * it any other time would paint tile graphics at s_menu_tile_bw's
     * STALE coordinates, straight into whatever is actually on screen
     * there right now (the live spectrum/waterfall panel if the menu
     * is closed - MENU_AREA overlaps that region, see its comment -
     * or the UI/HW page's own tiles otherwise). Same menu-only-repaint
     * guard settings_value_redraw() and friends already use, extended
     * with the page check the paged-grid redesign added. */
    if (s_menu_open && s_menu_page == MENU_PAGE_RADIO) {
    }
}


static void menu_tile_bw_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        /* Unconditional, unlike s_btn_audio_bw's callback below - this
         * is a deliberate menu action, not a live mode-dependent
         * readout, so cycling it while in NFM/WFM to get AM/SSB's
         * filter ready ahead of time is fine (see
         * demod_am_set_audio_bw()'s "harmless in NFM/WFM" comment in
         * demod_am.h). menu_tile_bw_refresh() (called inside
         * audio_bw_cycle() via s_menu_open) repaints THIS tile
         * regardless, since the menu is obviously open right now if
         * this callback fired at all. */
        audio_bw_cycle();
    }
}


static void menu_tile_rtty_baud_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        s_rtty_baud_idx = (uint8_t)((s_rtty_baud_idx + 1U) % RTTY_BAUD_COUNT);
        rtty_set_baud(k_rtty_baud_values[s_rtty_baud_idx]);
        debug_print("rtty: baud now ");
        debug_print(k_rtty_baud_labels[s_rtty_baud_idx]);
        debug_print("\n");
    }
}

/* s_btn_audio_bw's callback - unlike menu_tile_bw_callback() above,
 * this one is MODE-GATED: it's a live, always-visible readout (see
 * badges_draw()'s comment), so cycling it while its own label is
 * showing NFM's unrelated fixed "6K3" would silently change AM/SSB's
 * filter with zero visible feedback right now - confusing, not
 * "harmless". WFM (01/09/2026) is NOT gated out anymore - its badge
 * genuinely reflects and controls WFM's own audio filter now, so
 * tapping it there is exactly as meaningful as tapping it in AM/USB/
 * LSB. Tapping it in NFM remains a no-op. */
static void audio_bw_button_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        demod_mode_t mode = demod_am_get_mode();

        if (mode == DEMOD_MODE_AM || mode == DEMOD_MODE_USB || mode == DEMOD_MODE_LSB
            || mode == DEMOD_MODE_WFM) {
            audio_bw_cycle();   /* en CW cambia el ancho del filtro de CW */
        } else {
            debug_print("audio filter: BW badge tap ignored - not in AM/USB/LSB/WFM\n");
        }
    }
}


/* ZOOM cycles 1X -> 2X -> 4X -> 8X -> 1X, same "direct cycle, stay on
 * the grid" behavior as AGC/SPT/SPEC - see spec_zoom_t's comment for
 * what each level actually does. */
static void menu_tile_zoom_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        switch (s_spec_zoom) {
        case SPEC_ZOOM_1X: s_spec_zoom = SPEC_ZOOM_2X; break;
        case SPEC_ZOOM_2X: s_spec_zoom = SPEC_ZOOM_4X; break;
        case SPEC_ZOOM_4X: s_spec_zoom = SPEC_ZOOM_8X; break;
        case SPEC_ZOOM_8X:
        default:            s_spec_zoom = SPEC_ZOOM_1X; break;
        }
        debug_print_dec("spectrum: zoom now", (uint32_t)1U << (uint8_t)s_spec_zoom);
    }
}

/*
 * Shared by all BAND_PRESET_COUNT preset tiles - which preset arrives
 * via user_data (see menu_bands_show(), where each tile's user_data is
 * set to its own index into k_band_presets[], cast through a
 * uintptr_t round-trip). Applies the whole preset in one go (frequency
 * + mode + step) and returns straight to the main radio screen - same
 * "close the menu so the result is immediately visible" reasoning the
 * SQUELCH/BACKLIGHT/SCALE/VOLUME/SMOOTH tiles already use, just with
 * nothing left to adjust afterward (unlike those, a band preset is a
 * one-shot jump, not an ongoing knob target).
 */
/*
 * Applies a demod mode change, including the WFM<->96kHz rate switch
 * when the change crosses that boundary (05/08/2026, WFM's 192kHz
 * reactivation - see demod_am.c's demod_wfm_process_raw() comment for
 * why WFM alone needs a different rate, and aic3204_rate_switch_
 * reset()'s comment in aic3204.c for exactly what the codec side does
 * and why the order below matters). BOTH places that change mode
 * (band presets, the MODE picker) call this instead of demod_am_set_
 * mode() directly - a mode change from either one can cross the WFM
 * boundary just as easily as the other.
 *
 * SEQUENCE (order matters throughout - see aic3204_rate_switch_reset()'s
 * comment in aic3204.c for the full story of why this exact order was
 * needed, found via real hardware testing across several earlier
 * attempts that each glitched a different way):
 *   1. Stop BOTH DMA channels (capture + TX stream) - reprogramming
 *      codec clock dividers while the I2S bus is actively clocking
 *      risks exactly the bus-contention/RXORERR history this project
 *      already fought once (see gd32_i2s.h's architecture comment).
 *   2. Reset the codec (aic3204_rate_switch_reset(), a real hardware
 *      nRESET pulse) - the codec falls completely silent, no BCLK/WS
 *      at all, until step 4 below.
 *   3. Resize both DMA channels' transfer counts for the NEW rate and
 *      swap which function receives each captured block
 *      (demod_am_process_raw <-> demod_wfm_process_raw) - these have
 *      entirely separate buffers/state (see demod_wfm_process_raw()'s
 *      comment on the "ruta separada" decision), so this swap alone
 *      is what actually routes audio through the right pipeline.
 *   4. Re-arm both DMA channels (sdr_rx_start()/gd32_i2s_stream_
 *      start()) - the GD32's own I2S peripherals resync here too,
 *      and critically, this happens WHILE the codec is STILL SILENT
 *      from step 2 - the GD32 side is listening and ready before the
 *      codec ever produces a single real clock edge, matching how
 *      cold boot naturally orders things. Getting this ordering
 *      backwards (codec clocking again before the GD32 side resyncs)
 *      is what caused a persistent SPI_STAT_FERR that neither a full
 *      register reset NOR a genuine hardware reset alone could fix -
 *      see aic3204_rate_switch_reset()'s comment for that whole story.
 *   5. NOW reconfigure the codec's registers for the new rate
 *      (aic3204_configure_rate()) - BCLK/WS start coming back partway
 *      through this call, straight into a GD32 side that's already
 *      armed and listening from the first real edge.
 *   6. Power the codec's ADC/DAC back UP
 *      (aic3204_set_rate_power_up()) - only NOW does real audio data
 *      start flowing, straight into an already-armed DMA path. Doing
 *      this any earlier left a window where real samples arrived with
 *      nothing draining them - a continuous receive overrun that read
 *      as "sounds like NFM"/bandwidth-limited, not a DSP bug, and
 *      never recovered on its own even after the DMA was eventually
 *      armed.
 *
 * A brief audio/spectrum dropout during this sequence is EXPECTED,
 * not a bug - see the project owner's own acknowledgment of this
 * tradeoff when the dual-rate approach was first discussed.
 *
 * KNOWN GAP as of 05/08/2026: the panadapter's own FFT/spectrum
 * pipeline (fft.c, main.c's spectrum buffers) is NOT YET resized for
 * WFM's 512-sample blocks by this function - it stays fixed at
 * FFT_SIZE=128 regardless of which rate is active. The AUDIO path
 * above is fully correct either way; the SPECTRUM DISPLAY while in
 * WFM is the remaining piece, tracked separately, not blocking this
 * audio-path change from being useful on its own.
 */
/*
 * *** 05/08/2026, added after raw I/Q sample dumps proved the real
 * root cause *** - the AGC/mute work above turned out to be treating
 * a symptom, not the disease. Raw sample dumps (added to
 * demod_wfm_process_raw()/demod_am_process_raw() for one round of
 * testing) showed that on some rate-switches the incoming I/Q looks
 * exactly like real signal (small, smoothly-varying values, matching
 * the known-good boot capture) - and on OTHERS, from the very first
 * sample, it's wild alternating near-full-scale swings that don't
 * look like RF content at all (e.g. one pair +21516/+21502 followed
 * two samples later by an almost exact negation, -21503/-20482) -
 * the classic signature of an I2S slave locking onto the WS (word
 * select / frame sync) line at the WRONG bit-phase when the
 * peripheral is disabled and re-enabled. Which phase it lands on
 * depends on the exact clock edge at the moment of re-enable, so it's
 * genuinely a coin-flip per switch - explaining why no amount of
 * settle-muting or AGC tuning ever fixed the reported "ruido/pitido
 * fuerte, sin voz reconocible", since real numbers were being
 * computed from corrupted samples the whole session through, not
 * just for the first few blocks.
 *
 * Fix: after arming and powering up, capture one real block and check
 * it for that corruption signature - if found, redo the disable/
 * re-enable/rearm sequence and check again, up to
 * RX_LOCK_MAX_ATTEMPTS times. This can't fix WHICH phase the hardware
 * locks onto, but re-rolling the coin flip a few times in a row is
 * cheap and, empirically, very unlikely to land on the bad phase
 * every single time.
 */
/*
 * *** 05/08/2026, RX_LOCK RETRY LOOP REMOVED - see apply_demod_mode()
 * below *** - this whole mechanism existed to paper over the
 * nondeterminism of the OLD live-switch approach (partial resync of
 * already-configured peripherals): since which WS bit-phase the
 * hardware happened to lock onto on any given disable/re-enable was a
 * coin flip, retrying up to RX_LOCK_MAX_ATTEMPTS times was a cheap way
 * to avoid landing on a bad one. The rewritten apply_demod_mode() now
 * does a FULL teardown/rebuild of the I2S peripherals AND the codec on
 * every switch - the same sequence cold boot always used, which real
 * hardware testing has never once caught landing on a bad phase. No
 * coin flip left to retry. rx_capture_looks_corrupted() below is kept
 * as a single post-switch sanity check/log line (informational only,
 * no retry) rather than removed outright - still useful evidence if
 * this rewrite ever needs revisiting.
 */
#define RX_LOCK_JUMP_THRESHOLD 16000  /* ~half full-scale int16 */
#define RX_LOCK_BAD_FRACTION_NUM 1U   /* flag corrupted if more than */
#define RX_LOCK_BAD_FRACTION_DEN 4U   /* 1/4 of samples show a wild jump */
#define RX_LOCK_WAIT_TIMEOUT_MS 20U

/* Own buffers, sized like main.c's later s_rx_i/s_rx_q (declared much
 * further down in this file, near the spectrum polling code, so not
 * yet visible up here) - kept separate rather than reordering
 * existing declarations, to keep this change minimal and self-
 * contained. */
static int16_t s_rx_lock_check_i[SDR_RX_BLOCK_SAMPLES_MAX];
static int16_t s_rx_lock_check_q[SDR_RX_BLOCK_SAMPLES_MAX];

/* Waits for one fresh captured block (via the same poll the
 * spectrum/panadapter uses) and checks it for the wild-swing
 * corruption signature described above. Returns 1 if the capture
 * looks corrupted (or never arrived within the timeout - treated the
 * same as corrupted, since either way this lock attempt isn't
 * trustworthy), 0 if it looks like plausible real signal. */
/* Herramienta de diagnostico: se compila siempre pero hoy no la llama nadie
 * (su punto de uso esta detras de un #if desactivado). Se marca como
 * posiblemente sin usar en vez de borrarla, para no perder el codigo. */
__attribute__((unused))
static uint8_t rx_capture_looks_corrupted(void)
{
    uint32_t start_ms = g_msticks;
    uint32_t n;
    uint32_t total;
    uint32_t bad_count = 0U;

    while (sdr_rx_poll_block_iq(s_rx_lock_check_i, s_rx_lock_check_q) == 0U) {
        if ((g_msticks - start_ms) >= RX_LOCK_WAIT_TIMEOUT_MS) {
            debug_print("rx_lock: no capture arrived within timeout - treating as bad\n");
            return 1U;
        }
    }

    total = sdr_rx_get_block_samples();
    for (n = 1U; n < total; n++) {
        int32_t di = (int32_t)s_rx_lock_check_i[n] - (int32_t)s_rx_lock_check_i[n - 1U];
        int32_t dq = (int32_t)s_rx_lock_check_q[n] - (int32_t)s_rx_lock_check_q[n - 1U];
        if (di < 0) { di = -di; }
        if (dq < 0) { dq = -dq; }
        if (di > RX_LOCK_JUMP_THRESHOLD || dq > RX_LOCK_JUMP_THRESHOLD) {
            bad_count++;
        }
    }

    if (bad_count * RX_LOCK_BAD_FRACTION_DEN > total * RX_LOCK_BAD_FRACTION_NUM) {
        debug_print_dec("rx_lock: capture looks corrupted, wild-jump samples", bad_count);
        return 1U;
    }
    return 0U;
}

/*
 * *** 05/08/2026, REWRITTEN - "full reinit instead of live resync" ***
 *
 * Every earlier version of this function tried to keep the I2S
 * peripherals' and codec's EXISTING configuration and nudge them back
 * into sync for the new rate - a disable/re-enable of I2SEN, a partial
 * codec register rewrite, then (after that was proven insufficient) a
 * full codec register replay via a genuine nRESET - while the GD32
 * side only ever got the lighter disable/re-enable treatment. Real
 * hardware logs kept finding the same result regardless of exactly
 * which combination was tried: FERR fires after a live switch and
 * NEVER clears again, while running a rate from a genuine COLD BOOT
 * (gd32_i2s_init_slave()+aic3204_phase2_init(), once, before the main
 * loop starts) is rock solid, FERR always 0, indefinitely - confirmed
 * directly by testing a build that boots straight into 192kHz and
 * never switches at all: perfect audio, no FERR, for the entire
 * session.
 *
 * That gap (cold boot always clean, ANY live switch never fully
 * clean) means the difference isn't which registers get rewritten -
 * it's that a live switch was never actually doing the SAME thing cold
 * boot does. This version fixes that literally: every switch that
 * crosses the 96kHz/192kHz boundary now runs gd32_i2s_init_slave(rate)
 * (gd32_i2s.c) - a FULL spi_i2s_deinit()/PLLI2S reconfigure/i2s_init()/
 * GPIO-AF replay, not just re-enabling what was already configured -
 * immediately followed by the exact same codec bring-up cold boot
 * uses (aic3204_rate_switch_reset() + aic3204_configure_rate() +
 * aic3204_start_bclk_wclk() + aic3204_set_rate_power_up()) and the
 * exact same DMA bring-up (sdr_rx_bringup()/gd32_i2s_stream_arm(),
 * which sdr_rx_init()/gd32_i2s_dma_start_stream() now also call under
 * the hood for cold boot, so there is only ONE bring-up path left,
 * not a separate "lighter" one for live switches to drift out of sync
 * with).
 */
/*
 * s_nonwfm_use_48k/s_current_rate declared near main()'s own boot
 * sequence (needed there too - see this file's earlier declaration
 * comment for the full "birdie escape hatch" reasoning).
 */
/*
 * GANANCIA DE ENTRADA EN VHF - 22/09/2026.
 *
 * Por el dueno del proyecto, midiendo con una emisora de FM comercial: "si
 * en ganancia de entrada pongo 28,5 dB la radio empieza a oirse por encima
 * del ruido, si llego al tope 47,5 se oye perfectamente". Y el S-meter se
 * mueve, o sea que la señal LLEGA: lo que falta es nivel a la entrada del
 * codec, no antena.
 *
 * Eso es perdida del mezclador en VHF, y ningun cambio de software la
 * recupera: a 100 MHz el conmutador del QSD trabaja a 400 MHz y su
 * eficiencia de apertura se desploma. Lo que SI puede hacer el firmware es
 * dejar de obligarte a subir el mando cada vez que entras en WFM.
 *
 * Asi que WFM usa el tope de ganancia y punto. No hay nada que recordar
 * porque no hay nada que elegir: la medida dice que hace falta todo lo que
 * hay. Y lo que se guarda en CONFIG.CSV es s_pga_hf_x2, tu ajuste de
 * SIEMPRE, no el valor vivo - si no, pasar por WFM con el autoguardado
 * activo te dejaria 47,5 dB puestos en HF la proxima vez que encendieras.
 * Es el mismo fallo que tenia el atenuador antes del suelo manual.
 */
static int16_t s_pga_hf_x2 = -1;   /* -1 = aun sin inicializar */

static void apply_demod_mode(demod_mode_t mode)
{
    uint8_t will_be_wfm = (mode == DEMOD_MODE_WFM) ? 1U : 0U;
    uint8_t era_wfm = (demod_am_get_mode() == DEMOD_MODE_WFM) ? 1U : 0U;

    if (s_pga_hf_x2 < 0) { s_pga_hf_x2 = s_pga_gain_db_x2; }

    if (will_be_wfm && !era_wfm) {
        s_pga_hf_x2 = s_pga_gain_db_x2;   /* guarda lo tuyo antes de pisarlo */
        s_pga_gain_db_x2 = (int16_t)PGA_MAX_X2;
        rf_agc_apply_pga();
        debug_print("pga: WFM, al tope por perdida de VHF\n");
    } else if (!will_be_wfm && era_wfm) {
        s_pga_gain_db_x2 = s_pga_hf_x2;
        rf_agc_apply_pga();
        debug_print_dec("pga: fuera de WFM, restaurado a x2", (uint32_t)s_pga_gain_db_x2);
    } else if (!will_be_wfm) {
        s_pga_hf_x2 = s_pga_gain_db_x2;   /* fuera de WFM, lo vivo ES lo tuyo */
    }
    aic3204_rate_t desired_rate = will_be_wfm ? AIC3204_RATE_192K :
        (s_nonwfm_use_48k ? AIC3204_RATE_48K : AIC3204_RATE_96K);

    /* See this function's own comment history: setting s_mode BEFORE
     * anything touches the DMA avoids a real race where the ISR could
     * read a stale mode for the first several blocks after a switch. */
    demod_am_set_mode(mode);

    if (desired_rate != s_current_rate) {
        aic3204_rate_t rate = desired_rate;
        uint32_t block_samples = will_be_wfm ? SDR_RX_BLOCK_SAMPLES_WFM : SDR_RX_BLOCK_SAMPLES;

        /* Only the CLEAN result of this bring-up should ever reach the
         * real demodulator - keep the block hook detached throughout
         * (sdr_rx.c's ISR already skips calling a NULL hook safely). */
        sdr_rx_set_block_hook(0);

        /* 1. Stop both DMA channels cleanly before tearing down the
         *    peripherals underneath them. */
        sdr_rx_stop();
        gd32_i2s_stream_stop();

        /* 2. Codec falls fully silent - genuine hardware nRESET, no
         *    BCLK/WCLK at all. */
        aic3204_rate_switch_reset();

        /* 3. FULL teardown/rebuild of SPI1/I2S1_ADD for the new rate -
         *    the actual fix (see this function's header comment).
         *    Also re-arms DMA0/CH4 with silence, same as cold boot. */
        gd32_i2s_init_slave(rate);

        /* 4. Codec clock tree configured for the new rate (PLL,
         *    NDAC/MDAC/DOSR, NADC/MADC/AOSR) - BCLK/WCLK still NOT
         *    driven yet (see aic3204_start_bclk_wclk()'s own comment).
         *    ADC/DAC left powered down. */
        aic3204_configure_rate(rate);

        /* 5. Both DMA channels armed and both I2S peripherals already
         *    freshly enabled (from step 3) and listening - fresh,
         *    matching cold boot's own ordering exactly. */
        sdr_rx_bringup(block_samples);
        gd32_i2s_stream_arm(block_samples);

        /* 6. NOW the codec starts driving BCLK/WCLK - the GD32 side is
         *    already listening, so this is the first real edge it
         *    sees. */
        aic3204_start_bclk_wclk(rate);

        /* 7. ADC/DAC power up - both DMA channels already armed and
         *    waiting, so nothing overruns. */
        aic3204_set_rate_power_up();

        /* Informational only now (see this section's header comment) -
         * no retry, just a log line confirming whether this bring-up
         * looks clean, the same way cold boot's own diagnostics do. */
        debug_print(rx_capture_looks_corrupted()
                        ? "rx_lock: *** capture looks corrupted after full reinit - "
                          "this should not happen; treat as a real regression, not a "
                          "coin flip to retry ***\n"
                        : "rx_lock: capture looks clean after full reinit\n");

        if (will_be_wfm) {
            sdr_rx_set_block_hook(demod_wfm_process_raw);
            demod_wfm_reset_diag(); /* fresh diagnostic log for this WFM entry - see its own comment */
        } else {
            sdr_rx_set_block_hook(demod_am_process_raw);
            demod_am_reset_diag(); /* fresh diagnostic log for this AM/SSB/LSB/NFM entry - see its own comment */
        }

        switch (rate) {
        case AIC3204_RATE_192K:
            debug_print("mode: switched INTO WFM - codec/DMA now at 192kHz (full reinit)\n");
            break;
        case AIC3204_RATE_48K:
            debug_print("mode: rate change - codec/DMA now at 48kHz (full reinit)\n");
            break;
        case AIC3204_RATE_96K:
        default:
            debug_print("mode: rate change - codec/DMA now at 96kHz (full reinit)\n");
            break;
        }

        /*
         * *** 05/08/2026, added alongside the FULL-RESET rate-switch
         * fix in aic3204.c *** - aic3204_set_rate_registers() now runs
         * a genuine software reset every time (see its own comment for
         * why: fixes the persistent FERR that the old partial register
         * rewrite left behind), which resets DAC volume and MIC_PGA
         * gain back to aic3204_phase2_init()'s captured baseline
         * (0dB / 20dB) along with everything else. Without this, any
         * volume/PGA adjustment the person made would silently get
         * wiped on the very next mode change that crosses the WFM
         * boundary - re-apply whatever they actually have dialed in
         * (s_volume_db_x2/s_pga_gain_db_x2 are already the live,
         * current values regardless of how they got there) now that
         * the codec is back up and listening. PGA goes through
         * rf_agc_apply_pga() (07/08/2026) rather than a direct
         * aic3204_set_pga_gain_db() call, so a mode change that
         * crosses the WFM boundary while the RF-AGC is actively backed
         * off re-applies the EFFECTIVE gain (ceiling - backoff), not
         * the raw ceiling - otherwise the codec reset above would
         * silently undo an active backoff and risk an immediate re-clip
         * right after the switch. */
        aic3204_set_volume_db((float)s_volume_db_x2 * 0.5f);
        rf_agc_apply_pga();
        /* *** 01/09/2026, found alongside the ATT tile work *** -
         * aic3204_configure_rate() above unconditionally rewrites
         * P1R52/54/55/57 back to the captured 10k baseline (see its
         * own "ADC input routing" comment) as part of the SAME
         * captured register sequence that resets DAC volume/MIC_PGA -
         * exactly the state-loss bug the volume/PGA re-apply just
         * above already fixed for those two, but Rin was missed: if
         * RFAGC's auto-escalation (or a manual ATT tap) had the codec
         * sitting at 20k/40k before this switch, s_rf_agc_rin_level
         * still SAYS 20k/40k afterward while the codec itself is
         * silently back at 10k - a real desync, not just a cosmetic
         * one, since rf_agc_poll()'s escalate/deescalate math assumes
         * s_rf_agc_rin_level matches hardware. Re-apply it here too,
         * same "whatever's actually live, regardless of how it got
         * there" reasoning as the volume/PGA re-apply - no extra mute
         * needed, the demod_wfm_reset_diag()/demod_am_reset_diag()
         * call above already just armed this mode entry's own settle
         * window. */
        if (s_rf_agc_rin_level != 0U) {
            aic3204_set_input_impedance((aic3204_rin_t)s_rf_agc_rin_level);
        }

        /* *** 01/09/2026, added alongside the 48kHz rate option ***
         * - demod_am.c needs to know which of its two coefficient
         * sets (96kHz/48kHz) to actually run, independent of WFM -
         * see demod_am_set_active_rate()'s own comment in demod_am.h.
         * Harmless (and correctly a no-op re-init) when desired_rate
         * is AIC3204_RATE_192K (WFM) too, since is_48k just resolves
         * to 0 in that case, same as the AIC3204_RATE_96K case -
         * WFM never actually reads anything this call sets. */
        demod_am_set_active_rate((desired_rate == AIC3204_RATE_48K) ? 1U : 0U);

        s_current_rate = desired_rate;
    }

    /* See s_settings_ready_for_autosave's comment (same reasoning as
     * apply_lo_tune()'s own settings_mark_dirty() call). */
    if (s_settings_ready_for_autosave) {
        settings_mark_dirty();
    }
}

static void menu_band_preset_callback(void *widget, ui_event_t event, void *user_data)
{
    uintptr_t idx = (uintptr_t)user_data;

    (void)widget;
    if (event == UI_EVENT_RELEASE && idx < BAND_PRESET_COUNT) {
        const band_preset_t *p = &k_band_presets[idx];

        s_tune_hz = p->freq_hz;
        set_tune_step_idx(p->step_idx);
        apply_demod_mode(p->mode);
        apply_lo_tune(s_tune_hz);

        debug_print("bands: preset applied -> ");
        debug_print(p->label);
        debug_print("\n");

        /* menu_screen_close() only restores the spectrum/waterfall
         * panel + span labels (see its own comment) - it doesn't know
         * the frequency/mode/step actually CHANGED (as opposed to just
         * having been hidden behind the menu, unchanged, the way it is
         * for every other tile) - so those three readouts (and the
         * BW badge, which is mode-dependent) need an explicit repaint
         * here, not just a restore. */
        freq_display_draw();
        mode_display_draw();
        step_display_draw();
        badges_draw();
        menu_screen_close();
    }
}

/*
 * SQUELCH/BACKLIGHT/SCALE/VOLUME/SMOOTH tiles all do the SAME thing on
 * tap now (31/07/2026): open a DETAIL view for that target instead of
 * just selecting it and bouncing back to the main screen - see
 * s_menu_screen's declaration comment on why the old behavior wasn't
 * REAL interaction. One tiny callback per tile (rather than one
 * generic callback reading which ui_button_t* fired) because
 * ui_button_t's on_event already gets `widget` for exactly that kind
 * of dispatch, but menu_detail_show() takes an encoder_target_t, not a
 * widget pointer - a switch-on-widget-pointer indirection here would
 * be MORE code than five one-liners, not less.
 */
static void menu_tile_squelch_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) { menu_detail_show(ENCODER_TARGET_SQUELCH); }
}

static void menu_tile_backlight_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) { menu_detail_show(ENCODER_TARGET_BACKLIGHT); }
}

static void menu_tile_scale_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) { menu_detail_show(ENCODER_TARGET_SCALE); }
}

static void menu_tile_volume_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) { menu_detail_show(ENCODER_TARGET_VOLUME); }
}

static void menu_tile_pga_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) { menu_detail_show(ENCODER_TARGET_PGA); }
}

/*
 * SHIFT (DIG page) - opens the same DETAIL view (ENCODER_TARGET_
 * RTTY_SHIFT) tune_encoder_poll()'s ENCODER_TARGET_RTTY_SHIFT branch
 * and menu_detail_value_redraw()'s ENCODER_TARGET_RTTY_SHIFT case
 * already handle in full (both added 08/08/2026 alongside
 * rtty_set_shift_hz()) - only the grid TILE itself (this callback +
 * menu_tile_rtty_shift_refresh() below) was still missing until
 * 09/08/2026, when it moved here from its original planned home on
 * RADIO (which had already reached 8/8 - see this file's "Settings
 * grid PAGES" comment for the DIG page this and BAUD/INV now live on).
 */
static void menu_tile_rtty_shift_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) { menu_detail_show(ENCODER_TARGET_RTTY_SHIFT); }
}

/*
 * Tono del CW: rango continuo, asi que abre la pantalla de detalle con
 * el mando y los botones grandes, igual que el desplazamiento del RTTY.
 * Es el ajuste que de verdad se toca, porque sintonizar CW consiste en
 * mover el pitido hasta el tono al que escucha el detector, y a veces es
 * mas comodo mover el detector que el mando.
 */
static void menu_tile_cw_tone_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) { menu_detail_show(ENCODER_TARGET_CW_TONE); }
}

/*
 * Velocidad: una casilla que cicla, no una pantalla de detalle, y a
 * proposito. Esto NO fija la velocidad: el decodificador la mide y la
 * persigue solo, y esto solo dice por donde empieza a buscar. Darle una
 * pantalla con "-" y "+" invitaria a ajustarlo finamente, que es tiempo
 * perdido: de 10 a 45 PPM engancha solo partiendo de 20.
 */
static void menu_tile_cw_wpm_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        s_cw_wpm_idx = (uint8_t)((s_cw_wpm_idx + 1U) % CW_WPM_OPCIONES);
        cw_set_wpm_hint((float)k_cw_wpm[s_cw_wpm_idx]);
        settings_value_redraw();
    }
}

/*
 * RFAGC tile: a plain toggle (per the project owner, "como el botón
 * NR"), not a detail view - there's nothing to dial in, just on/off.
 * Turning OFF immediately drops any active backoff AND Rin escalation,
 * restoring the PGA to the plain ceiling value at 10k input impedance,
 * rather than leaving either frozen in place - "off" should mean
 * "back to exactly what the manual PGA control says, at the baseline
 * input configuration", unsurprising even if you switch it off
 * mid-backoff or mid-Rin-escalation.
 */
static void menu_tile_rfagc_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        s_rf_agc_enabled = s_rf_agc_enabled ? 0U : 1U;
        debug_print(s_rf_agc_enabled ? "rf_agc: on\n" : "rf_agc: off\n");
        if (!s_rf_agc_enabled) {
            s_rf_agc_backoff_x2 = 0;
            /* Vuelve al SUELO, no a cero: "apagado" significa "exactamente
             * lo que dicen los mandos manuales", y el atenuador es uno de
             * ellos. Antes apagar el AGC te tiraba la atenuacion que habias
             * elegido a mano. */
            if (s_rf_agc_rin_level != s_att_suelo) {
                s_rf_agc_rin_level = s_att_suelo;
                aic3204_set_input_impedance((aic3204_rin_t)s_rf_agc_rin_level);
                rf_agc_mute_for_transition(); /* Rin change - see its own comment for why this needs a mute */
            }
        }
        rf_agc_apply_pga();
        /* Both safe here: we're provably ON this exact tile (its own
         * tap callback) for the grid redraw, and badges live outside
         * MENU_AREA entirely - see menu_tile_rfagc_refresh()'s and the
         * OVR badge's comments. Only needed for the OFF case (drops
         * an active backoff straight to 0, so OVR should go dark
         * immediately rather than wait for the next poll), but cheap
         * enough to just always do it. */
        badges_draw();
    }
}


static void menu_tile_att_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;

    if (event == UI_EVENT_RELEASE) {
        /* El toque mueve el SUELO, y se aplica ya: si estabas por encima
         * porque el automatico habia escalado, bajar a lo que acabas de
         * pedir es lo que se espera de un control manual. Si la senal lo
         * pide, el AGC volvera a subir - pero desde aqui, no desde cero. */
        s_att_suelo = (uint8_t)((s_att_suelo + 1U) % 3U);
        s_rf_agc_rin_level = s_att_suelo;
        aic3204_set_input_impedance((aic3204_rin_t)s_rf_agc_rin_level);
        rf_agc_mute_for_transition();
        debug_print_dec("att: suelo manual ahora (0=10k/1=20k/2=40k)", (uint32_t)s_att_suelo);
        badges_draw(); /* 07/09/2026 - keeps the new top-strip ATT badge in sync with manual changes too, not just rf_agc_escalate_rin()/deescalate_rin()'s automatic ones */
    }
}


static void menu_tile_ifbw_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;

    if (event == UI_EVENT_RELEASE) {
        wfm_ifbw_t bw = (demod_am_get_wfm_ifbw() == WFM_IFBW_WIDE) ? WFM_IFBW_NARROW : WFM_IFBW_WIDE;

        demod_am_set_wfm_ifbw(bw);
        debug_print(bw == WFM_IFBW_NARROW ? "wfm ifbw: now 80K (narrow)\n" : "wfm ifbw: now 96K (wide/off)\n");
    }
}


static void menu_tile_specagc_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;

    if (event == UI_EVENT_RELEASE) {
        s_spec_agc_enabled = (uint8_t)(s_spec_agc_enabled ? 0U : 1U);
        debug_print(s_spec_agc_enabled ? "spectrum AGC: on\n" : "spectrum AGC: off\n");
    }
}


static void menu_tile_rate_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;

    if (event == UI_EVENT_RELEASE) {
        s_nonwfm_use_48k = (uint8_t)(s_nonwfm_use_48k ? 0U : 1U);
        debug_print(s_nonwfm_use_48k ? "nonwfm rate: 48K selected\n" : "nonwfm rate: 96K selected\n");
        badges_draw(); /* updates the RATE badge in the status strip - see its comment */

        if (demod_am_get_mode() != DEMOD_MODE_WFM) {
            apply_demod_mode(demod_am_get_mode());
            apply_lo_tune(s_tune_hz);
        }
    }
}

static void menu_tile_nr_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) { menu_detail_show(ENCODER_TARGET_NR); }
}


static void menu_tile_rtty_inv_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        rtty_set_station_inverted(!rtty_get_station_inverted());
    }
}


static void menu_tile_speaker_pa_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        speaker_pa_set_enabled(!s_speaker_pa_enabled);
        debug_print("speaker PA: now ");
        debug_print(s_speaker_pa_enabled ? "ON\n" : "OFF (headphones only)\n");
        speaker_icon_draw(); /* 07/09/2026 - instant feedback, don't wait for some unrelated badges_draw() call elsewhere */
        if (s_settings_ready_for_autosave) { settings_mark_dirty(); } /* 07/09/2026 - was missing, see settings.h's comment */
    }
}

/*
 * SLEEP (HW page) - one-shot action, not a toggle: fires
 * screen_sleep_enter() and that's it, same shape as EXIT
 * (menu_tile_exit_callback() just below) rather than the cycle/
 * detail-view tiles elsewhere on this grid - there's no state to
 * flip back and forth here, the WHOLE point is "stop looking at the
 * screen until the encoder wakes it back up" (see screen_sleep_
 * enter()'s comment).
 */
static void menu_tile_sleep_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        screen_sleep_enter();
    }
}

/*
 * Fires once the touch_calib.c wizard has computed and APPLIED a good
 * calibration (touch_calib_start()'s on_done - see touch_calib.h's
 * comment; not called if the wizard is cancelled). The wizard drew
 * over the ENTIRE screen (top bar, right column, bottom bar included -
 * unlike the settings grid, which only ever covers MENU_AREA), so a
 * full radio_screen_draw() is needed here, not menu_screen_close()'s
 * usual partial spectrum/waterfall-only repaint - same reasoning as
 * screen_wake()'s comment for why IT does a full repaint too. Also
 * closes the settings menu itself (CAL is a HW-page tile, so
 * s_menu_open was necessarily 1 to have reached this) so the radio
 * screen isn't drawn underneath a menu that's about to look stale.
 */
static void touch_calib_done_callback(const touch_calibration_t *cal)
{
    (void)cal; /* touch_calib.c already applied it via touch_set_calibration() and printed it to the debug UART - nothing left for this callback to do with the value itself */
    if (s_menu_open) {
        menu_screen_close();
    }
    radio_screen_draw();
    debug_print("touch_calib: wizard finished, radio screen restored\n");

    /* Immediate save (not the debounced settings_mark_dirty() path) -
     * finishing the calibration wizard is already a deliberate,
     * infrequent action on its own; waiting out
     * SETTINGS_SAVE_DEBOUNCE_MS on top of that would just be a
     * pointless delay before something the user explicitly just did
     * gets persisted. */
    settings_save_now(s_tune_hz, demod_am_get_mode(), k_tune_steps[s_tune_step_idx], demod_am_get_audio_bw(), s_volume_db_x2, s_nonwfm_use_48k,
                       s_pga_gain_db_x2, spectrum_smooth_pct_for_save(), s_speaker_pa_enabled, s_rf_agc_rin_level, s_tema_idx);
}

/*
 * CAL (HW page) - one-shot action, same "leaves the current view"
 * shape as SLEEP/EXIT just above/below rather than a cycle or detail
 * tile: tapping it hands the WHOLE screen to touch_calib.c's wizard
 * (see its header comment) until the user either finishes all 3
 * points or cancels with a short encoder press (see main()'s loop,
 * the touch_calib_active() branch).
 */
static void menu_tile_cal_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        gfx_guard_top_set(0); /* el asistente pinta la pantalla entera, cabecera incluida; radio_screen_draw() la vuelve a armar al salir */
        touch_calib_start(touch_calib_done_callback);
    }
}

/* Longest transient label this tile ever shows ("-99.9PM") plus the
 * null terminator - menu_tile_cal_ppm_refresh() below always writes a
 * complete, freshly-terminated string into this buffer before
 * pointing the tile's label at it, so there's no stale-data risk
 * across refreshes despite it being static. */
static char s_cal_ppm_label[10] = "PPM";

/*
 * PPM (HW page) - one-shot action, added 26/08/2026 per the project
 * owner: turns the live ppm error sam_current_ppm_error() already
 * computes (see its comment, and sam_calib_display_draw() for the
 * on-screen readout this shares its math with) into an actual
 * correction of the MS5351's assumed crystal frequency, instead of
 * leaving that as a number you had to compute PPM from by hand and
 * then go recompile MS5351_XTAL_HZ_DEFAULT with (the old workflow -
 * see ms5351.h's history comment).
 *
 * Preconditions enforced here rather than just documented, since a
 * bogus correction silently applied would be worse than no
 * correction at all:
 *   - Must be in SAM mode (sam_current_ppm_error() is meaningless
 *     otherwise - the PLL isn't even running).
 *   - |ppm| must be under CAL_PPM_SANITY_LIMIT - a PLL that hasn't
 *     locked yet (just switched into SAM, or not actually tuned to a
 *     carrier) reads garbage/noise here, not a real crystal error;
 *     50ppm is already enormous for any crystal, genuine values from
 *     the 21/08/2026 measurements were ~3ppm - this is a "reject
 *     obvious garbage" gate, not a tight tolerance.
 * Both rejections flash the tile label with a short reason (see
 * menu_tile_cal_ppm_refresh()) instead of silently doing nothing, so
 * a tap that didn't work doesn't look identical to one that did.
 *
 * On success: computes the corrected crystal frequency from the
 * CURRENTLY assumed one (ms5351_get_xtal_hz(), not the compiled-in
 * default) so repeated calibration runs compose correctly instead of
 * each one overwriting the last from the same 26MHz nominal baseline;
 * applies it (ms5351_set_xtal_hz()); forces an immediate retune at
 * the current VFO frequency so the correction takes effect without
 * requiring the user to nudge the encoder first; and saves CONFIG.CSV
 * right away (settings_save_now(), not the debounced path - same
 * "deliberate, infrequent action" reasoning as touch_calib_done_callback()
 * just above).
 */
#define CAL_PPM_SANITY_LIMIT_PPM 50.0f

static void menu_tile_cal_ppm_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event != UI_EVENT_RELEASE) {
        return;
    }

    if (demod_am_get_mode() != DEMOD_MODE_SAM) {
        debug_print("cal_ppm: not in SAM mode - tune to a known-frequency station in SAM first\n");
        {
            const char *msg = "SAM?";
            uint8_t i;
            for (i = 0U; (i < 8U) && (msg[i] != '\0'); i++) { s_cal_ppm_label[i] = msg[i]; }
            s_cal_ppm_label[i] = '\0';
        }
        return;
    }

    {
        float ppm_err = sam_current_ppm_error();
        float ppm_mag = (ppm_err < 0.0f) ? -ppm_err : ppm_err;

        if (ppm_mag > CAL_PPM_SANITY_LIMIT_PPM) {
            debug_print("cal_ppm: reading exceeds sanity limit - PLL likely not locked, ignoring\n");
            {
                const char *msg = "LOCK?";
                uint8_t i;
                for (i = 0U; (i < 8U) && (msg[i] != '\0'); i++) { s_cal_ppm_label[i] = msg[i]; }
                s_cal_ppm_label[i] = '\0';
            }
            return;
        }

        {
            uint32_t old_xtal_hz = ms5351_get_xtal_hz();
            /* new = old * (1 - ppm_err/1e6) - see ms5351.h's direction
             * comment for why a POSITIVE ppm_err means the assumed
             * xtal is too HIGH and must be REDUCED. +0.5f rounds to
             * nearest Hz instead of always truncating down. */
            uint32_t new_xtal_hz = (uint32_t)((float)old_xtal_hz * (1.0f - ppm_err / 1.0e6f) + 0.5f);

            ms5351_set_xtal_hz(new_xtal_hz);
            apply_lo_tune(s_tune_hz); /* retune NOW at the corrected reference - see this function's header comment */

            debug_print_dec("cal_ppm: old MS5351 xtal Hz", old_xtal_hz);
            debug_print_dec("cal_ppm: new MS5351 xtal Hz", new_xtal_hz);

            settings_save_now(s_tune_hz, demod_am_get_mode(), k_tune_steps[s_tune_step_idx], demod_am_get_audio_bw(), s_volume_db_x2, s_nonwfm_use_48k,
                       s_pga_gain_db_x2, spectrum_smooth_pct_for_save(), s_speaker_pa_enabled, s_rf_agc_rin_level, s_tema_idx);

            /* Show the applied correction (not the post-correction
             * residual, which would read ~0 and tell the user
             * nothing useful) on the tile itself, same manual
             * formatting convention as sam_calib_display_draw(). */
            {
                uint8_t negative = (ppm_err < 0.0f) ? 1U : 0U;
                uint16_t whole = (uint16_t)ppm_mag;
                uint16_t tenth = (uint16_t)((ppm_mag - (float)whole) * 10.0f + 0.5f);
                char tmp[10];
                int8_t pos = 9;
                if (tenth >= 10U) { tenth = 0U; whole++; }
                tmp[pos] = '\0';
                tmp[--pos] = 'M';
                tmp[--pos] = 'P';
                tmp[--pos] = (char)('0' + tenth);
                tmp[--pos] = '.';
                do {
                    if (pos > 0) { tmp[--pos] = (char)('0' + (whole % 10U)); }
                    whole /= 10U;
                } while (whole > 0U && pos > 0);
                if (pos > 0) { tmp[--pos] = negative ? '-' : '+'; }
                {
                    uint8_t i = 0U;
                    int8_t src = pos;
                    while ((tmp[src] != '\0') && (i < 9U)) { s_cal_ppm_label[i++] = tmp[src++]; }
                    s_cal_ppm_label[i] = '\0';
                }
            }
        }
    }
}

static void menu_tile_smooth_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) { menu_detail_show(ENCODER_TARGET_SMOOTH); }
}

static void menu_tile_exit_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        menu_screen_close();
    }
}

/*
 * BANDS preset list - reachable from the bottom bar (s_btn_bands,
 * added 02/08/2026, replacing the old SPT smoothing-cycle shortcut -
 * see its comment in demo_button_callback()). It USED to also have a
 * tile inside the settings grid (menu_tile_bands_callback()), but
 * that tile was repurposed 02/08/2026 into the BW audio-filter toggle
 * (see s_menu_tile_bw's declaration comment) once the bottom-bar
 * shortcut made the grid entry point redundant - the grid was already
 * completely full (12/12 slots), so freeing this one was the only way
 * to fit BW in without a bigger layout change. All BAND_PRESET_COUNT
 * preset tiles fill the WHOLE 4x3 grid (12 slots) - no BACK tile: every
 * preset tap already calls menu_screen_close() itself (see
 * menu_band_preset_callback()'s comment), so a separate "never mind,
 * go back" tile had nothing left to do once picking ANY tile here
 * closes the screen anyway. If you open this by mistake and don't
 * want to change bands, the long-press-the-knob gesture (see
 * tune_encoder_poll()'s comment) still gets you out without picking
 * anything.
 */
/* ===========================================================================
 * AJUSTES, MODO Y PASOS: todo sobre la misma rejilla
 * ===========================================================================
 *
 * *** 22/09/2026, por el dueno del proyecto: "toda la parte de ajustes
 * entera", "la ventana de modo tambien tienes que hacerla", "y la de pasos
 * tambien" ***
 *
 * QUE HABIA
 * ---------
 * Cuatro pantallas distintas hechas con la fuente 5x7: la rejilla de ajustes
 * (con una columna entera gastada en ANTERIOR / nombre de pagina / SIGUIENTE,
 * es decir tres de las doce casillas para navegar), la lista de modos, la de
 * pasos y la de bandas. Cada casilla decia una sigla y nada mas: "SPT 2",
 * "TRC CLR", "RATE 96K". Para saber que valia un ajuste habia que entrar.
 *
 * QUE HAY AHORA
 * -------------
 * Una sola rejilla (ui_grid.c) para las cuatro, y cada casilla dice su NOMBRE
 * y su VALOR ACTUAL. La navegacion baja a un pie de tres botones, asi que las
 * doce casillas son doce ajustes y no nueve.
 *
 * COMO SE EVITA LA DUPLICACION
 * ----------------------------
 * Un ajuste se declara UNA vez en k_ajustes[] con su nombre, su pagina y un
 * identificador. Lo que vale y lo que hace al tocarlo salen de dos funciones
 * que reparten por ese identificador, y lo que HACEN es llamar a los
 * callbacks que ya existian. Ni un solo ajuste se reimplementa aqui: si
 * manana cambia el recorte de limites de la ganancia, cambia en un sitio.
 */

/* --- identificadores de ajuste --------------------------------------- */
enum {
    AJ_AGC = 0, AJ_SQL, AJ_VOL, AJ_BW, AJ_PGA, AJ_NR, AJ_RFAGC, AJ_ATT,
    AJ_BRILLO, AJ_ESCALA, AJ_AUTOESC, AJ_SUAVIZ, AJ_ESTILO, AJ_ZOOM,
    AJ_PALETA, AJ_TRAZA, AJ_CONTORNO,
    AJ_ALTAVOZ, AJ_TASA, AJ_IFBW, AJ_TACTIL, AJ_CAL_TACTIL, AJ_CAL_PPM, AJ_DORMIR,
    AJ_RTTY_SHIFT, AJ_RTTY_BAUD, AJ_RTTY_INV,
    AJ_CW_TONO, AJ_CW_PPM, AJ_CW_AUTO,
    AJ_TEMA, AJ_INFO
};

#define AJ_PAG_RADIO    0U
#define AJ_PAG_PANTALLA 1U
#define AJ_PAG_EQUIPO   2U
#define AJ_PAG_DIGITAL  3U
#define AJ_PAGINAS      4U

typedef struct {
    const char *nombre;
    uint8_t     pagina;
    uint8_t     id;
} ajuste_t;

/*
 * El orden dentro de cada pagina es el de uso, no el alfabetico ni el
 * historico: lo que se toca a menudo arriba a la izquierda, lo que se toca
 * una vez en la vida (calibrar) abajo.
 */
static const ajuste_t k_ajustes[] = {
    { "AGC",           AJ_PAG_RADIO,    AJ_AGC        },
    { "Ancho",         AJ_PAG_RADIO,    AJ_BW         },
    { "Volumen",       AJ_PAG_RADIO,    AJ_VOL        },
    { "Silenciador",   AJ_PAG_RADIO,    AJ_SQL        },
    { "Reducción",     AJ_PAG_RADIO,    AJ_NR         },
    { "Ganancia",      AJ_PAG_RADIO,    AJ_PGA        },
    { "AGC de RF",     AJ_PAG_RADIO,    AJ_RFAGC      },
    { "Atenuador",     AJ_PAG_RADIO,    AJ_ATT        },

    { "Escala",        AJ_PAG_PANTALLA, AJ_ESCALA     },
    { "Autoescala",    AJ_PAG_PANTALLA, AJ_AUTOESC    },
    { "Zoom",          AJ_PAG_PANTALLA, AJ_ZOOM       },
    { "Paleta",        AJ_PAG_PANTALLA, AJ_PALETA     },
    { "Estilo",        AJ_PAG_PANTALLA, AJ_ESTILO     },
    { "Traza",         AJ_PAG_PANTALLA, AJ_TRAZA      },
    { "Suavizado",     AJ_PAG_PANTALLA, AJ_SUAVIZ     },
    { "Contorno",      AJ_PAG_PANTALLA, AJ_CONTORNO   },
    /* Tema, 22/09/2026: la paleta de la interfaz, distinta de "Paleta",
     * que son los colores del espectro. Entra aqui porque Brillo se ha
     * ido a Equipo - la pagina estaba en 9 de 9 y no cabia nada. */
    { "Tema",          AJ_PAG_PANTALLA, AJ_TEMA       },

    { "Brillo",        AJ_PAG_EQUIPO,   AJ_BRILLO     },
    { "Altavoz",       AJ_PAG_EQUIPO,   AJ_ALTAVOZ    },
    { "Táctil",        AJ_PAG_EQUIPO,   AJ_TACTIL     },
    { "Muestreo",      AJ_PAG_EQUIPO,   AJ_TASA       },
    { "Filtro FM",     AJ_PAG_EQUIPO,   AJ_IFBW       },
    { "Dormir",        AJ_PAG_EQUIPO,   AJ_DORMIR     },
    { "Calibrar táctil", AJ_PAG_EQUIPO, AJ_CAL_TACTIL },
    { "Calibrar PPM",  AJ_PAG_EQUIPO,   AJ_CAL_PPM    },
    { "Información",   AJ_PAG_EQUIPO,   AJ_INFO       },

    { "Desplazamiento",AJ_PAG_DIGITAL,  AJ_RTTY_SHIFT },
    { "Baudios",       AJ_PAG_DIGITAL,  AJ_RTTY_BAUD  },
    { "Inversión",     AJ_PAG_DIGITAL,  AJ_RTTY_INV   },

    { "Tono CW",       AJ_PAG_DIGITAL,  AJ_CW_TONO    },
    { "Velocidad CW",  AJ_PAG_DIGITAL,  AJ_CW_PPM     },
    { "Autoenganche",  AJ_PAG_DIGITAL,  AJ_CW_AUTO    }
};
#define AJUSTE_COUNT (sizeof(k_ajustes) / sizeof(k_ajustes[0]))

static const char *const k_aj_paginas[AJ_PAGINAS] = {
    "Radio", "Pantalla", "Equipo", "Digital"
};

/* Nombres largos para lo que antes eran siglas de tres letras. */
static const char *const k_agc_nombres[4]   = { "Sin AGC", "Lenta", "Media", "Rápida" };
/* Los nombres largos de Ajustes y las siglas de la barra son la misma lista
 * en dos formatos, indexada por el mismo perfil de AGC. */
_Static_assert(sizeof(k_agc_nombres) / sizeof(k_agc_nombres[0])
               == sizeof(k_agc_profile_labels) / sizeof(k_agc_profile_labels[0]),
               "k_agc_nombres y k_agc_profile_labels tienen que medir lo mismo");
static const char *const k_att_nombres[3]   = { "0 dB", "-6 dB", "-12 dB" };
static const char *const k_tactil_nombres[3]= { "Sensible", "Normal", "Firme" };

/* Buffer compartido por las celdas: se rellena una por una al construir la
 * pagina, y ui_grid solo guarda el puntero, asi que cada celda necesita el
 * suyo. Doce celdas por 16 caracteres son 192 bytes. */
static char s_aj_val[UIG_CELLS][16];

static void aj_u2s(char *b, uint32_t v) { top_u2s(b, v); }

/* Texto del valor actual de un ajuste. Devuelve 0 si el ajuste no tiene
 * valor (es una accion). */
static const char *ajuste_valor(uint8_t id, char *buf)
{
    switch (id) {
    case AJ_AGC:    return k_agc_nombres[(uint8_t)demod_am_get_agc_profile()];
    case AJ_BW: {
        demod_mode_t m = demod_am_get_mode();
        if (cw_get_enabled()) {
            /* 22/09/2026: aqui ponia "0,5 kHz de CW" fijo, que era honrado
             * cuando el selector no hacia nada en CW y paso a ser mentira el
             * dia que empezo a hacerlo: el sonido cambiaba y la celda seguia
             * diciendo 0,5. Ahora se pregunta al demodulador, que es quien
             * sabe el ancho que tiene puesto. */
            bw_format(buf, (uint32_t)(demod_am_get_cw_bw_hz() + 0.5f), "kHz CW");
            return buf;
        }
        if (m == DEMOD_MODE_AM || m == DEMOD_MODE_USB || m == DEMOD_MODE_LSB) {
            bw_format(buf, k_audio_bw_hz[(uint8_t)demod_am_get_audio_bw()], "kHz");
            return buf;
        }
        return "fijo";
    }
    case AJ_VOL:    volume_format_ui(s_volume_db_x2, buf); return buf;
    case AJ_PGA:    volume_format_ui(s_pga_gain_db_x2, buf); return buf;
    case AJ_SQL: {
        int16_t db = (int16_t)demod_am_get_squelch_db();
        uint8_t i = 0;
        if (db < 0) { buf[i++] = '-'; db = (int16_t)(-db); }
        aj_u2s(&buf[i], (uint32_t)db);
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = 'd'; buf[i++] = 'B'; buf[i] = '\0';
        return buf;
    }
    case AJ_NR:     aj_u2s(buf, (uint32_t)s_nr_strength); return buf;
    case AJ_RFAGC:  return s_rf_agc_enabled ? "Activo" : "Apagado";
    case AJ_ATT:
        /* Lo que se ensena es LO QUE HAS PEDIDO, no lo que el automatico
         * este haciendo en este segundo: si no, la celda cambiaba sola y
         * parecia que el boton no obedecia. Cuando el AGC de RF ha subido
         * por encima de tu suelo se dice aparte, entre parentesis, que es
         * informacion util y no un valor distinto del tuyo. */
        if (s_rf_agc_rin_level > s_att_suelo) {
            /* El buffer mas pequeno de los dos que llaman aqui es
             * s_aj_val[][16], asi que el limite es 15 caracteres. El peor
             * caso, "-12 dB (-12 dB)", son exactamente 15 - pero el limite
             * se comprueba en cada paso en vez de fiarse de esa cuenta, que
             * es justo lo que deja de ser cierto el dia que un rotulo
             * cambie. */
            const char *mio  = k_att_nombres[s_att_suelo];
            const char *sube = k_att_nombres[s_rf_agc_rin_level];
            uint8_t k = 0U, j;
            for (j = 0U; mio[j]  != '\0' && k < 15U; j++) { buf[k++] = mio[j]; }
            if (k < 15U) { buf[k++] = ' '; }
            if (k < 15U) { buf[k++] = '('; }
            for (j = 0U; sube[j] != '\0' && k < 15U; j++) { buf[k++] = sube[j]; }
            if (k < 15U) { buf[k++] = ')'; }
            buf[k] = '\0';
            return buf;
        }
        return k_att_nombres[s_att_suelo];

    case AJ_BRILLO: {
        uint8_t i;
        aj_u2s(buf, backlight_get_percent());
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ' '; buf[i++] = '%'; buf[i] = '\0';
        return buf;
    }
    case AJ_ESCALA: {
        /* "-110 / -20": los dos limites a la vez, que es como se piensa la
         * escala; entrar solo hacia falta para moverlos. */
        int16_t lo = (int16_t)s_db_min, hi = (int16_t)s_db_max;
        uint8_t i = 0;
        if (lo < 0) { buf[i++] = '-'; lo = (int16_t)(-lo); }
        aj_u2s(&buf[i], (uint32_t)lo);
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = '/'; buf[i++] = ' ';
        if (hi < 0) { buf[i++] = '-'; hi = (int16_t)(-hi); }
        aj_u2s(&buf[i], (uint32_t)hi);
        return buf;
    }
    case AJ_AUTOESC: return s_spec_agc_enabled ? "Activa" : "Apagada";
    case AJ_SUAVIZ: {
        uint8_t i;
        aj_u2s(buf, (uint32_t)spectrum_smooth_pct_for_save());
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ' '; buf[i++] = '%'; buf[i] = '\0';
        return buf;
    }
    case AJ_CONTORNO: aj_u2s(buf, (uint32_t)s_spec_smooth_passes); return buf;
    case AJ_ESTILO:
        switch (spectrum_get_style()) {
        case SPECTRUM_STYLE_HEATMAP: return "Relleno";
        case SPECTRUM_STYLE_LINE:    return "Línea";
        default:                     return "Contorno";
        }
    case AJ_ZOOM:
        return (s_spec_zoom == SPEC_ZOOM_8X) ? "8x" :
               (s_spec_zoom == SPEC_ZOOM_4X) ? "4x" :
               (s_spec_zoom == SPEC_ZOOM_2X) ? "2x" : "1x";
    case AJ_PALETA:
        /* Sale de la tabla de spectrum.c, que es donde vive la lista. Esta
         * misma celda mostraba "Clásica" para Templada, Viva y WebSDR
         * porque aqui habia una copia a mano de la lista a la que le
         * faltaban tres ramas. */
        return spectrum_palette_nombre((uint8_t)spectrum_get_palette());
    case AJ_TRAZA:   return spectrum_get_heatmap_trace_white() ? "Blanca" : "Del color";

    case AJ_ALTAVOZ: return s_speaker_pa_enabled ? "Activo" : "Mudo";
    case AJ_TASA:    return s_nonwfm_use_48k ? "48 kHz" : "96 kHz";
    case AJ_IFBW:    return (demod_am_get_wfm_ifbw() == WFM_IFBW_NARROW) ? "80 kHz" : "96 kHz";
    case AJ_TACTIL:  return k_tactil_nombres[touch_get_firmeza() - 1U];

    case AJ_RTTY_SHIFT: {
        uint8_t i;
        aj_u2s(buf, (uint32_t)(rtty_get_shift_hz() + 0.5f));
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ' '; buf[i++] = 'H'; buf[i++] = 'z'; buf[i] = '\0';
        return buf;
    }
    case AJ_RTTY_BAUD: return k_rtty_baud_labels[s_rtty_baud_idx];

    case AJ_CW_TONO: {
        uint8_t i;
        aj_u2s(buf, (uint32_t)(cw_get_pitch_hz() + 0.5f));
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ' '; buf[i++] = 'H'; buf[i++] = 'z'; buf[i] = '\0';
        return buf;
    }
    /*
     * Se ensenan DOS numeros: el que se ha puesto y el que el
     * decodificador esta midiendo ahora mismo, "20 / 24". El primero es
     * por donde empieza a buscar y el segundo es lo que ha encontrado,
     * y es el segundo el que dice si esta enganchado: si baila, no lo
     * esta. Poner solo el ajustado seria ensenar el unico de los dos
     * que no informa de nada.
     */
    case AJ_INFO:   return 0;   /* es una accion: abre su pantalla */
    case AJ_CW_AUTO:
        return cw_get_autotune() ? "Automático" : "Fijo en el tono";
    case AJ_TEMA:   return k_temas[s_tema_idx].nombre;
    case AJ_CW_PPM: {
        uint8_t i;
        aj_u2s(buf, (uint32_t)k_cw_wpm[s_cw_wpm_idx]);
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ' '; buf[i++] = '/'; buf[i++] = ' ';
        aj_u2s(&buf[i], (uint32_t)(cw_get_wpm() + 0.5f));
        return buf;
    }
    case AJ_RTTY_INV:  return rtty_get_station_inverted() ? "Invertida" : "Normal";

    default: return 0;   /* acciones: calibrar, dormir */
    }
}

/* Lo que hace tocar un ajuste. Llama a los callbacks que ya existen. */
static void ajuste_accion(uint8_t id)
{
    switch (id) {
    case AJ_AGC:    menu_tile_agc_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_BW:     menu_tile_bw_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_RFAGC:  menu_tile_rfagc_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_ATT:    menu_tile_att_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_AUTOESC: menu_tile_specagc_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_ESTILO: menu_tile_spec_style_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_ZOOM:   menu_tile_zoom_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_PALETA: paleta_cambiar(); break;
    case AJ_TRAZA:  menu_tile_trace_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_CONTORNO: menu_tile_nb_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_ALTAVOZ: menu_tile_speaker_pa_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_TASA:   menu_tile_rate_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_IFBW:   menu_tile_ifbw_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_RTTY_BAUD: menu_tile_rtty_baud_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_RTTY_INV:  menu_tile_rtty_inv_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_DORMIR:    menu_tile_sleep_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_CAL_TACTIL: menu_tile_cal_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_CAL_PPM:    menu_tile_cal_ppm_callback(0, UI_EVENT_RELEASE, 0); break;

    case AJ_TACTIL: {
        /* Sensible -> Normal -> Firme -> Sensible. El unico ajuste que no
         * tenia celda antes: se anade con la funcion nueva de touch.c. */
        uint8_t n = (uint8_t)(touch_get_firmeza() + 1U);
        if (n > 3U) { n = 1U; }
        touch_set_firmeza(n);
        if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
        break;
    }

    /* Los que tienen un rango continuo abren la pantalla de detalle, donde
     * estan el mando y los botones "-" y "+". */
    case AJ_VOL: menu_tile_volume_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_SQL: menu_tile_squelch_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_PGA: menu_tile_pga_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_NR: menu_tile_nr_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_BRILLO: menu_tile_backlight_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_ESCALA: menu_tile_scale_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_SUAVIZ: menu_tile_smooth_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_RTTY_SHIFT: menu_tile_rtty_shift_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_CW_TONO:    menu_tile_cw_tone_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_CW_PPM:     menu_tile_cw_wpm_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_CW_AUTO:
        cw_set_autotune((uint8_t)(cw_get_autotune() ? 0U : 1U));
        settings_value_redraw();
        break;
    case AJ_TEMA:       tema_cambiar(); break;
    case AJ_INFO:       grid_show(GRID_INFO); break;
    default: break;
    }
}


/* ===========================================================================
 * PANTALLA DE AJUSTES (columna de categorias a la izquierda)
 * ===========================================================================
 * *** 22/09/2026, por el dueno del proyecto: le gustaba la maqueta inicial,
 * "las 4 categorias a la izquierda, y luego ponerle los botones a la
 * derecha" ***
 *
 * Sustituye a la rejilla paginada. Con paginas, ir de Radio a Equipo eran dos
 * pulsaciones de "Siguiente" y leer la cabecera para saber donde estabas; con
 * la columna, las cuatro categorias estan siempre a la vista y cambiar es un
 * toque. Y sale gratis en sitio: la columna ocupa lo que ocupaba el pie, y
 * las celdas pasan de 192x76 a 200x98 px.
 *
 * Las otras tres pantallas -bandas, modos y pasos- siguen en la rejilla
 * paginada, que es lo que les va: son listas largas de cosas del mismo tipo,
 * no cuatro grupos fijos.
 */
/* Las dos pantallas que usan la columna de categorias. Bandas se paso a este
 * reparto el 22/09/2026, por el dueno del proyecto: "en la pantalla de bandas
 * vamos a hacer lo mismo que en la de ajustes, menu lateral que marque la
 * categoria". Con 16 celdas por familia, ninguna necesita paginar. */
static ui_cfg_state_t s_cfg;
static uint8_t        s_cfg_cat = 0U;
static int8_t         s_cfg_cursor = 0;
static int8_t         s_cfg_press = -1;
static char           s_cfg_val[UIC_CELLS][20];

/* Cuantas entradas tiene la pantalla activa y a que categoria pertenece cada
 * una: lo unico que distingue ajustes de bandas dentro de este bloque. */
static uint16_t cfg_total(void)
{
    return (s_cfg_pant == CFG_BANDAS) ? (uint16_t)BAND_PRESET_COUNT
                                      : (uint16_t)AJUSTE_COUNT;
}
static uint8_t cfg_cat_de(uint16_t i)
{
    return (s_cfg_pant == CFG_BANDAS) ? k_band_presets[i].familia
                                      : k_ajustes[i].pagina;
}
static uint8_t cfg_cats(void)
{
    return (s_cfg_pant == CFG_BANDAS) ? BAND_FAM_COUNT : AJ_PAGINAS;
}

/* Indice global de la celda `cel` de la categoria activa, o -1. */
static int16_t cfg_indice(uint8_t cel)
{
    uint16_t i, total = cfg_total();
    uint8_t  n = 0;

    for (i = 0; i < total; i++) {
        if (cfg_cat_de(i) != s_cfg_cat) { continue; }
        if (n == cel) { return (int16_t)i; }
        n++;
    }
    return -1;
}

static uint8_t cfg_cuantos(uint8_t cat)
{
    uint16_t i, total = cfg_total();
    uint8_t  n = 0;

    for (i = 0; i < total; i++) {
        if (cfg_cat_de(i) == cat) { n++; }
    }
    return n;
}

static void cfg_fill(void)
{
    uint8_t i, n = 0, cats = cfg_cats();
    uint16_t k, total = cfg_total();

    for (i = 0U; i < UIC_CATS; i++) {
        s_cfg.cat[i] = (i < cats)
            ? ((s_cfg_pant == CFG_BANDAS) ? k_band_familias[i] : k_aj_paginas[i])
            : "";
    }
    s_cfg.cats    = cats;
    s_cfg.denso   = (uint8_t)(s_cfg_pant == CFG_BANDAS);
    s_cfg.cat_sel = s_cfg_cat;
    s_cfg.cursor  = s_cfg_cursor;
    s_cfg.pressed = s_cfg_press;
    s_cfg.marcada = 0xFFU;

    for (k = 0; k < total && n < UIC_CELLS; k++) {
        if (cfg_cat_de(k) != s_cfg_cat) { continue; }

        if (s_cfg_pant == CFG_BANDAS) {
            const band_preset_t *b = &k_band_presets[k];
            uint8_t m;

            s_cfg.cel[n].nombre = b->label;
            s_cfg.cel[n].valor  = b->rango;
            s_cfg.cel[n].extra  = "";
            for (m = 0U; m < (uint8_t)DEMOD_MODE_ENTRY_COUNT; m++) {
                if (k_demod_modes[m].mode == b->mode &&
                    k_demod_modes[m].rtty_variant == RTTY_VARIANT_NONE &&
                    !k_demod_modes[m].cw) {
                    s_cfg.cel[n].extra = k_demod_modes[m].label;
                    break;
                }
            }
            if (b->freq_hz == s_tune_hz) { s_cfg.marcada = n; }
        } else {
            const ajuste_t *a = &k_ajustes[k];
            const char *v = ajuste_valor(a->id, s_cfg_val[n]);

            s_cfg.cel[n].nombre = a->nombre;
            /* Los que son una accion y no un valor lo dicen, en vez de dejar
             * el renglon vacio como si les faltara algo. */
            s_cfg.cel[n].valor  = v ? v : "tocar";
            s_cfg.cel[n].extra  = 0;
        }
        n++;
    }
    for (i = n; i < UIC_CELLS; i++) {
        s_cfg.cel[i].nombre = "";
        s_cfg.cel[i].valor  = "";
        s_cfg.cel[i].extra  = 0;
    }
    s_cfg.n = n;
}

/*
 * Cambia la paleta de la interfaz y repinta.
 *
 * Todo el nucleo grafico lee los colores a traves de g_pal, asi que
 * cambiar el puntero y volver a pintar es literalmente todo lo que hay
 * que hacer: ni un solo sitio de uso se toca. Era la razon por la que la
 * paleta se hizo conmutable en su dia (ver el comentario de palette.h),
 * y hasta hoy no habia forma de aprovecharlo sin recompilar.
 *
 * Se repinta la pantalla de ajustes, que es desde donde se toca. Lo que
 * hay debajo -la pantalla principal- se repinta entero al cerrar el
 * menu, asi que no hace falta hacer nada aqui por ella.
 *
 * NO SE GUARDA: al reiniciar vuelve a la oscura. Queda pendiente, y es
 * una asimetria fea con el resto de los ajustes de esa pagina, que si
 * persisten.
 */
/*
 * Pone el tema i. Sin repintar: eso lo hace quien llama, que es el unico
 * que sabe lo que hay en pantalla. Al arrancar no hay nada pintado.
 */
static void tema_aplicar(uint8_t i)
{
    if (i >= (uint8_t)TEMA_COUNT) { i = 0U; }
    s_tema_idx = i;
    g_pal = k_temas[i].pal;
    spectrum_set_palette(k_temas[i].wf);
    debug_print("tema: ");
    debug_print(k_temas[i].nombre);
    debug_print("\n");
}

/*
 * Pasa al siguiente tema y repinta.
 *
 * Cicla en vez de abrir una pantalla de eleccion, y es a proposito: con
 * cuatro temas, tocar y ver es mas rapido que entrar, elegir y salir. Y
 * hay algo que una pantalla de eleccion no puede dar - al tocar, lo que
 * se repinta es la pantalla donde estas, asi que ves el tema puesto
 * sobre el sitio donde lo vas a usar y no sobre una lista de nombres.
 *
 * Todo el nucleo grafico lee los colores por g_pal, asi que cambiar el
 * puntero y repintar es literalmente todo. Era para lo que se hizo
 * conmutable la paleta en su dia (ver palette.h), y hasta hoy no habia
 * forma de aprovecharlo sin recompilar.
 */
/*
 * Siguiente paleta del espectro, en el sitio - 22/09/2026, por el dueno del
 * proyecto: "un boton que sea la paleta del espectro y que vaya cambiando
 * conforme le vas dando, igual que el de tema".
 *
 * El "igual que el de tema" es literal: mismo gesto, misma celda, mismo
 * volver a la lista con el nombre nuevo puesto. La diferencia es que un tema
 * repinta toda la pantalla porque cambia todos los colores de la interfaz, y
 * una paleta solo cambia el espectro y la cascada, asi que basta con
 * refrescar el valor de su propia celda.
 *
 * El "siguiente" sale de la tabla de spectrum.c. Antes era un switch de
 * quince ramas escrito a mano aqui, y era una de las siete copias de la
 * misma lista: anadir una paleta obligaba a acordarse de siete sitios, y a
 * tres de ellas ya se les habia olvidado uno.
 */
static void paleta_cambiar(void)
{
    uint8_t n = spectrum_palette_count();
    uint8_t i = (uint8_t)(((uint8_t)spectrum_get_palette() + 1U) % n);

    spectrum_set_palette((spectrum_palette_t)i);
    if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
    settings_value_redraw();
}

static void tema_cambiar(void)
{
    tema_aplicar((uint8_t)((s_tema_idx + 1U) % TEMA_COUNT));
    if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
    cfg_fill();
    ui_cfg_draw(&s_cfg);
}

static void cfg_show(cfg_pant_t p)
{
    if (p != s_cfg_pant) { s_cfg_pant = p; s_cfg_cat = 0U; s_cfg_cursor = 0; }
    s_cfg_press = -1;
    if (s_cfg_cursor >= (int8_t)cfg_cuantos(s_cfg_cat)) { s_cfg_cursor = 0; }
    cfg_fill();
    ui_cfg_draw(&s_cfg);

    s_menu_detail_active = 0U;
    s_menu_bands_active  = 0U;
    s_menu_step_active   = 0U;
    s_menu_mode_active   = 0U;
    s_menu_freq_active   = 0U;
    s_menu_time_active   = 0U;
    s_kbd_modo           = KBD_NADA;
    s_grid_pant = GRID_NADA;   /* esta pantalla no es una rejilla paginada */
    s_menu_cfg_active = 1U;
    s_menu_bands_active = (uint8_t)(p == CFG_BANDAS);
    s_menu_open = 1U;
    /* La barra de abajo se repinta al ABRIR cualquier pantalla, no solo al
     * cerrarla: el boton iluminado es el de la pantalla activa, y abrir una
     * desde un sitio que no sea la propia barra (el teclado de frecuencia,
     * por ejemplo) tambien lo cambia. Ponerlo aqui lo cubre por construccion.
     */
    act_draw();


    /* El cursor del mando arranca donde estas, si esa celda esta a la vista. */
    if (s_cfg.marcada != 0xFFU) {
        s_cfg_cursor = (int8_t)s_cfg.marcada;
        cfg_fill();
        ui_cfg_draw(&s_cfg);
    }
}

static void cfg_apply(uint8_t cel)
{
    int16_t k = cfg_indice(cel);

    if (k < 0) { return; }
    if (s_cfg_pant == CFG_BANDAS) {
        menu_band_preset_callback(0, UI_EVENT_RELEASE, (void *)(uintptr_t)k);
        return;
    }
    ajuste_accion(k_ajustes[k].id);
    /* Los que cambian en el sitio se quedan aqui y refrescan; los que abren
     * el detalle o una pantalla completa ya han cambiado de sitio. */
    if (s_menu_cfg_active && !s_menu_detail_active && !s_screen_asleep) {
        cfg_fill();
        ui_cfg_draw(&s_cfg);
    }
}

/* El mando recorre las celdas y, al llegar al final, salta de categoria: asi
 * se pasa por los 27 ajustes de un tiron sin tener que tocar la columna. */
static void cfg_cursor_move(int32_t d)
{
    while (d > 0) {
        if (s_cfg_cursor + 1 < (int8_t)cfg_cuantos(s_cfg_cat)) {
            s_cfg_cursor++;
        } else if (s_cfg_cat + 1U < cfg_cats()) {
            s_cfg_cat++; s_cfg_cursor = 0;
        }
        d--;
    }
    while (d < 0) {
        if (s_cfg_cursor > 0) {
            s_cfg_cursor--;
        } else if (s_cfg_cat > 0U) {
            s_cfg_cat--;
            s_cfg_cursor = (int8_t)(cfg_cuantos(s_cfg_cat) - 1U);
        }
        d++;
    }
    cfg_fill();
    ui_cfg_draw(&s_cfg);
}

static void cfg_touch(uint16_t x, uint16_t y, uint8_t pressed)
{
    if (pressed) {
        if (s_cfg_press < 0) {
            s_cfg_press = ui_cfg_hit(x, y);
            if (s_cfg_press >= 0) { cfg_fill(); ui_cfg_draw_one(&s_cfg, s_cfg_press); }
        }
        return;
    }
    if (s_cfg_press < 0) { return; }
    {
        int8_t k = s_cfg_press;

        s_cfg_press = -1;
        cfg_fill();
        ui_cfg_draw_one(&s_cfg, k);

        if (k < UIC_CELLS) {
            cfg_apply((uint8_t)k);
        } else {
            uint8_t cat = (uint8_t)(k - UIC_HIT_CAT0);
            if (cat != s_cfg_cat) {
                s_cfg_cat = cat;
                s_cfg_cursor = 0;
                cfg_fill();
                ui_cfg_draw(&s_cfg);
            }
        }
    }
}

/* ===========================================================================
 * MOTOR DE PANTALLAS DE REJILLA
 * ===========================================================================
 * Cuatro pantallas -ajustes, bandas, modos y pasos- comparten estado, dibujo,
 * paginacion, reparto de toques y navegacion con el mando. Lo unico propio de
 * cada una son tres funciones cortas: cuantas paginas tiene, que pone en cada
 * celda y que pasa al tocarla.
 */
static ui_grid_state_t  s_grid;
static uint8_t          s_grid_page = 0U;
static int8_t           s_grid_cursor = 0;
static int8_t           s_grid_press = -1;

/* Descripciones de modo y de paso: la sigla sola no dice nada a quien no se
 * la sepa ya, y aqui hay sitio de sobra para la palabra entera. */
/*
 * Los pasos siguen teniendo sus textos en arrays paralelos a
 * k_tune_steps[] -las etiquetas por un lado y las descripciones por
 * otro- porque ahi no hay una estructura donde meterlos y el array de
 * valores es un uint32_t pelado que usa media docena de sitios.
 *
 * Pero el fallo que se comio la pantalla de modos era exactamente este,
 * asi que al menos que no pueda pasar en silencio: si alguien anade un
 * paso y se olvida de un texto, esto no compila. Es una linea, y es la
 * diferencia entre un error de compilacion y una pantalla que desaparece
 * sin decir por que.
 */
static const char *const k_paso_desc[] = {
    "Ajuste fino", "SSB", "Onda corta", "CB y onda media",
    "Marina y 2 m", "Aeronáutica", "FM comercial", "Saltos grandes"
};
_Static_assert(sizeof(k_paso_desc) / sizeof(k_paso_desc[0]) == TUNE_STEP_COUNT,
               "k_paso_desc[] tiene que tener una entrada por cada paso de k_tune_steps[]");
_Static_assert(sizeof(k_tune_step_labels_ui) / sizeof(k_tune_step_labels_ui[0]) == TUNE_STEP_COUNT,
               "k_tune_step_labels_ui[] tiene que tener una entrada por cada paso de k_tune_steps[]");

/* ===========================================================================
 * PANTALLA DE INFORMACION
 * ===========================================================================
 * *** 22/09/2026, a peticion del dueno del proyecto ***
 *
 * Solo se lee, no se toca nada. Reutiliza la rejilla comun, igual que
 * modos y pasos: una pantalla de solo lectura no merece codigo propio.
 *
 * TODOS los numeros salen de donde estan de verdad, ninguno esta escrito
 * a mano:
 *
 *   - La fecha es __DATE__/__TIME__, o sea el momento de compilar. No se
 *     puede desincronizar del binario porque no hay nada que actualizar.
 *   - El uso de memoria sale de los simbolos del enlazador, los mismos
 *     con los que el arranque copia .data y .tcmram. La cuenta de flash
 *     esta comprobada contra el tamano real del .bin: coincide byte a
 *     byte.
 *   - La temperatura es el sensor del propio micro. Ver
 *     battery_get_chip_temp_c() para por que pone "del chip" y por que
 *     el numero absoluto no es de fiar.
 *
 * El tope de flash que se ensena son 256 kB y no los 384 que da el mapa
 * de memoria, porque el limite de verdad no es el enlazador: es el
 * camino de actualizacion, que rellena la imagen a 0x40000 y le pega la
 * firma detras. Ensenar 384 seria ensenar un margen que no existe.
 */
extern uint32_t _sdata, _edata, _sbss, _ebss, _stcmram, _etcmram, _sitcmram;

#define INFO_FLASH_ORIGEN 0x08020000UL
#define INFO_FLASH_TOPE   (256UL * 1024UL)   /* ver el comentario de arriba */
#define INFO_SRAM_TOPE    (192UL * 1024UL)
#define INFO_TCM_TOPE     (64UL  * 1024UL)

/* "123,4 / 256 kB" a partir de bytes usados y tope en bytes. */
static void info_kb(char *b, uint32_t usados, uint32_t tope)
{
    uint32_t e = usados / 1024U;
    uint32_t d = ((usados % 1024U) * 10U) / 1024U;
    uint8_t  i = 0U;

    aj_u2s(b, e);
    while (b[i] != '\0') { i++; }
    b[i++] = ','; b[i++] = (char)('0' + d);
    b[i++] = ' '; b[i++] = '/'; b[i++] = ' ';
    aj_u2s(&b[i], tope / 1024U);
    while (b[i] != '\0') { i++; }
    b[i++] = ' '; b[i++] = 'k'; b[i++] = 'B'; b[i] = '\0';
}

/*
 * __DATE__ llega como "Sep 22 2026" y __TIME__ como "18:43:11", en
 * ingles y en el orden americano. Se le da la vuelta a mano a
 * "22/09/2026 18:43" porque es lo que espera quien lo va a leer, y
 * traducir tres letras es mas barato que acostumbrarse.
 */
static void info_fecha(char *b)
{
    static const char k_meses[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *f = __DATE__;
    const char *h = __TIME__;
    uint8_t m, i = 0U;

    for (m = 0U; m < 12U; m++) {
        if (f[0] == k_meses[m * 3U] && f[1] == k_meses[m * 3U + 1U] &&
            f[2] == k_meses[m * 3U + 2U]) {
            break;
        }
    }
    m++;   /* 1..12; si no se reconocio queda 13, que canta a la vista */

    b[i++] = (f[4] == ' ') ? '0' : f[4];
    b[i++] = f[5];
    b[i++] = '/';
    b[i++] = (char)('0' + (m / 10U));
    b[i++] = (char)('0' + (m % 10U));
    b[i++] = '/';
    b[i++] = f[7]; b[i++] = f[8]; b[i++] = f[9]; b[i++] = f[10];
    b[i++] = ' ';
    b[i++] = h[0]; b[i++] = h[1]; b[i++] = ':'; b[i++] = h[3]; b[i++] = h[4];
    b[i] = '\0';
}

enum { INFO_VERSION = 0, INFO_FECHA, INFO_MODELO, INFO_MICRO, INFO_FLASH,
       INFO_SRAM, INFO_TCM, INFO_TEMP, INFO_BATT, INFO_COUNT };

static const char *const k_info_nombres[INFO_COUNT] = {
    "Versión", "Compilado", "Equipo", "Micro", "Flash", "SRAM", "TCM",
    "Temp. del chip", "Batería"
};

static char s_info_val[UIG_CELLS][24];


static const char *info_valor(uint8_t id, char *buf)
{
    switch (id) {
    case INFO_VERSION: return CONFIG_FW_VERSION;
    case INFO_FECHA:   info_fecha(buf); return buf;
    case INFO_MODELO:  return "DEEPSDR 101";
    case INFO_MICRO:   return "GD32F450VET6";
    case INFO_FLASH:
        info_kb(buf, ((uint32_t)&_sitcmram - INFO_FLASH_ORIGEN) +
                     ((uint32_t)&_etcmram - (uint32_t)&_stcmram), INFO_FLASH_TOPE);
        return buf;
    case INFO_SRAM:
        info_kb(buf, ((uint32_t)&_edata - (uint32_t)&_sdata) +
                     ((uint32_t)&_ebss - (uint32_t)&_sbss), INFO_SRAM_TOPE);
        return buf;
    case INFO_TCM:
        info_kb(buf, (uint32_t)&_etcmram - (uint32_t)&_stcmram, INFO_TCM_TOPE);
        return buf;
    case INFO_TEMP: {
        int16_t t = battery_get_chip_temp_c();
        uint8_t i = 0U;
        if (t < 0) { buf[i++] = '-'; t = (int16_t)(-t); }
        aj_u2s(&buf[i], (uint32_t)t);
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = '\xC2'; buf[i++] = '\xB0'; buf[i++] = 'C';
        buf[i] = '\0';
        return buf;
    }
    case INFO_BATT: {
        uint8_t i = 0U;
        aj_u2s(buf, battery_get_percent());
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = '%'; buf[i] = '\0';
        return buf;
    }
    default: return "";
    }
}

/* Numero de entradas de la pantalla activa. */
static uint16_t grid_total(void)
{
    switch (s_grid_pant) {
    case GRID_AJUSTES: return (uint16_t)AJUSTE_COUNT;
    case GRID_BANDAS:  return (uint16_t)BAND_PRESET_COUNT;
    case GRID_MODO:    return (uint16_t)DEMOD_MODE_ENTRY_COUNT;
    case GRID_PASOS:   return (uint16_t)TUNE_STEP_COUNT;
    case GRID_INFO:    return (uint16_t)INFO_COUNT;
    default:           return 0U;
    }
}

/* Familia/grupo de la entrada i, o 0 si la pantalla no agrupa. */
static uint8_t grid_grupo(uint16_t i)
{
    switch (s_grid_pant) {
    case GRID_AJUSTES: return k_ajustes[i].pagina;
    case GRID_BANDAS:  return k_band_presets[i].familia;
    default:           return 0U;
    }
}

static const char *grid_grupo_nombre(uint8_t g)
{
    switch (s_grid_pant) {
    case GRID_AJUSTES: return k_aj_paginas[g];
    case GRID_BANDAS:  return k_band_familias[g];
    case GRID_MODO:    return "Modo";
    case GRID_PASOS:   return "Paso de sintonía";
    case GRID_INFO:    return "Información";
    default:           return "";
    }
}

/*
 * Primera entrada, numero de entradas y grupo de la pagina pedida.
 *
 * Las paginas no mezclan grupos: se recorre la tabla acumulando tramos del
 * mismo grupo y cada tramo se parte en paginas de UIG_CELLS. Calcularlo en
 * vez de escribirlo a mano es lo que permite anadir un ajuste o una banda sin
 * tocar nada mas.
 */
static uint8_t grid_page_info(uint8_t pagina, uint16_t *primero, uint8_t *n, uint8_t *grupo)
{
    uint16_t i = 0, total = grid_total();
    uint8_t  p = 0;

    while (i < total) {
        uint8_t  g = grid_grupo(i);
        uint16_t run = 0, off = 0;

        while (i + run < total && grid_grupo((uint16_t)(i + run)) == g) { run++; }
        while (off < run) {
            uint16_t quedan = (uint16_t)(run - off);
            uint8_t  cuantas = (uint8_t)((quedan > UIG_CELLS) ? UIG_CELLS : quedan);
            if (p == pagina) {
                if (primero) { *primero = (uint16_t)(i + off); }
                if (n)       { *n = cuantas; }
                if (grupo)   { *grupo = g; }
                return 1U;
            }
            p++;
            off = (uint16_t)(off + cuantas);
        }
        i = (uint16_t)(i + run);
    }
    return 0U;
}

static uint8_t grid_page_count(void)
{
    uint8_t p = 0;
    while (grid_page_info(p, 0, 0, 0)) { p++; }
    return p;
}

/* Rellena s_grid con la pagina actual. */
static void grid_fill(void)
{
    uint8_t  grupo = 0, n = 0, i;
    uint16_t primero = 0;

    if (!grid_page_info(s_grid_page, &primero, &n, &grupo)) {
        s_grid_page = 0U;
        if (!grid_page_info(0U, &primero, &n, &grupo)) { n = 0U; }
    }

    s_grid.titulo  = grid_grupo_nombre(grupo);
    s_grid.pagina  = s_grid_page;
    s_grid.paginas = grid_page_count();
    s_grid.n       = n;
    s_grid.pressed = s_grid_press;
    s_grid.cursor  = s_grid_cursor;
    s_grid.marcada = 0xFFU;

    s_grid.nav[0] = "Anterior";  s_grid.nav_on[0] = (uint8_t)(s_grid_page > 0U);
    s_grid.nav[1] = "Siguiente"; s_grid.nav_on[1] = (uint8_t)(s_grid_page + 1U < s_grid.paginas);

    for (i = 0U; i < UIG_CELLS; i++) {
        s_grid.cel[i].l1 = "";
        s_grid.cel[i].l2 = "";
        s_grid.cel[i].l3 = 0;

        if (i >= n) { continue; }

        switch (s_grid_pant) {
        case GRID_AJUSTES: {
            const ajuste_t *a = &k_ajustes[primero + i];
            const char *v = ajuste_valor(a->id, s_aj_val[i]);
            s_grid.cel[i].l1 = a->nombre;
            /* Los ajustes que son una accion y no un valor lo dicen, en vez
             * de dejar el renglon vacio como si les faltara algo. */
            s_grid.cel[i].l2 = v ? v : "tocar para hacerlo";
            break;
        }
        case GRID_BANDAS: {
            const band_preset_t *b = &k_band_presets[primero + i];
            uint8_t m;
            s_grid.cel[i].l1 = b->label;
            s_grid.cel[i].l2 = b->rango;
            s_grid.cel[i].l3 = "";
            for (m = 0U; m < (uint8_t)DEMOD_MODE_ENTRY_COUNT; m++) {
                if (k_demod_modes[m].mode == b->mode &&
                    k_demod_modes[m].rtty_variant == RTTY_VARIANT_NONE &&
                    !k_demod_modes[m].cw) {
                    s_grid.cel[i].l3 = k_demod_modes[m].label;
                    break;
                }
            }
            if (b->freq_hz == s_tune_hz) { s_grid.marcada = i; }
            break;
        }
        case GRID_MODO: {
            uint16_t k = (uint16_t)(primero + i);
            s_grid.cel[i].l1 = k_demod_modes[k].label;
            s_grid.cel[i].l2 = k_demod_modes[k].desc;
            /* rtty.h no expone en que variante esta, asi que se deduce:
             * RTTY encendido significa que la entrada activa es una de las
             * dos de teletipo, y cual de las dos lo decide la banda lateral.
             * Es exactamente la informacion que hay, sin inventar un getter
             * nuevo en el modulo de RTTY para una etiqueta de pantalla. */
            /* Misma condicion que la etiqueta del boton de modo, y por
             * eso la misma funcion: ver demod_mode_entry_active(). */
            if (demod_mode_entry_active((uint8_t)k)) { s_grid.marcada = i; }
            break;
        }
        case GRID_INFO: {
            uint8_t id = (uint8_t)(primero + i);
            s_grid.cel[i].l1 = k_info_nombres[id];
            s_grid.cel[i].l2 = info_valor(id, s_info_val[i]);
            break;
        }
        case GRID_PASOS: {
            uint16_t k = (uint16_t)(primero + i);
            s_grid.cel[i].l1 = k_tune_step_labels_ui[k];
            s_grid.cel[i].l2 = k_paso_desc[k];
            if (k == s_tune_step_idx) { s_grid.marcada = i; }
            break;
        }
        default: break;
        }
    }
}

/* Que pasa al elegir la celda `cel` de la pagina actual. */
static void grid_apply(uint8_t cel)
{
    uint16_t primero = 0;
    uint8_t  n = 0;

    if (!grid_page_info(s_grid_page, &primero, &n, 0) || cel >= n) { return; }

    switch (s_grid_pant) {
    case GRID_AJUSTES:
        ajuste_accion(k_ajustes[primero + cel].id);
        /* Los ajustes que cambian en el sitio se quedan en la rejilla y
         * refrescan su celda; los que abren el detalle ya han cambiado de
         * pantalla y no hay que repintar nada debajo. */
        if (s_grid_pant == GRID_AJUSTES && !s_menu_detail_active && !s_screen_asleep) {
            grid_fill();
            ui_grid_draw(&s_grid);
        }
        break;
    case GRID_BANDAS:
        menu_band_preset_callback(0, UI_EVENT_RELEASE, (void *)(uintptr_t)(primero + cel));
        break;
    case GRID_MODO:
        menu_mode_preset_callback(0, UI_EVENT_RELEASE, (void *)(uintptr_t)(primero + cel));
        break;
    case GRID_PASOS:
        menu_step_preset_callback(0, UI_EVENT_RELEASE, (void *)(uintptr_t)(primero + cel));
        break;
    default: break;
    }
}

static void grid_show(grid_pant_t p)
{
    s_grid_pant = p;
    s_grid_page = 0U;
    s_grid_cursor = 0;
    s_grid_press = -1;

    /* El cursor del mando arranca en la celda marcada si la hay, para que
     * girar una vez lleve a la de al lado y no al principio de todo. Hay que
     * rellenar antes para saber cual es. */
    grid_fill();
    if (s_grid.marcada != 0xFFU) { s_grid_cursor = (int8_t)s_grid.marcada; }
    grid_fill();
    ui_grid_draw(&s_grid);

    /* s_menu_screen se deja vacia: estas pantallas no usan widgets de ui.c.
     * Inicializarla evita que un toque herede los botones de la anterior. */

    s_menu_cfg_active    = 0U;
    s_menu_detail_active = 0U;
    s_menu_bands_active  = (uint8_t)(p == GRID_BANDAS);
    s_menu_step_active   = (uint8_t)(p == GRID_PASOS);
    s_menu_mode_active   = (uint8_t)(p == GRID_MODO);
    s_menu_freq_active   = 0U;
    s_menu_time_active   = 0U;
    s_kbd_modo           = KBD_NADA;
    s_menu_open = 1U;
    /* La barra de abajo se repinta al ABRIR cualquier pantalla, no solo al
     * cerrarla: el boton iluminado es el de la pantalla activa, y abrir una
     * desde un sitio que no sea la propia barra (el teclado de frecuencia,
     * por ejemplo) tambien lo cambia. Ponerlo aqui lo cubre por construccion.
     */
    act_draw();
}

/* Mueve el cursor del mando, saltando de pagina por los extremos. */
static void grid_cursor_move(int32_t d)
{
    uint8_t n = 0;

    while (d > 0) {
        (void)grid_page_info(s_grid_page, 0, &n, 0);
        if (s_grid_cursor + 1 < (int8_t)n) {
            s_grid_cursor++;
        } else if (s_grid_page + 1U < grid_page_count()) {
            s_grid_page++; s_grid_cursor = 0;
        }
        d--;
    }
    while (d < 0) {
        if (s_grid_cursor > 0) {
            s_grid_cursor--;
        } else if (s_grid_page > 0U) {
            s_grid_page--;
            (void)grid_page_info(s_grid_page, 0, &n, 0);
            s_grid_cursor = (int8_t)(n - 1U);
        }
        d++;
    }
    grid_fill();
    ui_grid_draw(&s_grid);
}

/* Reparto de un toque dentro de una pantalla de rejilla. */
static void grid_touch(uint16_t x, uint16_t y, uint8_t pressed)
{
    if (pressed) {
        if (s_grid_press < 0) {
            s_grid_press = ui_grid_hit(x, y);
            /* Con una sola pagina no se dibuja el pie (ver ui_grid.c), asi
             * que tampoco se toca: modos y pasos caben enteros de una vez. */
            if (s_grid_press >= UIG_CELLS && grid_page_count() <= 1U) {
                s_grid_press = -1;
            }
            if (s_grid_press >= 0) {
                grid_fill();
                ui_grid_draw_one(&s_grid, s_grid_press);
            }
        }
        return;
    }
    if (s_grid_press < 0) { return; }
    {
        int8_t k = s_grid_press;

        s_grid_press = -1;
        grid_fill();
        ui_grid_draw_one(&s_grid, k);

        if (k < UIG_CELLS) {
            grid_apply((uint8_t)k);
        } else if (k == UIG_HIT_PREV) {
            if (s_grid_page > 0U) {
                s_grid_page--; s_grid_cursor = 0;
                grid_fill(); ui_grid_draw(&s_grid);
            }
        } else if (k == UIG_HIT_NEXT) {
            if (s_grid_page + 1U < grid_page_count()) {
                s_grid_page++; s_grid_cursor = 0;
                grid_fill(); ui_grid_draw(&s_grid);
            }
        }
    }
}

static uint8_t grid_activa(void)
{
    return (uint8_t)(s_grid_pant != GRID_NADA && s_menu_open && !s_menu_detail_active
                     && !s_menu_freq_active && !s_menu_time_active);
}

/*
 * LOS BOTONES DE LA BARRA SE COMPORTAN COMO PESTAÑAS
 * --------------------------------------------------
 * *** 22/09/2026, por el dueno del proyecto: "el boton cerrar no hace falta,
 * tiene que volverse a la principal si pulso ajustes", "y lo mismo en bandas
 * y modo", "y en pasos" ***
 *
 * Pulsar el boton de la pantalla que ya esta abierta vuelve a la principal;
 * pulsar el de otra cambia a esa. Es como se comporta cualquier barra de
 * pestañas, y aqui ademas ya se estaba anunciando asi: el boton se resalta
 * mientras su pantalla esta abierta.
 *
 * Con esto, el boton "Cerrar" que habia dentro de cada pantalla sobra - hacia
 * lo mismo que otro boton que esta siempre a la vista, y obligaba a buscarlo
 * dentro en vez de salir por donde entraste.
 *
 * GRID_NADA significa aqui "la pantalla de ajustes", que no es una rejilla
 * paginada sino la de la columna de categorias (ui_cfg.c).
 */
static void accion_pantalla(grid_pant_t p)
{
    uint8_t abierta;

    if (p == GRID_NADA) {
        abierta = (uint8_t)(s_menu_cfg_active && s_cfg_pant == CFG_AJUSTES);
    } else if (p == GRID_BANDAS) {
        abierta = (uint8_t)(s_menu_cfg_active && s_cfg_pant == CFG_BANDAS);
    } else {
        abierta = (uint8_t)(s_menu_open && s_grid_pant == p);
    }

    /* Estando en el detalle de un ajuste, el boton "Ajustes" devuelve a la
     * rejilla en vez de salir del todo: es un nivel mas adentro, no otra
     * pantalla. Los demas botones si sacan. */
    if (p == GRID_NADA && s_menu_detail_active) {
        menu_grid_show();
        return;
    }
    if (abierta) {
        menu_screen_close();
        return;
    }
    if (p == GRID_NADA)        { menu_grid_show(); }
    else if (p == GRID_BANDAS) { menu_bands_show(); }
    else                       { grid_show(p); }
}

/* Se conservan por los sitios que abren estas pantallas desde fuera de la
 * barra de acciones (aplicar una banda desde el teclado de frecuencia, por
 * ejemplo). Los botones de la barra usan accion_pantalla(). */
static void menu_bands_show(void) { cfg_show(CFG_BANDAS); }


/*
 * STEP / MODE picker lists - added 01/08/2026 per the project owner:
 * the bottom-bar STEP and MODE buttons used to just cycle to the next
 * entry on tap (see the old s_btn_step/s_btn_mode branches in
 * demo_button_callback()'s history); now they open a real "pick from
 * all N at once" screen instead, same tile-grid mechanism as BANDS
 * above, just entered DIRECTLY from the bottom bar rather than from
 * within the settings menu grid. Unlike BANDS (no BACK tile at all -
 * see its comment), these DO get a BACK/CANCEL tile, reusing
 * menu_tile_exit_callback(): with only 8 (STEP) or 5 (MODE) entries
 * there's always a free slot for one, and a "never mind" escape hatch
 * costs nothing when there's room for it.
 * Applying a selection closes the same way BANDS does - see
 * menu_band_preset_callback()'s comment for that reasoning, which
 * applies here unchanged.
 */
static void menu_step_preset_callback(void *widget, ui_event_t event, void *user_data)
{
    uintptr_t idx = (uintptr_t)user_data;

    (void)widget;
    if (event == UI_EVENT_RELEASE && idx < TUNE_STEP_COUNT) {
        set_tune_step_idx((uint8_t)idx);
        debug_print_dec("tune: step now Hz", k_tune_steps[s_tune_step_idx]);
        /* Same "menu_screen_close() doesn't know this readout actually
         * CHANGED" gap as menu_band_preset_callback()'s comment - needs
         * an explicit repaint, not just the panel-border restore. */
        step_display_draw();
        menu_screen_close();
    }
}

static void menu_mode_preset_callback(void *widget, ui_event_t event, void *user_data)
{
    uintptr_t idx = (uintptr_t)user_data;

    (void)widget;
    if (event == UI_EVENT_RELEASE && idx < DEMOD_MODE_ENTRY_COUNT) {
        demod_mode_t mode = k_demod_modes[idx].mode;

        apply_demod_mode(mode);
        /* Re-tune at the (unchanged) selected frequency so the LO
         * offset behavior matches the NEW mode immediately - same
         * WFM/NFM reasoning as the old cycling MODE button, see
         * apply_lo_tune()'s comment. */
        apply_lo_tune(s_tune_hz);

        /* RTTY on/off + polarity - see k_demod_modes[]'s comment for
         * the RTTY_VARIANT_NORMAL/INVERTED story. Picking a PLAIN
         * mode (RTTY_VARIANT_NONE) always turns RTTY off, even if it
         * was on before - switching to e.g. WFM or plain USB should
         * unambiguously mean "I'm done with RTTY", not leave the
         * decoder/scope silently running against audio that's no
         * longer even SSB. */
        /* CW: mismo trato que el RTTY, y con la misma regla de que
         * elegir un modo llano lo apaga sin preguntar. Va aparte del
         * switch de abajo para que quede claro que son dos interruptores
         * independientes y que ninguno se queda encendido por descuido
         * al pasar al otro. */
        cw_set_enabled(k_demod_modes[idx].cw);

        switch (k_demod_modes[idx].rtty_variant) {
        case RTTY_VARIANT_NORMAL:
            rtty_set_mark_space_hz(CONFIG_RTTY_MARK_HZ, CONFIG_RTTY_SPACE_HZ);
            /* Reapply any active station NORMAL/REVERSE convention on
             * top of this fresh sideband-mirror base pair - see
             * rtty_reapply_station_inversion()'s comment in rtty.h.
             * Without this, switching modes would silently drop the
             * DIG page's INV tile back to NORMAL even though the tile
             * itself still reads REVERSE. */
            rtty_reapply_station_inversion();
            rtty_set_enabled(1U);
            break;
        case RTTY_VARIANT_INVERTED:
            rtty_set_mark_space_hz(CONFIG_RTTY_SPACE_HZ, CONFIG_RTTY_MARK_HZ);
            rtty_reapply_station_inversion();
            rtty_set_enabled(1U);
            break;
        case RTTY_VARIANT_NONE:
        default:
            rtty_set_enabled(0U);
            break;
        }

        debug_print("mode: demodulator now ");
        debug_print(k_demod_modes[idx].label);
        debug_print("\n");
        mode_display_draw();
        sam_calib_display_draw(); /* prompt blank/draw right on mode change, not just at the next periodic tick */
        badges_draw(); /* BW badge is mode-dependent, see its comment */
        menu_screen_close();
    }
}





/*
 * --- Frequency-entry keypad ----------------------------------------------
 *
 * Added 07/08/2026, per the project owner: tap the frequency readout
 * in the top bar (FREQ_TAP_X1/Y1's zone, checked in demo_touch_poll())
 * to open this instead of only being able to spin the encoder or pick
 * a BANDS preset. Same "reuse s_menu_screen/MENU_AREA" treatment as
 * the STEP/MODE picker lists above, opened straight from the top bar
 * rather than the settings grid.
 *
 * No decimal point key - deliberately. The three accept buttons
 * (Hz/kHz/MHz) already cover fractional MHz entry without needing a
 * float parser on a bare-metal target: typing "146520" then tapping
 * kHz gives 146,520,000 Hz (146.520MHz) exactly as if you'd typed
 * "146.520" and tapped MHz. Hz stays available for the rare case of
 * wanting the exact integer Hz value directly (e.g. from a frequency
 * counter reading).
 */
/* 22/09/2026: lo que habia aqui pintaba la lectura a mano con gfx.c. Ahora
 * la compone kbd_lectura_freq() y la dibuja gfx2/ui_kbd.c; esto se queda
 * como el nombre por el que la llaman las funciones de entrada de abajo, que
 * no tienen por que saber quien dibuja. */
static void freq_keypad_readout_draw(void)
{
    kbd_lectura_draw();
}

static void menu_freq_keypad_digit_callback(void *widget, ui_event_t event, void *user_data)
{
    uintptr_t digit = (uintptr_t)user_data;

    (void)widget;
    if (event == UI_EVENT_RELEASE && s_freq_entry_digits < FREQ_ENTRY_MAX_DIGITS) {
        s_freq_entry_value = s_freq_entry_value * 10U + (uint32_t)digit;
        s_freq_entry_digits++;
        freq_keypad_readout_draw();
    }
}

/* Decimal point - see s_freq_entry_point_pos's declaration comment.
 * Ignored if a point is already placed (only one per entry makes
 * sense) - matches the digit callback's own "ignore once the cap is
 * hit" guard shape. */
static void menu_freq_keypad_point_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE && s_freq_entry_point_pos == FREQ_ENTRY_NO_POINT) {
        s_freq_entry_point_pos = s_freq_entry_digits;
        freq_keypad_readout_draw();
    }
}

static void menu_freq_keypad_del_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        if (s_freq_entry_digits > 0U) {
            s_freq_entry_value /= 10U;
            s_freq_entry_digits--;
            /* Deleted back past the point itself (or exactly onto
             * it) - the point goes too, same as backspacing over a
             * "." in any normal numeric entry field. */
            if (s_freq_entry_point_pos != FREQ_ENTRY_NO_POINT
                && s_freq_entry_point_pos > s_freq_entry_digits) {
                s_freq_entry_point_pos = FREQ_ENTRY_NO_POINT;
            }
            freq_keypad_readout_draw();
        } else if (s_freq_entry_point_pos != FREQ_ENTRY_NO_POINT) {
            /* No digits left, but a lone point is still showing
             * (e.g. user pressed "." then DEL with nothing typed
             * either side) - one more DEL clears it. */
            s_freq_entry_point_pos = FREQ_ENTRY_NO_POINT;
            freq_keypad_readout_draw();
        }
    }
}

static void menu_freq_keypad_clr_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        s_freq_entry_value = 0U;
        s_freq_entry_digits = 0U;
        s_freq_entry_point_pos = FREQ_ENTRY_NO_POINT;
        freq_keypad_readout_draw();
    }
}

/*
 * Shared by the kHz/MHz buttons - user_data is the multiplier
 * (1000/1000000), passed the same (void*)(uintptr_t) way the
 * digit callback's digit is. Ignored entirely if nothing was typed
 * (s_freq_entry_digits==0 and no lone point either) - no accidental
 * retune to TUNE_MIN_HZ from an empty entry. The multiply happens in
 * a uint64_t intermediate on purpose: s_freq_entry_value is capped at
 * 9 digits (max 999,999,999) precisely so it can never overflow
 * uint32_t on its own, but 999,999,999 * 1,000,000 overflows uint32_t
 * many times over - the same int64_t-then-clamp pattern
 * tune_encoder_poll() and spec_drag_tune_apply() already use for
 * exactly this reason.
 *
 * *** 01/09/2026, decimal-point support added, plain HZ button
 * removed *** - per the project owner: entering a frequency out to
 * bare-Hz precision digit-by-digit (the old HZ button, multiplier=1)
 * had no practical use once kHz/MHz entry already existed, and typing
 * a frequency exactly the way it's normally written (e.g. "14.200" +
 * MHZ, or "0.621" + MHZ) is far more natural than only being able to
 * type a bare integer count of the chosen unit (the old "146520" +
 * KHZ for 146.520MHz still works exactly as before - the point is
 * purely additive). If a point was entered, divide back out by
 * 10^(fractional digit count) AFTER the multiply, same uint64_t
 * intermediate as the multiply itself so a full 9-digit entry times
 * 10^6 still can't overflow before the divide brings it back down.
 */
static void menu_freq_keypad_accept_callback(void *widget, ui_event_t event, void *user_data)
{
    uint32_t multiplier = (uint32_t)(uintptr_t)user_data;

    (void)widget;
    if (event == UI_EVENT_RELEASE
        && (s_freq_entry_digits > 0U || s_freq_entry_point_pos != FREQ_ENTRY_NO_POINT)) {
        uint64_t hz64 = (uint64_t)s_freq_entry_value * (uint64_t)multiplier;

        if (s_freq_entry_point_pos != FREQ_ENTRY_NO_POINT) {
            uint8_t frac_digits = (uint8_t)(s_freq_entry_digits - s_freq_entry_point_pos);
            uint32_t divisor = 1U;
            uint8_t i;

            for (i = 0U; i < frac_digits; i++) {
                divisor *= 10U;
            }
            hz64 /= (uint64_t)divisor;
        }

        if (hz64 < (uint64_t)TUNE_MIN_HZ) {
            hz64 = (uint64_t)TUNE_MIN_HZ;
        } else if (hz64 > (uint64_t)TUNE_MAX_HZ) {
            hz64 = (uint64_t)TUNE_MAX_HZ;
        }

        s_tune_hz = (uint32_t)hz64;
        apply_lo_tune(s_tune_hz);
        debug_print_dec("tune: keypad entry now Hz", s_tune_hz);
        freq_display_draw(); /* top bar - see its call in tune_encoder_poll() */
        menu_screen_close();
    }
}

static void menu_freq_keypad_show(void)
{
    kbd_show(KBD_FREQ);
    debug_print("menu: teclado de frecuencia abierto\n");
}

/*
 * --- Clock-setting keypad (08/09/2026, per the project owner) -----------
 *
 * "podemos poner un tile para ajustar el reloj... hora y minutos" -
 * tap the clock readout (TIME_TAP_X1/Y2's zone, checked in
 * demo_touch_poll()) to type in HH:MM as 4 digits, same "reuse
 * s_menu_screen/MENU_AREA, opened straight from the top bar" shape as
 * menu_freq_keypad_show() just above (and its own comment covers why
 * this isn't a real ui_button_t either - same reasoning, this is a
 * plain gfx_text() readout too). Deliberately much simpler than the
 * frequency keypad: exactly 4 digits (HHMM, e.g. "0830" for 8:30,
 * "1430" for 14:30), no decimal point, one single SET accept button
 * instead of separate kHz/MHz ones - a clock only ever has one unit.
 *
 * s_time_entry_value/digits: identical accumulation shape to
 * s_freq_entry_value/digits (value = value*10+digit) - see that pair's
 * own comment for why (no float parsing needed, matches this whole
 * file's existing integer-accumulation keypad pattern). Capped at
 * TIME_ENTRY_MAX_DIGITS(4) - HHMM never needs a 5th digit. Reset to
 * 0/0 every time this keypad opens, never pre-filled with the current
 * time, same "typing a fresh number is the point" reasoning as the
 * frequency keypad.
 */
#define TIME_ENTRY_MAX_DIGITS 4U
static uint16_t s_time_entry_value = 0U;
static uint8_t  s_time_entry_digits = 0U;

/*
 * Readout draw - same fixed-field-repaint shape as
 * freq_keypad_readout_draw() (always blank+redraw the whole strip so
 * a shorter new value can't leave a ghost digit behind).
 *
 * *** 08/09/2026 - rewritten to fix a real confusion (reported by the
 * project owner: typing "0424" only seemed to "capture" 00:42) ***
 * Each already-typed digit is now shown in ITS OWN fixed slot
 * (H-tens, H-ones, M-tens, M-ones), with a '-' placeholder for slots
 * not yet typed - replacing the old "re-derive HH=value/100,
 * MM=value%100 after every keypress" display. That old approach was
 * numerically correct as SOON as all 4 digits had landed, but every
 * INTERMEDIATE frame re-split the partial value differently: typing
 * "0" then "4" then "2" showed "00:00" -> "00:04" -> "00:42", not
 * "0-:--" -> "04:--" -> "04:2-" the way any real digital-clock entry
 * field fills in - easy to glance at "00:42" mid-typing, mistake it
 * for the finished result, and hit SET one digit early (exactly what
 * happened: "0","4","2" alone already reads as 00:42 under the old
 * scheme). menu_time_keypad_accept_callback() also now requires all
 * 4 digits before it will act at all, so an incomplete entry like
 * that can no longer be accepted even by mistake.
 *
 * Extracts each typed digit straight from s_time_entry_value/digits
 * (position i's digit, counting from the FIRST digit typed, is
 * (value / 10^(digits-1-i)) % 10) rather than keeping a separate
 * digit buffer - the accumulated value already records exactly what
 * was typed, in order; this just reads it back out per-slot instead
 * of re-splitting the whole number as if it were already complete.
 */
/* Igual que freq_keypad_readout_draw(): el formato de casillas con rayas se
 * ha mudado a kbd_lectura_hora(), con su porque. */
static void time_keypad_readout_draw(void)
{
    kbd_lectura_draw();
}

static void menu_time_keypad_digit_callback(void *widget, ui_event_t event, void *user_data)
{
    uintptr_t digit = (uintptr_t)user_data;

    (void)widget;
    if (event == UI_EVENT_RELEASE && s_time_entry_digits < TIME_ENTRY_MAX_DIGITS) {
        s_time_entry_value = (uint16_t)(s_time_entry_value * 10U + (uint16_t)digit);
        s_time_entry_digits++;
        time_keypad_readout_draw();
    }
}

static void menu_time_keypad_del_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE && s_time_entry_digits > 0U) {
        s_time_entry_value /= 10U;
        s_time_entry_digits--;
        time_keypad_readout_draw();
    }
}

static void menu_time_keypad_clr_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE) {
        s_time_entry_value = 0U;
        s_time_entry_digits = 0U;
        time_keypad_readout_draw();
    }
}

/*
 * SET - clamps HH to 0-23 and MM to 0-59 (same "clamp rather than
 * reject" policy as every other manual entry field in this file, e.g.
 * the frequency keypad's TUNE_MIN_HZ/MAX_HZ clamp) then solves
 * s_time_offset_min so time_display_draw() reads exactly this HH:MM
 * at THIS instant - see s_time_offset_min's own declaration comment
 * for what it means and its one limitation.
 *
 * *** 08/09/2026 - now requires EXACTLY 4 digits, not just "any
 * digits" *** per the project owner's report that "0424" seemed to
 * "capture" 00:42 - the readout fix above (time_keypad_readout_draw())
 * addresses the visual confusion that caused it, but this is the
 * actual backstop: a 1-3 digit entry is almost certainly an
 * accidental early SET press (mid-typing), not a deliberately short
 * time, so it's now ignored outright rather than silently accepted
 * via the old "any digit is enough" rule - same spirit as the
 * frequency keypad's "empty entry does nothing" guard, just a
 * stricter threshold appropriate to a fixed-width HHMM field (a
 * frequency has no fixed digit count to compare against; a clock
 * does).
 *
 * int32_t intermediate for the delta because uptime_min's own modulo
 * result (0..1439) can legitimately be LARGER than desired_min (e.g.
 * uptime shows 23:50 and the user sets 00:10) - the raw subtraction
 * goes negative there, and one +1440 fixup brings it back into range
 * before the final %1440 (which cannot itself go negative once that
 * fixup ran, since desired_min and uptime_min%1440 are each already
 * within 0..1439).
 */
static void menu_time_keypad_accept_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE && s_time_entry_digits == TIME_ENTRY_MAX_DIGITS) {
        extern volatile uint32_t g_msticks;
        uint16_t hh = s_time_entry_value / 100U;
        uint16_t mm = s_time_entry_value % 100U;
        int32_t desired_min;
        int32_t uptime_min_now;
        int32_t delta;

        if (hh > 23U) { hh = 23U; }
        if (mm > 59U) { mm = 59U; }
        desired_min = (int32_t)hh * 60 + (int32_t)mm;
        uptime_min_now = (int32_t)((g_msticks / 60000UL) % 1440UL);
        delta = desired_min - uptime_min_now;
        if (delta < 0) { delta += 1440; }
        s_time_offset_min = (uint32_t)delta;

        debug_print_dec("clock: set, offset minutes now", s_time_offset_min);
        time_display_draw(); /* top bar - instant feedback, don't wait for the next periodic tick */
        menu_screen_close();
    }
}

/* ===========================================================================
 * TECLADO NUMERICO (etapa 19)
 * ===========================================================================
 * Las dos pantallas de teclado -frecuencia y hora- eran lo ultimo que
 * quedaba dibujado con gfx.c, o sea con el aspecto de antes del rediseno.
 * Ahora las pinta gfx2/ui_kbd.c, que no sabe de frecuencias ni de horas:
 * recibe rotulos y una lectura ya formateada.
 *
 * LO QUE NO CAMBIA es como se teclea. Las funciones de entrada de mas abajo
 * -digito, coma, borrar, vaciar y las de aceptar- se quedan tal cual, con sus
 * comentarios y sus casos raros ya resueltos (el punto que se comia su propio
 * terminador, el "0424" que parecia capturar las 00:42, el intermedio en
 * uint64_t para que 9 cifras por un millon no desborden). Reescribirlas
 * habria sido tirar todo eso para volver a encontrarlo.
 * =========================================================================== */

static ui_kbd_state_t s_kbd;
static kbd_modo_t     s_kbd_modo = KBD_NADA;  /* declaracion tentativa arriba */
static int8_t         s_kbd_press = -1;
static char           s_kbd_lectura[16];

/* Los rotulos. En espanol y en palabras, no "DEL/CLR/BACK/SET": el sitio da
 * de sobra y "Vaciar" no hay que aprenderselo. */
static const char *const k_kbd_freq_rot[UIK_KEYS] = {
    "1", "2", "3", "Borrar",
    "4", "5", "6", "Vaciar",
    "7", "8", "9", "Volver",
    ",", "0", "kHz", "MHz",
};
static const uint8_t k_kbd_freq_tipo[UIK_KEYS] = {
    UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_BORRA,
    UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_BORRA,
    UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_VUELVE,
    UIK_DIGITO, UIK_DIGITO, UIK_ACEPTA, UIK_ACEPTA,
};
static const char *const k_kbd_hora_rot[UIK_KEYS] = {
    "1", "2", "3", "Borrar",
    "4", "5", "6", "Vaciar",
    "7", "8", "9", "Volver",
    0,   "0", "Poner en hora", 0,
};
static const uint8_t k_kbd_hora_tipo[UIK_KEYS] = {
    UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_BORRA,
    UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_BORRA,
    UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_VUELVE,
    UIK_NADA,   UIK_DIGITO, UIK_ACEPTA, UIK_NADA,
};

/* La cifra que hay en cada tecla, para no volver a escribir el mapa en el
 * reparto de toques. 0xFF = esta tecla no es una cifra. */
static const uint8_t k_kbd_digito[UIK_KEYS] = {
    1U, 2U, 3U, 0xFFU,
    4U, 5U, 6U, 0xFFU,
    7U, 8U, 9U, 0xFFU,
    0xFFU, 0U, 0xFFU, 0xFFU,
};

static uint8_t kbd_activa(void)
{
    return (uint8_t)(s_kbd_modo != KBD_NADA && s_menu_open);
}

/* --- la lectura, cada modo a su manera ------------------------------------ */

/* Frecuencia: las cifras tal y como se han tecleado, ceros de delante
 * incluidos, con la coma donde se puso. Es lo mismo que hacia el dibujo
 * anterior; lo que cambia es que ahora se deja escrito en una cadena en vez
 * de pintarse a mano. */
static void kbd_lectura_freq(void)
{
    char tmp[FREQ_ENTRY_MAX_DIGITS + 1U];
    uint32_t v = s_freq_entry_value;
    uint8_t n = s_freq_entry_digits;
    uint8_t i = n, k = 0U, j;

    if (s_freq_entry_digits == 0U && s_freq_entry_point_pos == FREQ_ENTRY_NO_POINT) {
        s_kbd_lectura[0] = '\0';
        return;
    }

    tmp[n] = '\0';
    while (i > 0U) { tmp[--i] = (char)('0' + (v % 10U)); v /= 10U; }

    for (j = 0U; j < n; j++) {
        if (s_freq_entry_point_pos != FREQ_ENTRY_NO_POINT && j == s_freq_entry_point_pos) {
            s_kbd_lectura[k++] = ',';
        }
        s_kbd_lectura[k++] = tmp[j];
    }
    /* Una coma sin cifras detras todavia: se ensena igual, que si no el
     * teclazo no parece haber entrado. */
    if (s_freq_entry_point_pos != FREQ_ENTRY_NO_POINT && s_freq_entry_point_pos >= n) {
        s_kbd_lectura[k++] = ',';
    }
    s_kbd_lectura[k] = '\0';
}

/* Hora: cada cifra en SU casilla, con raya en las que faltan. Ver el
 * comentario de la version anterior: mostrar el valor recalculado en cada
 * tecla hacia que "0","4","2" se leyera como 00:42 a medio escribir. */
static void kbd_lectura_hora(void)
{
    static const uint16_t k_pot10[TIME_ENTRY_MAX_DIGITS] = {1U, 10U, 100U, 1000U};
    uint8_t i;

    if (s_time_entry_digits == 0U) { s_kbd_lectura[0] = '\0'; return; }

    for (i = 0U; i < TIME_ENTRY_MAX_DIGITS; i++) {
        uint8_t slot = (i < 2U) ? i : (uint8_t)(i + 1U);  /* el hueco 2 es el ':' */
        if (i < s_time_entry_digits) {
            uint16_t div = k_pot10[s_time_entry_digits - 1U - i];
            s_kbd_lectura[slot] = (char)('0' + (char)((s_time_entry_value / div) % 10U));
        } else {
            s_kbd_lectura[slot] = '-';
        }
    }
    s_kbd_lectura[2] = ':';
    s_kbd_lectura[5] = '\0';
}

static void kbd_lectura_draw(void)
{
    if (s_kbd_modo == KBD_FREQ) { kbd_lectura_freq(); }
    else                        { kbd_lectura_hora(); }
    s_kbd.lectura = s_kbd_lectura;
    if (!s_screen_asleep) { ui_kbd_draw_lectura(&s_kbd); }
}

static void kbd_show(kbd_modo_t modo)
{
    uint8_t i;

    s_kbd_modo  = modo;
    s_kbd_press = -1;
    s_kbd.pressed = -1;
    s_kbd.cursor  = -1;
    s_kbd.unidad  = 0;

    if (modo == KBD_FREQ) {
        s_freq_entry_value = 0U;
        s_freq_entry_digits = 0U;
        s_freq_entry_point_pos = FREQ_ENTRY_NO_POINT;
        s_kbd.titulo = "Frecuencia";
        s_kbd.pista  = "teclea y elige la unidad";
        for (i = 0U; i < UIK_KEYS; i++) {
            s_kbd.tecla[i] = k_kbd_freq_rot[i];
            s_kbd.tipo[i]  = k_kbd_freq_tipo[i];
        }
    } else {
        s_time_entry_value = 0U;
        s_time_entry_digits = 0U;
        s_kbd.titulo = "Hora";
        s_kbd.pista  = "cuatro cifras seguidas, HHMM";
        for (i = 0U; i < UIK_KEYS; i++) {
            s_kbd.tecla[i] = k_kbd_hora_rot[i];
            s_kbd.tipo[i]  = k_kbd_hora_tipo[i];
        }
    }

    if (s_kbd_modo == KBD_FREQ) { kbd_lectura_freq(); } else { kbd_lectura_hora(); }
    s_kbd.lectura = s_kbd_lectura;

    /* Esta pantalla no usa widgets de ui.c. Inicializar s_menu_screen evita
     * que un toque herede los botones de la pantalla anterior - lo mismo que
     * hace grid_show(). */
    ui_kbd_draw(&s_kbd);

    s_grid_pant          = GRID_NADA;
    s_menu_cfg_active    = 0U;
    s_menu_detail_active = 0U;
    s_menu_bands_active  = 0U;
    s_menu_step_active   = 0U;
    s_menu_mode_active   = 0U;
    s_menu_freq_active   = (uint8_t)(modo == KBD_FREQ);
    s_menu_time_active   = (uint8_t)(modo == KBD_HORA);
    s_menu_open = 1U;
}

/* Que hace cada tecla. Las cifras y las acciones se despachan por el mapa de
 * arriba; las de aceptar dependen del modo. */
static void kbd_apply(uint8_t i)
{
    uint8_t d = k_kbd_digito[i];

    if (d != 0xFFU) {
        if (s_kbd_modo == KBD_FREQ) {
            menu_freq_keypad_digit_callback(0, UI_EVENT_RELEASE, (void *)(uintptr_t)d);
        } else {
            menu_time_keypad_digit_callback(0, UI_EVENT_RELEASE, (void *)(uintptr_t)d);
        }
        return;
    }

    switch (i) {
    case 3U:  /* Borrar */
        if (s_kbd_modo == KBD_FREQ) { menu_freq_keypad_del_callback(0, UI_EVENT_RELEASE, 0); }
        else                        { menu_time_keypad_del_callback(0, UI_EVENT_RELEASE, 0); }
        break;
    case 7U:  /* Vaciar */
        if (s_kbd_modo == KBD_FREQ) { menu_freq_keypad_clr_callback(0, UI_EVENT_RELEASE, 0); }
        else                        { menu_time_keypad_clr_callback(0, UI_EVENT_RELEASE, 0); }
        break;
    case 11U: /* Volver */
        menu_tile_exit_callback(0, UI_EVENT_RELEASE, 0);
        break;
    case 12U: /* coma, solo en frecuencia */
        if (s_kbd_modo == KBD_FREQ) { menu_freq_keypad_point_callback(0, UI_EVENT_RELEASE, 0); }
        break;
    case 14U: /* kHz, o "Poner en hora" */
        if (s_kbd_modo == KBD_FREQ) {
            menu_freq_keypad_accept_callback(0, UI_EVENT_RELEASE, (void *)(uintptr_t)1000UL);
        } else {
            menu_time_keypad_accept_callback(0, UI_EVENT_RELEASE, 0);
        }
        break;
    case 15U: /* MHz */
        if (s_kbd_modo == KBD_FREQ) {
            menu_freq_keypad_accept_callback(0, UI_EVENT_RELEASE, (void *)(uintptr_t)1000000UL);
        }
        break;
    default:
        break;
    }
}

/* Reparto de un toque dentro del teclado - misma forma que grid_touch(): se
 * decide en la pulsacion y se respeta hasta la suelta, porque en este panel
 * las coordenadas de la suelta no son de fiar. */
static void kbd_touch(uint16_t x, uint16_t y, uint8_t pressed)
{
    if (pressed) {
        if (s_kbd_press < 0) {
            int8_t k = ui_kbd_hit(x, y);
            if (k >= 0 && s_kbd.tipo[k] != (uint8_t)UIK_NADA) {
                s_kbd_press = k;
                s_kbd.pressed = k;
                ui_kbd_draw_one(&s_kbd, k);
            }
        }
        return;
    }
    if (s_kbd_press < 0) { return; }
    {
        int8_t k = s_kbd_press;
        s_kbd_press = -1;
        s_kbd.pressed = -1;
        ui_kbd_draw_one(&s_kbd, k);
        kbd_apply((uint8_t)k);
    }
}

static void menu_time_keypad_show(void)
{
    kbd_show(KBD_HORA);
    debug_print("menu: teclado de hora abierto\n");
}

/*
 * Repaints ONLY the value area of the currently-open detail view -
 * called both by menu_detail_show() (initial paint) and by
 * tune_encoder_poll() through settings_value_redraw() every time the
 * knob actually changes something. Fixed clear rect up front so
 * switching targets (via BACK -> another tile) or SCALE's LO/HI
 * toggle never leaves a ghost of the previous content, same fixed-
 * width-repaint reasoning used everywhere else in this file.
 *
 * SQUELCH/BACKLIGHT/VOLUME/SMOOTH: one big centered value. SCALE is
 * the odd one out - two values (LO and HI) side by side, the active
 * one (s_scale_adjust_max) highlighted cyan, matching the same visual
 * language aux_row_display_draw() already uses for it on the main
 * screen.
 */
/*
 * DETAIL VIEW geometry - confined to MENU_AREA (800x318 @
 * (0,SPEC_Y)), same reasoning as the GRID's MENU_TILE_* macros above.
 *
 * *** 01/09/2026, RECOMPUTED for the status-strip redesign - real bug
 * fix, per the project owner (same failure class as MENU_TILE_W/H's
 * own fix above) *** - every one of these used to be a plain absolute
 * Y literal, computed back when MENU_AREA_Y was 64. Once MENU_AREA_Y
 * grew to 104 (status strip taking the first 40px), TITLE_Y(72) and
 * HINT_Y(104) both landed AT OR ABOVE the new area's own top edge -
 * i.e. drawn INTO the status strip itself, overlapping the S-meter/
 * badges, exactly the "doesn't stay confined to spectrum+waterfall"
 * symptom reported on real hardware. Fixed by treating each value as
 * an OFFSET from whichever edge of MENU_AREA it was always meant to
 * hang off of: TITLE_Y/HINT_Y/VALUE_CLEAR_Y (and the two SCALE-
 * specific Ys) are all offsets from the TOP (MENU_AREA_Y) and shift
 * down by the same 40px MENU_AREA_Y itself grew by; BACK_Y is an
 * offset from the BOTTOM (which never moved - MENU_AREA still ends at
 * 422 either way, see MENU_AREA_H's own comment) and needs no change
 * at all. VALUE_CLEAR_H (a height, not a position) shrinks by the
 * same 40px, since it's what actually absorbs the area's own overall
 * height loss - it's sandwiched between the now-lower-starting hint
 * text and the unmoved BACK button, so the gap between them is
 * genuinely smaller than before, not just relocated. VALUE_Y (which
 * centers a value line WITHIN VALUE_CLEAR_Y/H) is recomputed from
 * those two, not offset directly, so it stays correctly centered
 * whatever they come out to. BACK_X is (MENU_AREA_W - BACK_W)/2 -
 * recentered for the new, wider MENU_AREA_W(800) - it was 228 for the
 * old 676px width, not a coincidence: (676-220)/2=228 exactly, so
 * this project already centered it deliberately, just needs
 * recomputing at the new width.
 *
 * Vertical zones, non-overlapping: title (112-140) / hint (144-158) /
 * value area, redrawn on every encoder tick (166-350) / BACK button
 * (358-414) - all comfortably inside MENU_AREA_Y..MENU_AREA_Y+
 * MENU_AREA_H (104-422).
 */
#define MENU_DETAIL_TITLE_Y  112
#define MENU_DETAIL_HINT_Y   144
#define MENU_DETAIL_VALUE_CLEAR_Y 166
/* *** 22/09/2026: la banda que se borra ya no ocupa todo el ancho ni todo el
 * alto *** - los botones "-" y "+" viven a los lados y el "LO / HI" justo
 * debajo, y el borrado de ancho completo los habria borrado en cada refresco
 * del valor (es decir, en cada clic). Se recorta al hueco central que ocupa
 * de verdad el numero. Mismo fallo que el recuadro azul del AGC, en otro
 * sitio: pintar mas de lo que hace falta. */
#define MENU_DETAIL_VALUE_CLEAR_H 136
#define MENU_DETAIL_VALUE_CLEAR_X (uint16_t)(MENU_DETAIL_PM_X0 + MENU_DETAIL_PM_W + 8)
#define MENU_DETAIL_VALUE_CLEAR_W (uint16_t)(MENU_DETAIL_PM_X1 - 8 - MENU_DETAIL_VALUE_CLEAR_X)
#define MENU_DETAIL_VALUE_Y  237 /* centers a scale-6 (42px tall) line in the clear band above: 166+(184-42)/2 */
#define MENU_DETAIL_SCALE_LABEL_Y 180
#define MENU_DETAIL_SCALE_VALUE_Y 230
#define MENU_DETAIL_BACK_X 290 /* (MENU_AREA_W(800) - MENU_DETAIL_BACK_W(220)) / 2, centered */
#define MENU_DETAIL_BACK_Y 358 /* unchanged - offset from the BOTTOM of MENU_AREA, which never moved */
#define MENU_DETAIL_BACK_W 220
#define MENU_DETAIL_BACK_H 56

/* Solo el numero: es lo unico que cambia al girar el mando o pulsar "-"/"+",
 * y repintar la pantalla entera 30 veces mientras se mantiene el dedo se
 * notaria. En escala cambia tambien el titulo al alternar LO/HI, asi que ahi
 * se repinta todo. */
static void menu_detail_value_redraw(void)
{
    static uint8_t s_alt_ant = 0xFFU;

    det_sync();
    if (s_menu_detail_target == ENCODER_TARGET_SCALE &&
        s_alt_ant != s_scale_adjust_max) {
        s_alt_ant = s_scale_adjust_max;
        ui_det_draw(&s_det);
        return;
    }
    ui_det_draw_valor(&s_det);
}

/* ===========================================================================
 * PANTALLA DE UN AJUSTE (ETAPA 6, rehecha con gfx2)
 * ===========================================================================
 * Antes esta pantalla estaba dibujada con la fuente 5x7 a escala 6 y decia
 * "TURN KNOB TO ADJUST". Ahora es la misma tipografia que el resto, el valor
 * va en la fuente de cifras grande y los dos objetivos tactiles son los mas
 * grandes de la interfaz. Lo que hacen no cambia: meten un detente en la cola
 * del mando (ver encoder_inject_detents()), asi que corre el mismo codigo de
 * siempre con los mismos topes.
 */
static void det_sync(void)
{
    s_det.pie    = 0;
    s_det.alt    = 0;
    s_det.alt_on = 0U;
    s_det.pressed = s_det_press;

    switch (s_menu_detail_target) {
    case ENCODER_TARGET_VOLUME:
        s_det.titulo = "Volumen";
        s_det.valor  = ajuste_valor(AJ_VOL, s_det_val);
        break;
    case ENCODER_TARGET_SQUELCH:
        s_det.titulo = "Silenciador";
        s_det.valor  = ajuste_valor(AJ_SQL, s_det_val);
        break;
    case ENCODER_TARGET_PGA:
        s_det.titulo = "Ganancia de entrada";
        s_det.valor  = ajuste_valor(AJ_PGA, s_det_val);
        s_det.pie    = "PGA del códec, 0 a 47,5 dB";
        break;
    case ENCODER_TARGET_NR:
        s_det.titulo = "Reducción de ruido";
        s_det.valor  = ajuste_valor(AJ_NR, s_det_val);
        break;
    case ENCODER_TARGET_BACKLIGHT:
        s_det.titulo = "Brillo";
        s_det.valor  = ajuste_valor(AJ_BRILLO, s_det_val);
        break;
    case ENCODER_TARGET_SMOOTH:
        s_det.titulo = "Suavizado";
        s_det.valor  = ajuste_valor(AJ_SUAVIZ, s_det_val);
        s_det.pie    = "cuánto se promedia entre fotogramas";
        break;
    case ENCODER_TARGET_RTTY_SHIFT:
        s_det.titulo = "Desplazamiento RTTY";
        s_det.valor  = ajuste_valor(AJ_RTTY_SHIFT, s_det_val);
        break;
    case ENCODER_TARGET_CW_TONE:
        s_det.titulo = "Tono de CW";
        s_det.valor  = ajuste_valor(AJ_CW_TONO, s_det_val);
        break;
    case ENCODER_TARGET_SCALE: {
        /* El numero grande es el limite que se esta moviendo, y el pie dice
         * cual es y donde queda el otro: sin eso, dos pantallas identicas con
         * numeros distintos no dicen cual estas tocando. */
        int16_t v = (int16_t)(s_scale_adjust_max ? s_db_max : s_db_min);
        uint8_t i = 0;
        if (v < 0) { s_det_val[i++] = '-'; v = (int16_t)(-v); }
        top_u2s(&s_det_val[i], (uint32_t)v);
        s_det.titulo = s_scale_adjust_max ? "Escala: límite alto"
                                          : "Escala: límite bajo";
        s_det.valor  = s_det_val;
        s_det.alt    = "Cambiar a LO / HI";
        s_det.alt_on = s_scale_adjust_max;
        break;
    }
    default:
        s_det.titulo = "";
        s_det.valor  = "";
        break;
    }
}

/* Reparto de un toque dentro de la pantalla de un ajuste. */
static void det_touch(uint16_t x, uint16_t y, uint8_t pressed)
{
    if (pressed) {
        if (s_det_press < 0) {
            s_det_press = ui_det_hit(x, y);
            if (s_det_press >= 0) { det_sync(); ui_det_draw(&s_det); }
        }
        return;
    }
    if (s_det_press < 0) { return; }
    {
        int8_t k = s_det_press;

        s_det_press = -1;
        det_sync();
        ui_det_draw(&s_det);

        switch (k) {
        case UID_HIT_MENOS:  encoder_inject_detents(-1); break;
        case UID_HIT_MAS:    encoder_inject_detents(1);  break;
        case UID_HIT_ALT:
            /* Solo existe en escala; en las demas ese medio pie es "Volver",
             * que ya lo coge la otra mitad. */
            if (s_menu_detail_target == ENCODER_TARGET_SCALE) {
                encoder_inject_press();
            } else {
                menu_grid_show();
            }
            break;
        case UID_HIT_VOLVER: menu_grid_show(); break;
        default: break;
        }
    }
}

static void menu_detail_show(encoder_target_t target)
{
    s_encoder_target = target;
    s_menu_detail_target = target;
    s_menu_detail_active = 1U;
    s_menu_cfg_active = 0U;
    s_det_press = -1;

    det_sync();
    ui_det_draw(&s_det);

    /* Esta pantalla tampoco usa widgets de ui.c. */
    s_menu_open = 1U;
}

/*
 * settings_value_redraw(): the dispatcher tune_encoder_poll() calls
 * instead of aux_row_display_draw() directly, for every target that
 * can now be reached from EITHER the main screen's aux row OR the
 * menu's detail view - it decides which (if either) is actually
 * visible right now and repaints that one, so the live value always
 * updates wherever the user can actually see it.
 */
static void settings_value_redraw(void)
{
    if (s_menu_open) {
        if (s_menu_detail_active) {
            menu_detail_value_redraw();
        }
        /* else: grid is showing, not a detail view - nothing visible
         * needs repainting here (shouldn't normally happen, since
         * s_encoder_target only becomes one of these targets via
         * menu_detail_show(), which also sets s_menu_detail_active). */
    } else {
        aux_row_display_draw();
    }
}

/*
 * ETAPA 6: la rejilla de ajustes.
 *
 * Se conserva el nombre y las tres llamadas que ya existian (abrir el menu, y
 * volver desde el detalle o desde una lista) y solo cambia lo que hace por
 * dentro: ahora es la rejilla comun, con los 27 ajustes repartidos en cuatro
 * paginas y cada celda diciendo su valor.
 *
 * Las ~230 lineas que habia aqui montando 27 ui_button_t a mano, pagina por
 * pagina y con una columna entera gastada en navegar, se van enteras: esa
 * informacion vive ahora en k_ajustes[], que es una tabla.
 */
static void menu_grid_show(void)
{
    cfg_show(CFG_AJUSTES);
}


static void menu_screen_close(void)
{
    /* La barra de acciones se repinta SIEMPRE al salir de un menu, no solo
     * cuando quien cierra sabe que ha cambiado algo. Elegir una banda cambia
     * el modo y el paso, y el boton "Modo" se quedaba con el valor viejo
     * (visto por el dueno del proyecto, 22/09/2026). Ponerlo aqui lo cubre
     * para todas las pantallas a la vez, en vez de acordarse en cada
     * callback. */
    s_grid_pant = GRID_NADA;
    s_menu_cfg_active = 0U;
    s_menu_open = 0U;
    s_menu_detail_active = 0U;
    s_menu_bands_active = 0U;
    s_menu_step_active = 0U;
    s_menu_mode_active = 0U;
    s_menu_freq_active = 0U;
    s_menu_time_active = 0U;
    /* Hand the knob back to TUNE unconditionally - fixes a real bug
     * (26/08/2026, reported by the project owner): closing the menu
     * via EXIT left s_encoder_target on whatever detail was open
     * (PGA, SCALE, SQUELCH, ...), so the knob kept adjusting that
     * parameter after the menu screen was already gone, with no way
     * back short of the long-press gesture below (tune_encoder_poll())
     * or reopening the menu and picking something else to bounce
     * through. That long-press handler already does exactly this
     * s_encoder_target reset + aux_row_display_draw() pair for its
     * own case - this just makes EXIT (and every other path that
     * reaches menu_screen_close(): a MODE/STEP/BANDS/RTTY picker
     * selection, screen_sleep_enter(), ...) do the same, since none
     * of those should leave a menu-opened target "hot" either. Safe
     * even when the target was already TUNE (menu_detail_show() is
     * the only thing that ever sets a non-TUNE target from inside the
     * menu, and it can't run while the menu is closed) and safe with
     * VOLUME too, whether that was reached via its own menu tile or
     * (harmlessly redundantly, since the bottom VOL button's own
     * timeout already retires it - see s_volume_target_last_ms's
     * comment) via the bottom VOL button. */
    if (s_encoder_target != ENCODER_TARGET_TUNE) {
        s_encoder_target = ENCODER_TARGET_TUNE;
        aux_row_display_draw();
    }
    /* Only the spectrum+waterfall panels need restoring - the top
     * bar, right column, and bottom bar were never hidden or
     * touch-disabled while the menu was open (see s_menu_screen's
     * declaration comment), so redrawing the WHOLE screen here (the
     * old behavior, back when the menu covered everything) would just
     * be wasted EXMC bandwidth and a visible flash of things that
     * never changed. ui_panel_draw() restores each panel's
     * background+border immediately; the actual spectrum TRACE and
     * waterfall content follow naturally on the next
     * sdr_spectrum_waterfall_tick() frame (within ~33ms - imperceptible). */
    ui_panel_draw(&s_spectrum_panel);
    ui_panel_draw(&s_waterfall_panel);
    /* The span-label row (+/- edges, "LO" marker, divider) lives
     * INSIDE the spectrum panel and gets wiped by the menu's black
     * fill same as the border does - restore it too, zoom-aware in
     * case the ZOOM tile was used while the menu was open. */
    spec_chrome_full_draw(); /* ETAPA 3b: la canaleta y la leyenda tambien se han quedado tapadas por el menu */
    top_sync();
    ui_top_draw(&s_top);     /* frecuencia, modo y chips, por si la pantalla los cambio */
    act_draw();              /* ver el comentario de arriba: el boton "Modo" */
    debug_print("menu: settings screen closed\n");
}

/*
 * screen_sleep_enter()/screen_wake() - added 10/08/2026, per the
 * project owner: a HW-page action (see menu_tile_sleep_callback())
 * for long, unattended listening sessions where the display is both a
 * battery drain (backlight PWM + constant EXMC redraw traffic) and,
 * sitting right next to the RF front-end on this board, a real source
 * of receive interference. Closing the menu and blanking the panel
 * here is a ONE-TIME EXMC write, not the ongoing traffic this mode
 * exists to stop - everything else that would keep touching the panel
 * (spectrum/waterfall redraw, the RTTY scope, touch polling) is
 * instead skipped entirely by main()'s loop while s_screen_asleep is
 * set - see s_screen_asleep's declaration comment for the exact list,
 * and just as importantly, what DOESN'T stop: the radio itself.
 * Demodulation/audio run straight off the DMA ISR (s_block_hook, see
 * sdr_rx.c), never through this loop at all, so listening continues
 * uninterrupted with the screen dark - that's the whole point.
 */
static void screen_sleep_enter(void)
{
    if (s_menu_open) {
        menu_screen_close(); /* leaves nothing stale registered in s_menu_screen behind for when the menu next opens */
    }
    gfx_guard_top_set(0); /* aqui si queremos borrar el panel entero, cabecera incluida */
    gfx_fill_screen(GFX_COLOR_BLACK); /* one-time write - see this function's comment, not the ongoing traffic sleep exists to stop */
    backlight_sleep();
    s_screen_asleep = 1U;
    debug_print("screen: asleep (press the knob to wake)\n");
}

/*
 * Wakes from screen_sleep_enter() - triggered from main()'s loop by a
 * SHORT press on the encoder while s_screen_asleep is set (see its
 * comment there for why LONG press and rotation are both discarded
 * instead of also waking/acting on it).
 */
static void screen_wake(void)
{
    backlight_wake();
    s_screen_asleep = 0U;
    /* Full repaint, the same call boot uses - every periodic readout
     * (spectrum/waterfall, time, battery, S-meter, badges) was frozen
     * the whole time asleep, so a partial/diff redraw has nothing
     * valid left to diff against; simplest correct thing is exactly
     * what boot already does. */
    radio_screen_draw();
    debug_print("screen: awake\n");
}

/*
 * Called from the main loop: drains the encoder and applies tuning.
 * Deltas accumulate inside the encoder driver while the loop is busy
 * with the FFT/waterfall, so a fast spin during a slow loop pass
 * coalesces into ONE retune of (detents * step) instead of queueing
 * stale intermediate retunes - the radio always jumps straight to
 * where the knob actually is.
 */
/*
 * Programs the LO for s_tune_hz according to the CURRENT demod mode,
 * and keeps demod_am's if-offset flag in sync with it. Added
 * 31/07/2026 alongside WFM and factored out of tune_encoder_poll()
 * because now TWO things can trigger a re-tune at the same
 * frequency: moving the encoder, and toggling the MODE button into or
 * out of WFM (see demo_button_callback()) - before WFM, only the
 * encoder ever changed s_tune_hz, so this logic lived inline there.
 *
 * WFM tunes the LO DIRECTLY on the selected frequency, no offset -
 * see demod_am.h's WFM note for why (it needs the full +/-96kHz of
 * complex bandwidth centered on the station, not shifted 48kHz off
 * it). AM/USB/LSB keep the existing low-IF behavior unchanged.
 */
/*
 * s_lo_source_is_gd32: which physical oscillator is currently driving
 * CLK0/CLK1's net - 0xFF (unknown) forces the very first call to fully
 * set up whichever side is actually needed, same "sentinel forces
 * first-time full init" shape as ms5351.c's own s_last_div=0. See
 * lo_gen_gd32.h's big comment for why the crossover exists and why
 * it's narrow (LO_GEN_CROSSOVER_HZ, 300kHz) rather than covering the
 * whole <5MHz range the project owner originally asked about.
 *
 * *** 01/09/2026, full history: forced off, then genuinely re-enabled
 * same day *** - disabling MCLK to free TIMER2 caused a real
 * regression (frequencies off, degraded WFM reception) and was
 * reverted, which briefly meant TIMER2 wasn't actually available for
 * this module (want_gd32 was hardcoded to 0 here for a while). Real
 * fix: gd32_i2s_mclk_timer_start() (gd32_i2s.c) was moved to
 * TIMER7_CH0/PC6 instead - a different alternate function the project
 * owner's own datasheet table showed was also available on that same
 * pin - freeing TIMER2 for real, no MCLK tradeoff needed. want_gd32 is
 * evaluated normally again below.
 */
static uint8_t s_lo_source_is_gd32 = 0xFFU;

static void apply_lo_tune(uint32_t freq_hz)
{
    uint8_t is_wfm = (demod_am_get_mode() == DEMOD_MODE_WFM) ? 1U : 0U;
    /* Same low-IF offset math as before, just computed once up front
     * so both the MS5351 and the GD32 generator branches below tune
     * to the identical actual LO frequency the old code already used -
     * this offset need is about avoiding the LO-leakage artifact
     * landing on the wanted signal, which has nothing to do with
     * which physical oscillator produces the LO. */
    uint32_t actual_lo_hz = is_wfm ? freq_hz : (freq_hz - demod_if_offset_hz());
    uint8_t want_gd32 = (actual_lo_hz < LO_GEN_CROSSOVER_HZ) ? 1U : 0U;
    uint8_t ok;

    /* Only touch the OTHER side when actually crossing the boundary -
     * same "don't redo I2C/timer work (and don't risk an audible
     * click) on every single retune, only on a real zone change"
     * discipline ms5351.c's own s_last_div/s_last_lowf_zone checks
     * already use. */
    if (want_gd32 != s_lo_source_is_gd32) {
        if (want_gd32) {
            (void)ms5351_lo_disable();
        } else {
            lo_gen_gd32_stop();
        }
        s_lo_source_is_gd32 = want_gd32;
    }

    if (want_gd32) {
        ok = lo_gen_gd32_set_freq(actual_lo_hz);
        if (!ok) {
            debug_print_dec("tune: lo_gen_gd32_set_freq FAILED at Hz", actual_lo_hz);
        }
    } else {
        ok = ms5351_set_lo_freq(actual_lo_hz);
        if (!ok) {
            debug_print_dec("tune: ms5351_set_lo_freq FAILED at Hz", actual_lo_hz);
        }
    }
    if (ok) {
        demod_am_set_if_offset_active(is_wfm ? 0U : 1U);
    }

    /* See s_settings_ready_for_autosave's comment: only a REAL retune
     * (encoder/BANDS/keypad, after boot) should trigger the debounced
     * autosave - not the two boot-time calls that just re-apply
     * whatever was already loaded (or the firmware default). */
    if (s_settings_ready_for_autosave) {
        settings_mark_dirty();
    }
}

/*
 * RF-level (analog PGA) auto-AGC poll - called once per main-loop
 * iteration, same as tune_encoder_poll() right below (both are
 * "drain some ISR-set state and act on it outside the ISR" jobs).
 * See s_rf_agc_enabled's declaration comment for the full design;
 * this function is just the ballistics: instant-ish attack (one
 * RF_AGC_STEP_X2 PGA-backoff step per RF_AGC_ATTACK_COOLDOWN_MS at
 * most, as fast as the flag keeps firing, escalating to an Rin step
 * instead once PGA backoff is maxed and clipping still isn't gone),
 * slow release (one step back down only after RF_AGC_RELEASE_COOLDOWN_MS
 * of a genuinely clip-free signal, PGA first then Rin).
 */
static void rf_agc_poll(void)
{
    uint32_t now = g_msticks;
    uint8_t clipped;

    if (!s_rf_agc_enabled) {
        return;
    }

    clipped = demod_am_get_and_clear_rf_clip_flag();

    if (clipped) {
        s_rf_agc_last_clip_ms = now; /* restarts the release timer on ANY clip, even mid-cooldown */
        if ((now - s_rf_agc_last_action_ms) >= RF_AGC_ATTACK_COOLDOWN_MS) {
            if (s_rf_agc_backoff_x2 < (int16_t)RF_AGC_BACKOFF_MAX_X2) {
                s_rf_agc_backoff_x2 = (int16_t)(s_rf_agc_backoff_x2 + RF_AGC_STEP_X2);
                if (s_rf_agc_backoff_x2 > (int16_t)RF_AGC_BACKOFF_MAX_X2) {
                    s_rf_agc_backoff_x2 = (int16_t)RF_AGC_BACKOFF_MAX_X2;
                }
                rf_agc_apply_pga();
                s_rf_agc_last_action_ms = now;
                badges_draw(); /* updates the OVR badge - see its comment; safe from ANY context, unlike the old tile redraw */
                debug_print_dec("rf_agc: backing off, now x2 units", (uint32_t)s_rf_agc_backoff_x2);
            } else if (s_rf_agc_rin_level < (uint8_t)AIC3204_RIN_40K) {
                /* PGA backoff already maxed out AND still clipping -
                 * last resort, see s_rf_agc_enabled's comment. */
                rf_agc_escalate_rin();
                s_rf_agc_last_action_ms = now;
            }
            /* else: maxed PGA backoff AND maxed Rin (40k) - nothing
             * more this can do automatically; a signal strong enough
             * to still clip through 30dB of PGA backoff plus 12dB of
             * Rin attenuation is beyond what this feature can fix -
             * time for a real external attenuator or a lower manual
             * PGA ceiling. */
        }
    } else if ((s_rf_agc_backoff_x2 > 0 || s_rf_agc_rin_level > s_att_suelo)
               && (now - s_rf_agc_last_clip_ms) >= RF_AGC_RELEASE_COOLDOWN_MS
               && (now - s_rf_agc_last_action_ms) >= RF_AGC_ATTACK_COOLDOWN_MS) {
        if (s_rf_agc_backoff_x2 > 0) {
            s_rf_agc_backoff_x2 = (int16_t)(s_rf_agc_backoff_x2 - RF_AGC_STEP_X2);
            if (s_rf_agc_backoff_x2 < 0) {
                s_rf_agc_backoff_x2 = 0;
            }
            rf_agc_apply_pga();
            s_rf_agc_last_action_ms = now;
            badges_draw(); /* see the attack branch's comment above */
            debug_print_dec("rf_agc: recovering, now x2 units", (uint32_t)s_rf_agc_backoff_x2);
        } else {
            /* PGA backoff already fully recovered to 0 at this Rin
             * level - step Rin back down one, which itself restores
             * some PGA backoff again (see rf_agc_deescalate_rin()) so
             * there's room to keep recovering PGA-side on the NEXT
             * release step instead of needing a second Rin step right
             * away. */
            /* Solo hasta el suelo que haya pedido el usuario: por debajo de
             * ahi el automatico no manda. Sin esta condicion, poner -12 en
             * una banda tranquila duraba lo que tardara el primer ciclo de
             * recuperacion. */
            if (s_rf_agc_rin_level > s_att_suelo) {
                rf_agc_deescalate_rin();
                s_rf_agc_last_action_ms = now;
            }
        }
        /* Deliberately NOT resetting s_rf_agc_last_clip_ms here - the
         * NEXT recovery step still has to wait out the same
         * RELEASE_COOLDOWN_MS measured from the last real clip, not
         * from this recovery step, so a string of steps back up to
         * the ceiling doesn't creep faster than one every 3s just
         * because each step itself resets some other timer. */
    }
}

/*
 * Multi-line decoded-text panel - REWRITTEN 10/08/2026, per the
 * project owner, from the original single-line ticker (see this
 * file's git history for rtty_screen_text_push()): that version had
 * two real problems - CR/LF collapsed to a plain space instead of an
 * actual line break (so a station's line structure was invisible, just
 * one long run-on ticker), and only RTTY_TEXT_STRIP_H (24px, one line)
 * was reserved for it, no real scrollback at all.
 *
 * Now drawn into the SAME screen region the normal waterfall panel
 * occupies (WF_PANEL_Y downward) - the waterfall itself is frozen the
 * whole time the RTTY scope is showing anyway (see digi_panel_active()'s
 * comment: sdr_spectrum_waterfall_tick() simply isn't called), so that
 * space was sitting idle. ALSO ENLARGED beyond the waterfall's native
 * 76px by reclaiming part of the scope's own trace height too - see
 * RTTY_TEXT_PANEL_H/RTTY_SCOPE_TRACE_H's comment just below for the
 * exact split. Net effect: a real multi-line RTTY_TEXT_ROWS x
 * RTTY_TEXT_COLS character grid instead of one ticker line, at the
 * cost of a shorter (but still plenty resolving, see rtty_scope_draw()'s
 * own comment on why the FFT itself is unaffected) tuning-scope trace.
 *
 * Genuinely LINE-ORIENTED now, not a byte ring buffer: rtty_text_push()
 * tracks a cursor (row, col) into a fixed character grid.
 *   - Printable characters just get placed at the cursor and advance
 *     it; hitting RTTY_TEXT_COLS without an explicit CR/LF hard-wraps
 *     to a new row (character wrap, not word wrap - this decoder has
 *     no idea where a word boundary will fall until a character is
 *     already committed to the grid, so word-wrap would need a whole
 *     extra layer of lookahead/reflow for little practical benefit at
 *     ~50 baud).
 *   - CR and LF both start a new row - the actual fix for "line
 *     endings aren't interpreted" - but a RUN of consecutive CR/LF
 *     characters (real stations commonly send CR CR LF, or CR LF, at
 *     end of line - the double CR gives an electromechanical
 *     teleprinter's print head time to return) collapses to exactly
 *     ONE line break via s_rtty_text_last_was_eol, so that convention
 *     doesn't leave a trail of blank rows eating into the limited
 *     scrollback.
 *   - Once RTTY_TEXT_ROWS fills, the grid scrolls up by one row (the
 *     oldest line dropped) instead of wrapping/overwriting - a real
 *     scrollback feel instead of the old ticker's single-line slide.
 */
/* Cabecera (boton de borrar + chapa de velocidad) mas los ocho renglones.
 * Los tres numeros salen de ui_digi.h, que es quien dibuja: asi el alto que
 * reserva main.c no puede quedarse desfasado del que usa el modulo. */
#define RTTY_TEXT_PANEL_H  (uint16_t)(UDG_HDR_H + UDG_ROWS * UDG_LINE_H)
#define RTTY_TEXT_PANEL_Y  (uint16_t)((WF_PANEL_Y + WATERFALL_ROWS + 4U) - RTTY_TEXT_PANEL_H)
#define RTTY_SCOPE_GAP_H   2U /* thin gap between the scope trace and the text panel, same idea as WF_PANEL_Y's own "64+280+2" gap from the normal spectrum panel */
#define RTTY_SCOPE_TRACE_H (uint16_t)(RTTY_TEXT_PANEL_Y - SPEC_Y - RTTY_SCOPE_GAP_H) /* 358 - 144 - 2 = 212, replaces the old SPEC_H-based bar_area_h */

#define RTTY_TEXT_SCALE    2U
#define RTTY_TEXT_LINE_H   ((uint16_t)UDG_LINE_H)
#define RTTY_TEXT_CHAR_W   12U /* (5+1)px * scale 2 - mirrors gfx.c's own per-glyph step formula (gfx_font.h isn't included outside gfx.c, so this is a plain literal like RTTY_TEXT_COLS' comment already is) */
#define RTTY_TEXT_COLS     66U /* MAIN_W(800) / RTTY_TEXT_CHAR_W = 66 - was 56 (MAIN_W=676) before the status-strip redesign widened MAIN_W to the full screen */
#define RTTY_TEXT_ROWS     ((uint16_t)UDG_ROWS)

/*
 * El panel lo dibuja ahora gfx2/ui_digi.c (etapa 20). Esto es lo que se le
 * pasa: los ocho renglones, la chapa de estado y el boton de borrar. Se
 * rellena justo antes de cada dibujo desde la rejilla de abajo, que sigue
 * siendo la que mandan los decodificadores.
 */
static ui_digi_state_t s_digi;
static char            s_digi_chip[16];

static char    s_rtty_text_grid[RTTY_TEXT_ROWS][RTTY_TEXT_COLS + 1U]; /* +1 NUL per row */
static uint8_t s_rtty_text_row_len[RTTY_TEXT_ROWS];
static uint8_t s_rtty_text_cur_row;
static uint8_t s_rtty_text_cur_col;
static uint8_t s_rtty_text_last_was_eol = 1U; /* starts "true" so a leading CR/LF right after rtty_text_panel_reset() doesn't waste a blank first line */

/*
 * s_rtty_text_draw_row/col: how far rtty_text_panel_draw() has ALREADY
 * painted onto the real LCD - always <= (s_rtty_text_cur_row,
 * s_rtty_text_cur_col) in reading order. Added 10/08/2026, per the
 * project owner ("mucho flicker el texto"): the original version
 * cleared the WHOLE 676x144 panel (gfx_fill_rect, ~97k pixels) and
 * redrew every line on EVERY new character, even though normally only
 * the single newest glyph actually changed - that clear-then-redraw
 * cycle is exactly what read as flicker (a visible black flash before
 * the text reappears), and was needless EXMC traffic on top of it.
 * Now the common case (appending a character, no scroll) draws ONLY
 * that one new glyph via gfx_char() directly onto its own still-blank
 * background - no clear at all, so nothing ever flashes.
 * s_rtty_text_full_redraw (below) is the escape hatch for the cases
 * that genuinely need a full repaint (grid scrolled - every row's
 * SCREEN POSITION changed - or the panel was just covered by the menu
 * and needs repainting from the grid, not the grid re-cleared - see
 * main()'s two separate transition checks for entering RTTY mode vs.
 * merely the menu closing again).
 */
static uint8_t s_rtty_text_draw_row;
static uint8_t s_rtty_text_draw_col;
static uint8_t s_rtty_text_full_redraw = 1U; /* starts 1 so the very first draw after boot/reset paints the (blank) panel once */

/*
 * Blanks the grid and the panel, and resets the cursor - called ONLY
 * on genuinely entering RTTY mode fresh (see main()'s s_rtty_mode_was_
 * active transition, NOT the s_rtty_scope_was_active one - opening/
 * closing the menu while already in RTTY must NOT wipe the
 * scrollback, only repaint it, see rtty_text_force_redraw() below).
 */
/* 22/09/2026: CW_CLR_X/Y/W/H se fueron con el boton. Su sitio y su tamano
 * los decide ahora ui_digi.c (UDG_BTN_*), que es quien lo dibuja y quien
 * resuelve su toque - antes eran dos copias de la misma geometria. */

/*
 * Un color del tema en el formato que pide gfx.c.
 *
 * Las rayas de sintonia del osciloscopio se dibujan con gfx_vline(), que
 * escribe RGB565 directo al panel, y llevaban colores fijos -cian, naranja,
 * gris- de antes de que hubiera temas. Con un tema ambar o frio esas tres
 * rayas eran lo unico en pantalla que no se enteraba del cambio.
 */
static uint16_t color_tema(uint32_t hex)
{
    return gfx2_to565((uint8_t)((hex >> 16) & 0xFFU),
                      (uint8_t)((hex >> 8) & 0xFFU),
                      (uint8_t)(hex & 0xFFU));
}

/* Borra la zona de los modos digitales con el fondo del tema. */
static void digi_borra(gfx2_surf_t *s, void *ctx)
{
    (void)ctx;
    gfx2_fill(s, 0, (int16_t)SPEC_Y, (int16_t)MAIN_W,
              (int16_t)((RTTY_TEXT_PANEL_Y + RTTY_TEXT_PANEL_H) - SPEC_Y),
              gfx2_rgb(PAL_SURF_0));
}

/* Pasa la rejilla al estado que entiende ui_digi.c. Una sola funcion para
 * que no haya dos sitios decidiendo que renglon va donde. */
static void digi_sync(void)
{
    uint8_t r;

    s_digi.y = (int16_t)RTTY_TEXT_PANEL_Y;
    s_digi.h = (int16_t)RTTY_TEXT_PANEL_H;
    for (r = 0U; r < RTTY_TEXT_ROWS; r++) {
        s_digi.fila[r] = (s_rtty_text_row_len[r] > 0U) ? s_rtty_text_grid[r] : 0;
    }

    s_digi.btn   = "Borrar";
    /* btn_press NO se toca aqui: lo pone el reparto de toques, que es quien
     * sabe si el dedo esta encima. digi_sync() corre en cada cuadro y lo
     * borraria en el siguiente. */

    if (cw_get_enabled()) {
        uint32_t w = (uint32_t)(cw_get_wpm() + 0.5f);
        uint8_t i = 0U;

        if (w > 99U) { w = 99U; }
        if (w >= 10U) { s_digi_chip[i++] = (char)('0' + (w / 10U)); }
        s_digi_chip[i++] = (char)('0' + (w % 10U));
        s_digi_chip[i++] = ' '; s_digi_chip[i++] = 'p';
        s_digi_chip[i++] = 'p'; s_digi_chip[i++] = 'm';
        s_digi_chip[i] = '\0';
        s_digi.chip = s_digi_chip;
        s_digi.enganchado = cw_get_decode_ok();
    } else {
        s_digi.chip = 0;
        s_digi.enganchado = 0U;
    }
}

static void rtty_text_panel_reset(void)
{
    uint8_t r;

    for (r = 0; r < RTTY_TEXT_ROWS; r++) {
        s_rtty_text_grid[r][0] = '\0';
        s_rtty_text_row_len[r] = 0U;
    }
    s_rtty_text_cur_row = 0U;
    s_rtty_text_cur_col = 0U;
    s_rtty_text_last_was_eol = 1U;
    digi_sync();
    ui_digi_draw_texto(&s_digi);
    s_rtty_text_draw_row = 0U;
    s_rtty_text_draw_col = 0U;
    s_rtty_text_full_redraw = 0U; /* just painted blank directly above - nothing left pending */
}

/*
 * Forces the NEXT rtty_text_panel_draw() to do a full repaint from the
 * grid, WITHOUT touching the grid's actual content - added 10/08/2026
 * for the "menu covered this area, now it needs repainting" case (see
 * main()'s comment): the scrollback text itself is still exactly
 * right, only the physical LCD pixels underneath went stale while the
 * menu was drawn over them.
 */
static void rtty_text_force_redraw(void)
{
    s_rtty_text_full_redraw = 1U;
}

/* Advances the cursor to a fresh row, scrolling the whole grid up by
 * one (dropping the oldest line) once RTTY_TEXT_ROWS is full - the
 * actual "real scrollback" behavior, versus the old ticker's single-
 * line slide. A scroll shifts every row's SCREEN position, so the
 * incremental single-glyph draw path can't handle it - falls back to
 * rtty_text_force_redraw() in that case only. */
static void rtty_text_newline(void)
{
    if ((uint8_t)(s_rtty_text_cur_row + 1U) < RTTY_TEXT_ROWS) {
        s_rtty_text_cur_row++;
    } else {
        uint8_t r;

        for (r = 0; r < (RTTY_TEXT_ROWS - 1U); r++) {
            uint8_t i;

            for (i = 0; i <= s_rtty_text_row_len[r + 1U]; i++) { /* <= to copy the NUL too */
                s_rtty_text_grid[r][i] = s_rtty_text_grid[r + 1U][i];
            }
            s_rtty_text_row_len[r] = s_rtty_text_row_len[r + 1U];
        }
        s_rtty_text_grid[RTTY_TEXT_ROWS - 1U][0] = '\0';
        s_rtty_text_row_len[RTTY_TEXT_ROWS - 1U] = 0U;
        /* s_rtty_text_cur_row stays at RTTY_TEXT_ROWS-1 - already the
         * bottom row before AND after the scroll, only its CONTENTS
         * (now blank, ready for the new line) changed. */
        rtty_text_force_redraw();
    }
    s_rtty_text_cur_col = 0U;
}

/* Places one printable character at the cursor, hard-wrapping to a
 * new row first if the current one is already full (see this block's
 * top comment on why character-wrap, not word-wrap). */
static void rtty_text_putc(char c)
{
    uint8_t row;

    /*
     * Dos motivos para saltar de linea, y hacen falta los dos.
     *
     * El de siempre: se acabaron las columnas de la rejilla, que es lo que
     * cabe en memoria.
     *
     * Y desde la etapa 20, que el renglon YA NO CABE A LO ANCHO. El panel se
     * dibuja con tipografia proporcional, asi que 66 caracteres pueden ser
     * 450 px de minusculas estrechas o 996 de mayusculas anchas - medido:
     * 66 "W" dan 996 px en una pantalla de 800. Sin esta comprobacion, una
     * linea de indicativos en mayusculas se cortaria por la derecha y el
     * final desapareceria sin avisar.
     *
     * Se mide el renglon ya escrito mas el caracter que entra, no el
     * renglon entero despues: asi el que provoca el salto se escribe en la
     * linea nueva y no se pierde.
     */
    if (s_rtty_text_cur_col >= RTTY_TEXT_COLS) {
        rtty_text_newline();
    } else if (s_rtty_text_cur_col > 0U) {
        char uno[2];
        uno[0] = c; uno[1] = '\0';
        if (gfx2_text_w(s_rtty_text_grid[s_rtty_text_cur_row], &font_ui_14b)
            + gfx2_text_w(uno, &font_ui_14b) > (int16_t)(MAIN_W - 12)) {
            rtty_text_newline();
        }
    }
    row = s_rtty_text_cur_row;
    s_rtty_text_grid[row][s_rtty_text_cur_col] = c;
    s_rtty_text_cur_col++;
    s_rtty_text_row_len[row] = s_rtty_text_cur_col;
    s_rtty_text_grid[row][s_rtty_text_cur_col] = '\0';
}

/*
 * Feeds one decoded character into the multi-line grid - the actual
 * CR/LF interpretation fix, replacing the old rtty_screen_text_push()'s
 * "collapse to a space" - see this block's top comment for the
 * consecutive-CR/LF collapsing rule. Purely updates the GRID (state) -
 * never touches the LCD directly, so it's safe to call unconditionally
 * from rtty_poll() every main loop pass regardless of whether the
 * scope panel is actually visible right now (menu open, different
 * mode momentarily, etc.) - same "ISR/background sets state, the
 * visible-when-appropriate draw call reads it" split this project
 * already uses everywhere else. rtty_text_panel_draw() (called only
 * when the scope is genuinely on screen) is what turns this into
 * pixels.
 */
static void rtty_text_push(char c)
{
    if (c == '\r' || c == '\n') {
        if (!s_rtty_text_last_was_eol) {
            rtty_text_newline();
        }
        s_rtty_text_last_was_eol = 1U;
    } else {
        s_rtty_text_last_was_eol = 0U;
        rtty_text_putc(c);
    }
}

/*
 * Paints the panel from the grid - see s_rtty_text_draw_row/col's
 * comment for the flicker fix this implements. Two paths:
 *
 *   - FULL redraw (s_rtty_text_full_redraw): the expensive path, only
 *     taken right after a scroll or after the menu covered this area -
 *     clears the whole panel once and redraws every non-empty row.
 *   - INCREMENTAL (the common case, every OTHER call): draws just the
 *     characters that arrived since the last draw, one gfx_char() each,
 *     straight onto still-blank pixels - no clear, so nothing flashes.
 *     Safe to assume at most one row's worth of characters arrived
 *     between two calls: RTTY's fastest common rate (100 baud) is
 *     still one character every ~10ms, while this is only called once
 *     per scope FFT frame (~20-30fps, ~33-50ms/frame) - filling an
 *     entire 56-char row between two draws would need a baud rate far
 *     beyond anything this decoder supports.
 */
static void rtty_text_panel_draw(void)
{
    digi_sync();

    if (s_rtty_text_full_redraw) {
        ui_digi_draw_texto(&s_digi);
        s_rtty_text_full_redraw = 0U;
        s_rtty_text_draw_row = s_rtty_text_cur_row;
        s_rtty_text_draw_col = s_rtty_text_cur_col;
        return;
    }

    /*
     * Por RENGLONES, no por caracteres.
     *
     * La version anterior dibujaba UN glifo en su hueco, que con la fuente
     * de ancho fijo se sabia cual era sin medir nada. Con tipografia
     * proporcional el hueco depende de lo que haya escrito antes en la
     * linea, asi que se redibuja la linea entera.
     *
     * Sale barato: 800x18 px por caracter, y a 20 palabras por minuto son
     * diez caracteres por segundo. El espectro repinta 800x208 treinta veces
     * por segundo al lado de esto.
     */
    while (s_rtty_text_draw_row != s_rtty_text_cur_row) {
        ui_digi_draw_fila(&s_digi, s_rtty_text_draw_row);
        s_rtty_text_draw_row++;
        s_rtty_text_draw_col = 0U;
    }
    if (s_rtty_text_draw_col != s_rtty_text_cur_col) {
        ui_digi_draw_fila(&s_digi, s_rtty_text_draw_row);
        s_rtty_text_draw_col = s_rtty_text_cur_col;
    }
}

/*
 * Drains rtty.c's decoded-character ring buffer, both to the on-screen
 * text panel (rtty_text_push(), above) and to debug UART - added
 * 08/08/2026. UART side: accumulates into a small line buffer and
 * flushes on CR/LF or when nearly full, rather than one debug_print()
 * call per character - RTTY's ~45 baud means a character every
 * ~170ms at best, so call overhead isn't a real concern, but a few
 * dozen individual "single-char" UART writes per line would still be
 * needlessly noisy in the log output. Both consumers read from the
 * SAME rtty_get_char() ring buffer via this one drain loop - can't
 * have two separate poll functions each calling rtty_get_char()
 * independently, since it's a single tail pointer (whichever drains
 * first would silently steal characters from the other).
 */
static void rtty_poll(void)
{
    static char line[64];
    static uint8_t line_len = 0U;
    char c;

    while (rtty_get_char(&c)) {
        rtty_text_push(c);

        if (c == '\r' || c == '\n') {
            if (line_len > 0U) {
                line[line_len] = '\0';
                debug_print("rtty: ");
                debug_print(line);
                debug_print("\n");
                line_len = 0U;
            }
        } else if (line_len < (uint8_t)(sizeof(line) - 1U)) {
            line[line_len] = c;
            line_len++;
        } else {
            /* line buffer full without a CR/LF - flush what we have
             * rather than silently drop the rest of a long line. */
            line[line_len] = '\0';
            debug_print("rtty: ");
            debug_print(line);
            debug_print("\n");
            line_len = 0U;
        }
    }
}

/*
 * Saca el texto decodificado del CW al MISMO panel y al MISMO UART que
 * el RTTY, con la misma acumulacion por lineas. Es una funcion aparte de
 * rtty_poll() y no una rama dentro de ella por la razon que da el propio
 * comentario de rtty_poll(): cada decodificador tiene su anillo de
 * salida con su propia cola, y de cada anillo tiene que tirar uno solo.
 * Dos anillos, dos funciones. Lo que comparten es el destino.
 *
 * No hace falta comprobar si el CW esta encendido: si no lo esta, su
 * anillo esta vacio y el bucle no da ni una vuelta.
 */
static void cw_poll(void)
{
    static char line[64];
    static uint8_t line_len = 0U;
    char c;

    while (cw_get_char(&c)) {
        rtty_text_push(c);

        if (c == '\r' || c == '\n') {
            if (line_len > 0U) {
                line[line_len] = '\0';
                debug_print("cw: ");
                debug_print(line);
                debug_print("\n");
                line_len = 0U;
            }
        } else if (line_len < (uint8_t)(sizeof(line) - 1U)) {
            line[line_len] = c;
            line_len++;
        } else {
            line[line_len] = '\0';
            debug_print("cw: ");
            debug_print(line);
            debug_print("\n");
            line_len = 0U;
        }
    }
}

/*
 * Clears the spectrum panel once, on the transition INTO the scope
 * from the normal spectrum (see that call site's comment) - never
 * per-frame. Unlike the old hand-rolled bar-diff renderer this used
 * to protect, spectrum_draw() (see rtty_scope_draw(), rewritten
 * 08/08/2026 to reuse it - per the project owner, wanting the normal
 * spectrum's HEATMAP/LINE/OUTLINE rendering instead of a plain green
 * bar plot) already redraws every column unconditionally each call,
 * so there's no "stale diff baseline" risk anymore - this is now
 * purely cosmetic, avoiding a brief (<=1 scope-frame, ~43ms) flash of
 * the old RF spectrum's last frame while the FIRST new FFT window
 * fills. Only clears down to RTTY_SCOPE_TRACE_H now (used to be the
 * full old SPEC_H) - the rest of the old spectrum-panel area below
 * that now belongs to the text panel, reset separately by
 * rtty_text_panel_reset() at the same call site (see main()'s
 * transition block).
 */
static void rtty_scope_panel_reset(void)
{
    /*
     * Se borra HASTA EL FINAL DEL PANEL DE TEXTO, no solo la altura de la
     * traza (22/09/2026, viendo una foto de CW real).
     *
     * El motivo: la regla de frecuencias del espectro normal vive en
     * y=318, que cae DENTRO de la banda que ocupa el panel de texto
     * (280..424). Al entrar en un modo digital el espectro deja de
     * dibujarse -y con el la regla-, pero los pixeles que dejo pintados
     * la ultima vez se quedan ahi: el panel de texto escribe encima sin
     * borrar el fondo entero, asi que las cifras de la regla asomaban
     * ENTRE los renglones del texto decodificado. En la foto se leia el
     * QSO con "13.956  13.980  14.004" atravesado por la mitad.
     *
     * Y ademas esas cifras eran mentira ahi: son frecuencias de radio, y
     * lo que hay encima en un modo digital es un espectro de AUDIO.
     *
     * Borrar de mas es gratis porque justo despues repintan los dos
     * dueños de la zona: la traza y el texto, este ultimo con
     * rtty_text_panel_reset() o rtty_text_force_redraw() segun el caso.
     */
    /* 22/09/2026: era un gfx_fill_rect() a negro puro. Con temas claros u
     * oscuros distintos eso dejaba un rectangulo negro donde el resto de la
     * pantalla es del color del tema; ahora se borra con el fondo del tema. */
    gfx2_render(0, (int16_t)SPEC_Y, (int16_t)MAIN_W,
                (int16_t)((RTTY_TEXT_PANEL_Y + RTTY_TEXT_PANEL_H) - SPEC_Y),
                digi_borra, 0);
}

/*
 * 1 when the RTTY tuning scope should be showing INSTEAD of the
 * normal RF spectrum: actually in USB or LSB (the only modes RTTY/
 * rtty_scope make sense in) AND the RTTY decoder itself switched on -
 * i.e. currently in one of the RTTY-L/RTTY-U modes (see
 * k_demod_modes[]/menu_mode_preset_callback()), not plain USB/LSB.
 * Checked fresh every frame rather than cached - cheap (two reads),
 * and means flipping RTTY on/off or changing mode swaps the panel
 * back and forth automatically, no extra plumbing needed.
 */
static uint8_t digi_panel_active(void)
{
    demod_mode_t m = demod_am_get_mode();
    return (uint8_t)((m == DEMOD_MODE_USB || m == DEMOD_MODE_LSB) &&
                     (rtty_get_enabled() || cw_get_enabled()));
}

/*
 * 1 cuando el MANDO deja de sintonizar y pasa a mover las dos
 * frecuencias del RTTY. Solo en RTTY.
 *
 * Esto es una pregunta DISTINTA de digi_panel_active(), aunque durante
 * meses la respuesta fuera la misma y una sola funcion sirviera para las
 * dos. Al llegar el CW dejaron de coincidir, y usar la misma dejo el
 * mando muerto en CW: se lo llevaba la rama del RTTY a mover un
 * mark/space que en CW no pinta nada, y encima se comia la pulsacion,
 * asi que tampoco habia forma de salir girando.
 *
 * Y no coinciden por una razon de uso, no por un detalle de
 * implementacion. En RTTY las dos frecuencias son una convencion fija y
 * lo que se ajusta al vuelo es el detector, asi que el mando se dedica a
 * eso. En CW el tono es una preferencia personal que se pone una vez
 * -esta en Ajustes, con su pantalla y sus botones- y lo que se mueve
 * todo el rato es la frecuencia: sintonizar CW es correr la emisora
 * hasta que el pitido cae en el tono. O sea que en CW el mando tiene que
 * sintonizar, que es justo lo contrario.
 *
 * Que la respuesta de dos preguntas coincida hoy no las convierte en la
 * misma pregunta. Este fallo es de esa familia.
 */
static uint8_t rtty_encoder_grabs_tuning(void)
{
    demod_mode_t m = demod_am_get_mode();
    return (uint8_t)((m == DEMOD_MODE_USB || m == DEMOD_MODE_LSB) && rtty_get_enabled());
}

/*
 * Draws the RTTY tuning scope trace into the TOP RTTY_SCOPE_TRACE_H px
 * of the normal spectrum panel's area (SPEC_Y/MAIN_W) - see
 * digi_panel_active() for when this replaces the normal spectrum
 * instead of sdr_spectrum_waterfall_tick(), and this block's own
 * comment (right above RTTY_TEXT_PANEL_H) for how that height and the
 * text panel below it split the combined SPEC_Y..(WF_PANEL_Y+
 * WATERFALL_ROWS+4) region between them. Trace itself via
 * spectrum_draw() - REUSED from the normal RF spectrum (08/08/2026,
 * per the project owner) rather than this module's own original
 * hand-rolled green bar plot. spectrum_draw() is genuinely decoupled
 * from its usual RF/I-Q data source (see spectrum.h: it just takes a
 * dB array + a rectangle), so it picks up whichever style (HEATMAP/
 * LINE/OUTLINE, spectrum_set_style()) and line-smooth setting is
 * already active for the normal spectrum, automatically - no separate
 * style state for this panel.
 *
 * *** WHY A SEPARATE FFT ENGINE STILL EXISTS (rtty_scope.c), EVEN
 * THOUGH THE RENDERER IS SHARED *** - see rtty_scope.h's own comment:
 * reusing spectrum_draw() only reuses the RENDERING. The actual FFT
 * resolution problem that motivated a dedicated 512-point real FFT
 * (23.4Hz/bin @ 12kHz vs the RF spectrum's fixed 256-point/46.9Hz-bin
 * @ its own 8X zoom - insufficient to separate a 170Hz RTTY shift,
 * confirmed by hand before this was built) is completely unrelated to
 * which function paints the pixels - fft.c's FFT_SIZE is a
 * compile-time constant shared by the real-time RF spectrum/waterfall
 * path, not something this panel can safely resize without touching
 * that shared, performance-critical engine. Shrinking RTTY_SCOPE_
 * TRACE_H (see this block's top comment) only affects the trace's
 * on-screen PIXEL HEIGHT (amplitude axis) - it has no bearing on this
 * frequency-axis resolution at all.
 *
 * Linear magnitude -> dB conversion: rtty_scope_get_frame() returns
 * power normalized to the frame's own peak (0..1, see its comment) -
 * converted here to dB relative to that peak (0dB at the peak,
 * clamped to a DB_FLOOR below), matching the normal spectrum's own
 * dB-based convention and giving spectrum_draw()'s log-scaled
 * rendering something meaningful to compress - a raw linear 0..1
 * plot would look overly spiky (peak dominant, everything else
 * nearly invisible) compared to how any of HEATMAP/LINE/OUTLINE
 * actually expect to be fed. Uses the same IEEE754 bit-trick log2
 * approximation as the S-meter (smeter_log2_approx()) instead of
 * libm's log10f, for the same reason: this project links without
 * syscall stubs, and log10f drags in __errno.
 *
 * Two vertical marker lines at the LIVE rtty_get_mark_hz()/
 * rtty_get_space_hz() (cyan/orange) - not the config.h defaults
 * directly, since those are now just the STARTING point (see
 * rtty_set_mark_space_hz()'s comment: the encoder nudges these live
 * while the scope is showing). Drawn AFTER spectrum_draw() every
 * frame, unconditionally - no more separate "erase the old line
 * position" bookkeeping needed (that used to matter when this used a
 * diff-based bar renderer that only touched CHANGED columns;
 * spectrum_draw() repaints every column every call, so any previous
 * line position gets overwritten as a side effect automatically).
 *
 * Text panel drawn LAST, via rtty_text_panel_draw() - separated out
 * (10/08/2026) from this function's own body since it now has its own
 * dirty-flag gating (see s_rtty_text_dirty's comment) rather than the
 * old ticker's unconditional every-frame repaint.
 */
/* Boton de borrar el texto, arriba a la izquierda del panel digital.
 * 96x26 px son 11,2 x 3,0 mm: estrecho de alto para lo que pide un dedo
 * en resistivo, pero es una accion que se hace de vez en cuando y que no
 * duele si hay que repetir el toque - al reves que las de la barra, que
 * se usan a cada rato. */
/* Las cuatro constantes del boton de borrar subieron aqui arriba con la
 * etapa 20: digi_sync() las necesita y esta por delante. */

#define RTTY_SCOPE_DB_FLOOR -60.0f
static void rtty_scope_draw(void)
{
    static float s_db[RTTY_SCOPE_BINS];
    const float *mag;
    float hz_per_bin = rtty_scope_hz_per_bin();
    float nyquist_hz = hz_per_bin * (float)RTTY_SCOPE_BINS;
    uint16_t bar_y = SPEC_Y;
    uint16_t bar_area_h = RTTY_SCOPE_TRACE_H;
    uint32_t i;
    uint16_t mark_x, space_x;

    if (!rtty_scope_frame_ready()) {
        return;
    }
    mag = rtty_scope_get_frame();

    for (i = 0; i < RTTY_SCOPE_BINS; i++) {
        /* 10*log10(x) = 10*log2(x)/log2(10) = 10*log2(x)*0.30103.
         * Clamp the input away from 0 first (smeter_log2_approx()'s
         * bit-trick needs x>0), same floor-then-log shape as
         * smeter_segments_from_peak() uses. */
        float p = mag[i];
        if (p < 1.0e-6f) { p = 1.0e-6f; }
        s_db[i] = 10.0f * smeter_log2_approx(p) * 0.30103f;
        if (s_db[i] < RTTY_SCOPE_DB_FLOOR) { s_db[i] = RTTY_SCOPE_DB_FLOOR; }
    }

    spectrum_draw(s_db, RTTY_SCOPE_BINS, 0, bar_y, MAIN_W, bar_area_h,
                  RTTY_SCOPE_DB_FLOOR, 0.0f,
                  0,        /* center_mark_offset_px - no meaningful "LO" in audio-domain, dead center is fine/harmless */
                  0, 0, 0); /* band_active off - mark/space already have their own dedicated lines below */

    /* En CW hay UNA frecuencia que mirar, no dos: el tono al que escucha
     * el detector. Sintonizar es mover el pico hasta esa raya, que es
     * exactamente el mismo gesto que en RTTY con sus dos. */
    if (cw_get_enabled()) {
        /*
         * DOS rayas, y la que importa es la de escuchar.
         *
         * La cian marca donde el decodificador esta escuchando DE VERDAD,
         * o sea la sonda a la que se ha enganchado. La gris marca el tono
         * ajustado, que es solo el centro de la busqueda.
         *
         * Antes habia una sola raya, en el tono ajustado, y era una
         * mentira util: daba a entender que habia que llevar el pico
         * hasta ahi, cuando lo que hace falta es acercarlo a la ventana.
         * Con las dos se ve de un vistazo que el decodificador ya ha
         * cuadrado solo, y cuanto habria que mover el mando para que
         * ademas el pitido suene al tono preferido.
         */
        uint16_t nom_x = (uint16_t)((cw_get_pitch_hz() / nyquist_hz) * (float)MAIN_W);
        mark_x = (uint16_t)((cw_get_detect_hz() / nyquist_hz) * (float)MAIN_W);
        if (nom_x  < MAIN_W && nom_x != mark_x) {
            gfx_vline(nom_x, bar_y, bar_area_h, color_tema(PAL_INK_MUTE));
        }
        if (mark_x < MAIN_W) { gfx_vline(mark_x, bar_y, bar_area_h, color_tema(PAL_ACCENT)); }
    } else {
        mark_x  = (uint16_t)((rtty_get_mark_hz()  / nyquist_hz) * (float)MAIN_W);
        space_x = (uint16_t)((rtty_get_space_hz() / nyquist_hz) * (float)MAIN_W);
        if (mark_x < MAIN_W)  { gfx_vline(mark_x,  bar_y, bar_area_h, color_tema(PAL_ACCENT)); }
        if (space_x < MAIN_W) { gfx_vline(space_x, bar_y, bar_area_h, color_tema(PAL_WARN)); }
    }

    /*
     * En CW, la velocidad medida y si esta enganchado o no, arriba a la
     * derecha del osciloscopio.
     *
     * Esto no es adorno. Un decodificador de CW puede estar sacando
     * letras perfectamente plausibles a partir de ruido, y desde fuera
     * no se distingue de uno que esta leyendo de verdad. Los dos datos
     * que lo dicen son estos: si la velocidad se queda quieta en un
     * numero razonable, esta leyendo; si baila, no. Y el punto verde se
     * enciende cuando lo que llega tiene forma de Morse, que es la misma
     * condicion que decide si sale texto o no.
     *
     * Se dibuja sobre el trazo, que spectrum_draw() acaba de repintar
     * entero, asi que no hace falta borrar lo de antes.
     */
    /*
     * Boton de BORRAR el texto decodificado.
     *
     * Vive aqui dentro y no en la barra de acciones de abajo por dos
     * razones. Una, que la barra esta llena y cuadrada al pixel: 2 + 6
     * botones de 126 + 5 huecos de 8 + 2 = 800 exactos, y meter un
     * septimo obliga a recalcularlo todo para un boton que solo sirve en
     * dos modos de nueve. Y dos, que aqui esta donde se usa: al lado del
     * texto que borra.
     *
     * Se dibuja DESPUES de la traza a proposito. spectrum_draw() repinta
     * todas las columnas en cada cuadro, asi que cualquier cosa pintada
     * antes desaparece; este orden es el mismo que ya seguia el
     * indicador de velocidad de justo debajo.
     */
    /*
     * NI el boton NI la chapa se repintan por frame - 22/09/2026, por el
     * dueno del proyecto: "los ppm y el boton borrar parpadean".
     *
     * Parpadeaban porque estaban ENCIMA del trazo y spectrum_draw() repinta
     * el trazo entero en cada cuadro: cada frame los borraba y volvian a
     * salir justo despues. Ahora viven en la cabecera del panel de texto,
     * fuera de lo que repinta el trazo, asi que basta con dibujarlos cuando
     * de verdad cambian: el boton al pulsarlo y al repintar el panel, y la
     * chapa solo cuando cambia lo que dice.
     */
    digi_sync();
    {
        static char s_chip_visto[16] = "";
        static uint8_t s_eng_visto = 0xFFU;
        if (s_digi.chip &&
            (strcmp(s_chip_visto, s_digi.chip) != 0 || s_eng_visto != s_digi.enganchado)) {
            uint8_t i;
            for (i = 0U; i < sizeof s_chip_visto - 1U && s_digi.chip[i]; i++) {
                s_chip_visto[i] = s_digi.chip[i];
            }
            s_chip_visto[i] = '\0';
            s_eng_visto = s_digi.enganchado;
            ui_digi_draw_chip(&s_digi);
        }
    }

    rtty_text_panel_draw();
}


/*
 * DETENTES Y PULSACIONES INYECTADOS DESDE EL TACTIL
 * -------------------------------------------------
 * Los botones "-" y "+" de la pantalla de detalle no reimplementan el ajuste:
 * meten detentes en la misma cola que el mando y dejan que corra el codigo de
 * siempre. Asi el recorte de limites, el refresco del valor y el marcado para
 * guardar son literalmente los mismos, no una copia parecida que un dia se
 * queda atras. Vale para los ocho destinos sin escribir uno solo de ellos.
 */
static int32_t s_inject_detents = 0;
static uint8_t s_inject_press   = 0U;

static void encoder_inject_detents(int32_t d) { s_inject_detents += d; }
static void encoder_inject_press(void)        { s_inject_press = 1U; }

static void tune_encoder_poll(void)
{
    int32_t detents    = encoder_take_delta() + s_inject_detents;
    uint8_t press       = (uint8_t)(encoder_take_press() | s_inject_press);
    uint8_t long_press  = encoder_take_long_press();

    s_inject_detents = 0;
    s_inject_press   = 0U;
    uint8_t changed = 0;

    /*
     * APRETAR Y GIRAR = MOVER EL DIGITO DE SINTONIA
     *
     * Recuperado del firmware de serie, que lo tenia y este no: estando por
     * ejemplo en 7.124 kHz con paso de 1 kHz, el mando mueve el 4; si se
     * aprieta el mando y se gira, el foco salta al 2, y entonces el mando
     * mueve el 2 sin tocar el 4.
     *
     * Lo que cambia por dentro es el indice de paso (k_tune_steps), que es
     * exactamente "que digito mueve el mando" - la cabecera ya subraya ese
     * digito desde la etapa 2, asi que el gesto se ve en el numero grande
     * mientras se hace, que es lo que lo hacia util en el original.
     *
     * Va lo PRIMERO, antes incluso de la pulsacion larga, y hace return: si
     * el boton esta apretado y ademas ha llegado un detente, el usuario esta
     * haciendo este gesto y ninguna otra interpretacion es razonable. La
     * llamada a encoder_consume_press() evita que la suelta posterior
     * dispare ademas la pulsacion corta (que en TUNE significa justo
     * cambiar el paso, asi que sin esto el gesto se pasaria de rosca en uno
     * al levantar el dedo).
     *
     * Solo en TUNE. En los demas destinos del mando el boton ya significa
     * otra cosa en cada uno (ver las ramas de abajo), y anadir un gesto
     * global encima seria justo el tipo de cosa que hace que un mando deje
     * de ser predecible.
     */
    if (detents != 0 && encoder_button_down() &&
        s_encoder_target == ENCODER_TARGET_TUNE) {
        int32_t idx = (int32_t)s_tune_step_idx + detents;

        encoder_consume_press();
        /* Sin envolver: llegar al extremo y seguir girando se queda ahi, en
         * vez de saltar del paso mas fino al mas grueso de golpe - un salto
         * de 100 Hz a 1 MHz por un detente de mas es de los errores que
         * cuesta deshacer. */
        if (idx < 0) { idx = 0; }
        if (idx > (int32_t)TUNE_STEP_COUNT - 1) { idx = (int32_t)TUNE_STEP_COUNT - 1; }
        if ((uint8_t)idx != s_tune_step_idx) {
            set_tune_step_idx((uint8_t)idx);
            if (!s_menu_open) { step_display_draw(); }
        }
        return;
    }

    /*
     * While the RTTY scope is showing AND the settings menu is
     * closed, the encoder temporarily nudges BOTH mark AND space
     * together (CONFIG_RTTY_ENCODER_STEP_HZ per detent, preserving
     * the shift between them) instead of tuning the VFO - see
     * rtty_set_mark_space_hz()'s comment for why this needs to be a
     * LIVE, no-recompile adjustment. Checked first, before touching
     * s_encoder_target/press/long_press below, and returns
     * immediately - same "intercept before the normal target logic"
     * shape as the long-press handler right after this block. Button
     * press/long-press are silently swallowed while in this mode (no
     * tune-step-cycle, no "back to TUNE" gesture) - fine while RTTY
     * doesn't have its own detail-view controls yet, not worth the
     * extra complexity of wiring those here too.
     *
     * *** !s_menu_open added 08/08/2026 *** - without it, the encoder
     * kept nudging mark/space even while MENU/MODE was open, hijacking
     * it away from whatever the open menu screen actually expects
     * (tile navigation, a detail-view value, etc.) - same class of bug
     * as rtty_scope_draw() not checking s_menu_open, just on the input
     * side instead of the display side.
     */
    if (rtty_encoder_grabs_tuning() && !s_menu_open) {
        if (detents != 0) {
            float step = (float)detents * CONFIG_RTTY_ENCODER_STEP_HZ;
            float mark  = rtty_get_mark_hz()  + step;
            float space = rtty_get_space_hz() + step;

            /* Clamp to a sane positive range within the scope's own
             * 0..6kHz (Nyquist) display - besides being physically
             * meaningless outside that band, a negative Hz value
             * would also misbehave cast to uint32_t for the debug
             * prints below. */
            if (mark  < 0.0f) { mark  = 0.0f; }
            if (space < 0.0f) { space = 0.0f; }
            if (mark  > 6000.0f) { mark  = 6000.0f; }
            if (space > 6000.0f) { space = 6000.0f; }

            rtty_set_mark_space_hz(mark, space);
            debug_print_dec("rtty: mark Hz now", (uint32_t)mark);
            debug_print_dec("rtty: space Hz now", (uint32_t)space);
        }
        return;
    }

    /*
     * Pantallas de rejilla (ajustes, bandas, modos, pasos): el mando tambien.
     * Girar mueve el recuadro de seleccion por las celdas -saltando de pagina
     * al llegar al final, asi que se recorren las 39 bandas o los 27 ajustes
     * de un tiron- y pulsar entra en la que este señalada. El dedo y el mando
     * hacen lo mismo, que es el requisito.
     *
     * Va antes que la pulsacion larga porque esta se queda como esta: larga
     * = salir, desde cualquier pantalla.
     */
    if (s_menu_cfg_active && !s_menu_detail_active && !long_press) {
        if (detents != 0) { cfg_cursor_move(detents); }
        if (press)        { cfg_apply((uint8_t)s_cfg_cursor); }
        if (detents != 0 || press) { return; }
    }

    if (grid_activa() && !long_press) {
        if (detents != 0) {
            grid_cursor_move(detents);
        }
        if (press) {
            grid_apply((uint8_t)s_grid_cursor);
        }
        if (detents != 0 || press) {
            return;
        }
    }

    /* Long-press: unconditionally hands the knob back to TUNE and, if
     * the settings menu is open, closes it too - one gesture that
     * always gets you back to "turning the knob retunes the radio",
     * from anywhere. Added 01/08/2026 to fix a real bug: closing the
     * menu via the EXIT tile (menu_screen_close()) never reset
     * s_encoder_target, so after e.g. picking SCALE from the menu and
     * exiting, the knob kept adjusting the dB scale instead of the
     * frequency, with no way back short of reopening the menu and
     * picking VOL/etc to bounce through. A SHORT press couldn't be
     * reused for this - it already means something different in
     * every non-TUNE target (cycles the tune step, or toggles SCALE's
     * LO/HI bound - see the per-target branches below), so this
     * needed a gesture none of them use. Checked first, before
     * touching detents/press against whatever the OLD target was, and
     * returns immediately - a long-press is a deliberate "get me out"
     * and shouldn't also be evaluated as a turn against a target
     * we're leaving in the same poll. */
    if (long_press) {
        if (s_encoder_target != ENCODER_TARGET_TUNE) {
            s_encoder_target = ENCODER_TARGET_TUNE;
            debug_print("encoder: long-press - knob back to TUNE\n");
            aux_row_display_draw(); /* clear the stale LO/HI/SQL/BL/... row - always visible, menu or not, see s_btn_vol's callback for the same unconditional call */
        }
        if (s_menu_open) {
            menu_screen_close();
        }
        return;
    }

    if (s_encoder_target == ENCODER_TARGET_VOLUME) {
        uint8_t volume_activity = 0U;

        /* Volume mode: the encoder button still cycles the tune step
         * (ready for when you flip back) - but only draw it if the
         * main screen is actually showing; STEP's position doesn't
         * exist on the menu detail view (see s_menu_open's checks
         * throughout this function). */
        if (press) {
            set_tune_step_idx((uint8_t)((s_tune_step_idx + 1U) % TUNE_STEP_COUNT));
            if (!s_menu_open) { step_display_draw(); }
            volume_activity = 1U;
        }

        if (detents != 0) {
            int32_t v = (int32_t)s_volume_db_x2 + detents * VOLUME_STEP_X2;

            if (v < VOLUME_MIN_X2) { v = VOLUME_MIN_X2; }
            if (v > VOLUME_MAX_X2) { v = VOLUME_MAX_X2; }

            if ((int16_t)v != s_volume_db_x2) {
                set_volume_db_x2((int16_t)v);
                settings_value_redraw();
            }
            volume_activity = 1U;
        }

        /* Auto-timeout (26/08/2026) - see s_volume_target_last_ms's
         * comment: VOL is a bottom-bar toggle, not a menu detail with
         * its own EXIT, so without this the encoder kept adjusting
         * volume indefinitely. Any real activity this poll (press OR
         * detents, checked above) refreshes the timestamp instead of
         * tripping the timeout on the very poll that just used it.
         * !s_menu_open excludes VOLUME reached via its own menu tile
         * (menu_detail_show(ENCODER_TARGET_VOLUME)) - that path ends
         * via EXIT (menu_screen_close()) same as PGA/SCALE/etc, not a
         * timeout. */
        if (volume_activity) {
            s_volume_target_last_ms = g_msticks;
        } else if (!s_menu_open && ((g_msticks - s_volume_target_last_ms) >= VOLUME_TARGET_TIMEOUT_MS)) {
            s_encoder_target = ENCODER_TARGET_TUNE;
            debug_print("vol: inactivity timeout - encoder back to TUNE\n");
            aux_row_display_draw();
        }
        return;
    }

    if (s_encoder_target == ENCODER_TARGET_PGA) {
        /* Same "button still cycles tune step" courtesy as VOLUME
         * above. */
        if (press) {
            set_tune_step_idx((uint8_t)((s_tune_step_idx + 1U) % TUNE_STEP_COUNT));
            if (!s_menu_open) { step_display_draw(); }
        }

        if (detents != 0) {
            int32_t v = (int32_t)s_pga_gain_db_x2 + detents * PGA_STEP_X2;

            if (v < PGA_MIN_X2) { v = PGA_MIN_X2; }
            if (v > PGA_MAX_X2) { v = PGA_MAX_X2; }

            if ((int16_t)v != s_pga_gain_db_x2) {
                s_pga_gain_db_x2 = (int16_t)v;
                /* rf_agc_apply_pga() (07/08/2026), not a direct
                 * aic3204_set_pga_gain_db() call - the encoder now
                 * moves the CEILING, and this recomputes/applies the
                 * effective gain (ceiling - any active RF-AGC
                 * backoff) instead of overwriting the codec with the
                 * raw ceiling and silently undoing an active backoff -
                 * see s_rf_agc_enabled's declaration comment. */
                rf_agc_apply_pga();
                settings_value_redraw();
                if (s_settings_ready_for_autosave) { settings_mark_dirty(); } /* 07/09/2026 - was missing, so PGA changes never actually reached CONFIG.CSV, see settings.h's comment */
            }
        }
        return;
    }

    if (s_encoder_target == ENCODER_TARGET_CW_TONE) {
        /* Misma cortesia que el resto: el boton sigue cambiando el paso
         * de sintonia aunque el giro este dedicado a otra cosa. */
        if (press) {
            set_tune_step_idx((uint8_t)((s_tune_step_idx + 1U) % TUNE_STEP_COUNT));
            if (!s_menu_open) { step_display_draw(); }
        }

        if (detents != 0) {
            /* 10 Hz por detente. El recorte de limites vive dentro de
             * cw_set_pitch_hz(), no aqui: un rango repetido en dos sitios
             * es un rango que un dia deja de coincidir. */
            cw_set_pitch_hz(cw_get_pitch_hz() + (float)detents * 10.0f);
            /* El filtro de audio va detras del tono: si no, se quedaria
             * centrado donde estaba y el pitido se saldria del filtro
             * justo al intentar colocarlo. */
            demod_am_set_cw_filter_hz(cw_get_pitch_hz());
            settings_value_redraw();
        }
        return;
    }

    if (s_encoder_target == ENCODER_TARGET_RTTY_SHIFT) {
        /* Same "button still cycles tune step" courtesy as PGA/VOLUME
         * above. */
        if (press) {
            set_tune_step_idx((uint8_t)((s_tune_step_idx + 1U) % TUNE_STEP_COUNT));
            if (!s_menu_open) { step_display_draw(); }
        }

        if (detents != 0) {
            float v = rtty_get_shift_hz() + (float)detents * CONFIG_RTTY_SHIFT_STEP_HZ;

            if (v < CONFIG_RTTY_SHIFT_MIN_HZ) { v = CONFIG_RTTY_SHIFT_MIN_HZ; }
            if (v > CONFIG_RTTY_SHIFT_MAX_HZ) { v = CONFIG_RTTY_SHIFT_MAX_HZ; }

            rtty_set_shift_hz(v);
            settings_value_redraw();
        }
        return;
    }

    if (s_encoder_target == ENCODER_TARGET_BACKLIGHT) {

        /* Same "button still cycles tune step" courtesy as VOLUME
         * mode above. */
        if (press) {
            set_tune_step_idx((uint8_t)((s_tune_step_idx + 1U) % TUNE_STEP_COUNT));
            if (!s_menu_open) { step_display_draw(); }
        }

        if (detents != 0) {
            int32_t pct = (int32_t)backlight_get_percent() + detents * (int32_t)BACKLIGHT_STEP;

            if (pct < 20)   { pct = 20; }
            if (pct > 100) { pct = 100; }

            if ((uint8_t)pct != backlight_get_percent()) {
                backlight_set_percent((uint8_t)pct);
                settings_value_redraw();
                if (s_settings_ready_for_autosave) { settings_mark_dirty(); } /* 07/09/2026 - was missing, see settings.h's comment */
            }
        }
        return;
    }

    if (s_encoder_target == ENCODER_TARGET_SCALE) {

        /* Here the encoder BUTTON does NOT cycle the tune step like
         * the other two non-TUNE targets - it toggles which bound
         * (db_min/"LO" vs db_max/"HI") the knob's rotation moves. See
         * s_scale_adjust_max's comment. */
        if (press) {
            s_scale_adjust_max = (uint8_t)(s_scale_adjust_max ? 0U : 1U);
            settings_value_redraw();
        }

        if (detents != 0) {
            float step = (float)detents * SPECTRUM_DB_STEP;

            /* Manual adjustment always wins - turning the knob means
             * "I'm taking over," never "fight the auto-tracker". See
             * s_spec_agc_enabled's declaration comment. */
            if (s_spec_agc_enabled) {
                s_spec_agc_enabled = 0U;
                debug_print("spectrum AGC: off (manual SCALE adjustment)\n");
            }

            if (s_scale_adjust_max) {
                float v = s_db_max + step;

                if (v > SPECTRUM_DB_CEIL) { v = SPECTRUM_DB_CEIL; }
                /* Don't let HI get pulled down past LO+GAP - clamp
                 * against the OTHER bound, not just the ceiling. */
                if (v < s_db_min + SPECTRUM_DB_MIN_GAP) { v = s_db_min + SPECTRUM_DB_MIN_GAP; }
                if (v != s_db_max) {
                    s_db_max = v;
                    settings_value_redraw();
                }
            } else {
                float v = s_db_min + step;

                if (v < SPECTRUM_DB_FLOOR) { v = SPECTRUM_DB_FLOOR; }
                if (v > s_db_max - SPECTRUM_DB_MIN_GAP) { v = s_db_max - SPECTRUM_DB_MIN_GAP; }
                if (v != s_db_min) {
                    s_db_min = v;
                    settings_value_redraw();
                }
            }
        }
        return;
    }

    if (s_encoder_target == ENCODER_TARGET_SQUELCH) {
        

        /* Button just cycles the tune step, same courtesy as VOLUME/
         * BACKLIGHT - no second sub-value to toggle here. */
        if (press) {
            set_tune_step_idx((uint8_t)((s_tune_step_idx + 1U) % TUNE_STEP_COUNT));
            if (!s_menu_open) { step_display_draw(); }
        }

        if (detents != 0) {
            float v = demod_am_get_squelch_db() + (float)detents * SQUELCH_DB_STEP;

            if (v < SQUELCH_DB_FLOOR) { v = SQUELCH_DB_FLOOR; }
            if (v > SQUELCH_DB_CEIL)  { v = SQUELCH_DB_CEIL; }
            if (v != demod_am_get_squelch_db()) {
                demod_am_set_squelch_db(v);
                settings_value_redraw();
            }
        }
        return;
    }

    if (s_encoder_target == ENCODER_TARGET_SMOOTH) {

        /* Same "button still cycles tune step" courtesy as VOLUME/
         * BACKLIGHT/SQUELCH above. */
        if (press) {
            set_tune_step_idx((uint8_t)((s_tune_step_idx + 1U) % TUNE_STEP_COUNT));
            if (!s_menu_open) { step_display_draw(); }
        }

        if (detents != 0) {
            float v = s_spectrum_smooth_alpha + (float)detents * SPECTRUM_SMOOTH_STEP;

            if (v < SPECTRUM_SMOOTH_MIN) { v = SPECTRUM_SMOOTH_MIN; }
            if (v > SPECTRUM_SMOOTH_MAX) { v = SPECTRUM_SMOOTH_MAX; }
            if (v != s_spectrum_smooth_alpha) {
                s_spectrum_smooth_alpha = v;
                settings_value_redraw();
                if (s_settings_ready_for_autosave) { settings_mark_dirty(); } /* 07/09/2026 - was missing, see settings.h's comment */
            }
        }
        return;
    }

    if (s_encoder_target == ENCODER_TARGET_NR) {

        /* Same "button still cycles tune step" courtesy as every other
         * target above. */
        if (press) {
            set_tune_step_idx((uint8_t)((s_tune_step_idx + 1U) % TUNE_STEP_COUNT));
            if (!s_menu_open) { step_display_draw(); }
        }

        if (detents != 0) {
            int32_t v = (int32_t)s_nr_strength + detents * (int32_t)NR_STRENGTH_STEP;

            if (v < 0) { v = 0; }
            if (v > (int32_t)NR_STRENGTH_MAX) { v = (int32_t)NR_STRENGTH_MAX; }
            if ((uint16_t)v != s_nr_strength) {
                s_nr_strength = (uint16_t)v;
                nr_ss_set_strength(s_nr_strength);
                settings_value_redraw();
            }
        }
        return;
    }

    if (press) {
        set_tune_step_idx((uint8_t)((s_tune_step_idx + 1U) % TUNE_STEP_COUNT));
        debug_print_dec("tune: step now Hz", k_tune_steps[s_tune_step_idx]);
        step_display_draw();
    }

    if (detents != 0) {
        int64_t f = tune_mueve_pasos((int64_t)s_tune_hz, detents,
                                     (int64_t)k_tune_steps[s_tune_step_idx]);

        if (f < (int64_t)TUNE_MIN_HZ) {
            f = (int64_t)TUNE_MIN_HZ;
        } else if (f > (int64_t)TUNE_MAX_HZ) {
            f = (int64_t)TUNE_MAX_HZ;
        }

        if ((uint32_t)f != s_tune_hz) {
            s_tune_hz = (uint32_t)f;
            apply_lo_tune(s_tune_hz);
            changed = 1;
        }
    }

    if (changed) {
        freq_display_draw(); /* top bar - never covered by the menu
                               * overlay (see menu_screen_close()'s
                               * comment), so this stays unconditional. */
        if (!s_menu_open) {
            /* spec_span_labels_draw() paints INSIDE the spectrum
             * panel, which the menu grid overlays (MENU_AREA starts
             * at SPEC_Y - see its #define above). Retuning while the
             * menu is open must still update s_tune_hz/the LO (above,
             * unconditional), but painting the label row here would
             * scribble frequency text over whatever menu tile/detail
             * view is currently showing. menu_screen_close() already
             * calls spec_span_labels_draw() once on the way out to
             * catch up on any change made while the menu masked it -
             * same "skip the paint, not the state change" pattern as
             * step_display_draw() above (see the !s_menu_open checks
             * throughout this function). */
            spec_span_labels_draw(); /* "esta escala tiene que variar
                                       * con la frecuencia" - see its
                                       * comment */
        }
    }
}

/*
 * Shared by the 6 bottom-bar buttons (fires on RELEASE, the actual
 * "click"):
 *   MODE - opens the mode picker list (menu_mode_list_show()) instead
 *           of cycling directly - added 01/08/2026, see its comment.
 *   VOL  - jumps the encoder target straight to VOLUME (or back to
 *           TUNE if already there) - see s_encoder_target's comment.
 *   STEP - opens the tune-step picker list (menu_step_list_show())
 *           instead of cycling directly - added 01/08/2026, see its
 *           comment alongside menu_mode_list_show()'s above. Pressing
 *           the encoder (not this button) still cycles the step the
 *           old way - see tune_encoder_poll()'s per-target branches.
 *   NR   - toggles Spectral Subtraction noise reduction on/off - see
 *           s_nr_on's comment and nr_ss.h. Restored to this real job
 *           03/08/2026 (was briefly repurposed to cycle the AGC
 *           profile from 31/07 while the actual NR DSP didn't exist
 *           yet).
 *   BANDS - opens the BANDS preset list (menu_bands_show()) directly,
 *           without detouring through the settings grid - repurposed
 *           02/08/2026 from the SPT spatial-line-smoothing shortcut
 *           (still reachable via its badge + the settings-menu grid
 *           tile - see s_spec_smooth_passes' comment).
 *   MENU - opens the settings menu screen (menu_screen_open()) - see
 *           s_menu_screen's declaration comment. Replaced the old
 *           encoder-target cycle 31/07/2026.
 */
static void demo_button_callback(void *widget, ui_event_t event, void *user_data)
{
    ui_button_t *btn = (ui_button_t *)widget;
    (void)btn;
    (void)user_data;

    if (event == UI_EVENT_RELEASE) {
        debug_print("button pressed: ");
        debug_print(btn->label);
        debug_print("\n");

        if (widget == &s_btn_vol) {
            s_encoder_target = (s_encoder_target == ENCODER_TARGET_VOLUME)
                                ? ENCODER_TARGET_TUNE : ENCODER_TARGET_VOLUME;
            if (s_encoder_target == ENCODER_TARGET_VOLUME) {
                s_volume_target_last_ms = g_msticks; /* starts the inactivity timeout - see s_volume_target_last_ms's comment */
            }
            debug_print((s_encoder_target == ENCODER_TARGET_VOLUME)
                        ? "vol: encoder now controls VOLUME\n"
                        : "vol: encoder now controls TUNE\n");
            aux_row_display_draw();
        } else if (widget == &s_btn_step) {
            /* Opens the picker list (menu_step_list_show()) instead of
             * cycling directly - see its comment. Applying a choice
             * there closes the menu itself; nothing else to do here. */
            /* Pestaña: si ya esta abierta, se vuelve a la principal. Ver
             * accion_pantalla(). */
            accion_pantalla(GRID_PASOS);
        } else if (widget == &s_btn_nr) {
            /* Restored to its real job 03/08/2026, now that Spectral
             * Subtraction NR actually exists (nr_ss.h) - the
             * "repurposed to cycle AGC" stopgap from 31/07/2026 is
             * gone (AGC has its own proper home now: the AGC tile in
             * the settings menu, plus s_btn_agc_profile - see
             * agc_profile_cycle()'s comment). Toggles s_nr_on and
             * mirrors it into nr_ss_set_enabled(), which demod_am.c
             * actually checks (see its NR INTEGRATION comment) - a
             * genuine master switch, independent of the strength
             * tile's value (RADIO page's NR tile - see
             * ENCODER_TARGET_NR), and independent of demod mode too
             * (harmless to leave ON while in WFM/NFM, same "pre-set
             * for later" philosophy as BW - demod_am.c's own mode
             * check is what actually gates whether it does anything). */
            s_nr_on = s_nr_on ? 0U : 1U;
            nr_ss_set_enabled(s_nr_on);
            debug_print(s_nr_on ? "NR: on\n" : "NR: off\n");
            badges_draw();
        } else if (widget == &s_btn_bands) {
            /* Repurposed 02/08/2026 from the SPT smoothing-cycle
             * shortcut (see s_spec_smooth_passes' comment) to open the
             * BANDS list (menu_bands_show()) directly from the main
             * screen, without detouring through the settings grid -
             * same direct-entry treatment s_btn_step/s_btn_mode
             * already got 01/08/2026. menu_bands_show() sets
             * s_menu_open itself (see its comment), so there's nothing
             * else to do here - applying a preset closes the menu on
             * its own too (menu_band_preset_callback()). */
            accion_pantalla(GRID_BANDAS);
        } else if (widget == &s_btn_menu) {
            /* Opens the settings menu screen (menu_screen_open()) -
             * replaces the old TUNE -> BACKLIGHT -> SCALE -> SQUELCH
             * -> TUNE cycle 31/07/2026: tapping the tile you actually
             * want directly is strictly better than stepping through
             * a cycle to find it, now that there's somewhere for those
             * tiles to live - see s_menu_screen's declaration comment.
             * s_encoder_target itself is untouched here - each tile's
             * own callback sets it (or leaves it alone, for AGC/SPT
             * which act immediately instead). */
            accion_pantalla(GRID_NADA); /* GRID_NADA = la de ajustes, ver accion_pantalla() */
        } else if (widget == &s_btn_mode) {
            /* Opens the picker list (menu_mode_list_show()) instead of
             * cycling directly - see its comment alongside
             * menu_step_list_show(). Applying a choice there closes
             * the menu itself; nothing else to do here. */
            accion_pantalla(GRID_MODO);
        }
    }
}

static void radio_screen_draw(void)
{
    uint8_t i;

    /* Esta funcion SI puede pintar la pantalla entera: es la que la
     * reconstruye. Se desarma la banda reservada para el borrado inicial y
     * se vuelve a armar al final, cuando la cabecera ya esta puesta. Ver
     * gfx_guard_top_set() en gfx.h. */
    gfx_guard_top_set(0);

    gfx_fill_screen(GFX_COLOR_BLACK);

    /* Top bar: freq/mode/step/vol/time/battery live here, drawn by
     * their own readout functions after the panels. */
    /* hidden=1: la cabecera la pinta ui_top.c entera. El panel se queda
     * registrado por su geometria, pero ya no rellena nada. */
    s_title_panel = (ui_panel_t){0, 0, GFX_SCREEN_WIDTH, TOP_H,
                                  GFX_COLOR_DARKGRAY, GFX_COLOR_DARKGRAY, 1};

    /* Main display column: spectrum over waterfall. */
    /* Sin borde (borde == fondo, ver ui_panel_draw): ese rectangulo gris de
     * 1 px lo pisan por la izquierda la canaleta de los ejes y por abajo la
     * regla, asi que en la placa no se veia un marco sino dos rayas sueltas.
     * Un marco a medias es peor que ninguno, y el diseno nuevo separa zonas
     * con fondo y aire, no con lineas. */
    s_spectrum_panel = (ui_panel_t){0, SPEC_Y, MAIN_W, SPEC_H,
                                     GFX_COLOR_BLACK, GFX_COLOR_BLACK};

    /* Sin borde, misma razon que s_spectrum_panel justo arriba. */
    s_waterfall_panel = (ui_panel_t){0, WF_PANEL_Y, MAIN_W,
                                      (uint16_t)(WATERFALL_ROWS + 4), GFX_COLOR_BLACK, GFX_COLOR_BLACK};

    /* Status strip (S-meter + SNR + badges drawn on top afterwards) -
     * replaces the old right-hand column panel (s_rcol_panel, removed
     * 01/09/2026) - see STATUS_STRIP_Y/H's declaration comment. */
    /* hidden=1, misma razon que s_title_panel: la barra de estado la pinta
     * ui_top_draw_status(). */
    s_status_strip_panel = (ui_panel_t){0, STATUS_STRIP_Y, MAIN_W, STATUS_STRIP_H,
                                         GFX_COLOR_BLACK, GFX_COLOR_GRAY, 1};

    /* Bottom bar: 6 buttons. enabled=1 set explicitly - if omitted, a
     * freshly declared ui_button_t defaults to enabled=0 (C zero-
     * initialization) and ui_screen_touch() would ignore it even
     * though it draws fine. MODE gets the yellow "primary" styling. */
    /*
     * ETAPA 4: los seis botones ya NO se registran en s_demo_screen.
     *
     * Siguen existiendo como ui_button_t porque demo_button_callback()
     * distingue cual le ha llamado comparando el puntero, y esa logica -que
     * es toda la funcionalidad de la barra- no se toca. Lo que cambia es
     * quien los dibuja (ui_act.c, con nombres y valores) y quien decide que
     * boton ha tocado el dedo (ui_act_hit(), que ademas reparte los huecos
     * entre botones vecinos en vez de dejarlos muertos).
     *
     * Registrarlos ADEMAS en la pantalla haria que cada toque disparase el
     * callback dos veces: una por ui_screen_touch() y otra por el reparto
     * nuevo. hidden=1 es cinturon y tirantes por si alguien los vuelve a
     * registrar sin darse cuenta.
     */
    for (i = 0; i < UI_ACT_N; i++) {
        /* Geometria a CERO, y no una copia de la de ui_act.h: estos widgets ya
         * no se dibujan ni se consultan para nada espacial. Repetir aqui las
         * coordenadas seria dejar una segunda fuente de verdad que nadie usa
         * y que se queda desfasada en silencio - que es justo lo que acaba de
         * pasar al mover la barra (estas lineas seguian con el margen viejo).
         * hidden=1 ademas garantiza que, si alguien los vuelve a registrar sin
         * darse cuenta, no pinten un rectangulo en la esquina. */
        *k_act_slots[i].btn = (ui_button_t){0, 0, 0, 0,
                                    k_act_slots[i].nombre,
                                    GFX_COLOR_WHITE, GFX_COLOR_DARKGRAY, GFX_COLOR_WHITE,
                                    2, 0, 1, demo_button_callback, NULL, 1};
    }

    /* 22/09/2026: aqui se rellenaban s_btn_agc_profile y s_btn_audio_bw, dos
     * widgets de ui.c que desde la etapa 4 ya no se registraban en ninguna
     * pantalla. Servian de caja para un puntero a funcion y una etiqueta que
     * nadie leia: los toques de esos dos chips los enruta ui_top_hit() y
     * llama a sus callbacks directamente (ver demo_touch_poll()). */



    /*
     * 22/09/2026: esto era ui_screen_draw(&s_demo_screen), que recorria una
     * lista de widgets registrados. De esa lista solo quedaban los cuatro
     * marcos de aqui abajo -los botones y las etiquetas se fueron con el
     * rediseno-, asi que se dibujan directamente y desaparece la maquinaria
     * de registro: dos estructuras de 132 bytes en RAM y la mitad de ui.c.
     */
    ui_panel_draw(&s_title_panel);
    ui_panel_draw(&s_spectrum_panel);
    ui_panel_draw(&s_waterfall_panel);
    ui_panel_draw(&s_status_strip_panel);

    /* Static labels drawn once, bypassing the screen (no touch, no
     * state): panadapter span labels. Span: 192kHz I/Q sampling ->
     * +/-96kHz around the LO, which sits on the center line of the
     * trace (the demod point marker may sit off-center - see the SR/4
     * low-IF notes).
     *
     * *** 01/09/2026: the old "SIGNAL" caption above the S-meter was
     * dropped, not repositioned *** - it lived in the old vertical
     * right-hand column, which had room for a label ABOVE the meter;
     * the new horizontal status strip is only STATUS_STRIP_H(40)px
     * tall and everything in it (meter/SNR/badges) is vertically
     * centered within that - there's no clean spot left for a
     * separate caption line without either uncentering something or
     * shrinking the strip further. The segmented bar reads as a
     * signal meter on its own, especially sitting right next to the
     * SNR readout - the caption wasn't carrying its weight anymore. */
    spec_chrome_full_draw(); /* ETAPA 3b: canaleta + regla + leyenda */

    /* Dynamic readouts, first paint. */
    freq_display_draw();
    mode_display_draw();
    step_display_draw();
    aux_row_display_draw();
    time_display_draw();
    battery_display_draw();
    smeter_draw(0);
    badges_draw();
    act_draw();   /* ETAPA 4: barra de acciones */

    /* A partir de aqui, y < SPEC_Y es de ui_top.c y de nadie mas. Cualquier
     * dibujo por gfx.c que caiga ahi se recorta en silencio en vez de dejar
     * un recuadro encima de la cabecera. */
    gfx_guard_top_set(SPEC_Y);
}

/*
 * Drag-to-tune on the spectrum panel - added 01/08/2026 per the
 * project owner: dragging a finger across the spectrum RIGHT lowers
 * the tuned frequency, LEFT raises it - the classic "pan the content"
 * feel (like dragging a map or photo: dragging right reveals what was
 * further left, i.e. LOWER frequency on this panadapter, since
 * frequency increases left-to-right - see spec_span_labels_draw()'s
 * tick ruler).
 *
 * QUANTIZED to whole steps rather than applied as continuous
 * fractional Hz - added same day, per the project owner: raw pixel-
 * proportional Hz produced odd non-round frequencies and, on this
 * still-uncalibrated resistive touch panel (see demo_touch_poll()'s
 * CALIBRATION NOTE), tiny single-pixel jitter was enough to wobble
 * the tuned frequency by a few Hz with no visible finger movement.
 * *hz_accum carries the sub-step remainder between calls - same carry
 * technique encoder_take_delta() already uses for quarter-steps, so a
 * slow drag that hasn't crossed a full step yet isn't lost, just
 * accumulated for next time. Reset by the caller (demo_touch_poll())
 * at the START of each new drag gesture - see its comment - so
 * leftover remainder from a previous unrelated drag never bleeds into
 * this one.
 *
 * *** 01/09/2026: step size now follows k_tune_steps[s_tune_step_idx]
 * (the SAME step the STEP button/encoder short-press already cycles),
 * not a fixed 1kHz *** - per the project owner: dragging should
 * respect whatever step you've already dialed in (100Hz through 1MHz)
 * rather than always snapping to 1kHz regardless. Read fresh on every
 * call (not cached), so changing STEP mid-drag takes effect
 * immediately on the next touch sample - and *hz_accum's leftover
 * remainder, if any, is simply interpreted against the new step size
 * next time, no special handling needed (worst case a slightly odd
 * first jump right after a step change, same as changing step size
 * always risks with any accumulator-based scheme).
 *
 * Hz-per-pixel (before quantizing) still comes from the current
 * zoom's span (see spec_span_labels_draw()'s switch for where these
 * same span numbers come from) divided across SPEC_TRACE_W, so how
 * much finger travel one step takes still scales with zoom - finer
 * control zoomed in, coarser zoomed out - only the OUTPUT is snapped
 * to round steps, not the sensitivity.
 *
 * Called once per touch SAMPLE while dragging (see demo_touch_poll()),
 * not once per gesture - dx_px is the delta since the LAST sample, not
 * since the press started, so the trace/frequency updates live as the
 * finger moves rather than jumping once on release.
 */

/*
 * spec_tap_tune_to_x() - added 01/09/2026, per the project owner:
 * "poder elegir visualmente una señal para sintonizarla" - a genuine
 * TAP (as opposed to a drag - see s_spec_drag_moved's own comment in
 * demo_touch_poll() for how the two are told apart) on the spectrum
 * panel tunes DIRECTLY to whatever frequency that pixel column
 * represents, rather than nudging the current tune by a relative
 * amount the way a drag does.
 *
 * Uses the EXACT SAME pixel<->Hz mapping spec_span_labels_draw()
 * already draws its tick labels with (just algebraically inverted:
 * that function goes Hz->px to place a label, this one goes px->Hz
 * to interpret a tap) - deliberately, so tapping precisely on a
 * labeled tick mark tunes to precisely that labeled frequency, no
 * separate/inconsistent formula to drift out of sync with what the
 * screen visibly shows. See that function's own comment for why
 * panel_center_hz sits where it does (the IF-offset correction at
 * zoom 1x) and why full_span_hz comes from spec_zoom_full_span_hz().
 *
 * Rounded to the NEAREST active tune step (07/09/2026, per the
 * project owner - "interesa que se redondeen al step activo... asi
 * 7.123.531 pasaria a ser 7.124 si el step esta a 1k") - this used to
 * be deliberately UNQUANTIZED (landing exactly on whatever pixel the
 * finger hit, on the reasoning that a real signal's peak has no
 * reason to fall on a round step), but that leaves ugly, hard-to-read
 * frequencies most of the time in practice, so a tap now lands on the
 * nearest multiple of k_tune_steps[s_tune_step_idx] instead - same
 * step the encoder and spec_drag_tune_apply()'s relative panning
 * already move in, so a tap and a subsequent encoder nudge stay on
 * the same frequency grid. TUNE_MIN_HZ/MAX_HZ still clamp the
 * (rounded) result, same as every other tuning path.
 */
/*
 * TOCAR UNA SEÑAL Y CAER ENCIMA - etapa 21, 22/09/2026.
 *
 * Por el dueno del proyecto: "me gustaria hacer clickable el espectro, que yo
 * vea una señal, le clicke y vaya a ella".
 *
 * Tocar el espectro ya sintonizaba desde hace tiempo, pero con dos cosas que
 * hacian que no se notara:
 *
 * 1. EL MAPA ESTABA MAL. Se usaba `x - MAIN_W/2` sobre un ancho de
 *    SPEC_TRACE_W, o sea que se daba por hecho que la traza ocupa los 800 px
 *    centrada en 400. No: empieza en SPC_TRACE_X = 40 y mide 756, asi que su
 *    centro esta en 418. Eran 18 px de desplazamiento fijo mas un error de
 *    escala - unos 2,3 kHz con el span de 96 kHz. Tocabas la señal y caias al
 *    lado, que es justo lo que hace pensar que la funcion no existe.
 *
 * 2. SE REDONDEABA AL PASO DE SINTONIA. Con el paso en 1 kHz, hasta 500 Hz
 *    mas de error. En AM da igual; en CW te deja fuera del filtro.
 *
 * Ahora el toque BUSCA la señal alrededor del dedo en los mismos datos que
 * estas viendo dibujados (spec_snap.c, probado en sim/snaptest.c) y:
 *   - si encuentra una, cae en su frecuencia exacta, sin redondear, porque
 *     redondear seria deshacer justo lo que se acaba de afinar;
 *   - si no encuentra nada, se comporta como siempre: donde apuntaste,
 *     redondeado al paso. Tocar un hueco vacio para moverse ahi sigue
 *     funcionando.
 *
 * Y cae donde hay que caer para OIRLA, que no es lo mismo que donde esta:
 *   - AM/SAM/NFM/WFM: encima de la portadora.
 *   - CW: el tono tiene que salir al pitch ajustado, asi que el VFO va un
 *     pitch por debajo (en USB) o por encima (en LSB) del tono.
 *   - USB/LSB: no hay portadora y el pico esta en MEDIO de la voz; se cae en
 *     el borde de la señal, que es donde se pone el VFO en banda lateral.
 */
/*
 * MOVER LA SINTONIA N PASOS, VOLVIENDO A LA REJILLA - 22/09/2026.
 *
 * Por el dueno del proyecto: "me ha dejado la freq en 7.112,03 y el 3 no soy
 * capaz de dejarlo en cero".
 *
 * Y no podia. Lo rompi yo en la etapa 21: al tocar una señal en el espectro
 * se cae en su frecuencia EXACTA, sin redondear -que es justo lo que se
 * queria-, y eso deja el VFO fuera de la rejilla del paso. Como el mando
 * hacia "frecuencia + paso" sin mas, 7.112,03 pasaba a 7.113,03 y de ahi a
 * 7.114,03: los 30 Hz de mas no se iban NUNCA por mucho que girases.
 *
 * Un mando de sintonia no suma el paso: va al SIGUIENTE MULTIPLO del paso.
 * Asi el primer clic desde una frecuencia rara cae en la rejilla y los
 * siguientes se quedan en ella, que es como se comporta cualquier radio.
 * Y no estorba a lo del espectro: ahi se sigue cayendo exacto, y en cuanto
 * tocas el mando vuelves a numeros redondos.
 *
 * Lo comprueba sim/rejilla.c con el caso que lo destapo.
 */
static int64_t tune_mueve_pasos(int64_t f, int32_t pasos, int64_t paso_hz)
{
    int64_t resto;

    if (paso_hz <= 0 || pasos == 0) { return f; }

    resto = f % paso_hz;
    if (resto < 0) { resto += paso_hz; }

    if (resto != 0) {
        /* Fuera de la rejilla: el primer clic la usa para volver a ella, no
         * para sumar un paso entero. Hacia arriba se sube al multiplo de
         * encima y hacia abajo al de debajo, asi que un clic siempre mueve
         * en el sentido que has girado. */
        f -= resto;
        if (pasos > 0) { f += paso_hz; pasos--; }
        else           { pasos++; }
    }
    return f + (int64_t)pasos * paso_hz;
}

static void spec_tap_tune_to_x(uint16_t x)
{
    uint32_t full_span_hz = spec_zoom_full_span_hz();
    uint32_t panel_center_hz = s_tune_hz;
    demod_mode_t modo = demod_am_get_mode();
    int32_t px;
    float bin, bin_snap;
    uint8_t enganchado = 0U;
    int64_t off_hz, f;
    int64_t step_hz = (int64_t)k_tune_steps[s_tune_step_idx];

    if (s_spec_zoom == SPEC_ZOOM_1X && demod_am_get_if_offset_active()) {
        panel_center_hz = s_tune_hz - demod_if_offset_hz();
    }

    /* Pixel DENTRO de la traza, no de la pantalla. */
    px = (int32_t)x - (int32_t)SPC_TRACE_X;
    if (px < 0) { px = 0; }
    if (px > (int32_t)SPC_TRACE_W - 1) { px = (int32_t)SPC_TRACE_W - 1; }

    /* Px -> bin, con el mismo reparto que usa spectrum_draw() para dibujar. */
    bin = ((float)px * (float)FFT_BINS_IQ) / (float)SPC_TRACE_W;

    if (s_db_frame_listo) {
        snap_modo_t sm = SNAP_PICO;
        /* En CW el tono es un pico, asi que se busca como tal aunque el modo
         * de fondo sea banda lateral. */
        if (!cw_get_enabled()) {
            if (modo == DEMOD_MODE_USB) { sm = SNAP_USB; }
            else if (modo == DEMOD_MODE_LSB) { sm = SNAP_LSB; }
        }
        bin_snap = spec_snap_bin(s_db_frame, FFT_BINS_IQ, bin, sm, &enganchado);
    } else {
        bin_snap = bin;
    }

    /* Bin -> Hz. El bin del centro es FFT_BINS_IQ/2 (el array va
     * fftshifteado, el VFO en el centro). */
    off_hz = (int64_t)(((double)bin_snap - (double)(FFT_BINS_IQ / 2U))
                       * (double)full_span_hz / (double)FFT_BINS_IQ);
    f = (int64_t)panel_center_hz + off_hz;

    if (enganchado && cw_get_enabled()) {
        /* Que el tono salga al pitch elegido en vez de a cero batido, que es
         * inaudible. En USB el audio es (RF - VFO), asi que el VFO va por
         * debajo; en LSB al reves. */
        int64_t pitch = (int64_t)(cw_get_pitch_hz() + 0.5f);
        f += (modo == DEMOD_MODE_LSB) ? pitch : -pitch;
    }

    if (!enganchado && step_hz > 0) {
        /* Sin señal debajo: como siempre, al multiplo mas cercano del paso.
         * Con señal NO se redondea - ver el comentario de arriba. */
        int64_t rem = f % step_hz;

        if (rem < 0) { rem += step_hz; }
        f -= rem;
        if (rem * 2 >= step_hz) { f += step_hz; }
    }

    if (f < (int64_t)TUNE_MIN_HZ) { f = (int64_t)TUNE_MIN_HZ; }
    if (f > (int64_t)TUNE_MAX_HZ) { f = (int64_t)TUNE_MAX_HZ; }

    if ((uint32_t)f != s_tune_hz) {
        s_tune_hz = (uint32_t)f;
        apply_lo_tune(s_tune_hz);
        freq_display_draw();
        spec_span_labels_draw();
        debug_print_dec(enganchado ? "espectro: enganchado a Hz"
                                   : "espectro: sin señal, Hz", s_tune_hz);
    }
}

static void spec_drag_tune_apply(uint16_t x, uint16_t prev_x, float *hz_accum)
{
    int32_t dx_px = (int32_t)x - (int32_t)prev_x;
    float hz_per_px;
    float step_hz;
    int32_t steps;
    int64_t f;

    if (dx_px == 0) {
        return; /* no horizontal movement since the last sample */
    }

    /* *** 01/09/2026: rate-aware via spec_zoom_full_span_hz() *** -
     * see that function's own comment for the full "why". */
    hz_per_px = (float)spec_zoom_full_span_hz() / (float)SPEC_TRACE_W;
    step_hz = (float)k_tune_steps[s_tune_step_idx];

    /* Drag right (dx_px > 0) -> frequency DOWN, so subtract - see this
     * function's comment for the "pan the content" reasoning. */
    *hz_accum -= (float)dx_px * hz_per_px;

    /* C99 truncates toward zero - exactly what we want for both signs
     * here, same reasoning as encoder_take_delta()'s comment. Usually
     * +/-1 for a normal drag speed; a fast flick between polls can
     * legitimately produce more in one call. */
    steps = (int32_t)(*hz_accum / step_hz);
    if (steps == 0) {
        return; /* hasn't crossed a full step yet - remainder stays in *hz_accum for next time */
    }
    *hz_accum -= (float)steps * step_hz;

    f = tune_mueve_pasos((int64_t)s_tune_hz, steps, step_hz);

    if (f < (int64_t)TUNE_MIN_HZ) { f = (int64_t)TUNE_MIN_HZ; }
    if (f > (int64_t)TUNE_MAX_HZ) { f = (int64_t)TUNE_MAX_HZ; }

    if ((uint32_t)f != s_tune_hz) {
        s_tune_hz = (uint32_t)f;
        apply_lo_tune(s_tune_hz);
        freq_display_draw();
        spec_span_labels_draw(); /* "esta escala tiene que variar con
                                   * la frecuencia" - see its comment */
    }
}

/*
 * Touch input hook, already active. touch_read() does the heavy
 * lifting (bit-banged SPI transactions + averaging) ONLY once
 * touch_is_pressed() has already returned true (a cheap plain GPIO
 * read), so calling it every main-loop iteration is not expensive
 * while there's no contact.
 *
 * CALIBRATION NOTE: without calling touch_set_calibration(), touch.c
 * uses an identity mapping (raw 0-4095 -> screen 0-800/0-480) that
 * almost certainly does not match the resistive panel's real
 * orientation/scale. It's enough to validate that the whole pipeline
 * works (that a touch reaches and presses a button), but a real
 * calibration - touching the 4 known corners, noting the raw_x/raw_y
 * from touch_debug_raw() at each, and filling in a real
 * touch_calibration_t via touch_set_calibration() - is still pending.
 * Drag-to-tune (spec_drag_tune_apply() above) inherits this same
 * caveat: it works on RELATIVE deltas between consecutive samples, so
 * it degrades more gracefully than absolute-position touches (a
 * button hit-test) under a wrong scale/swap/invert - the DIRECTION
 * could still come out backwards if invert_x is wrong, worth
 * double-checking once real calibration lands.
 */
static void demo_touch_poll(void)
{
    /*
     * REGION-based routing (31/07/2026) - see s_menu_screen's
     * declaration comment on why the menu now only covers MENU_AREA
     * (the spectrum+waterfall panel) instead of the whole screen: the
     * top bar, right column, and bottom bar stay live and touchable
     * the entire time, so touches inside MENU_AREA while the menu is
     * open go to s_menu_screen, everything else always goes to
     * s_demo_screen.
     *
     * BUG FIXED HERE (same day): the region check must be decided
     * ONCE, on the FIRST sample of a touch (the press), and then held
     * for the WHOLE gesture through release - NOT re-evaluated on
     * every sample. The original version re-checked the region on
     * every poll, including the release sample - and on this
     * hardware's resistive touch driver, the release sample often
     * reports spurious coordinates (e.g. x=0,y=0) that fail the
     * region check even though the press that started the gesture
     * passed it. That sent the RELEASE to the WRONG screen: whichever
     * screen got the press (say s_menu_screen, for a tap on EXIT)
     * never received its matching release, so its active_index stayed
     * stuck on that widget forever - and since ui_screen_touch() only
     * does a fresh hit-test when active_index is -1, EVERY subsequent
     * tap on that screen (any tile, including EXIT again) then hit
     * the "already have an active widget, just check if still inside"
     * branch instead, matched against the WRONG (stuck) widget, and
     * did nothing. That's the exact "opens, then nothing responds,
     * not even EXIT" symptom.
     *
     * s_touch_owner_is_menu now records which screen owns the CURRENT
     * gesture, decided only when a NEW press starts (s_touch_active
     * transitions 0->1), and reused unchanged - including for the
     * release itself - until the finger lifts.
     */
    static uint8_t s_touch_active = 0U;         /* 1 while a press-hold is in progress */
    static uint8_t s_touch_owner_is_menu = 0U;  /* which screen the CURRENT gesture belongs to */
    static uint8_t s_spec_drag_active = 0U;     /* 1 while the CURRENT gesture started inside the spectrum panel */
    static uint16_t s_spec_tap_start_x = 0U;    /* x of the PRESS that started this gesture - unlike s_spec_drag_prev_x, never updated mid-gesture, so release can measure total travel */
    static uint8_t s_spec_drag_moved = 0U;      /* 0 until total travel from s_spec_tap_start_x exceeds SPEC_TAP_MOVE_THRESHOLD_PX - see its own comment below */
    static uint16_t s_spec_drag_prev_x = 0U;    /* x of the last sample applied, for per-sample deltas */
    static float s_spec_drag_hz_accum = 0.0f;   /* sub-step carry - see spec_drag_tune_apply()'s comment */
    /* s_freq_tap_active: added 07/08/2026 alongside the frequency
     * keypad - a tap starting in the top-bar FREQ_TAP_X1/Y1 zone opens
     * menu_freq_keypad_show(). Decided once on press and honored
     * through the whole gesture (fires on release regardless of where
     * the finger ends up), same deliberate reasoning as
     * s_touch_owner_is_menu/s_spec_drag_active above - the release
     * sample's coordinates on this resistive panel can't be trusted.
     * Checked entirely outside the ui_screen framework (no ui_button_t
     * sits over the frequency readout - see freq_display_draw()'s
     * comment for why that wouldn't work cleanly), same treatment as
     * the spectrum drag zone just above it. */
    static uint8_t s_freq_tap_active = 0U;
    /* s_time_tap_active: same shape as s_freq_tap_active just above,
     * for the clock-setting keypad (08/09/2026) - see TIME_TAP_X1/Y2's
     * and menu_time_keypad_show()'s comments. */
    static uint8_t s_time_tap_active = 0U;
    /* s_borrar_tap: el boton de borrar el texto decodificado, misma
     * forma que las dos zonas de arriba. Ver CW_CLR_X y su dibujo en
     * rtty_scope_draw(). */
    static uint8_t s_borrar_tap = 0U;
    /* Chip de la barra de estado bajo el dedo desde la pulsacion, o
     * UI_TOP_HIT_NONE. Ver ui_top_hit() en ui_top.h. */
    static ui_top_hit_t s_chip_tap = UI_TOP_HIT_NONE;
    uint16_t x = 0, y = 0;
    uint8_t pressed = touch_read(&x, &y);

    if (pressed && !s_touch_active) {
        s_touch_active = 1U;
        s_touch_owner_is_menu = (uint8_t)(s_menu_open != 0U
            && x < MENU_AREA_W && y >= MENU_AREA_Y
            && y < (uint16_t)(MENU_AREA_Y + MENU_AREA_H));

        /* Drag-to-tune / tap-to-tune: a press starting inside the
         * spectrum panel while the menu ISN'T covering it - see
         * spec_drag_tune_apply()'s and spec_tap_tune_to_x()'s own
         * comments for the two behaviors this single press might turn
         * into. Mutually exclusive with s_touch_owner_is_menu by
         * construction: MENU_AREA exactly covers the spectrum+
         * waterfall, so whenever the menu is open and owns this press,
         * this stays 0. Decided once here, same "decide on the press,
         * hold for the whole gesture" reasoning as s_touch_owner_is_menu
         * above (and for the same reason: the release sample's
         * coordinates can be garbage). s_spec_drag_hz_accum/
         * s_spec_drag_moved both reset here too - a fresh gesture
         * starts clean, regardless of whatever a previous one left
         * behind. */
        /*
         * Borrar el texto decodificado. Se comprueba ANTES que el
         * arrastre del espectro y lo excluye, porque la caja del boton
         * cae dentro del panel y si no el arrastre se quedaria el gesto
         * y el boton no respondiria nunca. Mismo patron de "se decide en
         * la pulsacion y se respeta hasta la suelta" que las zonas de la
         * frecuencia y el reloj, y por el mismo motivo: en este panel
         * resistivo las coordenadas de la suelta no son de fiar.
         */
        /* La zona la da ui_digi.c, que es quien dibuja el boton, en vez de
         * repetir aqui su rectangulo: era la tercera copia de la misma
         * geometria y la que se quedaria vieja el dia que el boton se mueva. */
        s_borrar_tap = (uint8_t)(!s_touch_owner_is_menu && !s_menu_open
            && digi_panel_active()
            && ui_digi_boton_hit(&s_digi, x, y));
        if (s_borrar_tap) {
            /* Retorno visual en la PULSACION, como en el resto de zonas: en
             * un tactil que pide fuerza, un boton que no se hunde invita a
             * un segundo toque. */
            s_digi.btn_press = 1U;
            ui_digi_draw_boton(&s_digi);
        }

        s_spec_drag_active = (uint8_t)(!s_touch_owner_is_menu && !s_borrar_tap
            && x < MAIN_W && y >= SPEC_Y && y < (uint16_t)(SPEC_Y + SPEC_H));
        s_spec_drag_prev_x = x;
        s_spec_tap_start_x = x;
        s_spec_drag_hz_accum = 0.0f;
        s_spec_drag_moved = 0U;

        /* Frequency keypad tap zone - top bar only, so mutually
         * exclusive with both of the above by construction (MENU_AREA
         * and the spectrum panel both start at SPEC_Y, well below
         * FREQ_TAP_Y1/TOP_H). Allowed even while s_menu_open (opening
         * the keypad from on top of some OTHER menu screen just swaps
         * s_menu_screen's contents, same as tapping BANDS/STEP/MODE
         * already does from the bottom bar while the settings grid is
         * open) - only excluded while a gesture already claimed by
         * the menu or the spectrum drag, which can't happen here
         * anyway since this zone is geometrically disjoint from both. */
        /* 22/09/2026: la zona la decide ui_top.c, que es quien dibuja la
         * cabecera y por tanto quien sabe donde acaba la frecuencia. La
         * constante que habia aqui, FREQ_TAP_X1 = MODE_X = 396, era la X del
         * rotulo de modo en la cabecera de ANTES del rediseno; el chip de
         * modo de ahora va pegado al "kHz" y acaba bastante antes, asi que
         * quedaba una franja de barra vacia a su derecha que seguia abriendo
         * el teclado de frecuencia - y con los ajustes abiertos eso es una
         * pantalla que no se ha pedido. Ver ui_top_freq_hit(). */
        s_freq_tap_active = ui_top_freq_hit(x, y);

        /* Clock keypad tap zone (08/09/2026) - see TIME_TAP_X1/Y2's
         * own comment for why this needs its own tighter box rather
         * than reusing FREQ_TAP's shape. Geometrically disjoint from
         * FREQ_TAP (x < MODE_X vs. x >= TIME_TAP_X1, and MODE_X is
         * far to the left of TIME_X) and from MENU_AREA/the spectrum
         * panel (both start at SPEC_Y, well below TIME_TAP_Y2) - same
         * "allowed even while s_menu_open" reasoning as the frequency
         * zone just above. */
        /* 22/09/2026: igual que la zona de la frecuencia de aqui arriba, la
         * decide ui_top.c. TIME_TAP_X1 valia TIME_X - 10 = 680, la X del
         * reloj en la cabecera de ANTES del rediseno; el reloj de ahora va
         * alineado a la derecha y su texto empieza en 726, asi que quedaban
         * 46 px de barra vacia que abrian el teclado de la hora. Es lo que
         * pasaba al tocar "entre LSB y la hora". Ver ui_top_clock_hit(). */
        s_time_tap_active = ui_top_clock_hit(x, y);

        /* ETAPA 4: chips de la barra de estado. Se decide en la PULSACION y
         * se respeta toda la gesticulacion, igual que las zonas de arriba y
         * por el mismo motivo: en este panel resistivo las coordenadas de la
         * SUELTA no son de fiar.
         *
         * No se permite con el menu abierto: los chips siguen visibles (la
         * barra de estado nunca se tapa) pero el menu tiene sus propias
         * celdas para lo mismo, y dejar dos caminos vivos a la vez para el
         * mismo ajuste es como se consigue que uno de los dos se quede sin
         * refrescar. */
        s_chip_tap = s_menu_open ? UI_TOP_HIT_NONE : ui_top_hit(x, y);

        /* ETAPA 4: barra de acciones. Sigue viva con el menu abierto, igual
         * que antes - vive por debajo de MENU_AREA, no la tapa nunca. El
         * retorno visual se pinta AQUI, en la pulsacion, que es lo que en un
         * tactil que pide fuerza evita el segundo toque de mas. */
        s_act_press = ui_act_hit(x, y);
        if (s_act_press >= 0) {
            act_sync();
            s_act.pressed = s_act_press;
            ui_act_draw_one(&s_act, (uint8_t)s_act_press);
        }
    }

    if (s_menu_detail_active && s_touch_owner_is_menu) {
        det_touch(x, y, pressed);
    } else if (s_menu_cfg_active && s_touch_owner_is_menu) {
        cfg_touch(x, y, pressed);
    } else if (kbd_activa() && s_touch_owner_is_menu) {
        /* Teclado de frecuencia o de hora (etapa 19). Va antes de la rejilla
         * porque ocupa la misma zona y las dos no pueden estar abiertas a la
         * vez - kbd_show() apaga la rejilla y grid_show() apaga el teclado. */
        kbd_touch(x, y, pressed);
    } else if (grid_activa() && s_touch_owner_is_menu) {
        /* Ajustes, bandas, modos y pasos: el mismo reparto para las cuatro.
         * Se decide en la pulsacion y se respeta hasta la suelta, misma razon
         * que el resto de zonas: en este panel las coordenadas de la suelta
         * no son de fiar. */
        grid_touch(x, y, pressed);
    }
    /* 22/09/2026: aqui habia dos ramas mas que pasaban el toque a
     * ui_screen_touch(). Ese reparto solo mira los widgets que se hayan
     * registrado con ui_screen_add_button(), y desde que el teclado dejo de
     * usarlos (etapa 19) no queda ninguno registrado en ninguna pantalla: las
     * dos ramas eran una llamada que recorria una lista vacia.
     *
     * Lo que queda de ui.c son los paneles -los marcos del espectro y de la
     * cascada-, que si dibujan. Las zonas tactiles vivas son las de ui_act.c,
     * ui_cfg.c, ui_det.c, ui_grid.c, ui_kbd.c y ui_top.c, todas con su propia
     * funcion de acierto, y las de arriba de esta misma funcion. */

    /*
     * SPEC_TAP_MOVE_THRESHOLD_PX: total travel from s_spec_tap_start_x
     * (not per-sample delta - see s_spec_drag_moved's own declaration
     * comment) beyond which a press-inside-the-spectrum gesture counts
     * as a genuine DRAG rather than a TAP. 6px chosen generously above
     * this still-uncalibrated resistive panel's own known single-
     * sample jitter (see spec_drag_tune_apply()'s CALIBRATION NOTE
     * cross-reference) - small enough that a real drag is recognized
     * almost immediately, large enough that an intended tap's own
     * finger-contact jitter never gets misread as the start of a drag.
     */
#define SPEC_TAP_MOVE_THRESHOLD_PX 6

    if (s_spec_drag_active && pressed) {
        int32_t total_dx = (int32_t)x - (int32_t)s_spec_tap_start_x;
        if (total_dx < 0) { total_dx = -total_dx; }
        if (total_dx > SPEC_TAP_MOVE_THRESHOLD_PX) {
            s_spec_drag_moved = 1U;
        }
        /* Only apply the relative-panning behavior once this gesture
         * has actually proven itself a drag - see spec_tap_tune_to_x()'s
         * comment for why an as-yet-undecided tap must NOT also nudge
         * the tune via spec_drag_tune_apply() first (jitter from a
         * stationary finger could otherwise sneak in a spurious small
         * relative retune before release ever gets to apply the
         * intended absolute one). s_spec_drag_prev_x still advances
         * every sample regardless, so the FIRST call after crossing
         * the threshold measures only the delta since the last sample,
         * not the whole gesture's travel - spec_drag_tune_apply()'s own
         * per-sample-delta design expects exactly that. */
        if (s_spec_drag_moved) {
            spec_drag_tune_apply(x, s_spec_drag_prev_x, &s_spec_drag_hz_accum);
        }
        s_spec_drag_prev_x = x;
    }

    if (s_act_press >= 0 && !pressed) {
        int8_t idx = s_act_press;

        /* Se apaga el resaltado ANTES de llamar al callback: varios de ellos
         * abren una pantalla que repinta media pantalla (Ajustes, Bandas), y
         * si se hace despues el boton se queda encendido debajo. */
        s_act_press = -1;
        act_sync();
        s_act.pressed = -1;
        ui_act_draw_one(&s_act, (uint8_t)idx);

        demo_button_callback(k_act_slots[idx].btn, UI_EVENT_RELEASE, NULL);
        /* El valor que muestra el boton casi siempre acaba de cambiar (modo,
         * paso, ruido...), asi que se repinta la barra entera - son 6 botones
         * y una sola ventana EXMC por banda. */
        act_draw();
    }

    if (s_chip_tap != UI_TOP_HIT_NONE && !pressed) {
        /*
         * Un toque en un chip hace EXACTAMENTE lo mismo que la celda del menu
         * con ese nombre: avanza al siguiente valor. Es lo que pediste para
         * la pantalla de ajustes ("si toco AGC una vez tiene que cambiar a
         * media"), aplicado tambien aqui.
         *
         * Se llaman los callbacks que ya existen, no una copia de su logica:
         * cada uno de ellos ademas de cambiar el valor refresca la celda del
         * menu y marca los ajustes para guardar. Reimplementar el "avanza
         * uno" aqui habria dejado fuera esas dos cosas sin que se note hasta
         * mucho despues.
         */
        switch (s_chip_tap) {
        case UI_TOP_HIT_AGC:
            agc_profile_button_callback(0, UI_EVENT_RELEASE, 0);
            break;
        case UI_TOP_HIT_BW:
            audio_bw_button_callback(0, UI_EVENT_RELEASE, 0);
            break;
        case UI_TOP_HIT_NR:
            /* Mismo par de lineas que el boton NR de la barra de abajo
             * (demo_button_callback()): el estado y el modulo de DSP van
             * siempre juntos. */
            s_nr_on = s_nr_on ? 0U : 1U;
            nr_ss_set_enabled(s_nr_on);
            badges_draw();
            break;
        case UI_TOP_HIT_SPK:
            /* El chip "MUDO" solo existe cuando lo esta: tocarlo solo puede
             * querer decir "desmutea". */
            speaker_pa_set_enabled(1U);
            top_sync();
            ui_top_draw_status(&s_top);
            break;
        default:
            break;   /* SOBRECARGA es un aviso, no un boton */
        }
    }

    if (s_freq_tap_active && !pressed) {
        menu_freq_keypad_show();
    }

    if (s_time_tap_active && !pressed) {
        menu_time_keypad_show();
    }

    if (s_borrar_tap && !pressed) {
        s_digi.btn_press = 0U;
        ui_digi_draw_boton(&s_digi);
        /* Borra el contenido y marca para repintar: es la misma funcion
         * que se usa al entrar en un modo digital desde otro, asi que no
         * hay una segunda forma de dejar el panel limpio que un dia se
         * comporte distinto. */
        rtty_text_panel_reset();
        debug_print("texto digital: borrado a mano\n");
    }

    if (s_spec_drag_active && !pressed && !s_spec_drag_moved) {
        /* Released without ever crossing the drag threshold - a
         * genuine tap. See spec_tap_tune_to_x()'s own comment. Uses
         * s_spec_tap_start_x (the ORIGINAL press position), not the
         * release sample's own (possibly garbage) coordinates - same
         * "don't trust the release sample" reasoning this whole
         * function already applies elsewhere on this resistive panel. */
        spec_tap_tune_to_x(s_spec_tap_start_x);
    }

    if (!pressed) {
        s_touch_active = 0U;   /* gesture over - the next press re-decides ownership */
        s_spec_drag_active = 0U;
        s_spec_drag_moved = 0U;
        s_freq_tap_active = 0U;
        s_time_tap_active = 0U;
        s_borrar_tap = 0U;
        s_chip_tap = UI_TOP_HIT_NONE;
        s_act_press = -1;
        s_grid_press = -1;
        s_kbd_press = -1;
        s_det_press = -1;
        s_cfg_press = -1;
    }
}

/*
 * s_db_min/s_db_max (now live, encoder-adjustable state - see their
 * declaration and full comment near s_encoder_target, above) are used
 * below for both spectrum_draw() and the waterfall's colormap. The
 * UNCALIBRATED-range caveat from their original comment still stands:
 * the "dB" value comes from a bit-manipulation log2 approximation
 * (see fft.c), not a referenced measurement.
 */

/* sdr_rx.h and fft.h define their sizes independently - if one is ever
 * changed without the other, a clear compile error is better than a
 * silent overflow of s_rx_i/s_rx_q. */
#if SDR_RX_BLOCK_SAMPLES != FFT_SIZE
#error "SDR_RX_BLOCK_SAMPLES (sdr_rx.h) and FFT_SIZE (fft.h) must match"
#endif

/*
 * *** CRITICAL FIX 05/08/2026 - was sized at plain SDR_RX_BLOCK_SAMPLES
 * (128), but sdr_rx_poll_block_iq() below (line ~4169) writes
 * sdr_rx_get_block_samples() samples - which is
 * SDR_RX_BLOCK_SAMPLES_WFM (512) while WFM is active. That was a
 * silent ~768-byte WRITE overrun past the end of EACH of these two
 * arrays on every single spectrum poll while in WFM - textbook memory
 * corruption of whatever static variables happen to sit next in the
 * linker layout, which is almost certainly what the project owner
 * saw as "el espectro se ralentiza, el volumen se sube solo, se
 * activan otros menus solos, y al volver a AM se cuelga" - all
 * classic symptoms of a buffer overrun clobbering unrelated state
 * (gain variables, UI state, whatever else the linker happened to
 * place right after these two arrays), not four separate bugs.
 *
 * Sized at SDR_RX_BLOCK_SAMPLES_MAX now so it can never overflow
 * regardless of which rate is active - same fix pattern as
 * s_stream_buf/s_raw_buf already got today for the exact same class
 * of bug (see gd32_i2s.c's STREAM_FRAMES_PER_HALF comment and
 * sdr_rx.c's own header comment).
 *
 * NOTE - this fixes the CRASH/CORRUPTION, not yet the DISPLAY: the
 * FFT/spectrum pipeline below this point still processes a fixed
 * FFT_SIZE (256) samples regardless of how many sdr_rx actually
 * delivered, so the panadapter in WFM will show only the FIRST 256 of
 * the 512 samples per block (a real picture, just not WFM's full
 * span) until that's resized too - see this project's WFM migration
 * notes for that remaining, separately-tracked piece. Nothing below
 * reads past what it currently reads, so this is now safe, just
 * incomplete for WFM specifically.
 */
static int16_t s_rx_i[SDR_RX_BLOCK_SAMPLES_MAX];
static int16_t s_rx_q[SDR_RX_BLOCK_SAMPLES_MAX];
static float   s_db[FFT_BINS_IQ]; /* fftshifted: VFO at the center index */

/*
 * UPDATED (28/07/2026): aic3204.c now runs the full real captured
 * sequence (see aic3204_phase2_init()), which restores the
 * differential I/Q routing: left = I (IN2_L/IN2_R differential),
 * right = Q (IN3_R/IN3_L differential) - this is no longer the
 * single-ended baseline from a few rounds ago.
 */

/* debug_uart.h has no signed decimal print - I/Q sample min/max are
 * int16_t and can be negative, hence this small local helper instead
 * of touching the UART module for it. Builds the full "label = -N\n"
 * string in one pass (does NOT delegate to debug_print_dec with an
 * empty label - that duplicated the " = " prefix and produced
 * confusing output like "label = - = 1" instead of "label = -1"). */
static void debug_print_dec_signed(const char *label, int32_t val)
{
    char buf[12];
    (void)buf;
    int i = 11;
    uint32_t uval;
    uint8_t negative = 0;

    buf[11] = '\0';
    if (val < 0) {
        negative = 1;
        uval = (uint32_t)(-val);
    } else {
        uval = (uint32_t)val;
    }

    if (uval == 0U) {
        buf[--i] = '0';
    } else {
        while (uval > 0U && i > 0) {
            buf[--i] = (char)('0' + (uval % 10U));
            uval /= 10U;
        }
    }
    if (negative && i > 0) {
        buf[--i] = '-';
    }

    debug_print(label);
    debug_print(" = ");
    debug_print(&buf[i]);
    debug_print("\n");
}

/*
 * --- Spectrum ZOOM: decimator + processing -------------------------------
 *
 * See spec_zoom_t's comment (near the menu tile code, above) for the
 * overall design. This block holds the actual DSP: a generic
 * decimate-by-2 FIR, cascadable up to 3x, plus the accumulation state
 * that gathers enough decimated samples across multiple raw 192kHz
 * blocks to fill one FFT_SIZE window.
 */

/*
 * ZOOM_DECIM2_COEFFS: a single decimate-by-2 stage, reused identically
 * at every cascade level (1, 2, or 3 passes) since only the INPUT:
 * OUTPUT ratio matters, not the absolute sample rate - the same 31-tap
 * filter is correct whether it's decimating 192kHz->96kHz (stage 1),
 * 96kHz->48kHz (stage 2), or 48kHz->24kHz (stage 3). Designed offline
 * via scipy.signal.firwin (Hamming window, same general FIR-lowpass
 * approach as demod_am.c's DECIM_COEFFS, just far shorter - a single
 * x2 stage doesn't need anywhere near that much stopband rejection,
 * especially since ZOOM_8X cascades three of these for a combined
 * ~84dB at the critical alias-fold point, verified numerically:
 *
 *   0.10-0.30 x Nyquist: essentially flat (+/-0.01dB) - the passband
 *   0.42 x Nyquist:      -6.00dB (roughly the -6dB corner)
 *   0.50 x Nyquist:      -27.97dB (the post-decimation Nyquist - the
 *                         worst-case fold-back point for aliasing)
 *   0.55-1.0 x Nyquist:  -54 to -62dB (deep stopband)
 *
 * A single stage's -28dB at the fold point is on the modest side for
 * a rigorous decimator, but this feeds a VISUAL spectrum display, not
 * a measurement - and cascading stages for higher zoom multiplies
 * that rejection (three stages for ZOOM_8X -> ~84dB), so it only gets
 * better at higher zoom, not worse.
 */
#define ZOOM_DECIM2_TAPS 31U
static const float32_t ZOOM_DECIM2_COEFFS[ZOOM_DECIM2_TAPS] = {
    0.0013731076f, -0.0007535444f, -0.0029087841f, -0.0005579049f, 0.0062459162f, 0.0057986726f,
    -0.0089671792f, -0.0177057998f, 0.0050097395f, 0.0361091799f, 0.0151443729f, -0.0569498814f,
    -0.0705344064f, 0.0736069684f, 0.3051388190f, 0.4199014482f, 0.3051388190f, 0.0736069684f,
    -0.0705344064f, -0.0569498814f, 0.0151443729f, 0.0361091799f, 0.0050097395f, -0.0177057998f,
    -0.0089671792f, 0.0057986726f, 0.0062459162f, -0.0005579049f, -0.0029087841f, -0.0007535444f,
    0.0013731076f
};

/* Three cascaded stages, one CMSIS decimator instance per channel per
 * stage (6 total) - each stage's block size is half the previous
 * one's, so they can't share instances/state. Named by INPUT rate at
 * 192kHz: stage1 sees the raw 512-sample block, stage2 sees stage1's
 * 256-sample output, stage3 sees stage2's 128-sample output. */
static arm_fir_decimate_instance_f32 s_zoom_dec1_i, s_zoom_dec1_q;
static arm_fir_decimate_instance_f32 s_zoom_dec2_i, s_zoom_dec2_q;
static arm_fir_decimate_instance_f32 s_zoom_dec3_i, s_zoom_dec3_q;
/* *** 01/09/2026: moved to TCM RAM *** - CMSIS decimator states and
 * their surrounding CPU working buffers, never DMA targets (only this
 * zoom-cascade's own CPU code touches them, once per raw RX block) -
 * see fft.c's fuller TCM comment for the "why" (freed main-RAM
 * headroom for the widened waterfall/spectrum panel). */
#define TCMRAM_BSS __attribute__((section(".tcmram")))
static float32_t s_zoom_dec1_i_state[ZOOM_DECIM2_TAPS + SDR_RX_BLOCK_SAMPLES - 1U] TCMRAM_BSS;
static float32_t s_zoom_dec1_q_state[ZOOM_DECIM2_TAPS + SDR_RX_BLOCK_SAMPLES - 1U] TCMRAM_BSS;
static float32_t s_zoom_dec2_i_state[ZOOM_DECIM2_TAPS + (SDR_RX_BLOCK_SAMPLES / 2U) - 1U] TCMRAM_BSS;
static float32_t s_zoom_dec2_q_state[ZOOM_DECIM2_TAPS + (SDR_RX_BLOCK_SAMPLES / 2U) - 1U] TCMRAM_BSS;
static float32_t s_zoom_dec3_i_state[ZOOM_DECIM2_TAPS + (SDR_RX_BLOCK_SAMPLES / 4U) - 1U] TCMRAM_BSS;
static float32_t s_zoom_dec3_q_state[ZOOM_DECIM2_TAPS + (SDR_RX_BLOCK_SAMPLES / 4U) - 1U] TCMRAM_BSS;

/* Working buffers, one per stage boundary. s_zoom_f_i/q hold the raw
 * block cast to float and (if needed) re-centered on the tuned
 * frequency, BEFORE stage 1. */
static float32_t s_zoom_f_i[SDR_RX_BLOCK_SAMPLES] TCMRAM_BSS;
static float32_t s_zoom_f_q[SDR_RX_BLOCK_SAMPLES] TCMRAM_BSS;
static float32_t s_zoom_s1_i[SDR_RX_BLOCK_SAMPLES / 2U] TCMRAM_BSS;
static float32_t s_zoom_s1_q[SDR_RX_BLOCK_SAMPLES / 2U] TCMRAM_BSS;
static float32_t s_zoom_s2_i[SDR_RX_BLOCK_SAMPLES / 4U] TCMRAM_BSS;
static float32_t s_zoom_s2_q[SDR_RX_BLOCK_SAMPLES / 4U] TCMRAM_BSS;
static float32_t s_zoom_s3_i[SDR_RX_BLOCK_SAMPLES / 8U] TCMRAM_BSS;
static float32_t s_zoom_s3_q[SDR_RX_BLOCK_SAMPLES / 8U] TCMRAM_BSS;

/* Accumulates decimated samples across as many raw blocks as it takes
 * to fill one FFT_SIZE window (2/4/8 raw blocks for ZOOM_2X/4X/8X -
 * see spec_zoom_t's comment). int16_t, not float32: fft_compute_db_iq()
 * takes int16_t input (same type sdr_rx.c's raw blocks use) - cheaper
 * to convert once here than to touch fft.c's signature for this. */
static int16_t s_zoom_acc_i[FFT_SIZE];
static int16_t s_zoom_acc_q[FFT_SIZE];
static uint16_t s_zoom_acc_count = 0U;

static void zoom_decimators_init(void)
{
    /* Same "check the return status, shout over UART if it ever
     * fails" discipline demod_am.c's own decimator init uses - see
     * its comment. blockSize % decimFactor == 0 is the actual
     * constraint (512/256/128, decimating by 2 each time - always
     * exact), so this isn't expected to ever trip, but a silently
     * unusable instance with no other symptom than a garbled zoomed
     * spectrum would be a nasty thing to debug blind. */
    if (arm_fir_decimate_init_f32(&s_zoom_dec1_i, ZOOM_DECIM2_TAPS, 2U, ZOOM_DECIM2_COEFFS,
                                    s_zoom_dec1_i_state, SDR_RX_BLOCK_SAMPLES) != ARM_MATH_SUCCESS) {
        debug_print("zoom: *** decimator stage1 I init FAILED ***\n");
    }
    if (arm_fir_decimate_init_f32(&s_zoom_dec1_q, ZOOM_DECIM2_TAPS, 2U, ZOOM_DECIM2_COEFFS,
                                    s_zoom_dec1_q_state, SDR_RX_BLOCK_SAMPLES) != ARM_MATH_SUCCESS) {
        debug_print("zoom: *** decimator stage1 Q init FAILED ***\n");
    }
    if (arm_fir_decimate_init_f32(&s_zoom_dec2_i, ZOOM_DECIM2_TAPS, 2U, ZOOM_DECIM2_COEFFS,
                                    s_zoom_dec2_i_state, SDR_RX_BLOCK_SAMPLES / 2U) != ARM_MATH_SUCCESS) {
        debug_print("zoom: *** decimator stage2 I init FAILED ***\n");
    }
    if (arm_fir_decimate_init_f32(&s_zoom_dec2_q, ZOOM_DECIM2_TAPS, 2U, ZOOM_DECIM2_COEFFS,
                                    s_zoom_dec2_q_state, SDR_RX_BLOCK_SAMPLES / 2U) != ARM_MATH_SUCCESS) {
        debug_print("zoom: *** decimator stage2 Q init FAILED ***\n");
    }
    if (arm_fir_decimate_init_f32(&s_zoom_dec3_i, ZOOM_DECIM2_TAPS, 2U, ZOOM_DECIM2_COEFFS,
                                    s_zoom_dec3_i_state, SDR_RX_BLOCK_SAMPLES / 4U) != ARM_MATH_SUCCESS) {
        debug_print("zoom: *** decimator stage3 I init FAILED ***\n");
    }
    if (arm_fir_decimate_init_f32(&s_zoom_dec3_q, ZOOM_DECIM2_TAPS, 2U, ZOOM_DECIM2_COEFFS,
                                    s_zoom_dec3_q_state, SDR_RX_BLOCK_SAMPLES / 4U) != ARM_MATH_SUCCESS) {
        debug_print("zoom: *** decimator stage3 Q init FAILED ***\n");
    }
    s_zoom_acc_count = 0U;
}

/*
 * Runs ONLY when s_spec_zoom != SPEC_ZOOM_1X (the caller checks first -
 * at 1X this whole pipeline is skipped, zero extra cost). Processes
 * ONE raw 192kHz block: casts to float, re-centers on the tuned
 * frequency if needed, runs however many cascaded x2 stages the
 * current zoom level calls for, and appends the result to the
 * accumulator. Returns 1 when the accumulator just became FULL (a
 * fresh FFT_SIZE window is ready in s_zoom_acc_i/q), 0 otherwise -
 * the caller only computes/draws a new frame on a 1.
 */
static uint8_t zoom_process_block(void)
{
    uint16_t n;
    const float32_t *out_i;
    const float32_t *out_q;
    uint16_t out_len;

    for (n = 0; n < SDR_RX_BLOCK_SAMPLES; n++) {
        s_zoom_f_i[n] = (float32_t)s_rx_i[n];
        s_zoom_f_q[n] = (float32_t)s_rx_q[n];
    }

    /* Re-center on the tuned frequency BEFORE decimating - otherwise
     * stage 1's anti-alias filter (centered on 0Hz) would attenuate
     * the very station this is supposed to zoom into, whenever the
     * low-IF down-mix has the LO sitting DEMOD_IF_OFFSET_HZ off the
     * true station (AM/USB/LSB/NFM - see demod_am.h's LOW-IF TUNING
     * note). Same sign-flip-only rotation demod_am.c's own down-mix
     * uses, at the same fixed Fs/4 rate (always relative to the RAW
     * 192kHz block, regardless of zoom level - this runs BEFORE any
     * decimation touches the data). WFM has no offset to correct
     * (demod_am_get_if_offset_active() is 0 there), so this is
     * skipped entirely in that mode - the station's already at DC. */
    if (demod_am_get_if_offset_active()) {
        for (n = 0; n < SDR_RX_BLOCK_SAMPLES; n += 4U) {
            float32_t hh1, hh2;

            hh1 =  s_zoom_f_q[n + 1U];
            hh2 = -s_zoom_f_i[n + 1U];
            s_zoom_f_i[n + 1U] = hh1;
            s_zoom_f_q[n + 1U] = hh2;

            hh1 = -s_zoom_f_i[n + 2U];
            hh2 = -s_zoom_f_q[n + 2U];
            s_zoom_f_i[n + 2U] = hh1;
            s_zoom_f_q[n + 2U] = hh2;

            hh1 = -s_zoom_f_q[n + 3U];
            hh2 =  s_zoom_f_i[n + 3U];
            s_zoom_f_i[n + 3U] = hh1;
            s_zoom_f_q[n + 3U] = hh2;
        }
    }

    arm_fir_decimate_f32(&s_zoom_dec1_i, s_zoom_f_i, s_zoom_s1_i, SDR_RX_BLOCK_SAMPLES);
    arm_fir_decimate_f32(&s_zoom_dec1_q, s_zoom_f_q, s_zoom_s1_q, SDR_RX_BLOCK_SAMPLES);
    out_i = s_zoom_s1_i;
    out_q = s_zoom_s1_q;
    out_len = SDR_RX_BLOCK_SAMPLES / 2U;

    if (s_spec_zoom >= SPEC_ZOOM_4X) {
        arm_fir_decimate_f32(&s_zoom_dec2_i, s_zoom_s1_i, s_zoom_s2_i, SDR_RX_BLOCK_SAMPLES / 2U);
        arm_fir_decimate_f32(&s_zoom_dec2_q, s_zoom_s1_q, s_zoom_s2_q, SDR_RX_BLOCK_SAMPLES / 2U);
        out_i = s_zoom_s2_i;
        out_q = s_zoom_s2_q;
        out_len = SDR_RX_BLOCK_SAMPLES / 4U;
    }
    if (s_spec_zoom >= SPEC_ZOOM_8X) {
        arm_fir_decimate_f32(&s_zoom_dec3_i, s_zoom_s2_i, s_zoom_s3_i, SDR_RX_BLOCK_SAMPLES / 4U);
        arm_fir_decimate_f32(&s_zoom_dec3_q, s_zoom_s2_q, s_zoom_s3_q, SDR_RX_BLOCK_SAMPLES / 4U);
        out_i = s_zoom_s3_i;
        out_q = s_zoom_s3_q;
        out_len = SDR_RX_BLOCK_SAMPLES / 8U;
    }

    for (n = 0; n < out_len && s_zoom_acc_count < FFT_SIZE; n++, s_zoom_acc_count++) {
        float32_t vi = out_i[n];
        float32_t vq = out_q[n];
        if (vi > 32767.0f)  { vi = 32767.0f; }
        if (vi < -32768.0f) { vi = -32768.0f; }
        if (vq > 32767.0f)  { vq = 32767.0f; }
        if (vq < -32768.0f) { vq = -32768.0f; }
        s_zoom_acc_i[s_zoom_acc_count] = (int16_t)vi;
        s_zoom_acc_q[s_zoom_acc_count] = (int16_t)vq;
    }

    if (s_zoom_acc_count >= FFT_SIZE) {
        s_zoom_acc_count = 0U;
        return 1U;
    }
    return 0U;
}

/*
 * Replaces the earlier synthetic-gradient waterfall demo with the real
 * capture -> FFT -> spectrum/waterfall pipeline. Non-blocking: if
 * sdr_rx_poll_block_iq() has no new block yet, this tick does nothing
 * (the rest of the main loop - touch, UI - stays just as responsive).
 */
static void sdr_spectrum_waterfall_tick(void)
{
    /*
     * RESTRUCTURED (30/07/2026) - display decoupled from block rate.
     *
     * Blocks arrive at 96000Hz/256 = 375/s (was 48000Hz/128 before
     * AM/SSB/NFM moved to 96kHz, and 192000Hz/512 before that - see
     * sdr_rx.h's SDR_RX_BLOCK_SAMPLES comment; SAME 375/s in every
     * case, by design); redrawing the whole
     * spectrum + waterfall (~128k EXMC pixel writes) for EVERY block
     * was the main reason the display felt slow AND the trace looked
     * nervous. Now:
     *
     *   - Every polled block still gets its FFT, but the dB bins are
     *     ACCUMULATED (summed) instead of drawn - all received signal
     *     contributes, nothing is thrown away.
     *   - Every SPECTRUM_FRAME_MS the accumulated average is drawn
     *     once: spectrum + one waterfall line. Averaging N FFTs per
     *     frame lowers the displayed noise variance (calmer floor,
     *     smoother waterfall) for free.
     *   - FFTs are capped at SPECTRUM_MAX_FFT_PER_FRAME per frame so
     *     the FFT itself can never starve touch/encoder polling in
     *     the main loop; excess blocks are simply skipped (poll still
     *     drains them so DMA never backs up).
     */
#define SPECTRUM_FRAME_MS        33U /* ~30 fps */
#define SPECTRUM_MAX_FFT_PER_FRAME 6U

    /* s_db_frame subio a fichero (22/09/2026): es EXACTAMENTE lo que se ve
     * dibujado, y el toque en el espectro necesita mirarlo para caer sobre
     * la señal. Sigue siendo el mismo almacenamiento estatico. */
    static float    s_db_sum[FFT_BINS_IQ];
    static uint32_t s_db_count = 0U;
    static uint32_t s_next_frame_ms = 0U;
    static uint8_t line[WATERFALL_WIDTH];   /* indices de colormap, no color */
    static uint32_t s_frame_count = 0U;
    uint32_t t_fft0, t_fft1, t_spec0, t_spec1, t_wf0, t_wf1;
    uint32_t fft_us = 0U;
    (void)fft_us;
    uint16_t x, n;
    int16_t i_min, i_max, q_min, q_max;
    uint32_t bi;

    if (sdr_rx_poll_block_iq(s_rx_i, s_rx_q) == 0U) {
        return;
    }

    /* Min/max of both channels, every block - the numeric evidence for
     * whether the ADC is producing real, varying samples at all (vs.
     * pinned at a fixed value, which is what we've been chasing).
     * i_min/i_max is I (left, IN2 differential), q_min/q_max is Q
     * (right, IN3 differential) - naming now matches reality again. */
    i_min = s_rx_i[0]; i_max = s_rx_i[0];
    q_min = s_rx_q[0]; q_max = s_rx_q[0];
    for (n = 1; n < SDR_RX_BLOCK_SAMPLES; n++) {
        if (s_rx_i[n] < i_min) { i_min = s_rx_i[n]; }
        if (s_rx_i[n] > i_max) { i_max = s_rx_i[n]; }
        if (s_rx_q[n] < q_min) { q_min = s_rx_q[n]; }
        if (s_rx_q[n] > q_max) { q_max = s_rx_q[n]; }
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    /* --- accumulate this block's FFT --- */
    if (s_spec_zoom == SPEC_ZOOM_1X) {
        /* Unchanged existing behavior: average up to
         * SPECTRUM_MAX_FFT_PER_FRAME raw-rate FFTs per display frame
         * (block skipped once the quota's met, still drained). */
        if (s_db_count < SPECTRUM_MAX_FFT_PER_FRAME) {
            t_fft0 = DWT->CYCCNT;
            /* Complex I/Q transform: +f/-f separated, VFO at the center of
             * the display (fftshift order, see fft.h). This is what makes
             * the panadapter read like one: a signal tuned exactly on the
             * VFO sits on the center line, not at the left edge. */
            fft_compute_db_iq(s_rx_i, s_rx_q, s_db);
            t_fft1 = DWT->CYCCNT;
            fft_us = (uint32_t)(((uint64_t)(t_fft1 - t_fft0)) * 1000000U / SystemCoreClock);

            if (s_db_count == 0U) {
                for (bi = 0; bi < FFT_BINS_IQ; bi++) {
                    s_db_sum[bi] = s_db[bi];
                }
            } else {
                for (bi = 0; bi < FFT_BINS_IQ; bi++) {
                    s_db_sum[bi] += s_db[bi];
                }
            }
            s_db_count++;
        }
    } else {
        /* Zoom mode (see spec_zoom_t's comment) - zoom_process_block()
         * MUST run on every raw block regardless of whether a completed
         * window is already waiting to be drawn: it's what keeps the
         * cascaded decimators' internal FIR history continuous. Skipping
         * a block here would corrupt that history and glitch the very
         * next window, not just this one.
         *
         * No multi-window averaging at zoom>1X - there's no time budget
         * left for it on top of the refresh-rate cost zoom already pays
         * (see spec_zoom_t's comment) - just take the FRESHEST completed
         * window each time one finishes, discarding any that complete
         * before the display-frame timer below actually fires (that
         * happens routinely at ZOOM_2X, whose ~5.3ms window time is much
         * shorter than the ~33ms frame period). s_db_count is reused as
         * the same "is there anything fresh to draw" signal the 1X path
         * already uses - 0 after a draw (or skip), 1 once a window's
         * ready - so the frame-ready gate right below needs no zoom-
         * specific logic of its own. */
        if (zoom_process_block()) {
            t_fft0 = DWT->CYCCNT;
            fft_compute_db_iq(s_zoom_acc_i, s_zoom_acc_q, s_db);
            t_fft1 = DWT->CYCCNT;
            fft_us = (uint32_t)(((uint64_t)(t_fft1 - t_fft0)) * 1000000U / SystemCoreClock);

            for (bi = 0; bi < FFT_BINS_IQ; bi++) {
                s_db_sum[bi] = s_db[bi];
            }
            s_db_count = 1U;
        }
    }

    /* --- not yet time to draw a frame? then this tick is done ---
     * Signed-difference comparison so the 32-bit ms counter wrapping
     * (every ~49 days) doesn't freeze the display. */
    if (((int32_t)(g_msticks - s_next_frame_ms) < 0) || s_db_count == 0U) {
        return;
    }
    s_next_frame_ms = g_msticks + SPECTRUM_FRAME_MS;

    /* Per-frame status updates, all cheap and all OUTSIDE the ISR:
     * S-meter (skips its blits when the segment count is unchanged)
     * and the once-a-minute time readout. ALWAYS run, even while the
     * settings menu is open (31/07/2026, see s_menu_screen's
     * declaration comment) - the right column and top bar stay live
     * the whole time the menu is showing, only the spectrum/waterfall
     * panel underneath the menu gets skipped below. */
    smeter_draw(smeter_segments_from_peak(demod_am_get_signal_peak()));
    smeter_dbfs_uart_report(demod_am_get_signal_peak()); /* see its own comment - S-meter calibration aid */
    /* *** 01/09/2026: replaces the old snr_update_and_draw(), moved
     * HERE specifically (not left inside the s_db_frame/!s_menu_open
     * gated block below, where the old SNR readout used to live) -
     * this reads demod_am_get_signal_peak() directly, same as the
     * S-meter bar right above it, so it should stay live exactly like
     * the S-meter does (never freezing while the menu is open) rather
     * than inheriting the old SNR readout's "freezes with s_db_frame"
     * behavior, which no longer applies now that this doesn't touch
     * s_db_frame at all. See smeter_dbm_update_and_draw()'s own
     * comment for the full "why" of this replacement. */
    smeter_dbm_update_and_draw(demod_am_get_signal_peak());
    sam_calib_display_draw(); /* MS5351 PPM calibration readout, 21/08/2026 - see its own comment; needs to update live as the PLL converges, same cadence as the S-meter above, not just on mode-change events */
    {
        static uint32_t s_last_time_min = 0xFFFFFFFFUL;
        uint32_t now_min = g_msticks / 60000UL;
        if (now_min != s_last_time_min) {
            s_last_time_min = now_min;
            time_display_draw();
        }
    }

    /* Settings menu covers the spectrum+waterfall panel while open
     * (see s_menu_screen's declaration comment) - no point spending
     * cycles/EXMC bandwidth averaging/smoothing FFT data or redrawing
     * a panel nobody can see underneath it. FFT accumulation above
     * this point keeps running regardless (cheap, and keeps data
     * fresh for the moment the menu closes) - just the CONSUMPTION of
     * it (this whole block) is skipped. s_db_count is still reset at
     * the very end either way, so accumulation restarts cleanly next
     * frame regardless of which path was taken. */
    if (!s_menu_open) {
    /* Average of all FFTs accumulated during this frame window. */
    {
        float inv = 1.0f / (float)s_db_count;
        for (bi = 0; bi < FFT_BINS_IQ; bi++) {
            s_db_frame[bi] = s_db_sum[bi] * inv;
        }
    }

    /*
     * TEMPORAL smoothing ACROSS display frames - added 31/07/2026 per
     * the project owner: the trace looked too "nervous"/jittery bin-
     * to-bin between frames. This is a SEPARATE thing from the
     * intra-frame FFT averaging just above (which combines multiple
     * FFT snapshots WITHIN one ~33ms display frame, reducing noise
     * within a single displayed frame) - this instead blends each new
     * frame with the PREVIOUS frame's already-smoothed result, an
     * exponential moving average per bin, reducing frame-to-frame
     * jumpiness on top of that.
     *
     * s_spectrum_smooth_alpha is the blend weight given to history:
     * 0.0 disables smoothing entirely (each frame drawn raw), 0.95 is
     * the practical ceiling (see SPECTRUM_SMOOTH_MAX below - true 1.0
     * would freeze the display forever). 0.75 (the default, same
     * value this started as a #define with) at ~33ms/frame works out
     * to a time constant of about -33ms/ln(0.75) =~ 115ms (a handful
     * of frames) - enough to visibly calm the trace down without
     * making it feel laggy behind a real signal actually changing.
     * NOW LIVE-ADJUSTABLE (31/07/2026) via ENCODER_TARGET_SMOOTH - see
     * its declaration and the SMOOTH tile in the settings menu screen
     * (menu_screen_open()).
     */
    {
        static float s_db_smooth[FFT_BINS_IQ];
        static uint8_t s_db_smooth_init = 0U;

        if (!s_db_smooth_init) {
            /* First frame ever: nothing to blend with yet - seed
             * directly, rather than blending against a zeroed array
             * (which would otherwise show a slow fade-IN from silence
             * on every boot, not just a jitter reduction). */
            for (bi = 0; bi < FFT_BINS_IQ; bi++) {
                s_db_smooth[bi] = s_db_frame[bi];
            }
            s_db_smooth_init = 1U;
        } else {
            for (bi = 0; bi < FFT_BINS_IQ; bi++) {
                s_db_smooth[bi] = s_spectrum_smooth_alpha * s_db_smooth[bi]
                                   + (1.0f - s_spectrum_smooth_alpha) * s_db_frame[bi];
            }
        }
        /* s_db_frame itself becomes the smoothed result from here on
         * - both spectrum_draw() and the waterfall colormap loop
         * below read s_db_frame, so this keeps them visually
         * consistent with each other without touching either call
         * site. */
        for (bi = 0; bi < FFT_BINS_IQ; bi++) {
            s_db_frame[bi] = s_db_smooth[bi];
            s_db_frame_listo = 1U;
        }
    }

    if (s_spec_agc_enabled) {
        spec_agc_step(s_db_frame, FFT_BINS_IQ, &s_db_min, &s_db_max);
    }

    t_spec0 = DWT->CYCCNT;
    /* Spectrum trace inside its panel (see the RADIO UI LAYOUT block):
     * top margin 4px, bottom leaves room for the span-label row.
     *
     * center_mark_offset_px: when low-IF tuning is active (see
     * demod_am.h's LOW-IF TUNING note), the demodulated signal sits
     * DEMOD_IF_OFFSET_HZ away from the true LO/center bin, not on it
     * - shift the marker line to match. Full span is +/-48kHz (96kHz
     * I/Q rate - was +/-24kHz @ 48kHz, and +/-96kHz @ 192kHz before
     * that, see sdr_rx.h's SDR_RX_BLOCK_SAMPLES comment), so
     * pixels-per-Hz = SPEC_TRACE_W/96000; at exactly Fs/4 that's
     * SPEC_TRACE_W/4 = 168, exact - UNCHANGED by any Fs move, since
     * DEMOD_IF_OFFSET_HZ is
     * ALWAYS defined as exactly Fs/4 (see its own comment in
     * demod_am.h), so this pixel offset was always really
     * SPEC_TRACE_W/4 algebraically, independent of whatever Fs
     * happens to be. POSITIVE (right,
     * higher frequency): bench-confirmed 31/07/2026 (flipped from an
     * earlier NEGATIVE assumption, which had it backwards) - the
     * wanted signal lands at +SR/4 relative to the LO. This is a
     * DISPLAY-ONLY marker; it doesn't need to (and doesn't) match
     * sign with demod_am.c's down-mix rotation, which operates on a
     * different axis (time-domain phase rotation direction, not a
     * screen-pixel offset) - don't assume the two must carry the same
     * sign just because they're both "SR/4-related".
     *
     * ZOOM (s_spec_zoom != SPEC_ZOOM_1X): always 0 regardless of
     * if_offset_active - zoom_process_block() already re-centers the
     * signal on the tuned frequency BEFORE decimating (see its own
     * comment), specifically so the zoomed FFT has the station sitting
     * at DC. Applying the SR/4 offset on top of that would be double-
     * correcting for something the zoom pipeline already fixed. */
    {
        int16_t center_mark_offset_px = 0;
        uint8_t band_active = 0U;
        int16_t band_lo_offset_px = 0;
        int16_t band_hi_offset_px = 0;

        if (s_spec_zoom == SPEC_ZOOM_1X && demod_am_get_if_offset_active()) {
            /* = SPEC_TRACE_W/4 exactly, Fs-independent - see this
             * block's own header comment for the full algebraic
             * derivation (pixels-per-Hz * DEMOD_IF_OFFSET_HZ, and
             * DEMOD_IF_OFFSET_HZ is ALWAYS exactly Fs/4, so the Fs
             * term cancels completely regardless of which rate is
             * active - 96kHz or 48kHz both land here identically).
             * Written as SPEC_TRACE_W/4 directly rather than the old
             * "* DEMOD_IF_OFFSET_HZ / 96000UL" form, which silently
             * assumed Fs=96000 in its denominator - harmless before
             * 01/09/2026 when 96kHz was the only rate, but would have
             * silently computed SPEC_TRACE_W/8 (wrong) once 48kHz
             * became a real option. */
            center_mark_offset_px = (int16_t)(SPEC_TRACE_W / 4U);
        }

        /*
         * Demodulated-bandwidth tint (see spectrum_draw()'s comment
         * in spectrum.h) - added 03/08/2026, per the project owner:
         * shows which slice of the panadapter the CURRENT audio
         * bandwidth selection (BW tile - see k_audio_bw_hz above)
         * actually covers, anchored on the SAME point
         * center_mark_offset_px already marks (so it moves together
         * with the low-IF marker, and collapses to the panel center
         * under ZOOM exactly like that marker does - see its comment
         * above for why).
         *
         * Only meaningful for AM/USB/LSB, which are the only modes
         * with a caller-selectable audio bandwidth (NFM's channel
         * filter and WFM's full-Nyquist width are both FIXED - see
         * the BW badge's "6K3"/"96K", non-interactive, in
         * aux_row_display_draw()) - band_active stays 0 for those,
         * same as the project owner asked ("en WFM, NFM no se
         * muestra").
         *
         * full_span_hz: the SAME halving-per-zoom-step span
         * spec_span_labels_draw() uses for its tick ruler (96000 at
         * 1X, matching DEMOD_IF_OFFSET_HZ's own scale above - was
         * 48000 before AM/SSB/NFM moved to 96kHz, and 192000 before
         * that, see sdr_rx.h's SDR_RX_BLOCK_SAMPLES comment) - needed
         * here too since the tint's WIDTH in pixels must shrink/grow
         * the same way the ruler's tick spacing (and the marker's
         * position) does when ZOOM changes what one pixel is worth in
         * Hz.
         *
         * AM is double-sideband: the tint straddles the center point
         * both ways, +/-bw_hz. USB only demodulates the UPPER
         * sideband: the tint extends RIGHT (higher frequency) only,
         * from the center point out to +bw_hz. LSB is the mirror:
         * LEFT only, -bw_hz to the center point - exactly the "a la
         * derecha o la izquierda" the project owner asked for.
         */
        {
            demod_mode_t mode = demod_am_get_mode();

            if (mode == DEMOD_MODE_AM || mode == DEMOD_MODE_USB || mode == DEMOD_MODE_LSB) {
                uint32_t bw_hz = k_audio_bw_hz[(uint8_t)demod_am_get_audio_bw()];
                int16_t bw_px;

                /* *** 01/09/2026: rate-aware via spec_zoom_full_span_
                 * hz() *** - see that function's own comment for the
                 * full "why". */
                bw_px = (int16_t)((uint32_t)SPEC_TRACE_W * bw_hz / spec_zoom_full_span_hz());

                band_active = 1U;
                if (mode == DEMOD_MODE_USB) {
                    band_lo_offset_px = center_mark_offset_px;
                    band_hi_offset_px = (int16_t)(center_mark_offset_px + bw_px);
                } else if (mode == DEMOD_MODE_LSB) {
                    band_lo_offset_px = (int16_t)(center_mark_offset_px - bw_px);
                    band_hi_offset_px = center_mark_offset_px;
                } else { /* DEMOD_MODE_AM */
                    band_lo_offset_px = (int16_t)(center_mark_offset_px - bw_px);
                    band_hi_offset_px = (int16_t)(center_mark_offset_px + bw_px);
                }
            }
        }

        /* ETAPA 3b: los ejes, por comparacion - ver spec_chrome_tick(). Va
         * ANTES de la traza para que, si la escala ha cambiado en este
         * frame, el numero nuevo y la traza nueva salgan a la vez. */
        spec_chrome_tick();

        spectrum_draw(s_db_frame, FFT_BINS_IQ,
                      SPC_TRACE_X, SPC_TRACE_Y,
                      SPC_TRACE_W, SPC_TRACE_H,
                      s_db_min, s_db_max,
                      center_mark_offset_px,
                      band_active, band_lo_offset_px, band_hi_offset_px);
    }
    t_spec1 = DWT->CYCCNT;

    t_wf0 = DWT->CYCCNT;
    /* Waterfall: one row of WATERFALL_WIDTH px, each column mapped to
     * its FFT bin, colored through the shared (LUT-backed) palette.
     * Uses the frame-averaged dB, so the waterfall inherits the same
     * noise smoothing as the trace. Blitted just inside the panel
     * border, same x origin as the spectrum trace so columns line up
     * vertically between the two views. */
    for (x = 0; x < WATERFALL_WIDTH; x++) {
        uint32_t bin = ((uint32_t)x * FFT_BINS_IQ) / WATERFALL_WIDTH;
        line[x] = spectrum_colormap_index(s_db_frame[bin], s_db_min, s_db_max);
    }
    waterfall_push_line(line);
    waterfall_blit(SPEC_TRACE_X, WF_Y, spectrum_colormap_lut());
    t_wf1 = DWT->CYCCNT;
    } /* !s_menu_open - see this block's opening comment above */

    /* Frame drawn (or skipped, if the menu covered it): restart the
     * accumulator for the next window either way. */
    s_db_count = 0U;

    s_frame_count++;
    if (!s_menu_open && (s_frame_count % 30U) == 0U) {
        uint32_t spec_us = (uint32_t)(((uint64_t)(t_spec1 - t_spec0)) * 1000000U / SystemCoreClock);
        (void)spec_us;
        uint32_t wf_us   = (uint32_t)(((uint64_t)(t_wf1 - t_wf0)) * 1000000U / SystemCoreClock);
        (void)wf_us;
        debug_print_dec("sdr_tick: last fft (us)", fft_us);
        debug_print_dec("sdr_tick: spectrum_draw (us)", spec_us);
        debug_print_dec("sdr_tick: waterfall push+blit (us)", wf_us);
        debug_print_dec("sdr_tick: frame TOTAL (us)", fft_us + spec_us + wf_us);
        debug_print_dec_signed("sdr_tick: I(left) min", i_min);
        debug_print_dec_signed("sdr_tick: I(left) max", i_max);
        debug_print_dec_signed("sdr_tick: Q(right) min", q_min);
        debug_print_dec_signed("sdr_tick: Q(right) max", q_max);
        {
            /*
             * *** 05/08/2026, replaced with real counts *** - the
             * previous version here sampled SPI_STAT once per check
             * (~every 1.5s) and could only say "set" or "clear" - not
             * enough to tell "one glitch since the last check" from
             * "hundreds per second", and the person's own report
             * (continuous background noise, not occasional clicks)
             * needed that distinction. sdr_rx.c's DMA0_Channel3_
             * IRQHandler() and gd32_i2s.c's gd32_i2s_stream_write_
             * half() now count every real FERR occurrence cheaply,
             * once per audio block on each side, with no UART cost in
             * the ISR itself - this just reads and resets those
             * accumulators. A high count here (relative to how many
             * blocks ran in this window - roughly window_ms/2.667 at
             * either rate) means genuinely frequent frame errors, not
             * a rare fluke; a low or zero count despite what was seen
             * before means the earlier once-per-check sampling was
             * catching something closer to intermittent.
             */
            uint32_t rx_ferr_n = sdr_rx_get_ferr_count();
            (void)rx_ferr_n;
            uint32_t tx_ferr_n = gd32_i2s_get_tx_ferr_count();
            (void)tx_ferr_n;
            sdr_rx_reset_ferr_count();
            gd32_i2s_reset_tx_ferr_count();
            debug_print_dec("sdr_tick: RX (SPI1) FERR count since last check", rx_ferr_n);
            debug_print_dec("sdr_tick: TX (I2S1_ADD) FERR count since last check", tx_ferr_n);

            /*
             * *** 05/08/2026, added to test the "I/Q misalignment, not
             * missing data" theory *** - see sdr_rx.c's
             * sdr_rx_get_ferr_snapshot() comment for what this snapshot
             * is and why. Cross-correlates I[n] against Q[n+shift] for
             * a handful of small integer shifts over the captured
             * window - if the peak correlation sits at a NONZERO shift,
             * that's direct evidence I and Q are being read from
             * different sample instants (a channel/word slip), not
             * just noisy or missing data, which is what the panadapter
             * spectrum being fine despite audible corruption already
             * suggested but couldn't confirm on its own. Runs here in
             * the slow main loop (UART-affordable), not the ISR -
             * plain integer multiply/accumulate, no floating point
             * needed for a comparative peak-shift readout.
             *
             * *** 05/08/2026, FIXED after the first real capture ***:
             * the first version correlated the raw samples directly,
             * with no DC removal - real hardware logs showed values
             * dominated by a huge, nearly shift-independent DC term
             * (blocks with a big negative mean gave ~10^8-magnitude
             * "correlation" at EVERY shift, changing by only a few %
             * across the whole -3..+3 range - a flat offset artifact,
             * not a lag-dependent peak), and the "best shift" jumped
             * around inconsistently between captures (-2, +1, -3, +1)
             * with no repeating winner - exactly what pure DC/noise
             * would produce, and the opposite of what a real, fixed
             * hardware misalignment should look like (the same shift
             * winning every time). Now removes each window's own mean
             * from I and Q before correlating (a real covariance, not
             * a raw dot product), and only calls a shift "meaningful"
             * if it beats the runner-up by a clear margin (>2x) -
             * otherwise this reports "inconclusive" rather than
             * pointing at a shift that's really just noise dressed up
             * as a number. Needs several REPEATED captures showing the
             * SAME winning shift before trusting it as a real,
             * physical misalignment - one capture proves nothing
             * either way.
             */
            {
                static int16_t s_snap_i[64];
                static int16_t s_snap_q[64];
                const uint32_t n = 64U;

                if (sdr_rx_get_ferr_snapshot(s_snap_i, s_snap_q, n)) {
                    int32_t shift;
                    int32_t best_shift = 0;
                    int64_t best_mag = 0;
                    int64_t second_mag = 0;
                    int32_t i_n;
                    int32_t mean_i = 0;
                    int32_t mean_q = 0;

                    for (i_n = 0; i_n < (int32_t)n; i_n++) {
                        mean_i += s_snap_i[i_n];
                        mean_q += s_snap_q[i_n];
                    }
                    mean_i /= (int32_t)n;
                    mean_q /= (int32_t)n;

                    debug_print("sdr_tick: --- FERR snapshot captured, I/Q cross-"
                                "correlation vs shift (DC removed) ---\n");
                    for (shift = -3; shift <= 3; shift++) {
                        int64_t acc = 0;
                        uint32_t count = 0;
                        int32_t n_i;
                        for (n_i = 0; n_i < (int32_t)n; n_i++) {
                            int32_t q_i = n_i + shift;
                            int32_t iv, qv;
                            if (q_i < 0 || q_i >= (int32_t)n) {
                                continue;
                            }
                            iv = (int32_t)s_snap_i[n_i] - mean_i;
                            qv = (int32_t)s_snap_q[q_i] - mean_q;
                            acc += (int64_t)iv * (int64_t)qv;
                            count++;
                        }
                        if (count > 0U) {
                            acc /= (int64_t)count; /* normalize so different
                                                     * overlap lengths at the
                                                     * edges are comparable */
                        }
                        debug_print_dec_signed("sdr_tick:   shift", shift);
                        debug_print_dec_signed("sdr_tick:   cov(I[n], Q[n+shift])", (int32_t)acc);
                        {
                            int64_t mag = (acc < 0) ? -acc : acc;
                            if (mag > best_mag) {
                                second_mag = best_mag;
                                best_mag = mag;
                                best_shift = shift;
                            } else if (mag > second_mag) {
                                second_mag = mag;
                            }
                        }
                    }
                    if (best_shift == 0) {
                        debug_print("sdr_tick: peak covariance at shift=0 - no evidence "
                                    "of I/Q sample misalignment in this snapshot\n");
                    } else if (best_mag < (2 * second_mag)) {
                        debug_print("sdr_tick: peak covariance is NOT a clear outlier vs "
                                    "the runner-up shift - inconclusive, treat as noise "
                                    "unless the SAME shift keeps winning repeatedly\n");
                    } else {
                        debug_print_dec_signed("sdr_tick: *** clear peak covariance at "
                                                "NONZERO shift - possible I/Q misalignment, "
                                                "shift", best_shift);
                    }
                }
            }
        }
    }
}

static void led_gpio_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_8);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_8);
}

/*
 * --- Speaker PA enable/mute (PB7) ---------------------------------------
 *
 * Added 03/08/2026, per the project owner: a software mute for the
 * onboard speaker power amplifier, toggled from the HW-page tile in
 * the settings menu (see MENU_PAGE_HW in menu_grid_show()) - lets you
 * listen on headphones only, without the speaker PA also driving.
 *
 * *** PIN/POLARITY UNCONFIRMED (03/08/2026) ***
 * PB7 was given from memory ("PB7 creo" - not yet checked against a
 * schematic or bench-verified). Per this project's own established
 * debugging principle (see the AIC3204 I2S master/slave bring-up
 * history: never assume a fixed hardware mapping from memory, always
 * confirm against the datasheet/schematic or a live measurement)
 * this should be verified with a multimeter/scope BEFORE relying on
 * it - worst case if it's wrong is simply that the tile does nothing
 * audible (or drives an unrelated/floating pin), not damage, but it's
 * still unconfirmed. SPEAKER_PA_PIN below is the only place to change
 * if PB7 turns out wrong. SPEAKER_PA_ACTIVE_HIGH is the only place to
 * flip if the enable polarity turns out to be active-LOW instead
 * (defaulted here to active-HIGH, the more common convention for a
 * load-switch/amp EN pin - equally UNCONFIRMED).
 */
#define SPEAKER_PA_GPIO         GPIOB
#define SPEAKER_PA_GPIO_RCU     RCU_GPIOB
#define SPEAKER_PA_PIN          GPIO_PIN_7
#define SPEAKER_PA_ACTIVE_HIGH  1 /* 1 = pin HIGH enables the PA, 0 = active-LOW - see comment above */

static void speaker_pa_set_enabled(uint8_t on)
{
    s_speaker_pa_enabled = on ? 1U : 0U;
#if SPEAKER_PA_ACTIVE_HIGH
    if (s_speaker_pa_enabled) { gpio_bit_set(SPEAKER_PA_GPIO, SPEAKER_PA_PIN); }
    else                      { gpio_bit_reset(SPEAKER_PA_GPIO, SPEAKER_PA_PIN); }
#else
    if (s_speaker_pa_enabled) { gpio_bit_reset(SPEAKER_PA_GPIO, SPEAKER_PA_PIN); }
    else                      { gpio_bit_set(SPEAKER_PA_GPIO, SPEAKER_PA_PIN); }
#endif
}

static void speaker_pa_gpio_init(void)
{
    rcu_periph_clock_enable(SPEAKER_PA_GPIO_RCU);
    gpio_mode_set(SPEAKER_PA_GPIO, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPEAKER_PA_PIN);
    gpio_output_options_set(SPEAKER_PA_GPIO, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPEAKER_PA_PIN);
    /* Drive the pin to match s_speaker_pa_enabled's initializer above,
     * so the GPIO's actual level and the firmware's idea of the
     * speaker state agree from the very first instant it's an output
     * (rather than whatever the pin defaulted to before this ran). */
    speaker_pa_set_enabled(s_speaker_pa_enabled);
}

static void systick_delay_init(void)
{
    /* SysTick at a real 1ms, based on SystemCoreClock (updated by
     * SystemInit()/system_gd32f4xx.c). Timing-sensitive code such as
     * the panel's power-up sequence in rm68120_exmc.c relies on this
     * being calibrated correctly. */
    if (SysTick_Config(SystemCoreClock / 1000U)) {
        while (1) {
            /* SysTick configuration failed - should never happen */
        }
    }
}

void SysTick_Handler(void)
{
    g_msticks++;
    encoder_tick(); /* 1kHz quadrature/button sampling, see encoder.c */
}

/*
 * The default HardFault_Handler (Default_Handler, weakly defined in
 * the startup file) is a SILENT infinite loop. This version does
 * report over UART, so a real HardFault can be told apart from any
 * other kind of hang (e.g. an infinite loop in application code) just
 * by checking whether this message appears.
 *
 * It does not decode CFSR/HFSR (that needs a debugger attached and the
 * stack frame inspected) - for now it's just the "we ended up here"
 * signal, enough to confirm or rule out a hard fault as the cause.
 */
void HardFault_Handler(void)
{
    debug_print("\n*** HARDFAULT_HANDLER: a bus/access fault has occurred ***\n");
    while (1) {
        __NOP();
    }
}
