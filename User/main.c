#include "gd32f4xx.h"
/* Por CARGA_APP_BASE: el VTOR de abajo y el cargador tienen que sacar la
 * direccion base del MISMO sitio. Ver el comentario de SCB->VTOR. */
#include "cargador.h"
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
#include "idioma.h"
#include "ui_qth.h"
#include "font_num_20.h"
#include "ui_top.h"   /* ETAPA 2: cabecera y barra de estado */
#include "ui.h"
#include "waterfall.h"
#include "ipa_blit.h"
#include "lcd_dma.h"
#include "analizador.h"
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
#include "anotch.h"     /* notch automatico - etapa 35 */
#include "img_store.h"  /* almacen de imagenes en la flash externa - etapa 33 */
#include "settings.h"
#include "aic3204.h"
#include "config.h"
#include "rtty.h"
#include "cw.h" /* decodificador de CW - ver k_demod_modes[] y cw_poll() */
#include "rtty_scope.h"
#include "psk31.h"
#include "emisoras.h"
#include "mapa.h"
#include "zona_alta.h"
#include "ft8_decoder.h"
#include "ms5351.h"
#include "lo_gen_gd32.h"
#include "nb.h"
#include "dcf77.h"
#include "ui_hora.h"
#include "rf_lpf.h"
#include "encoder.h"
#include "dfu_salto.h"
#include "battery.h"
#include "backlight.h"
#include "arm_math.h" /* arm_fir_decimate_* - spectrum ZOOM, see spec_zoom_t's comment */
#include "demod_am.h"
#include "nco.h"
#include "rds.h"
#include "rds_pi.h"
#include "reloj.h"
#include "rtc_hw.h"   /* el RTC de hardware - ver reloj_ahora_ms() */
#include "navtex.h" /* decodificador de NAVTEX (SITOR-B) - ver k_demod_modes[] */
#include "wefax.h"  /* facsimil meteorologico - ver k_demod_modes[] y fax_panel_*() */
#include "sstv.h"   /* fotos por radio - mismo panel que el fax, en color */
#include "bmp.h"    /* y sus cabeceras, para guardarlas en el pendrive */
#include "ax25.h"   /* paquete APRS - el unico que va en FM, no en banda lateral */
#include "ft8_modo.h"  /* FT8 - ver k_demod_modes[] y ft8_poll() */
#include "hfdl_modo.h" /* HFDL - ver k_demod_modes[] y hfdl_modo_poll() */
#include "wspr_modo.h" /* WSPR - ver k_demod_modes[] y wspr_modo_poll() */
#include "ais_modo.h"  /* AIS  - ver k_demod_modes[] y ais_modo_poll()  */
#include "ale_modo.h"  /* ALE  - ver k_demod_modes[] y ale_modo_poll()  */
#include "jtty_modo.h" /* JTTY - ver k_demod_modes[] y jtty_modo_poll() */
#include "irq_prio.h" /* quien corta a quien, y por que - etapa 29 */
#include "gd32_i2s.h"
#include "sdr_rx.h"
#include "fft.h"
#include "spectrum.h"
#include "spec_agc.h" /* autoescala del espectro, ver su cabecera */
#include "nr_ss.h" /* NR strength control (RADIO page tile) - see ENCODER_TARGET_NR */
#include "splash_screen.h"
#include "aviones_pantalla.h"

static void led_gpio_init(void);
static void speaker_pa_gpio_init(void);
static void speaker_pa_set_enabled(uint8_t on);
static void systick_delay_init(void);
/*
 * EL BUCLE ENTERO, NO SOLO EL DIBUJO - 01/10/2026.
 *
 * *** El dueno: "pero sigue investigando". ***
 *
 * Comparando los dos cargadores salio esto, y es lo que obliga a medir mas:
 *
 *                  FFT  marco  traza(empuje)  cascada  medido  real   tope
 *   boot de serie    0    0      13 (0)          5      18 ms  24 ms  22/45
 *   boot nuestro     0    0      13 (0)          4      17 ms  31 ms  22/45
 *
 * Dibujar cuesta LO MISMO con los dos -13 ms de traza, 0 de empujar
 * pixeles- y nuestra cascada hasta cuesta menos. Hacemos MENOS trabajo
 * medido, 17 contra 18 ms. Y aun asi tardamos 31 ms por fotograma en vez
 * de 24, con la aplicacion pidiendo lo mismo en los dos casos.
 *
 * O sea que hay **8 ms por fotograma fuera de todo lo que se mide**. Los
 * cuatro contadores de arriba cubren el dibujo y nada mas; el resto de la
 * vuelta -tactil, mando, decodificadores, audio, esperas- no lo mira nadie.
 *
 * Estos tres cronometros cubren la vuelta ENTERA y la parten en tres: lo
 * que tarda todo, lo que se va en el tactil (que habla por el bus que
 * compartimos con la flash, y es el unico periferico que el cargador deja
 * distinto y la aplicacion usa en cada vuelta) y lo que se va en los poll
 * de los decodificadores. Lo que quede sin contar entre los tres y el
 * dibujo es la espera.
 *
 * Igual que con el USB: cuando no se sabe donde esta el tiempo, se pone un
 * cronometro, no una teoria.
 */
static volatile uint16_t s_bucle_us;    /* la vuelta entera */
static volatile uint16_t s_tactil_us;   /* demo_touch_poll() */
static volatile uint16_t s_polls_us;    /* los poll de los decodificadores */

static void radio_screen_draw(void);
static void sdr_spectrum_waterfall_tick(void);
static void franja_arriba_tick(void);
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
/* El filtro de dos cortes: lo usan la chapa de arriba y el espectro, que
 * van muy por delante de donde viven estas funciones. */
static void fil_cortes(uint16_t *lo, uint16_t *hi);
static void fil_format(char *b, uint16_t lo, uint16_t hi, const char *cola);
static void fil_pon(uint16_t lo, uint16_t hi);
/* El volcado de la zona alta avanza desde el bucle principal, que esta
 * mucho antes que donde vive - ver volcado_arranca(). */
static void volcado_poll(void);
static void prueba_poll(void);
static const spi_flash_alta_t *alta_mira(void);
static void grid_fill(void);
/* El rotulo del boton de guardar del fax. Se declara aqui porque
 * fax_hdr_draw() esta antes que el guardador en el fichero. */
static const char *gsv_rotulo_fax(void);
static void menu_modes_show(void);
static void menu_freq_keypad_show(void);
static void menu_time_keypad_show(void);
static void rf_agc_apply_pga(void);
/* Los ajustes que se guardan por tabla (23/09/2026) - ver settings.h. La
 * definicion vive abajo, junto al resto de lo de los ajustes; main() las
 * necesita aqui arriba, igual que rf_agc_apply_pga() justo debajo. */
static void hora_show(void);
static void qth_show(void);
static void hora_sintoniza(void);
static void barrido_arranca(void);
static void hora_poll(void);
static void hora_poll_rds(void);
static void hora_touch(uint16_t x, uint16_t y, uint8_t pressed);
static void qth_touch(uint16_t x, uint16_t y, uint8_t pressed);
static void qth_mando(int32_t pasos);
static void reloj_poner(uint8_t hh, uint8_t mm);
static void reloj_anclar(int16_t mins_locales, uint32_t atraso_ms);
static void extras_leer(settings_extra_t *e);
static void extras_aplicar(const settings_extra_t *e);
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
static void rds_poll(void);
static void rds_conmutar(void);
static void rds_marq_poll(void);
static void rds_reinicia(void);
static void cw_poll(void);
static void navtex_poll(void);
static void ft8_poll(void);
static void ft8_panel_draw(void);
static void ft8_panel_reinicia(void);
static void fax_panel_reinicia(void);
static void fax_poll(void);
static void sstv_panel_reinicia(void);
static void sstv_poll(void);
static void ax25_poll(void);
static void tics_init(void);
static void tics_poll(void);
static uint32_t s_tics_perdidos;
static uint8_t digi_panel_active(void);
static void rtty_scope_panel_reset(void);
static void rtty_text_panel_reset(void);
static void rtty_text_force_redraw(void);
static void psk31_poll(void);
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
/*
 * DOS ARRAYS Y NO UNO - 30/09/2026.
 *
 * *** Por el dueño del proyecto: "conforme le subo el suavizado el waterfall
 * y el espectro se ralentiza muchisimo". ***
 *
 *   s_db_frame   el cuadro CRUDO, promediado dentro del propio cuadro. Es
 *                lo que baja a la CASCADA.
 *   s_db_traza   el mismo, suavizado entre cuadros. Es lo que se DIBUJA
 *                como traza, y contra lo que se engancha un toque.
 *
 * Antes era uno solo: el suavizado se escribia ENCIMA de s_db_frame "para
 * que la traza y la cascada se vean igual", y la cascada se llevaba por
 * delante el suavizado entero.
 *
 * Y eso no es una preferencia, es romperle el eje a la cascada. El
 * suavizado es una media exponencial entre cuadros, o sea una constante de
 * tiempo: a 22 ms por cuadro, con el ajuste al 75 % son 76 ms -tres cuadros,
 * que es para lo que se hizo-, pero al 95 % son 429 ms, casi VEINTE cuadros.
 * Cada renglon de la cascada llevaba entonces veinte cuadros de historia
 * encima, asi que una rafaga que dura un renglon salia estirada en veinte y
 * sin contraste. La cascada existe justo para enseñar CUANDO pasa cada cosa.
 *
 * Y ademas era incoherente consigo misma: la velocidad "Muy rapida" saca sus
 * dos renglones de s_db_sum y de la foto de media tanda, que NO estan
 * suavizadas, asi que a esa velocidad la cascada ya salia nitida y a las
 * otras cuatro no.
 */
/*
 * LOS LIMITES DE SINTONIA. Estaban definidos mil lineas mas abajo, junto al
 * comentario que explica de donde salen (que sigue alli). Suben aqui porque
 * el bloque que restaura los ajustes los necesita para recortar vfo_hz, y
 * son dos numeros: dejarlos abajo obligaba a repetirlos.
 */
#define TUNE_MIN_HZ 30000UL
#define TUNE_MAX_HZ 180000000UL

static float   s_db_frame[FFT_BINS_IQ]; /* crudo: lo que baja a la cascada */
static float   s_db_traza[FFT_BINS_IQ]; /* suavizado: lo que se dibuja */
static uint8_t s_db_traza_lista = 0U;
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


static const char *banda_actual(uint32_t hz);
static const char *ajuste_valor(uint8_t id, char *buf);
static const char *ajuste_nombre(uint8_t id);
static void settings_value_redraw(void);
static void tema_cambiar(void);
static void paleta_cambiar(void);
static void idioma_cambiar(void);
/*
 * GRID_NADA y GRID_MODO no son rejillas paginadas: las dos pantallas que
 * nombran -Ajustes y Modo, igual que Bandas- van por la columna de
 * categorias de ui_cfg.c. Siguen en este enum porque es el identificador
 * que usa accion_pantalla() para los botones de la barra de abajo, que es
 * uno por pantalla y no uno por forma de dibujarla.
 */
typedef enum { GRID_NADA = 0, GRID_AJUSTES, GRID_BANDAS, GRID_MODO, GRID_PASOS, GRID_INFO,
               GRID_SSTV, GRID_HFDL } grid_pant_t;
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
 * Se pone a 1 la primera vez que radio_screen_draw() termina. Antes de eso
 * no hay pantalla que repintar: el arranque aplica el tema guardado mucho
 * antes de que exista nada dibujado, y pintar ahi seria pintar sobre el
 * logotipo de arranque.
 */
static uint8_t s_pantalla_pintada = 0U;

/* El texto de la marquesina de RDS y su desplazamiento. Viven aqui arriba
 * porque top_sync(), que esta muy por delante del RDS, los pasa a la
 * cabecera en cada cuadro. Ver el bloque de la marquesina mas abajo. */
static char    s_rds_txt[176];
static char    s_rds_nom[20];    /* el renglon fijo: la cadena, o su codigo */
static char    s_rds_txt_ant[176];
static int16_t s_rds_off;
static int16_t s_rds_txt_w;
static uint32_t s_rds_next_ms;
static uint8_t s_rds_hora_puesta = 0U;
static uint8_t s_rds_hora_ok = 0U;      /* 1 = confirmada, ya se puede enseñar */
static uint32_t s_rds_seq_ant = 0UL;
static uint16_t s_rds_cand_mins = 0U;   /* la primera hora, esperando confirmacion */
static uint32_t s_rds_cand_ms = 0UL;
static uint8_t  s_rds_cand_vale = 0U;
/* Cuantas horas han llegado y cuantas se han descartado por no cuadrar con
 * la anterior. Sin estos dos numeros, la pantalla dice "confirmando" tanto
 * si esta esperando la segunda como si lleva diez y ninguna cuadra, que son
 * dos averias distintas con el mismo aspecto. */
static uint16_t s_rds_ct_n = 0U;
static uint16_t s_rds_ct_mal = 0U;




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

/* ---------------------------------------------------------------------
 * LO QUE SE LEYO AL ARRANCAR. 25/09/2026.
 * ---------------------------------------------------------------------
 * El contador de guardados dice "1 ok, 0 mal": CONFIG.CSV se escribe bien.
 * Y aun asi los ajustes vuelven a su sitio de fabrica al encender. O sea
 * que el fallo esta en el otro extremo, al LEER, y ahi hay tres sitios
 * donde se puede perder: que el fichero no se encuentre, que la clave no
 * venga dentro, o que venga y no se aplique.
 *
 * Esto guarda lo que se leyo, tal cual, antes de aplicarlo. Sale en la
 * ventana de informacion. Con eso los tres casos se distinguen de un
 * vistazo en vez de seguir deduciendo, que hoy ya ha fallado dos veces. */
static int32_t s_arranque_wf   = -1;   /* -1 = la clave no venia en el fichero */
static int32_t s_arranque_tema = -1;
static uint16_t s_arranque_claves;     /* cuantas claves de tabla traia */

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

/*
 * ===========================================================================
 * LOS PASOS DE SINTONIA, EN UN SOLO SITIO
 * ===========================================================================
 * 23/09/2026. Hasta hoy un paso de sintonia estaba escrito en CUATRO
 * sitios distintos: los hercios en k_tune_steps[], el indice en los
 * #define BAND_STEP_*, la etiqueta en k_tune_step_labels_ui[] y la
 * descripcion en k_paso_desc[] (esta ultima a 5.000 lineas de las otras
 * tres). Anadir un paso queria decir acordarse de los cuatro, y de
 * renumerar a mano los BAND_STEP_* de debajo; equivocarse NO daba error
 * de compilacion, daba bandas que saltan mal.
 *
 * Es exactamente el fallo que ya nos ha mordido cinco veces en este
 * proyecto - el mismo dato escrito dos veces - asi que al anadir 10 Hz y
 * 500 Hz se arregla de raiz en vez de sumar un quinto sitio: la lista
 * esta AQUI y solo aqui, y las cuatro cosas salen de ella. El indice ya
 * no se escribe, lo cuenta el enum.
 *
 * Para anadir un paso: una linea aqui, en su sitio por orden creciente.
 * Nada mas.
 *
 *     X(sufijo del nombre, hercios, etiqueta, para que sirve)
 *
 * HASTA DONDE LLEGA DE VERDAD EL PASO DE 10 Hz. Por encima de 300 kHz
 * manda el MS5351 y no hay problema: su rejilla vale xtal/((2^20-1)*div),
 * o sea 2,4 Hz a 90 MHz y 0,2 Hz en HF. Por DEBAJO de 300 kHz manda
 * lo_gen_gd32.c, que es un temporizador con divisor ENTERO a 99,84 MHz:
 * su rejilla vale aproximadamente 2*f^2/99,84e6 -18 Hz a 30 kHz, 120 Hz
 * en DCF77 (77,5 kHz), 526 Hz en ALS162-, asi que ahi abajo ni el paso de
 * 10 Hz ni el de 100 se notan siempre. No es nuevo, ya pasaba con el de
 * 100 Hz; pero mas vale saberlo que pensar que la radio no obedece.
 */
/* La quinta columna es la descripcion en ingles (29/09/2026). La etiqueta
 * -"12,5 kHz"- NO se traduce: es un numero con unidad, y el formato de los
 * numeros se queda en espanol en los dos idiomas por decision del dueno. */
#define TUNE_STEPS_LISTA(X)                                                  \
    X(10HZ,        10UL,  "10 Hz",    "Batido y deriva", "Beat and drift")   \
    X(100HZ,      100UL,  "100 Hz",   "Ajuste fino",     "Fine tuning")      \
    X(500HZ,      500UL,  "500 Hz",   "Buscar en SSB",   "Searching in SSB") \
    X(1K,        1000UL,  "1 kHz",    "SSB",             "SSB")              \
    X(5K,        5000UL,  "5 kHz",    "Onda corta",      "Shortwave")        \
    X(10K,      10000UL,  "10 kHz",   "CB y onda media", "CB and mediumwave")\
    X(12K5,     12500UL,  "12,5 kHz", "Marina y 2 m",    "Marine and 2 m")   \
    X(25K,      25000UL,  "25 kHz",   "Aeronáutica",     "Aeronautical")     \
    X(100K,    100000UL,  "100 kHz",  "FM comercial",    "Broadcast FM")     \
    X(1M,     1000000UL,  "1 MHz",    "Saltos grandes",  "Big jumps")

/* Los indices: BAND_STEP_1K y companyia, que es lo que guardan las
 * presintonias de banda (band_preset_t) y config.h. Son constantes de
 * enumeracion, no #define con un numero a mano: mover una linea de la
 * lista de arriba los renumera solo. */
#define TUNE_STEP_X_IDX(suf, hz, et, desc, desc_en) BAND_STEP_##suf,
enum { TUNE_STEPS_LISTA(TUNE_STEP_X_IDX) TUNE_STEP_COUNT_ENUM };
#undef TUNE_STEP_X_IDX

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
 * channels (2m repeaters, airband).
 *
 * 23/09/2026: los valores ya no estan aqui, salen de TUNE_STEPS_LISTA
 * (un poco mas arriba), que es tambien de donde salen los indices, las
 * etiquetas y las descripciones. Ver alli el porque. */
#define TUNE_STEP_X_HZ(suf, hz, et, desc, desc_en) hz,
static const uint32_t k_tune_steps[] = { TUNE_STEPS_LISTA(TUNE_STEP_X_HZ) };
#undef TUNE_STEP_X_HZ
#define TUNE_STEP_COUNT (sizeof(k_tune_steps) / sizeof(k_tune_steps[0]))
_Static_assert((int)TUNE_STEP_COUNT == (int)TUNE_STEP_COUNT_ENUM,
               "la tabla de pasos y sus indices se han desincronizado");

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

/*
 * ============================================================================
 * NCO DE SINTONIA - 23/09/2026, a peticion del dueno del proyecto
 * ============================================================================
 * QUE HACE. Normalmente, cada clic del mando reprograma el MS5351: el
 * oscilador local se va contigo y, como el espectro se calcula de lo que sale
 * del mezclador, el panorama ENTERO se desplaza. Las emisoras vecinas se
 * corren y la historia del waterfall -esos 72 renglones de los ultimos
 * segundos- deja de corresponder con lo que hay debajo. Si viste una traza
 * interesante hace cinco segundos y te mueves hacia ella, la referencia se te
 * mueve mientras la persigues.
 *
 * Con el NCO puesto, el oscilador se queda APARCADO y lo unico que se mueve
 * es el punto de demodulacion dentro de la ventana ya capturada. El espectro
 * y el waterfall se quedan clavados y tu paseas un cursor por encima. Para
 * desenredar un trozo denso de banda es otra cosa.
 *
 * POR QUE SE PUEDE HACER. Media pieza ya estaba puesta: el oscilador NUNCA
 * ha estado en la frecuencia sintonizada, esta Fs/4 por debajo, y demod_am.c
 * ya deshacia esos Fs/4 digitalmente (ver la nota de LOW-IF TUNING de
 * demod_am.h). O sea que "demodular en un sitio distinto de donde esta el
 * oscilador" lleva funcionando desde julio. Lo unico que cambia es que esa
 * distancia deja de ser un numero fijo.
 *
 * HASTA DONDE LLEGA. La ventana capturada es de Fs de ancho, o sea +/-48 kHz
 * a 96 kHz y +/-24 kHz a 48 kHz. No se usa entera: cerca del borde esta el
 * filtro antialias del codec, asi que el paseo se limita a +/-40 kHz (+/-20
 * a 48 kHz). En cuanto la sintonia se sale de ahi, el oscilador se REAPARCA
 * y el panorama pega un salto - una vez, en el borde, en vez de en cada clic.
 *
 * LA FUGA DEL OSCILADOR SIGUE FUERA. Esa fuga se auto-mezcla a 0 Hz
 * SIEMPRE, y es la razon de que exista el desplazamiento de Fs/4 desde
 * julio: manteniendo la emisora lejos del centro, la fuga se queda fuera del
 * filtro de canal. Si el NCO dejara pasear libremente por toda la ventana,
 * al cruzar por encima del oscilador se oiria una portadora que no esta en
 * el aire. Por eso la distancia nunca baja de NCO_OFF_MIN_HZ: el paseo va de
 * 6 a 44 kHz POR ENCIMA del oscilador, nunca por debajo ni por el medio.
 *
 * Y POR ESO SE REAPARCA MIRANDO HACIA DONDE IBAS. Cuando la distancia se
 * sale por arriba -venias subiendo- el oscilador nuevo se pone dejando la
 * distancia en el MINIMO, o sea con toda la ventana por delante; por abajo,
 * al reves. Reaparcar siempre a la distancia clasica dejaria la emisora en
 * medio y volveria a saltar a mitad de camino. No es una elegancia: la
 * primera version hacia eso y el banco midio un salto cada 16 kHz subiendo
 * en vez de cada 38.
 */
/*
 * La ventana de paseo. El maximo lo pone el filtro antialias del codec: la
 * ventana capturada es de +/-Fs/2 y cerca del borde ya no hay señal fiable,
 * asi que se deja un margen. El minimo lo pone la fuga del oscilador local,
 * que se auto-mezcla a 0 Hz: el filtro de canal es de +/-4 kHz, asi que a
 * 6 kHz la fuga ya esta fuera con holgura.
 *
 * Entre los dos queda el recorrido que se puede hacer sin que el panorama se
 * mueva: 38 kHz a 96 kHz de muestreo, 16 kHz a 48.
 */
#define NCO_OFF_MIN_HZ    6000UL
#define NCO_OFF_MAX_96K  44000UL
#define NCO_OFF_MAX_48K  22000UL

/* El oscilador que hay PROGRAMADO de verdad, en hercios. Antes no existia
 * como variable: se recalculaba en cada sitio que lo necesitaba a partir de
 * s_tune_hz y del desplazamiento, porque la relacion entre los dos era fija.
 * Con el NCO ya no lo es, asi que hay que acordarse de donde se dejo. */
static uint32_t s_lo_hz = 0UL;

/* El ajuste: NCO si o no. */
static uint8_t  s_nco_on = 0U;

static uint32_t nco_off_max_hz(void)
{
    return s_nonwfm_use_48k ? NCO_OFF_MAX_48K : NCO_OFF_MAX_96K;
}

/*
 * Donde hay que poner el oscilador para sintonizar `freq_hz`.
 *
 * Sin NCO es la regla de siempre: freq - Fs/4. Con NCO, el que ya hay, salvo
 * que la sintonia se haya salido de la ventana - o que no haya ninguno aun-,
 * en cuyo caso se reaparca dejando la frecuencia nueva en la posicion clasica
 * de Fs/4. Reaparcar ahi y no en el centro tiene su razon: es la posicion en
 * la que la fuga del oscilador queda mas lejos de la banda de paso, o sea la
 * que se oye mejor, y desde ella quedan 16 kHz de paseo hacia abajo y 64 kHz
 * hacia arriba antes del siguiente salto.
 */
static uint32_t lo_para_sintonia(uint32_t freq_hz, uint8_t is_wfm)
{
    if (is_wfm) {
        return freq_hz;            /* WFM va centrada, sin desplazamiento */
    }
    if (!s_nco_on) {
        return freq_hz - demod_if_offset_hz();
    }
    /* La regla de aparcado vive en User/nco.c para poder medirla en el
     * simulador: es la unica parte de todo esto que se puede equivocar sin
     * que se oiga. Ver sim/ncotest.c. */
    return nco_lo_aparcado(freq_hz, s_lo_hz, demod_if_offset_hz(),
                           NCO_OFF_MIN_HZ, nco_off_max_hz());
}

/* Definida mucho mas abajo, junto a los externs del enlazador que usa. Se
 * declara aqui porque main() la llama en su segunda linea y tiene que ser
 * asi de pronto: mide desde antes de que se llame a nada. */
static void pila_pinta(void);

/* Carga sola la base de aviones si hay un ICAO24.BIN en el disco.
 * Definida mas abajo, con el resto de lo de la zona alta. */
static uint8_t datos_al_arrancar(void);
static void emisoras_tick(void);


/* El nombre y el renglon de la emisora identificada. Se llenan en
 * emis_pon() y los lee top_sync(), que esta antes en el fichero. */
static char s_emis_nom[48];
static char s_emis_txt[96];

/* El panel de FT8/HFDL/AIS enseña la lista o el mapa. Se declara aqui
 * arriba porque el cambio de modo -que lo apaga- esta antes en el fichero
 * que el dibujado. Ver ft8_mapa_pinta(). */
static uint8_t s_ft8_mapa;

/*
 * EL TROZO DE MUNDO QUE SE ESTA MIRANDO - 28/09/2026.
 *
 * *** El dueño: "quiero que se pueda hacer zoom dentro del mapa", "dos
 * botones encima del mapa abajo a la izquierda que sean + y - y con el
 * dedo que se pueda arrastrar el mapa". ***
 *
 * Vive aqui y no dentro de la funcion que pinta porque el zoom y el sitio
 * tienen que sobrevivir a los repintados: el mapa se repinta entero cada
 * vez que entra una señal nueva, y si la caja se construyera en cada
 * pintada, cada decodificacion te devolveria al mundo entero justo cuando
 * te habias acercado a mirar algo. Son 14 bytes.
 */
static mapa_caja_t s_mapa_caja;
static uint8_t     s_mapa_puesta;   /* 0 = aun no se ha colocado */

int main(void)
{
    /*
     * LA COMPROBACION DEL DFU YA NO ESTA AQUI. Se fue a SystemInit(), que
     * corre ANTES de montar el reloj: la ROM mide el cristal para sacar sus
     * 48 MHz y necesita encontrarlo parado. Ver el comentario gordo en
     * CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c.
     */

    /*
     * Critical when chained after a bootloader: our vector table is not at
     * 0x08000000 (that's the bootloader's) but at CARGA_APP_BASE. Without
     * this, any interrupt - our own SysTick included - would look up its
     * handler in the BOOTLOADER's vector table, not ours.
     *
     * *** Y AQUI PONIA 0x08020000 A PELO. 02/10/2026, y costo una radio
     * colgada. ***
     *
     * Al mover la aplicacion a 0x0800C000 cambie el VTOR de SystemInit()
     * y me deje ESTE, que corre despues y lo deshacia. El resultado es el
     * peor posible de depurar: el firmware enlaza, el cargador graba al
     * 100 %, salta... y la radio se queda con la pantalla del cargador
     * congelada, porque se estrella en la primera interrupcion con el
     * vector apuntando a mitad de su propio .text. Ni un mensaje.
     *
     * Lo gracioso es que el aviso estaba escrito: tools/mapa.py existe
     * justo para esto y decia "si alguien mueve la base y se deja ese, la
     * radio se cuelga en la primera interrupcion". Lo que no hacia era
     * BUSCAR los SCB->VTOR que no conocia de antemano - miraba uno solo,
     * en un fichero concreto-. Ahora los busca todos, en todo el arbol.
     *
     * Por eso ya no hay ningun numero escrito aqui: sale de CARGA_APP_BASE
     * (User/cargador.h), que es el mismo sitio del que lo saca el cargador.
     */
    SCB->VTOR = CARGA_APP_BASE;

    /* Lo segundo, y antes de que nadie llame a nada: pintar el hueco de la
     * pila para poder medir despues hasta donde llega. Ver pila_pinta(). */
    pila_pinta();

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
     * Y el reparto de prioridades, AQUI y no en cada sitio que enciende una
     * interrupcion. Tiene que ir ANTES de cualquier nvic_irq_enable(), porque
     * esa funcion lee el reparto vigente para saber como codificar el numero
     * que le pasan - y si no hay ninguno puesto, se inventa uno en silencio.
     * Ver irq_prio.h para la historia completa. */
    nvic_priority_group_set(IRQ_GRUPO);

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

    /* El acelerador grafico, antes de que se pinte la primera cascada. Si no
     * responde se queda apagado solo y todo sigue por el camino de la CPU.
     * Ver ipa_blit.h. */
    ipa_blit_init();

    /* Y el DMA que empuja los pixeles al panel. Hace una autoprueba contra
     * RAM -sin tocar la pantalla- y si no cuadra se queda apagado solo. Ver
     * lcd_dma.h. */
    lcd_dma_init();

    touch_init();
    debug_print_hex32("RCU_PLLI2S after touch_init", RCU_PLLI2S);

    spi_flash_init(); /* unconditional - settings_load()/settings_poll() need this every boot, not just under the SPI_FLASH_PROBE_TEST diagnostics below */

    /*
     * El RTC, antes de que nadie pregunte la hora. Si ya venia andando de un
     * reinicio anterior no lo toca -lo sabe por una marca en el dominio de
     * respaldo- asi que reflashear no borra la hora. Ver rtc_hw.c.
     */
    rtc_hw_init();

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
    /*
     * EL POR DEFECTO PRIMERO, EL FICHERO DESPUES, y en ese orden justo para
     * que manden los ajustes: si CONFIG.CSV trae clave "grid",
     * settings_load() pisa esto en la linea siguiente. Una radio recien
     * flasheada se queda con IN80dk. Ver ft8_decoder_grid_por_defecto().
     */
    ft8_decoder_grid_por_defecto();
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
    /* La tabla del NCO, antes de que nadie pueda mezclar con el. Si esto
     * faltara, la tabla seria todo ceros y el desplazamiento digital
     * multiplicaria la señal por cero: silencio absoluto en AM/SSB/NFM, con
     * el espectro pintandose tan campante. Va aqui, pegado a demod_am_init(),
     * porque es quien lo usa. */
    nco_init();
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
    /* Y los diecisiete de tabla (23/09/2026). Van AQUI, al final del bloque
     * de aplicar: para entonces ya han corrido demod_am_init(), sdr_rx_init(),
     * spectrum_init(), touch_init() y el apply_demod_mode() de mas arriba, o
     * sea que todos los setters a los que llama esto tienen detras un modulo
     * ya en pie. Y antes de apply_lo_tune()/s_settings_ready_for_autosave,
     * para que nada de esto cuente como un cambio del usuario que haya que
     * volver a guardar. */
    /* Antes de aplicar: ver s_arranque_wf. */
    {
        const settings_extra_t *e = &s_loaded_settings.extra;
        uint32_t k;

        for (k = 0U; k < (uint32_t)SET_X_N; k++) {
            if ((e->presentes & ((uint64_t)1U << k)) != 0U) { s_arranque_claves++; }
        }
        s_arranque_wf   = ((e->presentes & ((uint64_t)1U << SET_X_WFVEL)) != 0U)
                          ? e->v[SET_X_WFVEL] : -1;
        s_arranque_tema = s_loaded_settings.have_tema_idx
                          ? (int32_t)s_loaded_settings.tema_idx : -1;
    }

    extras_aplicar(&s_loaded_settings.extra);

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
        /*
         * RECORTADA, como todo lo demas de este bloque - 30/09/2026.
         *
         * Era el unico valor de aqui que entraba sin recortar (el volumen,
         * el PGA, el suavizado y el atenuador ya lo hacian), y ademas rompe
         * dos invariantes que estan escritas en este mismo fichero:
         *
         *   lo_para_sintonia() hace `freq_hz - demod_if_offset_hz()` sin
         *   guarda, y su comentario dice que es seguro "gracias al
         *   invariante TUNE_MIN_HZ > DEMOD_IF_OFFSET_HZ, para evitar un
         *   underflow de uint32_t". Con vfo_hz por debajo de 24 kHz en el
         *   fichero, el uint32 da la vuelta a ~4,29 GHz y la radio arranca
         *   sorda, sin sintonizar nada y marcando 0 Hz.
         *
         *   y sam_current_ppm_error() divide por s_tune_hz, con un
         *   comentario que dice "s_tune_hz is always > 0 (TUNE_MIN_HZ
         *   enforces that), so no divide-by-zero guard needed". Con
         *   `vfo_hz,0` o `vfo_hz,` en el fichero, ya no lo enforzaba nadie.
         *
         * Todos los sitios por donde el usuario sintoniza recortan a
         * [TUNE_MIN_HZ, TUNE_MAX_HZ]; este es el camino por el que entraba
         * un numero sin pasar por ninguno de ellos.
         */
        uint32_t v = s_loaded_settings.vfo_hz;

        if (v < (uint32_t)TUNE_MIN_HZ) { v = (uint32_t)TUNE_MIN_HZ; }
        if (v > (uint32_t)TUNE_MAX_HZ) { v = (uint32_t)TUNE_MAX_HZ; }
        s_tune_hz = v;
    }
    apply_lo_tune(s_tune_hz);

#if CALIB_HEIGHT_TEST
    calib_height_ruler_draw();
#else
    splash_screen_draw(); /* personalizable splash screen, see splash_screen.c */

    /*
     * Y si hay un ICAO24.BIN en el disco, se carga entero AQUI, con su
     * propia pantalla, ANTES de pintar la radio.
     *
     * *** El dueno: "solo deberia de haber cargado la pantalla de
     * copiando". *** El primer intento pintaba la radio y le plantaba
     * encima un recuadro; esto no pinta la radio hasta que ha terminado,
     * asi que no hay nada a medias en pantalla ni hay que repintar el
     * espectro despues para quitar un cartel.
     *
     * Mirar si el fichero esta cuesta 8 ms cuando no esta, que es en
     * todos los arranques menos uno (medido en el banco de FAT, con el
     * chip simulado a nivel de pin).
     */
    (void)datos_al_arrancar();

    /*
     * Y se abre lo que haya. Las dos lecturas juntas cuestan menos de una
     * decima de segundo: el directorio son 144 bytes y cada cabecera unas
     * decenas. Si ahi no hay nada -que es lo normal hasta que el usuario
     * carga el fichero- las dos se van sin hacer ruido.
     */
    (void)zona_alta_abre(spi_flash_read);
    {
        uint32_t addr = 0UL;

        if (zona_alta_donde(ZA_TIPO_EMISORAS, &addr, (uint32_t *)0)) {
            (void)emisoras_abre(addr, spi_flash_read);
        }
    }

    radio_screen_draw(); /* full radio UI, all readouts included */
#endif

    /* From here on, apply_lo_tune()/apply_demod_mode() calls mean a
     * REAL user-driven change (encoder, BANDS, keypad, mode menu) -
     * see s_settings_ready_for_autosave's own comment for why this
     * has to wait until after the two boot-time calls just above. */
    s_settings_ready_for_autosave = 1U;

    debug_print("main: entering the main loop\n");

    while (1) {
        uint32_t t_vuelta0 = DWT->CYCCNT;
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

            /* La franja de arriba, ANTES de repartir entre espectro y panel
             * digital: es de los dos, no de uno. Ver franja_arriba_tick(). */
            franja_arriba_tick();
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
                /* FT8 no usa ni el osciloscopio ni la rejilla de texto de
                 * RTTY: su panel se repinta entero y ya esta. Vale para las
                 * dos transiciones -entrar en FT8 y cerrarse el menu-
                 * porque en las dos lo que hay debajo no sirve. */
                if (ft8_modo_activo() || hfdl_modo_activo() || wspr_modo_activo()
                    || ais_modo_activo() || ale_modo_activo()
                    || jtty_modo_activo()) {
                    ft8_panel_reinicia();
                } else {
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
            }
            s_rtty_scope_was_active = drawing_now;
            s_rtty_mode_was_active = active_now;

            if (drawing_now && (ft8_modo_activo() || hfdl_modo_activo()
                                || wspr_modo_activo() || ais_modo_activo()
                                || ale_modo_activo() || jtty_modo_activo())) {
                /* FT8 se queda con el panel entero: ni osciloscopio ni
                 * espectro. Ver FT8_PANEL_H. */
                ft8_panel_draw();
            } else if (drawing_now && (wefax_activo() || sstv_activo())) {
                /* El fax y el SSTV se pintan solos, en fax_poll() y
                 * sstv_poll(): una linea cada vez que el decodificador cierra
                 * una, no una vez por cuadro. Aqui solo hay que no pintar el
                 * espectro encima. */
            } else if (drawing_now) {
                rtty_scope_draw();
            } else {
                /* Covers BOTH "not in RTTY mode" and "menu is open" -
                 * sdr_spectrum_waterfall_tick() already skips its own
                 * drawing internally while s_menu_open, so it's always
                 * safe to call here regardless of which of those two
                 * reasons drawing_now was false for. */
                sdr_spectrum_waterfall_tick();
            }
            {
                uint32_t t0 = DWT->CYCCNT;
                demo_touch_poll();
                /*
                 * SOLO SI SE ESTA DIBUJANDO LA RADIO.
                 *
                 * *** La primera lectura del dueno fue "b65 t65", 65 ms de
                 * bucle y 65 de tactil. *** Y era mentira: la tomo dentro
                 * del menu, y ahi demo_touch_poll() no solo lee el bus,
                 * tambien despacha el toque y REPINTA la pagina. Estaba
                 * midiendo el menu, no la radio.
                 *
                 * El mismo fallo que tenia la fila Cascada, que decia 0/60
                 * dentro del menu. Un cronometro que se lee en un sitio
                 * distinto de donde se usa no mide nada.
                 */
                if (!s_menu_open) {
                    uint32_t d = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000000U);
                    s_tactil_us = (uint16_t)((d > 65535U) ? 65535U : d);
                }
            }
            tune_encoder_poll();
        }
        { uint32_t t_polls0 = DWT->CYCCNT;
        hora_poll();   /* pantalla de sincronizar la hora, si esta abierta */
        rf_agc_poll(); /* RF-level (analog PGA) auto-AGC - see its own comment */
        rtty_poll(); /* drains rtty.c's decoded text to debug UART - see its own comment */
        cw_poll();   /* lo mismo para el CW, al mismo panel - ver su comentario */
        navtex_poll(); /* y para el NAVTEX, etapa 25 */
        psk31_poll();  /* y para el PSK31, etapa 37 - al mismo panel de texto */
        ax25_poll();   /* y para el APRS, etapa 28 - tambien texto */
        ft8_modo_tick(); /* el trabajo caro del FT8: FFT y decodificacion - etapa 30 */
        ft8_poll();      /* y sus mensajes al panel de texto */
        /*
         * Y el Viterbi de HFDL, que se queda pendiente en la interrupcion
         * del audio y se corre AQUI. Ver hfdl_modo_poll(): es con diferencia
         * lo mas caro de HFDL y no cabe en un manejador de DMA. Si no hay
         * rafaga pendiente no hace nada, asi que llamarlo siempre sale
         * gratis.
         */
        hfdl_modo_poll();
        if (!s_menu_open) {
            uint32_t d = (DWT->CYCCNT - t_polls0) / (SystemCoreClock / 1000000U);
            s_polls_us = (uint16_t)((d > 65535U) ? 65535U : d);
        }
        }
        /*
         * Y WSPR. Aqui no hay nada caro por vuelta: mira el reloj y, dos
         * veces cada dos minutos, abre o cierra una captura. Lo caro -la
         * correlacion de sincronismo y el decodificador de Fano- pasa
         * dentro de wspr_modo_poll() UNA vez cada dos minutos, al cerrar.
         * Son unos cientos de milisegundos, y por eso no esta en la
         * interrupcion del audio: ahi cortaria el sonido.
         */
        wspr_modo_poll();
        /*
         * Y AIS. Aqui no hay nada caro: saca del anillo las tramas que la
         * interrupcion ya cerro con el CRC bueno y las desempaqueta. AIS no
         * tiene Viterbi ni nada que se le parezca - su unica proteccion es
         * ese CRC-, asi que desempaquetar son cuatro desplazamientos.
         */
        ais_modo_poll();
        /*
         * Y ALE. La frecuencia se le pasa en cada vuelta y no al sintonizar:
         * asi la que se apunta junto a cada indicativo es la que habia
         * cuando se le oyo, sin que ale_modo.c tenga que enterarse de nada
         * de la radio.
         */
        ale_modo_frec_pon(s_tune_hz / 1000UL);
        ale_modo_poll();
        jtty_modo_poll();
        tics_poll();   /* vigila que el reloj no pierda milisegundos - etapa 29 */
        volcado_poll();  /* un bloque de 4 kB por vuelta - ver volcado_arranca() */
        prueba_poll();   /* la prueba del almacen - ver prueba_arranca() */
        fax_poll();    /* el WEFAX pinta imagen, no texto - etapa 26 */
        sstv_poll();   /* y el SSTV, en color - etapa 27 */
        rds_poll();      /* y el RDS, que pone el reloj en hora si la emisora la manda */
        rds_marq_poll(); /* y corre la marquesina de la cabecera */
        {
            /* Los de tabla se recogen justo aqui, en una variable de pila:
             * settings.c no puede leer los estaticos de este fichero. */
            settings_extra_t extra;
            extras_leer(&extra);
            settings_poll(s_tune_hz, demod_am_get_mode(), k_tune_steps[s_tune_step_idx], demod_am_get_audio_bw(), s_volume_db_x2, s_nonwfm_use_48k,
                          ((s_pga_hf_x2 >= 0) ? s_pga_hf_x2 : s_pga_gain_db_x2), spectrum_smooth_pct_for_save(), s_speaker_pa_enabled, s_att_suelo, s_tema_idx, &extra);
        } /* debounced CONFIG.CSV autosave - see settings.h's comment; cheap no-op most iterations */
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
        if (!s_menu_open) {
            uint32_t d = (DWT->CYCCNT - t_vuelta0) / (SystemCoreClock / 1000000U);
            s_bucle_us = (uint16_t)((d > 65535U) ? 65535U : d);
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
/* 23/09/2026: "Bandas" se sube a la cabecera como pastilla (ver s_top.band) y
 * su hueco de la barra lo ocupa "Func", que engancha el mando a un ajuste.
 * El widget se conserva porque la barra identifica los botones por puntero. */
static ui_button_t s_btn_func;
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
    ENCODER_TARGET_CW_TONE,
    ENCODER_TARGET_FILTRO,
    /*
     * El mando enganchado a un ajuste CUALQUIERA de la lista - 23/09/2026,
     * por el dueno del proyecto, que lo queria como el boton FUNC de su
     * Yaesu: pulsas Func, eliges un ajuste tocandolo, vuelves a la principal
     * y el mando ya mueve ese ajuste, viendo el efecto en directo.
     *
     * Los ajustes de rango continuo -volumen, ganancia, brillo...- NO pasan
     * por aqui: ya tenian su destino propio en esta lista desde hace tiempo,
     * y engancharlos es sencillamente ponerlo. Este destino es para los OTROS,
     * los que son una lista de estados (tema, paleta, estilo, zoom...) y que
     * hasta ahora solo se podian cambiar tocandolos.
     *
     * Cual de ellos esta enganchado lo dice s_encoder_ajuste. Son dos
     * preguntas distintas -"que mueve el mando" y "como lo mueve"- y por eso
     * son dos variables.
     */
    ENCODER_TARGET_AJUSTE
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
/*
 * OJO: el valor por defecto de verdad NO esta aqui, esta en spectrum.c
 * (s_line_smooth_passes). 23/09/2026: aqui ponia 0 y alli 3, o sea DOS
 * valores por defecto para el MISMO ajuste, y no coincidian: la pantalla de
 * Ajustes decia "Contorno 0" mientras el espectro se dibujaba con tres
 * pasadas de suavizado. Mientras el ajuste no se guardaba, la mentira era
 * solo cosmetica; desde que se guarda (23/09/2026, CONFIG.CSV) se volvio
 * real, porque al arrancar se le manda a spectrum.c lo que main.c creia.
 *
 * Se arranca leyendo el getter en el arranque, asi que el valor por defecto
 * vive en UN solo sitio y los dos dicen lo mismo pase lo que pase.
 */
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
/* Los dos #define SUBIERON al principio del fichero el 30/09/2026, porque
 * hacen falta antes: el bloque que restaura los ajustes -mil lineas mas
 * arriba- recorta vfo_hz con ellos. El razonamiento de por que valen esto es
 * el de aqui arriba y se queda aqui. */

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

/* Los BAND_STEP_* ya no se escriben a mano aqui: son constantes de
 * enumeracion generadas por TUNE_STEPS_LISTA, mucho mas arriba en este
 * mismo fichero (junto a s_tune_step_idx). Antes eran diez #define con
 * su numero puesto a dedo, y anadir un paso obligaba a renumerarlos sin
 * que nada avisara si se colaba un despiste. */
/* Las mismas, escritas de verdad, para la cabecera y la barra de acciones,
 * que ya usan fuente proporcional. Las de arriba se quedan para las
 * pantallas que siguen dibujando con la 5x7 de ancho fijo, donde "12,5 kHz"
 * ni cabe ni se lee. Mismo orden e indices: si una crece, la otra tambien. */
#define TUNE_STEP_X_ET(suf, hz, et, desc, desc_en) et,
static const char *k_tune_step_labels_ui[] = { TUNE_STEPS_LISTA(TUNE_STEP_X_ET) };
#undef TUNE_STEP_X_ET
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
    const char *label;   /* sigla del modo: AM, USB, FT8... no se traduce */
    texto_t     desc;
    demod_mode_t mode;
    rtty_variant_t rtty_variant;
    uint8_t cw;                 /* 1 = ademas enciende el decodificador de CW */
    uint8_t navtex;             /* 1 = ademas enciende el de NAVTEX */
    uint8_t fax;                /* 1 = ademas enciende el de WEFAX */
    uint8_t sstv;               /* 1 = ademas enciende el de SSTV */
    uint8_t ax25;               /* 1 = ademas enciende el de paquete AX.25 */
    uint8_t ft8;                /* 1 = ademas enciende el de FT8 */
    uint8_t hfdl;               /* 1 = ademas enciende el de HFDL */
    uint8_t wspr;               /* 1 = ademas enciende el de WSPR */
    uint8_t ais;                /* 1 = ademas enciende el de AIS */
    uint8_t ale;                /* 1 = ademas enciende el de ALE */
    uint8_t psk31;              /* 1 = ademas enciende el de PSK31 */
    uint8_t jtty;               /* 1 = ademas enciende el de JTTY */
    uint8_t fam;                /* categoria de la columna: MODO_FAM_* */
} demod_mode_entry_t;

/*
 * LAS DOS CATEGORIAS DE LA COLUMNA - 24/09/2026, por el dueño del
 * proyecto: "en la ventana de modo, igual que la de bandas, en categorias
 * analogico y digital".
 *
 * El corte es por lo que SALE, no por lo que hay debajo. CW, RTTY, NAVTEX,
 * WEFAX y SSTV son USB por debajo, igual que USB a secas, y APRS es NFM
 * igual que NFM a secas - pero lo que entrega uno es sonido para el oido y
 * lo que entrega el otro es texto o una imagen. Quien busca "el modo para
 * oir la emisora" y quien busca "el modo que me saca el mapa" no buscan lo
 * mismo, y esa es la unica division que le sirve a ninguno de los dos.
 */
#define MODO_FAM_ANA 0U
#define MODO_FAM_DIG 1U
#define MODO_FAM_COUNT 2U

static const texto_t k_modo_familias[MODO_FAM_COUNT] = {
    T("Analógico", "Analogue"), T("Digital", "Digital")
};

static const demod_mode_entry_t k_demod_modes[] = {
    { "AM", T("Amplitud modulada", "Amplitude mod."),   DEMOD_MODE_AM,  RTTY_VARIANT_NONE,     0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_ANA },
    { "SAM", T("AM síncrona", "Synchronous AM"),         DEMOD_MODE_SAM, RTTY_VARIANT_NONE,     0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_ANA }, /* synchronous AM, 21/08/2026 - see sam.h */
    { "USB", T("Banda lateral alta", "Upper sideband"),  DEMOD_MODE_USB, RTTY_VARIANT_NONE,     0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_ANA },
    { "LSB", T("Banda lateral baja", "Lower sideband"),  DEMOD_MODE_LSB, RTTY_VARIANT_NONE,     0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_ANA },
    { "NFM", T("FM estrecha", "Narrow FM"),         DEMOD_MODE_NFM, RTTY_VARIANT_NONE,     0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_ANA },
    { "WFM", T("FM ancha", "Wide FM"),            DEMOD_MODE_WFM, RTTY_VARIANT_NONE,     0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_ANA },
    { "CW", T("Telegrafía Morse", "Morse telegraphy"),    DEMOD_MODE_USB, RTTY_VARIANT_NONE,     1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_DIG }, /* ver el comentario de arriba */
    { "RTTY-L", T("Teletipo en LSB", "Teletype on LSB"),     DEMOD_MODE_LSB, RTTY_VARIANT_NORMAL,   0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_DIG }, /* confirmed correct polarity on LSB, 08/08/2026 */
    { "RTTY-U", T("Teletipo en USB", "Teletype on USB"),     DEMOD_MODE_USB, RTTY_VARIANT_INVERTED, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_DIG }, /* USB mirrors LSB - see this block's comment */
    /*
     * NAVTEX, etapa 25. Otro oyente colgado de USB, como el CW: por debajo
     * es banda lateral alta y lo que anade es el decodificador de SITOR-B.
     * Va en USB porque es lo que usan los receptores de NAVTEX y porque asi
     * la polaridad de los dos tonos sale derecha; si alguna emisora se
     * escuchara del reves, el conmutador de navtex.c lo arregla sin cambiar
     * de modo.
     */
    { "NAVTEX", T("Avisos marítimos", "Marine warnings"),     DEMOD_MODE_USB, RTTY_VARIANT_NONE, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_DIG },
    /*
     * WEFAX, etapa 26. El cuarto oyente colgado de USB. A diferencia de los
     * otros tres, lo que saca no es texto sino una imagen, asi que se lleva
     * el panel entero - ver fax_panel_linea().
     */
    { "WEFAX", T("Mapas del tiempo", "Weather charts"),      DEMOD_MODE_USB, RTTY_VARIANT_NONE, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_DIG },
    /*
     * SSTV, etapa 27. Como el fax, pero en color y con el sincronismo metido
     * en la propia señal, asi que no hay nada que ajustar a mano.
     */
    { "SSTV", T("Fotos por radio", "Pictures over radio"),       DEMOD_MODE_USB, RTTY_VARIANT_NONE, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_DIG },
    /*
     * APRS, etapa 28. EL UNICO QUE NO VA EN BANDA LATERAL: el paquete de
     * VHF se transmite en FM estrecha, asi que por debajo lleva NFM y el
     * audio le llega del discriminador de FM, no del camino de banda
     * lateral como a los otros cinco. Ver demod_am.c, donde se engancha en
     * un sitio distinto y por eso.
     */
    { "APRS", T("Paquete AX.25", "AX.25 packet"),         DEMOD_MODE_NFM, RTTY_VARIANT_NONE, 0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_DIG },
    /*
     * FT8, etapa 30. Vuelve a banda lateral alta, como los cuatro de arriba,
     * pero se diferencia de todos ellos en una cosa que manda en todo lo
     * demas: NECESITA LA HORA. Una emision de FT8 dura 12,64 s dentro de una
     * ranura de 15, y las ranuras empiezan en los segundos 0, 15, 30 y 45 de
     * cada minuto UTC. Sin reloj en hora no hay nada que decodificar, y por
     * eso el panel dice "sin hora" en vez de quedarse mudo (ver
     * ft8_modo_hay_hora()).
     *
     * Y la segunda diferencia: FT8 VIVE EN LA MEMORIA DE LA CASCADA. Las dos
     * cosas no pueden estar a la vez (ver ft8_shared_ram.h), asi que
     * encender FT8 apaga la cascada de verdad, no solo la deja de pintar.
     * Eso es justo lo que hace ft8_modo_stop() al volver: reinicia la
     * cascada, porque lo que hay en ese buffer ya no son pixeles.
     */
    { "FT8", T("Digital de HF", "HF digital"),         DEMOD_MODE_USB, RTTY_VARIANT_NONE, 0U, 0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_DIG },
    /*
     * HFDL, etapa 31. Los datos de los aviones en onda corta.
     *
     * VA EN BANDA LATERAL ALTA como FT8, pero por debajo no se parecen en
     * nada. FT8 necesita la hora porque sus ranuras empiezan en segundos
     * fijos; HFDL no necesita reloj para nada - las rafagas llegan cuando
     * llegan y el preambulo dice donde empieza cada una.
     *
     * Y COMPARTE LA MEMORIA DE LA CASCADA, igual que FT8 (ver
     * ft8_shared_ram.h). Los tres no pueden estar a la vez, y por eso
     * hfdl_modo_start() comprueba si FT8 la tiene cogida antes de nada.
     *
     * DONDE ESCUCHAR: las estaciones de tierra reparten por bandas segun la
     * hora y la propagacion. De dia suelen dar 11.327, 13.276 y 13.312 kHz;
     * de noche 8.927, 8.942 y 6.559. Es USB, y el ancho de filtro ancho -no
     * hay que centrar nada a mano: el mezclador se busca la subportadora.
     */
    { "HFDL", T("Datos de aviones", "Aircraft data"),      DEMOD_MODE_USB, RTTY_VARIANT_NONE, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, MODO_FAM_DIG },
    /*
     * WSPR, etapa 34. Las balizas de onda corta: 200 mW cruzando un oceano.
     *
     * EL TERCERO QUE COMPARTE LA MEMORIA DE LA CASCADA (ver
     * ft8_shared_ram.h). Su espectrograma son 46 kB y solo existen mientras
     * el modo esta puesto; wspr_modo_start() se niega a arrancar si otro la
     * tiene cogida, igual que hace HFDL.
     *
     * NECESITA LA HORA como FT8, y mas todavia: una emision dura 110,6
     * segundos y solo empieza en los minutos PARES. Con el reloj mal, la
     * captura arranca donde no toca y no sale nada. La busqueda aguanta algo
     * mas de un segundo de desfase en cada sentido (ver WSPR_RX_ANTES_MS),
     * no minutos.
     *
     * Y LA DIFERENCIA QUE MAS SE NOTA AL USARLO: esto no escucha, ESPERA.
     * Entre emision y emision hay dos minutos en los que no puede pasar
     * nada, y por eso la barra es una cuenta atras y no un indicador de
     * enganche. Una pantalla quieta durante dos minutos es lo normal aqui,
     * no un cuelgue - y por eso la chapa dice "espera" y no se queda muda.
     *
     * DONDE: 14.095,6 kHz (20 m) es la banda con mas trafico de dia. El
     * boton de frecuencias recorre las demas. Es USB, y la ventana de WSPR
     * cae 1.500 Hz por encima de la portadora, que es justo donde la pone
     * el receptor (WSPR_RX_CENTRO_HZ).
     *
     * *** CORRECCION, 28/09/2026. Aqui ponia "la tabla lleva esa cuenta
     * hecha, o sea que las frecuencias de k_wspr_canales[] son las de
     * SINTONIA, no las del dial que publican las listas". Es FALSO y se vio
     * al montar la tabla de JTTY: 14.095,6 es exactamente el dial que
     * publican todas las listas de WSPR, no el dial mas 1.500. La tabla
     * lleva DIALES, igual que la de FT8 y por lo mismo - se sintoniza el
     * dial y la señal aparece a 1.500 Hz de audio, que es donde el receptor
     * pone su ventana-.
     *
     * No se ha tocado ni un numero: los numeros estaban bien y funcionan en
     * antena. Lo que estaba mal era el comentario, que es peor, porque el
     * siguiente que copie la tabla creyendolo le suma 1.500 Hz a todo. ***
     */
#if WSPR_MODO
    { "WSPR", T("Balizas de HF", "HF beacons"),         DEMOD_MODE_USB, RTTY_VARIANT_NONE, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, MODO_FAM_DIG },
#endif
    /*
     * AIS, etapa 35. Los barcos.
     *
     * EL SEGUNDO QUE NO VA EN BANDA LATERAL. Como el APRS, AIS es FM: la
     * senal es GMSK -que es FSK con las esquinas redondeadas- y el audio le
     * llega del discriminador, no del camino de banda lateral. Por eso
     * lleva DEMOD_MODE_NFM debajo.
     *
     * Y COMO EL APRS, TAMPOCO NECESITA NADA DE LA HORA: las rafagas llegan
     * cuando llegan.
     *
     * COMPARTE LA MEMORIA DE LA CASCADA con los otros tres (ver
     * ft8_shared_ram.h). La tabla de barcos y sus renglones son 1.776 bytes
     * y en la SRAM propia no cabian: el enlazador desbordo por 996. Ahi
     * caben de sobra y ademas no se pierde nada, porque en este modo el
     * panel se lleva la pantalla entera y la cascada no se veria igual.
     *
     * PERO SI NECESITA UN FILTRO DE CANAL MAS ANCHO que el de NFM. AIS
     * ocupa 14,4 kHz por la regla de Carson y el de NFM corta a 6,25:
     * pasaria la senal por un embudo y cerraria el ojo antes de llegar al
     * discriminador. Lo conmuta demod_am_ais_chf(), abajo, al entrar y
     * salir del modo.
     *
     * DONDE: 161,975 MHz (canal A) y 162,025 (canal B), a 50 kHz una de
     * otra. Con una sola cadena de FI se oye una cada vez y el boton
     * conmuta; los barcos alternan entre las dos, asi que en cualquiera se
     * ve la mitad del trafico - y en un puerto la mitad sobra. Que esto
     * entre en el margen del sintonizador (TUNE_MAX_HZ, 180 MHz) lo dijo el
     * dueno cuando yo di por hecho que no: "ais es en 160 mhz que yo sepa
     * esta radio llega a 200mhz". Tenia razon.
     */
    { "AIS", T("Barcos", "Ships"),                DEMOD_MODE_NFM, RTTY_VARIANT_NONE, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, MODO_FAM_DIG },
    /*
     * ALE, etapa 36. Quien anda por la banda.
     *
     * Vuelve a banda lateral alta, como FT8 y WSPR. Es 8-FSK a 125 baudios:
     * las estaciones militares y de gobierno de onda corta emiten cada
     * pocos minutos un "sondeo" con su indicativo, barriendo canales, para
     * que las demas sepan por donde se las oye. Eso es la mayor parte de lo
     * que hay en el aire y es lo que esto decodifica.
     *
     * NO NECESITA LA HORA -las emisiones llegan cuando llegan, como HFDL y
     * AIS- y SI comparte la memoria de la cascada, como los otros cuatro.
     *
     * SIN BOTON DE FRECUENCIAS, y a proposito. FT8, WSPR, HFDL y AIS tienen
     * canales publicados y fijos; ALE no: se usa en toda la onda corta y
     * cada red tiene los suyos. Poner una lista aqui seria inventarmela, y
     * una lista de frecuencias inventada en una radio es peor que no tener
     * lista - se pasan horas en un canal muerto creyendo que es el bueno.
     * Se busca con el mando, como toda la vida.
     *
     * LO QUE SI TIENE ES LA COLUMNA DE CALIDAD: los votos unanimes de la
     * votacion 2 de 3, de 0 a 48. Es el numero que dice cuanto fiarse de un
     * indicativo, y hace falta porque ALE no lleva CRC: el Golay corrige
     * hasta tres errores por mitad y, cuando no puede, a veces no se entera.
     */
    { "ALE", T("Sondeos de HF", "HF soundings"),         DEMOD_MODE_USB, RTTY_VARIANT_NONE, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 0U, 0U, MODO_FAM_DIG },
    /*
     * PSK31, etapa 37. Conversacion escrita en 31 hercios.
     *
     * *** Por el dueño del proyecto: "hay que implementar psk31". ***
     *
     * Es el modo de charla de la onda corta: dos personas escribiendose,
     * en directo, con la potencia de una bombilla. No lleva correccion de
     * errores ni indicativos empaquetados - es texto y ya esta, letra a
     * letra segun se teclea.
     *
     * NO ENTRA EN EL GRUPO DE LOS CINCO que se reparten la memoria de la
     * cascada: le caben 467 bytes propios y no necesita mas, asi que se
     * enciende y se apaga como el NAVTEX, por su cuenta.
     *
     * SALE POR EL PANEL DE TEXTO, el mismo del RTTY y el NAVTEX, y no por
     * la tabla de renglones de FT8/WSPR/AIS/ALE. La diferencia no es de
     * gusto: aquellos entregan REGISTROS -un indicativo, un localizador,
     * una potencia- y estos entregan un chorro de letras que puede cortar
     * una palabra por la mitad. Una tabla obligaria a inventarse donde
     * empieza y acaba cada fila.
     *
     * Y LLEVA EL OSCILOSCOPIO, como el RTTY y el CW, porque hay que
     * sintonizar a mano: el margen es +-7,8 Hz y eso en una ventana de
     * 3 kHz no se encuentra a ojo. La chapa de al lado enseña el error de
     * frecuencia medido en hercios con signo - se gira hasta que se acerca
     * a cero.
     */
    { "PSK31", T("Charla en fase", "Phase-shift chat"),        DEMOD_MODE_USB, RTTY_VARIANT_NONE, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 0U, MODO_FAM_DIG },
    /*
     * JTTY, etapa 37. El teletipo nuevo del equipo de WSJT-X, estrenado en
     * la v3.2.0-rc1 de septiembre de 2026.
     *
     * VA EN BANDA LATERAL ALTA como FT8, WSPR y ALE, y COMPARTE LA MEMORIA
     * DE LA CASCADA con ellos (ver ft8_shared_ram.h): su espectrograma y el
     * Viterbi de 512 estados son 29 kB y en la SRAM propia quedaban 152
     * bytes. Son ya SEIS modos repartiendose los mismos 54 kB.
     *
     * Y LA DIFERENCIA QUE MANDA EN TODO LO DEMAS: NO NECESITA LA HORA, Y NO
     * PORQUE LE DE IGUAL, SINO PORQUE NO HAY RANURAS. FT8 y WSPR empiezan
     * en segundos fijos del reloj y su receptor puede dormir entre medias;
     * JTTY empieza cuando el otro le da a la tecla y dura lo que dure. Eso
     * obliga a buscar el sincronismo CONTINUAMENTE, cada media fila de
     * simbolo, y a tener siempre una trama entera de pasado guardada por si
     * la que acaba de terminar era buena. Ver jtty_rx.h.
     *
     * ES 4-GFSK A 31,25 BAUDIOS EN 127 Hz: unas 60 palabras por minuto,
     * pensado para concursos. Un mensaje son de una a dieciseis tramas
     * seguidas de 34 bits, que jtty_modo.c junta por frecuencia porque dos
     * estaciones pueden estar emitiendo A LA VEZ.
     *
     * SIN BOTON DE FRECUENCIAS, por lo mismo que ALE: el modo tiene un mes
     * y no hay frecuencias de llamada asentadas que yo haya podido
     * comprobar. Una lista inventada no se lee como una suposicion.
     *
     * DONDE MIRAR, MIENTRAS TANTO: la ventana de busqueda son 500 Hz
     * alrededor de 1.500 Hz de audio, asi que basta con poner la emision
     * mas o menos en medio del filtro y el receptor la encuentra - dice el
     * desvio en su columna para poder afinar-.
     */
    { "JTTY", T("Teletipo digital", "Digital teletype"),      DEMOD_MODE_USB, RTTY_VARIANT_NONE, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, MODO_FAM_DIG }
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
    texto_t     label;
    const char *rango;    /* escrito tal cual se pinta: "5.800-6.200 kHz" */
    /*
     * Los mismos limites en hercios, para saber EN QUE BANDA estas - lo usa
     * la pastilla de banda de la cabecera (23/09/2026). 0,0 en las entradas
     * que no son un rango sino frecuencias sueltas (la de señales horarias).
     *
     * Son el rango escrito dos veces, una para leer y otra para calcular. Eso
     * se separa solo, asi que NO se editan a mano: los genera y los comprueba
     * tools/bandas_check.py a partir del texto, que es el que manda. Va en
     * `make comprueba`.
     */
    uint32_t lo_hz, hi_hz;
    uint32_t freq_hz;
    demod_mode_t mode;
    uint8_t step_idx; /* index into k_tune_steps[]/k_tune_step_labels[] - see BAND_STEP_* above */
    uint8_t familia;  /* BAND_FAM_* */
} band_preset_t;

/* El NUMERO manda: es el orden en que salen las familias en la columna de la
 * izquierda. Aficionados primero, por el dueno del proyecto. */
#define BAND_FAM_HAM   0U   /* aficionados */
#define BAND_FAM_BCST  1U   /* radiodifusion */
#define BAND_FAM_UTIL  2U   /* utilidades: lo de HF */
/*
 * LA CUARTA, 30/09/2026: VHF y UHF.
 *
 * "Utilidades" habia llegado a 19 bandas y la rejilla pinta 16
 * (UIC_DCOLS x UIC_DROWS). Las tres ultimas estaban en la tabla y NO SE
 * PODIAN TOCAR: no salian, sin aviso. El comentario de ui_cfg.h decia
 * "ninguna familia necesita paginar" y habia dejado de ser verdad.
 *
 * Y llevaba asi desde que se anadio APRS, callado, porque el banco que lo
 * vigila (sim/bandas2.c) mide una tabla que genera tools/bandas_dump.py, y
 * make solo la regenera cuando cambia el .py. El .py no se habia tocado en
 * meses, asi que el banco media una foto vieja de 46 bandas. Se descubrio el
 * 30/09 al tocar ese fichero por la traduccion.
 *
 * Se parte en dos en vez de paginar o de borrar bandas: el corte cae solo
 * -por debajo de 30 MHz es otra radio que por encima- y deja 11 y 8, o sea
 * sitio de sobra en las dos.
 */
#define BAND_FAM_VHF   3U   /* VHF y UHF */
#define BAND_FAM_COUNT 4U

static const band_preset_t k_band_presets[] = {
    /* ---- aficionados, Region 1 ---------------------------------------- */
    { T("2200 m", "2200 m"), "135,7-137,8 kHz",    135700UL, 137800UL, 136500UL,    DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_HAM },
    { T("630 m", "630 m"),  "472-479 kHz",        472000UL, 479000UL, 475500UL,    DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_HAM },
    { T("160 m", "160 m"),  "1.810-2.000 kHz",    1810000UL, 2000000UL, 1840000UL,   DEMOD_MODE_LSB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("80 m", "80 m"),   "3.500-3.800 kHz",    3500000UL, 3800000UL, 3750000UL,   DEMOD_MODE_LSB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("60 m", "60 m"),   "5.351-5.367 kHz",    5351000UL, 5367000UL, 5357000UL,   DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("40 m", "40 m"),   "7.000-7.200 kHz",    7000000UL, 7200000UL, 7150000UL,   DEMOD_MODE_LSB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("30 m", "30 m"),   "10.100-10.150 kHz",  10100000UL, 10150000UL, 10120000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("20 m", "20 m"),   "14.000-14.350 kHz",  14000000UL, 14350000UL, 14250000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("17 m", "17 m"),   "18.068-18.168 kHz",  18068000UL, 18168000UL, 18130000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("15 m", "15 m"),   "21.000-21.450 kHz",  21000000UL, 21450000UL, 21250000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("12 m", "12 m"),   "24.890-24.990 kHz",  24890000UL, 24990000UL, 24950000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("10 m", "10 m"),   "28,0-29,7 MHz",      28000000UL, 29700000UL, 28400000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("6 m", "6 m"),    "50-52 MHz",          50000000UL, 52000000UL, 50150000UL,  DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_HAM },
    { T("2 m", "2 m"),    "144-146 MHz",        144000000UL, 146000000UL, 145500000UL, DEMOD_MODE_NFM, BAND_STEP_12K5, BAND_FAM_HAM },

    /* ---- radiodifusion ------------------------------------------------ */
    { T("OL", "LW"),     "148-284 kHz",        148000UL, 284000UL, 198000UL,    DEMOD_MODE_AM,  BAND_STEP_1K,   BAND_FAM_BCST },
    { T("OM", "MW"),     "526-1.606 kHz",      526000UL, 1606000UL, 1000000UL,   DEMOD_MODE_AM,  BAND_STEP_1K,   BAND_FAM_BCST },
    { T("120 m", "120 m"),  "2.300-2.495 kHz",    2300000UL, 2495000UL, 2400000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("90 m", "90 m"),   "3.200-3.400 kHz",    3200000UL, 3400000UL, 3300000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("75 m", "75 m"),   "3.900-4.000 kHz",    3900000UL, 4000000UL, 3950000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("60 m", "60 m"),   "4.750-5.060 kHz",    4750000UL, 5060000UL, 4900000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("49 m", "49 m"),   "5.800-6.200 kHz",    5800000UL, 6200000UL, 6000000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("41 m", "41 m"),   "7.200-7.450 kHz",    7200000UL, 7450000UL, 7300000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("31 m", "31 m"),   "9.400-9.900 kHz",    9400000UL, 9900000UL, 9600000UL,   DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("25 m", "25 m"),   "11.600-12.100 kHz",  11600000UL, 12100000UL, 11800000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("22 m", "22 m"),   "13.570-13.870 kHz",  13570000UL, 13870000UL, 13700000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("19 m", "19 m"),   "15.100-15.830 kHz",  15100000UL, 15830000UL, 15400000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("16 m", "16 m"),   "17.480-17.900 kHz",  17480000UL, 17900000UL, 17650000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("15 m", "15 m"),   "18.900-19.020 kHz",  18900000UL, 19020000UL, 18950000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("13 m", "13 m"),   "21.450-21.850 kHz",  21450000UL, 21850000UL, 21600000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },
    { T("11 m", "11 m"),   "25.670-26.100 kHz",  25670000UL, 26100000UL, 25800000UL,  DEMOD_MODE_AM,  BAND_STEP_5K,   BAND_FAM_BCST },

    /* ---- utilidades ---------------------------------------------------- */
    { T("NDB", "NDB"),    "190-535 kHz",        190000UL, 535000UL, 350000UL,    DEMOD_MODE_USB, BAND_STEP_1K,   BAND_FAM_UTIL },
    /*
     * Las tres frecuencias de NAVTEX, 24/09/2026. 518 kHz es la
     * internacional y va en ingles; 490 kHz es la misma idea en el idioma
     * del pais; 4209,5 kHz es la de onda corta, que llega mas lejos.
     *
     * Van en USB porque es la convencion del receptor de NAVTEX. El modo que
     * hay que elegir despues es "NAVTEX" en la pantalla de modos - la banda
     * deja el aparato en el sitio, no enciende el decodificador, igual que
     * elegir la banda de 40 metros no enciende el RTTY.
     */
    { T("NAVTEX", "NAVTEX"), "518 kHz",            518000UL, 518000UL, 518000UL,    DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_UTIL },
    { T("NAVTEX-N", "NAVTEX-N"),"490 kHz",            490000UL, 490000UL, 490000UL,    DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_UTIL },
    { T("NAVTEX-HF", "NAVTEX-HF"),"4.209,5 kHz",        4209500UL, 4209500UL, 4209500UL, DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_UTIL },
    /*
     * Las tres del DWD (Pinneberg), 24/09/2026. Son las emisiones de fax
     * meteorologico que se reciben bien desde España, y tienen horario
     * publicado y fijo: por eso son las buenas para probar el decodificador,
     * porque se puede volver a intentar mañana a la misma hora.
     *
     * La frecuencia que se pone aqui es la de la PORTADORA. En banda lateral
     * alta, el tono de la imagen cae 1,9 kHz por encima, que es donde el
     * decodificador lo busca. Si se sintonizara el centro del tono, la imagen
     * saldria toda blanca o toda negra.
     */
    { T("FAX 4", "FAX 4"),  "DWD 3.855 kHz",       3855000UL, 3855000UL, 3855000UL,  DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_UTIL },
    { T("FAX 8", "FAX 8"),  "DWD 7.880 kHz",       7880000UL, 7880000UL, 7880000UL,  DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_UTIL },
    { T("FAX 14", "FAX 14"), "DWD 13.882,5 kHz",    13882500UL, 13882500UL, 13882500UL, DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_UTIL },
    /*
     * SSTV, 24/09/2026. 14,230 MHz es la frecuencia de llamada de imagen en
     * la banda de 20 metros y es donde esta casi todo el trafico que se puede
     * recibir desde aqui. La de 40 metros (7,165) se usa menos pero entra de
     * noche, cuando 20 metros ya esta cerrada.
     *
     * A diferencia del fax, NO hay que afinar la sintonia con cuidado: el
     * decodificador se entera solo de que modo viene por la cabecera, pero si
     * el tono no cae donde debe los colores salen lavados. La frecuencia que
     * va aqui es la de la portadora, como siempre en banda lateral.
     */
    { T("SSTV 20", "SSTV 20"),"14.230 kHz",         14230000UL, 14230000UL, 14230000UL, DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_UTIL },
    { T("SSTV 40", "SSTV 40"),"7.165 kHz",          7165000UL, 7165000UL, 7165000UL,  DEMOD_MODE_USB, BAND_STEP_100HZ, BAND_FAM_UTIL },
    /*
     * APRS, 24/09/2026. 144,800 MHz es la frecuencia de paquete de toda
     * Europa: posiciones de estaciones fijas y moviles, partes
     * meteorologicos, globos sonda y repetidores que reenvian lo de los
     * demas. La de 432,500 es la secundaria, mucho menos usada.
     *
     * Van en FM ESTRECHA, no en banda lateral, y por eso el modo que hay que
     * elegir despues -"APRS"- lleva NFM por debajo en vez de USB. Aqui se
     * sintoniza la frecuencia de verdad, sin desplazamiento de ningun tipo.
     */
    { T("APRS", "APRS"),   "144,800 MHz",        144800000UL, 144800000UL, 144800000UL, DEMOD_MODE_NFM, BAND_STEP_5K, BAND_FAM_VHF },
    { T("APRS 70", "APRS 70"),"432,500 MHz",        432500000UL, 432500000UL, 432500000UL, DEMOD_MODE_NFM, BAND_STEP_5K, BAND_FAM_VHF },
    { T("Horaria", "Time"),"2,5 / 5 / 10 MHz",   0UL, 0UL, 10000000UL,  DEMOD_MODE_AM,  BAND_STEP_1K,   BAND_FAM_UTIL },
    { T("CB", "CB"),     "26,965-27,405 MHz",  26965000UL, 27405000UL, 27185000UL,  DEMOD_MODE_AM,  BAND_STEP_10K,  BAND_FAM_UTIL },
    /*
     * Las DOS bandas de FM comercial que existen en el mundo. 23/09/2026.
     *
     * CCIR es la de siempre, 87,5 a 108 MHz, la que usa toda Europa
     * occidental, America y casi todo lo demas.
     *
     * 23/09/2026, a peticion del dueno: la de siempre se vuelve a llamar
     * "FM" a secas -que es lo que es para casi todo el mundo, y el nombre
     * es lo que sale tambien en la pastilla de la cabecera, donde cabe
     * poco- y la otra se queda con su sigla, "FM OIRT", que es como la
     * llama todo el que la busca. CCIR sigue escrito en el subtitulo de la
     * de siempre, que es donde no estorba.
     *
     * OIRT es la que uso el bloque del Este: 65,8 a 74 MHz. El nombre viene
     * de la organizacion de radiodifusion de esos paises, que eligio esa
     * banda cuando la occidental ya estaba repartida. Casi todas las
     * emisoras se han mudado ya a 87,5-108, pero quedan en Rusia, Bielorrusia
     * y Moldavia - y sobre todo, es el sitio clasico para cazar esporadica E
     * en verano: a 66 MHz la capa se abre antes y mas veces que a 100, asi
     * que cuando entra algo de 2.000 km ahi se oye primero. Por eso vale la
     * pena tenerla aunque de dia este casi siempre vacia.
     *
     * El paso es de 10 kHz y no de 100 como en CCIR: las asignaciones de
     * OIRT no estan en una rejilla de 100 kHz (hay emisoras en 66,44 o en
     * 70,19), asi que con 100 kHz se pasarian de largo.
     */
    { T("FM OIRT", "FM OIRT"),"65,8-74 MHz",        65800000UL, 74000000UL, 66300000UL,  DEMOD_MODE_WFM, BAND_STEP_10K,  BAND_FAM_VHF },
    { T("FM", "FM"),     "CCIR 87,5-108 MHz",  87500000UL, 108000000UL, 100000000UL, DEMOD_MODE_WFM, BAND_STEP_100K, BAND_FAM_VHF },
    { T("Aérea", "Air"),  "108-137 MHz",        108000000UL, 137000000UL, 118000000UL, DEMOD_MODE_AM,  BAND_STEP_25K,  BAND_FAM_VHF },
    { T("Meteo", "Weather"),  "137-138 MHz",        137000000UL, 138000000UL, 137500000UL, DEMOD_MODE_WFM, BAND_STEP_12K5, BAND_FAM_VHF },
    { T("Marina", "Marine"), "156-162 MHz",        156000000UL, 162000000UL, 156800000UL, DEMOD_MODE_NFM, BAND_STEP_12K5, BAND_FAM_VHF },
    { T("VHF alta", "VHF high"),"162-174 MHz",       162000000UL, 174000000UL, 165000000UL, DEMOD_MODE_NFM, BAND_STEP_12K5, BAND_FAM_VHF }
};

static const texto_t k_band_familias[BAND_FAM_COUNT] = {
    T("Aficionados", "Amateur"), T("Radiodifusión", "Broadcast"),
    T("Utilidades", "Utility"), T("VHF/UHF", "VHF/UHF")
};
#define BAND_PRESET_COUNT (sizeof(k_band_presets) / sizeof(k_band_presets[0]))

/* ---------------------------------------------------------------------
 * DONDE SE DEJO CADA BANDA. 25/09/2026.
 * ---------------------------------------------------------------------
 * Cada preset trae una frecuencia de entrada fija, y en las bandas de
 * fonia esa entrada esta ARRIBA a proposito, que es donde esta la gente:
 * 40 m entra en 7.150 de un rango que acaba en 7.200. Correcto para
 * fonia, y un fastidio si andabas en 7.040 con SSTV o en 14.074 con FT8:
 * tocar la banda te escupe al otro extremo y hay que volver girando.
 *
 * El dueno lo dijo asi: "cada vez que le doy a una banda me manda al final
 * de la banda... antes me mandaba al punto donde la habia dejado".
 *
 * Asi que se recuerda. Al salir de una banda se apunta donde estabas -con
 * su modo y su paso, que tambien son parte de "donde lo deje"- y al volver
 * se va ahi. La primera vez que se entra, y solo esa, manda la entrada del
 * preset.
 *
 * EN RAM Y NO EN CONFIG.CSV: son 49 bandas, y meterlas en el fichero
 * significaria 49 claves mas en un CSV que ya mide 1.536 bytes fijos.
 * "Donde lo deje" es una comodidad de la sesion en curso; al apagar, la
 * radio vuelve a la frecuencia guardada y las bandas a sus entradas, que
 * es un sitio conocido y no una sorpresa. Si algun dia hace falta que
 * sobreviva al apagado, el sitio es la zona alta de la flash, no el CSV.
 *
 * Coste: 49 x 8 bytes.
 */
typedef struct {
    uint32_t     hz;     /* 0 = en esta banda no se ha estado todavia */
    demod_mode_t modo;
    uint8_t      paso;
} banda_visita_t;

static banda_visita_t s_banda_visita[BAND_PRESET_COUNT];

/* Apunta la sintonia actual en TODAS las bandas que la contienen. Son
 * todas y no solo una a proposito: los rangos se solapan -el 40 m de
 * aficionados cae dentro del 41 m de radiodifusion- y adivinar en cual
 * "esta de verdad" seria inventarse una respuesta. Apuntandola en las dos,
 * cualquiera de las dos te devuelve donde estabas. */
static void banda_apunta(void)
{
    uint16_t i;

    for (i = 0U; i < (uint16_t)BAND_PRESET_COUNT; i++) {
        if ((s_tune_hz >= k_band_presets[i].lo_hz) &&
            (s_tune_hz <= k_band_presets[i].hi_hz)) {
            s_banda_visita[i].hz   = s_tune_hz;
            s_banda_visita[i].modo = demod_am_get_mode();
            s_banda_visita[i].paso = s_tune_step_idx;
        }
    }
}
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
/* Que ajuste tiene enganchado el mando: un AJ_* de la lista de ajustes, o
 * AJ_ENGANCHE_NINGUNO cuando el mando esta en la sintonia. Ver el destino
 * ENCODER_TARGET_AJUSTE para por que son dos variables y no una. */
#define AJ_ENGANCHE_NINGUNO 0xFFU
static uint8_t s_encoder_ajuste = AJ_ENGANCHE_NINGUNO;

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

/*
 * Cual de los dos cortes del filtro mueve el mando. Mismo reparto que la
 * escala del espectro y por la misma razon: son dos numeros del mismo
 * ajuste, y darles dos celdas separadas seria partir en dos algo que se
 * toca siempre junto. El boton del mando cambia de uno a otro.
 */
static uint8_t s_fil_ajusta_hi = 1U;   /* arranca en el de arriba, que es el que mas se toca */
static char    s_fil_det[16];
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
/*
 * EL DESFASE, AHORA EN MILISEGUNDOS. 24/09/2026.
 *
 * Era en minutos, y en minutos el reloj no puede saber en que SEGUNDO va: el
 * cambio de minuto caia donde cayera g_msticks al arrancar, o sea en un
 * instante cualquiera. Con el desfase en milisegundos, si alguien sabe donde
 * esta el borde de minuto de verdad, el reloj puede ponerse ahi - y eso es lo
 * que sabe el RDS (ver reloj_anclar()).
 *
 * Poner la hora a mano sigue funcionando EXACTAMENTE igual que antes: el
 * teclado solo sabe horas y minutos, asi que deja el desfase en un multiplo
 * de 60000 y la fase dentro del minuto se queda donde estaba. Quien teclea
 * 21:47 no esta diciendo nada sobre los segundos, y el reloj no se lo
 * inventa.
 */
static uint32_t s_time_offset_ms = 0U;

/*
 * 1 = el desfase esta ANCLADO a un borde de minuto real, o sea que el segundo
 * tambien es bueno. 0 = solo el minuto (que es lo que da el teclado). Esta
 * separado del desfase porque son dos cosas distintas: cuanto vale el reloj,
 * y cuanto vale lo que sabemos de el. Enseñar segundos sin esto seria dar por
 * bueno un numero que nadie ha medido.
 */
static uint8_t s_hora_anclada = 0U;

/*
 * AHORA HAY RTC. 24/09/2026.
 *
 * Hasta hoy la hora era "milisegundos desde el arranque mas un desfase", y
 * eso tiene dos problemas: se va -el SysTick sale del HXTAL, que no sigue al
 * tiempo real- y se pierde entera en cada reinicio.
 *
 * El GD32F450 lleva un RTC con su cristal de 32.768 Hz, que estaba en la
 * placa y sin usar: el driver del fabricante se enlazaba y no lo llamaba
 * nadie. rtc_hw.c (portado de la rama hfdl del proyecto padre, ver su
 * cabecera) lo arranca. Un cristal de esos se va unas decenas de ppm, o sea
 * unos segundos al DIA, contra los minutos que se iba lo anterior.
 *
 * SE MANTIENE EL DESFASE COMO RESPALDO. Si el cristal no arranca -placa con
 * el cristal mal soldado, o los condensadores de carga equivocados- el reloj
 * sigue funcionando como antes en vez de quedarse parado. Eso no es
 * prudencia gratuita: rtc_hw_lxtal_failed() existe precisamente porque a
 * ellos les paso.
 */
static uint32_t reloj_ahora_ms(void)
{
    extern volatile uint32_t g_msticks;
    rtc_hw_datetime_t dt;

    if (rtc_hw_lxtal_failed()) {
        return reloj_ms_del_dia(g_msticks, s_time_offset_ms);
    }
    rtc_hw_get(&dt);
    return (((uint32_t)dt.hour * 3600UL) + ((uint32_t)dt.minute * 60UL)
            + (uint32_t)dt.second) * 1000UL;
}

/*
 * Pone el RTC en hora conservando la fecha que ya tuviera.
 *
 * La fecha no la sabe nadie aqui: el teclado solo pide horas y minutos, y el
 * reloj del RDS tampoco la manda. Asi que se lee lo que hay, se cambia la
 * hora y se devuelve. El dia que un decodificador horario traiga fecha -el
 * DCF77 la lleva- se podra poner entera.
 */
static void rtc_pon_hora(uint8_t hh, uint8_t mm, uint8_t ss)
{
    rtc_hw_datetime_t dt;

    if (rtc_hw_lxtal_failed()) { return; }
    rtc_hw_get(&dt);
    dt.hour = hh;
    dt.minute = mm;
    dt.second = ss;
    rtc_hw_set(&dt);
}

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
    uint32_t total_min = reloj_ahora_ms() / 60000UL;
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
    /* Solo la parte que se mueve. Esto corre en CADA fotograma mientras
     * entra señal -la barra no para quieta- y repintar los 800 px de la
     * franja costaba 32.051 accesos al bus; los 284 de la izquierda, 11.379.
     * Ver ui_top_draw_smeter(). */
    ui_top_draw_smeter(&s_top);
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
 *               the BW badge callback's own guard comment already
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
/*
 * Las siglas del chip de AGC de la barra de estado. Tres letras, iguales en
 * los dos idiomas a proposito: son ingles ya -OFF/SLoW/MEDium/FaST-, se leen
 * igual en espanol, y el chip mide lo que mide. Los nombres LARGOS de la
 * celda de Ajustes si se traducen: ver k_agc_nombres[].
 */
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
 * que la unica respuesta correcta mira los CUATRO campos (NAVTEX tambien
 * es USB por debajo, etapa 25; WEFAX, etapa 26; y SSTV, etapa 27. Y APRS,
 * etapa 28, que va en FM y por tanto comparte demodulador con NFM).
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
                     k_demod_modes[i].cw == cw_get_enabled() &&
                     k_demod_modes[i].navtex == navtex_activo() &&
                     k_demod_modes[i].fax == wefax_activo() &&
                     k_demod_modes[i].sstv == sstv_activo() &&
                     k_demod_modes[i].ax25 == ax25_activo() &&
                     k_demod_modes[i].ft8 == ft8_modo_activo() &&
                     k_demod_modes[i].hfdl == hfdl_modo_activo() &&
                     k_demod_modes[i].wspr == wspr_modo_activo() &&
                     k_demod_modes[i].ais  == ais_modo_activo() &&
                     k_demod_modes[i].ale  == ale_modo_activo() &&
                     k_demod_modes[i].jtty == jtty_modo_activo() &&
                     k_demod_modes[i].psk31 == psk31_activo());
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
    case ENCODER_TARGET_FILTRO:     return UI_KNOB_SCALE_LO;
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
    /* Fuera de toda banda la pastilla dice "Bandas" en vez de esfumarse
     * (23/09/2026). Es el UNICO camino que queda a la lista desde que el
     * boton de abajo se cambio por Func, asi que sin ella, teclear 8.000 kHz
     * -un hueco entre presets- dejaba la lista inalcanzable: habia que
     * adivinar y teclear a mano una frecuencia que cayera dentro de alguna.
     * El nombre de la banda es informacion; el boton es la funcion, y esa
     * tiene que estar siempre. */
    s_top.band = banda_actual(s_tune_hz);
    if (!s_top.band) { s_top.band = tr("Bandas", "Bands"); }
    s_top.knob = top_knob_from_target(s_encoder_target);
    s_top.knob_value = s_top_knobval;
    /* Con el mando enganchado a un ajuste, la pastilla dice SU nombre y SU
     * valor, los dos de las mismas funciones que pinta la pantalla de
     * ajustes - no de una copia. Ver el boton Func. */
    s_top.knob_name = 0;
    if (s_encoder_target == ENCODER_TARGET_AJUSTE
        && s_encoder_ajuste != AJ_ENGANCHE_NINGUNO) {
        const char *v = ajuste_valor(s_encoder_ajuste, s_top_knobval);
        s_top.knob_name  = ajuste_nombre(s_encoder_ajuste);
        s_top.knob_value = v ? v : "";
        return;
    }
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
        } else if (m == DEMOD_MODE_AM || m == DEMOD_MODE_SAM || m == DEMOD_MODE_NFM
                   || m == DEMOD_MODE_USB || m == DEMOD_MODE_LSB) {
            /* Los DOS cortes, que es lo que hay puesto desde la etapa 34.
             * Enseñar solo el ancho seria esconder justo la mitad nueva. */
            uint16_t lo, hi;
            fil_cortes(&lo, &hi);
            fil_format(s_top_bw, lo, hi, "k");
            s_top.bw = s_top_bw;
        } else {
            s_top.bw = 0;
        }
    }
    s_top.nr_on = s_nr_on;
    s_top.nco_on = s_nco_on;
    /* La marquesina solo existe en FM ancha con el RDS puesto. Fuera de ahi
     * el puntero va a cero y la cabecera no pinta nada en ese hueco - que es
     * distinto de pintar un texto vacio: asi el sitio vuelve a ser fondo. */
    if (rds_activo() && demod_am_get_mode() == DEMOD_MODE_WFM) {
        s_top.rds = s_rds_txt;
        s_top.rds_off = s_rds_off;
        s_top.rds_nombre = s_rds_nom;
    } else if (s_emis_nom[0] != '\0') {
        /*
         * MISMO HUECO, MISMA PREGUNTA. El RDS contesta "que estoy
         * escuchando" en FM ancha y esto lo contesta en onda corta y
         * media, asi que no pueden coincidir: el sitio esta libre justo
         * cuando hace falta. Ver emisoras_tick().
         */
        s_top.rds = s_emis_txt;
        s_top.rds_off = 0;
        s_top.rds_nombre = s_emis_nom;
    } else {
        s_top.rds = 0;
        s_top.rds_off = 0;
        s_top.rds_nombre = 0;
    }
    s_top.ovr   = (uint8_t)(s_rf_agc_enabled && (s_rf_agc_backoff_x2 > 0));
    s_top.spk_muted = (uint8_t)(!s_speaker_pa_enabled);
    s_top.sam_ppm = (s_top_sam[0] != '\0') ? s_top_sam : 0;
}



/* ===========================================================================
 * ETAPA 4: barra de acciones
 * =========================================================================== */
/* Estado de las pantallas de menu, declarado aqui porque act_sync() -que
 * decide que boton de la barra va iluminado- esta antes en el fichero. */
typedef enum { CFG_AJUSTES = 0, CFG_BANDAS, CFG_MODOS } cfg_pant_t;
/*
 * La pantalla de ajustes, abierta para ELEGIR que mueve el mando en vez de
 * para cambiar valores - 23/09/2026, el boton Func.
 *
 * Es la misma pantalla y la misma lista: lo unico que cambia es que tocar una
 * celda engancha el mando a ese ajuste y vuelve a la principal, en lugar de
 * cambiarlo ahi mismo. Hacer una pantalla aparte habria sido tener dos listas
 * de ajustes que mantener iguales.
 */
static uint8_t s_cfg_func = 0U;
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
    texto_t             nombre;
    texto_t             desc;
    const palette_t    *pal;
    spectrum_palette_t  wf;
} tema_t;

static const tema_t k_temas[] = {
    { T("Oscura", "Dark"),
      T("La de siempre, con el waterfall clásico", "The usual one, classic waterfall"),
      &k_pal_oscura,    SPECTRUM_PALETTE_CLASSIC },
    { T("Contrastada", "High contrast"),
      T("Negro real y saltos mayores, para pleno sol", "True black, wider steps, for full sun"),
      &k_pal_contraste, SPECTRUM_PALETTE_TURBO },
    { T("Ámbar", "Amber"),
      T("Grises cálidos, sin azul: para la noche", "Warm greys, no blue: for night use"),
      &k_pal_ambar,     SPECTRUM_PALETTE_INFERNO },
    { T("Fría", "Cool"),
      T("Grises azulados, para luz de día", "Bluish greys, for daylight"),
      &k_pal_fria,      SPECTRUM_PALETTE_VIRIDIS },
    /*
     * Los tres del 23/09/2026 (ver palette.h para como estan calculados).
     * Van AL FINAL y no intercalados por gusto: el tema elegido se guarda
     * en CONFIG.CSV por INDICE, asi que meter uno en medio le cambiaria el
     * tema a quien ya tuviera uno puesto.
     *
     * La paleta de waterfall de cada uno se elige por coherencia, no al
     * azar: Oliva lleva SMOKE, que es la unica escala CLARA que hay (blanco
     * abajo, negro arriba) y es la que pega con una interfaz de fondo
     * claro; Naranja lleva FIRE, que es su misma familia de tono; y Fósforo
     * estrena una paleta de waterfall propia, PHOSPHOR, porque de las quince
     * que habia ninguna es verde: la que se llama "Clasica verde" lo es por
     * su autor, no por su color, y es azul casi entera.
     */
    { T("Oliva", "Olive"),
      T("Verde oliva con tinta negra, tema claro", "Olive green, black ink, light theme"),
      &k_pal_oliva,     SPECTRUM_PALETTE_SMOKE },
    { T("Naranja", "Orange"),
      T("Naranja fuerte con tinta negra, tema claro", "Strong orange, black ink, light theme"),
      &k_pal_naranja,   SPECTRUM_PALETTE_FIRE },
    { T("Fósforo", "Phosphor"),
      T("Verde fósforo sobre negro, como un terminal", "Phosphor green on black, like a terminal"),
      &k_pal_fosforo,   SPECTRUM_PALETTE_PHOSPHOR }
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
    texto_t      nombre;
} k_act_slots[] = {
    { &s_btn_func,  T("Func",    "Func")     },
    { &s_btn_mode,  T("Modo",    "Mode")     },
    { &s_btn_step,  T("Paso",    "Step")     },
    { &s_btn_vol,   T("Volumen", "Volume")   },
    { &s_btn_nr,    T("Ruido",   "Noise")    },
    { &s_btn_menu,  T("Ajustes", "Settings") },
};

/*
 * 23/09/2026: la tabla se declaraba como k_act_slots[UI_ACT_N], y C rellena
 * de ceros las filas que falten sin decir nada. Al subir UI_ACT_N a 7 y luego
 * volver a 6, una copia del fichero se quedo en 7 con la tabla en 6: salio un
 * septimo boton en blanco en la radio, y el compilador no dijo ni mu.
 *
 * Ahora la tabla no lleva tamano y esto comprueba que mide lo que dice la
 * barra. Si vuelven a no cuadrar, no compila.
 */
_Static_assert(sizeof(k_act_slots) / sizeof(k_act_slots[0]) == UI_ACT_N,
               "k_act_slots[] tiene que tener exactamente UI_ACT_N filas");

/*
 * Cada boton dice su valor actual. Se lee de las MISMAS variables que la
 * pastilla del mando y la cabecera - no de una copia - para que no puedan
 * discrepar.
 */
/*
 * En que banda de k_band_presets[] cae `hz`, o 0 si en ninguna.
 *
 * Se recorre la tabla entera y se elige la MAS ESTRECHA que contenga la
 * frecuencia, no la primera: los rangos se solapan a proposito -7.100 kHz
 * esta en "40 m" de aficionados y tambien dentro de la vecindad de "41 m" de
 * radiodifusion- y la mas estrecha es siempre la mas informativa.
 */
static const char *banda_actual(uint32_t hz)
{
    uint16_t i;
    const char *mejor = 0;
    uint32_t ancho_mejor = 0xFFFFFFFFUL;

    for (i = 0U; i < (uint16_t)BAND_PRESET_COUNT; i++) {
        const band_preset_t *b = &k_band_presets[i];
        uint32_t ancho;

        if (b->lo_hz == 0UL && b->hi_hz == 0UL) { continue; } /* no es un rango */
        if (hz < b->lo_hz || hz > b->hi_hz) { continue; }
        ancho = b->hi_hz - b->lo_hz;
        if (ancho < ancho_mejor) { ancho_mejor = ancho; mejor = b->label[idioma()]; }
    }
    return mejor;
}

static void act_sync(void)
{
    uint8_t i;

    volume_format_ui(s_volume_db_x2, s_act_vol);

    for (i = 0U; i < UI_ACT_N; i++) {
        const ui_button_t *b = k_act_slots[i].btn;

        s_act.name[i]   = k_act_slots[i].nombre[idioma()];
        s_act.value[i]  = 0;
        s_act.active[i] = 0U;

        if (b == &s_btn_mode) {
            s_act.value[i]  = demod_mode_label();
            /* Desde el 24/09/2026 Modo va por la columna de categorias, como
             * Bandas y Ajustes, asi que su luz se mira ahi y no en la rejilla
             * paginada - donde se quedaba apagada para siempre. */
            s_act.active[i] = (uint8_t)(s_menu_cfg_active && s_cfg_pant == CFG_MODOS);
        } else if (b == &s_btn_step) {
            /* Valor y luz, juntos. Hasta el 23/09/2026 habia DOS ramas
             * "b == &s_btn_step" en esta misma cadena else-if: esta, y otra
             * al final que encendia la luz. La segunda no se alcanzaba
             * nunca, asi que Paso era el unico boton de la barra que no se
             * iluminaba con su pantalla abierta. Lo caza -Wduplicated-cond,
             * que ahora esta puesto en el Makefile. */
            s_act.value[i]  = k_tune_step_labels_ui[s_tune_step_idx];
            s_act.active[i] = (uint8_t)(s_menu_open && s_grid_pant == GRID_PASOS);
        } else if (b == &s_btn_vol) {
            s_act.value[i]  = s_act_vol;
            s_act.active[i] = (uint8_t)(s_encoder_target == ENCODER_TARGET_VOLUME);
        } else if (b == &s_btn_nr) {
            s_act.value[i]  = s_nr_on ? tr("activo", "on") : tr("apagado", "off");
            s_act.active[i] = s_nr_on;
        } else if (b == &s_btn_menu) {
            /* Se ilumina el boton de LA pantalla que esta abierta, no el de
             * Ajustes siempre que haya algun menu: estando en Bandas se
             * encendia Ajustes, que es justo decir donde NO estas. El detalle
             * de un ajuste cuenta como Ajustes, que es de donde se llega. */
            s_act.active[i] = (uint8_t)((s_menu_cfg_active && s_cfg_pant == CFG_AJUSTES)
                                        || s_menu_detail_active);
        } else if (b == &s_btn_func) {
            /* Dice a QUE esta enganchado el mando: sin eso, "Func" es un
             * boton que no cuenta nada y hay que mirar la cabecera para
             * saber si sigue enganchado y a que. Suelto no lleva renglon de
             * valor: un 0 aqui hace que ui_act centre "Func" a una linea, en
             * vez de escribir "libre", que no dice nada que no diga ya el
             * boton apagado. */
            s_act.value[i]  = (s_encoder_ajuste != AJ_ENGANCHE_NINGUNO)
                              ? ajuste_nombre(s_encoder_ajuste) : 0;
            s_act.active[i] = (uint8_t)(s_cfg_func
                                        || s_encoder_ajuste != AJ_ENGANCHE_NINGUNO);
        }
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
/* Los cuatro estados del blanker. Ver nb.h: los umbrales que hay detras se
 * midieron con sim/nbtest.c, no se eligieron a ojo. */
/* Los cuatro grados del blanker. En texto_t desde el 29/09/2026: la celda
 * de Ajustes los ensena tal cual y son interfaz, no protocolo. */
static const texto_t k_nb_nombres[4] = {
    T("Apagado", "Off"), T("Suave", "Gentle"), T("Medio", "Medium"), T("Fuerte", "Strong")
};

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

    /*
     * Con el analizador puesto, el panel deja de enseñar radiofrecuencia y
     * pasa a enseñar el espectro del AUDIO. El eje ya no va de megahercios
     * sino de cero al tope de la banda util, y por eso la regla se pide en
     * hercios: en kilohercios enteros, un eje de 0 a 300 Hz se leeria
     * "0 0 0 0 0".
     *
     * No hay punto de demodulacion que marcar -en audio no existe-, asi que
     * demod_px se manda fuera de la regla para que ninguna marca se pinte
     * resaltada. Y band = NONE porque el rectangulo de la banda de paso
     * tampoco significa nada aqui.
     */
    if (analiz_activo()) {
        uint32_t tope = (uint32_t)analiz_tope_hz();
        c->db_min    = s_db_min;
        c->db_max    = s_db_max;
        c->center_hz = tope / 2U;
        c->span_hz   = (tope > 0U) ? tope : 1U;
        c->demod_px  = (int16_t)SPC_TRACE_W;
        c->eje_en_hz = 1U;
        c->band      = SPC_BAND_NONE;
        c->band_hz   = 0U;
        c->cmap      = spectrum_colormap_lut();
        return;
    }
    c->eje_en_hz = 0U;

    /* Misma condicion que usa sdr_spectrum_waterfall_tick() para
     * center_mark_offset_px: con low-IF activo el centro del PANEL no es la
     * frecuencia sintonizada, esta Fs/4 por debajo. */
    /*
     * El centro del PANEL es donde esta el oscilador, no donde esta la
     * sintonia: la FFT se calcula de lo que sale del mezclador. Antes la
     * distancia entre los dos era siempre Fs/4 y por eso el marcador caia
     * siempre en SPC_TRACE_W/4 exactos; con el NCO es cualquier cosa, asi
     * que se calcula. La formula general da el mismo 189 de antes cuando el
     * desplazamiento vuelve a ser Fs/4, que es la comprobacion de que no se
     * ha movido nada para el modo normal.
     */
    if (s_spec_zoom == SPEC_ZOOM_1X && demod_am_get_mix_hz() != 0.0f) {
        panel_center_hz = s_lo_hz;
        c->demod_px = (int16_t)((demod_am_get_mix_hz() * (float)SPC_TRACE_W)
                                / (float)full_span_hz);
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
    {
        /* El sombreado del paso de banda: ahora es la SEPARACION entre los
         * dos cortes y no un ancho de tabla. Sigue dibujandose centrado,
         * que con un corte de abajo de 300 Hz se queda a 150 Hz de la
         * verdad - poco al lado de los 2 o 3 kHz que mide. */
        uint16_t lo, hi;
        fil_cortes(&lo, &hi);
        c->band_hz = (uint32_t)(hi - lo);
    }
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
/*
 * LOS CUATRO AJUSTES RAPIDOS DEL FILTRO - etapa 34, 24/09/2026.
 *
 * El boton de ancho de la barra de abajo recorre estos cuatro, que van de
 * ancho a estrecho. No sustituyen al ajuste fino de los dos cortes (que
 * esta en la pantalla de detalle): son el camino de un toque para la
 * decision que se toma a diario, "me sobra banda por arriba".
 *
 * El primero, 0..4000, es EXACTAMENTE lo que hacia AUDIO_BW_4K0, que era
 * el valor de fabrica: quien nunca toque esto oye lo mismo que oia.
 */
/*
 * EN ORDEN CRECIENTE, de izquierda a derecha, por el dueño del proyecto
 * (24/09/2026). El orden de esta tabla ES el orden en que se pintan los
 * tres botones, asi que aqui no se puede poner "el mas usado primero":
 * una fila de numeros que no va de menor a mayor se lee dos veces.
 */
static const struct { uint16_t lo, hi; const char *rotulo; }
    k_fil_rapidos[UID_PRE_N] = {
    {   0U, 1800U, "1,8 kHz" },
    {   0U, 2300U, "2,3 kHz" },
    {   0U, 4000U, "4,0 kHz" }
};

/* Y los de CW, que son los tres que llevaba su paso banda desde la etapa
 * de telegrafia. Mismo papel: el mando afina de 50 en 50, estos tres
 * devuelven de un toque. */
static const struct { uint16_t an; const char *rotulo; }
    k_cw_rapidos[UID_PRE_N] = {
    {  250U, "250 Hz"  },
    {  500U, "500 Hz"  },
    { 1000U, "1,0 kHz" }
};

/* Cual de los tres esta puesto ahora, o -1 si los cortes estan a mano. */
static int8_t fil_rapido_puesto(void)
{
    uint8_t i;

    if (cw_get_enabled()) {
        uint16_t a = (uint16_t)(demod_am_get_cw_bw_hz() + 0.5f);
        for (i = 0U; i < (uint8_t)UID_PRE_N; i++) {
            if (k_cw_rapidos[i].an == a) { return (int8_t)i; }
        }
        return -1;
    }
    {
        uint16_t lo, hi;
        fil_cortes(&lo, &hi);
        for (i = 0U; i < (uint8_t)UID_PRE_N; i++) {
            if (k_fil_rapidos[i].lo == lo && k_fil_rapidos[i].hi == hi) {
                return (int8_t)i;
            }
        }
    }
    return -1;
}

/* Aplica uno de los tres. Es el paso compartido por el boton de la barra
 * de abajo y por los tres botones de la pantalla de detalle: dos caminos
 * al mismo ajuste tienen que pasar por la misma funcion o se separan. */
static void fil_rapido_pon(uint8_t i)
{
    if (i >= (uint8_t)UID_PRE_N) { return; }
    if (cw_get_enabled()) {
        demod_am_set_cw_bw_hz((float)k_cw_rapidos[i].an);
        if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
        return;
    }
    fil_pon(k_fil_rapidos[i].lo, k_fil_rapidos[i].hi);
    /* En CW manda su paso banda; se deja a la par para que cambiar de modo
     * no traiga una sorpresa. */
    demod_am_set_cw_bw_hz((float)k_cw_rapidos[i].an);
}

/*
 * Los dos cortes, escritos como "0,3-2,7". Sin paso alto se escribe
 * "0-2,7" y no "0,0-2,7": el cero de abajo no es una medida con decimal,
 * es que no hay filtro ahi.
 */
static void fil_format(char *b, uint16_t lo, uint16_t hi, const char *cola)
{
    uint8_t i = 0U;
    if (lo == 0U) { b[i++] = '0'; }
    else {
        b[i++] = (char)('0' + (lo / 1000U));
        b[i++] = ',';
        b[i++] = (char)('0' + ((lo / 100U) % 10U));
    }
    b[i++] = '-';
    b[i++] = (char)('0' + (hi / 1000U));
    b[i++] = ',';
    b[i++] = (char)('0' + ((hi / 100U) % 10U));
    while (*cola != '\0') { b[i++] = *cola++; }
    b[i] = '\0';
}

/* Los cortes de la familia que suena ahora. */
static void fil_cortes(uint16_t *lo, uint16_t *hi)
{
    demod_am_get_filtro(demod_am_filtro_familia(), lo, hi);
}

/* Los dos cortes de una familia, como lo*10000+hi - ver k_extra_claves[]. */
static int32_t fil_empaqueta(uint8_t fam)
{
    uint16_t lo = 0U, hi = 0U;
    demod_am_get_filtro(fam, &lo, &hi);
    return (int32_t)lo * 10000 + (int32_t)hi;
}

static void fil_desempaqueta(uint8_t fam, int32_t v)
{
    /* El recorte de verdad vive en audiofil.c; aqui solo se evita que un
     * CONFIG.CSV escrito a mano meta un numero que desborde esto. */
    if (v < 0 || v > 99999999) { return; }
    demod_am_set_filtro(fam, (uint16_t)(v / 10000), (uint16_t)(v % 10000));
}

static void fil_pon(uint16_t lo, uint16_t hi)
{
    demod_am_set_filtro(demod_am_filtro_familia(), lo, hi);
    if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
}

static void audio_bw_cycle(void)
{
    int8_t puesto = fil_rapido_puesto();
    uint8_t k;

    /*
     * El boton ESTRECHA, que es lo que ha hecho siempre: 4,0 -> 2,3 ->
     * 1,8 -> 4,0. Como la tabla va de menor a mayor -porque ese es el
     * orden en que se PINTAN los tres botones-, estrechar es ir hacia
     * atras en ella. Dos ordenes distintos para dos cosas distintas, y
     * cada uno es el que espera quien lo usa.
     *
     * Si los cortes estan a mano y no coinciden con ninguno se empieza por
     * el mas ancho: lo que espera quien pulsa es "devuelveme a algo
     * conocido", no "sigue desde donde no estabas".
     */
    if (puesto < 0) { k = (uint8_t)(UID_PRE_N - 1); }
    else if (puesto == 0) { k = (uint8_t)(UID_PRE_N - 1); }
    else { k = (uint8_t)(puesto - 1); }

    fil_rapido_pon(k);
    debug_print("audio filter: ajuste rapido cambiado\n");
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


/*
 * La celda de "Ancho (BW)" tenia aqui su propia llamada, que recorria los
 * tres anchos fijos. Desde la etapa 34 la celda abre el ajuste fino de los
 * dos cortes -ver ajuste_accion()- y el recorrido rapido se queda solo en
 * el boton de la barra de abajo, que es donde se usa de verdad. Dos
 * caminos vivos al mismo ajuste es como se desincronizan las cosas.
 */

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

/* s_btn_audio_bw's callback - unlike the settings cell,
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
/*
 * Herramienta de diagnostico. NO hay ningun #if de por medio, aunque este
 * comentario lo dijera hasta el 28/09/2026: la llamada esta en texto plano
 * mas abajo, DENTRO de un debug_print(). Con DEBUG_UART_ENABLED=0 -la
 * compilacion de serie- esa macro expande a ((void)0) y se lleva por
 * delante la llamada entera; con el UART encendido, la funcion SI se
 * ejecuta.
 *
 * Y eso es exactamente el riesgo del que avisa debug_uart.h: es la unica
 * llamada del arbol que mete una funcion CON TRABAJO dentro de un
 * debug_print(), o sea codigo que desaparece en silencio segun como se
 * compile. Queda anotado aqui porque el comentario viejo ("detras de un #if
 * desactivado") mandaba a buscar un interruptor que no existe.
 */
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

        /* 23/09/2026: el hook NO se vuelve a enganchar aqui. Antes si, y
         * entre esta linea y demod_am_set_active_rate() (73 lineas mas
         * abajo) hay tres transacciones I2C y varios debug_print por UART:
         * milisegundos, con bloques de audio entrando cada 2,67 ms. La ISR
         * se colaba a mitad de la reconfiguracion, cuando
         * demod_am_set_active_rate() ya ha escrito s_dec_block_samples = 64
         * (48 kHz) pero todavia no ha reinicializado s_interp_inst, que
         * sigue con L = 8 (96 kHz): arm_fir_interpolate_f32() escribia
         * 64 x 8 = 512 floats en s_env[256], 1 KB pasado el final, encima de
         * s_audio_out. Y s_interp_state[73] se queda corto (12 + 64 - 1 =
         * 75). Se engancha despues, al final del bloque. */

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
        /* El blanker mide contra un suelo promediado a lo largo de medio
         * segundo. Un cambio de modo o de tasa cambia el nivel de entrada de
         * golpe, asi que ese suelo se queda mintiendo: demasiado alto y no
         * caza nada, demasiado bajo y recorta la señal buena hasta que se
         * pone al dia. Se olvida y se vuelve a sembrar con el primer bloque
         * que entre. */
        nb_reset();

        /* AHORA, con los filtros, los diezmadores y los interpoladores ya
         * coherentes entre si, vuelve a entrar la ISR. Ver el comentario de
         * mas arriba, donde estaba esto antes. */
        if (will_be_wfm) {
            sdr_rx_set_block_hook(demod_wfm_process_raw);
            demod_wfm_reset_diag(); /* fresh diagnostic log for this WFM entry - see its own comment */
        } else {
            sdr_rx_set_block_hook(demod_am_process_raw);
            demod_am_reset_diag(); /* fresh diagnostic log for this AM/SSB/LSB/NFM entry - see its own comment */
        }

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
        const banda_visita_t *v = &s_banda_visita[idx];

        /* Antes de irnos, apuntar donde estabamos: si no, volver a la banda
         * de la que sales te devolveria a la entrada del preset en vez de a
         * donde la dejaste hace dos segundos. */
        banda_apunta();

        if (v->hz != 0UL) {
            s_tune_hz = v->hz;
            set_tune_step_idx(v->paso);
            apply_demod_mode(v->modo);
        } else {
            s_tune_hz = p->freq_hz;
            set_tune_step_idx(p->step_idx);
            apply_demod_mode(p->mode);
        }
        apply_lo_tune(s_tune_hz);

        debug_print("bands: preset applied -> ");
        debug_print(p->label[IDIOMA_ES]);   /* el UART, siempre en espanol */
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


/*
 * LO QUE HAY QUE VOLVER A ARMAR CUANDO CAMBIA LA TASA DE RF - 30/09/2026.
 *
 * La pastilla RATE conmuta entre 96 y 48 kHz y llama a apply_demod_mode() y
 * apply_lo_tune(), que rearman el demodulador y el oscilador. Pero hay dos
 * cosas mas que se calcularon con la tasa VIEJA y que nadie tocaba:
 *
 *   APRS. ax25_start() calcula con la tasa el paso del reloj de bit y las
 *   frecuencias de sus dos osciladores. Y solo se llama desde el conmutador
 *   de MODO. O sea que entrar en APRS y despues tocar RATE dejaba el
 *   decodificador buscando 600 y 1.100 Hz en vez de 1.200 y 2.200, con el
 *   reloj a mitad de velocidad: no decodificaba ni una trama, y no habia
 *   forma de saber por que ni de arreglarlo salvo volver a elegir el modo.
 *
 *   EL ANALIZADOR DE AUDIO. analiz_aplicar() tambien pregunta la tasa, y
 *   tampoco se llamaba desde aqui: con 96 kHz puestos dentro y 48 entrando,
 *   analiz_hz_por_bin() y analiz_tope_hz() devuelven EL DOBLE de lo real y
 *   todo el eje de frecuencias miente. Un instrumento de medida que se
 *   equivoca por un factor dos sin decirlo.
 *
 * Los dos se rearman desde aqui, que es el unico sitio donde cambia la tasa,
 * para que el tercero que dependa de ella tenga un sitio evidente donde
 * ponerse.
 */
static void analiz_aplicar(void);   /* definida mas abajo, con la tabla del zoom */

static void tasa_rf_reaplica(void)
{
    if (ax25_activo()) {
        ax25_start(demod_am_get_active_fs_hz());
    }
    if (analiz_activo()) {
        analiz_aplicar();
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
        tasa_rf_reaplica();
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
    {
        settings_extra_t extra;
        extras_leer(&extra);
        settings_save_now(s_tune_hz, demod_am_get_mode(), k_tune_steps[s_tune_step_idx], demod_am_get_audio_bw(), s_volume_db_x2, s_nonwfm_use_48k,
        s_pga_gain_db_x2, spectrum_smooth_pct_for_save(), s_speaker_pa_enabled, s_rf_agc_rin_level, s_tema_idx, &extra);
    }
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

            {
                settings_extra_t extra;
                extras_leer(&extra);
                settings_save_now(s_tune_hz, demod_am_get_mode(), k_tune_steps[s_tune_step_idx], demod_am_get_audio_bw(), s_volume_db_x2, s_nonwfm_use_48k,
                s_pga_gain_db_x2, spectrum_smooth_pct_for_save(), s_speaker_pa_enabled, s_rf_agc_rin_level, s_tema_idx, &extra);
            }

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

/*
 * VELOCIDAD DE LA CASCADA. 23/09/2026, y una velocidad mas el 24.
 *
 * La cascada baja una linea por frame y los frames van a ~30 por segundo.
 * Con WATERFALL_ROWS filas eso son poco mas de dos segundos de historia en
 * pantalla: suficiente para ver si una señal esta o no, y demasiado poco
 * para ver COMO se comporta. Una emisora horaria manda un pulso por segundo;
 * en dos segundos de cascada eso son dos rayas y no se ve el ritmo.
 *
 *     Muy rapida  2 lineas por frame   60 lineas/s     1,2 s en pantalla
 *     Rapida      1 por frame          30 lineas/s     2,4 s
 *     Media       1 de cada 2           15 lineas/s     4,8 s
 *     Lenta       1 de cada 4            7,5 lineas/s   9,6 s
 *     Muy lenta   1 de cada 8            3,7 lineas/s  19,2 s
 *
 * HACIA ABAJO no se tira nada: los frames que no bajan linea se acumulan y
 * lo que baja es su media, asi que bajar la velocidad ademas LIMPIA la
 * cascada en vez de enseñar una de cada ocho fotos.
 *
 * HACIA ARRIBA hubo que pensarlo, porque "Rapida" ya era una linea por
 * frame: para ir mas deprisa hay que bajar DOS lineas en un solo frame, y
 * eso solo vale la pena si las dos llevan datos distintos. Pintar la misma
 * linea dos veces es estirar la imagen, no ganar resolucion: una pantalla
 * que miente sobre el tiempo.
 *
 * Y se puede, porque dentro de un frame de 33 ms no hay UNA medida, hay
 * hasta seis: la traza del espectro es la media de hasta seis FFT (ver
 * SPECTRUM_MAX_FFT_PER_FRAME). Asi que a MITAD DE LAS FFT -no a mitad de
 * reloj: los bloques llegan a 375/s, las seis FFT del cupo se hacen en los
 * primeros ~16 ms y el resto del frame no aporta nada, asi que partir por
 * tiempo dejaria la segunda linea vacia- se guarda una foto del acumulado,
 * y al final se sacan dos lineas: la primera mitad y lo que vino despues.
 * Dos medidas de verdad, cada una de su trozo de tiempo.
 *
 * Esas dos lineas salen del acumulado CRUDO, sin el suavizado entre frames
 * del espectro (s_db_smooth): ese suavizado es una media movil de frames
 * enteros y volveria a pegar las dos mitades justo despues de separarlas.
 * Pedir mas resolucion temporal es exactamente pedir menos promedio.
 *
 * LO QUE CUESTA: casi nada. Lo caro de la cascada es el VOLCADO (756 x 72
 * pixeles por el bus del panel) y se sigue haciendo una vez por frame -
 * waterfall_push_line() solo mueve 756 bytes dentro de un anillo, el
 * desplazamiento es contabilidad, no memoria. O sea que 60 lineas por
 * segundo cuestan practicamente lo mismo que 30.
 *
 * LO QUE HAY QUE SABER: con dos lineas por frame, cada una promedia la
 * MITAD de FFT, asi que la cascada sale mas granulada. No es un defecto del
 * dibujo, es lo que significa pedir mas resolucion temporal: menos promedio.
 * Y en ZOOM y con el analizador de audio no hay dos medidas que separar -
 * ahi llega una ventana por frame-, asi que "Muy rapida" se comporta como
 * "Rapida" en vez de duplicar lineas iguales.
 */
#define WF_VEL_N 5U
static const struct {
    uint8_t     mult;      /* lineas por frame */
    uint8_t     div;       /* frames por linea (el otro sentido) */
    uint8_t     clave;     /* lo que se GUARDA: lineas por segundo, redondeado */
    texto_t     nombre;
} k_wf[WF_VEL_N] = {
    { 2U, 1U, 60U, T("Muy rápida", "Very fast") },
    { 1U, 1U, 30U, T("Rápida",     "Fast")      },
    { 1U, 2U, 15U, T("Media",      "Medium")    },
    { 1U, 4U,  7U, T("Lenta",      "Slow")      },
    { 1U, 8U,  3U, T("Muy lenta",  "Very slow") }
};

/*
 * SE GUARDA LA VELOCIDAD, NO EL INDICE.
 *
 * Hasta el 24/09/2026 en CONFIG.CSV iba el indice pelado, y al meter "Muy
 * rapida" DELANTE -que es donde tiene que ir para que la lista se lea de
 * rapida a lenta- ese indice habria cambiado de significado: quien tuviera
 * guardado un 0 ("Rapida") se habria encontrado con "Muy rapida" sin tocar
 * nada. Es el mismo fallo que ya se arreglo en los pasos de sintonia, asi
 * que se arregla igual: se guarda un numero que SIGNIFICA algo -las lineas
 * por segundo- y al cargar se busca la fila que lo lleva.
 */
static uint8_t  s_wf_vel = 1U;      /* indice en k_wf; 1 = "Rapida", la de siempre */
static uint8_t  s_wf_frames = 0U;   /* frames acumulados sin bajar linea */

static uint8_t wf_vel_por_clave(uint8_t clave)
{
    uint8_t i;
    for (i = 0U; i < WF_VEL_N; i++) {
        if (k_wf[i].clave == clave) { return i; }
    }
    return 1U;   /* lo que no se reconoce, a "Rapida" */
}

static void wf_vel_siguiente(void)
{
    /* HACIA ARRIBA, no hacia abajo (25/09/2026). La tabla esta ordenada de
     * rapida a lenta, y sumar uno llevaba de "Rapida" a "Media": tocar hacia
     * "mas lento" cuando lo que uno quiere casi siempre es mas rapido. Ahora
     * resta: de "Rapida" a "Muy rapida", y desde ahi da la vuelta a "Muy
     * lenta", "Lenta", "Media" y otra vez "Rapida".
     *
     * Restar con envoltura en un uint8_t hay que escribirlo con cuidado: con
     * s_wf_vel a 0, un "- 1U" da 255 y el modulo NO lo arregla. Sumar
     * WF_VEL_N - 1 antes del modulo es la misma vuelta sin pasar por debajo
     * de cero. */
    s_wf_vel = (uint8_t)((s_wf_vel + WF_VEL_N - 1U) % WF_VEL_N);
    /* Lo acumulado era de la velocidad anterior: se tira, porque mezclarlo
     * daria una linea con mas o menos promedio del que toca. */
    s_wf_frames = 0U;
}

/*
 * ANALIZADOR DE AUDIO: el conmutador. 23/09/2026.
 *
 * 0 = apagado y el panel enseña radiofrecuencia, como siempre. De 1 a 5, el
 * panel enseña el espectro del AUDIO con uno de los cinco zoom. Ver
 * User/analizador.h para el porque de cada tope.
 *
 * Se guarda en CONFIG.CSV, pero conviene saber que se guarda: si lo dejas
 * puesto, al arrancar vuelve a salir el audio y no la radio. Es a proposito
 * -un ajuste que no se acuerda de lo que le dijiste es un ajuste que
 * miente- pero es la clase de cosa que despista si no se espera.
 */
static const texto_t k_analiz_nombres[ANALIZ_N + 1U] = {
    T("Apagado", "Off"), T("2400 Hz", "2400 Hz"), T("1200 Hz", "1200 Hz"),
    T("600 Hz", "600 Hz"), T("300 Hz", "300 Hz"), T("150 Hz", "150 Hz")
};
static uint8_t s_analiz = 0U;   /* 0 = apagado; 1..5 = ANALIZ_* + 1 */

static void analiz_aplicar(void)
{
    if (s_analiz == 0U) {
        analiz_stop();
    } else {
        /* La tasa del audio es la activa de la radio, no un numero escrito
         * aqui: a 48 kHz todos los topes salen a la mitad y el eje lo dice. */
        analiz_start(s_nonwfm_use_48k ? 48000.0f : 96000.0f);
        analiz_zoom((analiz_zoom_t)(s_analiz - 1U));
    }
    /* El eje cambia de megahercios a hercios (o al reves): hay que repintar
     * la regla entera, no solo compararla. */
    s_chrome_init = 0U;
}

/*
 * Encender o apagar el NCO de sintonia. Ver el bloque grande del NCO, mas
 * arriba.
 *
 * Al ENCENDERLO no pasa nada visible: el oscilador ya esta Fs/4 por debajo
 * de la sintonia, que cae dentro de la ventana, asi que lo_para_sintonia()
 * lo deja donde esta y no se reprograma nada. Lo que cambia es lo que pasa a
 * partir del clic siguiente.
 *
 * Al APAGARLO, el oscilador vuelve a su sitio de siempre y el panorama se
 * recentra de golpe - una vez.
 *
 * En los dos casos hay que repintar la regla ENTERA y no compararla: el
 * centro del panel puede haberse movido varias decenas de kHz, y la
 * comparacion por campos de spec_chrome_ruler_changed() no cubre el caso de
 * que ademas cambie el marcador.
 */
/*
 * El puente de la traza. Ver spectrum_set_bridge().
 *
 * LO QUE HACE Y LO QUE CUESTA, mirado en el simulador antes de darle un
 * ajuste en vez de despues:
 *
 *   - Con CONTORNO, apagarlo devuelve exactamente el problema que se
 *     arreglo el 08/09/2026 ("los puntos del espectro no estan
 *     conectados"): en los flancos empinados los puntos dejan de tocarse y
 *     las puntas salen como una nube de motas. Ahi el puente ES el dibujo.
 *
 *   - Con el espectro relleno la diferencia es fina pero real y va en el
 *     otro sentido: sin puente las puntas salen mas finas y el suelo de
 *     ruido un poco mas limpio, porque el borde de arriba deja de
 *     engordar un pixel en cada columna que no coincide con su vecina.
 *
 * O sea que no hay una opcion buena: depende del estilo y del gusto. Por eso
 * es un ajuste y no una decision tomada por dentro, y por eso el ajuste
 * afecta a TODOS los estilos aunque en uno de ellos sea claramente peor - un
 * conmutador que en algunos sitios no hace nada es un conmutador que miente,
 * y se ve al instante lo que hace.
 */
static void puente_conmutar(void)
{
    spectrum_set_bridge((uint8_t)(!spectrum_get_bridge()));
    if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
    /* El espectro se repinta entero en el siguiente cuadro, asi que no hay
     * nada que forzar aqui. */
}

static void nco_conmutar(void)
{
    s_nco_on = s_nco_on ? 0U : 1U;
    apply_lo_tune(s_tune_hz);      /* aparca o desaparca, segun */
    s_chrome_init = 0U;
    /* El chip "NCO" de la barra de estado. Se repinta AQUI y no en cada
     * sitio que llama, que son tres (la casilla de ajustes, el propio chip,
     * y el mando si se le engancha): si se deja en manos de quien llama, el
     * dia que aparezca un cuarto camino el chip se queda mintiendo. */
    if (s_pantalla_pintada && !s_screen_asleep) {
        badges_draw();
    }
    if (s_settings_ready_for_autosave) {
        settings_mark_dirty();
    }
}

static void analiz_siguiente(void)
{
    s_analiz = (uint8_t)((s_analiz + 1U) % (ANALIZ_N + 1U));
    analiz_aplicar();
}

/* --- identificadores de ajuste --------------------------------------- */
enum {
    AJ_AGC = 0, AJ_SQL, AJ_VOL, AJ_BW, AJ_PGA, AJ_NR, AJ_NB, AJ_RFAGC, AJ_ATT,
    AJ_BRILLO, AJ_ESCALA, AJ_AUTOESC, AJ_SUAVIZ, AJ_ESTILO, AJ_ZOOM,
    AJ_PALETA, AJ_TRAZA, AJ_CONTORNO, AJ_PUENTE, AJ_WFVEL, AJ_ANALIZ, AJ_NCO,
    AJ_ALTAVOZ, AJ_TASA, AJ_IFBW, AJ_TACTIL, AJ_CAL_TACTIL, AJ_CAL_PPM, AJ_DORMIR,
    AJ_RTTY_SHIFT, AJ_RTTY_BAUD, AJ_RTTY_INV,
    AJ_CW_TONO, AJ_CW_PPM, AJ_CW_AUTO, AJ_WFX_LPM, AJ_RDS, AJ_HORA, AJ_NOTCH,
    AJ_TEMA, AJ_INFO, AJ_IDIOMA, AJ_QTH, AJ_MANDO
};

/*
 * Cinco paginas desde el 23/09/2026. Eran cuatro y Radio, Pantalla y Equipo
 * estaban las tres a 9 de 9: cada ajuste nuevo obligaba a echar otro fuera, y
 * eso ya habia pasado dos veces (Brillo se fue a Equipo para dejarle sitio a
 * Tema, y "Hora por radio" acabo en Digital porque Equipo estaba lleno).
 *
 * El corte no es "lo que sobraba de Pantalla" sino uno con sentido: PANTALLA
 * es COMO SE VE -paleta, tema, estilo de traza, suavizado- y ESPECTRO es LO
 * QUE SE MIDE -escala, zoom, velocidad de la cascada-. Quien busca la escala
 * ya no tiene que pasar por los colores.
 */
#define AJ_PAG_RADIO    0U
#define AJ_PAG_PANTALLA 1U
#define AJ_PAG_ESPECTRO 2U
/*
 * EQUIPO, EL ULTIMO. 27/09/2026, por el dueno: "poner equipo en la ultima
 * posicion de la lista".
 *
 * Es la unica de las cinco que no se toca mientras se escucha: calibrar el
 * tactil, el reposo, la informacion del aparato. Las otras cuatro son
 * ajustes de escucha y van juntas. Estos numeros solo ordenan la columna
 * de la izquierda -no se guardan en ninguna parte-, asi que cambiarlos no
 * mueve ningun ajuste de sitio.
 */
#define AJ_PAG_DIGITAL  3U
#define AJ_PAG_EQUIPO   4U
#define AJ_PAGINAS      5U

typedef struct {
    /* Los dos idiomas juntos, en el mismo sitio donde estaba la cadena
     * espanola: se ve de un vistazo si la traduccion es la que toca, y al
     * anadir una fila no hay una segunda tabla que se pueda olvidar.
     * Ver User/idioma.h. */
    texto_t nombre;
    uint8_t pagina;
    uint8_t id;
} ajuste_t;

/*
 * El orden dentro de cada pagina es el de uso, no el alfabetico ni el
 * historico: lo que se toca a menudo arriba a la izquierda, lo que se toca
 * una vez en la vida (calibrar) abajo.
 */
static const ajuste_t k_ajustes[] = {
    { T("AGC", "AGC"), AJ_PAG_RADIO, AJ_AGC },
    { T("Ancho (BW)", "Bandwidth"), AJ_PAG_RADIO, AJ_BW },
    /* El Notch ocupa el hueco que deja Volumen, y no es casualidad que
     * caiga aqui: Reducción, Blanker y Notch son las tres herramientas
     * contra lo que estorba, y estan juntas.
     *
     * Volumen se fue a Pantalla el 24/09/2026 por el dueño del proyecto.
     * Es la celda mas redundante de las nueve que habia: el volumen ya
     * tiene boton propio en la barra de abajo Y enganche del mando, asi
     * que su celda aqui era el tercer camino a lo mismo. Las cuatro
     * paginas estaban a 9 de 9 y una sexta categoria no cabe - saldrian
     * 44 px por categoria y el objetivo minimo de dedo son 47. */
    { T("Notch", "Notch"), AJ_PAG_RADIO, AJ_NOTCH },
    { T("Silenciador", "Squelch"), AJ_PAG_RADIO, AJ_SQL },
    { T("Reducción", "Noise red."), AJ_PAG_RADIO, AJ_NR },
    { T("Blanker", "Blanker"), AJ_PAG_RADIO, AJ_NB },
    { T("Ganancia", "Gain"), AJ_PAG_RADIO, AJ_PGA },
    { T("AGC de RF", "RF AGC"), AJ_PAG_RADIO, AJ_RFAGC },
    { T("Atenuador", "Attenuator"), AJ_PAG_RADIO, AJ_ATT },

    { T("Paleta", "Palette"), AJ_PAG_PANTALLA, AJ_PALETA },
    { T("Estilo", "Style"), AJ_PAG_PANTALLA, AJ_ESTILO },
    { T("Traza", "Trace"), AJ_PAG_PANTALLA, AJ_TRAZA },
    /* Tema, 22/09/2026: la paleta de la interfaz, distinta de "Paleta",
     * que son los colores del espectro. */
    { T("Tema", "Theme"), AJ_PAG_PANTALLA, AJ_TEMA },
    { T("Volumen", "Volume"), AJ_PAG_PANTALLA, AJ_VOL },
    /*
     * *** El dueno: "mover brillo a seccion pantalla". ***
     *
     * Estaba en Equipo desde que se movio para dejarle sitio a Tema, y ese
     * es justo el tipo de mudanza que deja las cosas donde no se buscan: el
     * brillo de la pantalla es de Pantalla. El comentario de cinco lineas
     * mas arriba ya lo contaba como un ejemplo de lo que no habia que
     * hacer, y seguia hecho.
     */
    { T("Brillo", "Brightness"), AJ_PAG_PANTALLA, AJ_BRILLO },
    /*
     * LOS DOS DEL TACTIL, 29/09/2026, por el dueno: "tienes que mover
     * tactil y calibrar tactil a pantalla".
     *
     * Estaban en Equipo desde siempre, y ahi no es donde se buscan: la
     * firmeza del tactil y su calibracion son de la PANTALLA, igual que el
     * brillo o el tema. En Equipo quedan las cosas del aparato que no se
     * tocan mientras se escucha.
     *
     * Y ademas hacia falta el sitio: Equipo se habia quedado a 9 de 9 al
     * entrar Idioma, y la rejilla dibuja nueve y corta en silencio. Con
     * esta mudanza Equipo baja a 7 y Pantalla sube a 8, asi que las dos
     * vuelven a tener hueco.
     *
     * Calibrar va la ultima de la pagina: es lo que se toca una vez en la
     * vida, y ese es el criterio de orden de toda la tabla.
     */
    { T("Táctil", "Touch"), AJ_PAG_PANTALLA, AJ_TACTIL },
    { T("Calibrar táctil", "Touch calib."), AJ_PAG_PANTALLA, AJ_CAL_TACTIL },

    { T("Escala", "Scale"), AJ_PAG_ESPECTRO, AJ_ESCALA },
    { T("Autoescala", "Auto scale"), AJ_PAG_ESPECTRO, AJ_AUTOESC },
    { T("Zoom", "Zoom"), AJ_PAG_ESPECTRO, AJ_ZOOM },
    { T("Suavizado", "Smoothing"), AJ_PAG_ESPECTRO, AJ_SUAVIZ },
    { T("Contorno", "Contour"), AJ_PAG_ESPECTRO, AJ_CONTORNO },
    /* Puente, 24/09/2026: si la traza une los puntos vecinos o los deja
     * sueltos. Ver spectrum_set_bridge(). */
    { T("Puente", "Bridge"), AJ_PAG_ESPECTRO, AJ_PUENTE },
    /* Cascada, 23/09/2026: cada cuanto baja una linea. */
    { T("Cascada", "Waterfall"), AJ_PAG_ESPECTRO, AJ_WFVEL },
    /* Analizador: el panel deja de enseñar radiofrecuencia y enseña el
     * espectro del audio. Ver User/analizador.h. */
    { T("Analizador", "Analyzer"), AJ_PAG_ESPECTRO, AJ_ANALIZ },
    { T("NCO", "NCO"), AJ_PAG_ESPECTRO, AJ_NCO },

    /*
     * ORDEN DE LAS FILAS DE EQUIPO - 30/09/2026, por el dueno: "los tres de
     * abajo tienen que estar en medio y los de en medio abajo".
     *
     * Y encaja con el criterio de toda la tabla, que es el de uso y no el
     * historico: QTH, Mando e Idioma son PREFERENCIAS que se ponen y se
     * miran; Reposo, Calibrar PPM e Informacion son acciones que se tocan
     * una vez en la vida. Lo que se toca a menudo arriba, lo que no, abajo.
     */
    { T("Altavoz", "Speaker"), AJ_PAG_EQUIPO, AJ_ALTAVOZ },
    { T("Muestreo", "Sampling"), AJ_PAG_EQUIPO, AJ_TASA },
    { T("Filtro FM", "FM filter"), AJ_PAG_EQUIPO, AJ_IFBW },
    { T("QTH", "QTH"), AJ_PAG_EQUIPO, AJ_QTH },
    /*
     * Mando, 30/09/2026, por el dueno: "el encoder en esta radio va al
     * reves". El cableado del encoder no es igual en todas las placas, asi
     * que el sentido no puede ser una constante de compilacion. Va en
     * Equipo porque es una propiedad DE ESTE aparato, como el tactil o el
     * altavoz, y no una preferencia de escucha.
     */
    { T("Mando", "Knob"), AJ_PAG_EQUIPO, AJ_MANDO },
    /*
     * Idioma, 29/09/2026, por el dueno: un boton que alterna entre espanol
     * e ingles y cambia el texto de toda la interfaz.
     *
     * Va en Equipo porque es una preferencia del aparato, no de escucha, y
     * porque es lo unico que queda de esa familia. Con esta fila EQUIPO SE
     * QUEDA A 9 DE 9: la rejilla dibuja nueve celdas y corta en silencio a
     * partir de ahi, asi que la decima fila de esta pagina no se veria y
     * nadie se enteraria. El _Static_assert de debajo de la tabla es lo que
     * impide que eso pase sin avisar.
     */
    { T("Idioma", "Language"), AJ_PAG_EQUIPO, AJ_IDIOMA },
    { T("Reposo", "Sleep"), AJ_PAG_EQUIPO, AJ_DORMIR },
    { T("Calibrar PPM", "PPM calib."), AJ_PAG_EQUIPO, AJ_CAL_PPM },
    { T("Información", "Information"), AJ_PAG_EQUIPO, AJ_INFO },
    /*
     * QTH, 29/09/2026, por el dueno. Tu localizador Maidenhead: de el
     * salen el aspa de "donde estamos" en los mapas de FT8, WSPR, AIS y
     * JTTY, y la distancia de las lineas de CQ de FT8.
     *
     * Hasta hoy era un #define de ft8_decoder.c que valia "IL18"
     * -Canarias, el ejemplo del comentario original- y no habia por donde
     * cambiarlo. Cabe aqui porque Tactil y Calibrar tactil se fueron a
     * Pantalla, que es donde se buscan.
     */

    { T("Desplazamiento", "Shift"), AJ_PAG_DIGITAL, AJ_RTTY_SHIFT },
    { T("Baudios", "Baud rate"), AJ_PAG_DIGITAL, AJ_RTTY_BAUD },
    { T("Inversión", "Inverted"), AJ_PAG_DIGITAL, AJ_RTTY_INV },

    { T("Tono CW", "CW pitch"), AJ_PAG_DIGITAL, AJ_CW_TONO },
    { T("Velocidad CW", "CW speed"), AJ_PAG_DIGITAL, AJ_CW_PPM },
    { T("Autoenganche", "Auto lock"), AJ_PAG_DIGITAL, AJ_CW_AUTO },

    /* Velocidad de linea del fax. El decodificador ya sabia las cuatro
     * desde la etapa 26 -wefax_set_lpm()-, pero no habia por donde
     * pedirlas: 120 es la normal y era la unica alcanzable, asi que una
     * carta a 90 o a 240 salia inclinada sin remedio. */
    { T("Velocidad fax", "Fax speed"), AJ_PAG_DIGITAL, AJ_WFX_LPM },
    /* Va en Digital y no en Equipo por dos razones, y las dos son buenas:
     * es un decodificador de radio como RTTY o CW -no una preferencia del
     * aparato-, y Equipo estaba a 9 de 9 mientras que aqui sobra sitio. El
     * dia que se añada MSF o alguna otra emisora horaria, van al lado. */
    { T("RDS", "RDS"), AJ_PAG_DIGITAL, AJ_RDS },
    { T("Hora por radio", "Radio clock"), AJ_PAG_DIGITAL, AJ_HORA }
};
#define AJUSTE_COUNT (sizeof(k_ajustes) / sizeof(k_ajustes[0]))

static const texto_t k_aj_paginas[AJ_PAGINAS] = {
    T("Radio",    "Radio"),
    T("Pantalla", "Display"),
    T("Espectro", "Spectrum"),
    T("Digital",  "Digital"),
    T("Equipo",   "Device")
};

/* Las cuatro fuerzas del notch. Los numeros de cada una salen del
 * barrido del banco - ver la tabla de anotch.c. */
static const texto_t k_notch_nombres[4] = {
    T("Suave", "Gentle"), T("Normal", "Normal"),
    T("Fuerte", "Strong"), T("A saco", "Maximum")
};

/* Nombres largos para lo que antes eran siglas de tres letras. */
static const texto_t k_agc_nombres[4] = {
    T("Sin AGC", "No AGC"), T("Lenta", "Slow"), T("Media", "Medium"), T("Rápida", "Fast")
};
/* Los nombres largos de Ajustes y las siglas de la barra son la misma lista
 * en dos formatos, indexada por el mismo perfil de AGC. */
_Static_assert(sizeof(k_agc_nombres) / sizeof(k_agc_nombres[0])
               == sizeof(k_agc_profile_labels) / sizeof(k_agc_profile_labels[0]),
               "k_agc_nombres y k_agc_profile_labels tienen que medir lo mismo");
static const char *const k_att_nombres[3]   = { "0 dB", "-6 dB", "-12 dB" };
static const texto_t k_tactil_nombres[3] = {
    T("Sensible", "Light"), T("Normal", "Normal"), T("Firme", "Firm")
};

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
    case AJ_AGC:    return k_agc_nombres[(uint8_t)demod_am_get_agc_profile()][idioma()];
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
        if (m == DEMOD_MODE_AM || m == DEMOD_MODE_SAM || m == DEMOD_MODE_NFM
            || m == DEMOD_MODE_USB || m == DEMOD_MODE_LSB) {
            uint16_t lo, hi;
            fil_cortes(&lo, &hi);
            fil_format(buf, lo, hi, " kHz");
            return buf;
        }
        return tr("fijo", "fixed");
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
    /* El blanker dice su nivel, no "activo/apagado": los tres grados no son
     * mas de lo mismo, son umbrales distintos, y cual tienes puesto es justo
     * lo que hay que saber cuando algo suena raro. */
    case AJ_NB:     return k_nb_nombres[nb_get_nivel()][idioma()];
    case AJ_RFAGC:  return s_rf_agc_enabled ? tr("Activo", "On") : tr("Apagado", "Off");
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
    case AJ_AUTOESC: return s_spec_agc_enabled ? tr("Activa", "On") : tr("Apagada", "Off");
    case AJ_SUAVIZ: {
        uint8_t i;
        aj_u2s(buf, (uint32_t)spectrum_smooth_pct_for_save());
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ' '; buf[i++] = '%'; buf[i] = '\0';
        return buf;
    }
    case AJ_CONTORNO: aj_u2s(buf, (uint32_t)s_spec_smooth_passes); return buf;
    case AJ_WFVEL:    return k_wf[s_wf_vel].nombre[idioma()];
    case AJ_ANALIZ:   return k_analiz_nombres[s_analiz][idioma()];
    /* "Activo"/"Apagado" y no "Puesto": es como lo dicen ya el AGC de RF, el
     * autoenganche y el altavoz, y una casilla que dice lo mismo con otra
     * palabra obliga a traducir mentalmente. 23/09/2026, por el dueno. */
    case AJ_NCO:      return s_nco_on ? tr("Activo", "On") : tr("Apagado", "Off");
    /* "Unido" y "Suelto" y no "Activo"/"Apagado": lo que cambia no es que
     * algo funcione o no, es como se ve la traza, y el rotulo lo dice. */
    case AJ_PUENTE:   return spectrum_get_bridge() ? tr("Unido", "Joined") : tr("Suelto", "Loose");
    case AJ_ESTILO:
        switch (spectrum_get_style()) {
        case SPECTRUM_STYLE_HEATMAP: return tr("Relleno", "Filled");
        case SPECTRUM_STYLE_LINE:    return tr("Línea", "Line");
        default:                     return tr("Contorno", "Outline");
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
    case AJ_TRAZA:   return spectrum_get_heatmap_trace_white() ? tr("Blanca", "White")
                                                              : tr("Del color", "Palette");

    case AJ_ALTAVOZ: return s_speaker_pa_enabled ? tr("Activo", "On") : tr("Mudo", "Muted");
    case AJ_TASA:    return s_nonwfm_use_48k ? "48 kHz" : "96 kHz";
    case AJ_IFBW:    return (demod_am_get_wfm_ifbw() == WFM_IFBW_NARROW) ? "80 kHz" : "96 kHz";
    case AJ_TACTIL:  return k_tactil_nombres[touch_get_firmeza() - 1U][idioma()];

    case AJ_RTTY_SHIFT: {
        uint8_t i;
        aj_u2s(buf, (uint32_t)(rtty_get_shift_hz() + 0.5f));
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ' '; buf[i++] = 'H'; buf[i++] = 'z'; buf[i] = '\0';
        return buf;
    }
    case AJ_RTTY_BAUD: return k_rtty_baud_labels[s_rtty_baud_idx];

    case AJ_NOTCH:
        return anotch_activo() ? k_notch_nombres[anotch_fuerza()][idioma()]
                               : tr("Apagado", "Off");

    case AJ_WFX_LPM: {
        uint8_t i;
        aj_u2s(buf, (uint32_t)wefax_get_lpm());
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ' '; buf[i++] = 'l'; buf[i++] = 'p'; buf[i++] = 'm';
        buf[i] = '\0';
        return buf;
    }

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
    /*
     * El idioma se escribe EN SI MISMO, no traducido al otro: en espanol
     * pone "Espanol" y en ingles "English", nunca "Ingles". Quien se
     * encuentra la radio en un idioma que no entiende busca la palabra que
     * reconoce, no su traduccion. Ver idioma_nombre().
     */
    case AJ_IDIOMA: return idioma_nombre();
    /* "Normal" es el sentido de fabrica de la placa, sea cual sea: lo que
     * el usuario ve es si lo ha dado la vuelta o no, no cual de los dos
     * cableados lleva dentro, que no puede saberlo ni le importa. */
    case AJ_MANDO:  return encoder_invertido() ? tr("Invertido", "Reversed")
                                               : tr("Normal", "Normal");
    /* El localizador tal cual, que es lo unico que hay que ver de un
     * vistazo. Las coordenadas se ensenan dentro de su pantalla. */
    case AJ_QTH: {
        /* Vacio = no hay QTH puesto, que desde el 30/09 es un estado normal
         * y no un fallo: ya no hay localizador compilado (ver la cabecera de
         * ft8_decoder.c). La celda tiene que DECIRLO, porque sin QTH no sale
         * el aspa de los mapas ni la distancia de los CQ y eso, en blanco,
         * se lee como que el mapa esta roto. */
        const char *g = ft8_decoder_get_own_grid();
        /* Con el por defecto del arranque en frio (IN80dk) esto no se ve
         * casi nunca; se queda porque un CONFIG.CSV con un "grid" que no
         * vale deja el localizador vacio, y entonces no sale el aspa ni las
         * distancias y la celda en blanco se leeria como que el mapa esta
         * roto. Ver ft8_decoder_set_own_grid(). */
        return (g != 0 && g[0] != '\0') ? g : tr("sin poner", "not set");
    }
    case AJ_CW_AUTO:
        return cw_get_autotune() ? tr("Automático", "Automatic")
                                 : tr("Fijo en el tono", "Fixed on pitch");
    case AJ_TEMA:   return k_temas[s_tema_idx].nombre[idioma()];
    case AJ_CW_PPM: {
        uint8_t i;
        aj_u2s(buf, (uint32_t)k_cw_wpm[s_cw_wpm_idx]);
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ' '; buf[i++] = '/'; buf[i++] = ' ';
        aj_u2s(&buf[i], (uint32_t)(cw_get_wpm() + 0.5f));
        return buf;
    }
    case AJ_RTTY_INV:  return rtty_get_station_inverted() ? tr("Invertida", "Inverted")
                                                          : tr("Normal", "Normal");
    case AJ_RDS:       return rds_activo() ? tr("Activo", "On") : tr("Apagado", "Off");

    default: return 0;   /* acciones: calibrar, dormir */
    }
}

/* ===========================================================================
 * SINCRONIZAR LA HORA POR RADIO (DCF77) - 23/09/2026
 * ===========================================================================
 * Ver dcf77.h para el formato de la emisora y ui_hora.h para por que la
 * pantalla ensena dos barras en vez de un "sincronizando...".
 *
 * Lo que hace esta parte es lo de menos: guardar donde estabas, irse a
 * 77,5 kHz en AM con el filtro mas estrecho, dejar trabajar al decodificador
 * y devolverte a tu sitio al salir. Lo unico con algo de miga es que la hora
 * NO se pone sola: se enseña y hay que pulsar "Aplicar". Poner la hora del
 * reloj de alguien sin preguntar, por muy seguro que uno este, es de mala
 * educacion - y si el decodificador se equivocara alguna vez, el usuario
 * tiene delante el numero para verlo antes de aceptarlo.
 * =========================================================================== */

static uint8_t     s_menu_hora_active = 0U;
static ui_hora_state_t s_hora;
static uint32_t    s_hora_vfo;        /* donde estabas, para volver */
static demod_mode_t s_hora_modo;
static audio_bw_t  s_hora_bw;
static uint32_t    s_hora_ult_ms;
static hora_emisora_t s_hora_emisora = HORA_DCF77;

/*
 * BARRIDO DE TODAS LAS EMISORAS - 23/09/2026.
 *
 * Con SEIS emisoras y ninguna certeza de cual entra desde un sitio dado, la
 * pregunta util no es "sincroniza?" sino "cual de las seis tiene algo?". Y
 * eso, a mano, son seis visitas de medio minuto mirando dos barras.
 *
 * El barrido las visita solo, se queda VISITA_S en cada una, se apunta lo
 * mejor que ha visto, y al acabar deja la tabla en pantalla. De un vistazo
 * se sabe donde merece la pena esperar los tres minutos que cuesta una
 * sincronizacion de verdad.
 *
 * Lo que se apunta es el CONTRASTE, no el nivel: nivel alto y contraste
 * cero es ruido de banda ancha o una portadora que no es la que se busca.
 * El contraste solo sube si la portadora hace lo que tiene que hacer.
 */
/* 25 segundos por emisora. Con seis emisoras el barrido entero son dos
 * minutos y medio, que es mucho para estar mirando - pero es la alternativa
 * a hacerlo a mano, que son los mismos dos minutos y medio y ademas hay que
 * estar ahi -. Menos de 25 no vale: el contraste de RBU se recalcula cada
 * cinco segundos y el de WWV necesita ver varios segundos seguidos. */
#define BARRIDO_VISITA_S 25U

static uint8_t  s_barrido = 0U;        /* 1 mientras esta barriendo */
static uint8_t  s_barrido_i;           /* emisora que esta visitando */
static uint32_t s_barrido_ms0;         /* cuando empezo la visita */
static uint8_t  s_barrido_mejor[HORA_N];   /* contraste maximo, 0..100 */
static uint8_t  s_barrido_nivel[HORA_N];   /* y el nivel, para distinguir */
/* Diez bytes por emisora sobran para "DCF 100%<  " y el numero crece con la
 * tabla: asi añadir una emisora no puede desbordar este texto. */
static char     s_barrido_txt[12 * HORA_N];
static char        s_hora_txt[12];    /* "HH:MM" o, anclado, "HH:MM:SS" */
static char        s_hora_fecha[16];  /* "15/06/26" */
static char        s_hora_det[64];

static void hora_cerrar(void);

static char *hora_u2(char *b, uint32_t v)   /* dos cifras con cero delante */
{
    b[0] = (char)('0' + ((v / 10U) % 10U));
    b[1] = (char)('0' + (v % 10U));
    return b + 2;
}
static char *hora_u3(char *b, uint32_t v)
{
    b[0] = (char)('0' + ((v / 100U) % 10U));
    return hora_u2(b + 1, v);
}
static char *hora_u4(char *b, uint32_t v)
{
    b[0] = (char)('0' + ((v / 1000U) % 10U));
    return hora_u3(b + 1, v);
}

/*
 * Sintoniza la emisora elegida y arranca el decodificador de cero. Se llama
 * al abrir la pantalla y cada vez que se cambia de emisora con el boton: las
 * dos cosas son lo mismo, y tenerlo en una sola funcion es lo que impide que
 * cambiar de emisora se olvide de alguno de los pasos.
 */
static void hora_sintoniza(void)
{
    /*
     * EL RDS ES OTRA COSA - 24/09/2026, por el dueno: "en la pantalla de
     * hora, rds no aparece".
     *
     * Y tenia que aparecer: hace exactamente lo mismo que las otras seis
     * -poner el reloj en hora sin internet- solo que por otro camino. La
     * diferencia practica es que NO tiene frecuencia propia: vale cualquier
     * emisora de FM que mande la hora, o sea casi todas. Asi que aqui no se
     * sintoniza nada; se pasa a FM ancha, se enciende el decodificador y, si
     * lo que hay puesto no es una emisora de FM, se cae en medio de la banda
     * para que haya algo que oir.
     */
    if (s_hora_emisora == HORA_RDS) {
        apply_demod_mode(DEMOD_MODE_WFM);
        if (s_tune_hz < 87500000UL || s_tune_hz > 108000000UL) {
            s_tune_hz = 100000000UL;
        }
        apply_lo_tune(s_tune_hz);
        freq_display_draw();
        if (!rds_activo()) {
            rds_start(192000.0f);
            rds_reinicia();
            if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
        }
        s_hora.emisora = dcf77_nombre(HORA_RDS);
        s_hora.hora = 0;
        s_hora.fecha = 0;
        s_hora.puede_aplicar = 0U;
        return;
    }

    /* Las seis emisoras horarias: AM y el filtro mas estrecho que hay. De la
     * portadora solo interesa cuanto sube y cuanto baja, asi que todo el
     * ancho que se deje pasar de mas es ruido que entra por la puerta.
     * Estaba en hora_show(), y se ha movido aqui para que cambiar de emisora
     * DESDE el RDS vuelva a poner AM - sin esto, saltar de RDS a DCF77
     * dejaba la radio en FM ancha escuchando 77,5 kHz. */
    apply_demod_mode(DEMOD_MODE_AM);
    demod_am_set_audio_bw(AUDIO_BW_1K8);

    s_tune_hz = dcf77_freq_hz(s_hora_emisora);
    apply_lo_tune(s_tune_hz);
    /* apply_lo_tune() cambia la radio pero NO repinta la cabecera - eso lo
     * hace siempre quien llama, ver el bucle del mando. Sin esto, arriba
     * seguia leyendose tu frecuencia anterior hasta que algo repintaba la
     * cabecera por su cuenta (el reloj, al cambiar de minuto), y parecia que
     * la pantalla no habia sintonizado. Reportado el 23/09/2026. */
    freq_display_draw();

    /* La tasa activa la sabe este fichero: s_nonwfm_use_48k es suyo. Asi no
     * hace falta abrir un getter nuevo en demod_am solo para esto. */
    /* Cuantas muestras por bloque necesita cada emisora lo dice su fila de
     * la tabla de dcf77.c, no este numero escrito aqui. Ver
     * dcf77_submuestras(). */
    dcf77_start(s_hora_emisora,
                (s_nonwfm_use_48k ? 48000.0f : 96000.0f)
                / (float)SDR_RX_BLOCK_SAMPLES
                * (float)dcf77_submuestras(s_hora_emisora));

    s_hora.emisora = dcf77_nombre(s_hora_emisora);
    s_hora.hora = 0;
    s_hora.fecha = 0;
    s_hora.puede_aplicar = 0U;
}

static void hora_show(void)
{
    s_hora_vfo  = s_tune_hz;
    s_hora_modo = demod_am_get_mode();
    s_hora_bw   = demod_am_get_audio_bw();

    /* El modo y el ancho los pone hora_sintoniza(), que es quien sabe si la
     * emisora elegida es una horaria de onda larga o el RDS. */
    hora_sintoniza();
    /* apply_lo_tune() cambia la radio pero NO repinta la cabecera - eso lo
     * hace siempre quien llama, ver el bucle del mando. Sin esto, arriba
     * seguia leyendose tu frecuencia anterior hasta que algo repintaba la
     * cabecera por su cuenta (el reloj, al cambiar de minuto), y parecia que
     * la pantalla no habia sintonizado. Reportado el 23/09/2026. */
    freq_display_draw();

    /* Una muestra de envolvente por bloque: el ritmo sale de la tasa activa,
     * no de un numero escrito a mano, porque a 48 kHz son la mitad. */
    s_hora.titulo = tr("Hora por radio", "Radio clock");
    s_hora.estado = tr("Buscando la señal", "Searching for the signal");
    s_hora.detalle = tr("puede tardar 3 o 4 minutos", "it can take 3 or 4 minutes");
    s_hora.nivel = 0U;
    s_hora.marca = 0U;
    s_hora.latido = 0U;
    s_hora.puede_aplicar = 0U;
    s_hora.pressed = UIH_HIT_NONE;

    s_menu_hora_active = 1U;
    s_menu_open = 1U;
    s_menu_cfg_active = 0U;
    s_menu_detail_active = 0U;
    s_grid_pant = GRID_NADA;
    s_kbd_modo = KBD_NADA;
    s_hora_ult_ms = 0U;

    ui_hora_draw(&s_hora);
    act_draw();
}

/*
 * Cerrar esta pantalla es SOLO cerrar el menu: lo de volver a tu frecuencia
 * lo hace menu_screen_close(), no esto.
 *
 * La primera version lo hacia al reves -restauraba aqui y luego cerraba- y
 * tenia un agujero: se sale de esta pantalla por el boton "Salir", pero
 * tambien pulsando "Ajustes" abajo o con la pulsacion larga del mando, y
 * esas dos van derechas a menu_screen_close() sin pasar por aqui. Por esos
 * dos caminos el decodificador se paraba pero la radio se quedaba en 77,5
 * kHz en AM, sin que nada lo dijera. Es el mismo fallo que ya tuvo el
 * enganche de Func, y se arregla igual: la limpieza vive donde pasan TODAS
 * las salidas, no donde pasa la que uno tenia en la cabeza.
 */
static void hora_cerrar(void)
{
    menu_screen_close();
}

static void barrido_arranca(void)
{
    uint8_t k;

    for (k = 0U; k < (uint8_t)HORA_N; k++) {
        s_barrido_mejor[k] = 0U;
        s_barrido_nivel[k] = 0U;
    }
    s_barrido = 1U;
    s_barrido_i = 0U;
    s_barrido_ms0 = g_msticks;
    s_hora_emisora = HORA_DCF77;
    hora_sintoniza();
    s_hora.emisora = tr("Buscando en todas", "Scanning all of them");
}

/* Escribe la tabla del barrido: una casilla por emisora con lo mejor que se
 * ha visto en cada una. */
static void barrido_texto(void)
{
    static const char *k_corto[HORA_N] = { "DCF", "MSF", "ALS",
                                          "RBU", "W10", "W15", "BPM", "RDS" };
    char *p = s_barrido_txt;
    uint8_t k;

    for (k = 0U; k < (uint8_t)HORA_BARRIDO_N; k++) {
        const char *n = k_corto[k];
        while (*n) { *p++ = *n++; }
        *p++ = ' ';
        p = hora_u3(p, s_barrido_mejor[k]);
        *p++ = '%';
        if (k == s_barrido_i && s_barrido) { *p++ = '<'; }
        *p++ = ' '; *p++ = ' ';
    }
    *p = '\0';
    s_hora.detalle = s_barrido_txt;
}

/* Un paso del barrido. Devuelve 1 si sigue barriendo. */
static uint8_t barrido_paso(const dcf_info_t *inf)
{
    uint32_t pct = (uint32_t)(inf->contraste * 100.0f);

    if (pct > 100U) { pct = 100U; }
    if ((uint8_t)pct > s_barrido_mejor[s_barrido_i]) {
        s_barrido_mejor[s_barrido_i] = (uint8_t)pct;
    }
    s_barrido_nivel[s_barrido_i] = s_hora.nivel;

    if ((uint32_t)(g_msticks - s_barrido_ms0) < BARRIDO_VISITA_S * 1000U) {
        return 1U;
    }

    /* Se acabo el turno de esta: a la siguiente, o fin. */
    if ((uint8_t)(s_barrido_i + 1U) < (uint8_t)HORA_BARRIDO_N) {
        s_barrido_i++;
        s_barrido_ms0 = g_msticks;
        s_hora_emisora = (hora_emisora_t)s_barrido_i;
        hora_sintoniza();
        s_hora.emisora = tr("Buscando en todas", "Scanning all of them");
        return 1U;
    }

    /* Fin: se queda en la que mas contraste dio, que es donde tiene sentido
     * esperar. Si ninguna dio nada, se queda donde estaba y lo dice. */
    {
        uint8_t k, mejor = 0U;
        for (k = 1U; k < (uint8_t)HORA_BARRIDO_N; k++) {
            if (s_barrido_mejor[k] > s_barrido_mejor[mejor]) { mejor = k; }
        }
        s_barrido = 0U;
        s_hora_emisora = (hora_emisora_t)mejor;
        hora_sintoniza();
    }
    return 0U;
}

/*
 * La misma pantalla, alimentada por el RDS.
 *
 * Las barras significan lo mismo que en las horarias aunque midan otra cosa,
 * y eso es a proposito: "nivel" es cuanta señal entra -el mismo medidor que
 * mueve el S-metro- y "marca" es cuanto de lo que llega sirve. En DCF77 eso
 * es el contraste de la marca de segundo; aqui es el porcentaje de bloques
 * que pasan la comprobacion. Son dos medidas distintas de la misma pregunta:
 * "esto va a salir o no".
 */
static void hora_poll_rds(void)
{
    rds_info_t in;
    char *p;
    uint32_t pct;

    rds_info(&in);

    {
        float n = demod_am_get_signal_peak();
        uint32_t nv = 0U;
        while (n >= 2.0f && nv < 100U) { n *= 0.5f; nv += 10U; }
        s_hora.nivel = (uint8_t)nv;
    }
    pct = (uint32_t)(in.calidad * 100.0f + 0.5f);
    if (pct > 100U) { pct = 100U; }
    s_hora.marca = (uint8_t)pct;
    s_hora.latido = in.enganchado;
    s_hora.emisora = dcf77_nombre(HORA_RDS);

    s_hora.hora = 0;
    s_hora.fecha = 0;
    s_hora.puede_aplicar = 0U;

    if (!in.enganchado) {
        s_hora.estado = tr("Buscando el RDS", "Searching for RDS");
        s_hora.detalle = tr("sintoniza una emisora de FM con el mando",
                        "tune an FM station with the knob");
    } else if (!in.tiene_hora) {
        /*
         * Enganchado pero sin hora todavia, que es el estado NORMAL durante
         * el primer minuto largo: el grupo que lleva la hora se manda una
         * vez por minuto, no once veces por segundo como el nombre. Decirlo
         * evita que parezca que no funciona.
         */
        s_hora.estado = in.tiene_ps ? in.ps : tr("Leyendo el RDS", "Reading RDS");
        s_hora.detalle = tr("la hora se manda una vez por minuto",
                        "the time is sent once a minute");

        /*
         * Y si la cadena NO esta en la lista de codigos, se enseña el codigo
         * aqui. Es el unico sitio de la radio donde tiene sentido verlo: esta
         * pantalla ya es de diagnostico -barras de nivel, estado del
         * decodificador- y es lo que hace falta para poder anotarlo y
         * anadirlo a User/rds_pi.c. Esa lista se hace escuchando, no
         * copiandola de ningun sitio.
         */
        if (in.tiene_pi && !rds_pi_nombre(in.pi)) {
            static const char hx[] = "0123456789ABCDEF";
            p = s_hora_det;
            *p++ = 'c'; *p++ = 'o'; *p++ = 'd'; *p++ = 'i'; *p++ = 'g'; *p++ = 'o';
            *p++ = ' ';
            *p++ = hx[(in.pi >> 12) & 0xFU];
            *p++ = hx[(in.pi >> 8) & 0xFU];
            *p++ = hx[(in.pi >> 4) & 0xFU];
            *p++ = hx[in.pi & 0xFU];
            *p++ = ' '; *p++ = '-'; *p++ = ' ';
            {
                const char *t = tr("la hora se manda una vez por minuto",
                                   "the time is sent once a minute");
                while (*t != '\0') { *p++ = *t++; }
            }
            *p = '\0';
            s_hora.detalle = s_hora_det;
        }
    } else if (!s_rds_hora_ok) {
        /*
         * Llego una hora pero aun no esta confirmada. Aqui SE ENSEÑA IGUAL,
         * en gris de "todavia no vale", y con la cuenta de cuantas han
         * llegado: esta pantalla decia lo mismo esperando la segunda que
         * llevando diez sin que ninguna cuadrara, y son dos cosas
         * completamente distintas. Viendo el numero -y viendo si el minuto
         * avanza- se sabe cual de las dos es.
         *
         * Y no, enseñarla sin confirmar no contradice lo de no mentir: no se
         * aplica al reloj, el boton Aplicar sigue apagado y el renglon de
         * abajo dice exactamente lo que es.
         */
        int16_t loc = (int16_t)(in.hora * 60 + in.minuto)
                    + (int16_t)in.desp_medias * 30;
        while (loc < 0)     { loc += 1440; }
        while (loc >= 1440) { loc -= 1440; }

        s_hora.estado = tr("Hora recibida - sin confirmar", "Time received - unconfirmed");

        p = s_hora_txt;
        p = hora_u2(p, (uint8_t)(loc / 60)); *p++ = ':';
        p = hora_u2(p, (uint8_t)(loc % 60)); *p = '\0';
        s_hora.hora = s_hora_txt;

        p = s_hora_det;
        p = hora_u3(p, (uint32_t)s_rds_ct_n);
        {
            const char *t = (s_rds_ct_n == 1U)
                    ? tr(" recibida - hace falta otra que cuadre",
                         " received - another matching one is needed")
                    : tr(" recibidas - hace falta que dos cuadren",
                         " received - two of them must match");
            while (*t != '\0') { *p++ = *t++; }
        }
        if (s_rds_ct_mal > 0U) {
            const char *t = ", descartadas ";
            while (*t != '\0') { *p++ = *t++; }
            p = hora_u3(p, (uint32_t)s_rds_ct_mal);
        }
        *p = '\0';
        s_hora.detalle = s_hora_det;
    } else {
        int16_t loc = (int16_t)(in.hora * 60 + in.minuto)
                    + (int16_t)in.desp_medias * 30;
        while (loc < 0)     { loc += 1440; }
        while (loc >= 1440) { loc -= 1440; }

        s_hora.estado = in.tiene_ps ? in.ps : tr("Hora confirmada", "Time confirmed");
        s_hora.detalle = s_hora_anclada
                       ? tr("en hora y anclado al segundo (la norma da +/-0,1 s)",
                            "on time and locked to the second (spec says +/-0,1 s)")
                       : tr("el reloj ya esta en hora", "the clock is already on time");

        /*
         * Una vez anclado se enseña el RELOJ DE LA RADIO con segundos, no la
         * hora que trajo el ultimo grupo. Son cosas distintas a proposito: lo
         * que interesa ver aqui es si NUESTRO reloj va bien, y para eso hay
         * que poder ponerlo al lado de otro y mirar si el segundo salta a la
         * vez. Enseñando la hora recibida se estaria enseñando el minuto de
         * hace un rato, congelado, que no dice nada.
         */
        if (s_hora_anclada) {
            uint32_t ms = reloj_ahora_ms();
            p = s_hora_txt;
            p = hora_u2(p, (uint8_t)(ms / 3600000UL)); *p++ = ':';
            p = hora_u2(p, (uint8_t)((ms / 60000UL) % 60UL)); *p++ = ':';
            p = hora_u2(p, (uint8_t)((ms / 1000UL) % 60UL)); *p = '\0';
        } else {
            p = s_hora_txt;
            p = hora_u2(p, (uint8_t)(loc / 60)); *p++ = ':';
            p = hora_u2(p, (uint8_t)(loc % 60)); *p = '\0';
        }
        s_hora.hora = s_hora_txt;

        p = s_hora_fecha;
        p = hora_u2(p, in.dia); *p++ = '/';
        p = hora_u2(p, in.mes); *p++ = '/';
        p = hora_u2(p, in.anno2); *p = '\0';
        s_hora.fecha = s_hora_fecha;
        s_hora.puede_aplicar = 1U;
    }

    ui_hora_draw(&s_hora);
}

/* Refresca la pantalla. Se llama desde el bucle principal, no desde la
 * interrupcion: aqui no hay nada que dependa de llegar a tiempo. */
static void hora_poll(void)
{
    dcf_info_t inf;
    char *p;
    uint32_t nivel_pct, marca_pct;

    if (!s_menu_hora_active) { return; }
    /*
     * Cinco veces por segundo basta: lo unico que se mueve es el latido de
     * la barra de marca, que es de una vez por segundo.
     *
     * Con el reloj ANCLADO se mira diez veces, porque entonces la pantalla
     * enseña segundos y esos si se mueven. No es capricho: a cinco por
     * segundo el segundo podria aparecer hasta 200 ms tarde, y eso es MAS de
     * lo que la propia norma del RDS promete (+/-0,1 s). Una pantalla que
     * enseña segundos tiene que refrescarse mas deprisa que el error que dice
     * tener, o los segundos no significan nada. A diez por segundo el retraso
     * de pantalla queda por debajo del error de la fuente, que es donde tiene
     * que estar.
     */
    {
        uint32_t cada = (s_hora_emisora == HORA_RDS && s_hora_anclada) ? 100U : 200U;
        if ((uint32_t)(g_msticks - s_hora_ult_ms) < cada) { return; }
    }
    s_hora_ult_ms = g_msticks;

    /* El RDS no pasa por dcf77.c: tiene su propia funcion, que rellena la
     * misma estructura de pantalla. Ver hora_poll_rds(). */
    if (s_hora_emisora == HORA_RDS) {
        hora_poll_rds();
        return;
    }

    dcf77_info(&inf);

    /* El barrido se lleva la pantalla mientras dura: lo que interesa ahi es
     * la tabla comparativa, no el estado del decodificador -que con 25
     * segundos por emisora no va a sincronizar nada, ni lo pretende-. */
    if (s_barrido) {
        uint32_t nv = 0U;
        float n = dcf77_nivel_propio(s_hora_emisora) ? inf.nivel
                                                     : demod_am_get_signal_peak();
        while (n >= 2.0f && nv < 100U) { n *= 0.5f; nv += 10U; }
        s_hora.nivel  = (uint8_t)nv;
        s_hora.marca  = (uint8_t)((inf.contraste > 1.0f) ? 100.0f : inf.contraste * 100.0f);
        s_hora.latido = (uint8_t)(s_hora.marca >= 25U);
        s_hora.estado = dcf77_nombre(s_hora_emisora);
        s_hora.hora = 0; s_hora.fecha = 0; s_hora.puede_aplicar = 0U;
        (void)barrido_paso(&inf);
        barrido_texto();
        if (!s_barrido) {
            /* Terminado: se queda en la mejor y la tabla se queda a la
             * vista, que es todo el producto del barrido. */
            s_hora.emisora = dcf77_nombre(s_hora_emisora);
        }
        ui_hora_draw(&s_hora);
        return;
    }

    /* El nivel se enseña en escala logaritmica a ojo -a saltos de x2- porque
     * lo que importa no es el numero sino distinguir "nada" de "algo". */
    {
        /* El nivel de portadora. Solo DCF77 y MSF lo miden ellas mismas; en
         * las demas se coge del medidor de señal de la propia radio, que es
         * el mismo que mueve el S-metro. Ver dcf77_nivel_propio(), que es
         * quien lo sabe. Sin esto la barra se quedaba a cero en ALS162 -y en
         * WWV, donde lo que mide el decodificador es la subportadora de
         * 100 Hz y no la portadora-. */
        float n = dcf77_nivel_propio(s_hora_emisora) ? inf.nivel
                                                     : demod_am_get_signal_peak();
        uint32_t pct = 0U;
        while (n >= 2.0f && pct < 100U) { n *= 0.5f; pct += 10U; }
        nivel_pct = pct;
    }
    marca_pct = (uint32_t)(inf.contraste * 100.0f);
    if (marca_pct > 100U) { marca_pct = 100U; }

    s_hora.nivel = (uint8_t)nivel_pct;
    s_hora.marca = (uint8_t)marca_pct;
    s_hora.latido = (uint8_t)(marca_pct >= 25U);

    p = s_hora_det;
    switch (inf.estado) {
    case DCF_BUSCANDO:
        s_hora.estado = tr("Buscando la señal", "Searching for the signal");
        s_hora.detalle = tr("si la barra de marca no late, no hay señal",
                        "if the mark bar does not beat, there is no signal");
        break;
    case DCF_LEYENDO:
    case DCF_PERDIDO:
        s_hora.estado = (inf.estado == DCF_PERDIDO) ? "Se ha perdido - reintentando"
                                                    : tr("Leyendo la trama", "Reading the frame");
        p = hora_u2(p, inf.bits); *p++ = '/'; p = hora_u2(p, 59U);
        /* Y los dos numeros que de verdad explican lo que pasa cuando no
         * avanza: cuanto duro la ultima marca y cuanto hay entre segundos.
         * Con señal buena el ciclo es ~1000 ms y las marcas caen en los
         * valores del formato (100/200 en DCF77; 100/200/300/500 en MSF).
         * Si el ciclo no es de un segundo, el problema esta en los flancos;
         * si las marcas son absurdas, en el nivel o en el filtro. Sin estos
         * dos numeros solo se puede adivinar, y adivinar ya costo una tarde. */
        *p++ = ' '; *p++ = 'm'; *p++ = 'a'; *p++ = 'r'; *p++ = 'c'; *p++ = 'a';
        *p++ = ' ';
        p = hora_u3(p, inf.ms_marca);
        *p++ = ' '; *p++ = 'c'; *p++ = 'i'; *p++ = 'c'; *p++ = 'l'; *p++ = 'o';
        *p++ = ' ';
        p = hora_u4(p, inf.ms_ciclo);
        *p++ = ' '; *p++ = 'r'; *p++ = 'a'; *p++ = 'r'; *p++ = 'a'; *p++ = 's';
        *p++ = ' ';
        p = hora_u3(p, (inf.marcas_raras > 999U) ? 999U : inf.marcas_raras);
        *p = '\0';
        s_hora.detalle = s_hora_det;
        break;
    case DCF_CONFIRMANDO:
        s_hora.estado = tr("Una trama buena - confirmando con la siguiente",
                       "One good frame - confirming with the next one");
        s_hora.detalle = tr("un minuto mas", "one more minute");
        break;
    case DCF_LISTO:
    default:
        s_hora.estado = tr("Hora recibida y confirmada", "Time received and confirmed");
        s_hora.detalle = tr("pulsa Aplicar para ponerla", "press Apply to set it");
        break;
    }

    if (inf.estado == DCF_LISTO) {
        p = s_hora_txt;
        p = hora_u2(p, inf.hora); *p++ = ':';
        p = hora_u2(p, inf.minuto); *p = '\0';
        s_hora.hora = s_hora_txt;

        p = s_hora_fecha;
        p = hora_u2(p, inf.dia); *p++ = '/';
        p = hora_u2(p, inf.mes); *p++ = '/';
        p = hora_u2(p, inf.anno); *p = '\0';
        s_hora.fecha = s_hora_fecha;
        s_hora.puede_aplicar = 1U;
    }

    ui_hora_draw(&s_hora);
}

/*
 * El boton se decide en la PULSACION y se respeta hasta la suelta, igual que
 * grid_touch(), cfg_touch() y det_touch(). No es un capricho de estilo: en
 * este panel resistivo las coordenadas de la SUELTA no son de fiar, cosa que
 * el resto del fichero ya sabia y decia. La primera version de esta funcion
 * volvia a mirar donde estaba el dedo al soltar, asi que ui_hora_hit()
 * devolvia "ninguno" y el boton Salir no hacia nada. Reportado por el dueno
 * del proyecto el 23/09/2026, el mismo dia que se escribio.
 *
 * Y recibe `pressed` como las otras tres, no un `suelta` invertido: tener
 * una sola de las cuatro con el parametro al reves es pedir que alguien -yo
 * mismo, dentro de un mes- lo llame mal desde el reparto de toques.
 */
static void hora_touch(uint16_t x, uint16_t y, uint8_t pressed)
{
    if (pressed) {
        if (s_hora.pressed < 0) {
            int8_t h = ui_hora_hit(x, y);
            /* "Aplicar" apagado no se hunde: un boton que da retorno visual
             * y luego no hace nada se vive como que la pantalla falla. */
            if (h == UIH_HIT_APLICAR && !s_hora.puede_aplicar) { h = UIH_HIT_NONE; }
            if (h != UIH_HIT_NONE) { s_hora.pressed = h; ui_hora_draw(&s_hora); }
        }
        return;
    }
    if (s_hora.pressed < 0) { return; }
    {
        int8_t k = s_hora.pressed;

        s_hora.pressed = UIH_HIT_NONE;
        ui_hora_draw(&s_hora);

        if (k == UIH_HIT_EMISORA) {
            /* Cambiar de emisora tira lo acumulado y empieza de cero, que es
             * lo unico honrado: los bits que llevaba eran de la otra. */
            /* El ciclo recorre la tabla de emisoras entera y al final pasa
             * por el barrido: DCF77 -> MSF -> ALS162 -> RBU -> WWV 10 ->
             * WWV 15 -> BPM -> barrido -> DCF77. El barrido es un "modo" mas del
             * mismo boton y no un boton nuevo: abajo solo caben tres y uno ya
             * es Salir. */
            if (s_barrido) {
                s_barrido = 0U;
                s_hora_emisora = HORA_DCF77;
            } else if ((unsigned)s_hora_emisora + 1U >= (unsigned)HORA_N) {
                barrido_arranca();
            } else {
                s_hora_emisora = (hora_emisora_t)(s_hora_emisora + 1U);
            }
            if (!s_barrido) { hora_sintoniza(); }
            ui_hora_draw(&s_hora);
        } else if (k == UIH_HIT_SALIR) {
            hora_cerrar();
        } else if (k == UIH_HIT_APLICAR) {
            if (s_hora_emisora == HORA_RDS) {
                /* Con el RDS el reloj ya se ha puesto solo en cuanto la hora
                 * quedo confirmada; el boton esta para cerrar dandola por
                 * buena, igual que en las otras. */
                rds_info_t in;
                rds_info(&in);
                if (in.tiene_hora && s_rds_hora_ok) {
                    /* Anclando, no poniendo el minuto a secas: con
                     * reloj_poner() este boton DESHARIA el anclaje que ya
                     * hizo rds_poll(), y el reloj perderia el segundo justo
                     * al confirmarlo. */
                    reloj_anclar((int16_t)(in.hora * 60 + in.minuto
                                           + (int16_t)in.desp_medias * 30),
                                 rds_hora_atraso_ms());
                    debug_print("rds: hora aplicada al reloj\n");
                }
            } else {
                dcf_info_t inf;
                dcf77_info(&inf);
                if (inf.estado == DCF_LISTO) {
                    reloj_poner(inf.hora, inf.minuto);
                    debug_print("dcf77: hora aplicada al reloj\n");
                }
            }
            hora_cerrar();
        }
    }
}

/* Lo que hace tocar un ajuste. Llama a los callbacks que ya existen. */
static void ajuste_accion(uint8_t id);
static const char *ajuste_nombre(uint8_t id);
static uint8_t ajuste_estados(uint8_t id);

/*
 * ENGANCHA EL MANDO A UN AJUSTE - 23/09/2026, el boton Func.
 *
 * Los de rango continuo ya tenian su propio destino de mando desde hace
 * tiempo -es lo que usa la pantalla de detalle con sus botones "-" y "+",
 * que lo unico que hacen es inyectar pasos de mando-, asi que engancharlos
 * es ponerlo y ya. Los demas van por ENCODER_TARGET_AJUSTE.
 *
 * Devuelve 0 si ese ajuste NO se puede enganchar: calibrar, dormir e
 * informacion son acciones, no valores, y un mando no puede "mover" una
 * accion. La pantalla de Func los deja fuera por esta misma funcion, asi que
 * no hay una segunda lista de cuales valen.
 */
static uint8_t ajuste_enganchar(uint8_t id)
{
    encoder_target_t t;

    switch (id) {
    case AJ_VOL:        t = ENCODER_TARGET_VOLUME;     break;
    case AJ_BRILLO:     t = ENCODER_TARGET_BACKLIGHT;  break;
    case AJ_ESCALA:     t = ENCODER_TARGET_SCALE;      break;
    case AJ_SQL:        t = ENCODER_TARGET_SQUELCH;    break;
    case AJ_SUAVIZ:     t = ENCODER_TARGET_SMOOTH;     break;
    case AJ_PGA:        t = ENCODER_TARGET_PGA;        break;
    case AJ_NR:         t = ENCODER_TARGET_NR;         break;
    case AJ_RTTY_SHIFT: t = ENCODER_TARGET_RTTY_SHIFT; break;
    case AJ_CW_TONO:    t = ENCODER_TARGET_CW_TONE;    break;
    case AJ_BW:         t = ENCODER_TARGET_FILTRO;     break;
    case AJ_CAL_TACTIL: case AJ_CAL_PPM: case AJ_DORMIR: case AJ_INFO:
    case AJ_HORA:      case AJ_QTH:
        return 0U;   /* son acciones, no valores */
    default:
        if (ajuste_estados(id) < 2U) { return 0U; }
        t = ENCODER_TARGET_AJUSTE;
        break;
    }

    s_encoder_ajuste = id;
    s_encoder_target = t;
    return 1U;
}

/* Suelta el mando y lo devuelve a la sintonia. */
static void ajuste_soltar(void)
{
    s_encoder_ajuste = AJ_ENGANCHE_NINGUNO;
    s_encoder_target = ENCODER_TARGET_TUNE;
}

/*
 * CUANTOS ESTADOS TIENE UN AJUSTE CICLICO, o 0 si no es de esos.
 *
 * Es lo unico que hace falta saber de nuevo para que el mando pueda mover
 * cualquier ajuste: ya existe ajuste_accion(), que avanza uno. Con el numero
 * de estados, ir hacia atras es avanzar n-1 veces, y no hay que escribir un
 * "anterior" por ajuste que se quedaria desincronizado del "siguiente" el dia
 * que alguien anada un estado.
 *
 * Los de rango continuo devuelven 0: esos no pasan por aqui, tienen su propio
 * destino de mando desde hace tiempo (ver ajuste_enganchar()).
 */
static uint8_t ajuste_estados(uint8_t id)
{
    switch (id) {
    case AJ_AGC:       return 4U;
    case AJ_BW:        return 3U;
    case AJ_ATT:       return 3U;
    case AJ_TACTIL:    return 3U;
    case AJ_ESTILO:    return 3U;
    case AJ_ZOOM:      return 4U;
    case AJ_CONTORNO:  return (uint8_t)(SPECTRUM_LINE_SMOOTH_MAX + 1);
    case AJ_WFVEL:     return WF_VEL_N;
    case AJ_ANALIZ:    return (uint8_t)(ANALIZ_N + 1U);
    case AJ_NCO:       return 2U;
    case AJ_PUENTE:    return 2U;
    case AJ_RDS:       return 2U;
    case AJ_RTTY_BAUD: return 4U;
    case AJ_WFX_LPM:   return WEFAX_LPM_N;
    case AJ_NOTCH:     return 5U;   /* apagado + cuatro fuerzas */
    case AJ_NB:        return 4U;   /* apagado + tres grados */
    /* 23/09/2026: faltaba. Velocidad CW cicla por k_cw_wpm[] igual que
     * Baudios por su tabla, pero al no estar aqui caia en el default,
     * devolvia 0, y ajuste_enganchar() la rechazaba: en el selector de Func
     * tocabas "Velocidad CW" y no pasaba absolutamente nada -ni engancha, ni
     * cierra, ni avisa-, que se vive como una celda muerta. */
    case AJ_CW_PPM:    return (uint8_t)CW_WPM_OPCIONES;
    /* De dos estados: da igual el sentido, un paso los cambia. */
    case AJ_RFAGC: case AJ_AUTOESC: case AJ_TRAZA: case AJ_ALTAVOZ:
    case AJ_TASA:  case AJ_IFBW:    case AJ_RTTY_INV: case AJ_CW_AUTO:
    case AJ_IDIOMA: case AJ_MANDO:
        return 2U;
    /* Paleta y tema tienen muchos estados y un indice de verdad, asi que se
     * mueven poniendo el indice en vez de avanzando n-1 veces. Ver
     * ajuste_ciclar(). */
    case AJ_PALETA:    return spectrum_palette_count();
    case AJ_TEMA:      return (uint8_t)TEMA_COUNT;
    default:           return 0U;
    }
}

/* Mueve un ajuste ciclico `pasos` posiciones, en el sentido que sea. */
/*
 * LOS AJUSTES QUE SE GUARDAN POR TABLA - 23/09/2026.
 *
 * Hasta hoy CONFIG.CSV guardaba 23 claves y se dejaba fuera diecisiete
 * ajustes: el AGC, el silenciador, la reduccion de ruido, el AGC de RF, la
 * escala, la autoescala, el zoom, el contorno, la firmeza del tactil, el
 * filtro de FM, los tres de RTTY y los tres de CW. Se perdian en cada
 * apagado. La firmeza del tactil era el que mas cantaba: es algo que se
 * ajusta una vez y se olvida, y volvia solo cada vez que arrancabas.
 *
 * Aqui estan los dos extremos de la tabla de settings.h: leerlos (para
 * guardar) y aplicarlos (al arrancar). settings.c no sabe nada de rangos ni
 * de setters; este fichero no sabe nada de claves ni de formato. Anadir un
 * ajuste es una linea en el enum, una en la tabla de claves y una en cada
 * una de estas dos funciones, y tools/persist_check.py comprueba que no
 * falte ninguna de las cuatro.
 *
 * Los de coma flotante se guardan redondeados a entero: es la resolucion
 * que enseña la pantalla, asi que guardar mas seria guardar ruido.
 */
/*
 * EN QUE FORMATO SE GUARDAN LAS FOTOS DE SSTV
 * -------------------------------------------
 * *** 24/09/2026, por el dueno del proyecto: "pon un boton en la ventana
 * de decodificacion que tenga las dos opciones" ***
 *
 * sstv_linea() entrega tres bytes por punto, asi que un BMP de 24 bits es
 * copia directa: sin conversion, sin paleta, sin mascaras de bits, y lo
 * abre cualquier cosa. 320x256x3 son 240 kB por foto, o sea cuatro en el
 * pendrive.
 *
 * El de 16 bits (RGB565) baja a 164 kB -seis fotos- a cambio de una
 * cabecera con mascaras y de convertir cada punto. Pierde poco a la vista
 * en una foto de radio, pero algun visor viejo lo enseña raro.
 *
 * Cual conviene depende de si te sobra sitio o te sobran fotos, y eso no
 * lo se yo: lo elige el boton.
 */
enum { SSTV_GUARDA_NO = 0, SSTV_GUARDA_24, SSTV_GUARDA_16, SSTV_GUARDA_N };

static const texto_t k_sstv_guarda[SSTV_GUARDA_N] = {
    T("No guardar", "Do not save"), T("BMP 24 bits", "BMP 24 bit"),
    T("BMP 16 bits", "BMP 16 bit")
};

/*
 * Y los dos que enseña el mismo boton mientras trabaja. Estan aqui, al
 * lado de los otros tres, porque tools/gen_rotulos.py los saca de aqui
 * para que sim/digi.c mida que TODOS caben en el boton. Un rotulo que se
 * sale no avisa: se pinta encima del de al lado.
 */
static const texto_t k_sstv_gsv_estado[] = {
    /* Las cifras las pone gsv_rotulo() copiando los DIEZ primeros
     * caracteres: "Guardando " y "Saving    " miden los dos diez a
     * proposito, rellenando con espacios el ingles, para que el bucle no
     * tenga que saber cual esta puesto. */
    T("Guardando 000", "Saving    000"),
    T("Fallo al guardar", "Save failed")
};

static uint8_t s_sstv_guarda;

/*
 * EL MODO CON EL QUE ARRANCA "EMPEZAR"
 * ------------------------------------
 * *** 24/09/2026, por el dueno del proyecto: "lo de martin 1 es tocable?",
 * "crea una ventana al tocar que salgan todos los modos y elijo uno, tocar
 * 14 veces es un coñazo" ***
 *
 * Hasta hoy "Empezar" llamaba a sstv_fuerza(0U) con un comentario que
 * decia "Martin 1, el mas usado". Y es verdad que es el mas usado, pero la
 * radio conoce catorce y a los otros trece no habia forma de llegar: si te
 * enganchas a mitad de una Scottie 1 y la cabecera VIS ya paso, no puedes
 * decirle que es Scottie 1.
 *
 * Ahora la chapa que dice el modo se toca y abre una rejilla con los
 * catorce. Empieza en 0 -Martin 1- porque sigue siendo el mas usado.
 */
static uint8_t s_sstv_modo_idx;

/*
 * GUARDAR LAS CARTAS DE FAX
 * -------------------------
 * Mismo boton, mismo sitio y misma maquinaria que en SSTV. Lo que cambia:
 *
 *   - un fax es de 756 puntos de ancho en GRIS de 8 bits, asi que cada
 *     renglon ocupa 756 bytes - casi el doble que una fila de SSTV a 16.
 *   - la altura NO se sabe: el fax acaba cuando llega el tono de parada de
 *     450 Hz, que puede ser a los tres minutos o a los diez. Por eso hay
 *     un tope, y por eso la cabecera se rehace al cerrar con la altura de
 *     verdad y el fichero se recorta.
 *
 * El tope son 600 lineas = 5 minutos a 120 lpm, que cubre la mayoria de
 * cartas. La reserva pide 448 kB SEGUIDOS, asi que subirlo tiene un coste
 * que no es solo disco: cuanto mas grande, mas facil es que no haya un
 * hueco entero donde meterlo.
 */
#define WFX_ALTO_MAX  600U

enum { WFX_GUARDA_NO = 0, WFX_GUARDA_SI, WFX_GUARDA_N };

static const texto_t k_wfx_guarda[WFX_GUARDA_N] = {
    T("No guardar", "Do not save"), T("BMP gris", "Grey BMP")
};

static uint8_t s_wfx_guarda;

static void extras_leer(settings_extra_t *e)
{
    e->presentes = 0U;   /* al guardar no significa nada; se deja limpio */
    e->v[SET_X_AGC]        = (int32_t)demod_am_get_agc_profile();
    e->v[SET_X_SQL]        = (int32_t)demod_am_get_squelch_db();
    e->v[SET_X_NR]         = (int32_t)s_nr_strength;
    e->v[SET_X_NR_ON]      = (int32_t)s_nr_on;
    e->v[SET_X_NB]         = (int32_t)nb_get_nivel();
    e->v[SET_X_RFAGC]      = (int32_t)s_rf_agc_enabled;
    e->v[SET_X_ESCALA_LO]  = (int32_t)s_db_min;
    e->v[SET_X_ESCALA_HI]  = (int32_t)s_db_max;
    e->v[SET_X_AUTOESC]    = (int32_t)s_spec_agc_enabled;
    e->v[SET_X_ZOOM]       = (int32_t)s_spec_zoom;
    e->v[SET_X_CONTORNO]   = (int32_t)s_spec_smooth_passes;
    e->v[SET_X_WFVEL]      = (int32_t)k_wf[s_wf_vel].clave;
    e->v[SET_X_ANALIZ]     = (int32_t)s_analiz;
    e->v[SET_X_NCO]        = (int32_t)s_nco_on;
    e->v[SET_X_PUENTE]     = (int32_t)spectrum_get_bridge();
    e->v[SET_X_RDS]        = (int32_t)rds_activo();
    e->v[SET_X_TACTIL]     = (int32_t)touch_get_firmeza();
    e->v[SET_X_IFBW]       = (int32_t)demod_am_get_wfm_ifbw();
    e->v[SET_X_RTTY_SHIFT] = (int32_t)(rtty_get_shift_hz() + 0.5f);
    e->v[SET_X_RTTY_BAUD]  = (int32_t)s_rtty_baud_idx;
    e->v[SET_X_RTTY_INV]   = (int32_t)rtty_get_station_inverted();
    e->v[SET_X_CW_TONO]    = (int32_t)(cw_get_pitch_hz() + 0.5f);
    e->v[SET_X_CW_WPM]     = (int32_t)s_cw_wpm_idx;
    e->v[SET_X_CW_AUTO]    = (int32_t)cw_get_autotune();
    e->v[SET_X_WFX_LPM]    = (int32_t)wefax_get_lpm_idx();
    /* Apagado se guarda como 0 y las fuerzas como 1..4, para que un
     * CONFIG.CSV sin esta clave arranque con el notch apagado. */
    e->v[SET_X_NOTCH]      = anotch_activo() ? (int32_t)(anotch_fuerza() + 1) : 0;
    e->v[SET_X_SSTV_GUARDA] = (int32_t)s_sstv_guarda;
    e->v[SET_X_WFX_GUARDA]  = (int32_t)s_wfx_guarda;
    e->v[SET_X_IDIOMA]      = (int32_t)idioma();
    e->v[SET_X_ENC_INV]     = (int32_t)encoder_invertido();
    /*
     * Las cuatro familias, ESCRITAS UNA A UNA y no en un bucle sobre
     * SET_X_FIL_SSB + f. El bucle funcionaba igual de bien, pero
     * tools/persist_check.py comprueba que cada clave se toca buscando su
     * nombre en el codigo, y con el bucle tres de los cuatro nombres no
     * aparecian en ningun sitio: la comprobacion daba fallo, y tenia
     * razon en darlo. Una clave que nadie puede ver que se guarda es una
     * clave que el dia que se rompa no lo va a notar nadie.
     */
    e->v[SET_X_FIL_SSB] = fil_empaqueta((uint8_t)FIL_FAM_SSB);
    e->v[SET_X_FIL_AM]  = fil_empaqueta((uint8_t)FIL_FAM_AM);
    e->v[SET_X_FIL_SAM] = fil_empaqueta((uint8_t)FIL_FAM_SAM);
    e->v[SET_X_FIL_NFM] = fil_empaqueta((uint8_t)FIL_FAM_NFM);
}

/* Recorta un entero a un rango. Cada ajuste recorta EL SUYO aqui y no en
 * settings.c: un CONFIG.CSV editado a mano, o escrito por una version del
 * firmware con mas opciones, no puede meter un indice fuera de tabla. */
_Static_assert((int)WFM_IFBW_WIDE < (int)WFM_IFBW_NARROW,
               "extras_aplicar() recorta el filtro de FM con WIDE como minimo: si se reordena el enum, el recorte se invierte");
_Static_assert((int)AGC_PROFILE_MANUAL == 0 && (int)AGC_PROFILE_FAST == 3,
               "extras_aplicar() recorta el perfil de AGC a 0..3");
_Static_assert((int)SPEC_ZOOM_1X < (int)SPEC_ZOOM_8X,
               "extras_aplicar() recorta el zoom con 1X como minimo");

static int32_t ex_rec(int32_t v, int32_t lo, int32_t hi)
{
    /* Un rango del reves es un fallo de quien llama, no del fichero de
     * configuracion, y sin esto no recorta: DEVUELVE EL CONTRARIO de lo que
     * le entra. Asi entro el filtro de FM invertido (ver su comentario), y
     * fue mudo. Aqui se endereza y se dice en voz alta. */
    if (lo > hi) {
        int32_t t = lo; lo = hi; hi = t;
        debug_print("extras: *** rango del reves en ex_rec() - mira los enums ***\n");
    }
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

static void extras_aplicar(const settings_extra_t *e)
{
    /* Una clave que no venia en el fichero (primer arranque, o un
     * CONFIG.CSV de antes de que existiera) deja el valor por defecto
     * compilado, que es justo lo que habia antes de todo esto. */
    /*
     * (uint64_t) y no (uint32_t). `presentes` se amplio a 64 bits el
     * 24/09/2026 justo para que cupieran mas claves, pero ESTA macro -la
     * unica que lo lee dentro de extras_aplicar()- se quedo con el
     * desplazamiento de 32. Con 31 claves nunca se noto; la clave numero
     * 33 se habria leido siempre como ausente, sin un solo aviso del
     * compilador y sin nada raro en pantalla: el ajuste guardado
     * simplemente no volveria. Encontrado el 29/09/2026 al anadir el
     * idioma, que es la clave 32 y aun cabia por los pelos.
     */
    #define EX_HAY(id) ((e->presentes & ((uint64_t)1U << (id))) != 0U)

    /* Punto de partida del contorno: el valor REAL que tiene spectrum.c, no
     * el 0 con el que se declara la variable de este fichero. Si CONFIG.CSV
     * no trae la clave -primer arranque, o un fichero de antes de que
     * existiera- manda este, y asi la pantalla de Ajustes dice exactamente
     * lo que se esta dibujando. Ver el comentario de s_spec_smooth_passes. */
    s_spec_smooth_passes = spectrum_get_line_smooth();

    if (EX_HAY(SET_X_AGC)) {
        demod_am_set_agc_profile((agc_profile_t)ex_rec(e->v[SET_X_AGC], 0, 3));
    }
    if (EX_HAY(SET_X_SQL)) {
        demod_am_set_squelch_db((float)ex_rec(e->v[SET_X_SQL],
                                              (int32_t)SQUELCH_DB_FLOOR,
                                              (int32_t)SQUELCH_DB_CEIL));
    }
    if (EX_HAY(SET_X_NR)) {
        s_nr_strength = (uint16_t)ex_rec(e->v[SET_X_NR], 0, (int32_t)NR_STRENGTH_MAX);
        nr_ss_set_strength(s_nr_strength);
    }
    /*
     * El INTERRUPTOR de la reduccion de ruido, no solo su intensidad.
     *
     * *** El dueno: "arregla eso que has encontrado que no se guarda". ***
     * Se guardaba "nr_strength" desde siempre y el encendido no, asi que la
     * NR arrancaba apagada por mucho que la dejaras puesta. Se noto por un
     * camino torcido: dos videos suyos con el mismo CONFIG.CSV y el boton
     * en estados distintos, que yo achaque a un fichero danado hasta que el
     * dijo lo evidente -los dos leen el mismo fichero- y resulto que el
     * dato no estaba en ningun fichero.
     *
     * Va detras del NB para no separarlo de su intensidad, y nr_ss_set_
     * enabled() se llama aqui mismo porque s_nr_on es solo el espejo: quien
     * manda de verdad es el modulo.
     */
    if (EX_HAY(SET_X_NR_ON)) {
        s_nr_on = (uint8_t)(ex_rec(e->v[SET_X_NR_ON], 0, 1) != 0);
        nr_ss_set_enabled(s_nr_on);
    }
    if (EX_HAY(SET_X_NB)) {
        nb_set_nivel((uint8_t)ex_rec(e->v[SET_X_NB], 0, 3));
    }
    if (EX_HAY(SET_X_RFAGC)) {
        /* Solo la bandera: la atenuacion de partida ya la ha puesto el
         * bloque de att_rin_level de main(), y si el automatico esta
         * encendido converge solo en las primeras vueltas del bucle. */
        s_rf_agc_enabled = (uint8_t)ex_rec(e->v[SET_X_RFAGC], 0, 1);
    }
    /* La escala, los dos limites a la vez y en este orden: el suelo primero
     * contra el techo que YA hay, y luego el techo contra el suelo nuevo.
     * Al reves, un fichero con la escala muy movida se recortaba contra los
     * valores compilados y entraba a medias. */
    if (EX_HAY(SET_X_ESCALA_LO)) {
        s_db_min = (float)ex_rec(e->v[SET_X_ESCALA_LO],
                                 (int32_t)SPECTRUM_DB_FLOOR,
                                 (int32_t)(SPECTRUM_DB_CEIL - SPECTRUM_DB_MIN_GAP));
    }
    if (EX_HAY(SET_X_ESCALA_HI)) {
        s_db_max = (float)ex_rec(e->v[SET_X_ESCALA_HI],
                                 (int32_t)(s_db_min + SPECTRUM_DB_MIN_GAP),
                                 (int32_t)SPECTRUM_DB_CEIL);
    }
    if (s_db_max < s_db_min + SPECTRUM_DB_MIN_GAP) {   /* cinturon y tirantes */
        s_db_max = s_db_min + SPECTRUM_DB_MIN_GAP;
    }
    if (EX_HAY(SET_X_AUTOESC)) {
        s_spec_agc_enabled = (uint8_t)ex_rec(e->v[SET_X_AUTOESC], 0, 1);
    }
    if (EX_HAY(SET_X_ZOOM)) {
        s_spec_zoom = (spec_zoom_t)ex_rec(e->v[SET_X_ZOOM],
                                          (int32_t)SPEC_ZOOM_1X, (int32_t)SPEC_ZOOM_8X);
        zoom_decimators_init();   /* el zoom no es solo un numero: rehace los diezmadores */
    }
    if (EX_HAY(SET_X_ANALIZ)) {
        s_analiz = (uint8_t)ex_rec(e->v[SET_X_ANALIZ], 0, (int32_t)ANALIZ_N);
        analiz_aplicar();
    }
    if (EX_HAY(SET_X_IDIOMA)) {
        /* idioma_pon() ya recorta por su cuenta -ver su comentario-, pero
         * el recorte va igualmente aqui: es la regla de este bloque, "cada
         * ajuste recorta EL SUYO", y confiar en que el otro extremo recorte
         * es justo como entro el filtro de FM invertido. */
        idioma_pon((uint8_t)ex_rec(e->v[SET_X_IDIOMA],
                                   (int32_t)IDIOMA_ES, (int32_t)IDIOMA_EN));
    }
    if (EX_HAY(SET_X_ENC_INV)) {
        encoder_invertido_pon((uint8_t)ex_rec(e->v[SET_X_ENC_INV], 0, 1));
    }
    if (EX_HAY(SET_X_NCO)) {
        /* Solo el ajuste. El oscilador NO se reaparca aqui: apply_lo_tune()
         * ya corre justo despues en el arranque y decide entonces, con la
         * frecuencia cargada ya puesta. Tocarlo aqui seria decidir dos
         * veces. */
        s_nco_on = (uint8_t)ex_rec(e->v[SET_X_NCO], 0, 1);
    }
    if (EX_HAY(SET_X_PUENTE)) {
        spectrum_set_bridge((uint8_t)ex_rec(e->v[SET_X_PUENTE], 0, 1));
    }
    if (EX_HAY(SET_X_RDS)) {
        if (ex_rec(e->v[SET_X_RDS], 0, 1)) { rds_start(192000.0f); }
    }
    if (EX_HAY(SET_X_WFVEL)) {
        s_wf_vel = wf_vel_por_clave((uint8_t)ex_rec(e->v[SET_X_WFVEL], 0, 255));
        s_wf_frames = 0U;
    }
    if (EX_HAY(SET_X_CONTORNO)) {
        s_spec_smooth_passes = (uint8_t)ex_rec(e->v[SET_X_CONTORNO], 0, SPECTRUM_LINE_SMOOTH_MAX);
        spectrum_set_line_smooth(s_spec_smooth_passes);
    }
    if (EX_HAY(SET_X_TACTIL)) {
        touch_set_firmeza((uint8_t)ex_rec(e->v[SET_X_TACTIL], 1, 3));
    }
    if (EX_HAY(SET_X_IFBW)) {
        /* OJO con el orden: WFM_IFBW_WIDE vale 0 y NARROW vale 1, al reves
         * de lo que sugieren los nombres. La primera version de esta linea
         * los puso en orden de nombre y el recorte quedaba invertido (lo=1,
         * hi=0), asi que devolvia SIEMPRE el contrario de lo guardado: el
         * filtro de FM salia al reves en cada arranque. El _Static_assert
         * de abajo hace que un dia que se reordene el enum falle la
         * compilacion en vez de volver a invertirse en silencio. */
        demod_am_set_wfm_ifbw((wfm_ifbw_t)ex_rec(e->v[SET_X_IFBW],
                                                 (int32_t)WFM_IFBW_WIDE, (int32_t)WFM_IFBW_NARROW));
    }
    if (EX_HAY(SET_X_RTTY_SHIFT)) {
        rtty_set_shift_hz((float)ex_rec(e->v[SET_X_RTTY_SHIFT],
                                        (int32_t)CONFIG_RTTY_SHIFT_MIN_HZ,
                                        (int32_t)CONFIG_RTTY_SHIFT_MAX_HZ));
    }
    if (EX_HAY(SET_X_RTTY_BAUD)) {
        s_rtty_baud_idx = (uint8_t)ex_rec(e->v[SET_X_RTTY_BAUD], 0, (int32_t)RTTY_BAUD_COUNT - 1);
        rtty_set_baud(k_rtty_baud_values[s_rtty_baud_idx]);
    }
    if (EX_HAY(SET_X_RTTY_INV)) {
        rtty_set_station_inverted((uint8_t)ex_rec(e->v[SET_X_RTTY_INV], 0, 1));
    }
    if (EX_HAY(SET_X_CW_TONO)) {
        /* El rango lo recorta cw_set_pitch_hz(), que es donde vive - ver su
         * comentario. Aqui solo se le pasa. */
        cw_set_pitch_hz((float)e->v[SET_X_CW_TONO]);
        demod_am_set_cw_filter_hz(cw_get_pitch_hz());
    }
    if (EX_HAY(SET_X_CW_WPM)) {
        s_cw_wpm_idx = (uint8_t)ex_rec(e->v[SET_X_CW_WPM], 0, (int32_t)CW_WPM_OPCIONES - 1);
        cw_set_wpm_hint((float)k_cw_wpm[s_cw_wpm_idx]);
    }
    if (EX_HAY(SET_X_CW_AUTO)) {
        cw_set_autotune((uint8_t)ex_rec(e->v[SET_X_CW_AUTO], 0, 1));
    }
    if (EX_HAY(SET_X_WFX_LPM)) {
        wefax_set_lpm_idx((uint8_t)ex_rec(e->v[SET_X_WFX_LPM], 0,
                                          (int32_t)WEFAX_LPM_N - 1));
    }
    /* Una a una, por la misma razon que al guardarlas. */
    if (EX_HAY(SET_X_FIL_SSB)) { fil_desempaqueta((uint8_t)FIL_FAM_SSB, e->v[SET_X_FIL_SSB]); }
    if (EX_HAY(SET_X_FIL_AM))  { fil_desempaqueta((uint8_t)FIL_FAM_AM,  e->v[SET_X_FIL_AM]);  }
    if (EX_HAY(SET_X_FIL_SAM)) { fil_desempaqueta((uint8_t)FIL_FAM_SAM, e->v[SET_X_FIL_SAM]); }
    if (EX_HAY(SET_X_FIL_NFM)) { fil_desempaqueta((uint8_t)FIL_FAM_NFM, e->v[SET_X_FIL_NFM]); }
    if (EX_HAY(SET_X_NOTCH)) {
        int32_t v = ex_rec(e->v[SET_X_NOTCH], 0, 4);
        if (v == 0) { anotch_set_activo(0U); }
        else { anotch_set_fuerza((uint8_t)(v - 1)); anotch_set_activo(1U); }
    }
    if (EX_HAY(SET_X_SSTV_GUARDA)) {
        s_sstv_guarda = (uint8_t)ex_rec(e->v[SET_X_SSTV_GUARDA], 0,
                                        (int32_t)SSTV_GUARDA_N - 1);
    }
    if (EX_HAY(SET_X_WFX_GUARDA)) {
        s_wfx_guarda = (uint8_t)ex_rec(e->v[SET_X_WFX_GUARDA], 0,
                                       (int32_t)WFX_GUARDA_N - 1);
    }
    #undef EX_HAY
}

static void ajuste_ciclar(uint8_t id, int32_t pasos)
{
    uint8_t n = ajuste_estados(id);
    int32_t k;

    if (n < 2U || pasos == 0) { return; }

    /* Los dos que tienen indice propio: un solo cambio, sin pasar por los
     * de en medio. Con quince paletas, ir hacia atras avanzando catorce
     * veces se veria. */
    if (id == AJ_PALETA || id == AJ_TEMA) {
        int32_t cur = (id == AJ_PALETA) ? (int32_t)spectrum_get_palette()
                                        : (int32_t)s_tema_idx;
        int32_t nuevo = (cur + pasos) % (int32_t)n;
        if (nuevo < 0) { nuevo += (int32_t)n; }
        if (id == AJ_PALETA) {
            spectrum_set_palette((spectrum_palette_t)nuevo);
            if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
            settings_value_redraw();
        } else {
            tema_aplicar((uint8_t)nuevo);
            if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
            radio_screen_draw();   /* un tema cambia TODOS los colores */
        }
        return;
    }

    /* El resto: avanzar es llamar a la accion que ya existe. Hacia atras,
     * n-1 avances. No se escribe un "anterior" por ajuste a proposito: seria
     * una segunda copia de la misma maquina de estados, y esa es justo la
     * clase de duplicado que en este fichero ya ha fallado tres veces. */
    k = pasos % (int32_t)n;
    if (k < 0) { k += (int32_t)n; }
    while (k-- > 0) { ajuste_accion(id); }
}

/* El nombre de un ajuste, de la MISMA tabla que pinta la pantalla. */
static const char *ajuste_nombre(uint8_t id)
{
    uint16_t i;
    for (i = 0U; i < (uint16_t)AJUSTE_COUNT; i++) {
        if (k_ajustes[i].id == id) { return k_ajustes[i].nombre[idioma()]; }
    }
    return "";
}

static void ajuste_accion(uint8_t id)
{
    switch (id) {
    case AJ_AGC:    menu_tile_agc_callback(0, UI_EVENT_RELEASE, 0); break;
    /* Desde la etapa 34 la celda abre el ajuste fino de los dos cortes en
     * vez de recorrer una lista: el recorrido rapido sigue estando en el
     * boton de la barra de abajo, que es donde se usa de verdad. */
    case AJ_BW:     menu_detail_show(ENCODER_TARGET_FILTRO); break;
    case AJ_RFAGC:  menu_tile_rfagc_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_ATT:    menu_tile_att_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_AUTOESC: menu_tile_specagc_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_ESTILO: menu_tile_spec_style_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_ZOOM:   menu_tile_zoom_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_PALETA: paleta_cambiar(); break;
    case AJ_TRAZA:  menu_tile_trace_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_CONTORNO: menu_tile_nb_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_WFVEL:    wf_vel_siguiente(); break;
    case AJ_ANALIZ:   analiz_siguiente(); break;
    case AJ_NCO:      nco_conmutar(); break;
    case AJ_PUENTE:   puente_conmutar(); break;
    case AJ_RDS:      rds_conmutar(); break;
    case AJ_ALTAVOZ: menu_tile_speaker_pa_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_TASA:   menu_tile_rate_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_IFBW:   menu_tile_ifbw_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_RTTY_BAUD: menu_tile_rtty_baud_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_RTTY_INV:  menu_tile_rtty_inv_callback(0, UI_EVENT_RELEASE, 0); break;
    case AJ_NOTCH: {
        /* Apagado, suave, normal, fuerte, a saco, y vuelta. */
        uint8_t n = anotch_activo() ? (uint8_t)(anotch_fuerza() + 1U) : 0U;
        if (n >= 4U) { anotch_set_activo(0U); }
        else { anotch_set_fuerza(n); anotch_set_activo(1U); }
        if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
        settings_value_redraw();
        break;
    }
    case AJ_WFX_LPM:
        wefax_set_lpm_idx((uint8_t)((wefax_get_lpm_idx() + 1U) % WEFAX_LPM_N));
        if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
        settings_value_redraw();
        break;
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
    case AJ_NB:
        nb_set_nivel((uint8_t)((nb_get_nivel() + 1U) % 4U));
        if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
        settings_value_redraw();
        break;
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
    case AJ_IDIOMA:     idioma_cambiar(); break;
    case AJ_MANDO:
        encoder_invertido_pon((uint8_t)(encoder_invertido() ? 0U : 1U));
        settings_value_redraw();
        break;
    case AJ_INFO:       grid_show(GRID_INFO); break;
    case AJ_HORA:       hora_show(); break;
    case AJ_QTH:        qth_show(); break;
    default: break;
    }

    /*
     * Y AHORA, PARA TODOS. 25/09/2026.
     *
     * Hasta hoy cada caso de ahi arriba tenia que acordarse de llamar a
     * settings_mark_dirty() por su cuenta, y unos cuantos no se acordaban:
     * la velocidad de la cascada, la sintonia automatica de CW... El dueno
     * lo noto como "siempre que arranco me pone rapida y tengo que
     * cambiarla". Cuarenta y tantos casos y una regla que hay que recordar
     * en cada uno es una regla que se va a romper; ya se rompio.
     *
     * Asi que se le da la vuelta al defecto: se avisa SIEMPRE, y la lista
     * corta es la de los que NO cambian nada que guardar -los que solo
     * abren otra pantalla-. Avisar de mas no rompe nada: settings_poll()
     * espera su tiempo y compara, y un guardado igual al anterior es un
     * desperdicio pequeno; no avisar deja un ajuste que el usuario cree
     * cambiado y que se pierde al apagar, que es mucho peor.
     *
     * Los casos que ya avisaban por dentro siguen haciendolo. Avisar dos
     * veces es poner una bandera a uno dos veces.
     */
    switch (id) {
    case AJ_INFO:
    case AJ_HORA:
    case AJ_QTH:
    case AJ_DORMIR:
    case AJ_CAL_TACTIL:
    case AJ_CAL_PPM:
        break;   /* solo abren pantalla: no hay nada que guardar */
    default:
        if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
        break;
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
    switch (s_cfg_pant) {
    case CFG_BANDAS: return (uint16_t)BAND_PRESET_COUNT;
    case CFG_MODOS:  return (uint16_t)DEMOD_MODE_ENTRY_COUNT;
    default:         return (uint16_t)AJUSTE_COUNT;
    }
}
static uint8_t cfg_cat_de(uint16_t i)
{
    switch (s_cfg_pant) {
    case CFG_BANDAS: return k_band_presets[i].familia;
    case CFG_MODOS:  return k_demod_modes[i].fam;
    default:         return k_ajustes[i].pagina;
    }
}
static uint8_t cfg_cats(void)
{
    switch (s_cfg_pant) {
    case CFG_BANDAS: return BAND_FAM_COUNT;
    case CFG_MODOS:  return MODO_FAM_COUNT;
    default:         return AJ_PAGINAS;
    }
}
static const char *cfg_cat_nombre(uint8_t i)
{
    switch (s_cfg_pant) {
    case CFG_BANDAS: return k_band_familias[i][idioma()];
    case CFG_MODOS:  return k_modo_familias[i][idioma()];
    default:         return k_aj_paginas[i][idioma()];
    }
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

/*
 * El nombre del demodulador sobre el que va un modo, o "" si la etiqueta ya
 * lo dice. Sale de la MISMA tabla, buscando la entrada analogica que tiene
 * ese demodulador: asi no hay una segunda lista de nombres que mantener al
 * dia, que es como se han separado tres tablas en este proyecto.
 */
static const char *modo_base_nombre(uint8_t k)
{
    uint8_t m;

    for (m = 0U; m < (uint8_t)DEMOD_MODE_ENTRY_COUNT; m++) {
        if (k_demod_modes[m].fam != MODO_FAM_ANA) { continue; }
        if (k_demod_modes[m].mode != k_demod_modes[k].mode) { continue; }
        return (m == k) ? "" : k_demod_modes[m].label;
    }
    return "";
}

static void cfg_fill(void)
{
    uint8_t i, n = 0, cats = cfg_cats();
    uint16_t k, total = cfg_total();

    for (i = 0U; i < UIC_CATS; i++) {
        s_cfg.cat[i] = (i < cats) ? cfg_cat_nombre(i) : "";
    }
    s_cfg.cats    = cats;
    /* Reparto denso (4x4) en bandas y en modos: las dos son listas de
     * celdas cortas -etiqueta y una linea- y la categoria mas larga de
     * cada una cabe de sobra en 16. Ajustes va en el reparto ancho
     * porque sus valores son mas largos. */
    s_cfg.denso   = (uint8_t)(s_cfg_pant == CFG_BANDAS || s_cfg_pant == CFG_MODOS);
    s_cfg.cat_sel = s_cfg_cat;
    s_cfg.cursor  = s_cfg_cursor;
    s_cfg.pressed = s_cfg_press;
    s_cfg.marcada = 0xFFU;

    for (k = 0; k < total && n < UIC_CELLS; k++) {
        if (cfg_cat_de(k) != s_cfg_cat) { continue; }

        if (s_cfg_pant == CFG_BANDAS) {
            const band_preset_t *b = &k_band_presets[k];
            uint8_t m;

            s_cfg.cel[n].nombre = b->label[idioma()];
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
        } else if (s_cfg_pant == CFG_MODOS) {
            s_cfg.cel[n].nombre = k_demod_modes[k].label;
            s_cfg.cel[n].valor  = k_demod_modes[k].desc[idioma()];
            /* El tercer renglon dice SOBRE QUE va, y solo cuando no es
             * evidente: CW, RTTY, NAVTEX, WEFAX y SSTV son banda lateral
             * por debajo y APRS es FM estrecha, que es la diferencia que
             * decide donde hay que sintonizar y que no dice el nombre.
             * En los analogicos la etiqueta YA es el demodulador, asi que
             * ahi se queda vacio en vez de repetirse. */
            s_cfg.cel[n].extra  = modo_base_nombre((uint8_t)k);
            /* Misma condicion que la etiqueta del boton de modo, y por eso
             * la misma funcion: ver demod_mode_entry_active(). */
            if (demod_mode_entry_active((uint8_t)k)) { s_cfg.marcada = n; }
        } else {
            const ajuste_t *a = &k_ajustes[k];
            const char *v = ajuste_valor(a->id, s_cfg_val[n]);

            s_cfg.cel[n].nombre = a->nombre[idioma()];
            /* Los que son una accion y no un valor dejan el renglon vacio,
             * igual que en la rejilla: "tocar" vale para toda la pantalla. */
            s_cfg.cel[n].valor  = v ? v : "";
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
    debug_print(k_temas[i].nombre[IDIOMA_ES]);   /* el UART, siempre en espanol */
    debug_print("\n");

    /*
     * LO QUE SE VE EN TODAS LAS PANTALLAS, REPINTADO AQUI - 23/09/2026, por
     * un aviso del dueno: "cuando cambias de tema solo se aplica en la zona
     * central, hasta que no sale a la pantalla principal no se aplica a
     * todo".
     *
     * Y era exactamente eso. Cambiar el tema tocando su casilla llamaba a
     * tema_cambiar(), que repintaba... la pantalla de ajustes VIEJA (s_cfg),
     * que no es la que esta puesta; y despues grid_apply() repintaba la
     * rejilla. O sea que la zona central se ponia del color nuevo y la
     * cabecera, la barra de estado y la barra de acciones se quedaban con el
     * viejo hasta salir.
     *
     * El arreglo va AQUI, en la funcion que cambia la paleta, y no en cada
     * camino que la llama: hay tres (tocar la casilla, el mando enganchado,
     * cargar los ajustes al arrancar) y anadir un cuarto sin acordarse de
     * repintar volveria a dejar media pantalla con el tema anterior sin que
     * nada avise. Lo de en medio si se queda en manos de quien llama, porque
     * cada pantalla sabe repintarse a si misma y desde aqui no se sabe cual
     * hay puesta.
     */
    if (s_pantalla_pintada && !s_screen_asleep) {
        freq_display_draw();   /* la cabecera entera - ver su cuerpo */
        badges_draw();         /* la barra de estado entera */
        act_draw();            /* la barra de acciones de abajo */
    }
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
    /* La pantalla de ajustes vieja, SOLO si es la que esta puesta. Sin el
     * guardia esto repintaba s_cfg encima de la rejilla, que es otra
     * pantalla distinta: no se veia el destrozo porque grid_apply() volvia a
     * pintar la rejilla justo despues, pero era un dibujo entero tirado a la
     * basura en cada toque. */
    if (s_menu_cfg_active) {
        cfg_fill();
        ui_cfg_draw(&s_cfg);
    }
}

/*
 * ALTERNA EL IDIOMA Y REPINTA LO QUE SE VE EN TODAS LAS PANTALLAS.
 *
 * Es el mismo problema que tuvo el tema el 23/09/2026 y se resuelve igual,
 * copiando su solucion a proposito: el idioma no cambia solo la celda que
 * has tocado, cambia la cabecera, la barra de estado y la barra de acciones
 * de abajo. Sin este repintado tocarias "Idioma" y verias el valor en
 * ingles con "Ajustes", "Modo" y "Paso" todavia en espanol, hasta salir a
 * la pantalla principal.
 *
 * Lo de EN MEDIO lo repinta quien llama, porque cada pantalla sabe
 * repintarse a si misma y desde aqui no se sabe cual hay puesta. Aqui solo
 * se cubre el caso de la pantalla de ajustes, que es desde donde se toca.
 */
static void idioma_cambiar(void)
{
    (void)idioma_alterna();
    debug_print("idioma: ");
    debug_print(idioma_nombre());
    debug_print("\n");

    if (s_settings_ready_for_autosave) { settings_mark_dirty(); }

    if (s_pantalla_pintada && !s_screen_asleep) {
        freq_display_draw();   /* la cabecera entera */
        badges_draw();         /* la barra de estado entera */
        act_draw();            /* la barra de acciones de abajo */
    }
    if (s_menu_cfg_active) {
        cfg_fill();
        ui_cfg_draw(&s_cfg);
    }
}

/* ===========================================================================
 * PANTALLA DEL QTH - 29/09/2026
 * ===========================================================================
 * Seis casillas, una por caracter del localizador, y cada posicion solo
 * ofrece su alfabeto. Ver User/ui_qth.h para por que asi y no con un
 * teclado.
 *
 * Lo que se edita es una COPIA (s_qth_edit). El localizador de verdad no
 * cambia hasta que se pulsa Aplicar: salir a medias no puede dejar la radio
 * apuntando a un sitio que no has terminado de escribir.
 */
static ui_qth_state_t s_qth;
static char           s_qth_edit[UIQ_N + 1];
static char           s_qth_coord[28];
static uint8_t        s_menu_qth_active = 0U;

/* El alfabeto de cada posicion, y cuantos tiene. Una sola tabla: la pista
 * que se pinta y el ciclado salen de aqui, asi que no pueden decir cosas
 * distintas. */
static const char *qth_alfabeto(uint8_t i, uint8_t *n)
{
    if (i < 2U) { *n = 18U; return "ABCDEFGHIJKLMNOPQR"; }
    if (i < 4U) { *n = 10U; return "0123456789"; }
    *n = 24U;   return "abcdefghijklmnopqrstuvwx";
}

/*
 * EL RENGLON DE COORDENADAS lo monta ft8_decoder.c - 30/09/2026.
 *
 * Aqui habia el formateo Y, antes, una segunda conversion de localizador a
 * coordenadas que estaba mal (la subcuadricula x10: "IN80fp" salia en
 * Francia). Ahora ni la conversion ni el formato viven en este fichero: los
 * dos son uno solo, en ft8_decoder.c, y sim/qthcoord.c los mide compilando
 * ESE fichero. Dos sitios que contestan la misma pregunta es como se cuela
 * que solo uno este bien.
 */
static void qth_coord_txt(const char *g)
{
    if (!ft8_decoder_grid_coord_txt(g, (int)UIQ_N,
                                    s_qth_coord, sizeof s_qth_coord)) {
        s_qth_coord[0] = '\0';
    }
}

static void qth_fill(void)
{
    uint8_t i, n;

    for (i = 0U; i < UIQ_N; i++) {
        s_qth.loc[i] = s_qth_edit[i];
        (void)qth_alfabeto(i, &n);
        s_qth.pista[i] = (i < 2U) ? "A-R" : ((i < 4U) ? "0-9" : "a-x");
    }
    s_qth.loc[UIQ_N] = '\0';

    qth_coord_txt(s_qth_edit);
    s_qth.coord = s_qth_coord;

    s_qth.titulo = "QTH";   /* igual en los dos idiomas */
    s_qth.pie    = tr("tu localizador: de aquí salen el aspa de los mapas y las distancias",
                      "your locator: the map cross and the distances come from this");
    s_qth.rot[UIQ_HIT_MENOS]   = "-";
    s_qth.rot[UIQ_HIT_MAS]     = "+";
    s_qth.rot[UIQ_HIT_APLICAR] = tr("Aplicar", "Apply");
    s_qth.rot[UIQ_HIT_SALIR]   = tr("Salir", "Exit");

    {
        const char *ya = ft8_decoder_get_own_grid();
        uint8_t dif = 0U;
        for (i = 0U; i < UIQ_N; i++) {
            if (ya[i] != s_qth_edit[i]) { dif = 1U; break; }
        }
        s_qth.cambiado = dif;
    }
}

static void qth_show(void)
{
    const char *ya = ft8_decoder_get_own_grid();
    uint8_t i;

    /*
     * Se parte de lo que hay puesto. Si por lo que sea estuviera vacio -un
     * CONFIG.CSV con un valor que no valido, ver ft8_decoder_set_own_grid()-
     * se arranca en "AA00aa", que es el primero de cada alfabeto: mejor un
     * sitio evidentemente falso que una pantalla en blanco.
     */
    for (i = 0U; i < UIQ_N; i++) {
        char c = ya[i];
        if (c == '\0') { c = (i < 2U) ? 'A' : ((i < 4U) ? '0' : 'a'); }
        s_qth_edit[i] = c;
    }
    s_qth_edit[UIQ_N] = '\0';

    s_qth.sel = 0U;
    s_qth.pressed = UIQ_HIT_NONE;
    qth_fill();

    s_menu_qth_active = 1U;
    s_menu_open = 1U;
    s_menu_cfg_active = 0U;
    s_menu_detail_active = 0U;
    s_menu_hora_active = 0U;
    s_grid_pant = GRID_NADA;
    s_kbd_modo = KBD_NADA;

    ui_qth_draw(&s_qth);
    act_draw();
}

/* Mueve la casilla elegida `pasos` posiciones dentro de SU alfabeto. Lo
 * usan el mando y los dos botones, asi que ciclar es una sola cosa. */
static void qth_mando(int32_t pasos)
{
    uint8_t n, i;
    const char *alf;
    int32_t k;

    if (!s_menu_qth_active) { return; }
    alf = qth_alfabeto(s_qth.sel, &n);

    for (i = 0U; i < n; i++) { if (alf[i] == s_qth_edit[s_qth.sel]) { break; } }
    if (i >= n) { i = 0U; }

    k = ((int32_t)i + pasos) % (int32_t)n;
    if (k < 0) { k += (int32_t)n; }
    s_qth_edit[s_qth.sel] = alf[k];

    qth_fill();
    ui_qth_draw(&s_qth);
}

static void qth_touch(uint16_t x, uint16_t y, uint8_t pressed)
{
    if (pressed) {
        if (s_qth.pressed < 0) {
            int8_t h = ui_qth_hit(x, y);
            /* "Aplicar" apagado no se hunde: un boton que da retorno visual
             * y luego no hace nada se vive como que la pantalla falla. Es la
             * misma regla que en hora_touch(). */
            if (h == UIQ_HIT_APLICAR && !s_qth.cambiado) { h = UIQ_HIT_NONE; }
            if (h != UIQ_HIT_NONE) { s_qth.pressed = h; ui_qth_draw(&s_qth); }
        }
        return;
    }
    if (s_qth.pressed < 0) { return; }
    {
        int8_t k = s_qth.pressed;

        s_qth.pressed = UIQ_HIT_NONE;

        if (k >= UIQ_HIT_CAJA) {
            s_qth.sel = (uint8_t)(k - UIQ_HIT_CAJA);
            qth_fill();
            ui_qth_draw(&s_qth);
        } else if (k == UIQ_HIT_MENOS) {
            qth_mando(-1);
        } else if (k == UIQ_HIT_MAS) {
            qth_mando(1);
        } else if (k == UIQ_HIT_APLICAR) {
            ft8_decoder_set_own_grid(s_qth_edit, (int)UIQ_N);
            if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
            debug_print("qth: ");
            debug_print(ft8_decoder_get_own_grid());
            debug_print("\n");
            menu_screen_close();
        } else {
            /* Salir SIN aplicar: lo editado se tira. Ver el comentario de
             * arriba sobre por que se edita una copia. */
            menu_screen_close();
        }
    }
}

static void cfg_show(cfg_pant_t p)
{
    /* Abrir una pantalla apaga el selector de Func; quien lo quiere lo
     * enciende despues. Ver el boton Func en demo_button_callback(). */
    s_cfg_func = 0U;
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
    s_menu_mode_active  = (uint8_t)(p == CFG_MODOS);
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
    if (s_cfg_pant == CFG_MODOS) {
        menu_mode_preset_callback(0, UI_EVENT_RELEASE, (void *)(uintptr_t)k);
        return;
    }
    if (s_cfg_func) {
        uint8_t id = k_ajustes[k].id;

        if (!ajuste_enganchar(id)) {
            /* Calibrar, dormir e informacion son acciones: no hay valor que
             * mover. Se ignora el toque en vez de cerrarse sin haber hecho
             * nada, que se viviria como que la pantalla no responde. */
            debug_print("func: ese ajuste no se puede enganchar al mando\n");
            return;
        }
        /* El enganche se pone DESPUES de cerrar: menu_screen_close() devuelve
         * el mando a la sintonia a proposito (ver su comentario), asi que
         * hacerlo antes lo borraria. */
        s_cfg_func = 0U;
        menu_screen_close();
        (void)ajuste_enganchar(id);
        act_sync();
        ui_act_draw(&s_act);
        top_sync();
        ui_top_draw_status(&s_top);
        debug_print("func: mando enganchado a ");
        debug_print(ajuste_nombre(id));
        debug_print("\n");
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
 * Los textos de los pasos ya no son un array paralelo que haya que
 * mantener a mano: salen de TUNE_STEPS_LISTA, la misma lista de la que
 * salen los hercios, los indices y las etiquetas cortas. Estaban a cinco
 * mil lineas de la tabla de valores, que es justo la distancia a la que
 * uno se olvida de tocar el segundo sitio.
 *
 * Los _Static_assert de debajo se quedan de todas formas: ahora no pueden
 * saltar (los tres arrays se generan de la misma lista), y esa es
 * precisamente la idea - que la comprobacion sobre y no que no exista.
 */
#define TUNE_STEP_X_DESC(suf, hz, et, desc, desc_en) T(desc, desc_en),
static const texto_t k_paso_desc[] = { TUNE_STEPS_LISTA(TUNE_STEP_X_DESC) };
#undef TUNE_STEP_X_DESC
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
 *     con los que el arranque copia .data y pone a cero .bss y .tcmram.
 *     La cuenta de flash esta comprobada contra el tamano real del .bin:
 *     coincide byte a byte.
 *   - La temperatura es el sensor del propio micro. Ver
 *     battery_get_chip_temp_c() para por que pone "del chip" y por que
 *     el numero absoluto no es de fiar.
 *
 * El tope de flash que se ensena son 256 kB y no los 384 que da el mapa
 * de memoria, porque el limite de verdad no es el enlazador: es el
 * camino de actualizacion, que rellena la imagen a 0x40000 y le pega la
 * firma detras. Ensenar 384 seria ensenar un margen que no existe.
 */
/* ---------------------------------------------------------------------
 * LO QUE CUESTA DE VERDAD LA CASCADA. 24/09/2026.
 * ---------------------------------------------------------------------
 * El coste del volcado ya se medía con el contador de ciclos del nucleo
 * (t_wf0/t_wf1 en sdr_tick), pero solo salia por el puerto serie de
 * depuracion, que va compilado a cero (DEBUG_UART_ENABLED=0). O sea que
 * el dato existia y no lo veia nadie.
 *
 * Sale ahora en la ventana de informacion, y con el va la otra mitad, que
 * es la que de verdad importa: las lineas por segundo que se estan
 * CONSIGUIENDO. La tabla k_wf promete 60, 30, 15, 7 o 3 segun la velocidad
 * elegida, pero eso es lo que se PIDE; si el fotograma no da tiempo, lo que
 * sale es otra cosa, y hasta hoy no habia forma de saberlo desde la radio.
 *
 * Sirve para no discutir a ojo: primero se lee el numero, luego se toca
 * (tiempos del bus EXMC, IPA, DMA, desplazamiento por hardware), y luego se
 * vuelve a leer.
 */
static volatile uint32_t s_wf_ciclos;   /* ciclos del ultimo push+volcado */
static volatile uint16_t s_wf_lps;      /* lineas por segundo conseguidas */

/*
 * EL ULTIMO VALOR QUE SIGNIFICABA ALGO - 01/10/2026.
 *
 * *** El dueno, comparando dos cargadores: "la señal va un poquito mas
 * lenta y ademas se ralentiza mas aun cuando recibe señal". ***
 *
 * Esa observacion hay que poder MEDIRLA, y hasta hoy no se podia. La fila
 * "Cascada" enseña s_wf_lps, pero dentro del menu la cascada no se dibuja,
 * asi que al segundo el contador se pone a cero y la fila dice "0/60".
 * Inservible justo cuando se quiere leer.
 *
 * Han hecho falta tres intentos de medirlo desde fuera con una camara
 * -correlacion vertical, desplazamiento acumulado y deteccion de linea
 * nueva- y los tres han fallado: el patron de la cascada son rayas
 * verticales, que son invariantes al desplazamiento, y el temblor de la
 * mano tiene el mismo tamaño que la señal que se busca. No se puede medir
 * por fuera. Tiene que contarlo la radio.
 *
 * Asi que se guarda el ultimo valor DISTINTO DE CERO, mas el peor
 * fotograma visto. Con eso se mira el menu, se apunta, se graba el otro
 * cargador y se comparan dos numeros en vez de dos impresiones.
 */
static volatile uint16_t s_wf_lps_vivo;   /* ultimo s_wf_lps != 0 */
static volatile uint16_t s_fps_vivo;      /* ultimo s_fps != 0 */
static volatile uint16_t s_frame_peor_ms; /* el fotograma mas largo visto */
static uint16_t s_wf_lineas;            /* van contadas dentro de este segundo */
static uint16_t s_wf_lineas_antes;      /* para saber si ESTE fotograma volco */
/* Fotogramas por segundo, en la misma ventana de un segundo que las lineas.
 * Hace falta para distinguir dos cosas que dan el mismo numero de lineas:
 * "Muy rapida" funcionando a 14 fotogramas (dos lineas cada uno) de "Muy
 * rapida" sin funcionar a 28 fotogramas (una linea cada uno). Sin esto solo
 * se puede adivinar. */
/* Cuantas FFT entraron en el ultimo fotograma y en cual se hizo la foto de
 * media tanda. Es el par de numeros que explica si "Muy rapida" dobla o no:
 * hacen falta MAS FFT despues de la foto que antes. Sale en Fotograma. */
static volatile uint8_t  s_tanda_n;
static volatile uint8_t  s_tanda_mitad;
#define SPECTRUM_FRAME_MS_DEF 22U
static uint32_t s_frame_ms = SPECTRUM_FRAME_MS_DEF;
static volatile uint16_t s_fps;
static uint16_t s_fotogramas;
static uint32_t s_wf_ms0;               /* g_msticks cuando empezo la cuenta */

/* Las otras dos patas del fotograma. Se median ya (t_fft*, t_spec*) y
 * tambien se iban al puerto serie apagado. Sin ellas, saber que la cascada
 * cuesta 15 ms no dice nada: lo que hace falta es saber que parte del
 * fotograma es eso. Con 11 fotogramas por segundo el fotograma dura unos
 * 90 ms, o sea que la cascada es el 16% y el 84% esta en otro sitio. */
static volatile uint16_t s_fft_us;
static volatile uint16_t s_spec_us;
static volatile uint16_t s_chrome_us;
static volatile uint16_t s_push_us;   /* de los de arriba, lo que se fue en empujar pixeles */


extern uint32_t _sdata, _edata, _sbss, _ebss, _stcmram, _etcmram, _eflash;

/* ---------------------------------------------------------------------
 * MARCA DE AGUA DE LA PILA. 24/09/2026.
 * ---------------------------------------------------------------------
 * tools/pila.py calcula el peor caso leyendo el grafo de llamadas del
 * binario, y eso es lo que impide que vuelva a colarse un desbordamiento.
 * Pero tiene un punto ciego que esta escrito en su propia cabecera: las
 * llamadas por PUNTERO A FUNCION. objdump ve "blx r3" y no sabe a donde
 * va, asi que esos caminos no entran en la cuenta - y este firmware tiene
 * unos cuantos (imgs_init, gfx2_render, imgs_verif_paso).
 *
 * Esto mide lo que de verdad pasa. Al arrancar se pinta todo el hueco
 * libre de pila con un patron; despues, lo que sigue sin pintar por abajo
 * es hasta donde llego la pila alguna vez. No es un calculo: es la huella.
 *
 * Sale en la ventana de informacion, en "Pila". Si algun dia marca el
 * total, la pila llego al fondo y lo que haya por debajo esta pisado.
 *
 * Coste: unos 30 bytes de codigo y un barrido de 4 kB una sola vez al
 * arrancar. La lectura recorre el hueco entero, pero solo cuando se abre
 * la ventana de informacion.
 */
extern uint32_t end;      /* fondo del hueco de pila, lo coloca el enlazador */
extern uint32_t _estack;  /* techo: el SP con el que arranca la aplicacion */

#define PILA_PATRON  0x5AA55AA5UL

static void pila_pinta(void)
{
    uint32_t  aqui;   /* vive EN la pila: su direccion es el suelo de lo nuestro */
    uint32_t *p    = (uint32_t *)&end;
    uint32_t *tope = (uint32_t *)(((uint32_t)&aqui) - 64U);

    while (p < tope) {
        *p++ = PILA_PATRON;
    }
}

/* Bytes de pila que se han llegado a usar desde el arranque. */
static uint32_t pila_usada(void)
{
    const uint32_t *p   = (const uint32_t *)&end;
    const uint32_t *fin = (const uint32_t *)&_estack;

    while ((p < fin) && (*p == PILA_PATRON)) {
        p++;
    }
    return (uint32_t)((uint32_t)fin - (uint32_t)p);
}

/*
 * LA FLASH, QUE DESDE LA RAMA "reload" ES UN SOLO NUMERO.
 *
 * Aqui habia DOS filas, "Flash" y "Flash alta", y no era un capricho de
 * presentacion: con el gestor de fabrica la flash estaba partida en una
 * region baja de 256 kB y un desvan de 64 kB por encima de su firma, y las
 * dos NO eran intercambiables -el enlazador no derrama de una a otra, lo
 * que subia al desvan se elegia a mano seccion por seccion-. Que el desvan
 * tuviera sitio no significaba que cupiera una linea de codigo mas, y por
 * eso habia que enseñar los dos numeros por separado.
 *
 * Con cargador propio la region es de una pieza, 0x0800C000..0x08080000, y
 * un solo numero vuelve a decir la verdad entera.
 */
#define INFO_FLASH_ORIGEN 0x0800C000UL
#define INFO_FLASH_TOPE   (0x08080000UL - INFO_FLASH_ORIGEN)   /* 475.136 */
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

/* ----------------------------------------------------------------------
 * LA FLASH EXTERNA, MIRADA - etapa 33, 24/09/2026.
 *
 * Cuatro celdas de Informacion que contestan de donde puede salir sitio
 * para guardar las imagenes de WEFAX y de SSTV, y sobre todo de donde NO.
 *
 * El chip de la flash externa lo comparten dos cosas: el sistema de
 * ficheros FAT12 donde vive CONFIG.CSV, y -posiblemente- la imagen de
 * update4.bin, que es el UNICO camino que existe para meter firmware en
 * esta radio. Escribir en el sitio equivocado deja el aparato sin forma
 * de recibir la siguiente version, y desde dentro no hay marcha atras.
 * Asi que esto mira antes, y solo lee.
 *
 * Las cifras se MIDEN, no se creen: ver el comentario de
 * spi_flash_mira_alta() en spi_flash.h. La celda de la flash pone las
 * dos -lo medido y lo que dice el identificador- justamente para que un
 * chip que miente se vea a simple vista.
 * -------------------------------------------------------------------- */
static spi_flash_alta_t s_alta;
static uint8_t          s_alta_hecho;

/*
 * El almacen de imagenes cuelga de las MISMAS medidas: el sistema de
 * ficheros le pone el suelo y el tamaño medido del chip el techo. Con el
 * chip de fabrica de 2 MB esto devuelve 0 y no se monta nada, que es lo
 * correcto - ver el comentario de IMGS_SUELO_MIN en img_store.h.
 */
static const imgs_medio_t k_imgs_medio = {
    spi_flash_read, spi_flash_sector_erase_4k, spi_flash_page_program
};
static uint8_t s_imgs_hecho;

/*
 * MONTAR EL ALMACEN NO PUEDE PASAR AL ABRIR UNA PANTALLA - 24/09/2026.
 *
 * Estaba colgado de alta_mira(), o sea que ocurria solo al entrar en
 * Informacion. Y montar no es barato: busca el suelo recorriendo bloques
 * por un bus bit a bit y, la primera vez, ESCRIBE la cabecera de zona.
 * La radio se quedaba clavada al entrar en la pantalla, que es justo el
 * sitio donde uno va a mirar por que algo no va.
 *
 * Ahora se monta solo cuando alguien lo pide a proposito - la celda de
 * "Probar imagenes"-. Dos razones, y la segunda es la buena:
 *
 *   - abrir una pantalla vuelve a costar lo que costaba;
 *   - y si montar fallara de verdad, solo falla cuando se toca esa celda.
 *     Apagar y encender devuelve la radio. Si esto se hiciera al arrancar
 *     -que era la otra opcion- un fallo ahi dejaria la radio sin arrancar,
 *     que es cambiar una molestia por un ladrillo.
 */
static void imgs_monta(void)
{
    const spi_flash_alta_t *a;

    if (s_imgs_hecho) { return; }
    a = alta_mira();
    s_imgs_hecho = 1U;
    /*
     * EL SUELO ES ZA_TOPE, NO EL FINAL DEL SISTEMA DE FICHEROS. 02/10/2026.
     *
     * Antes se le pasaba a->fs_bytes (1 MB), o sea el megabyte de arriba
     * ENTERO, y el almacen de imagenes repartia desde la cola hacia abajo
     * cogiendo bloques borrados. Mientras la zona alta acababa en 0x180000
     * eso no chocaba con nada... pero tampoco lo impedia NADA: lo unico que
     * los separaba era que la zona alta estuviera llena.
     *
     * Con ZA_TOPE en el final del chip, el hueco que deje un BD.BIN mas
     * pequeño que su region seguiria pareciendole libre al almacen, lo
     * usaria, y el siguiente BD.BIN mas gordo se lo llevaria por delante.
     * Un fallo que solo aparece cuando crece el fichero de datos, o sea
     * meses despues y sin relacion aparente.
     *
     * Pasandole ZA_TOPE como suelo, las dos cosas no pueden solaparse
     * aunque la zona alta este medio vacia. Hoy eso significa que el
     * almacen se queda sin sitio y lo dice, que es la respuesta correcta y
     * explicita en vez de una convivencia que funciona por casualidad.
     */
    (void)imgs_init(&k_imgs_medio, ZA_TOPE, a->bytes_mide);
}

/*
 * VOLCAR LA ZONA ALTA - etapa 36, 24/09/2026.
 *
 * Lo que hay por encima del sistema de ficheros no se sabe que es, y la
 * unica forma de averiguarlo es sacarlo de la radio. Esta celda lo copia
 * dentro de un fichero VOLCADO.BIN que TIENE QUE EXISTIR YA, creado
 * desde el ordenador con el tamaño que se quiera: asi no hay que escribir
 * ni una estructura del sistema de ficheros. Ver el comentario de
 * spi_flash_volcado_abre().
 *
 * Va por pasos desde el bucle principal, un bloque de 4 kB cada vez, para
 * que medio minuto de escritura no deje la radio congelada.
 */
static const char k_vol_nombre[8] = { 'V','O','L','C','A','D','O',' ' };
static const char k_vol_ext[3]    = { 'B','I','N' };
static uint8_t  s_vol_estado;  /* 0 parado, 1 en curso, 2 hecho, 3 no se pudo */
static uint32_t s_vol_bytes;   /* cuantos se han volcado, para decirlo */
static uint8_t  s_vol_sin_zona;
/*
 * Y CUANDO ACABA, SE BORRA DEL DISCO.
 *
 * *** Idea del dueno: "lo deberia de borrar el propio firmware una vez lo
 * ha cargado, igual que hace con los update". ***
 *
 * Medio mega en un disco de pocos no es poca cosa, y una vez el fichero
 * esta en la zona alta ya no pinta nada ahi. Esta bandera dice que el
 * volcado que hay en marcha es el de los aviones y no el de volcar la
 * zona o el de restaurarla: esos NO se borran, que son las copias de
 * seguridad y borrarlas seria justo lo contrario de lo que son.
 */
static uint8_t  s_vol_aviones;
/* El estado de la prueba se declara aqui porque restaura_arranca(), que
 * esta antes, tiene que poder pararla: restaurar reescribe justo el sitio
 * donde vive lo que la prueba escribio. */
static uint8_t  s_pru_estado;

/*
 * LA FLASH INTERNA DEL MICRO - etapa 36b, 24/09/2026.
 *
 * Es lo unico de lo que no hay copia, y es lo que mas falta hace tenerla:
 * ahi vive el gestor de arranque, que es el unico camino para meter
 * firmware en esta radio. Si un dia deja de arrancar y no hay copia, no
 * hay vuelta atras sin una sonda.
 *
 *   0x08000000  128 kB  gestor de arranque
 *   0x08020000  256 kB  la aplicacion (esto)
 *   0x08060000  128 kB  resto, sin usar que se sepa
 *
 * Medio megabyte justo, que es lo mismo que mide el VOLCADO.BIN de la
 * zona alta: el mismo fichero sirve para las dos cosas.
 *
 * Leerla no tiene ningun misterio - esta mapeada en memoria- y no
 * interfiere con ejecutar desde ella: leer no es borrar ni programar.
 */
#define MICRO_FLASH_BASE ((const uint8_t *)0x08000000UL)
#define MICRO_FLASH_TAM  (512UL * 1024UL)

/*
 * LA ROM DE FABRICA DEL CHIP - 01/10/2026.
 *
 * *** El dueno, cuando le pedi que sacara esto por SWD: "que no quiero
 * abrir la radio joder" ... "pa que te crees que estamos haciendo esto".
 * *** Tiene toda la razon: el objetivo entero del DFU era no abrirla, y yo
 * le propuse dos pruebas que las dos pasaban por destripar el aparato.
 *
 * No hace falta. Esos 30 kB estan MAPEADOS en memoria, igual que la flash
 * del micro, asi que el volcado se hace desde dentro con el mismo
 * spi_flash_volcado_abre_mem() de siempre y sale por el disco USB.
 *
 * Y es lo unico que contesta la pregunta de verdad: si ahi dentro hay
 * descriptores de USB, la ROM habla USB y el que falla es mi salto. Si solo
 * hay codigo de USART, no hay nada que arreglar y la idea se cae.
 */
#define ROM_FABRICA_BASE ((const uint8_t *)0x1FFF0000UL)
#define ROM_FABRICA_TAM  (0x7800UL)          /* 30 kB */

/*
 * Y VA AL VOLCADO.BIN DE SIEMPRE, no a un fichero propio.
 *
 * *** El dueno, 01/10/2026: "no encuentro el .BIN" ... "eso dice el
 * boton". *** El boton decia "no encuentro el .BIN" y decia la verdad:
 * spi_flash_volcado_abre() NO crea el fichero de destino, lo BUSCA. Tiene
 * que estar ya en el volumen, entero y sin trocear, porque el volcado
 * escribe dentro de sus clusters sin tocar la FAT.
 *
 * La primera version pedia un ROMFABRI.BIN que no existia en ningun sitio y
 * que yo no le dije que hiciera falta. Usando VOLCADO.BIN el procedimiento
 * es el MISMO que el dueno ya conoce del volcado de la flash del micro, con
 * el fichero que ya tiene: copiarlo al disco, tocar, reiniciar, copiarlo de
 * vuelta. Y de paso no hay un segundo fichero que mantener.
 */

/*
 * EL CAMINO DE VUELTA - etapa 38, 24/09/2026.
 *
 * Lee ZONAALTA.BIN del disco USB y lo escribe donde acaba el sistema de
 * ficheros, que es de donde salio. Deshace cualquier cosa que hayamos
 * puesto ahi arriba - imagenes, pruebas- y devuelve la fuente china a su
 * sitio.
 *
 * Sin esto, "podemos usar esa zona porque es reversible" era falso:
 * reversible con un programador y un destornillador, que no es lo mismo.
 * Ahora es reversible con un fichero y un toque.
 *
 * La guardia de que el destino no puede caer por debajo del sistema de
 * ficheros vive en spi_flash_restaura_abre(), no aqui: es lo que impide
 * que esto sea un arma, y tiene que estar donde no se pueda rodear.
 */
static const char k_zona_nombre[8] = { 'Z','O','N','A','A','L','T','A' };

static void restaura_arranca(void)
{
    const spi_flash_alta_t *a;

    if (s_vol_estado == 1U || s_pru_estado == 1U || s_pru_estado == 5U) { return; }
    s_vol_sin_zona = 0U;
    a = alta_mira();
    if (a->fs_bytes == 0UL || a->bytes_mide <= a->fs_bytes) {
        s_vol_sin_zona = 1U;
        s_vol_estado = 3U;
        return;
    }
    if (spi_flash_restaura_abre(k_zona_nombre, k_vol_ext, a->fs_bytes,
                                a->bytes_mide - a->fs_bytes,
                                a->fs_bytes, a->bytes_mide)) {
        s_vol_aviones = 0U;   /* este volcado no es el de los aviones: no se borra nada */
        s_vol_estado = 1U;
        s_vol_bytes = spi_flash_volcado_total();
        /* Lo que hubiera montado deja de valer: se acaba de reescribir
         * el sitio entero por debajo de sus pies. */
        s_imgs_hecho = 0U;
        s_pru_estado = 0U;
        return;
    }
    s_vol_estado = 3U;
    return;
}

/*
 * CARGAR LA BASE DE MODELOS DE AVION EN LA ZONA ALTA. 25/09/2026.
 *
 * *** Idea del dueno: "tenemos 1 mega no accesible via usb, quizas
 * podriamos usarlo para guardar ahi cosas". ***
 *
 * Traslada ICAO24.BIN del disco USB a 0x101000, que es donde vivia la
 * fuente china del firmware de fabrica y donde icao24_db.c va a buscarla
 * (ver I24_BASE). De una sola vez: despues se borra del disco y el megabyte
 * vuelve a ser del usuario.
 *
 * SE USA LA MAQUINARIA QUE YA HABIA. spi_flash_restaura_abre() es la misma
 * que restaura la zona, con sus guardias: nunca por debajo del sistema de
 * ficheros, nunca por encima del chip, y a pasos para no bloquear la
 * interfaz. Lo unico que cambia es el destino y el nombre del fichero.
 *
 * EL TOPE NO ES EL FINAL DEL CHIP, es 0x180000. Ese medio mega de arriba
 * sigue siendo el candidato a area de preparacion del gestor de arranque y
 * no se toca ni por accidente: pasarselo como `tope` es lo que impide que
 * un fichero demasiado grande se meta ahi.
 */
/*
 * EL REPARTO DE LA ZONA ALTA YA NO ESTA AQUI - 28/09/2026.
 *
 * Aqui habia dos direcciones fijas: los aviones de 0x101000 a 0x140000 y
 * las emisoras de 0x140000 a 0x180000. Ese 0x140000 era un numero a ojo, y
 * era el firmware decidiendo cuanto puede ocupar cada base: para cambiarlo
 * habia que recompilar, y ya se quedo corto la primera vez.
 *
 * Ahora lo dice el directorio de BD.BIN, que escribe el PC mirando lo
 * que miden las dos de verdad. Ver zona_alta.h y datos_arranca().
 */

/*
 * EL CARTEL DEL ARRANQUE. 25/09/2026.
 *
 * *** Idea del dueno: "en vez de tener que ir y darle yo a info y luego a
 * grabar el bin, quiero que cada vez que arranques la radio revise si hay
 * un icao24.bin y si lo hay que saque un popup y empiece a grabarlo... y
 * luego que lo borre del usb claro". ***
 *
 * CUANTO CUESTA MIRARLO EN CADA ARRANQUE. No se contesta a ojo: el banco
 * de FAT tiene el chip simulado a nivel de pin y cuenta los bits que pasan
 * por el bus. Medido alli:
 *
 *     mirar si esta, SIN estar:   8,3 ms   (una lectura de 512 bytes)
 *     mirar si esta, ESTANDO:    57,5 ms
 *
 * O sea que el caso normal -el fichero no esta, que es el de todos los
 * arranques menos uno- son OCHO MILISEGUNDOS. La pantalla de arranque dura
 * mucho mas que eso. Y el otro caso solo pasa una vez, justo antes de una
 * carga que tarda medio minuto, asi que sus 57 ms no los ve nadie.
 *
 * El banco lo deja como requisito -"mirarlo en el arranque cuesta menos de
 * 50 ms"- para que si algun dia alguien mete ahi algo mas caro, salte.
 */
/*
 * CARGAR LA BASE DE MODELOS DE AVION EN LA ZONA ALTA. 25/09/2026.
 *
 * *** Idea del dueno: "tenemos 1 mega no accesible via usb, quizas
 * podriamos usarlo para guardar ahi cosas". ***
 *
 * Traslada ICAO24.BIN del disco USB a 0x101000, que es donde vivia la
 * fuente china del firmware de fabrica y donde icao24_db.c va a buscarla
 * (ver I24_BASE). De una sola vez: despues se borra del disco y el megabyte
 * vuelve a ser del usuario.
 *
 * SE USA LA MAQUINARIA QUE YA HABIA. spi_flash_restaura_abre() es la misma
 * que restaura la zona, con sus guardias: nunca por debajo del sistema de
 * ficheros, nunca por encima del chip, y a pasos para no bloquear la
 * interfaz. Lo unico que cambia es el destino y el nombre del fichero.
 *
 * EL TOPE NO ES EL FINAL DEL CHIP, es 0x180000. Ese medio mega de arriba
 * sigue siendo el candidato a area de preparacion del gestor de arranque y
 * no se toca ni por accidente: pasarselo como `tope` es lo que impide que
 * un fichero demasiado grande se meta ahi.
 */
/*
 * EL REPARTO DE LA ZONA ALTA YA NO ESTA AQUI - 28/09/2026.
 *
 * Aqui habia dos direcciones fijas: los aviones de 0x101000 a 0x140000 y
 * las emisoras de 0x140000 a 0x180000. Ese 0x140000 era un numero a ojo, y
 * era el firmware decidiendo cuanto puede ocupar cada base: para cambiarlo
 * habia que recompilar, y ya se quedo corto la primera vez.
 *
 * Ahora lo dice el directorio de BD.BIN, que escribe el PC mirando lo
 * que miden las dos de verdad. Ver zona_alta.h y datos_arranca().
 */

/*
 * EL CARTEL DEL ARRANQUE. 25/09/2026.
 *
 * *** Idea del dueno: "en vez de tener que ir y darle yo a info y luego a
 * grabar el bin, quiero que cada vez que arranques la radio revise si hay
 * un icao24.bin y si lo hay que saque un popup y empiece a grabarlo... y
 * luego que lo borre del usb claro". ***
 *
 * CUANTO CUESTA MIRARLO EN CADA ARRANQUE. No se contesta a ojo: el banco
 * de FAT tiene el chip simulado a nivel de pin y cuenta los bits que pasan
 * por el bus. Medido alli:
 *
 *     mirar si esta, SIN estar:   8,3 ms   (una lectura de 512 bytes)
 *     mirar si esta, ESTANDO:    57,5 ms
 *
 * O sea que el caso normal -el fichero no esta, que es el de todos los
 * arranques menos uno- son OCHO MILISEGUNDOS. La pantalla de arranque dura
 * mucho mas que eso. Y el otro caso solo pasa una vez, justo antes de una
 * carga que tarda medio minuto, asi que sus 57 ms no los ve nadie.
 *
 * El banco lo deja como requisito -"mirarlo en el arranque cuesta menos de
 * 50 ms"- para que si algun dia alguien mete ahi algo mas caro, salte.
 */
/*
 * AL ARRANCAR, SI ESTA, SE CARGA SOLO.
 *
 * Devuelve 1 si ha salido su pantalla. La llama el arranque ANTES de
 * pintar la radio: si ICAO24.BIN no esta en el disco -todos los arranques
 * menos uno- son 8 ms y no pasa nada mas.
 */

/*
 * ES LA PANTALLA, NO UN CARTEL ENCIMA.
 *
 * *** El dueno, del primer intento: "es muy cutre", y luego "solo deberia
 * de haber cargado la pantalla de copiando". ***
 *
 * El primer intento pintaba la radio entera y le plantaba encima un
 * recuadro. Esto pinta SU pantalla, copia entera, y vuelve; la radio se
 * dibuja despues, una vez y ya con todo hecho. Por eso puede ser
 * bloqueante: aqui no hay nada mas que atender -ni menu, ni toques, ni
 * cascada-, el bucle principal todavia no ha empezado.
 *
 * Y por eso tampoco pasa por volcado_poll(): esa maquina existe para
 * copiar SIN congelar la interfaz, y aqui no hay interfaz que congelar.
 */
/* Espera de bloqueo, solo para el arranque: aqui todavia no hay bucle
 * principal al que devolverle el control. g_msticks lo mueve el SysTick,
 * que ya esta en marcha mucho antes de llegar aqui. */
static void espera_ms(uint32_t ms)
{
    uint32_t t0 = g_msticks;
    while ((uint32_t)(g_msticks - t0) < ms) { /* nada */ }
}

/*
 * CARGAR LAS BASES DE DATOS: UN SOLO FICHERO - 28/09/2026.
 *
 * *** Por el dueño del proyecto: "creo que para futuras versiones estaria
 * bien fusionarlos, para solo tener que grabar un .bin cada vez". ***
 *
 * Antes eran dos: ICAO24.BIN a 0x101000 y EMISORAS.BIN a 0x140000. Y ese
 * reparto era un numero escrito a mano en el firmware, o sea una decision
 * sobre cuanto puede ocupar cada base que solo se cambiaba recompilando.
 * Peor: habia DOS cargadores escribiendo en la misma zona, y el segundo
 * tenia que mirar donde acababa el primero.
 *
 * ESO FALLO EN LA PRIMERA PRUEBA DE VERDAD. El dueño mando una foto: un
 * cartel que decia "Base de datos de aviones - ICAO24.BIN, del disco a la
 * zona alta" mientras lo que habia fallado era el cargador de EMISORAS,
 * porque reutilizaba su pantalla. Y debajo, un motivo -"No cabe: la base
 * de aviones llega hasta aqui"- que se soltaba en TODOS los casos de
 * fallo, no solo en ese. O sea: el cartel decia el modo equivocado y una
 * causa que a lo mejor no era la suya. Dos formas de mentir en un sitio
 * donde lo unico que hay es el mensaje.
 *
 * AHORA HAY UN CARGADOR Y UN FICHERO. BD.BIN lleva un directorio
 * delante que dice donde vive cada base, lo escribe tools/datos_pack.py
 * mirando lo que miden de verdad, y se copia entero de una vez. No puede
 * haber solape porque no hay dos cosas que solapar, y no hay reparto que
 * cuadrar porque el firmware no opina: lee donde le dicen.
 *
 * Y cada fallo tiene SU motivo. Ver s_datos_porque.
 */
#define DATOS_BASE ZA_BASE
#define DATOS_TOPE ZA_TOPE

static const char *s_datos_porque;

static uint8_t datos_arranca(void)
{
    const spi_flash_alta_t *a;
    uint32_t tam = 0UL, bloques;

    s_datos_porque = 0;
    if (s_vol_estado == 1U || s_pru_estado == 1U || s_pru_estado == 5U) {
        s_datos_porque = tr("la flash esta ocupada con otra copia",
                            "the flash is busy with another copy");
        return 0U;
    }
    if (!spi_flash_fichero_tam("BD      ", "BIN", &tam) || tam == 0UL) {
        s_datos_porque = tr("BD.BIN esta en el disco pero no se puede medir",
                            "BD.BIN is on the disk but cannot be measured");
        s_vol_estado = 3U;
        return 0U;
    }
    bloques = (tam + 4095UL) & ~4095UL;
    if (bloques > (DATOS_TOPE - DATOS_BASE)) {
        s_datos_porque = tr("BD.BIN es mayor que la zona alta",
                            "BD.BIN is bigger than the upper area");
        s_vol_estado = 3U;
        return 0U;
    }

    a = alta_mira();
    if (a->fs_bytes == 0UL) {
        s_datos_porque = tr("no se ha podido medir el sistema de ficheros",
                            "the file system could not be measured");
        s_vol_sin_zona = 1U;
        s_vol_estado = 3U;
        return 0U;
    }
    /*
     * DATOS_TOPE sale de spi_flash_capacidad(), que mide por alias y, si
     * eso no vale -chip recien borrado-, tira del codigo JEDEC. Aqui se
     * compara contra esa misma respuesta y no contra a->bytes_mide, que es
     * SOLO la medida por alias: con un chip nuevo vale 0, y esta condicion
     * rechazaria la carga diciendo que la zona alta no llega, con el chip
     * entero delante. Ver spi_flash_capacidad().
     *
     * Lo que si tiene que cumplirse es que la zona de datos empiece por
     * encima del sistema de ficheros: si el volumen fuera mas grande de lo
     * que el disco anuncia, escribir las bases se lo llevaria por delante.
     */
    if (DATOS_TOPE > spi_flash_capacidad() || DATOS_BASE < a->fs_bytes) {
        s_datos_porque = tr("la zona alta de este chip no llega hasta aqui",
                            "this chip's upper area does not reach that far");
        s_vol_sin_zona = 1U;
        s_vol_estado = 3U;
        return 0U;
    }
    if (spi_flash_restaura_abre("BD      ", "BIN", DATOS_BASE, bloques,
                                a->fs_bytes, DATOS_TOPE)) {
        s_vol_aviones = 0U;   /* el borrado del disco lo hace datos_al_arrancar */
        s_vol_estado = 1U;
        s_vol_bytes = spi_flash_volcado_total();
        s_imgs_hecho = 0U;
        s_pru_estado = 0U;
        return 1U;
    }
    s_datos_porque = spi_flash_volcado_porque_txt();
    s_vol_estado = 3U;
    return 0U;
}

/* El rotulo de la pantalla. Se pasa a proposito: ver aviones_pantalla_t(). */
static const char *k_datos_tit(void) { return tr("Bases de datos", "Databases"); }
static const char *k_datos_que(void)
{ return tr("BD.BIN, del disco a la zona alta de la flash",
            "BD.BIN, from disk to the upper flash area"); }

static void datos_cartel(avip_fase_t f, uint8_t pct, const char *motivo,
                         uint32_t bytes)
{
    aviones_pantalla_t(f, pct, motivo, bytes, k_datos_tit(), k_datos_que());
}

/*
 * EL AVISO DE LA COMPROBACION. Ver AVIP_COMPROBANDO en aviones_pantalla.h:
 * son 8,5 segundos de bus y hasta hoy la barra se quedaba llena y quieta.
 *
 * Solo repinta cuando CAMBIA el porcentaje, como la copia: repintar la
 * pantalla en cada trozo de 256 bytes serian dos mil repintados y la
 * comprobacion tardaria mas que el propio bus.
 */
static uint32_t s_datos_pct;

static void datos_comprobando(uint32_t hechos, uint32_t total)
{
    uint32_t pct;

    if (total == 0UL) { return; }
    pct = (hechos * 100UL) / total;
    if (pct == s_datos_pct) { return; }
    s_datos_pct = pct;
    datos_cartel(AVIP_COMPROBANDO, (uint8_t)pct, (const char *)0, total);
}

static uint8_t datos_al_arrancar(void)
{
    uint32_t total, ultimo_pct = 0xFFFFUL;
    int8_t paso;

    if (!spi_flash_fichero_hay("BD      ", "BIN")) { return 0U; }

    datos_cartel(AVIP_BUSCANDO, 0U, (const char *)0, 0UL);
    if (!datos_arranca()) {
        datos_cartel(AVIP_FALLO, 0U, s_datos_porque, 0UL);
        espera_ms(3000U);
        return 1U;
    }

    total = spi_flash_volcado_total();
    datos_cartel(AVIP_COPIANDO, 0U, (const char *)0, total);
    do {
        paso = spi_flash_volcado_paso();
        if (total != 0UL) {
            uint32_t pct = (spi_flash_volcado_hechos() * 100UL) / total;

            if (pct != ultimo_pct) {
                ultimo_pct = pct;
                datos_cartel(AVIP_COPIANDO, (uint8_t)pct, (const char *)0, total);
            }
        }
    } while (paso > 0);

    s_vol_estado = (paso == 0) ? 2U : 3U;
    if (paso != 0) {
        datos_cartel(AVIP_FALLO, 0U, spi_flash_volcado_porque_txt(), 0UL);
        espera_ms(3000U);
        return 1U;
    }
    /* 0xFF y no 0: con 0, el primer aviso -que tambien es 0 %- no
     * repintaria, y la barra se quedaria en el 100 % de la copia hasta el
     * 1 %. Justo el hueco que se viene a tapar. */
    s_datos_pct = 0xFFFFFFFFUL;
    datos_cartel(AVIP_COMPROBANDO, 0U, (const char *)0, total);

    /*
     * Y AQUI SE COMPRUEBAN LAS SUMAS, antes de decir que esta cargado y
     * antes de borrar el fichero del disco. Son casi medio megabyte por un
     * bus lento -un par de segundos- y este es el unico momento en que
     * hace falta: acaba de pasar por el USB, por el volumen FAT y por cien
     * bloques de borrado y grabacion.
     *
     * El orden importa. Si la copia salio mal, el fichero del disco es la
     * UNICA copia que queda: borrarlo y descubrir despues que lo copiado
     * no vale seria perderlo.
     */
    if (!zona_alta_abre(spi_flash_read)) {
        datos_cartel(AVIP_FALLO, 0U, tr("Copiado, pero el directorio no cuadra",
                                        "Copied, but the directory does not add up"), 0UL);
    } else if (!zona_alta_verifica_p(datos_comprobando)) {
        datos_cartel(AVIP_FALLO, 0U, tr("Copiado, pero las sumas no cuadran",
                                        "Copied, but the checksums do not match"), 0UL);
    } else if (spi_flash_fichero_borra("BD      ", "BIN")) {
        datos_cartel(AVIP_HECHO, 100U, (const char *)0, total);
    } else {
        datos_cartel(AVIP_FALLO, 0U,
                     tr("Cargado, pero no se pudo borrar del disco",
                        "Loaded, but it could not be deleted from the disk"), 0UL);
    }
    espera_ms(2500U);
    return 1U;
}

/*
 * BORRAR LA ZONA ALTA - etapa 38b, 24/09/2026.
 *
 * Nacio para contestar una pregunta: ¿lee alguien la fuente china? Se
 * borro, se reinicio, y la pantalla del gestor de arranque y el modo USB
 * salieron igual. Nadie la echa de menos.
 *
 * Asi que ahora es lo que su nombre dice: deja limpio TODO lo que hay por
 * encima del sistema de ficheros, cabecera del almacen incluida, para que
 * la proxima vez se vuelva a medir del mega entero.
 *
 * DOS GUARDIAS, Y LA SEGUNDA ES LA IMPORTANTE:
 *
 * 1. nunca por debajo del sistema de ficheros. Los limites los pone
 *    spi_flash_borra_abre(), que no se puede rodear desde aqui.
 *
 * 2. NO BORRA SI ZONAALTA.BIN NO ESTA EN EL DISCO. Es el fichero con el
 *    que se devuelve la fuente a su sitio, y sin el esto deja de ser un
 *    experimento reversible para pasar a ser una perdida. No se quita la
 *    red sin haber comprobado que esta puesta.
 */
static void borra_zona_arranca(void)
{
    const spi_flash_alta_t *a;

    if (s_vol_estado == 1U || s_pru_estado == 1U || s_pru_estado == 5U) { return; }
    s_vol_sin_zona = 0U;

    if (!spi_flash_fichero_hay(k_zona_nombre, k_vol_ext)) {
        /* Sin con que deshacerlo, no se hace. */
        s_vol_estado = 3U;
        return;
    }

    a = alta_mira();
    if (a->fs_bytes == 0UL || a->bytes_mide <= a->fs_bytes) {
        s_vol_sin_zona = 1U;
        s_vol_estado = 3U;
        return;
    }
    /*
     * TODO lo que hay por encima del sistema de ficheros, no solo el
     * tramo de la fuente. Desde que se comprobo que nadie la usa, la
     * distincion entre "la fuente" y "el resto de ahi arriba" dejo de
     * significar nada: es una sola zona libre. Y borrarla entera incluye
     * la cabecera del almacen, que es lo que hace que la proxima vez se
     * vuelva a medir y salga del mega entero en vez de quedarse con los
     * 508 kB que midio cuando la fuente estorbaba.
     */
    if (spi_flash_borra_abre(a->fs_bytes, a->bytes_mide - a->fs_bytes,
                             a->fs_bytes, a->bytes_mide)) {
        s_vol_aviones = 0U;   /* este volcado no es el de los aviones: no se borra nada */
        s_vol_estado = 1U;
        s_vol_bytes = spi_flash_volcado_total();
        return;
    }
    s_vol_estado = 3U;
}

static void volcado_arranca_micro(void)
{
    if (s_vol_estado == 1U) { return; }
    s_vol_sin_zona = 0U;
    if (spi_flash_volcado_abre_mem(k_vol_nombre, k_vol_ext,
                                   MICRO_FLASH_BASE, MICRO_FLASH_TAM)) {
        s_vol_aviones = 0U;   /* este volcado no es el de los aviones: no se borra nada */
        s_vol_estado = 1U;
        s_vol_bytes = spi_flash_volcado_total();
        return;
    }
    s_vol_estado = 3U;
}

static void volcado_arranca_rom(void)
{
    if (s_vol_estado == 1U) { return; }
    s_vol_sin_zona = 0U;
    if (spi_flash_volcado_abre_mem(k_vol_nombre, k_vol_ext,
                                   ROM_FABRICA_BASE, ROM_FABRICA_TAM)) {
        s_vol_aviones = 0U;
        s_vol_estado = 1U;
        s_vol_bytes = spi_flash_volcado_total();
        return;
    }
    s_vol_estado = 3U;
}

static void volcado_arranca(void)
{
    const spi_flash_alta_t *a;

    if (s_vol_estado == 1U) { return; }   /* ya esta */
    a = alta_mira();
    if (a->fs_bytes == 0UL || a->bytes_mide <= a->fs_bytes) {
        /* Ni siquiera se llega a mirar el fichero: no hay de donde
         * copiar. Es un fallo distinto y se dice distinto. */
        s_vol_sin_zona = 1U;
        s_vol_estado = 3U;
        return;
    }
    s_vol_sin_zona = 0U;
    /* Se pide TODO lo que hay entre el final del sistema de ficheros y el
     * final del chip; si el fichero es mas pequeño, spi_flash_volcado_abre()
     * recorta a lo que quepa. Asi vale cualquier tamaño de fichero y no hay
     * que acertarlo de antemano. */
    if (spi_flash_volcado_abre(k_vol_nombre, k_vol_ext, a->fs_bytes,
                               a->bytes_mide - a->fs_bytes)) {
        s_vol_aviones = 0U;   /* este volcado no es el de los aviones: no se borra nada */
        s_vol_estado = 1U;
        s_vol_bytes = spi_flash_volcado_total();
        return;
    }
    s_vol_estado = 3U;
}

static void volcado_poll(void)
{
    static uint8_t cuenta;

    if (s_vol_estado != 1U) { return; }

    {
        int8_t paso = spi_flash_volcado_paso();
        if (paso > 0) { goto sigue; }
        /* Solo con 0 -terminado-. Con -1 no habia volcado abierto, y
         * borrar el fichero porque no se estaba copiando seria
         * exactamente el peor momento para borrarlo. */
        if ((paso == 0) && s_vol_aviones) {
            (void)spi_flash_fichero_borra("ICAO24  ", "BIN");
        }
        s_vol_aviones = 0U;
        s_vol_estado = 2U;
        cuenta = 0U;
        if (s_menu_open && s_grid_pant == GRID_INFO) {
            grid_fill();
            ui_grid_draw(&s_grid);
        }
        return;
    }
sigue:

    /*
     * Y REPINTAR MIENTRAS VA, no solo al acabar.
     *
     * La primera version solo repintaba al terminar, asi que la celda se
     * quedaba en el 0 % que tenia al tocarla y no se movia en medio
     * minuto largo. Desde fuera eso no es "va lento": es "esto se ha
     * colgado", que es justo la conclusion equivocada. Un porcentaje que
     * no avanza no informa de nada - es peor que no ponerlo, porque
     * ademas miente.
     *
     * Cada ocho bloques son 32 kB, o sea unas tres veces por segundo como
     * mucho. Repintar la rejilla entera a ese ritmo no se nota al lado de
     * un borrado de flash.
     */
    cuenta++;
    if ((cuenta & 0x07U) == 0U && s_menu_open && s_grid_pant == GRID_INFO) {
        grid_fill();
        ui_grid_draw(&s_grid);
    }
}

/* ----------------------------------------------------------------------
 * LA PRUEBA DEL ALMACEN - etapa 37, 24/09/2026.
 *
 * El sitio donde viven las imagenes se eligio por DEDUCCION: el gestor de
 * arranque no parece tocar esa zona, segun sus propias cadenas de texto.
 * Deducir no es medir, asi que esto lo convierte en algo repetible:
 *
 *   1. un toque escribe una imagen de prueba de 256 kB con un patron
 *      que se puede recalcular;
 *   2. se flashea una version nueva del firmware, que es lo que se
 *      sospecha que podria llevarsela por delante;
 *   3. otro toque la verifica.
 *
 * Si tras la actualizacion sigue cuadrando, la zona es nuestra. Si no
 * cuadra, se sabe con una imagen de prueba en vez de con una tarde de
 * cartas del tiempo.
 *
 * Se escribe A PASOS desde el bucle principal, igual que el volcado: 64
 * bloques con su borrado y su programacion son varios segundos, y
 * dejarlos de una sentada congela la radio.
 * -------------------------------------------------------------------- */
#define PRUEBA_BYTES (256UL * 1024UL)

/* 0 parado, 1 escribiendo, 2 escrita, 3 bien, 4 MAL, 5 verificando.
 * Declarado mas arriba - ver su comentario. */
static uint32_t s_pru_hechos;
static uint8_t  s_pru_vi;       /* que imagen se esta verificando */

/* El patron: barato de calcular y distinto en cada posicion, para que un
 * trozo escrito en el sitio equivocado no pase por bueno. */
static uint8_t prueba_byte(uint32_t k)
{
    return (uint8_t)((k * 31U) ^ (k >> 8) ^ 0x5AU);
}

static void prueba_arranca(void)
{
    if (s_pru_estado == 1U || s_vol_estado == 1U) { return; }

    /* Aqui SI se monta, porque aqui lo ha pedido alguien. */
    imgs_monta();

    if (imgs_cuantas() > 0U) {
        /*
         * Ya hay algo guardado: esto es la SEGUNDA mitad de la prueba.
         *
         * Y va A PASOS, como la escritura. La primera version releia aqui
         * mismo los 256 kB de un tiron, dentro del toque, y la radio se
         * quedaba parada varios segundos: desde fuera eso no se lee como
         * "va lento", se lee como "se ha colgado" - y asi se leyo-. El
         * resultado acababa saliendo bien, pero un par de segundos mudos
         * sin nada que mire ya son un fallo aunque el calculo sea
         * correcto.
         */
        s_pru_vi = 0U;
        imgs_verif_inicia(0U);
        s_pru_estado = 5U;
        return;
    }

    if (imgs_total() == 0UL) { s_pru_estado = 4U; return; }
    if (!imgs_abre(IMGS_TIPO_SSTV, 320U, 256U, 16U, PRUEBA_BYTES,
                   reloj_ahora_ms())) {
        s_pru_estado = 4U;
        return;
    }
    s_pru_hechos = 0UL;
    s_pru_estado = 1U;
}

static void prueba_poll(void)
{
    uint8_t trozo[256];
    uint32_t i, n;

    if (s_pru_estado == 5U) {
        int8_t r = imgs_verif_paso();
        if (r > 0) {
            /* Repintar de vez en cuando, por lo mismo que el volcado: un
             * numero que no se mueve no informa, miente. */
            if ((imgs_verif_hechos() & 0x3FFFU) == 0U &&
                s_menu_open && s_grid_pant == GRID_INFO) {
                grid_fill();
                ui_grid_draw(&s_grid);
            }
            return;
        }
        if (r < 0) { s_pru_estado = 4U; }
        else {
            s_pru_vi++;
            if (s_pru_vi < imgs_cuantas()) {
                imgs_verif_inicia(s_pru_vi);
                return;
            }
            s_pru_estado = 3U;
        }
        if (s_menu_open && s_grid_pant == GRID_INFO) {
            grid_fill();
            ui_grid_draw(&s_grid);
        }
        return;
    }

    if (s_pru_estado != 1U) { return; }

    n = PRUEBA_BYTES - s_pru_hechos;
    if (n > (uint32_t)sizeof trozo) { n = (uint32_t)sizeof trozo; }
    for (i = 0U; i < n; i++) { trozo[i] = prueba_byte(s_pru_hechos + i); }

    if (!imgs_escribe(trozo, n)) {
        imgs_cancela();
        s_pru_estado = 4U;
        return;
    }
    s_pru_hechos += n;

    if (s_pru_hechos >= PRUEBA_BYTES) {
        s_pru_estado = imgs_cierra() ? 2U : 4U;
        if (s_menu_open && s_grid_pant == GRID_INFO) {
            grid_fill();
            ui_grid_draw(&s_grid);
        }
    } else if ((s_pru_hechos & 0x3FFFU) == 0U &&
               s_menu_open && s_grid_pant == GRID_INFO) {
        grid_fill();
        ui_grid_draw(&s_grid);
    }
}

/*
 * EL BOTON DE GRABAR TROZOS SE HA IDO - 28/09/2026.
 *
 * *** Nacio el 24/09 por el dueño del proyecto: "pos haz un boton que
 * grabe un trozo cada vez que le doy" ... "y cuando me canse de darle te
 * paso el fichero". Y se va hoy tambien por el: al preguntarle de donde
 * sacamos SRAM, eligio este. ***
 *
 * Era el ensayo del camino de escritura al pendrive antes de confiarle una
 * imagen de verdad, y sirvio: con el se cazo que el escritor se llevaba
 * por delante a los ficheros vecinos. Lo que hacia -sacar un fichero de la
 * radio a trozos- ya no hace falta, y el camino que probaba sigue teniendo
 * su banco, sim/fatanade.c, que nunca dependio de este boton.
 *
 * Costaba 4.096 bytes de SRAM PERMANENTES por una herramienta que solo se
 * usa una tarde.
 */

static const spi_flash_alta_t *alta_mira(void)
{
    if (!s_alta_hecho) {
        spi_flash_mira_alta(&s_alta);
        s_alta_hecho = 1U;
    }
    return &s_alta;
}

/* Un tamaño en bytes como "2,0 MB", o "1,5 MB", o "?" si no se midio. */
static uint8_t info_mb(char *b, uint8_t i, uint32_t bytes)
{
    uint32_t e, d;
    if (bytes == 0UL) { b[i++] = '?'; return i; }
    e = bytes / (1024UL * 1024UL);
    d = ((bytes % (1024UL * 1024UL)) * 10UL) / (1024UL * 1024UL);
    aj_u2s(&b[i], e);
    while (b[i] != '\0') { i++; }
    b[i++] = ','; b[i++] = (char)('0' + d);
    b[i++] = ' '; b[i++] = 'M'; b[i++] = 'B';
    return i;
}

static uint8_t info_hex32(char *b, uint8_t i, uint32_t v)
{
    static const char k_h[] = "0123456789ABCDEF";
    int8_t k;
    for (k = 7; k >= 0; k--) { b[i++] = k_h[(v >> (k * 4)) & 0xFU]; }
    return i;
}

/*
 * LAS FILAS DE INFORMACION, EN DOS GRUPOS
 * ---------------------------------------
 * De INFO_VERSION a INFO_TICS: lo que el micro sabe de si mismo. Sale de
 * registros y de variables que ya estan en RAM, asi que pintarlas no
 * cuesta nada medible y la pantalla abre al instante.
 *
 * De INFO_XCHIP en adelante: todo lo de la flash SPI de fuera. Esas si
 * cuestan, y mucho -ver el comentario de INFO_BASICAS-, y ademas son
 * herramientas de destripar, no informacion de diario.
 *
 * *** 24/09/2026, por el dueno del proyecto: "me sobra compilado equipo",
 * "todo lo que tenemos en la pagina 2 lo quiero oculto, y que la pagina 2
 * aparezca si pulsas 5 veces el boton de version", "y el boton de
 * siguiente tambien oculto, y una vez los muestres que se queden hasta que
 * se reinicie el equipo" ***
 *
 * Compilado (la fecha de compilacion) y Equipo ("DEEPSDR 101") se fueron
 * enteras: la primera solo le dice algo a quien compila, y la segunda dice
 * el modelo de la radio a quien la tiene en la mano.
 */
enum { INFO_VERSION = 0, INFO_MICRO, INFO_FLASH,
       INFO_SRAM, INFO_TCM, INFO_PILA, INFO_CASCADA, INFO_TEMP, INFO_BATT, INFO_TICS, INFO_RTC,
       INFO_XCHIP, INFO_XFS, INFO_XFORMATO, INFO_XALTA, INFO_XFIRMA, INFO_XIMG,
       INFO_XVOLCADO, INFO_XVOLMICRO, INFO_XVOLROM, INFO_XPRUEBA, INFO_XRESTAURA, INFO_XAVIONES,
       INFO_XBORRAF, INFO_XDFU,
       INFO_XIPA, INFO_XDMA, INFO_XBUS, INFO_XFRAME, INFO_XMSFRAME, INFO_XISR,
       INFO_XAUDIO,
       INFO_XAJUSTES, INFO_XARRANQUE, INFO_COUNT };

/*
 * DONDE ACABA LO BASICO Y EMPIEZA LO AVANZADO
 * -------------------------------------------
 * El corte no es un capricho de orden: es una frontera de COSTE.
 *
 * La flash SPI va a patadas por GPIO (ver spi_xfer_byte() en spi_flash.c:
 * dos microsegundos de retardo por bit, o sea dieciseis por byte). Y la
 * fila "Zona alta" la rellena spi_flash_mira_alta(), que recorre los 256
 * bloques de la zona de arriba leyendo 32 bytes del principio y 8 del
 * final de cada uno. Son 512 transacciones y unos 12 kB: alrededor de
 * 200 ms BLOQUEANDO el hilo de la interfaz, dentro del manejador del
 * toque. Como alta_mira() cachea, solo se paga una vez por arranque -pero
 * se pagaba justo al abrir Informacion, que es cuando mas canta-.
 *
 * Dejando las cinco filas de la flash del lado avanzado, la pagina que se
 * abre por defecto no toca el bus SPI ni una vez. Es el mismo principio
 * que ya se aplico al montaje del almacen y a la verificacion: lo lento
 * pasa detras de un toque explicito, nunca de sorpresa.
 *
 * Se escribe aparte del enum y con asertos detras para que meter una fila
 * nueva no rompa el reparto en silencio.
 */
#define INFO_BASICAS  ((uint8_t)INFO_XCHIP)

_Static_assert(INFO_BASICAS <= UIG_CELLS,
               "las filas basicas de Informacion ya no caben en una pagina");
_Static_assert((INFO_XVOLCADO - INFO_BASICAS) <= UIG_CELLS,
               "las filas de Flash externa ya no caben en una pagina");
_Static_assert((INFO_XIPA - INFO_XVOLCADO) <= UIG_CELLS,
               "las filas de Volcar y restaurar ya no caben en una pagina");
_Static_assert((INFO_COUNT - INFO_XIPA) <= UIG_CELLS,
               "las filas de Pantalla ya no caben en una pagina");

/* Primer toque arma la fila, el segundo salta. Se pone a cero al entrar en
 * la pantalla, igual que el contador de los cinco toques de la version: si
 * sales y vuelves, hay que volver a tocar dos veces. */
static uint8_t s_dfu_armado = 0U;

static const texto_t k_info_nombres[INFO_COUNT] = {
    T("Versión", "Version"), T("Micro", "MCU"), T("Flash", "Flash"),
    T("SRAM", "SRAM"), T("TCM", "TCM"),
    T("Pila", "Stack"), T("Cascada", "Waterfall"),
    T("Temp. del chip", "Chip temp."), T("Batería", "Battery"),
    T("Reloj", "Clock"), T("RTC", "RTC"),
    T("Flash externa", "External flash"), T("Sist. ficheros", "File system"),
    T("Formato", "Format"), T("Zona alta", "Upper area"),
    T("Firma update4", "update4 signature"),
    T("Imágenes", "Images"), T("Volcar zona alta", "Dump upper area"),
    T("Volcar flash micro", "Dump MCU flash"),
    T("Volcar ROM de fábrica", "Dump factory ROM"), T("Probar imágenes", "Test images"),
    T("Restaurar zona", "Restore area"), T("Cargar datos", "Load data"),
    T("Borrar zona alta", "Erase upper area"),
    T("Modo DFU", "DFU mode"),
    T("Acelerador IPA", "IPA accelerator"), T("DMA de pantalla", "Display DMA"),
    T("Bus pantalla", "Display bus"), T("Fotograma", "Frame"),
    T("Tope de fotograma", "Frame budget"),
    T("Carga de audio", "Audio load"),
    T("Salud del audio", "Audio health"),
    T("Guardados", "Saves"), T("Al arrancar", "At boot")
};

/*
 * LA PAGINA 2, QUE NO EXISTE HASTA QUE SE PIDE
 * --------------------------------------------
 * Cinco toques en la fila de la version la destapan, y se queda destapada
 * hasta que se apaga la radio. NO se guarda en ajustes a proposito: lo que
 * hay detras son el volcado, el borrado y la restauracion de la zona alta,
 * y eso es mejor que haya que volver a pedirlo cada vez que se enciende
 * que no que se quede abierto para siempre porque un dia hiciste cinco
 * toques.
 *
 * El contador se pone a cero al entrar en la pantalla (ver grid_show), asi
 * que son cinco toques SEGUIDOS, no cinco repartidos por la semana.
 */
#define INFO_TOQUES  5U

static uint8_t s_info_avanzado;   /* la pagina 2 esta destapada */
static uint8_t s_info_toques;     /* cuantos llevamos en la fila de version */

/* 32 y no 24 (25/09/2026). La fila de la cascada ya llega a 23 caracteres
 * con el contador de fotogramas, y a 24 si los milisegundos pasan de una
 * cifra: estaba justo en el borde. Doce celdas por ocho bytes son 96, que a
 * cambio de quitar un acantilado se pagan solos. */
static char s_info_val[UIG_CELLS][32];

/* Los dos renglones de abajo de cada celda de la rejilla de modos de SSTV.
 * Se arman al vuelo, como los de Informacion, asi que necesitan donde
 * vivir mientras se pintan. */
static char s_sstv_v2[UIG_CELLS][8];
static char s_sstv_v3[UIG_CELLS][12];


/* El texto de las filas de volcado. Ver el comentario en su case. */
static const char *volcado_valor_txt(char *buf)
{
    uint8_t i;
    switch (s_vol_estado) {
    case 1U: {
        uint32_t t = spi_flash_volcado_total();
        uint32_t h = spi_flash_volcado_hechos();
        aj_u2s(buf, (t != 0UL) ? (h * 100UL / t) : 0UL);
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ' '; buf[i++] = '%'; buf[i] = '\0';
        return buf;
    }
    case 2U: {
        /* Cuanto se ha sacado de verdad, que con un fichero mas
         * pequeño que la zona no es todo - ver volcado_arranca(). */
        uint8_t k = 0U;
        buf[k++] = 'h'; buf[k++] = 'e'; buf[k++] = 'c'; buf[k++] = 'h';
        buf[k++] = 'o'; buf[k++] = ','; buf[k++] = ' ';
        aj_u2s(&buf[k], s_vol_bytes / 1024UL);
        while (buf[k] != '\0') { k++; }
        buf[k++] = ' '; buf[k++] = 'k'; buf[k++] = 'B'; buf[k] = '\0';
        return buf;
    }
    case 3U:
        /* El motivo EXACTO, que son seis distintos - ver
         * spi_flash_volcado_porque(). La primera version decia
         * "falta VOLCADO.BIN" para todos, y el fichero estaba
         * puesto: el mensaje mandaba a mirar donde no era. */
        return s_vol_sin_zona ? "no hay zona arriba"
                          : spi_flash_volcado_porque_txt();
    default: return tr("tocar", "tap");
    }
}

static const char *info_valor(uint8_t id, char *buf)
{
    /* El buffer viene con lo que escribio la vez anterior -es el mismo de
     * esta celda, y al cambiar de pagina la celda cambia de fila-. Dejarlo
     * terminado de entrada no arregla que alguien se olvide de poner el cero
     * al final, pero si evita que lo de antes se cuele detras de lo nuevo,
     * que es justo lo que paso con la fila de Fotograma. */
    buf[0] = '\0';

    switch (id) {
    case INFO_VERSION: {
        /*
         * La version, y a partir del tercer toque cuantos faltan para
         * destapar la pagina 2.
         *
         * Los dos primeros toques no dicen nada: quien no sabe el truco no
         * tiene por que enterarse de que existe. Del tercero en adelante si,
         * porque cinco toques sin ninguna respuesta se sienten como un
         * boton roto y acabas dando veinte.
         */
        /*
         * LA VERSION LLEVA EL CRC DETRAS - 02/10/2026.
         *
         * *** Por el dueño, y con razon: "no estas cambiando el numero de
         * version en los updates". ***
         *
         * Salieron SEIS update.bin distintos llamados todos V3.00. Subir
         * el numero cada vez es lo primero, pero depende de que yo me
         * acuerde, y eso ya se ha demostrado que no basta.
         *
         * Esto no depende de que nadie se acuerde: el CRC32 sale de la
         * cabecera que tools/cabecera.py le pega a la imagen, o sea que es
         * del CONTENIDO. Dos binarios distintos no pueden enseñar el mismo
         * numero aunque los dos digan V3.00, y comparar el de la pantalla
         * con el que imprime el make dice sin ninguna duda cual esta
         * grabado.
         *
         * Se lee de la flash interna, en CARGA_APP_BASE + 0x200 + 8. Ver
         * User/cabecera_app.c para el formato.
         */
        const char *v = CONFIG_FW_VERSION;
        uint8_t i = 0U, faltan;
        const uint8_t *cab = (const uint8_t *)(CARGA_CAB_ADDR);
        if (s_info_avanzado || s_info_toques < 2U) {
            uint32_t crc;
            while (v[i] != '\0') { buf[i] = v[i]; i++; }
            crc = (uint32_t)cab[8] | ((uint32_t)cab[9] << 8)
                | ((uint32_t)cab[10] << 16) | ((uint32_t)cab[11] << 24);
            buf[i++] = ' '; buf[i++] = '/'; buf[i++] = ' ';
            i = (uint8_t)(i + info_hex32(&buf[i], 0U, crc));
            buf[i] = '\0';
            return buf;
        }
        while (v[i] != '\0') { buf[i] = v[i]; i++; }
        faltan = (uint8_t)(INFO_TOQUES - s_info_toques);
        buf[i++] = ' '; buf[i++] = '(';
        buf[i++] = (char)('0' + faltan);
        buf[i++] = ')'; buf[i] = '\0';
        return buf;
    }
    case INFO_TICS: {
        /* Tics perdidos. Tiene que poner 0; si pone otra cosa, el reloj se
         * esta quedando corto y hay una interrupcion tapando a SysTick. Ver
         * irq_prio.h y tics_poll(). */
        char *p = buf;
        if (s_tics_perdidos == 0U) {
            const char *t = tr("0 tics perdidos", "0 lost ticks");
            while (*t != '\0') { *p++ = *t++; }
        } else {
            /* Ancho variable, sin ceros delante: los hora_uN() de este
             * fichero son de ancho fijo porque un reloj tiene que ser de
             * ancho fijo, y aqui eso se leeria como "00042". */
            uint32_t v = s_tics_perdidos, div = 1U;
            while (v / div >= 10U) { div *= 10U; }
            while (div > 0U) { *p++ = (char)('0' + ((v / div) % 10U)); div /= 10U; }
            { const char *t = tr(" PERDIDOS", " LOST");
              while (*t != '\0') { *p++ = *t++; } }
        }
        *p = '\0';
        return buf;
    }
    case INFO_MICRO:   return "GD32F450VET6";

    case INFO_RTC: {
        /*
         * Si el RTC esta andando y desde cuando se sabe su hora.
         *
         * Las tres respuestas son distintas y hay que poder distinguirlas:
         * el cristal no arranco (problema de placa, y entonces el reloj esta
         * corriendo sobre SysTick como antes), el cristal va pero nadie le ha
         * dicho la hora nunca (va, pero marca la epoca por defecto), y va y
         * esta puesto (y entonces interesa cuanto hace, porque un cristal se
         * va unos segundos al dia).
         */
        uint32_t seg;
        uint8_t  i = 0U;

        if (rtc_hw_lxtal_failed()) { return tr("cristal KO", "crystal dead"); }
        if (!rtc_hw_has_ever_synced()) {
            return tr("en hora: nunca", "set: never");
        }

        seg = rtc_hw_get_seconds_since_sync();
        /*
         * DOS TROZOS, uno delante y otro detras, y no una sola cadena: en
         * español el "hace" va ANTES del numero ("en hora hace 50 m") y en
         * ingles el "ago" va DESPUES ("set 50 m ago"). Una sola llamada a
         * tr() solo puede poner texto en un sitio, asi que saldria "set 50 m"
         * a secas, que no dice si hace 50 minutos o dentro de 50.
         */
        { const char *t = tr("en hora hace ", "set ");
          while (*t != '\0') { buf[i++] = *t++; } }
        if (seg < 3600UL) {
            aj_u2s(&buf[i], seg / 60UL);
            while (buf[i] != '\0') { i++; }
            buf[i++] = ' '; buf[i++] = 'm';
        } else if (seg < 86400UL) {
            aj_u2s(&buf[i], seg / 3600UL);
            while (buf[i] != '\0') { i++; }
            buf[i++] = ' '; buf[i++] = 'h';
        } else {
            /* Topado a 999 dias. Mas de dos años sin poner el reloj no es un
             * dato, y el numero sin topar -hasta 49.710 dias- se sale de la
             * celda y se pinta encima de la de al lado. Lo caza sim/infotest.c. */
            uint32_t d = seg / 86400UL;
            aj_u2s(&buf[i], (d > 999UL) ? 999UL : d);
            while (buf[i] != '\0') { i++; }
            buf[i++] = ' '; buf[i++] = 'd';
        }
        { const char *t = tr("", " ago");   /* ver el comentario de arriba */
          while (*t != '\0') { buf[i++] = *t++; } }
        buf[i] = '\0';
        return buf;
    }

    case INFO_XCHIP: {
        /*
         * El tamaño MEDIDO. Y solo si no coincide con el que dice el
         * identificador, tambien ese: un chip clonado que se anuncia mas
         * grande de lo que es no es una curiosidad, es la forma de romper
         * el sistema de ficheros -escribir por encima de su tamaño de
         * verdad da la vuelta y cae encima del principio-. Cuando los dos
         * cuadran no se enseña nada de eso, que seria ruido.
         */
        const spi_flash_alta_t *a = alta_mira();
        uint8_t i = info_mb(buf, 0U, a->bytes_mide);
        if (a->bytes_dice != a->bytes_mide) {
            buf[i++] = ' '; buf[i++] = '!'; buf[i++] = ' ';
            buf[i++] = 'd'; buf[i++] = 'i'; buf[i++] = 'c'; buf[i++] = 'e'; buf[i++] = ' ';
            i = info_mb(buf, i, a->bytes_dice);
        }
        buf[i] = '\0';
        return buf;
    }
    case INFO_XFS: {
        const spi_flash_alta_t *a = alta_mira();
        uint8_t i = info_mb(buf, 0U, a->fs_bytes);
        buf[i] = '\0';
        return buf;
    }
    case INFO_XFORMATO:
        /*
         * Si la distribucion del volumen es una que este driver sabe
         * manejar. Formatear el pendrive desde el PC la cambia, y con las
         * constantes viejas eso corrompia el volumen en la primera
         * escritura. Ahora se comprueba, y esta fila dice el veredicto.
         */
        return spi_flash_geo_txt();

    case INFO_XALTA: {
        /* El mapa de la zona que queda por encima del sistema de
         * ficheros, partida en ocho: '#' es "aqui hay algo". */
        const spi_flash_alta_t *a = alta_mira();
        uint8_t i, k;
        if (a->bloques_mirados == 0UL) {
            return tr("no medida", "not measured");
        }
        for (k = 0U; k < 8U; k++) { buf[k] = a->mapa[k] ? '#' : '.'; }
        i = 8U; buf[i++] = ' ';
        aj_u2s(&buf[i], a->bloques_con_datos);
        while (buf[i] != '\0') { i++; }
        buf[i++] = '/';
        aj_u2s(&buf[i], a->bloques_mirados);
        return buf;
    }
    case INFO_XIMG: {
        /* Cuantas imagenes guardadas y cuanto queda. Con el chip de
         * fabrica pone que no hay sitio, que no es un fallo: es que ese
         * chip no tiene ningun sitio en el que se pueda escribir sin
         * estorbar al gestor de arranque. */
        uint8_t i;
        /* Sin montar no se monta AQUI: ver imgs_monta(). Se dice, que es
         * mas honrado que enseñar un cero que parece "no hay sitio". */
        if (!s_imgs_hecho) { return tr("sin montar", "not mounted"); }
        /* Si no hay almacen, POR QUE no lo hay. */
        if (imgs_total() == 0UL) { return imgs_veredicto_texto(); }
        aj_u2s(buf, (uint32_t)imgs_cuantas());
        for (i = 0U; buf[i] != '\0'; i++) { }
        buf[i++] = ','; buf[i++] = ' ';
        i = info_mb(buf, i, imgs_libre());
        buf[i] = '\0';
        return buf;
    }
    case INFO_XPRUEBA: {
        uint8_t i;
        switch (s_pru_estado) {
        case 1U:
            aj_u2s(buf, s_pru_hechos * 100UL / PRUEBA_BYTES);
            for (i = 0U; buf[i] != '\0'; i++) { }
            buf[i++] = ' '; buf[i++] = '%'; buf[i] = '\0';
            return buf;
        case 5U: {
            uint32_t t = imgs_verif_total();
            aj_u2s(buf, (t != 0UL) ? (imgs_verif_hechos() * 100UL / t) : 0UL);
            for (i = 0U; buf[i] != '\0'; i++) { }
            buf[i++] = '%'; buf[i++] = ' '; buf[i++] = 'v'; buf[i++] = 'e';
            buf[i++] = 'r'; buf[i] = '\0';
            return buf;
        }
        case 2U: return tr("escrita: ya flashea", "written: flashing now");
        case 3U: return tr("INTACTA", "UNTOUCHED");
        case 4U: return tr("NO CUADRA", "MISMATCH");
        default:
            if (!s_imgs_hecho) { return tr("tocar: montar", "tap: mount"); }
            return (imgs_cuantas() > 0U) ? tr("tocar: verificar", "tap: verify")
                                         : tr("tocar: escribir", "tap: write");
        }
    }
    case INFO_XDFU:
        /*
         * LA FILA QUE APAGA LA RADIO A PROPOSITO.
         *
         * *** El dueno: "ponle un boton dfu en el menu de informacion, en
         * la parte oculta". *** Salta al cargador DFU de fabrica del chip,
         * que reprograma la flash por el mismo USB-C. Sirve para cambiar el
         * CARGADOR DE ARRANQUE sin abrir la radio: un cargador no puede
         * reescribirse a si mismo, el de fabrica si.
         *
         * Pide DOS toques. No porque sea peligroso -se deshace apagando y
         * encendiendo, no se borra nada- sino porque desde fuera parece que
         * la radio se ha colgado: pantalla quieta y sin audio. Un toque sin
         * querer en la pagina oculta y el dueno pensando que ha roto algo.
         */
        return s_dfu_armado ? tr("Tocar otra vez", "Tap again")
                            : tr("Reiniciar en DFU", "Reboot into DFU");

    case INFO_XBORRAF:
        if (s_vol_estado == 1U) { return tr("en curso", "in progress"); }
        if (s_vol_estado == 2U) { return tr("borrada", "erased"); }
        if (!spi_flash_fichero_hay(k_zona_nombre, k_vol_ext)) {
            /* Se dice ANTES de tocar, no despues: el que mira la celda
             * tiene que saber que le falta el paracaidas. */
            return tr("falta ZONAALTA.BIN", "ZONAALTA.BIN missing");
        }
        return (s_vol_estado == 3U) ? spi_flash_volcado_porque_txt()
                                    : tr("tocar: borra todo", "tap: erases everything");
    case INFO_XRESTAURA:
        if (s_vol_estado == 1U) { return tr("en curso", "in progress"); }
        if (s_vol_estado == 3U) { return spi_flash_volcado_porque_txt(); }
        return (s_vol_estado == 2U) ? tr("hecho", "done")
                                    : tr("tocar: ZONAALTA.BIN", "tap: ZONAALTA.BIN");
    case INFO_XAVIONES:
        if (s_vol_estado == 1U) { return tr("en curso", "in progress"); }
        /* El motivo PROPIO si lo hay, y el del volcado solo si no. Ver
         * s_datos_porque: cada fallo tiene el suyo desde el 28/09/2026. */
        if (s_vol_estado == 3U) {
            return (s_datos_porque != 0) ? s_datos_porque
                                         : spi_flash_volcado_porque_txt();
        }
        if (s_vol_estado == 2U) { return tr("hecho", "done"); }
        /* Si el fichero no esta en el disco se dice ANTES de tocar: es un
         * traslado de medio mega y enterarse a mitad es tarde. */
        /*
         * Y AQUI NO SE DICE "FALTA" SIN HABERLO COMPROBADO - 28/09/2026.
         *
         * *** El dueño: "el fichero esta, te lo aseguro". *** Y estaba:
         * la celda decia "falta BD.BIN" con el fichero delante, porque
         * spi_flash_fichero_hay() devuelve 0 por CUATRO motivos -no esta,
         * mide cero, el primer cluster no vale, o la cadena esta
         * troceada- y yo los contaba todos como "falta".
         *
         * El motivo de verdad lleva desde siempre en s_vol_porque. Lo que
         * faltaba era mirarlo.
         */
        if (spi_flash_fichero_hay("BD      ", "BIN")) {
            return tr("tocar: BD.BIN", "tap: BD.BIN");
        }
        return spi_flash_volcado_porque_txt();
    case INFO_XVOLROM:
    case INFO_XVOLMICRO:
    case INFO_XVOLCADO:
        /*
         * LOS TRES VOLCADOS DICEN LO MISMO, Y AHORA DE VERDAD.
         *
         * *** El dueno, el 01/10/2026: "volcar rom de fabrica deberia de
         * hacer algo si lo toco?" *** Y hacia algo: fallar. Pero la fila de
         * la ROM y la de la flash del micro solo sabian decir "en curso",
         * "hecho" y "tocar", asi que un fallo -estado 3- caia en el default
         * y se pintaba "tocar": EXACTAMENTE igual que si no lo hubieras
         * tocado. La fila mentia por omision.
         *
         * Ahora las tres comparten el texto de VOLCADO, que es el que ya
         * sabia decir el tanto por ciento mientras corre y el motivo exacto
         * cuando falla -seis distintos, ver spi_flash_volcado_porque()-.
         */
        return volcado_valor_txt(buf);
    case INFO_XFIRMA: {
        const spi_flash_alta_t *a = alta_mira();
        uint8_t i;
        if (!a->firma_hay) { return tr("no aparece", "not present"); }
        i = info_hex32(buf, 0U, a->firma_addr);
        buf[i] = '\0';
        return buf;
    }
    case INFO_FLASH:
        /* _eflash lo pone el enlazador: es donde acaba lo ultimo que se
         * graba. La cuenta anterior sumaba ademas el tamano de .tcmram,
         * porque hasta el 23/09/2026 esa seccion guardaba en flash una copia
         * inicial de 48 kB de ceros que nadie llegaba a usar. Ya no la
         * guarda, asi que ya no hay que sumarla. */
        info_kb(buf, (uint32_t)&_eflash - INFO_FLASH_ORIGEN, INFO_FLASH_TOPE);
        return buf;
    case INFO_SRAM:
        info_kb(buf, ((uint32_t)&_edata - (uint32_t)&_sdata) +
                     ((uint32_t)&_ebss - (uint32_t)&_sbss), INFO_SRAM_TOPE);
        return buf;
    case INFO_TCM:
        info_kb(buf, (uint32_t)&_etcmram - (uint32_t)&_stcmram, INFO_TCM_TOPE);
        return buf;
    case INFO_XARRANQUE: {
        /* "17cl wf60 t3": cuantas claves de tabla traia CONFIG.CSV al
         * arrancar, y que valian las dos que se pierden. Un "-" quiere decir
         * que esa clave NO venia en el fichero. Ver s_arranque_wf. */
        uint8_t i = 0U;

        aj_u2s(buf, (uint32_t)s_arranque_claves);
        while (buf[i] != '\0') { i++; }
        buf[i++] = 'c'; buf[i++] = 'l'; buf[i++] = ' ';
        buf[i++] = 'w'; buf[i++] = 'f';
        if (s_arranque_wf < 0) { buf[i++] = '-'; buf[i] = '\0'; }
        else { aj_u2s(&buf[i], (uint32_t)s_arranque_wf); while (buf[i] != '\0') { i++; } }
        buf[i++] = ' '; buf[i++] = 't';
        if (s_arranque_tema < 0) { buf[i++] = '-'; buf[i] = '\0'; }
        else { aj_u2s(&buf[i], (uint32_t)s_arranque_tema); while (buf[i] != '\0') { i++; } }
        buf[i] = '\0';
        return buf;
    }
    case INFO_XMSFRAME: {
        /* Los milisegundos que se le dan a cada fotograma y los fotogramas
         * por segundo que salen de ahi. Ver el comentario de
         * SPECTRUM_FRAME_MS. Se toca para bajarlo; no se guarda. */
        uint8_t i = 0U;

        aj_u2s(buf, s_frame_ms);
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = 'm'; buf[i++] = 's';
        buf[i++] = ' '; buf[i++] = '\xE2'; buf[i++] = '\x86'; buf[i++] = '\x92';
        buf[i++] = ' ';
        aj_u2s(&buf[i], 1000UL / (s_frame_ms ? s_frame_ms : 1UL));
        while (buf[i] != '\0') { i++; }
        buf[i++] = 'f'; buf[i++] = 'p'; buf[i++] = 's';
        buf[i] = '\0';
        return buf;
    }
    case INFO_XISR: {
        /*
         * CUANTO DEL PRESUPUESTO SE COME LA INTERRUPCION DEL AUDIO -
         * 30/09/2026.
         *
         * *** Por el dueño: "si activo el boton de ruido tambien se
         * ralentiza". ***
         *
         * El desglose por etapas existe desde el 31/07, pero solo salia por
         * el puerto serie, que va apagado en la compilacion de verdad
         * (DEBUG_UART_ENABLED=0). O sea que la pregunta "¿cuanto cuesta la
         * reduccion de ruido en ESTA radio?" no se podia contestar mirando
         * la radio, solo estimando desde el simulador. Y una estimacion no
         * es una medida.
         *
         * Aqui sale lo unico que hace falta para contestarla: los ciclos
         * que tardo el ultimo bloque frente a los que caben en el tiempo de
         * ese bloque, en tantos por ciento, y aparte lo que se lleva la
         * reduccion de ruido. Se lee con el boton de Ruido apagado y con el
         * encendido, y la diferencia es el dato.
         *
         * El presupuesto: a 96 kHz y 256 muestras por bloque, un bloque dura
         * 2,667 ms, que a 200 MHz son 533.333 ciclos. A 48 kHz, el doble de
         * tiempo. Se calcula, no se escribe: la tasa la puede cambiar el
         * usuario con la pastilla RATE.
         */
        uint32_t tasa = s_nonwfm_use_48k ? 48000UL : 96000UL;
        uint32_t tope = (SystemCoreClock / tasa) * (uint32_t)SDR_RX_BLOCK_SAMPLES;
        uint32_t usados = demod_am_get_last_cycles();
        demod_am_cycles_breakdown_t bd = demod_am_get_last_cycles_breakdown();
        uint8_t i = 0U;

        if (tope == 0UL) { return "?"; }
        /* Topado a 999: por encima del 100 % ya no importa cuanto. */
        {
            uint32_t pc = (uint32_t)(((uint64_t)usados * 100ULL) / tope);
            uint32_t pn = (uint32_t)(((uint64_t)bd.nr * 100ULL) / tope);

            if (pc > 999UL) { pc = 999UL; }
            if (pn > 999UL) { pn = 999UL; }
            aj_u2s(buf, pc);
            while (buf[i] != '\0') { i++; }
            buf[i++] = ' '; buf[i++] = '%';
            buf[i++] = ','; buf[i++] = ' ';
            aj_u2s(&buf[i], pn);
            while (buf[i] != '\0') { i++; }
            buf[i++] = ' ';
            /* "rui" / "nr": el nombre corto de la reduccion de ruido. */
            { const char *t = tr("rui", "nr");
              while (*t != '\0') { buf[i++] = *t++; } }
            buf[i] = '\0';
        }
        return buf;
    }

    case INFO_XAUDIO: {
        /* Los dos contadores de error que ya llevaban las dos cadenas de
         * audio y que nunca habian salido de su fichero: fallos de trama
         * del I2S que saca el sonido, y de la captura de radiofrecuencia.
         * Los dos deberian quedarse en cero.
         *
         * Se pone aqui porque el dueno pregunto si el audio habia empeorado
         * en algun momento de la noche y yo no tenia con que contestar. Dos
         * numeros y se deja de opinar: si suben mientras se escucha, hay un
         * problema de verdad y ademas se sabe de que lado; si se quedan en
         * cero y aun asi suena peor, el problema no es que se pierdan
         * muestras y hay que mirar otra cosa. */
        uint8_t i = 0U;

        aj_u2s(buf, gd32_i2s_get_tx_ferr_count());
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = 's'; buf[i++] = 'a'; buf[i++] = 'l';
        buf[i++] = ','; buf[i++] = ' ';
        aj_u2s(&buf[i], sdr_rx_get_ferr_count());
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = 'r'; buf[i++] = 'f';
        buf[i] = '\0';
        return buf;
    }
    case INFO_XAJUSTES: {
        /* Cuantas veces se ha guardado CONFIG.CSV desde que arranco, cuantas
         * han fallado, y hace cuanto fue la ultima buena. Ver el comentario
         * de los contadores en settings.c: esto convierte "no se me guardan
         * los ajustes" en un numero. */
        uint16_t ok = 0U, mal = 0U;
        uint32_t ult = 0U;
        uint8_t  i = 0U;

        settings_cuentas(&ok, &mal, &ult);
        aj_u2s(buf, (uint32_t)ok);
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = 'o'; buf[i++] = 'k'; buf[i++] = ',';
        buf[i++] = ' ';
        aj_u2s(&buf[i], (uint32_t)mal);
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = 'm'; buf[i++] = 'a'; buf[i++] = 'l';
        if (ok != 0U) {
            buf[i++] = ','; buf[i++] = ' ';
            aj_u2s(&buf[i], (g_msticks - ult) / 1000UL);
            while (buf[i] != '\0') { i++; }
            buf[i++] = 's';
        }
        buf[i] = '\0';
        return buf;
    }
    case INFO_XIPA: {
        /* El acelerador grafico encendido o apagado, y las veces que tuvo
         * que rendirse. Se toca para cambiarlo: es la unica forma de
         * comparar de verdad -mismo trasto, misma senal, mismo minuto- en
         * vez de fiarse de dos medidas tomadas con cinco minutos y un
         * reflasheo por medio. */
        uint8_t i = 0U;
        const char *e = ipa_blit_hay() ? "sí" : "no";

        while (*e != '\0') { buf[i++] = *e++; }
        if (ipa_blit_fallos() != 0U) {
            buf[i++] = ','; buf[i++] = ' ';
            aj_u2s(&buf[i], (uint32_t)ipa_blit_fallos());
            while (buf[i] != '\0') { i++; }
            buf[i++] = ' '; buf[i++] = 'f'; buf[i++] = 'a';
            buf[i++] = 'l'; buf[i++] = 'l';
        }
        buf[i] = '\0';
        return buf;
    }
    case INFO_XDMA: {
        /* El DMA que empuja los pixeles. "no" puede significar dos cosas
         * distintas y conviene distinguirlas: apagado a mano, o que la
         * autoprueba del arranque dijo que no. Ver lcd_dma.h. */
        uint8_t i = 0U;
        const char *e = lcd_dma_hay() ? "sí" : "no";

        while (*e != '\0') { buf[i++] = *e++; }
        if (lcd_dma_fallos() != 0U) {
            buf[i++] = ','; buf[i++] = ' ';
            aj_u2s(&buf[i], (uint32_t)lcd_dma_fallos());
            while (buf[i] != '\0') { i++; }
            buf[i++] = ' '; buf[i++] = 'f'; buf[i++] = 'a';
            buf[i++] = 'l'; buf[i++] = 'l';
        }
        buf[i] = '\0';
        return buf;
    }
    case INFO_XBUS: {
        /* Ciclos de dato y los nanosegundos que salen. Se toca para bajar un
         * escalon. Ver rm68120_bus_dato_pon(): esto NO se guarda, asi que si
         * te pasas y la pantalla se vuelve ilegible, apagar la arregla. */
        uint8_t i = 0U;

        aj_u2s(buf, (uint32_t)rm68120_bus_dato());
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = '\xE2'; buf[i++] = '\x86'; buf[i++] = '\x92';
        buf[i++] = ' ';
        aj_u2s(&buf[i], (uint32_t)rm68120_bus_ns());
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = 'n'; buf[i++] = 's';
        buf[i] = '\0';
        return buf;
    }
    case INFO_XFRAME: {
        /* Las tres patas del fotograma en milisegundos: FFT, espectro y
         * cascada. Es el reparto que decide donde merece la pena tocar, y
         * hasta hoy solo salia por un puerto serie apagado.
         *
         * Cuatro numeros: FFT + marco + traza + cascada. El marco (regla,
         * canaletas, panel) va por el compositor gfx2_render(); la traza es
         * el bucle de pixeles de spectrum_draw(). Juntos no dicen nada. */
        uint8_t  i = 0U;
        uint32_t c = s_wf_ciclos / (SystemCoreClock / 1000000U);

        aj_u2s(buf, (uint32_t)(s_fft_us / 1000U));
        while (buf[i] != '\0') { i++; }
        buf[i++] = '+';
        aj_u2s(&buf[i], (uint32_t)(s_chrome_us / 1000U));
        while (buf[i] != '\0') { i++; }
        buf[i++] = '+';
        /* El espectro SIN el marco: el cronometro de fuera los mide juntos. */
        aj_u2s(&buf[i], (uint32_t)((s_spec_us > s_chrome_us)
                                   ? ((s_spec_us - s_chrome_us) / 1000U) : 0U));
        while (buf[i] != '\0') { i++; }
        buf[i++] = '(';
        /* De la traza, lo que se fue solo en empujar pixeles al panel. Si
         * este numero es casi toda la traza, el problema es el bus o la
         * falta de un DMA; si es una parte pequena, el problema es el bucle
         * que decide los pixeles, y el bus no tiene nada que ver. */
        aj_u2s(&buf[i], (uint32_t)(s_push_us / 1000U));
        while (buf[i] != '\0') { i++; }
        buf[i++] = ')';
        /* SIN "while (buf[i] != 0) i++;" aqui. Lo puse y era un fallo de
         * libro: ese bucle avanza hasta encontrar un cero, y despues de
         * escribir un ')' a mano NO hay cero - hay lo que quedara en el
         * buffer de la vez anterior. Se colaba el "6 kB" de la fila de Pila,
         * que comparte buffer, y en cada repintado se anadia otro "+4 ms".
         *
         * Los demas "while" de esta funcion si valen: van justo detras de un
         * aj_u2s(), que SI deja el cero. Detras de un caracter escrito a mano
         * la posicion ya la sabemos, que es i, y no hay nada que buscar. */
        buf[i++] = '+';
        aj_u2s(&buf[i], c / 1000U);
        while (buf[i] != '\0') { i++; }
        buf[i++] = ' '; buf[i++] = 'm'; buf[i++] = 's';
        /* Y al final, las FFT del fotograma y en cual se partio: "5/2". */
        buf[i++] = ' ';
        aj_u2s(&buf[i], (uint32_t)s_tanda_n);
        while (buf[i] != '\0') { i++; }
        buf[i++] = '/';
        aj_u2s(&buf[i], (uint32_t)s_tanda_mitad);
        while (buf[i] != '\0') { i++; }
        /*
         * Y LA VUELTA ENTERA, QUE ES LO QUE FALTABA - 01/10/2026.
         *
         * "b31 t1 p0": la vuelta completa del bucle principal, lo que se
         * fue en el tactil y lo que se fue en los poll de los
         * decodificadores, en milisegundos.
         *
         * Los cuatro numeros de la izquierda cubren el DIBUJO y nada mas.
         * Comparando los dos cargadores salio que el dibujo cuesta lo mismo
         * -13 ms de traza, 0 de empujar pixeles- y aun asi el fotograma
         * tardaba 31 ms con el nuestro y 24 con el de serie. Con la vuelta
         * entera medida, la resta dice de una vez donde se van esos 8 ms:
         *
         *   b mucho mayor que la suma de la izquierda -> esta fuera del
         *     dibujo, y t y p dicen si es el tactil o los decodificadores;
         *   b parecido a la suma -> entonces es espera, y hay que mirar
         *     quien la impone.
         */
        buf[i++] = ' '; buf[i++] = 'b';
        aj_u2s(&buf[i], (uint32_t)(s_bucle_us / 1000U));
        while (buf[i] != '\0') { i++; }
        buf[i++] = '/';
        aj_u2s(&buf[i], (uint32_t)(s_tactil_us / 1000U));
        while (buf[i] != '\0') { i++; }
        buf[i++] = '/';
        aj_u2s(&buf[i], (uint32_t)(s_polls_us / 1000U));
        while (buf[i] != '\0') { i++; }
        buf[i] = '\0';
        return buf;
    }
    case INFO_CASCADA: {
        /* "7,2 ms · 8/s": lo que tardo el ultimo volcado, y las lineas por
         * segundo que se estan consiguiendo. Ver s_wf_ciclos. */
        /* Division de 32 bits a proposito: multiplicar por un millon y dividir
         * en 64 bits arrastra __udivmoddi4 de libgcc, y aqui no hace falta.
         * SystemCoreClock/1000000 son los ciclos que dura un microsegundo
         * (200 a 200 MHz), y el mayor volcado imaginable son unos pocos
         * millones de ciclos: cabe de sobra en 32 bits. */
        uint32_t us = s_wf_ciclos / (SystemCoreClock / 1000000U);
        uint8_t  i  = 0U;

        aj_u2s(&buf[i], us / 1000U);
        while (buf[i] != '\0') { i++; }
        buf[i++] = ',';
        buf[i++] = (char)('0' + (char)((us / 100U) % 10U));
        buf[i++] = ' '; buf[i++] = 'm'; buf[i++] = 's';
        buf[i++] = ' '; buf[i++] = '\xC2'; buf[i++] = '\xB7'; buf[i++] = ' ';
        /* CONSEGUIDAS/PEDIDAS. Lo de al lado no es adorno: k_wf dice cuantas
         * lineas por segundo PIDE la velocidad elegida, y el primer numero
         * es cuantas SALEN. Con los dos juntos, un vistazo distingue tres
         * cosas que por separado se confunden:
         *
         *   11/30  se pide mas de lo que da tiempo -> falta maquina
         *   11/11  se esta dando lo que se pide    -> el limite es la tabla
         *   11/3   se consiguen mas de las pedidas -> la medida esta mal
         *
         * Y sobre todo: si el segundo numero NO cambia al cambiar la
         * velocidad, el problema no es la velocidad de dibujo sino que el
         * ajuste no se esta aplicando. */
        aj_u2s(&buf[i], (uint32_t)((s_wf_lps != 0U) ? s_wf_lps : s_wf_lps_vivo));
        while (buf[i] != '\0') { i++; }
        buf[i++] = '/';
        aj_u2s(&buf[i], (uint32_t)k_wf[s_wf_vel].clave);
        while (buf[i] != '\0') { i++; }
        /* Y los fotogramas por segundo: ver s_fps. Con los tres numeros
         * juntos no hay nada que deducir - 28 lineas a 28 fotogramas es una
         * linea por fotograma, y 28 a 14 son dos. */
        buf[i++] = ' '; buf[i++] = '\xC2'; buf[i++] = '\xB7'; buf[i++] = ' ';
        aj_u2s(&buf[i], (uint32_t)((s_fps != 0U) ? s_fps : s_fps_vivo));
        while (buf[i] != '\0') { i++; }
        buf[i++] = 'f';
        buf[i] = '\0';
        return buf;
    }
    case INFO_PILA:
        /* Usada de disponible. El tope no es una constante escrita a mano:
         * es el hueco real que deja el enlazador entre el final de .tcmram
         * y el SP inicial. Ver pila_pinta(). */
        info_kb(buf, pila_usada(), (uint32_t)&_estack - (uint32_t)&end);
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


/*
 * LOS CANALES DE HFDL QUE SE OYEN DESDE AQUI. 25/09/2026.
 *
 * *** Por el dueno del proyecto: "ponme un botoncito con todas las posibles
 * frecuencias... que le des y te salga una ventana para elegir". ***
 *
 * LA TABLA ENTERA TIENE 105 FRECUENCIAS y estan casi todas de mas: son las
 * diecisiete estaciones de tierra del mundo, y desde Espana no se oyen ni
 * San Francisco ni Agana ni Auckland salvo milagro de propagacion. Meter
 * las 105 seria nueve paginas para usar dos.
 *
 * Asi que van las VEINTIUNA de las tres estaciones del Atlantico norte:
 * Shannon (Irlanda), Canarias (que ademas es de casa) y Reykjavik. Doce por
 * debajo de 10 MHz para la noche y nueve por encima para el dia, que es
 * justo el reparto que hace falta.
 *
 * Los numeros salen de hfdl_systable_data.inc, generado de la tabla de
 * dumphfdl - no de una lista copiada de un foro. Si algun dia hace falta
 * otra estacion, esta ahi entera.
 *
 * EL GRUPO es la banda horaria y no la estacion: cuando buscas HFDL lo
 * primero que decides es "es de dia o de noche", y la estacion viene
 * despues. Por eso las paginas se cortan por ahi.
 */
typedef struct {
    uint32_t    khz;
    const char *estacion;
    uint8_t     noche;   /* 1 = por debajo de 10 MHz */
} hfdl_canal_t;

static const hfdl_canal_t k_hfdl_canales[] = {
    /* --- noche: por debajo de 10 MHz --- */
    { 2998,  "Shannon",   1U },
    { 3455,  "Shannon",   1U },
    { 3900,  "Reykjavik", 1U },
    { 5547,  "Shannon",   1U },
    { 5720,  "Reykjavik", 1U },
    { 6529,  "Canarias",  1U },
    { 6532,  "Shannon",   1U },
    { 6712,  "Reykjavik", 1U },
    { 8843,  "Shannon",   1U },
    { 8942,  "Shannon",   1U },
    { 8948,  "Canarias",  1U },
    { 8977,  "Reykjavik", 1U },
    /* --- dia: por encima de 10 MHz --- */
    { 10081, "Shannon",   0U },
    { 11184, "Reykjavik", 0U },
    { 11348, "Canarias",  0U },
    { 11384, "Shannon",   0U },
    { 13303, "Canarias",  0U },
    { 15025, "Reykjavik", 0U },
    { 17928, "Canarias",  0U },
    { 17985, "Reykjavik", 0U },
    { 21955, "Canarias",  0U }
};
#define HFDL_CANAL_COUNT (sizeof(k_hfdl_canales) / sizeof(k_hfdl_canales[0]))

/* El texto de cada celda: "13.303" arriba y la estacion debajo. Se guarda
 * porque ui_grid no copia, solo apunta. */
static char s_hfdl_khz_txt[UIG_CELLS][10];

/*
 * CUANTAS TRAMAS BUENAS HA DADO CADA CANAL. 25/09/2026.
 *
 * *** Nace de ver al dueno apuntando a mano: "he cazado un oaci", "otro",
 * "desde reykiavik", "15.025". Eso es un mapa de que frecuencia va a que
 * hora, y lo estaba construyendo el a base de acordarse. ***
 *
 * Las listas generales de HFDL valen para empezar, pero lo que de verdad
 * sirve es lo que TU antena oye desde TU sitio: una frecuencia excelente
 * sobre el papel puede no llegarte nunca, y otra mediocre darte tramas todas
 * las tardes. Ese dato solo lo tiene la radio.
 *
 * Asi que se cuentan las buenas por canal y salen en la rejilla, debajo de la
 * estacion. Cuarenta y dos bytes de RAM para que elegir frecuencia deje de
 * ser a ciegas.
 *
 * NO SE GUARDAN entre arranques, y es a proposito: la propagacion de hace
 * tres dias no dice nada de la de ahora, y un numero viejo con aspecto de
 * dato fresco es peor que ninguno. Se llena solo en un rato de escucha.
 */
static uint16_t s_hfdl_aciertos[HFDL_CANAL_COUNT];
static char     s_hfdl_ok_txt[UIG_CELLS][12];

/*
 * LAS FRECUENCIAS DE FT8 - 27/09/2026.
 *
 * *** El dueno: "hay que poner un boton de frec en el ft8 igual que el del
 * hfdl". ***
 *
 * No salen de mi memoria: comprobadas el 27/09/2026 contra sigidwiki. Las
 * de HFDL salieron de la tabla del propio proyecto y estas no tenian de
 * donde salir, asi que se miraron antes de escribirlas - que ya me paso
 * una vez dar de memoria tres frecuencias del Pacifico a alguien que
 * escucha desde Espana.
 *
 * Son las nueve de onda corta. No se ponen los 50,313 de 6 metros: este
 * receptor llega a 30 MHz.
 */
typedef struct { uint32_t khz; const char *banda; } ft8_canal_t;

static const ft8_canal_t k_ft8_canales[] = {
    {  1840UL, "160 m" },
    {  3573UL,  "80 m" },
    {  7074UL,  "40 m" },
    { 10136UL,  "30 m" },
    { 14074UL,  "20 m" },
    { 18100UL,  "17 m" },
    { 21074UL,  "15 m" },
    { 24915UL,  "12 m" },
    { 28074UL,  "10 m" },
};
#define FT8_CANAL_COUNT (sizeof(k_ft8_canales) / sizeof(k_ft8_canales[0]))

static char s_ft8_frec_txt[12];

/* El mismo formato que el de HFDL: kHz con el punto de los miles. */
static const char *ft8_frec_rotulo(void)
{
    uint32_t khz = (uint32_t)(s_tune_hz / 1000U);
    uint8_t  i = 0U;

    if (khz >= 1000U) {
        aj_u2s(s_ft8_frec_txt, khz / 1000U);
        while (s_ft8_frec_txt[i] != '\0') { i++; }
        s_ft8_frec_txt[i++] = '.';
        s_ft8_frec_txt[i++] = (char)('0' + ((khz / 100U) % 10U));
        s_ft8_frec_txt[i++] = (char)('0' + ((khz / 10U) % 10U));
        s_ft8_frec_txt[i++] = (char)('0' + (khz % 10U));
        s_ft8_frec_txt[i] = '\0';
    } else {
        aj_u2s(s_ft8_frec_txt, khz);
    }
    return s_ft8_frec_txt;
}

static void ft8_frec_siguiente(void)
{
    uint32_t khz = (uint32_t)(s_tune_hz / 1000U);
    uint16_t i, sig = 0U;

    for (i = 0U; i < (uint16_t)FT8_CANAL_COUNT; i++) {
        if (k_ft8_canales[i].khz == khz) {
            sig = (uint16_t)((i + 1U) % FT8_CANAL_COUNT);
            break;
        }
    }
    /* Si no estas en ninguna de la lista, el primer toque te mete por el
     * principio en vez de no hacer nada - igual que en HFDL. */
    s_tune_hz = (uint32_t)(k_ft8_canales[sig].khz * 1000U);
    apply_lo_tune(s_tune_hz);
    top_sync();
    ui_top_draw(&s_top);
}

/*
 * LAS FRECUENCIAS DE JTTY - 28/09/2026.
 *
 * *** El dueño: "jtty tendra igual que ft8, una lista de frecuencias". ***
 *
 * Y la tiene. Cuando escribi el modo dije que no habia frecuencias
 * asentadas que hubiera podido comprobar, y por eso no puse boton. Lo que
 * pasa es que no las busque donde estaban: **las trae el propio WSJT-X**,
 * en models/FrequencyList.cpp de la v3.2.0-rc1, con su comentario al lado
 * explicando de donde sale cada una.
 *
 * Son estas nueve, copiadas de ahi y no deducidas:
 *
 *     1.838   la frecuencia de RTTY de siempre
 *     3.575   se queda por debajo del tramo estrecho de la Region 3 incluso
 *             con los 1.500 Hz de audio sumados
 *     7.090   la de RTTY + 10 kHz
 *    10.140   la de RTTY; el borde de banda (10.150) no deja sitio para los
 *             10 kHz de costumbre
 *    14.090   la de RTTY + 10 kHz
 *    18.100   la de RTTY; +10 kHz sacaria la señal del tramo de datos de
 *             EE.UU. y la pondria encima de las balizas del NCDXF
 *    21.090   la de RTTY + 10 kHz
 *    24.920   la de RTTY; el tramo estrecho (24.920-24.925) no deja sitio
 *    28.090   la de RTTY + 10 kHz
 *
 * SON DIALES, como las de FT8: se sintoniza esto en USB y la señal aparece
 * alrededor de 1.500 Hz de audio, que es justo donde jtty_rx.c pone el
 * centro de su ventana de busqueda. No hay que sumar nada.
 *
 * Y CUATRO DE LAS NUEVE CAEN ENCIMA DE FT8. HAY QUE SABERLO.
 *
 * *** Por el dueño del proyecto: "me has puesto 18.100 como frecuencia de
 * jtty?" ... "pero si 18.100 es frecuencia de ft8". ***
 *
 * Tiene razon en el hecho, y la tabla tambien: en el MISMO fichero de
 * WSJT-X, cuatro renglones mas arriba, esta `{18100000, Modes::FT8}`. Los
 * dos modos comparten dial en 17 m porque el sitio de RTTY es ese y subir
 * 10 kHz sacaria la señal del tramo de datos de EE.UU. Lo dice el
 * comentario de arriba, que es el de ellos.
 *
 * Contra la lista de FT8 de esta misma radio, las coincidencias son:
 *
 *     18.100   JTTY y FT8, los dos como frecuencia PRINCIPAL de 17 m
 *     14.090   JTTY, y la de FT8 para expediciones
 *     21.090   JTTY, y 21.091 de FT8 para expediciones -1 kHz-
 *     28.090   JTTY, y 28.091 de FT8 para expediciones -1 kHz-
 *
 * La tabla NO se toca por eso: es la del programa que define el modo, y
 * cambiarla a mi criterio seria inventarme frecuencias, que es exactamente
 * lo que no quise hacer cuando no puse boton. Pero se escribe aqui porque
 * en el aire se nota: en 17 m se van a oir los dos, y lo que suene mas
 * fuerte sera FT8. Si en 18.100 no sale nada en JTTY, el modo no esta
 * roto - es que ahi vive otro-.
 */
typedef struct { uint32_t khz; const char *banda; } jtty_canal_t;

static const jtty_canal_t k_jtty_canales[] = {
    {  1838UL, "160 m" },
    {  3575UL,  "80 m" },
    {  7090UL,  "40 m" },
    { 10140UL,  "30 m" },
    { 14090UL,  "20 m" },
    { 18100UL,  "17 m" },
    { 21090UL,  "15 m" },
    { 24920UL,  "12 m" },
    { 28090UL,  "10 m" },
};
#define JTTY_CANAL_COUNT (sizeof(k_jtty_canales) / sizeof(k_jtty_canales[0]))

static char s_jtty_frec_txt[12];

/* El mismo formato que FT8 y HFDL: kHz con punto de millar. */
static const char *jtty_frec_rotulo(void)
{
    uint32_t khz = (uint32_t)(s_tune_hz / 1000U);
    uint8_t  i = 0U;

    if (khz >= 1000U) {
        aj_u2s(s_jtty_frec_txt, khz / 1000U);
        while (s_jtty_frec_txt[i] != '\0') { i++; }
        s_jtty_frec_txt[i++] = '.';
        s_jtty_frec_txt[i++] = (char)('0' + ((khz / 100U) % 10U));
        s_jtty_frec_txt[i++] = (char)('0' + ((khz / 10U) % 10U));
        s_jtty_frec_txt[i++] = (char)('0' + (khz % 10U));
        s_jtty_frec_txt[i] = '\0';
    } else {
        aj_u2s(s_jtty_frec_txt, khz);
    }
    return s_jtty_frec_txt;
}

static void jtty_frec_siguiente(void)
{
    uint32_t khz = (uint32_t)(s_tune_hz / 1000U);
    uint16_t i, sig = 0U;

    for (i = 0U; i < (uint16_t)JTTY_CANAL_COUNT; i++) {
        if (k_jtty_canales[i].khz == khz) {
            sig = (uint16_t)((i + 1U) % JTTY_CANAL_COUNT);
            break;
        }
    }
    /* Si no estas en ninguna, el primer toque te mete por el principio en
     * vez de no hacer nada - igual que en FT8 y HFDL. */
    s_tune_hz = (uint32_t)(k_jtty_canales[sig].khz * 1000U);
    apply_lo_tune(s_tune_hz);
    top_sync();
    ui_top_draw(&s_top);
}

/*
 * WSPR: LAS FRECUENCIAS DE SINTONIA, Y POR QUE NO SON LAS DE LAS LISTAS.
 *
 * WSPR se transmite en una ventana de 200 Hz de ancho. Lo que publican las
 * listas -14.095,600 y demas- no es el centro de esa ventana sino la
 * PORTADORA que hay que poner en el dial en USB, porque la ventana cae
 * unos 1.500 Hz por encima. Y 1.500 Hz es justo donde nuestro receptor la
 * busca (WSPR_RX_CENTRO_HZ), asi que la cuenta ya esta hecha: estas son
 * las de la lista, tal cual, y van directas a s_tune_hz.
 *
 * EN HERCIOS Y NO EN KILOHERCIOS, que es lo que hace k_ft8_canales[]. Las
 * de FT8 son kilohercios redondos (7.074, 14.074...) y las de WSPR no:
 * 14.095,6 tiene una cifra detras de la coma. Guardarlas en kHz obligaria
 * a redondear, y redondear 600 Hz en una ventana de 200 de ancho es
 * quedarse fuera de la ventana entera.
 *
 * La de 60 m (5.287,200) esta en la lista porque el modo existe ahi, pero
 * en Espana esa banda no es de aficionado: sirve para escuchar, no para
 * transmitir. Esto solo recibe, asi que no hay nada que decidir.
 */
typedef struct { uint32_t hz; const char *banda; } wspr_canal_t;

static const wspr_canal_t k_wspr_canales[] = {
    {  1836600UL, "160 m" },
    {  3568600UL,  "80 m" },
    {  5287200UL,  "60 m" },
    {  7038600UL,  "40 m" },
    { 10138700UL,  "30 m" },
    { 14095600UL,  "20 m" },
    { 18104600UL,  "17 m" },
    { 21094600UL,  "15 m" },
    { 24924600UL,  "12 m" },
    { 28124600UL,  "10 m" },
};
#define WSPR_CANAL_COUNT (sizeof(k_wspr_canales) / sizeof(k_wspr_canales[0]))

static char s_wspr_frec_txt[12];

/*
 * "14.095,6": kilohercios con punto de millar y UNA cifra decimal.
 *
 * Los de HFDL y FT8 ensenan kilohercios enteros porque sus canales lo son.
 * Aqui la cifra de detras de la coma no es adorno: es la que distingue
 * 14.095,6 de 14.095,0, y esos 600 Hz son tres veces el ancho de la
 * ventana entera de WSPR.
 */
static const char *wspr_frec_rotulo(void)
{
    uint32_t khz = (uint32_t)(s_tune_hz / 1000U);
    uint32_t dec = (uint32_t)((s_tune_hz % 1000U) / 100U);
    uint8_t  i = 0U;

    if (khz >= 1000U) {
        aj_u2s(s_wspr_frec_txt, khz / 1000U);
        while (s_wspr_frec_txt[i] != '\0') { i++; }
        s_wspr_frec_txt[i++] = '.';
        s_wspr_frec_txt[i++] = (char)('0' + ((khz / 100U) % 10U));
        s_wspr_frec_txt[i++] = (char)('0' + ((khz / 10U) % 10U));
        s_wspr_frec_txt[i++] = (char)('0' + (khz % 10U));
    } else {
        aj_u2s(s_wspr_frec_txt, khz);
        while (s_wspr_frec_txt[i] != '\0') { i++; }
    }
    s_wspr_frec_txt[i++] = ',';
    s_wspr_frec_txt[i++] = (char)('0' + dec);
    s_wspr_frec_txt[i] = '\0';
    return s_wspr_frec_txt;
}

/*
 * AIS: LOS DOS CANALES, Y NADA MAS.
 *
 * AIS no tiene "bandas": son dos frecuencias fijas en todo el mundo,
 * 161,975 y 162,025 MHz, separadas 50 kHz. Los barcos alternan entre las
 * dos ranura a ranura, asi que en cualquiera de ellas se ve alrededor de la
 * mitad del trafico.
 *
 * OIR LAS DOS A LA VEZ seria lo suyo y no se puede: la cadena de FI entrega
 * 96 kHz complejos, o sea +-48 kHz, y los dos canales estan a 50 kHz uno de
 * otro. Con el desplazamiento de FI baja que lleva la cadena
 * (DEMOD_IF_OFFSET_HZ) el segundo se sale de la ventana. Queda dicho por si
 * algun dia se sube la tasa: con 192 kHz entrarian los dos y se podrian
 * demodular en paralelo, que es lo que hace un receptor de AIS de verdad.
 */
typedef struct { uint32_t khz; const char *canal; } ais_canal_t;

static const ais_canal_t k_ais_canales[] = {
    { 161975UL, "A" },
    { 162025UL, "B" },
};
#define AIS_CANAL_COUNT (sizeof(k_ais_canales) / sizeof(k_ais_canales[0]))

static char s_ais_frec_txt[12];

/* El mismo formato que HFDL y FT8: kilohercios con punto de millar. */
static const char *ais_frec_rotulo(void)
{
    uint32_t khz = (uint32_t)(s_tune_hz / 1000U);
    uint8_t  i = 0U;

    aj_u2s(s_ais_frec_txt, khz / 1000U);
    while (s_ais_frec_txt[i] != '\0') { i++; }
    s_ais_frec_txt[i++] = '.';
    s_ais_frec_txt[i++] = (char)('0' + ((khz / 100U) % 10U));
    s_ais_frec_txt[i++] = (char)('0' + ((khz / 10U) % 10U));
    s_ais_frec_txt[i++] = (char)('0' + (khz % 10U));
    s_ais_frec_txt[i] = '\0';
    return s_ais_frec_txt;
}

static void ais_frec_siguiente(void)
{
    uint32_t khz = (uint32_t)(s_tune_hz / 1000U);
    uint16_t i, sig = 0U;

    for (i = 0U; i < (uint16_t)AIS_CANAL_COUNT; i++) {
        if (k_ais_canales[i].khz == khz) {
            sig = (uint16_t)((i + 1U) % AIS_CANAL_COUNT);
            break;
        }
    }
    s_tune_hz = (uint32_t)(k_ais_canales[sig].khz * 1000U);
    ais_modo_canal_pon((uint8_t)sig);
    apply_lo_tune(s_tune_hz);
    top_sync();
    ui_top_draw(&s_top);
}

static void wspr_frec_siguiente(void)
{
    uint16_t i, sig = 0U;

    for (i = 0U; i < (uint16_t)WSPR_CANAL_COUNT; i++) {
        if (k_wspr_canales[i].hz == s_tune_hz) {
            sig = (uint16_t)((i + 1U) % WSPR_CANAL_COUNT);
            break;
        }
    }
    /* Si no estas en ninguna de la lista, el primer toque te mete por el
     * principio en vez de no hacer nada - igual que en FT8 y HFDL. */
    s_tune_hz = k_wspr_canales[sig].hz;
    apply_lo_tune(s_tune_hz);
    top_sync();
    ui_top_draw(&s_top);
}

/*
 * Una linea de FT8 empieza por CQ?
 *
 * El mensaje es el QUINTO campo -Hora, Hz, dB, DT, Mensaje-, asi que hay
 * que saltarse cuatro tabuladores. Mirar al principio del renglon miraria
 * la hora.
 *
 * Se exige "CQ" y detras algo que no sea letra ni numero: hay indicativos
 * que empiezan por CQ y marcarlos seria decir que se les puede contestar
 * cuando no.
 */
static uint8_t es_cq(const char *l)
{
    uint8_t tabs = 0U;

    if (l == (const char *)0) { return 0U; }
    while (*l != '\0' && tabs < 4U) {
        if (*l == '\t') { tabs++; }
        l++;
    }
    if (tabs < 4U) { return 0U; }
    while (*l == ' ') { l++; }
    if (l[0] != 'C' || l[1] != 'Q') { return 0U; }
    return (uint8_t)((l[2] == ' ') || (l[2] == '\t') || (l[2] == '\0'));
}

/* Que canal de la tabla es el que esta puesto, o -1 si es otra frecuencia. */
static int16_t hfdl_canal_actual(void)
{
    uint32_t khz = (uint32_t)(s_tune_hz / 1000U);
    uint16_t i;

    for (i = 0U; i < (uint16_t)HFDL_CANAL_COUNT; i++) {
        if (k_hfdl_canales[i].khz == khz) { return (int16_t)i; }
    }
    return -1;
}

/*
 * EL BOTON QUE RECORRE LA LISTA. 25/09/2026.
 *
 * *** El dueno: "el boton de frec te saca la lista y tu eliges, lo cual
 * esta bien, pero si quieres ir probando todas sin muchos clicks es
 * necesario otro boton". ***
 *
 * El rotulo es la frecuencia en la que estas, en kHz con el punto de los
 * miles ("11.184"), y cada toque salta a la siguiente de k_hfdl_canales[].
 * Al llegar al final vuelve a empezar.
 *
 * SI ESTAS EN UNA FRECUENCIA QUE NO ESTA EN LA LISTA -porque la has metido
 * a mano- el rotulo la ensena igual y el primer toque te mete en la lista
 * por el principio, en vez de no hacer nada. Un boton que no responde
 * porque estas "fuera" es un boton que parece roto.
 */
static char s_hfdl_frec_txt[12];

static const char *hfdl_frec_rotulo(void)
{
    uint32_t khz = (uint32_t)(s_tune_hz / 1000U);
    uint8_t  i = 0U;

    if (khz >= 1000U) {
        aj_u2s(s_hfdl_frec_txt, khz / 1000U);
        while (s_hfdl_frec_txt[i] != '\0') { i++; }
        s_hfdl_frec_txt[i++] = '.';
        s_hfdl_frec_txt[i++] = (char)('0' + ((khz / 100U) % 10U));
        s_hfdl_frec_txt[i++] = (char)('0' + ((khz / 10U) % 10U));
        s_hfdl_frec_txt[i++] = (char)('0' + (khz % 10U));
        s_hfdl_frec_txt[i] = '\0';
    } else {
        aj_u2s(s_hfdl_frec_txt, khz);
    }
    return s_hfdl_frec_txt;
}

static void hfdl_frec_siguiente(void)
{
    int16_t act = hfdl_canal_actual();
    uint16_t sig = (act < 0) ? 0U
                             : (uint16_t)(((uint16_t)act + 1U) % HFDL_CANAL_COUNT);

    /* Igual que al elegir en la rejilla: se sintoniza y NO se toca el modo.
     * Ya estas en HFDL -este boton solo sale en su panel- y reiniciar el
     * decodificador aqui tiraria la rafaga que estuviera entrando. */
    s_tune_hz = (uint32_t)(k_hfdl_canales[sig].khz * 1000U);
    apply_lo_tune(s_tune_hz);
    /*
     * Y REPINTAR LA FRECUENCIA DE ARRIBA. apply_lo_tune() mueve el
     * oscilador pero no toca el rotulo; en la rejilla eso lo arregla
     * menu_screen_close(), que hace top_sync() al cerrarse. Aqui no se
     * cierra nada, asi que sin esto el boton decia 13.303 y el numero
     * grande seguia diciendo 11.184: dos frecuencias en pantalla que se
     * contradicen, que es justo lo que hace desconfiar del boton.
     */
    top_sync();
    ui_top_draw(&s_top);
}

/*
 * Se llama desde el dibujado del panel, que corre a cada cuadro. Apunta al
 * canal en el que estas las tramas buenas nuevas.
 *
 * Mira el INCREMENTO y no el total porque el total se reinicia al cambiar de
 * modo o al darle a Borrar, y entonces restar daria negativo; y porque al
 * cambiar de frecuencia el contador sigue corriendo, asi que hay que cerrar
 * la cuenta del canal viejo antes de empezar la del nuevo.
 */
static void hfdl_apunta_aciertos(void)
{
    static uint32_t visto;
    static int16_t  canal = -1;
    int16_t ahora = hfdl_canal_actual();
    uint32_t b = hfdl_modo_buenas();

    if (ahora != canal) { canal = ahora; visto = b; return; }
    if (b <= visto)     { visto = b; return; }   /* se reinicio el contador */

    if (canal >= 0 && s_hfdl_aciertos[canal] < 0xFFFFU) {
        uint32_t d = b - visto;
        uint32_t n = (uint32_t)s_hfdl_aciertos[canal] + d;
        s_hfdl_aciertos[canal] = (uint16_t)((n > 0xFFFFU) ? 0xFFFFU : n);
    }
    visto = b;
}

/* Numero de entradas de la pantalla activa. */
static uint16_t grid_total(void)
{
    switch (s_grid_pant) {
    case GRID_AJUSTES: return (uint16_t)AJUSTE_COUNT;
    case GRID_BANDAS:  return (uint16_t)BAND_PRESET_COUNT;
    case GRID_PASOS:   return (uint16_t)TUNE_STEP_COUNT;
    case GRID_INFO:    return s_info_avanzado ? (uint16_t)INFO_COUNT
                                             : (uint16_t)INFO_BASICAS;
    case GRID_SSTV:    return (uint16_t)sstv_modos_n();
    case GRID_HFDL:    return (uint16_t)HFDL_CANAL_COUNT;
    default:           return 0U;
    }
}

/* Familia/grupo de la entrada i, o 0 si la pantalla no agrupa. */
static uint8_t grid_grupo(uint16_t i)
{
    switch (s_grid_pant) {
    case GRID_AJUSTES: return k_ajustes[i].pagina;
    case GRID_BANDAS:  return k_band_presets[i].familia;
    /* Por banda horaria, no por estacion - ver k_hfdl_canales[]. */
    case GRID_HFDL:    return (uint8_t)(k_hfdl_canales[i].noche ? 0U : 1U);
    /* Sin esto el reparto seria por aritmetica -doce y las que sobren- y
     * "Flash externa" acabaria en la pagina 1 arrastrando sus 200 ms. Las
     * paginas no mezclan grupos (ver grid_page_info), asi que declarar el
     * grupo es lo que clava el corte en INFO_BASICAS. */
    /* Tres grupos desde el 24/09/2026: las dos paginas estaban llenas (11 y
     * 12 de 12 celdas) y hacian falta los mandos de la pantalla. El reparto
     * en paginas lo calcula grid_page_info() solo a partir de esto. */
    /*
     * CUATRO GRUPOS DESDE EL 25/09/2026. Eran tres y la pagina de la flash
     * externa se lleno: al anadir "Cargar aviones" salto el
     * _Static_assert de abajo, que para eso esta. Se parte en dos por donde
     * el contenido ya se partia solo: mirar el chip y sus ficheros por un
     * lado, y mover megabytes de un sitio a otro por el otro.
     */
    case GRID_INFO:    return (uint8_t)((i < (uint16_t)INFO_BASICAS) ? 0U :
                                        ((i < (uint16_t)INFO_XVOLCADO) ? 1U :
                                         ((i < (uint16_t)INFO_XIPA) ? 2U : 3U)));
    default:           return 0U;
    }
}

static const char *grid_grupo_nombre(uint8_t g)
{
    switch (s_grid_pant) {
    case GRID_AJUSTES: return k_aj_paginas[g][idioma()];
    case GRID_BANDAS:  return k_band_familias[g][idioma()];
    case GRID_PASOS:   return tr("Paso de sintonía", "Tuning step");
    case GRID_INFO:    return (g == 0U) ? tr("Información", "Information")
                                        : ((g == 1U) ? tr("Flash externa", "External flash")
                                        : ((g == 2U) ? tr("Volcar y restaurar", "Dump and restore")
                                                     : tr("Pantalla", "Display")));
    case GRID_SSTV:    return tr("Modo de SSTV", "SSTV mode");
    case GRID_HFDL:    return (g == 0U)
                       ? tr("HFDL de noche (bajo 10 MHz)", "HFDL at night (below 10 MHz)")
                       : tr("HFDL de día (sobre 10 MHz)",  "HFDL by day (above 10 MHz)");
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

    s_grid.nav[0] = tr("Anterior", "Previous");
    s_grid.nav_on[0] = (uint8_t)(s_grid_page > 0U);
    s_grid.nav[1] = tr("Siguiente", "Next");
    s_grid.nav_on[1] = (uint8_t)(s_grid_page + 1U < s_grid.paginas);

    for (i = 0U; i < UIG_CELLS; i++) {
        s_grid.cel[i].l1 = "";
        s_grid.cel[i].l2 = "";
        s_grid.cel[i].l3 = 0;

        if (i >= n) { continue; }

        switch (s_grid_pant) {
        case GRID_AJUSTES: {
            const ajuste_t *a = &k_ajustes[primero + i];
            const char *v = ajuste_valor(a->id, s_aj_val[i]);
            s_grid.cel[i].l1 = a->nombre[idioma()];
            /* Los ajustes que son una accion y no un valor dejan el renglon
             * vacio: la pantalla entera se toca, asi que poner "tocar" en
             * tres celdas solo repite lo que ya vale para todas. */
            s_grid.cel[i].l2 = v ? v : "";
            break;
        }
        case GRID_BANDAS: {
            const band_preset_t *b = &k_band_presets[primero + i];
            uint8_t m;
            s_grid.cel[i].l1 = b->label[idioma()];
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
        case GRID_HFDL: {
            const hfdl_canal_t *c = &k_hfdl_canales[primero + i];
            uint32_t k = c->khz;
            uint8_t  j = 0U;

            /* "13.303" con el punto de los miles, como la frecuencia de
             * arriba: leer "13303" en una rejilla cuesta un segundo mas. */
            s_hfdl_khz_txt[i][j++] = (char)('0' + (k / 10000U) % 10U);
            if (k >= 10000U) {
                s_hfdl_khz_txt[i][j++] = (char)('0' + (k / 1000U) % 10U);
            } else {
                j = 0U;
                s_hfdl_khz_txt[i][j++] = (char)('0' + (k / 1000U) % 10U);
            }
            s_hfdl_khz_txt[i][j++] = '.';
            s_hfdl_khz_txt[i][j++] = (char)('0' + (k / 100U) % 10U);
            s_hfdl_khz_txt[i][j++] = (char)('0' + (k / 10U) % 10U);
            s_hfdl_khz_txt[i][j++] = (char)('0' + k % 10U);
            s_hfdl_khz_txt[i][j] = '\0';

            s_grid.cel[i].l1 = s_hfdl_khz_txt[i];
            s_grid.cel[i].l2 = c->estacion;

            /* Lo que ESTA frecuencia te ha dado hoy. Vacio si nada: un "0"
             * en veinte celdas es ruido, y ademas se lee como "aqui no hay
             * nada" cuando lo que dice es "todavia no has escuchado". */
            if (s_hfdl_aciertos[primero + i] != 0U) {
                uint8_t j = 0U;
                aj_u2s(s_hfdl_ok_txt[i], s_hfdl_aciertos[primero + i]);
                while (s_hfdl_ok_txt[i][j] != '\0') { j++; }
                s_hfdl_ok_txt[i][j++] = ' ';
                s_hfdl_ok_txt[i][j++] = 'o';
                s_hfdl_ok_txt[i][j++] = 'k';
                s_hfdl_ok_txt[i][j] = '\0';
                s_grid.cel[i].l3 = s_hfdl_ok_txt[i];
            } else {
                s_grid.cel[i].l3 = "";
            }
            /* La que esta puesta ahora sale marcada, igual que en Bandas. */
            if ((uint32_t)(s_tune_hz / 1000U) == k) { s_grid.marcada = i; }
            break;
        }
        case GRID_INFO: {
            uint8_t id = (uint8_t)(primero + i);
            s_grid.cel[i].l1 = k_info_nombres[id][idioma()];
            s_grid.cel[i].l2 = info_valor(id, s_info_val[i]);
            break;
        }
        case GRID_SSTV: {
            uint8_t  k = (uint8_t)(primero + i);
            sstv_plan_t pl;
            float linea_s = 0.0f;

            s_grid.cel[i].l1 = sstv_modo_nombre(k);
            /*
             * Lo que de verdad distingue un modo de otro cuando eliges a
             * ojo: cuanto tarda y a que resolucion. El codigo VIS no - ese
             * lo lee la radio, no tu.
             */
            if (sstv_plan(k, &pl) && sstv_modo_tiempos(k, &linea_s, 0, 0, 0)) {
                uint32_t seg = (uint32_t)((linea_s * (float)pl.alto /
                                           (float)pl.lineas_img) + 0.5f);
                uint8_t  j = 0U;
                aj_u2s(s_sstv_v2[i], seg);
                while (s_sstv_v2[i][j] != '\0') { j++; }
                s_sstv_v2[i][j++] = ' '; s_sstv_v2[i][j++] = 's'; s_sstv_v2[i][j] = '\0';
                j = 0U;
                aj_u2s(s_sstv_v3[i], (uint32_t)pl.ancho);
                while (s_sstv_v3[i][j] != '\0') { j++; }
                s_sstv_v3[i][j++] = 'x';
                aj_u2s(&s_sstv_v3[i][j], (uint32_t)pl.alto);
                s_grid.cel[i].l2 = s_sstv_v2[i];
                s_grid.cel[i].l3 = s_sstv_v3[i];
            }
            if (k == s_sstv_modo_idx) { s_grid.marcada = i; }
            break;
        }
        case GRID_PASOS: {
            uint16_t k = (uint16_t)(primero + i);
            s_grid.cel[i].l1 = k_tune_step_labels_ui[k];
            s_grid.cel[i].l2 = k_paso_desc[k][idioma()];
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
    case GRID_PASOS:
        menu_step_preset_callback(0, UI_EVENT_RELEASE, (void *)(uintptr_t)(primero + cel));
        break;
    case GRID_HFDL:
        /*
         * Elegir un canal sintoniza y cierra, como en Bandas. NO se toca el
         * modo: ya estas en HFDL -este boton solo sale en su panel- y
         * cambiarlo aqui apagaria y encenderia el decodificador para nada,
         * perdiendo la rafaga que estuviera entrando.
         */
        s_tune_hz = (uint32_t)(k_hfdl_canales[primero + cel].khz * 1000U);
        apply_lo_tune(s_tune_hz);
        menu_screen_close();
        break;
    case GRID_SSTV:
        /*
         * Elegir un modo no es solo "apuntalo para la proxima": arranca YA
         * con el. Es lo que quieres cuando ves la imagen salir corrida
         * porque engancho el modo equivocado - no hay que parar nada ni
         * volver a tocar "Empezar".
         */
        s_sstv_modo_idx = (uint8_t)(primero + cel);
        sstv_fuerza(s_sstv_modo_idx);
        sstv_panel_reinicia();
        menu_screen_close();
        break;
    case GRID_INFO:
        /* La unica celda de Informacion que HACE algo: arranca el volcado
         * de la zona alta. Las demas son lecturas. */
        {
            uint8_t id = (uint8_t)(primero + cel);
            /*
             * Cinco toques en la version destapan la pagina 2.
             *
             * El repintado va AQUI y no en grid_touch porque alli el
             * ui_grid_draw_one() se hace ANTES de llamar a esta funcion:
             * si me fiara de aquel, la cuenta atras iria un toque por
             * detras de los dedos. Y se repinta la rejilla entera, no la
             * celda, porque al destapar aparece el pie con "Siguiente" y
             * cambia el numero de paginas.
             */
            if (id == (uint8_t)INFO_VERSION) {
                if (!s_info_avanzado) {
                    s_info_toques++;
                    if (s_info_toques >= INFO_TOQUES) {
                        s_info_avanzado = 1U;
                        s_info_toques = 0U;
                    }
                    grid_fill();
                    ui_grid_draw(&s_grid);
                }
                break;
            }
            if (id == (uint8_t)INFO_XVOLCADO)       { volcado_arranca(); }
            else if (id == (uint8_t)INFO_XVOLMICRO) { volcado_arranca_micro(); }
            else if (id == (uint8_t)INFO_XVOLROM)   { volcado_arranca_rom(); }
            else if (id == (uint8_t)INFO_XPRUEBA)   { prueba_arranca(); }
            else if (id == (uint8_t)INFO_XRESTAURA) { restaura_arranca(); }
            else if (id == (uint8_t)INFO_XAVIONES)  { (void)datos_arranca(); }
            else if (id == (uint8_t)INFO_XBORRAF)   { borra_zona_arranca(); }
            else if (id == (uint8_t)INFO_XDFU) {
                if (s_dfu_armado) { dfu_pide_reinicio(); }   /* no vuelve */
                s_dfu_armado = 1U;
            }
            else if (id == (uint8_t)INFO_XIPA)      { ipa_blit_pon(!ipa_blit_hay()); }
            else if (id == (uint8_t)INFO_XDMA)      { lcd_dma_pon(!lcd_dma_hay()); }
            else if (id == (uint8_t)INFO_XMSFRAME) {
                /* 33 es el de siempre (30 fps), 16 son 60. Por debajo de lo
                 * que cuesta dibujar no sirve de nada: el limitador deja de
                 * limitar y lo que sale es lo que de verdad da tiempo. */
                static const uint16_t k_ms[] = { 33U, 28U, 25U, 22U, 20U, 18U, 16U };
                uint8_t j;

                for (j = 0U; j < (uint8_t)((sizeof k_ms / sizeof k_ms[0]) - 1U); j++) {
                    if (k_ms[j] == (uint16_t)s_frame_ms) { break; }
                }
                s_frame_ms = k_ms[(uint8_t)((j + 1U) % (sizeof k_ms / sizeof k_ms[0]))];
            }

            else if (id == (uint8_t)INFO_XBUS) {
                /* A LA MITAD por toque, y al llegar abajo vuelve a lo que
                 * habia al arrancar. Partir por la mitad y no restar de uno
                 * en uno porque no sabemos de que numero salimos -lo puso el
                 * gestor de arranque, ver rm68120_bus_dato()- y podria ser
                 * 39 o 200. Asi son cuatro o cinco toques hasta el limite
                 * salga de donde salga, y la vuelta al valor seguro esta
                 * siempre a un toque mas. */
                /* De uno en uno por abajo, a la mitad por arriba. Empezo
                 * siendo siempre a la mitad y en la radio salto de 5 a 2 -de
                 * 45 ns a 30- y la pantalla se ensucio, saltandose el 4 y el
                 * 3, que son los que podian valer. Cuando quedan pocos
                 * escalones hay que pisarlos todos. */
                uint16_t d = rm68120_bus_dato();

                if (d <= 2U)      { d = rm68120_bus_dato_arranque(); }
                else if (d <= 8U) { d = (uint16_t)(d - 1U); }
                else              { d = (uint16_t)(d / 2U); }
                rm68120_bus_dato_pon(d);
            }
            else { break; }
            grid_fill();
            ui_grid_draw(&s_grid);
        }
        break;
    default: break;
    }
}

static void grid_show(grid_pant_t p)
{
    s_cfg_func = 0U;   /* misma razon que en cfg_show() */
    /* Cinco toques SEGUIDOS. Sin esto, tres toques de hoy y dos de manana
     * destaparian la pagina, que es justo lo que no se quiere de un gesto
     * que esta escondido a proposito. Lo YA destapado no se vuelve a tapar:
     * eso solo lo hace apagar la radio. */
    s_info_toques = 0U;
    s_dfu_armado  = 0U;   /* salir y volver desarma la fila DFU */
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
    } else if (p == GRID_MODO) {
        abierta = (uint8_t)(s_menu_cfg_active && s_cfg_pant == CFG_MODOS);
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
    else if (p == GRID_MODO)   { menu_modes_show(); }
    else                       { grid_show(p); }
}

/* Se conservan por los sitios que abren estas pantallas desde fuera de la
 * barra de acciones (aplicar una banda desde el teclado de frecuencia, por
 * ejemplo). Los botones de la barra usan accion_pantalla(). */
static void menu_bands_show(void) { cfg_show(CFG_BANDAS); }
static void menu_modes_show(void) { cfg_show(CFG_MODOS); }


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

        /* NAVTEX: tercer interruptor independiente, misma regla que los dos
         * de arriba - elegir cualquier otro modo lo apaga sin preguntar. Se
         * arranca de cero cada vez (navtex_start) en vez de dejarlo vivo:
         * el sincronismo de caracter y la paridad de las repeticiones son de
         * la emisora que estabas oyendo, y arrastrarlos a otra es
         * exactamente una pantalla que miente. */
        if (k_demod_modes[idx].navtex) {
            navtex_start(12000.0f);   /* la tasa del audio de banda lateral */
        } else {
            navtex_stop();
        }

        /* Y el cuarto y el quinto: WEFAX y SSTV. Mismo trato. */
        if (k_demod_modes[idx].fax) {
            wefax_start(12000.0f);
            fax_panel_reinicia();
        } else {
            wefax_stop();
        }
        if (k_demod_modes[idx].sstv) {
            sstv_start(12000.0f);
            sstv_panel_reinicia();
        } else {
            sstv_stop();
        }

        /*
         * Y PSK31: interruptor propio, como el NAVTEX y por la misma regla
         * -elegir cualquier otro modo lo apaga sin preguntar-. Se arranca de
         * cero cada vez en vez de dejarlo vivo: el reloj de simbolo, la
         * sintonia fina y el caracter a medio formar son de la emision que
         * estabas oyendo, y arrastrarlos a otra es una pantalla que miente.
         *
         * NO va en el grupo de los cinco de abajo (FT8/HFDL/WSPR/AIS/ALE):
         * ese grupo existe porque se reparten los bytes de la cascada, y
         * PSK31 no toca esa memoria.
         */
        if (k_demod_modes[idx].psk31) {
            psk31_start(12000.0f);   /* la tasa del audio de banda lateral */
        } else {
            psk31_stop();
        }

        /*
         * Y el mapa se apaga al cambiar de modo. No es solo higiene: lo
         * que hay pintado son las estaciones del modo ANTERIOR, y dejarlo
         * puesto enseñaria barcos mientras se escucha FT8.
         */
        s_ft8_mapa = 0U;

        /*
         * Y el sexto: APRS. La tasa NO es la de los otros: su audio sale del
         * discriminador de FM, que trabaja a la tasa de radiofrecuencia
         * (48 o 96 kHz segun el ajuste), no a los 12 kHz del camino de banda
         * lateral. Se pregunta en vez de suponerla, que es justo el tipo de
         * numero que un dia cambia en un sitio y no en el otro.
         */
        if (k_demod_modes[idx].ax25) {
            ax25_start(demod_am_get_active_fs_hz());
        } else {
            ax25_stop();
        }

        /*
         * Y el septimo: FT8. Tasa de banda lateral como los cinco primeros.
         *
         * Este apagado NO es opcional ni por higiene, como lo son los de
         * arriba: FT8 y la cascada comparten los mismos bytes de memoria
         * (ver ft8_shared_ram.h). Mientras FT8 esta encendido, lo que hay en
         * el buffer de la cascada son magnitudes, no pixeles; si se saliera
         * de FT8 sin llamar a ft8_modo_stop(), la cascada volveria a pintar
         * esos bytes como si fueran colores. ft8_modo_stop() la reinicia.
         */
        /*
         * PRIMERO SE PARA, DESPUES SE ARRANCA. Los dos, en ese orden.
         *
         * 27/09/2026. El orden de antes -cada modo con su if/else- estaba
         * bien en un sentido y mal en el otro, y el comentario de abajo
         * solo miraba el bueno:
         *
         *   FT8 -> HFDL   ft8_modo_stop() va antes, correcto.
         *   HFDL -> FT8   ft8_modo_start() iba PRIMERO -coge el prestamo
         *                 de la cascada- y hfdl_modo_stop() despues,
         *                 que llama a waterfall_presta(0) y SUELTA el
         *                 prestamo que FT8 acababa de coger.
         *
         * A partir de ahi waterfall_prestada() dice 0 mientras FT8 tiene
         * la RAM, o sea que la cascada vuelve a pintar como pixeles los
         * bytes del decodificador -que es exactamente lo que el aviso de
         * aqui arriba dice que no puede pasar-, y ademas el siguiente
         * hfdl_modo_start() se cree que esta libre y se la quita a un FT8
         * vivo.
         *
         * Y son modos CONTIGUOS en k_demod_modes[], o sea un paso de
         * rueda.
         */
        /*
         * WSPR entra en el mismo grupo desde el 27/09/2026: son TRES los que
         * se reparten los bytes de la cascada, no dos. La regla no cambia
         * -parar los tres, arrancar uno- y por eso esta escrita asi y no con
         * if/else encadenados: con dos, un if/else casi funcionaba; con tres
         * las parejas que hay que probar pasan de dos a seis, y el fallo que
         * esto arreglo (HFDL -> FT8 soltaba el prestamo que FT8 acababa de
         * coger) ya demostro que "casi" no vale.
         */
        /*
         * LOS CUATRO SE PARAN ANTES DE QUE ARRANQUE NINGUNO. Sin
         * excepciones y sin colar uno en medio.
         *
         * AIS entro en este grupo el 27/09/2026, cuando su tabla de barcos
         * paso a vivir tambien en la memoria de la cascada (no cabia en
         * SRAM propia; ver ais_modo.c). La primera version lo dejaba fuera
         * -paraba AIS DESPUES de arrancar FT8- y eso es EXACTAMENTE el
         * fallo que se arreglo aqui el 27/09 por la manana entre FT8 y
         * HFDL: ais_modo_stop() llama a hfdl_ram_suelta(), o sea que
         * soltaba el prestamo que FT8 acababa de coger, y a partir de ahi
         * la cascada volvia a pintar como pixeles los bytes del
         * decodificador.
         *
         * El mismo fallo, en el mismo sitio, con un modo mas, tres horas
         * despues. Por eso ahora es una lista de cuatro paradas seguidas de
         * una lista de cuatro arranques, y no cuatro parejas.
         */
        if (!k_demod_modes[idx].ft8)  { ft8_modo_stop(); }
        if (!k_demod_modes[idx].hfdl) { hfdl_modo_stop(); }
        if (!k_demod_modes[idx].wspr) { wspr_modo_stop(); }
        if (!k_demod_modes[idx].ais)  { ais_modo_stop(); }
        if (!k_demod_modes[idx].ale)  { ale_modo_stop(); }
        if (!k_demod_modes[idx].jtty) { jtty_modo_stop(); }

        /*
         * EL FILTRO DE CANAL VA ANTES QUE EL RECEPTOR, y eso no es cosmetico:
         * asi las primeras muestras que le llegan a ais_rx.c ya vienen por
         * el filtro ancho. Arrancando al reves, el preambulo de la primera
         * rafaga pasaria por el filtro estrecho de NFM y el lazo de reloj se
         * engancharia con el ojo medio cerrado.
         */
        demod_am_ais_chf(k_demod_modes[idx].ais);

        if (k_demod_modes[idx].ft8)   { ft8_modo_start(12000.0f); }
        if (k_demod_modes[idx].ais)   { (void)ais_modo_start(); }
        if (k_demod_modes[idx].ale)   { (void)ale_modo_start(); }
        if (k_demod_modes[idx].jtty)  { (void)jtty_modo_start(); }

        /*
         * Y el octavo: HFDL. Mismo aviso que FT8 justo arriba y por la misma
         * razon - comparte los bytes de la cascada, asi que salir sin parar
         * dejaria a la cascada pintando simbolos como si fueran colores.
         *
         * El orden importa: ft8_modo_stop() va ANTES (esta arriba), porque
         * hfdl_modo_start() se niega a arrancar si la cascada sigue prestada.
         */
        if (k_demod_modes[idx].hfdl)  { (void)hfdl_modo_start(); }

        /*
         * Y el noveno: WSPR. Mismo aviso que los dos de arriba y mismo
         * motivo.
         *
         * wspr_modo_start() puede devolver 0 por dos cosas: que la cascada
         * siga prestada, o que el hueco no llegue para el espectrograma.
         * Las dos estan cerradas por arriba: la primera porque los tres
         * stop() de aqui encima han corrido ya, y la segunda porque el
         * _Static_assert de wspr_modo.c no deja compilar un espectrograma
         * que no quepa. Si aun asi devolviera 0, lo que se ve NO es una
         * chapa avisando: es el panel de texto de RTTY, porque digi_sync()
         * pregunta por wspr_modo_activo() y ese dice que no. Queda dicho
         * para que nadie busque un mensaje de error que no existe.
         */
        if (k_demod_modes[idx].wspr)  { (void)wspr_modo_start(); }

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
/*
 * PONER EL RELOJ EN HH:MM. 23/09/2026.
 *
 * El reloj de esta radio no es un RTC: es el tiempo desde el arranque mas un
 * desfase en minutos (ver s_time_offset_min). Poner la hora es calcular ese
 * desfase, y esa cuenta -con su vuelta por medianoche- la necesitan ahora
 * DOS sitios: el teclado de hora, que lleva aqui desde el principio, y la
 * sincronizacion por DCF77. Asi que vive aqui y no dentro del callback del
 * teclado: la misma formula escrita dos veces es la misma formula que un dia
 * deja de coincidir en uno de los dos.
 */
static void reloj_poner(uint8_t hh, uint8_t mm)
{
    extern volatile uint32_t g_msticks;

    /* Multiplo exacto de un minuto: la fase dentro del minuto se queda como
     * estaba, porque quien teclea HH:MM no ha dicho nada del segundo. */
    s_time_offset_ms = reloj_desfase_minuto(g_msticks, hh, mm);
    s_hora_anclada = 0U;

    /* Al RTC tambien, y con el segundo a cero: quien teclea HH:MM no ha
     * dicho nada del segundo, y cero es tan bueno como cualquier otro y
     * ademas se ve venir. */
    rtc_pon_hora(hh, mm, 0U);

    debug_print_dec("clock: set, offset (ms)", s_time_offset_ms);
    time_display_draw(); /* top bar - instant feedback, don't wait for the next periodic tick */
}

/*
 * PONER EL RELOJ CON EL SEGUNDO. 24/09/2026.
 *
 * mins_locales es la hora local en minutos del dia, y atraso_ms es cuantos
 * milisegundos hace que paso su borde de minuto. O sea: "hace atraso_ms
 * milisegundos eran exactamente las mins_locales en punto".
 *
 * DE DONDE SALE ESE BORDE. Del RDS. La hora del grupo 4A no trae segundos y
 * no le hacen falta, porque la norma obliga a que el borde de minuto caiga a
 * menos de 0,1 s del FINAL del grupo: el instante en que el grupo termina de
 * llegar ES el cambio de minuto. rds.c anota en que muestra acaba y
 * rds_hora_atraso_ms() dice cuanto hace de eso.
 *
 * LO QUE VALE ESTO, con numeros y no de oidas:
 *
 *   - retardo propio del decodificador:  1,01 ms  (medido en sim/rdstest.c,
 *     prueba 6, contra el instante real en que el transmisor del banco emite
 *     el ultimo bit; entre 0,90 y 1,12 ms, y no se mueve ni con +/-50 ppm de
 *     error de reloj). Es RDS_CT_RETARDO_MS de aqui abajo.
 *
 *   - lo que admite la norma:            +/-100 ms. Esto NO se puede reducir
 *     desde aqui: depende de lo bien que la emisora tenga puesto su codificador.
 *
 * O sea que el error es practicamente todo del que emite, y lo nuestro es
 * ruido al lado. Por eso el retardo se resta aunque sea un milisegundo: no
 * porque cambie el resultado, sino porque deja el numero donde se puede ver y
 * volver a medir.
 *
 * PARA QUE SIRVE. Para que el minuto del reloj cambie CUANDO cambia de
 * verdad, en vez de en un instante cualquiera heredado del arranque. Y de
 * paso, es el requisito que faltaba para pensar en modos que necesitan saber
 * el segundo -el FT8 pide medio segundo largo- aunque eso sea otra historia.
 */
#define RDS_CT_RETARDO_MS 1U   /* medido, ver sim/rdstest.c prueba 6 */

static void reloj_anclar(int16_t mins_locales, uint32_t atraso_ms)
{
    extern volatile uint32_t g_msticks;

    s_time_offset_ms = reloj_desfase_anclado(g_msticks, (int32_t)mins_locales,
                                             atraso_ms + RDS_CT_RETARDO_MS);
    s_hora_anclada = 1U;

    /*
     * Y al RTC, con el segundo SI: esto viene anclado a un borde de minuto de
     * verdad, asi que el segundo se sabe. Es la diferencia entre este camino
     * y el del teclado, y es justo para lo que se hizo el anclaje.
     *
     * El desfase ya esta calculado arriba, asi que se lee de ahi en vez de
     * repetir la aritmetica: reloj.c es quien sabe de vueltas por medianoche
     * y de minutos fuera de rango, y esa cuenta esta medida en el banco.
     */
    {
        uint32_t ms = reloj_ms_del_dia(g_msticks, s_time_offset_ms);
        rtc_pon_hora((uint8_t)(ms / 3600000UL),
                     (uint8_t)((ms / 60000UL) % 60UL),
                     (uint8_t)((ms / 1000UL) % 60UL));
    }

    debug_print_dec("clock: anclado, atraso (ms)", atraso_ms);
    time_display_draw();
}

static void menu_time_keypad_accept_callback(void *widget, ui_event_t event, void *user_data)
{
    (void)widget;
    (void)user_data;
    if (event == UI_EVENT_RELEASE && s_time_entry_digits == TIME_ENTRY_MAX_DIGITS) {
        reloj_poner((uint8_t)(s_time_entry_value / 100U),
                    (uint8_t)(s_time_entry_value % 100U));
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
static const texto_t k_kbd_freq_rot[UIK_KEYS] = {
    T("1","1"), T("2","2"), T("3","3"), T("Borrar","Delete"),
    T("4","4"), T("5","5"), T("6","6"), T("Vaciar","Clear"),
    T("7","7"), T("8","8"), T("9","9"), T("Volver","Back"),
    T(",",","), T("0","0"), T("kHz","kHz"), T("MHz","MHz"),
};
static const uint8_t k_kbd_freq_tipo[UIK_KEYS] = {
    UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_BORRA,
    UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_BORRA,
    UIK_DIGITO, UIK_DIGITO, UIK_DIGITO, UIK_VUELVE,
    UIK_DIGITO, UIK_DIGITO, UIK_ACEPTA, UIK_ACEPTA,
};
static const texto_t k_kbd_hora_rot[UIK_KEYS] = {
    T("1","1"), T("2","2"), T("3","3"), T("Borrar","Delete"),
    T("4","4"), T("5","5"), T("6","6"), T("Vaciar","Clear"),
    T("7","7"), T("8","8"), T("9","9"), T("Volver","Back"),
    T(0,0),     T("0","0"), T("Poner en hora","Set the clock"), T(0,0),
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
        s_kbd.titulo = tr("Frecuencia", "Frequency");
        s_kbd.pista  = tr("teclea y elige la unidad", "type it and pick the unit");
        for (i = 0U; i < UIK_KEYS; i++) {
            s_kbd.tecla[i] = k_kbd_freq_rot[i][idioma()];
            s_kbd.tipo[i]  = k_kbd_freq_tipo[i];
        }
    } else {
        s_time_entry_value = 0U;
        s_time_entry_digits = 0U;
        s_kbd.titulo = tr("Hora", "Time");
        s_kbd.pista  = "cuatro cifras seguidas, HHMM";
        for (i = 0U; i < UIK_KEYS; i++) {
            s_kbd.tecla[i] = k_kbd_hora_rot[i][idioma()];
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
    /* La fila de ajustes rapidos no la tiene casi ninguna pantalla: se
     * apaga aqui y la enciende quien la quiera, para que no se herede de
     * la anterior. */
    s_det.preset[0] = 0;
    s_det.preset[1] = 0;
    s_det.preset[2] = 0;
    s_det.preset_on = -1;
    s_det.pressed = s_det_press;

    switch (s_menu_detail_target) {
    case ENCODER_TARGET_VOLUME:
        s_det.titulo = tr("Volumen", "Volume");
        s_det.valor  = ajuste_valor(AJ_VOL, s_det_val);
        break;
    case ENCODER_TARGET_SQUELCH:
        s_det.titulo = tr("Silenciador", "Squelch");
        s_det.valor  = ajuste_valor(AJ_SQL, s_det_val);
        break;
    case ENCODER_TARGET_PGA:
        s_det.titulo = tr("Ganancia de entrada", "Input gain");
        s_det.valor  = ajuste_valor(AJ_PGA, s_det_val);
        s_det.pie    = tr("PGA del códec, 0 a 47,5 dB", "codec PGA, 0 to 47,5 dB");
        break;
    case ENCODER_TARGET_NR:
        s_det.titulo = tr("Reducción de ruido", "Noise reduction");
        s_det.valor  = ajuste_valor(AJ_NR, s_det_val);
        break;
    case ENCODER_TARGET_BACKLIGHT:
        s_det.titulo = tr("Brillo", "Brightness");
        s_det.valor  = ajuste_valor(AJ_BRILLO, s_det_val);
        break;
    case ENCODER_TARGET_SMOOTH:
        s_det.titulo = tr("Suavizado", "Smoothing");
        s_det.valor  = ajuste_valor(AJ_SUAVIZ, s_det_val);
        s_det.pie    = tr("cuánto se promedia entre fotogramas",
                          "how much is averaged between frames");
        break;
    case ENCODER_TARGET_RTTY_SHIFT:
        s_det.titulo = tr("Desplazamiento RTTY", "RTTY shift");
        s_det.valor  = ajuste_valor(AJ_RTTY_SHIFT, s_det_val);
        break;
    case ENCODER_TARGET_CW_TONE:
        s_det.titulo = tr("Tono de CW", "CW pitch");
        s_det.valor  = ajuste_valor(AJ_CW_TONO, s_det_val);
        break;
    case ENCODER_TARGET_FILTRO: {
        /* El numero grande es el corte que se esta moviendo, y el pie dice
         * donde queda el otro: sin eso, dos pantallas iguales con numeros
         * distintos no dicen cual estas tocando. Igual que en la escala. */
        uint16_t lo, hi, v;
        uint8_t i = 0U;
        fil_cortes(&lo, &hi);
        v = s_fil_ajusta_hi ? hi : lo;
        top_u2s(s_fil_det, (uint32_t)v);
        while (s_fil_det[i] != '\0') { i++; }
        s_fil_det[i++] = ' '; s_fil_det[i++] = 'H'; s_fil_det[i++] = 'z';
        s_fil_det[i] = '\0';
        if (cw_get_enabled()) {
            /* En CW lo que hay es un paso banda y un solo mando: su
             * ancho. Enseñar dos cortes que no se estan usando seria
             * enseñar un ajuste que no hace nada. */
            top_u2s(s_fil_det, (uint32_t)(demod_am_get_cw_bw_hz() + 0.5f));
            i = 0U;
            while (s_fil_det[i] != '\0') { i++; }
            s_fil_det[i++] = ' '; s_fil_det[i++] = 'H'; s_fil_det[i++] = 'z';
            s_fil_det[i] = '\0';
            s_det.titulo = tr("Filtro de CW: ancho", "CW filter: width");
            s_det.valor  = s_fil_det;
            s_det.alt    = 0;
            s_det.alt_on = 0U;
            for (i = 0U; i < (uint8_t)UID_PRE_N; i++) {
                s_det.preset[i] = k_cw_rapidos[i].rotulo;
            }
            s_det.preset_on = fil_rapido_puesto();
            break;
        }
        s_det.titulo = s_fil_ajusta_hi ? tr("Filtro: corte alto", "Filter: high cut")
                                       : tr("Filtro: corte bajo", "Filter: low cut");
        s_det.valor  = s_fil_det;
        s_det.alt    = tr("Cambiar a LO / HI", "Switch to LO / HI");
        s_det.alt_on = s_fil_ajusta_hi;
        for (i = 0U; i < (uint8_t)UID_PRE_N; i++) {
            s_det.preset[i] = k_fil_rapidos[i].rotulo;
        }
        s_det.preset_on = fil_rapido_puesto();
        break;
    }
    case ENCODER_TARGET_SCALE: {
        /* El numero grande es el limite que se esta moviendo, y el pie dice
         * cual es y donde queda el otro: sin eso, dos pantallas identicas con
         * numeros distintos no dicen cual estas tocando. */
        int16_t v = (int16_t)(s_scale_adjust_max ? s_db_max : s_db_min);
        uint8_t i = 0;
        if (v < 0) { s_det_val[i++] = '-'; v = (int16_t)(-v); }
        top_u2s(&s_det_val[i], (uint32_t)v);
        s_det.titulo = s_scale_adjust_max ? tr("Escala: límite alto", "Scale: upper limit")
                                          : tr("Escala: límite bajo", "Scale: lower limit");
        s_det.valor  = s_det_val;
        s_det.alt    = tr("Cambiar a LO / HI", "Switch to LO / HI");
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
            /*
             * MEDIO PIE NO EXISTE CUANDO NO HAY DOS BOTONES - 30/09/2026.
             *
             * ui_det_hit() reparte el pie por la x -izquierda ALT, derecha
             * VOLVER- porque es un mapa de coordenadas y no sabe cuantos
             * botones hay. Con st->alt a 0, que son todas las pantallas de
             * detalle menos Filtro y Escala, el pie es UN solo boton de 8 a
             * 792. La ACCION salia bien (el `else` de det_touch cae en
             * menu_grid_show()), pero el DIBUJO solo hunde el boton cuando
             * lo pulsado es UID_HIT_VOLVER: tocando la mitad izquierda de
             * "Volver" no se hundia nada. En un tactil resistivo que ya pide
             * fuerza, eso es exactamente el "no ha entrado" que hace que des
             * el segundo toque.
             *
             * Se normaliza aqui, que es donde se sabe si hay uno o dos.
             */
            if (!s_det.alt && s_det_press == UID_HIT_ALT) {
                s_det_press = UID_HIT_VOLVER;
            }
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
            /* Solo existe en Escala y en Filtro. En las demas ya no llega
             * aqui: la pulsacion se normaliza a VOLVER al cogerla, para que
             * el boton se hunda tambien por la mitad izquierda - ver el
             * comentario de arriba. El `else` se queda como red. */
            if (s_menu_detail_target == ENCODER_TARGET_SCALE ||
                s_menu_detail_target == ENCODER_TARGET_FILTRO) {
                encoder_inject_press();
            } else {
                menu_grid_show();
            }
            break;
        case UID_HIT_VOLVER: menu_grid_show(); break;
        default:
            /*
             * Y SOLO SI DE VERDAD HAY BOTONES - 30/09/2026.
             *
             * ui_det_hit() devuelve UID_HIT_PRE0+i para CUALQUIER toque en su
             * franja (y de 316 a 375), porque no recibe el estado y no puede
             * saber si esa fila esta pintada. La fila de ajustes rapidos solo
             * existe en la pantalla del FILTRO (ver det_sync(), que apaga
             * s_det.pre en todas las demas).
             *
             * O sea que en Volumen, Silenciador, Ganancia, Reduccion, Brillo,
             * Suavizado, Desplazamiento, Tono y Escala esa franja esta VACIA
             * -cero pixeles distintos del fondo- y un dedo ahi cambiaba el
             * filtro de audio, el ancho de CW y la chapa BW de la barra de
             * estado, y lo marcaba para guardar en CONFIG.CSV. Sin que se
             * iluminara nada, porque no hay nada que iluminar.
             */
            if (s_menu_detail_target == ENCODER_TARGET_FILTRO
                && k >= UID_HIT_PRE0 && k < UID_HIT_PRE0 + UID_PRE_N) {
                fil_rapido_pon((uint8_t)(k - UID_HIT_PRE0));
                det_sync();
                ui_det_draw(&s_det);
            }
            break;
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
    /* La pantalla de la hora por radio se cierra como las demas, y ademas
     * hay que soltar el decodificador: si no, seguiria comiendose la
     * envolvente de cada bloque en la interrupcion para nada. hora_cerrar()
     * pasa por aqui despues de restaurar la frecuencia, asi que esto es la
     * red para las otras salidas -el boton de Ajustes, la pulsacion larga-. */
    s_menu_qth_active = 0U;
    if (s_menu_hora_active) {
        s_menu_hora_active = 0U;
        dcf77_stop();
        /* Y de vuelta a donde estabas. Ver el comentario de hora_cerrar()
         * para por que esto vive aqui y no alli. */
        demod_am_set_audio_bw(s_hora_bw);
        apply_demod_mode(s_hora_modo);
        s_tune_hz = s_hora_vfo;
        apply_lo_tune(s_tune_hz);
    }
    /* Salir del menu cancela el modo "elegir para enganchar". Va AQUI y no en
     * cfg_show(): ahi lo ponia la primera version y se cancelaba solo, porque
     * el boton Func enciende el modo y JUSTO DESPUES abre la pantalla - que
     * lo apagaba. Resultado: tocabas Paleta y cambiaba la paleta en vez de
     * engancharla. Abrir una pantalla no puede deshacer lo que ha pedido
     * quien la abre. */
    s_cfg_func           = 0U;
    /* Y suelta el enganche, no solo el destino del mando. 23/09/2026: la
     * linea de abajo devolvia s_encoder_target a TUNE pero dejaba
     * s_encoder_ajuste puesto, asi que despues de enganchar algo y abrir y
     * cerrar CUALQUIER pantalla -o elegir una banda, o meter una frecuencia
     * por teclado- el boton Func seguia encendido y rotulado con el ajuste
     * mientras el mando sintonizaba: los dos indicadores decian cosas
     * distintas. Y la siguiente pulsacion de Func caia en la rama de SOLTAR,
     * asi que solo apagaba la luz y habia que pulsarlo DOS veces para elegir
     * otra vez. Quien engancha por Func (cfg_apply) llama a esta funcion y
     * engancha DESPUES a proposito, asi que no se pisa a si mismo. */
    s_encoder_ajuste     = AJ_ENGANCHE_NINGUNO;
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
    /*
     * EL HUECO ENTRE LOS DOS PANELES. 25/09/2026.
     *
     * *** Por el dueno del proyecto: "cuando entro a ajustes y salgo, entre
     * el waterfall y la barra de frecuencias se queda como un resto de
     * imagen de la pantalla de ajustes". ***
     *
     * Y tenia razon. El menu tapa MENU_AREA entera -de SPEC_Y hasta el final
     * del panel de la cascada- pero al salir se repintan DOS PANELES, y
     * entre ellos hay dos filas que no son de ninguno: el panel del espectro
     * acaba en 343 y el de la cascada empieza en 346. Esas dos filas (344 y
     * 345) son el hueco que separa los dos recuadros, se quedaban con los
     * pixeles del menu, y ahi siguen hasta que algo mas las pisara.
     *
     * Es el fallo tipico de restaurar POR PIEZAS lo que se tapo DE UNA: hay
     * que acordarse de todas las piezas, y el hueco no es una pieza, asi que
     * nadie se acuerda de el.
     *
     * Se tapa calculandolo de la geometria y no escribiendo un 344: si algun
     * dia SPEC_H o WF_PANEL_Y se mueven -que ya se movieron una vez, cuando
     * la barra de estado empujo SPEC_Y de 64 a 104- esto se mueve solo. Si
     * los dos paneles llegaran a tocarse, la altura sale 0 y no se pinta
     * nada.
     */
    {
        uint16_t hueco_y = (uint16_t)(SPEC_Y + SPEC_H);

        if (WF_PANEL_Y > hueco_y) {
            gfx_fill_rect(0, hueco_y, MAIN_W,
                          (uint16_t)(WF_PANEL_Y - hueco_y), GFX_COLOR_BLACK);
        }
    }
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
    /*
     * Donde va el oscilador. Antes era una linea -freq menos Fs/4- porque la
     * relacion entre sintonia y oscilador era fija; ahora la decide
     * lo_para_sintonia(), que con el NCO puesto devuelve el que ya hay salvo
     * que la sintonia se haya salido de la ventana. Ver el bloque del NCO de
     * mas arriba.
     */
    uint32_t actual_lo_hz = lo_para_sintonia(freq_hz, is_wfm);
    uint8_t want_gd32 = (actual_lo_hz < LO_GEN_CROSSOVER_HZ) ? 1U : 0U;
    uint8_t ok = 1U;

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
        /* Acabamos de APAGAR el otro generador, asi que lo que hubiera
         * programado ya no vale: hay que reprogramar aunque los hercios
         * coincidan. Sin esto, cruzar los 300 kHz hacia una frecuencia que
         * ya se habia visitado dejaria la radio muda con toda la pinta de
         * estar sintonizada. */
        s_lo_hz = 0UL;
    }

    /*
     * Reprogramar SOLO si de verdad cambia. Antes no hacia falta la
     * comprobacion -el oscilador cambiaba en cada clic por definicion- y con
     * el NCO es justo al reves: lo normal es que NO cambie, y escribirle los
     * mismos registros por I2C en cada clic seria tirar el tiempo y arriesgar
     * un chasquido en el audio por nada. Esto es, de hecho, lo que hace que
     * el NCO se note: sintonizar deja de tocar el hardware.
     */
    if (actual_lo_hz != s_lo_hz) {
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
            s_lo_hz = actual_lo_hz;
        }
    }

    /*
     * Y el desplazamiento digital, que es EXACTAMENTE la distancia entre lo
     * que se sintoniza y lo que hay programado. Se pone aqui, en la misma
     * funcion que programa el oscilador y sin nada en medio, porque los dos
     * numeros tienen que decir lo mismo o la señal se sale del filtro de
     * canal. Se calcula de s_lo_hz -lo que quedo de verdad-, no de
     * actual_lo_hz, para que un fallo al programar no deje la cuenta mintiendo.
     */
    if (ok) {
        demod_am_set_mix_hz(is_wfm ? 0.0f
                                   : (float)((int64_t)freq_hz - (int64_t)s_lo_hz));
    }

    /*
     * Cambiar de frecuencia es cambiar de emisora, asi que el RDS empieza de
     * cero - 24/09/2026, por el dueno: "y si cambio de freq, el texto del
     * rds se deberia de reiniciar". Un nombre de emisora que se queda de la
     * anterior es una pantalla que miente, y el radiotexto es peor todavia
     * porque parece informacion fresca.
     *
     * Se compara con la frecuencia ANTERIOR y no se reinicia a ciegas: esta
     * funcion tambien se llama al arrancar y al cambiar de modo, y reiniciar
     * ahi tiraria un enganche bueno por nada.
     */
    {
        static uint32_t s_rds_ult_freq = 0UL;
        if (freq_hz != s_rds_ult_freq) {
            s_rds_ult_freq = freq_hz;
            rds_reinicia();
        }
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
/*
 * EL PANEL DE FT8 SE QUEDA CON TODO - 25/09/2026.
 *
 * *** Por el dueno del proyecto: "para ft8 no me hace falta ver el
 * espectro" ***
 *
 * Y lleva razon. El osciloscopio de sintonia existe para llevar un pico
 * hasta una raya, que es lo que se hace en RTTY y en CW. En FT8 no se
 * sintoniza nada a mano: la emision cae donde cae dentro de la ventana y el
 * decodificador la busca solo. Lo que si se mira todo el rato es la LISTA,
 * y con ocho renglones se pierde media tanda cada quince segundos.
 *
 * Asi que en FT8 el panel ocupa desde donde empieza el espectro hasta donde
 * acaba el de texto: 318 px en vez de 174, y dieciseis renglones en vez de
 * ocho. Los numeros no se escriben aqui, se derivan de los mismos de
 * siempre, para que no haya dos sitios que cuadrar a mano.
 */
#define FT8_PANEL_Y     SPEC_Y
#define FT8_PANEL_H     (uint16_t)((RTTY_TEXT_PANEL_Y + RTTY_TEXT_PANEL_H) - SPEC_Y)
#define FT8_PANEL_FILAS (uint8_t)((FT8_PANEL_H - UDG_HDR_H) / UDG_LINE_H)

#define RTTY_SCOPE_GAP_H   2U /* thin gap between the scope trace and the text panel, same idea as WF_PANEL_Y's own "64+280+2" gap from the normal spectrum panel */
#define RTTY_SCOPE_TRACE_H (uint16_t)(RTTY_TEXT_PANEL_Y - SPEC_Y - RTTY_SCOPE_GAP_H) /* 248 - 104 - 2 = 142. Decia "358 - 144 - 2 = 212" hasta el 28/09/2026:
    * son las constantes de antes del rediseno de la franja de estado, que
    * subio SPEC_Y de 64 a 104 -lo explica main.c donde define RTTY_TEXT_PANEL_Y-.
    * Un alto de traza equivocado en 70 px es con lo que alguien razona si algo
    * cabe encima del panel de texto. Sustituye al viejo bar_area_h basado en SPEC_H */

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

/*
 * ===========================================================================
 * EL PANEL DE RDS - 23/09/2026
 * ===========================================================================
 * Reutiliza el panel de renglones del RTTY y del Morse. Lo que cambia es de
 * donde sale el texto.
 */
/*
 * ===========================================================================
 * LA MARQUESINA DE RDS - 24/09/2026
 * ===========================================================================
 * El primer intento le dio al RDS dos renglones donde va la cascada.
 * Funcionaba, pero se comia media pantalla por un dato que se mira de reojo,
 * y el dueno lo dijo en una linea: "creo que el texto del rds estaria mejor
 * en otro sitio, como entre el boton de bandas y la hora, en modo
 * marquesina". Tenia razon: ese hueco de la cabecera estaba vacio, y asi la
 * cascada vuelve entera.
 *
 * Aqui se monta el texto; ui_top.c lo pinta recortado al hueco. El texto
 * lleva todo seguido -emisora, codigo, hora y radiotexto- separado por
 * puntos, y cuando se acaba vuelve a empezar.
 */

static uint8_t rds_txt_pon(char *b, uint8_t i, const char *t)
{
    while (*t != '\0' && i < (uint8_t)(sizeof s_rds_txt - 1U)) { b[i++] = *t++; }
    return i;
}

static uint8_t rds_txt_dos(char *b, uint8_t i, uint8_t v)
{
    b[i++] = (char)('0' + (v / 10U) % 10U);
    b[i++] = (char)('0' + v % 10U);
    return i;
}


/*
 * Monta el texto con lo que se sepa.
 *
 * Lo que NO se sabe no se deja en blanco: se dice. Un hueco vacio se lee
 * como "esto no existe"; "buscando RDS" se lee como "esto viene". Es la
 * misma decision que en las pantallas de hora por radio.
 */
static void rds_marq_sync(void)
{
    rds_info_t in;
    uint8_t i = 0U, j;

    rds_info(&in);

    /*
     * EL NOMBRE DE LA EMISORA NO VA AQUI desde que hay renglon fijo encima:
     * estaria dos veces en la misma esquina de la pantalla. La marquesina se
     * queda con lo que cambia -la hora y el radiotexto- y el renglon de
     * arriba con lo que identifica.
     *
     * Y mientras no haya nada que decir, no se dice nada - 24/09/2026, por
     * el dueno: "y lo de buscando RDS no deberia de salir". En las pantallas
     * de hora por radio si se avisa de que se esta buscando, porque ahi el
     * aparato esta dedicado a eso y el silencio se leeria como averia; aqui
     * no, esto es un hueco de la cabecera mientras escuchas la radio.
     */

    if (in.tiene_hora && s_rds_hora_ok) {
        /*
         * La hora del RDS viene en UTC mas un desplazamiento local en medias
         * horas que manda la propia emisora. Eso es MEJOR que nuestra regla
         * de verano/invierno: la emisora sabe en que huso esta y cuando se
         * cambia la hora; nosotros lo deduciamos.
         */
        int16_t mins = (int16_t)(in.hora * 60 + in.minuto)
                     + (int16_t)in.desp_medias * 30;
        while (mins < 0)     { mins += 1440; }
        while (mins >= 1440) { mins -= 1440; }
        i = rds_txt_dos(s_rds_txt, i, (uint8_t)(mins / 60));
        s_rds_txt[i++] = ':';
        i = rds_txt_dos(s_rds_txt, i, (uint8_t)(mins % 60));
        s_rds_txt[i++] = ' ';
        i = rds_txt_dos(s_rds_txt, i, in.dia);
        s_rds_txt[i++] = '/';
        i = rds_txt_dos(s_rds_txt, i, in.mes);
    }

    if (in.tiene_rt) {
        uint8_t fin;
        if (i > 0U) { i = rds_txt_pon(s_rds_txt, i, "   "); }
        fin = i;
        for (j = 0U; j < 64U && i < (uint8_t)(sizeof s_rds_txt - 8U); j++) {
            s_rds_txt[i++] = in.rt[j];
            if (in.rt[j] != ' ') { fin = i; }
        }
        i = fin;
    }

    /* Los espacios del final son el hueco entre una vuelta y la siguiente.
     * Si no hay nada que enseñar, el texto se queda VACIO de verdad: con los
     * espacios, la marquesina estaria corriendo un texto invisible. */
    if (i > 0U) {
        i = rds_txt_pon(s_rds_txt, i, "     ");
    }
    s_rds_txt[i] = '\0';

    /*
     * EL RENGLON FIJO: que cadena es.
     *
     * El codigo de programa identifica la CADENA, no el transmisor: todos
     * los repetidores de COPE en Espana mandan el mismo. Asi que se puede
     * traducir a un nombre - ver User/rds_pi.h para como esta hecho el
     * codigo y de donde sale la lista.
     *
     * Si el codigo no esta en la lista se enseña EL CODIGO, no un hueco
     * vacio: asi se puede anotar y anadirlo despues, que es literalmente
     * como se hacen estas listas. Un hueco vacio se leeria como "no hay
     * RDS", que seria mentira.
     *
     * El codigo en crudo desaparecio de la marquesina el mismo dia a peticion
     * del dueno, y con razon: ahi era un numero suelto estorbando. Aqui es
     * otra cosa - es la identidad de la emisora cuando no sabemos su nombre.
     */
    {
        const char *n = in.tiene_pi ? rds_pi_nombre(in.pi) : 0;
        uint8_t k = 0U;

        if (n) {
            /* Lo mejor: la cadena, por su codigo de programa. */
            while (n[k] != '\0' && k < (uint8_t)(sizeof s_rds_nom - 1U)) {
                s_rds_nom[k] = n[k]; k++;
            }
        } else if (in.tiene_ps) {
            /* Lo segundo: el nombre que manda la propia emisora. Son ocho
             * caracteres y los rellena con espacios, asi que se recortan. */
            for (k = 0U; k < 8U; k++) { s_rds_nom[k] = in.ps[k]; }
            while (k > 0U && s_rds_nom[k - 1U] == ' ') { k--; }
        } else if (in.tiene_pi) {
            /* Y si no hay ni una cosa ni la otra, EL CODIGO EN CRUDO. No un
             * hueco vacio, que se leeria como "no hay RDS": asi se puede
             * anotar y anadirlo a la lista despues, que es literalmente como
             * se hacen estas listas. */
            static const char hx[] = "0123456789ABCDEF";
            s_rds_nom[k++] = hx[(in.pi >> 12) & 0xFU];
            s_rds_nom[k++] = hx[(in.pi >> 8) & 0xFU];
            s_rds_nom[k++] = hx[(in.pi >> 4) & 0xFU];
            s_rds_nom[k++] = hx[in.pi & 0xFU];
        }
        s_rds_nom[k] = '\0';
    }
}

/*
 * Correrla y repintarla.
 *
 * Va a su propio ritmo (40 ms) y no al del espectro: la cabecera se repinta
 * por franjas y a treinta por segundo el texto se movia a tirones porque
 * cada frame avanzaba un pixel entero. A 25 por segundo y dos pixeles se lee
 * mejor y cuesta menos bus.
 */
#define RDS_MARQ_MS   40U
#define RDS_MARQ_PX    2

static void rds_marq_poll(void)
{
    if (!rds_activo() || demod_am_get_mode() != DEMOD_MODE_WFM
        || s_menu_open || s_screen_asleep || !s_pantalla_pintada) {
        return;
    }
    if ((int32_t)(g_msticks - s_rds_next_ms) < 0) { return; }
    s_rds_next_ms = g_msticks + RDS_MARQ_MS;

    rds_marq_sync();
    if (strcmp(s_rds_txt, s_rds_txt_ant) != 0) {
        /*
         * El texto ha cambiado: se vuelve a medir, PERO NO SE VUELVE A
         * EMPEZAR - 24/09/2026, por el dueno: "se ve que la marquesina cada
         * vez que decodifica un caracter reinicia su movimiento".
         *
         * Y era eso literalmente. El nombre y el radiotexto NO llegan de
         * golpe: llegan de dos en dos y de cuatro en cuatro caracteres, un
         * grupo cada pocas decimas de segundo. Volver al principio en cada
         * cambio dejaba la marquesina temblando en el sitio sin avanzar
         * nunca.
         *
         * Ahora solo se recorta el desplazamiento cuando el texto nuevo es
         * mas corto que lo que ya se habia corrido. Se vuelve a empezar de
         * verdad en un unico caso: al cambiar de emisora, que lo hace
         * rds_reinicia().
         */
        strcpy(s_rds_txt_ant, s_rds_txt);
        s_rds_txt_w = ui_top_rds_texto_w(s_rds_txt);
        if (s_rds_off > (int16_t)(s_rds_txt_w + 8)) { s_rds_off = 0; }
    }

    s_top.rds = s_rds_txt;
    s_top.rds_off = s_rds_off;
    s_top.rds_nombre = s_rds_nom;
    ui_top_draw_rds(&s_top);

    s_rds_off = (int16_t)(s_rds_off + RDS_MARQ_PX);
    if (s_rds_off > (int16_t)(s_rds_txt_w + 8)) {
        s_rds_off = (int16_t)(-ui_top_rds_ancho());   /* vuelve por la derecha */
    }
}

/* Lo que hay que hacer cuando se cambia de emisora: tirar TODO lo del RDS
 * anterior. Un nombre de emisora que se queda de la de antes es exactamente
 * una pantalla que miente, y el radiotexto es peor todavia porque parece
 * informacion fresca. */
static void rds_reinicia(void)
{
    if (!rds_activo()) { return; }
    rds_start(192000.0f);
    s_rds_hora_puesta = 0U;
    s_rds_hora_ok = 0U;
    s_rds_cand_vale = 0U;
    s_rds_seq_ant = 0UL;
    s_rds_ct_n = 0U;
    s_rds_ct_mal = 0U;
    s_rds_txt[0] = '\0';
    s_rds_nom[0] = '\0';
    s_rds_txt_ant[0] = '\1';   /* forzar el remedido y el repintado */
    s_rds_off = 0;
}

/*
 * Encender o apagar el RDS.
 *
 * Solo significa algo en FM ancha: la subportadora de 57 kHz va dentro de la
 * senal multiplex de la FM comercial y en AM o en banda lateral no existe.
 * Se deja encender igualmente -el ajuste se guarda y en cuanto pasas a WFM
 * se pone a trabajar- en vez de bloquearlo segun el modo, que obligaria a
 * acordarse de encenderlo despues.
 *
 * Al encenderlo se borra lo que hubiera: el nombre de la emisora anterior
 * pintado mientras se busca la siguiente es exactamente una pantalla que
 * miente.
 */
static void rds_conmutar(void)
{
    if (rds_activo()) {
        rds_stop();
    } else {
        rds_start(192000.0f);   /* la tasa del camino de FM ancha */
        s_rds_hora_puesta = 0U;
        s_rds_hora_ok = 0U;
        s_rds_cand_vale = 0U;
        s_rds_seq_ant = 0UL;
        s_rds_ct_n = 0U;
        s_rds_ct_mal = 0U;
    }
    if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
    /* El panel aparece o desaparece con esto, y debajo hay cascada: hay que
     * reconstruir la pantalla entera. */
    if (s_pantalla_pintada && !s_screen_asleep && !s_menu_open) {
        radio_screen_draw();
    }
}

/*
 * Poner el reloj de la radio en hora con lo que diga el RDS. Una sola vez
 * por sesion: la hora llega una vez por minuto y no tiene sentido reescribir
 * el reloj cada vez.
 */
static void rds_poll(void)
{
    rds_info_t in;
    int16_t mins;

    if (!rds_activo()) { return; }
    rds_info(&in);
    if (!in.tiene_hora || in.hora_seq == s_rds_seq_ant) { return; }
    s_rds_seq_ant = in.hora_seq;

    mins = (int16_t)(in.hora * 60 + in.minuto);   /* en UTC, minutos del dia */
    s_rds_ct_n++;

    /*
     * DOS HORAS COHERENTES ANTES DE TOCAR EL RELOJ.
     *
     * rds.c ya exige mucho para dar una hora por buena: los cuatro bloques
     * del grupo, el identificador de emisora coincidiendo con el que ya
     * teniamos, y que la hora y la fecha tengan sentido. Pero desde que hay
     * correccion de errores, "bloque bueno" incluye "bloque arreglado", y
     * una correccion equivocada en el bloque que lleva la hora pondria el
     * reloj en cualquier sitio.
     *
     * Asi que se espera a la SEGUNDA. El grupo de la hora llega una vez por
     * minuto, y la segunda tiene que caer exactamente donde nuestro propio
     * reloj dice que debe caer: si la primera decia 21:47 y han pasado tres
     * minutos, la segunda tiene que decir 21:50. Falsificar eso por
     * casualidad es practicamente imposible, y el precio es un minuto de
     * espera en un aparato que hasta anteayer tardaba cuatro en sacar la
     * hora de DCF77 con viento a favor.
     *
     * Se admite un minuto de margen porque nuestro reloj y el suyo no tienen
     * por que cambiar de minuto en el mismo instante.
     */
    /*
     * ATAJO PARA LOS GRUPOS LIMPIOS.
     *
     * Si los cuatro bloques llegaron bien, sin que la correccion tuviera que
     * tocar ninguno, la hora se aplica ya. Ese es exactamente el nivel de
     * confianza que tenia esto ANTES de que existiera la correccion -cuatro
     * restos de diez bits correctos, el identificador de emisora coincidiendo
     * y la fecha con sentido-, y se comporto bien. Esperar un minuto mas en
     * ese caso seria castigar la buena recepcion por una precaucion que solo
     * hace falta cuando ha habido que arreglar bits.
     */
    if (in.hora_limpia) {
        /* Se ancla en vez de poner solo el minuto: el instante en que acaba
         * de llegar este grupo ES el borde de minuto. Ver reloj_anclar(). */
        reloj_anclar((int16_t)(mins + (int16_t)in.desp_medias * 30),
                     rds_hora_atraso_ms());
        s_rds_hora_ok = 1U;
        s_rds_hora_puesta = 1U;
        s_rds_cand_vale = 0U;
        debug_print("rds: reloj puesto en hora (grupo limpio)\n");
        return;
    }

    if (s_rds_cand_vale) {
        int32_t pasados = (int32_t)((g_msticks - s_rds_cand_ms + 30000UL) / 60000UL);
        int32_t esperado = ((int32_t)s_rds_cand_mins + pasados) % 1440;
        int32_t d = (int32_t)mins - esperado;

        while (d < -720) { d += 1440; }
        while (d >  720) { d -= 1440; }
        if (d >= -1 && d <= 1) {
            /* Confirmada. La hora del RDS viene en UTC mas un desplazamiento
             * local en medias horas que manda la propia emisora: eso es
             * MEJOR que nuestra regla de verano/invierno, porque la emisora
             * sabe en que huso esta y cuando se cambia la hora. */
            reloj_anclar((int16_t)(mins + (int16_t)in.desp_medias * 30),
                         rds_hora_atraso_ms());
            s_rds_hora_ok = 1U;
            s_rds_hora_puesta = 1U;
            s_rds_cand_vale = 0U;
            debug_print("rds: reloj puesto en hora (dos horas coherentes)\n");
            return;
        }
        s_rds_ct_mal++;
        debug_print("rds: la segunda hora no cuadra, se vuelve a empezar\n");
    }

    s_rds_cand_mins = mins;
    s_rds_cand_ms = g_msticks;
    s_rds_cand_vale = 1U;
}

/*
 * LA CABECERA DE FT8: la barra de la ranura y la chapa del segundo.
 *
 * LA BARRA cuenta los 15 s de la ranura y dentro lleva los dos numeros que
 * contestan a "¿por que no decodifica?": cuantos CANDIDATOS encontro la
 * busqueda de sincronismo en la ultima ranura, y el margen de dB que vio.
 * Con candidatos a cero no hay nada que decodificar -o la senal esta fuera de
 * la ventana, o no hay senal-; con candidatos y sin mensajes, la senal esta
 * pero llega rota. Son dos preguntas distintas y sin estos numeros se ven
 * igual: la lista vacia.
 *
 * LA CHAPA cuenta el segundo dentro de la ranura, y se toca para cuadrarla
 * (ver ft8_modo_cuadra()). Catorce de cada quince segundos no llega ningun
 * mensaje -eso es lo NORMAL en FT8-, asi que sin un numero que suba, "va
 * bien y esta callado" y "no ha arrancado" se ven exactamente igual.
 */
static char s_ft8_barra[40];

static void ft8_num(char *b, uint8_t *i, int32_t v)
{
    uint32_t u;
    char tmp[12];
    uint8_t n = 0U;

    if (v < 0) { b[(*i)++] = '-'; u = (uint32_t)(-v); } else { u = (uint32_t)v; }
    do { tmp[n++] = (char)('0' + (u % 10U)); u /= 10U; } while (u != 0U);
    while (n > 0U) { b[(*i)++] = tmp[--n]; }
}

/*
 * LA CHAPA DE HFDL: en que anda el receptor.
 *
 * HFDL no tiene ranuras -las rafagas llegan cuando llegan- asi que la barra
 * no cuenta segundos como la de FT8. Lo que hace falta ver mientras se
 * busca una frecuencia es OTRA cosa: si el sincronismo esta encontrando
 * algo. Por eso la barra marca la fase del preambulo (buscando A1, luego A2,
 * luego M1, luego enganchado) y el texto la dice con palabras.
 *
 * Es el numero que contesta a "estoy en la frecuencia buena?" antes de que
 * salga ningun mensaje, que es justo lo que no se sabe al empezar.
 */
static void hfdl_chapa(void)
{
    static const char *const k_fase[4] = {
        "buscando", "media senal", "casi", "ENGANCHADO"
    };
    uint8_t e = hfdl_modo_estado();

    if (e > 3U) { e = 3U; }

    s_digi.barra_on  = 1U;
    s_digi.barra_max = 3U;
    s_digi.barra_v   = e;
    s_digi.enganchado = (uint8_t)(e == 3U);

    /*
     * EL EMBUDO DEL PREAMBULO, DENTRO DE LA BARRA. 25/09/2026.
     *
     * *** Idea de la pantalla del Kleos, que el dueno mando de referencia:
     * abajo lleva "START 25 > CONFIRM 20 > MODE 20 > FRAME 11". ***
     *
     * Es lo mas util de toda su pantalla. La FASE ya la dice el relleno de la
     * barra -de cero a tres- asi que el texto de dentro queda libre para algo
     * que no se pueda dibujar, y estos cuatro numeros son justo eso.
     *
     * La forma del embudo dice que hacer, sin tener que entender nada:
     *
     *   muchos A1 y pocos A2   -> enganches falsos con ruido, no hay senal
     *   A1 y A2 parejos, pocos M1 -> hay senal pero llega rota
     *   M1 y tramas parejos    -> la cadena va; lo que falla es propagacion
     *   todo a cero            -> la frecuencia esta muerta
     *
     * En la de fabrica esos numeros estan en una esquina y no se relacionan
     * con nada. Aqui van DENTRO de la barra que dibuja la fase, que es donde
     * significan algo: el relleno dice donde estas ahora y los numeros
     * cuantas veces has llegado hasta ahi.
     */
    {
        static char emb[24];
        uint32_t a1 = 0U, a2 = 0U, m1 = 0U;
        uint8_t i = 0U;

        hfdl_modo_embudo(&a1, &a2, &m1);
        #define EMB_NUM(v) do { \
            aj_u2s(&emb[i], (v) > 999U ? 999U : (v)); \
            while (emb[i] != '\0') { i++; } \
        } while (0)
        EMB_NUM(a1); emb[i++] = '>';
        EMB_NUM(a2); emb[i++] = '>';
        EMB_NUM(m1); emb[i++] = '>';
        EMB_NUM(hfdl_modo_total());
        /*
         * Y las que no cupieron, con una X delante, SOLO si las hay. En una
         * radio sana esto no sale nunca; si sale, el numero de al lado deja
         * de ser mala propagacion y pasa a ser un limite de memoria - dos
         * problemas que se arreglan de formas distintas. Ver
         * hfdl_modo_no_cupo().
         */
        {
            uint32_t nc = hfdl_modo_no_cupo();

            if (nc != 0U) { emb[i++] = ' '; emb[i++] = 'X'; EMB_NUM(nc); }
        }
        emb[i] = '\0';
        #undef EMB_NUM
        /* Sin nada todavia, el nombre de la fase se lee mejor que "0>0>0>0". */
        s_digi.barra_txt = (a1 == 0U) ? k_fase[e] : emb;
    }

    /*
     * LA CHAPA: BUENAS DE TOTAL, "3/42".
     *
     * Antes salian solo las rafagas. Ahora que las de CRC malo no ocupan
     * renglon hace falta un sitio donde se vea que llegan, y sobre todo el
     * RENDIMIENTO: tres de cuarenta y dos dice "la sintonia esta bien y la
     * propagacion es justa", que es una frase entera en dos numeros.
     *
     * Si el segundo sube y el primero no se mueve, el problema esta despues
     * del preambulo. Si no sube ninguno, es la frecuencia.
     */
    {
        static char chapa[16];
        uint8_t i = 0U;
        uint32_t b = hfdl_modo_buenas();
        uint32_t n = hfdl_modo_total();
        uint8_t  sig = hfdl_modo_senal();

        /*
         * LA SENAL DELANTE DE TODO. 25/09/2026.
         *
         * Es el numero que contesta a "me quedo en esta frecuencia?" y lo
         * hace EN SEGUIDA, sin esperar a que llegue una rafaga: sube en
         * cuanto hay algo por encima del suelo de ruido. Los otros dos
         * tardan minutos en decir nada.
         *
         * Va sin calibrar y no son dB - ver hfdl_modo_senal(). Sirve para
         * comparar una frecuencia con otra, que es justo para lo que se
         * mira mientras se barre.
         */
        if (sig >= 10U) { chapa[i++] = (char)('0' + sig / 10U); }
        chapa[i++] = (char)('0' + sig % 10U);
        chapa[i++] = '%';
        chapa[i++] = ' ';

        if (b > 999U) { b = 999U; }
        if (n > 999U) { n = 999U; }
        if (b >= 100U) { chapa[i++] = (char)('0' + b / 100U); }
        if (b >= 10U)  { chapa[i++] = (char)('0' + (b / 10U) % 10U); }
        chapa[i++] = (char)('0' + b % 10U);
        chapa[i++] = '/';
        if (n >= 100U) { chapa[i++] = (char)('0' + n / 100U); }
        if (n >= 10U)  { chapa[i++] = (char)('0' + (n / 10U) % 10U); }
        chapa[i++] = (char)('0' + n % 10U);
        chapa[i] = '\0';
        s_digi.chip = chapa;
    }
}

/*
 * LA CHAPA DE WSPR: la cuenta atras, porque aqui NO PASA NADA CASI NUNCA.
 *
 * Es la diferencia que de verdad manda en esta pantalla. FT8 trae algo cada
 * quince segundos y HFDL cuando pasa un avion; WSPR abre la oreja durante
 * 110,6 segundos, la cierra, decodifica, y luego se queda DOS MINUTOS
 * enteros sin nada que hacer, porque las emisiones solo empiezan en los
 * minutos pares.
 *
 * Una pantalla quieta dos minutos se lee como un cuelgue. Asi que la barra
 * no marca enganche -no hay tal cosa que marcar- sino donde estas dentro
 * del ciclo:
 *
 *   capturando    la barra va de 0 a 100 mientras entra la emision
 *   esperando     la barra baja y el texto dice los segundos que faltan
 *   sin hora      ni una cosa ni otra, y lo dice con todas las letras
 *
 * Y la chapa cuenta las decodificadas. En WSPR eso es el dato: una sola
 * linea en veinte minutos puede ser un corresponsal a diez mil kilometros
 * con 200 mW, y es un resultado excelente, no una pantalla que falla.
 */
/*
 * LA CORROBORACION, PEGADA A LA BARRA.
 *
 * Es el numero que contesta a "¿por que no sale nada?", y sin el la tabla
 * vacia no distingue entre no haber senal, haberla y llegar justa, o tener
 * el reloj corrido. Hacen falta 88; el ruido puro da 74-83 y una emision
 * de verdad 93-100.
 *
 * Va en la barra y no en la chapa porque la barra se ve durante los 115
 * segundos de la captura y la espera dura cinco: puesto en la chapa, el
 * numero se veria un 4% del tiempo. Y es el de la captura ANTERIOR, que es
 * justo lo que se quiere mirar mientras corre la siguiente.
 *
 * NO SE ENSENA EL DESVIO EN HERCIOS, aunque el receptor lo sabe. Cuando la
 * corroboracion es baja, el candidato que gano es ruido, y su frecuencia
 * no significa nada: seria un numero preciso y falso al lado de uno
 * honesto.
 */
static uint8_t wspr_pon_q(char *b, uint8_t i)
{
    uint8_t q = wspr_modo_q();

    if (q == 0U) { return i; }   /* todavia no ha habido ninguna captura */

    b[i++] = ' '; b[i++] = ' '; b[i++] = 'q'; b[i++] = ' ';
    aj_u2s(&b[i], q);
    while (b[i] != '\0') { i++; }
    return i;
}

static void wspr_chapa(void)
{
    static char barra[32];   /* "empieza en 1:47  q 100" y su cero */
    static char chapa[16];
    uint8_t i = 0U;

    s_digi.barra_on  = 1U;
    s_digi.barra_max = 100U;

    if (!wspr_modo_hay_hora()) {
        /* Igual que FT8, y por la misma razon: sin reloj la captura
         * arrancaria donde no toca. Dibujar una barra seria fingir que
         * hay un ciclo al que agarrarse. */
        s_digi.barra_v    = 0U;
        s_digi.barra_txt  = tr("pon el reloj en hora", "set the clock first");
        s_digi.chip       = tr("sin hora", "no clock");
        s_digi.enganchado = 0U;
        return;
    }

    if (wspr_modo_capturando()) {
        uint8_t pc = wspr_modo_por_ciento();

        s_digi.barra_v    = pc;
        s_digi.enganchado = 1U;
        i = 0U;
        barra[i++] = 'e'; barra[i++] = 's'; barra[i++] = 'c';
        barra[i++] = 'u'; barra[i++] = 'c'; barra[i++] = 'h'; barra[i++] = 'a';
        barra[i++] = 'n'; barra[i++] = 'd'; barra[i++] = 'o'; barra[i++] = ' ';
        aj_u2s(&barra[i], pc);
        while (barra[i] != '\0') { i++; }
        barra[i++] = '%';
        i = wspr_pon_q(barra, i);
        barra[i] = '\0';
        s_digi.barra_txt = barra;
    } else {
        /*
         * LA CUENTA ATRAS EN MINUTOS Y SEGUNDOS, no en segundos a secas:
         * "1:47" se lee de un vistazo y "107 s" hay que traducirlo. Y la
         * barra se VACIA segun se acerca -de 119 a 0 escalado a 100-, o
         * sea que llena quiere decir "acaba de perderse una" y vacia
         * "esta a punto de empezar". Es la misma informacion que el
         * numero, pero sin leer.
         */
        uint16_t f = wspr_modo_espera_s();

        s_digi.barra_v    = (uint8_t)((f > 119U) ? 100U : ((f * 100U) / 119U));
        s_digi.enganchado = 0U;
        i = 0U;
        barra[i++] = 'e'; barra[i++] = 'm'; barra[i++] = 'p'; barra[i++] = 'i';
        barra[i++] = 'e'; barra[i++] = 'z'; barra[i++] = 'a'; barra[i++] = ' ';
        barra[i++] = 'e'; barra[i++] = 'n'; barra[i++] = ' ';
        barra[i++] = (char)('0' + (f / 60U));
        barra[i++] = ':';
        barra[i++] = (char)('0' + ((f % 60U) / 10U));
        barra[i++] = (char)('0' + (f % 10U));
        i = wspr_pon_q(barra, i);
        barra[i] = '\0';
        s_digi.barra_txt = barra;
    }

    {
        uint32_t b = wspr_modo_buenas();

        if (b > 999U) { b = 999U; }
        i = 0U;
        aj_u2s(chapa, b);
        while (chapa[i] != '\0') { i++; }
        chapa[i++] = ' '; chapa[i++] = 'o'; chapa[i++] = 'i'; chapa[i++] = 'd';
        chapa[i++] = 'a';
        /* "1 oida" y "3 oidas": el plural cambia y el singular no se
         * escribe mal por no gastar una linea. */
        if (b != 1U) { chapa[i++] = 's'; }
        chapa[i] = '\0';
        s_digi.chip = chapa;
    }
}

/*
 * LA CHAPA DE AIS: cuantos barcos y con que rendimiento.
 *
 * AIS no tiene fases de enganche que ensenar (como HFDL) ni ranuras que
 * contar (como FT8): las rafagas llegan cuando llegan y o el CRC cuadra o
 * no. Lo que hace falta saber mientras se mira es OTRA cosa - si la
 * frecuencia y el nivel estan bien-, y eso son dos numeros:
 *
 *   la barra    el nivel a la salida del discriminador, sin calibrar. Sube
 *               en cuanto entra una rafaga, sin esperar a que decodifique:
 *               es lo primero que dice "hay algo ahi".
 *   la chapa    tramas buenas de tramas vistas. "12/14" quiere decir que la
 *               senal llega limpia; "2/40", que llega pero rota - y eso es
 *               nivel bajo o filtro estrecho, no falta de barcos.
 *
 * La diferencia entre las dos cuentas es justo el CRC, que en AIS es la
 * unica proteccion que hay: no lleva ni correccion de errores ni
 * repeticion.
 */
/*
 * LA CHAPA DE ALE: si esta enganchado y con cuanta calidad.
 *
 * ALE no lleva CRC. Lo unico que dice cuanto fiarse de una palabra son los
 * VOTOS UNANIMES de la votacion 2 de 3: de los 48 bits, cuantos llegaron
 * iguales en las tres copias. 48 es una senal limpia; por debajo de 30, el
 * Golay puede haber "corregido" hacia una palabra que no se emitio.
 *
 * Por eso la barra es la calidad y no el nivel: el nivel dice que hay algo,
 * y la calidad dice si lo que hay se entiende. En un modo sin CRC, lo
 * segundo es lo que hace falta mirar.
 */
static void ale_chapa(void)
{
    static char barra[28];
    static char chapa[16];
    uint8_t i = 0U;
    uint8_t cal = ale_modo_calidad();

    s_digi.barra_on  = 1U;
    s_digi.barra_max = 48U;
    s_digi.barra_v   = cal;
    s_digi.enganchado = ale_modo_enganchado();

    if (!ale_modo_enganchado()) {
        s_digi.barra_v = 0U;
        s_digi.barra_txt = tr("buscando palabra", "searching for a word");
    } else {
        i = 0U;
        barra[i++] = 'c'; barra[i++] = 'a'; barra[i++] = 'l'; barra[i++] = 'i';
        barra[i++] = 'd'; barra[i++] = 'a'; barra[i++] = 'd'; barra[i++] = ' ';
        aj_u2s(&barra[i], cal);
        while (barra[i] != '\0') { i++; }
        barra[i++] = '/'; barra[i++] = '4'; barra[i++] = '8';
        barra[i] = '\0';
        s_digi.barra_txt = barra;
    }

    {
        uint32_t w = ale_modo_palabras();

        if (w > 9999U) { w = 9999U; }
        i = 0U;
        aj_u2s(chapa, w);
        while (chapa[i] != '\0') { i++; }
        chapa[i++] = ' '; chapa[i++] = 'p'; chapa[i++] = 'a'; chapa[i++] = 'l';
        chapa[i] = '\0';
        s_digi.chip = chapa;
    }
}

static void ais_chapa(void)
{
    static char barra[28];
    static char chapa[16];
    uint8_t  i = 0U;
    uint8_t  sig = ais_modo_senal();
    uint32_t b = ais_modo_tramas();
    uint32_t v = ais_modo_vistas();

    s_digi.barra_on  = 1U;
    s_digi.barra_max = 99U;
    s_digi.barra_v   = sig;
    s_digi.enganchado = (uint8_t)(b > 0U);

    aj_u2s(barra, ais_modo_barcos());
    while (barra[i] != '\0') { i++; }
    barra[i++] = ' '; barra[i++] = 'b'; barra[i++] = 'a'; barra[i++] = 'r';
    barra[i++] = 'c'; barra[i++] = 'o'; barra[i++] = 's';
    /* "1 barco" y "3 barcos". */
    if (ais_modo_barcos() == 1U) { i--; }
    barra[i] = '\0';
    s_digi.barra_txt = barra;

    if (b > 999U) { b = 999U; }
    if (v > 999U) { v = 999U; }
    i = 0U;
    aj_u2s(chapa, b);
    while (chapa[i] != '\0') { i++; }
    chapa[i++] = '/';
    aj_u2s(&chapa[i], v);
    s_digi.chip = chapa;
}

static void ft8_chapa(void)
{
    uint8_t i = 0U;

    s_digi.barra_on  = 1U;
    s_digi.barra_max = 15U;

    if (!ft8_modo_hay_hora()) {
        /* Sin hora no hay ranura que dibujar, y decir "0/15" seria fingir que
         * la hay. La barra se queda vacia y lo dice con todas las letras. */
        s_digi.barra_v   = 0U;
        s_digi.barra_txt = tr("pon el reloj en hora", "set the clock first");
        s_digi.chip = ft8_modo_estado_txt();
        s_digi.enganchado = 0U;
        return;
    }

    s_digi.barra_v = ft8_modo_segundo();

    {
        /*
         * LOS TRES NUMEROS DE LA BARRA - cambiados el 25/09/2026, cuando
         * FT8 pasó a decodificar "grupos de cinco por minuto" en vez de por
         * tanda.
         *
         * Antes salia aqui el margen de dB, que sirvio para lo suyo -
         * comprobar que el cuantizador no saturaba, y no saturaba- y ya no
         * dice nada nuevo. Ahora salen los tres que contestan a por que una
         * tanda no decodifica:
         *
         *   CANDIDATOS: sitios donde la busqueda creyo ver sincronismo.
         *      Cero = no llega nada con forma de FT8.
         *   MILISEGUNDOS: lo que tardo la ultima decodificacion. Es el
         *      numero que descubrio el fallo: si se acerca a los 15.000 de
         *      una ranura, el trabajo no cabe en el tiempo que hay.
         *   PERDIDOS: subbloques de audio que se tiraron porque el bucle
         *      principal tardo mas de 320 ms en vaciar el remuestreador.
         *      TIENE QUE SER CERO. Si no lo es, a la captura le faltan
         *      trozos y FT8 decodifica peor sin que se vea por que - que es
         *      exactamente lo que estaba pasando.
         *
         * Recortados a proposito: el ancho de la barra esta medido contra el
         * texto mas largo que estos limites permiten (141 px de 150, ver
         * UDG_BAR_W). Una cifra mas se saldria por encima de la chapa.
         */
        uint16_t c = ft8_modo_candidatos();
        uint16_t ms = ft8_modo_ms();
        uint16_t pd = ft8_modo_perdidos();

        ft8_num(s_ft8_barra, &i, (int32_t)((c > 999U) ? 999U : c));
        s_ft8_barra[i++] = 'c';
        s_ft8_barra[i++] = ' '; s_ft8_barra[i++] = ' ';
        ft8_num(s_ft8_barra, &i, (int32_t)((ms > 9999U) ? 9999U : ms));
        s_ft8_barra[i++] = 'm'; s_ft8_barra[i++] = 's';
        s_ft8_barra[i++] = ' '; s_ft8_barra[i++] = ' ';
        ft8_num(s_ft8_barra, &i, (int32_t)((pd > 99U) ? 99U : pd));
        s_ft8_barra[i++] = 'p';

        /*
         * Y EL DESFASE MEDIO, que es el cuarto numero desde el 25/09/2026.
         *
         * Sale aqui para poder VER a la radio cuadrarse sola: tiene que irse
         * acercando a cero tanda tras tanda. Si en vez de eso se doblara, el
         * signo de la correccion estaria del reves - y esa es exactamente la
         * clase de cosa que no se puede dejar sin enseñar, porque el sintoma
         * de tenerla al reves es "decodifica peor", no un fallo.
         */
        {
            int16_t dt = ft8_modo_dt_ms();
            if (dt > 999) { dt = 999; }
            if (dt < -999) { dt = -999; }
            s_ft8_barra[i++] = ' '; s_ft8_barra[i++] = ' ';
            s_ft8_barra[i++] = (dt < 0) ? '-' : '+';
            if (dt < 0) { dt = (int16_t)(-dt); }
            ft8_num(s_ft8_barra, &i, (int32_t)(dt / 100));
            s_ft8_barra[i++] = ',';
            ft8_num(s_ft8_barra, &i, (int32_t)((dt / 10) % 10));
        }
        s_ft8_barra[i] = '\0';
        s_digi.barra_txt = s_ft8_barra;
    }

    {
        uint8_t sg = ft8_modo_segundo();
        uint8_t k = 0U;

        if (sg >= 10U) { s_digi_chip[k++] = (char)('0' + (sg / 10U)); }
        s_digi_chip[k++] = (char)('0' + (sg % 10U));
        s_digi_chip[k++] = '/'; s_digi_chip[k++] = '1'; s_digi_chip[k++] = '5';
        s_digi_chip[k] = '\0';
        s_digi.chip = s_digi_chip;
        s_digi.enganchado = 1U;
    }
}

/*
 * EL PANEL DE FT8, cuadro a cuadro.
 *
 * Repinta lo menos posible, y no por ahorrar sino porque repintar de mas SE
 * VE: la lista entera son 800x318 px y hacerlo sesenta veces por segundo
 * parpadea. Asi que la lista solo se repinta cuando llega un mensaje nuevo
 * -o sea, una vez cada quince segundos como mucho- y la cabecera cuando
 * cambia el segundo.
 */
static void digi_sync(void);

static uint8_t  s_ft8_todo = 1U;
static uint32_t s_ft8_total_visto;
static uint8_t  s_ft8_seg_visto = 0xFFU;

static void ft8_panel_reinicia(void)
{
    s_ft8_todo = 1U;
}

/*
 * EL MAPA DE FT8 - 28/09/2026.
 *
 * *** Por el dueño del proyecto: "me gustaria ponerle mapa a las señales
 * ft8 que cazamos y si sale bien a los aviones de hfdl y a los barcos de
 * ais si lo conseguimos". ***
 *
 * La lista contesta "quien"; el mapa contesta "por donde se oye hoy", que
 * es la pregunta de verdad cuando uno mira una banda: si lo que entra es
 * Europa, si ha abierto el Atlantico, si asoma Japon por el paso gris.
 *
 * SE CAMBIA TOCANDO LA LISTA, y no con un boton, porque no hay hueco: la
 * cabecera del panel de FT8 ya lleva Borrar, la barra de los quince
 * segundos, el boton de frecuencias y el de "la siguiente". Meter un
 * quinto cambiaria la geometria de ui_digi.c para todos los modos, que es
 * mucho mas riesgo que un toque en una zona de 800x300 que no hace otra
 * cosa. El renglon de titulos lo dice.
 *
 * NO GASTA SRAM. El mapa se dibuja directamente sobre la banda que gfx2
 * esta componiendo y la costa vive en la flash. Cuando se escribio esto
 * quedaban 308 bytes libres, asi que no era opcional.
 */


/* El hueco del mapa: el panel de FT8 menos su cabecera de botones. */
#define FT8_MAPA_Y (int16_t)(FT8_PANEL_Y + UDG_HDR_H)
#define FT8_MAPA_H (int16_t)(FT8_PANEL_H - UDG_HDR_H)

/* La caja, colocada la primera vez y conservada despues. Ver s_mapa_caja. */
static mapa_caja_t *mapa_caja(void)
{
    if (!s_mapa_puesta) {
        s_mapa_puesta = 1U;
        mapa_pon(&s_mapa_caja, 0, FT8_MAPA_Y, (int16_t)MAIN_W, FT8_MAPA_H);
    }
    return &s_mapa_caja;
}

static void ft8_mapa_pinta(gfx2_surf_t *s, void *ctx)
{
    const mapa_caja_t *cp = mapa_caja();
    mapa_caja_t caja = *cp;
    int16_t px, py;

    (void)ctx;
    /*
     * EL MAPA OCUPA EL HUECO ENTERO - 28/09/2026, por el dueño: "ahora
     * mismo deja unas franjas laterales".
     *
     * Ya no hay margenes que rellenar: la caja ES el hueco. El relleno se
     * queda porque el mapa recorta su propio dibujo a la caja y las
     * esquinas de los botones de zoom son redondeadas.
     */
    gfx2_fill(s, 0, FT8_MAPA_Y, (int16_t)MAIN_W, FT8_MAPA_H, gfx2_rgb(0x06101AUL));
    mapa_dibuja(s, &caja);

    /*
     * EL MISMO MAPA PARA LOS TRES MODOS, y lo unico que cambia es de donde
     * salen las posiciones:
     *
     *   FT8    del LOCALIZADOR que viene en el mensaje. Es un cuadrado de
     *          150 km de lado, asi que se pinta su centro.
     *   WSPR   igual, de la columna "Loc." de la baliza.
     *   JTTY   igual, del atomo que lo traiga dentro del mensaje.
     *   HFDL   de la POSICION del avion, en grados por diez mil. Son
     *          coordenadas de verdad, medidas por el propio avion.
     *   AIS    igual, del barco.
     *
     * Y por eso cada uno va en un color distinto: no es decoracion, es que
     * un punto de FT8 significa "por aqui cerca" y uno de AIS significa
     * "aqui".
     *
     *     FT8   verde      WSPR  rojo claro    JTTY  violeta
     *     HFDL  ambar      AIS   azul
     */
    /*
     * EL ORDEN DE LOS RENGLONES NO ES EL MISMO EN TODOS, Y AQUI IMPORTA.
     *
     * *** Descubierto el 28/09 por la tarde al poner el mapa de WSPR: el
     * de JTTY, que puse esta misma mañana, tenia el resalte AL REVES. ***
     *
     * FT8 y WSPR meten el renglon nuevo en el 0 y empujan a los demas,
     * asi que `linea(0)` es EL MAS NUEVO y sus paneles le dan la vuelta
     * para pintar de viejo a nuevo. JTTY no: mete el nuevo AL FINAL y su
     * panel lo pinta en orden, asi que ahi `linea(0)` es EL MAS VIEJO.
     *
     * Yo copie el bucle de FT8 a JTTY sin mirar eso, y el resultado era
     * que en JTTY se rellenaban los tres puntos MAS VIEJOS y el mas viejo
     * quedaba encima del mas nuevo. No se ve como un fallo -salen puntos,
     * y los que salen estan bien puestos- sino como que el resalte no
     * significa nada.
     *
     * Por eso los tres bucles van ahora por RANGO DE ANTIGUEDAD y no por
     * indice: `r` = 1 es el mas nuevo, pase lo que pase con el orden de
     * dentro. Del mas viejo al mas nuevo, y el mas nuevo ENCIMA, que es lo
     * que hace que cuando dos estaciones caen en el mismo cuadrado se vea
     * lo de ahora.
     */
    if (ft8_modo_activo()) {
        uint8_t n = 0U, r;

        while (n < (uint8_t)FT8_MODO_LINEAS
               && ft8_modo_linea(n)[0] != '\0') { n++; }
        for (r = n; r > 0U; r--) {
            char loc[8];
            /* FT8: linea(0) es la mas nueva, o sea indice = rango - 1. */
            const char *l = ft8_modo_linea((uint8_t)(r - 1U));

            if (!mapa_loc_de_linea(l, loc)) { continue; }
            if (!mapa_loc_a_px(&caja, loc, &px, &py)) { continue; }
            mapa_marca(s, &caja, px, py, (uint8_t)(r <= 3U),
                       gfx2_rgb((r <= 3U) ? 0x46E0A0UL : 0x2E8C68UL));
        }
    } else if (hfdl_modo_activo()) {
        uint32_t n = hfdl_modo_aviones();
        uint32_t i;

        for (i = n; i > 0UL; i--) {
            int32_t la, lo;

            if (!hfdl_modo_pos(i - 1UL, &la, &lo)) { continue; }
            if (!mapa_e4_a_px(&caja, lo, la, &px, &py)) { continue; }
            /* rec[0] es el que ha hablado mas recientemente */
            mapa_marca(s, &caja, px, py, (uint8_t)(i <= 3UL),
                       gfx2_rgb((i <= 3UL) ? 0xFFB63CUL : 0xA8762AUL));
        }
    } else if (ais_modo_activo()) {
        uint32_t n = (uint32_t)ais_modo_barcos();
        uint32_t i;

        for (i = n; i > 0UL; i--) {
            int32_t la, lo;

            if (!ais_modo_pos(i - 1UL, &la, &lo)) { continue; }
            if (!mapa_e4_a_px(&caja, lo, la, &px, &py)) { continue; }
            mapa_marca(s, &caja, px, py, (uint8_t)(i <= 3UL),
                       gfx2_rgb((i <= 3UL) ? 0x5AC8FFUL : 0x357C9EUL));
        }
    } else if (wspr_modo_activo()) {
        /*
         * WSPR, del localizador de la baliza - 28/09/2026, por el dueño:
         * "wspr no tiene mapa".
         *
         * Aqui el localizador tiene COLUMNA PROPIA -las de WSPR son Hora,
         * Indicativo, Loc., dBm, Desvio y Cal.-, o sea el campo 3, y no hay
         * que buscarlo por el mensaje como en JTTY. Cuando el mensaje es de
         * los que no llevan localizador la columna viene vacia y
         * mapa_loc_de_campo() devuelve 0, que es lo que hace falta: ese
         * renglon simplemente no pone punto.
         *
         * Los localizadores de WSPR son de CUATRO caracteres, o sea un
         * cuadrado de unos 150 km: el punto dice "por esta zona", igual que
         * en FT8 y por lo mismo.
         */
        uint8_t n = (uint8_t)wspr_modo_lineas(), r;

        if (n > (uint8_t)WSPR_MODO_LINEAS) { n = (uint8_t)WSPR_MODO_LINEAS; }
        for (r = n; r > 0U; r--) {
            char loc[8];
            /* WSPR: linea(0) es la mas nueva, como FT8. */
            const char *l = wspr_modo_linea((uint32_t)(r - 1U));

            if (!mapa_loc_de_campo(l, 3U, loc)) { continue; }
            if (!mapa_loc_a_px(&caja, loc, &px, &py)) { continue; }
            mapa_marca(s, &caja, px, py, (uint8_t)(r <= 3U),
                       gfx2_rgb((r <= 3U) ? 0xFF8A7AUL : 0xA8564BUL));
        }
    } else if (jtty_modo_activo()) {
        /*
         * JTTY, del localizador que venga en el mensaje - 28/09/2026, por
         * el dueño: "y ademas tendra mapa digo yo".
         *
         * Dos diferencias con FT8, y las dos importan:
         *
         *   el CAMPO es el cuarto, no el quinto: las columnas de JTTY son
         *   Hora, Desvio, Cal. y Mensaje;
         *
         *   y el localizador puede estar en CUALQUIER palabra, porque un
         *   mensaje son varios atomos pegados y el localizador viene en el
         *   suyo. Ver mapa_loc_de_campo().
         *
         * En su propio color, como los otros tres: un punto de JTTY
         * significa lo mismo que uno de FT8 -"por aqui cerca", que es lo
         * que dice un cuadrado de localizador- pero de otro modo.
         */
        uint8_t n = jtty_modo_lineas(), r;

        if (n > (uint8_t)JTTY_MODO_LINEAS) { n = (uint8_t)JTTY_MODO_LINEAS; }
        for (r = n; r > 0U; r--) {
            char loc[8];
            /*
             * JTTY AL CONTRARIO: linea(0) es la MAS VIEJA, o sea que el
             * indice del rango r es n - r. Aqui estaba el fallo -copie el
             * `r - 1` de FT8- y por eso se rellenaban los tres mas viejos.
             */
            const char *l = jtty_modo_linea((uint8_t)(n - r));

            if (!mapa_loc_de_campo(l, 4U, loc)) { continue; }
            if (!mapa_loc_a_px(&caja, loc, &px, &py)) { continue; }
            mapa_marca(s, &caja, px, py, (uint8_t)(r <= 3U),
                       gfx2_rgb((r <= 3U) ? 0xC89AFFUL : 0x7A5CA8UL));
        }
    } else {
        /* nadie mas tiene mapa */
    }

    /*
     * Y nosotros, en todos. Sin esto el mapa no dice de donde salen los
     * saltos - y en AIS, ademas, es lo que da la escala: si los barcos
     * salen pegados al aspa, se esta oyendo el puerto de al lado.
     */
    {
        const char *mio = ft8_decoder_get_own_grid();

        if (mio != 0 && mio[0] != '\0' && mapa_loc_a_px(&caja, mio, &px, &py)) {
            mapa_casa(s, &caja, px, py);
        }
    }

    /* Y los dos mandos del zoom, los ultimos: van ENCIMA de todo lo
     * demas, que es lo que los hace legibles sobre cualquier cosa. Los
     * dibuja mapa.c, que es tambien quien dice donde estan cuando llega un
     * toque - ver mapa_zoom_hit()-. */
    mapa_zoom_botones(s, &caja);
}

static void ft8_mapa_draw(void)
{
    gfx2_render(0, FT8_MAPA_Y, (int16_t)MAIN_W, FT8_MAPA_H, ft8_mapa_pinta, 0);
}

/*
 * PINTAR EL PANEL: la lista, o el mapa si esta puesto.
 *
 * Esta en una funcion y no repetido en cada modo porque son CINCO sitios
 * -FT8, HFDL, WSPR, AIS y ALE- y de esos cinco solo tres tienen mapa. Una
 * lista escrita a mano en cinco sitios es exactamente lo que fallo con
 * digi_panel_active() el 28/09 por la mañana: se añadieron WSPR, AIS y
 * ALE a la tabla de modos y a la lista de al lado no, tres veces
 * seguidas, y el panel no cambiaba al elegirlos.
 */
/*
 * QUIEN TIENE MAPA, EN UN SOLO SITIO - 28/09/2026.
 *
 * *** Por el dueño del proyecto: "wspr no tiene mapa". ***
 *
 * Y la tenia escrita A MANO EN DOS SITIOS: aqui, para decidir si se pinta
 * el mapa o la lista, y en el despacho del toque, para decidir si un dedo
 * en esa zona es un arrastre del mapa. Añadir WSPR habria sido acordarse
 * de los dos.
 *
 * Eso ya fallo dos veces en este mismo panel -`digi_panel_active()` con
 * WSPR, AIS y ALE el 28 por la mañana, y `solo_chip()` borrando el boton
 * del mapa tres veces seguidas-, asi que en vez de añadir un modo a dos
 * listas se quita una de las dos.
 *
 * Lo que decide quien tiene mapa es si se le puede sacar una POSICION a lo
 * que decodifica: FT8, WSPR y JTTY traen localizador en el mensaje, y HFDL
 * y AIS traen coordenadas de verdad. ALE y los demas no traen nada.
 */
static uint8_t modo_tiene_mapa(void)
{
    return (uint8_t)(ft8_modo_activo() || hfdl_modo_activo()
                     || ais_modo_activo() || jtty_modo_activo()
                     || wspr_modo_activo());
}

static void panel_pinta(void)
{
    if (s_ft8_mapa && modo_tiene_mapa()) {
        uint8_t k;

        /*
         * La cabecera se pinta IGUAL -la barra, el boton de frecuencias y
         * el de la siguiente siguen haciendo falta-, asi que se le dan a
         * ui_digi los renglones vacios y el mapa se dibuja despues, solo
         * en el hueco que dejan.
         */
        for (k = 0U; k < (uint8_t)UDG_ROWS_MAX; k++) { s_digi.fila[k] = 0; }
        /*
         * SIN RENGLON DE TITULOS, y no por ahorrar - 30/09/2026.
         *
         * Aqui habia esto:
         *
         *     s_digi.titulos = tr("toca el mapa para volver a la lista",
         *                         "tap the map to go back to the list");
         *
         * y no se ha visto NUNCA. ui_digi pinta los titulos en
         * `st->y + UDG_HDR_H`, o sea en FT8_PANEL_Y + 30, y el mapa empieza
         * en FT8_MAPA_Y, que es FT8_PANEL_Y + UDG_HDR_H: EL MISMO PIXEL. La
         * linea de abajo lo tapa entero, los 18 px de alto, desde el primer
         * repintado. Un rotulo tapado no se nota como fallo: no sale nada
         * raro, simplemente no sale.
         *
         * Y encima ya era mentira. Cuando se escribio, el mapa se ponia y se
         * quitaba TOCANDOLO. Luego aparecio el boton (btn5, "Mapa") y esta
         * zona se dedico a arrastrar el mapa con el dedo, que es lo que pidio
         * el dueño; desde entonces tocar el mapa no vuelve a la lista, lo
         * mueve. O sea que el unico texto que explicaba como salir explicaba
         * algo que ya no pasa - y por suerte no se veia.
         *
         * Se quita en vez de hacerlo visible porque hacerlo visible cuesta
         * los 18 px de alto del renglon, y el mapa ocupa el hueco entero
         * justo porque lo pidio el dueño el 28/09 ("ahora mismo deja unas
         * franjas laterales"). Quien avisa de como se sale es el propio
         * boton "Mapa", que se queda pulsado mientras el mapa esta puesto
         * (ver btn5_press).
         *
         * tools/tapado_check.py vigila que no vuelva a aparecer.
         */
        s_digi.titulos = 0;
        /*
         * SOLO LA CABECERA, no el panel entero - 30/09/2026.
         *
         * Aqui habia ui_digi_draw_texto(), que pinta la cabecera Y el cuerpo
         * con los ocho renglones vacios; y justo despues el mapa repintaba
         * ese cuerpo entero. 800 x 318 mas 800 x 288 = 484.800 pixeles por
         * el bus para los 254.400 que hacen falta: el 47,5 % del trabajo de
         * cada cuadro del mapa era tirarlo. No se ve como un fallo -sale lo
         * correcto- pero se arrastra el mapa mas despacio de lo que deberia.
         */
        ui_digi_draw_cabecera(&s_digi);
        ft8_mapa_draw();
    } else {
        ui_digi_draw_texto(&s_digi);   /* cabecera incluida */
    }
}

static void ft8_panel_draw(void)
{
    uint32_t total;
    uint8_t  seg;

    /*
     * HFDL COMPARTE ESTE DIBUJADO, y por eso sale antes de tocar nada de
     * FT8: ft8_modo_total() y ft8_modo_segundo() no valen aqui -uno cuenta
     * mensajes de FT8 y el otro el segundo dentro de la ranura, y HFDL no
     * tiene ranuras-.
     *
     * *** Por el dueno del proyecto: "borrar no borra". *** Y no borraba
     * porque el boton SI se detectaba y hfdl_modo_borra() SI vaciaba la
     * lista: lo que no pasaba era el repintado. Este dibujado solo repinta
     * cuando cambia el total, y borrar hace que el total BAJE a cero, que
     * tambien es un cambio... pero es que ni siquiera se llegaba aqui,
     * porque el despacho del bucle preguntaba solo por FT8.
     *
     * Dos sitios que habia que acordarse de tocar y de los que me acorde de
     * ninguno. Ahora los dos preguntan por los dos modos.
     */
    /*
     * WSPR, y aqui el "cuanto ha cambiado" es OTRO. Este dibujado repinta
     * la lista cuando cambia un total y la cabecera cuando cambia un
     * segundo; en WSPR el total sube como mucho una vez cada dos minutos,
     * pero la cuenta atras de la barra cambia CADA SEGUNDO y es lo unico
     * que se mueve en toda la pantalla. Si no se repinta, la pantalla
     * parece congelada justo durante los dos minutos en que WSPR no puede
     * hacer nada - que es exactamente cuando hace falta que se note que
     * esta viva.
     *
     * Por eso el "segundo visto" de aqui son los segundos que faltan, y no
     * una fase ni un segundo de ranura.
     */
    /*
     * AIS: el total que manda el repintado es el de TRAMAS BUENAS, no el de
     * renglones. Los renglones casi no cambian -un barco que ya esta en la
     * tabla sigue ahi- pero su CONTENIDO si, cada vez que llega una posicion
     * nueva. Mirando el numero de renglones, la pantalla se quedaria
     * congelada con las posiciones de hace cinco minutos.
     */
    /*
     * JTTY: lo que manda el repintado es la cuenta de ATOMOS, no la de
     * renglones. Un mensaje largo son hasta dieciseis atomos que no se
     * enseñan hasta que llega el ultimo, asi que mirando los renglones la
     * pantalla se quedaria quieta treinta segundos con la radio
     * decodificando a toda maquina.
     */
    if (jtty_modo_activo()) {
        uint32_t t = jtty_modo_atomos();

        digi_sync();

        if (s_ft8_todo || (t != s_ft8_total_visto)) {
            s_ft8_todo = 0U;
            s_ft8_total_visto = t;
            panel_pinta();
        }
        return;
    }

    if (ale_modo_activo()) {
        uint32_t t = ale_modo_palabras();
        uint8_t  e = ale_modo_calidad();

        digi_sync();

        if (s_ft8_todo || (t != s_ft8_total_visto)) {
            s_ft8_todo = 0U;
            s_ft8_total_visto = t;
            s_ft8_seg_visto = e;
            panel_pinta();
            return;
        }
        if (e != s_ft8_seg_visto) {
            s_ft8_seg_visto = e;
            ui_digi_draw_barra(&s_digi);
            ui_digi_draw_chip(&s_digi);
        }
        return;
    }

    if (ais_modo_activo()) {
        uint32_t t = ais_modo_tramas();
        uint8_t  e = ais_modo_senal();

        digi_sync();

        if (s_ft8_todo || (t != s_ft8_total_visto)) {
            s_ft8_todo = 0U;
            s_ft8_total_visto = t;
            s_ft8_seg_visto = e;
            panel_pinta();
            return;
        }
        if (e != s_ft8_seg_visto) {
            s_ft8_seg_visto = e;
            ui_digi_draw_barra(&s_digi);
            ui_digi_draw_chip(&s_digi);
        }
        return;
    }

    if (wspr_modo_activo()) {
        uint32_t t = wspr_modo_lineas();
        uint8_t  e = (uint8_t)(wspr_modo_capturando()
                               ? wspr_modo_por_ciento()
                               : (wspr_modo_espera_s() & 0x7FU));

        digi_sync();

        if (s_ft8_todo || (t != s_ft8_total_visto)) {
            s_ft8_todo = 0U;
            s_ft8_total_visto = t;
            s_ft8_seg_visto = e;
            panel_pinta();
            return;
        }
        if (e != s_ft8_seg_visto) {
            s_ft8_seg_visto = e;
            ui_digi_draw_barra(&s_digi);
            ui_digi_draw_chip(&s_digi);
        }
        return;
    }

    if (hfdl_modo_activo()) {
        uint32_t t = hfdl_modo_total();
        uint8_t  e = hfdl_modo_estado();

        digi_sync();

        if (s_ft8_todo || (t != s_ft8_total_visto)) {
            s_ft8_todo = 0U;
            s_ft8_total_visto = t;
            s_ft8_seg_visto = e;
            panel_pinta();
            return;
        }
        /* La fase del preambulo se mueve mucho mas que la lista: cuando solo
         * cambia ella se repintan la barra y la chapa, no las catorce
         * lineas. Es lo mismo que hace FT8 con el segundo de la ranura. */
        if (e != s_ft8_seg_visto) {
            s_ft8_seg_visto = e;
            ui_digi_draw_barra(&s_digi);
            ui_digi_draw_chip(&s_digi);
        }
        return;
    }

    total = ft8_modo_total();
    seg   = ft8_modo_segundo();

    digi_sync();

    if (s_ft8_todo || (total != s_ft8_total_visto)) {
        s_ft8_todo = 0U;
        s_ft8_total_visto = total;
        s_ft8_seg_visto = seg;
        panel_pinta();
        return;
    }

    if (seg != s_ft8_seg_visto) {
        s_ft8_seg_visto = seg;
        ui_digi_draw_barra(&s_digi);
        ui_digi_draw_chip(&s_digi);
    }
}

/* Pasa la rejilla al estado que entiende ui_digi.c. Una sola funcion para
 * que no haya dos sitios decidiendo que renglon va donde. */
static void digi_sync(void)
{
    uint8_t r;

    /*
     * FT8 se sale de la forma de los demas y por eso sale primero: panel
     * entero, sin botones y con barra en vez de ellos. Ver FT8_PANEL_H.
     */
    /*
     * HFDL usa el mismo panel entero que FT8 y por la misma razon: lo que
     * sale es una lista que crece, no dos lineas de texto corrido. Va antes
     * que FT8 solo porque los dos no pueden estar a la vez y el orden da
     * igual; lo que no da igual es que ninguno caiga en la rama de RTTY.
     */
    /*
     * WSPR va PRIMERO por el mismo motivo por el que HFDL va antes que FT8:
     * los tres usan el panel entero, los tres son exclusivos entre si, y lo
     * unico que importa es que ninguno acabe cayendo en la rama de RTTY.
     */
    /*
     * AIS va antes que los tres de arriba, y el orden entre los cuatro sigue
     * dando igual: son exclusivos. Lo que no da igual es que ninguno caiga
     * en la rama de RTTY del final.
     */
    /*
     * JTTY va con los demas del panel entero y el orden sigue dando igual
     * por lo mismo: son exclusivos entre si. Lo unico que importa es que no
     * caiga en la rama de RTTY del final.
     */
    if (jtty_modo_activo()) {
        uint8_t n = (uint8_t)(FT8_PANEL_FILAS - 1U);
        uint8_t k, hay = 0U;

        if (n > (uint8_t)JTTY_MODO_LINEAS) { n = (uint8_t)JTTY_MODO_LINEAS; }

        s_digi.y     = (int16_t)FT8_PANEL_Y;
        s_digi.h     = (int16_t)FT8_PANEL_H;
        s_digi.filas = n;
        s_digi.btn   = tr("Borrar", "Clear");
        s_digi.btn2  = 0;
        s_digi.barra_w = 0;
        /*
         * NI BARRA NI CUENTA ATRAS, y esta vez no es que falte: es que no
         * hay nada que contar. La barra de FT8 y la de WSPR dicen cuanto
         * queda de ranura, y aqui no hay ranuras (ver k_demod_modes[]). Lo
         * que dice que esto esta vivo es que suba la cuenta de la chapa.
         */
        s_digi.barra_on = 0U;
        s_digi.btn3    = 0;
        s_digi.btn4    = jtty_frec_rotulo();
        s_digi.btn5       = tr("Mapa", "Map");
        s_digi.btn5_press = s_ft8_mapa;
        /*
         * CON CEBRA desde el 28/09 por la tarde. *** Por el dueño del
         * proyecto: "y ponle cebreado", justo despues de meter la columna
         * del pais. ***
         *
         * Y es el mismo motivo que en HFDL y AIS, que ya la tienen: con
         * cuatro columnas la vista iba del mensaje a la hora sin perderse,
         * pero con cinco -y la ultima pegada al otro extremo de la
         * pantalla- hay 496 px de mensaje entre el "Cal." y el pais. Leer
         * de que pais es un mensaje que esta medio metro a la izquierda es
         * justo donde el ojo se salta de renglon.
         */
        s_digi.cebra   = 1U;
        s_digi.cols    = ui_digi_cols_jtty;
        s_digi.ncols   = UI_DIGI_COLS_JTTY_N;
        s_digi.titulos = tr("Hora\tDesvío\tCal.\tMensaje\tPaís",
                            "Time\tOffset\tQual.\tMessage\tCountry");

        while ((hay < n) && (hay < jtty_modo_lineas())) { hay++; }
        for (k = 0U; k < hay; k++) {
            s_digi.fila[k] = jtty_modo_linea(k);
        }
        for (k = hay; k < (uint8_t)UDG_ROWS_MAX; k++) { s_digi.fila[k] = 0; }

        s_digi.chip = jtty_modo_estado_txt();
        s_digi.enganchado = (uint8_t)(jtty_modo_atomos() != 0UL);
        return;
    }

    if (ale_modo_activo()) {
        uint8_t n = (uint8_t)(FT8_PANEL_FILAS - 1U);
        uint8_t k, hay = 0U;

        if (n > (uint8_t)ALE_MODO_LINEAS) { n = (uint8_t)ALE_MODO_LINEAS; }

        s_digi.y     = (int16_t)FT8_PANEL_Y;
        s_digi.h     = (int16_t)FT8_PANEL_H;
        s_digi.filas = n;
        s_digi.btn   = tr("Borrar", "Clear");
        s_digi.btn2  = 0;
        s_digi.barra_w = 0;
        s_digi.btn3    = 0;
        s_digi.btn4    = 0;   /* ALE no tiene canales fijos: ver k_demod_modes[] */
        s_digi.btn5    = 0;   /* sin mapa: ver btn5 en ui_digi.h */
        s_digi.cebra   = 0U;  /* cinco columnas separadas y pocas filas */
        s_digi.cols    = ui_digi_cols_ale;
        s_digi.ncols   = UI_DIGI_COLS_ALE_N;
        s_digi.titulos = tr("Indicativo\tTipo\tkHz\tCal.\tN",
                            "Callsign\tType\tkHz\tQual.\tN");

        /* Tabla, no lista: el orden es el de llegada y no se toca, igual
         * que en AIS y por la misma razon. */
        while ((hay < n) && (hay < ale_modo_lineas())) { hay++; }
        for (k = 0U; k < hay; k++) {
            s_digi.fila[k] = ale_modo_linea((uint32_t)k);
        }
        for (k = hay; k < (uint8_t)UDG_ROWS_MAX; k++) { s_digi.fila[k] = 0; }

        ale_chapa();
        return;
    }

    if (ais_modo_activo()) {
        uint8_t n = (uint8_t)(FT8_PANEL_FILAS - 1U);
        uint8_t k, hay = 0U;

        if (n > (uint8_t)AIS_MODO_LINEAS) { n = (uint8_t)AIS_MODO_LINEAS; }

        s_digi.y     = (int16_t)FT8_PANEL_Y;
        s_digi.h     = (int16_t)FT8_PANEL_H;
        s_digi.filas = n;
        s_digi.btn   = tr("Borrar", "Clear");
        s_digi.btn2  = 0;
        s_digi.barra_w = 0;      /* entera: dentro va la cuenta de barcos */
        s_digi.btn3    = 0;      /* no hay lista de frecuencias que abrir */
        s_digi.btn4    = ais_frec_rotulo();
        /* El mapa: AIS es uno de los tres que lo tienen. Ver btn5. */
        s_digi.btn5       = tr("Mapa", "Map");
        s_digi.btn5_press = s_ft8_mapa;
        /*
         * CON CEBRA, como HFDL y al reves que WSPR. Aqui si hacen falta: son
         * seis columnas anchas, catorce filas, y la que mas se lee -el
         * nombre- esta en medio. Sin fondo alterno el ojo salta de renglon
         * entre el nombre y la posicion.
         */
        s_digi.cebra   = 1U;
        s_digi.cols    = ui_digi_cols_ais;
        s_digi.ncols   = UI_DIGI_COLS_AIS_N;
        /*
         * EL CANAL VA EN LA CABECERA, como la estacion en HFDL: estas en UNO
         * de los dos, no en los dos, y ponerlo en cada fila gastaria una
         * columna para decir siempre lo mismo.
         */
        s_digi.titulos = (ais_modo_canal() == 0U)
                         ? tr("MMSI\tNombre\tPosición\tVel\tRumbo\tN  (canal A)",
                              "MMSI\tName\tPosition\tSpd\tCourse\tN  (channel A)")
                         : tr("MMSI\tNombre\tPosición\tVel\tRumbo\tN  (canal B)",
                              "MMSI\tName\tPosition\tSpd\tCourse\tN  (channel B)");

        /*
         * SIN DARLE LA VUELTA, al reves que en los otros tres. Aqui no es
         * una lista que crece sino una TABLA: cada renglon es un barco y se
         * queda en su sitio mientras siga ahi. Invertirla haria que un barco
         * nuevo empujara a todos los demas una fila hacia abajo, y leer una
         * tabla que se mueve sola no hay quien lo haga.
         */
        while ((hay < n) && (hay < ais_modo_lineas())) { hay++; }
        for (k = 0U; k < hay; k++) {
            s_digi.fila[k] = ais_modo_linea((uint32_t)k);
        }
        for (k = hay; k < (uint8_t)UDG_ROWS_MAX; k++) { s_digi.fila[k] = 0; }

        ais_chapa();
        return;
    }

    if (wspr_modo_activo()) {
        uint8_t n = (uint8_t)(FT8_PANEL_FILAS - 1U);
        uint8_t k, hay = 0U;

        if (n > (uint8_t)WSPR_MODO_LINEAS) { n = (uint8_t)WSPR_MODO_LINEAS; }

        s_digi.y     = (int16_t)FT8_PANEL_Y;
        s_digi.h     = (int16_t)FT8_PANEL_H;
        s_digi.filas = n;
        s_digi.btn   = tr("Borrar", "Clear");
        s_digi.btn2  = 0;
        /*
         * LA BARRA VA ENTERA, COMO LA DE FT8, y el cuarto boton en la franja
         * que sobra hasta la chapa. No se acorta como la de HFDL por dos
         * motivos. El de fuera: lo que hay que leer aqui es el TEXTO de
         * dentro -"empieza en 1:47"-, no la posicion del relleno, y ese
         * texto necesita sitio.
         *
         * El de dentro: acortar la barra SIN poner tercer boton deja 130 px
         * de la franja vieja que ya no repinta nadie -btn3_w() devuelve 0 si
         * no hay btn3, y el ancho de la banda que se repinta sale de ahi-,
         * o sea restos de "Frec." al venir de HFDL. Esa pareja
         * (barra corta + sin btn3) no la usa ningun otro panel y no hay por
         * que estrenarla aqui: barra entera + btn4 es justo lo que hace FT8
         * y esta probado.
         */
        s_digi.barra_w = 0;
        s_digi.btn3    = 0;      /* no hay lista de frecuencias que abrir */
        s_digi.btn4    = wspr_frec_rotulo();
        /*
         * EL MAPA. *** Por el dueño del proyecto: "wspr no tiene mapa". ***
         * Y lo pedia a gritos: de los cinco modos con panel, WSPR es el que
         * mas util lo tiene. Lo que se escucha son balizas de un vatio o
         * menos, asi que la pregunta al mirar la lista no es quien ha
         * hablado sino DESDE DONDE se ha oido -y un renglon "IO91" no
         * contesta a eso hasta que te aprendes la rejilla-.
         */
        s_digi.btn5       = tr("Mapa", "Map");
        s_digi.btn5_press = s_ft8_mapa;
        /*
         * CON CEBRA desde el 28/09 por la tarde. *** Por el dueño del
         * proyecto, mirando su radio: "de hecho creo que te falta cebreado
         * tambien en wspr". ***
         *
         * Aqui ponia "SIN CEBRA" con este razonamiento: "en HFDL son seis
         * columnas apretadas y catorce filas, y el ojo pierde el renglon;
         * aqui son seis columnas muy separadas y doce filas como mucho,
         * CASI SIEMPRE MENOS DE CINCO. Pintar el fondo alterno sobre una
         * tabla de tres lineas es decoracion".
         *
         * Lo de "casi siempre menos de cinco" me lo invente. No mire nunca
         * cuantas balizas salen de verdad en un ciclo, y en 20 m pasan de
         * diez sin despeinarse. Y justamente porque las columnas estan muy
         * separadas -el indicativo en 110 y la calidad en 500- es donde mas
         * falta hace: son 390 px de blanco entre un dato y el siguiente.
         *
         * El que tiene la radio delante lo ve y yo no. Se queda anotado
         * porque el fallo no era el gusto: era razonar sobre un numero
         * -cuantas filas hay- que no habia medido.
         */
        s_digi.cebra   = 1U;
        s_digi.cols    = ui_digi_cols_wspr;
        s_digi.ncols   = UI_DIGI_COLS_WSPR_N;
        s_digi.titulos = tr("Hora\tIndicativo\tLoc.\tdBm\tDesvío\tCal.",
                            "Time\tCallsign\tLoc.\tdBm\tOffset\tQual.");

        /* Como en FT8 y HFDL: contar los que hay y pegarlos ARRIBA. El
         * fallo de "el texto ha aparecido abajo del todo" se arregla una
         * vez y se copia. */
        while (hay < n && hay < wspr_modo_lineas()) { hay++; }
        /* wspr_modo_linea(0) es el MAS NUEVO y el panel pinta de viejo a
         * nuevo, asi que hay que darle la vuelta - igual que HFDL. Los tres
         * paneles crecen hacia abajo; tener uno al reves seria una
         * sorpresa gratuita. */
        for (k = 0U; k < hay; k++) {
            s_digi.fila[k] = wspr_modo_linea((uint32_t)(hay - 1U - k));
        }
        for (k = hay; k < (uint8_t)UDG_ROWS_MAX; k++) { s_digi.fila[k] = 0; }

        wspr_chapa();
        return;
    }

    if (hfdl_modo_activo()) {
        uint8_t n = (uint8_t)(FT8_PANEL_FILAS - 1U);
        uint8_t k, hay = 0U;

        s_digi.y     = (int16_t)FT8_PANEL_Y;
        s_digi.h     = (int16_t)FT8_PANEL_H;
        s_digi.filas = n;
        s_digi.btn   = tr("Borrar", "Clear");
        s_digi.btn2  = 0;
        /*
         * La barra de HFDL solo tiene cuatro posiciones, asi que con 170 px
         * se lee igual de bien que con 300 - y los 130 que sobran son justo
         * el hueco donde cabe el boton de frecuencias, pegado a la chapa.
         * Ver btn3 en ui_digi.h.
         */
        s_digi.barra_w = 170;
        s_digi.btn3    = tr("Frec.", "Freq.");
        s_digi.btn4    = hfdl_frec_rotulo();
        s_digi.btn5       = tr("Mapa", "Map");
        s_digi.btn5_press = s_ft8_mapa;
        /*
         * *** El dueno: "en hfdl cada fila un color para que se lea
         * bien". *** Seis columnas y hasta catorce filas: el ojo pierde el
         * renglon a mitad de camino entre la direccion y el evento. Fondo
         * alterno, que es lo que hace cualquier tabla larga - y no tinta
         * alterna, porque eso se lee como "estas filas importan mas".
         */
        s_digi.cebra   = 1U;
        s_digi.cols    = ui_digi_cols_hfdl;
        s_digi.ncols   = UI_DIGI_COLS_HFDL_N;
        /*
         * LA ESTACION VA EN LA CABECERA, NO EN CADA FILA. 25/09/2026.
         *
         * Igual que en la pantalla de Esteban, que pone "GS 3 REYKJAVIK"
         * arriba: estas en UNA frecuencia, o sea en UNA estacion, y
         * repetirla en las catorce filas gastaba una columna entera para
         * decir siempre lo mismo. Ese sitio se lo queda ahora la posicion.
         */
        {
            static char tit[64];
            const char *gs = hfdl_modo_estacion();
            uint8_t i = 0U, c;
            const char *base = tr("OACI\tModelo\tVuelo\tPosición\t",
                                  "ICAO\tModel\tFlight\tPosition\t");

            while (base[i] != '\0') { tit[i] = base[i]; i++; }
            if (gs[0] != '\0') {
                tit[i++] = '>'; tit[i++] = ' ';
                for (c = 0U; gs[c] != '\0' && i < 52U; c++) { tit[i++] = gs[c]; }
            } else {
                {
                    /* "Ultimo" / "Latest", copiado con un bucle como el de
                     * arriba. Escrito letra a letra no se podia traducir y
                     * ademas era invisible a cualquier busqueda de cadenas. */
                    const char *u = tr("Ultimo", "Latest");
                    for (c = 0U; u[c] != '\0' && i < 52U; c++) { tit[i++] = u[c]; }
                }
            }
            tit[i++] = '\t'; tit[i++] = 'N'; tit[i] = '\0';
            s_digi.titulos = tit;
        }

        /* Igual que FT8: contar primero los que hay y pegarlos arriba. El
         * fallo de "el texto ha aparecido abajo del todo" se arregla una vez
         * y se copia, no se vuelve a cometer. */
        while (hay < n && hay < hfdl_modo_lineas()) { hay++; }
        for (k = 0U; k < hay; k++) {
            s_digi.fila[k] = hfdl_modo_linea((uint32_t)(hay - 1U - k));
        }
        for (k = hay; k < (uint8_t)UDG_ROWS_MAX; k++) { s_digi.fila[k] = 0; }

        hfdl_apunta_aciertos();
        hfdl_chapa();
        return;
    }

    if (ft8_modo_activo()) {
        /* Un renglon menos: el de los titulos se come uno. */
        uint8_t n = (uint8_t)(FT8_PANEL_FILAS - 1U);
        uint8_t k;

        if (n > (uint8_t)FT8_MODO_LINEAS) { n = (uint8_t)FT8_MODO_LINEAS; }

        s_digi.y     = (int16_t)FT8_PANEL_Y;
        s_digi.h     = (int16_t)FT8_PANEL_H;
        s_digi.filas = n;
        s_digi.btn   = tr("Borrar", "Clear");
        s_digi.btn2  = 0;   /* su hueco lo ocupa la barra de la ranura */
        /*
         * *** El dueno: "hay que poner un boton de frec en el ft8 igual
         * que el del hfdl". ***
         *
         * En HFDL el hueco salia de acortar la barra, porque aquella solo
         * tiene cuatro posiciones. La de FT8 cuenta los 15 segundos de la
         * ranura y se queda entera -acortarla seria empeorar el unico
         * dato que da-, asi que este boton va donde el CUARTO de HFDL: en
         * la franja que sobra entre el final de la barra y la chapa. Sale
         * uno en vez de dos, y es el que el dueno pidio para ir probando
         * frecuencias sin abrir listas.
         */
        s_digi.barra_w = 0; /* la de FT8 va entera: cuenta 15 segundos */
        s_digi.btn3    = 0;
        s_digi.btn4    = ft8_frec_rotulo();
        s_digi.btn5       = tr("Mapa", "Map");
        s_digi.btn5_press = s_ft8_mapa;
        s_digi.cebra   = 0U;

        /*
         * Columnas y titulos - 25/09/2026, por el dueno del proyecto: "una
         * cabecera que diga que es cada campo", "algo mas espaciados", "hay
         * mucho hueco a la derecha vacio".
         *
         * Las unidades viven AQUI, en los titulos, y no en cada renglon: un
         * "Hz" repetido dieciseis veces es ruido, y ademas era lo que hacia
         * la linea larga y desordenada. Las x las pone ui_digi.c, que es
         * quien pinta; aqui solo se dicen los nombres.
         */
        s_digi.cols    = ui_digi_cols_ft8;
        s_digi.ncols   = UI_DIGI_COLS_FT8_N;
        s_digi.titulos = tr("Hora\tHz\tdB\tDT\tMensaje\tkm\tPaís",
                            "Time\tHz\tdB\tDT\tMessage\tkm\tCountry");

        /*
         * DE ARRIBA HACIA ABAJO, Y PEGADO ARRIBA - 25/09/2026, por el dueno
         * del proyecto: "el texto ha aparecido abajo del todo".
         *
         * ft8_modo_linea(0) es el MAS NUEVO y el panel pinta de viejo a
         * nuevo, asi que hay que darle la vuelta. Lo que fallaba no era eso
         * sino DONDE empezar: se recorrian los dieciseis renglones del panel
         * aunque solo hubiera siete mensajes, y como los huecos del anillo
         * estan en los indices altos, los siete caian en los siete ultimos
         * renglones. Siete lineas pegadas al fondo con nueve en blanco encima
         * se ve como una pantalla rota, no como una lista que empieza.
         *
         * Asi que primero se cuenta cuantos hay DE VERDAD y se colocan desde
         * arriba. La lista crece hacia abajo hasta llenar el panel y a partir
         * de ahi va corriendo, que es como se comporta el panel de RTTY y lo
         * que espera cualquiera que haya visto un registro.
         */
        {
            uint8_t hay = 0U;
            while (hay < n && ft8_modo_linea(hay)[0] != '\0') { hay++; }

            for (k = 0U; k < hay; k++) {
                const char *l = ft8_modo_linea((uint8_t)(hay - 1U - k));
                s_digi.fila[k] = l;
                /*
                 * *** El dueno: "en el ft8 hay que pintar las filas con cq
                 * de otro color". ***
                 *
                 * Y tiene sentido mas alla de lo bonito: una CQ es la
                 * unica linea a la que se PUEDE contestar. El resto son
                 * conversaciones entre otros dos.
                 *
                 * El mensaje es el ultimo campo del renglon, detras del
                 * ultimo tabulador, asi que se busca ahi y no al principio
                 * -al principio esta la hora-. Se pide "CQ" seguido de
                 * espacio para no marcar un indicativo que empiece por CQ,
                 * que los hay.
                 */
                s_digi.fila_tinta[k] = es_cq(l) ? UDG_T_LLAMA : UDG_T_NORMAL;
            }
            for (k = hay; k < (uint8_t)UDG_ROWS_MAX; k++) {
                s_digi.fila[k] = 0;
                s_digi.fila_tinta[k] = UDG_T_NORMAL;
            }
        }

        ft8_chapa();
        return;
    }

    s_digi.y     = (int16_t)RTTY_TEXT_PANEL_Y;
    s_digi.h     = (int16_t)RTTY_TEXT_PANEL_H;
    s_digi.filas = (uint8_t)RTTY_TEXT_ROWS;
    s_digi.barra_on = 0U;
    s_digi.barra_w  = 0;
    s_digi.btn3     = 0;
    s_digi.btn4     = 0;
    s_digi.btn5     = 0;   /* RTTY, CW y NAVTEX no tienen mapa */
    s_digi.cebra    = 0U;
    /* Sin columnas ni titulos: lo que llega en RTTY, CW y NAVTEX es texto
     * corrido, no una tabla. */
    s_digi.cols    = 0;
    s_digi.ncols   = 0U;
    s_digi.titulos = 0;

    for (r = 0U; r < RTTY_TEXT_ROWS; r++) {
        s_digi.fila[r] = (s_rtty_text_row_len[r] > 0U) ? s_rtty_text_grid[r] : 0;
    }

    s_digi.btn   = tr("Borrar", "Clear");
    s_digi.btn2  = 0;     /* misma razon que en el fax: lo que no se borra se hereda */
    /* btn_press NO se toca aqui: lo pone el reparto de toques, que es quien
     * sabe si el dedo esta encima. digi_sync() corre en cada cuadro y lo
     * borraria en el siguiente. */

    if (navtex_activo()) {
        /*
         * La chapa cuenta EN QUE FASE va el decodificador, y eso importa mas
         * aqui que en los otros modos: el NAVTEX tarda unos segundos en
         * engancharse y despues puede estar minutos sin un solo caracter
         * porque la emisora no esta emitiendo -cada estacion tiene su ranura
         * de diez minutos cada cuatro horas-. Sin esto, "enganchado y
         * callado" y "no se entera de nada" se ven exactamente igual.
         *
         * Cuando ya hay cabecera se enseña ella: esas cuatro letras dicen
         * quien emite y de que va el mensaje, que es lo unico que hay que
         * mirar si solo se va a mirar una cosa.
         */
        navtex_info_t in;
        navtex_info(&in);

        if (in.tiene_cab) {
            /* COPIADA, no apuntada: `in` es una variable local de esta
             * funcion y muere al salir, mientras que s_digi.chip lo lee el
             * dibujante mucho despues. Apuntar ahi es un puntero colgando
             * que casi siempre "funciona" -la pila todavia tiene los bytes-
             * hasta el dia que no. */
            uint8_t i;
            for (i = 0U; i < 5U; i++) { s_digi_chip[i] = in.cab[i]; }
            s_digi_chip[5] = '\0';
            s_digi.chip = s_digi_chip;
        } else if (in.car_sync) {
            s_digi.chip = "SITOR";
        } else if (in.bit_sync) {
            s_digi.chip = "bits";
        } else {
            s_digi.chip = tr("buscando", "searching");
        }
        s_digi.enganchado = in.car_sync;
    } else if (cw_get_enabled()) {
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
    } else if (psk31_activo()) {
        /*
         * LA CHAPA DE PSK31 ES EL ERROR DE FRECUENCIA, no la calidad, y
         * esa eleccion es la que hace el modo usable.
         *
         * PSK31 solo aguanta +-7,8 Hz de desvio: pasado eso, la vuelta de
         * fase de la portadora imita a la de los datos y sale texto con
         * pinta de bueno que no lo es. Y una emision de PSK31 son 31 Hz
         * dentro de una ventana de 3 kHz: a ojo no se encuentra.
         *
         * Asi que aqui va el numero con el que se sintoniza -"-3,4 Hz",
         * se gira hasta que se acerca a cero- y el punto verde de
         * enganchado se enciende con la calidad. Enseñar la calidad en la
         * chapa seria enseñar el sintoma sin decir hacia donde girar.
         */
        uint8_t cal = 0U;
        int16_t e100 = 0;
        int32_t v;
        uint8_t i = 0U;

        psk31_diag(&cal, &e100);
        v = (int32_t)e100;
        if (v < 0) { s_digi_chip[i++] = '-'; v = -v; }
        else       { s_digi_chip[i++] = '+'; }
        if (v > 9999) { v = 9999; }
        if ((v / 1000) != 0) { s_digi_chip[i++] = (char)('0' + (v / 1000)); }
        s_digi_chip[i++] = (char)('0' + ((v / 100) % 10));
        s_digi_chip[i++] = ',';
        s_digi_chip[i++] = (char)('0' + ((v / 10) % 10));
        s_digi_chip[i++] = ' '; s_digi_chip[i++] = 'H'; s_digi_chip[i++] = 'z';
        s_digi_chip[i] = '\0';
        s_digi.chip = s_digi_chip;
        s_digi.enganchado = (uint8_t)(cal >= PSK31_CAL_MIN);
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
/*
 * Y el de PSK31, al mismo panel de texto. Aparte de rtty_poll() y no
 * dentro: son dos anillos distintos y nunca estan los dos encendidos, pero
 * mezclarlos en una sola funcion invita a que un dia uno se quede sin
 * drenar. Sin volcado al UART: PSK31 puede dar treinta caracteres por
 * segundo y eso llenaria el log de ruido.
 */
static void psk31_poll(void)
{
    char c;

    while (psk31_get_char(&c)) { rtty_text_push(c); }
}

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

/* ==========================================================================
 * EL PANEL DEL WEFAX - etapa 26
 * ==========================================================================
 *
 * Lo que hace distinto a este panel de los otros tres: no compone nada en
 * memoria. Cada linea que suelta el decodificador se escribe DIRECTAMENTE en
 * el controlador del panel, que tiene su propia memoria de video y se queda
 * con ella. La radio no guarda la imagen en ningun sitio.
 *
 * Eso no es una optimizacion, es lo que hace que quepa: una carta de
 * superficie reducida a lo que se ve en pantalla son 150 kB de grises, y aqui
 * hay 192 kB de RAM con casi todo ya ocupado. Guardarla era imposible;
 * escribirla no cuesta nada.
 *
 * LO QUE SE PIERDE A CAMBIO: no se puede volver atras. La imagen no se puede
 * desplazar, ni repintar al cambiar de tema, ni recuperar despues de abrir el
 * menu por encima. Es una maquina de escribir, no un documento.
 *
 * Y CUANDO SE LLENA, vuelve arriba. Caben FAX_ALTO lineas, que a 120 lineas
 * por minuto son unos dos minutos y medio; una carta entera dura diez. O sea
 * que una carta da unas cuatro vueltas. Es lo que hay con esta pantalla, y se
 * avisa con una raya que marca por donde va, para que no parezca que la
 * imagen esta rota.
 */
#define FAX_X      SPC_TRACE_X
#define FAX_ANCHO  WEFAX_ANCHO
#define FAX_HDR_H  UDG_HDR_H
#define FAX_Y      (uint16_t)(SPEC_Y + FAX_HDR_H)
#define FAX_ALTO   (uint16_t)((WF_PANEL_Y + WATERFALL_ROWS + 4U) - FAX_Y)

static uint16_t s_fax_fila;
static uint8_t  s_fax_limpio;

static void fax_panel_reinicia(void)
{
    s_fax_fila = 0U;
    s_fax_limpio = 0U;
}

/* Un gris de 8 bits al formato del panel. El verde se lleva seis bits, asi
 * que un gris "de verdad" tiene que repartirse 5-6-5 y no 5-5-5. */
static uint16_t fax_gris565(uint8_t g)
{
    return (uint16_t)(((uint16_t)(g >> 3) << 11)
                    | ((uint16_t)(g >> 2) << 5)
                    |  (uint16_t)(g >> 3));
}

static void fax_panel_linea(const uint8_t *l)
{
    uint16_t x;

    rm68120_set_window((uint16_t)FAX_X, (uint16_t)(FAX_Y + s_fax_fila),
                       (uint16_t)(FAX_X + FAX_ANCHO - 1U),
                       (uint16_t)(FAX_Y + s_fax_fila));
    /* rm68120_dato: sin llamada, ver rm68120_exmc.h. */
    for (x = 0U; x < FAX_ANCHO; x++) { rm68120_dato(fax_gris565(l[x])); }

    s_fax_fila++;
    if (s_fax_fila >= FAX_ALTO) { s_fax_fila = 0U; }

    /* La raya que marca por donde va, en la linea SIGUIENTE. Sin ella, al dar
     * la vuelta no hay forma de saber que parte de lo que se ve es nueva. */
    rm68120_set_window((uint16_t)FAX_X, (uint16_t)(FAX_Y + s_fax_fila),
                       (uint16_t)(FAX_X + FAX_ANCHO - 1U),
                       (uint16_t)(FAX_Y + s_fax_fila));
    for (x = 0U; x < FAX_ANCHO; x++) { rm68120_dato(0xF800U); }
}

/*
 * La cabecera: en que fase va y con que velocidad e inclinacion. Importa mas
 * que en los otros modos porque el fax pasa por estados largos en los que no
 * pinta nada -cinco segundos de tono, treinta de señal de fase- y sin esto no
 * hay forma de distinguir "esperando" de "no se entera".
 */
static char s_fax_txt[40];

/*
 * ======================================================================
 * GUARDAR LAS FOTOS DE SSTV EN EL PENDRIVE - etapa 39b, 24/09/2026
 * ======================================================================
 *
 * SE ESCRIBE SEGUN LLEGA, PORQUE NO HAY DONDE GUARDARLA
 * -----------------------------------------------------
 * sstv.c no tiene la imagen: entrega un renglon y lo olvida. Y la foto
 * entera son 240 kB a 24 bits, que no caben en la RAM que queda. Asi que
 * no hay un "guardar al final": se va escribiendo mientras entra.
 *
 * El fichero se monta con spi_flash_anade_trozo(), de 4096 en 4096, que es
 * el bloque de borrado del chip y el camino que se probo a mano 27 veces
 * antes de confiarle esto. La cabecera BMP ocupa un bloque entero (ver
 * bmp.h) justamente para que las cuentas salgan en multiplos exactos:
 * 61 bloques a 24 bits, 41 a 16.
 *
 * LO QUE CUESTA Y POR QUE SE PUEDE PAGAR
 * --------------------------------------
 * Cada bloque bloquea el bucle principal medio segundo. El decodificador
 * NO se entera -corre en la interrupcion, colgado de sdr_rx_set_block_hook-
 * pero las lineas que cierre mientras tanto hay que poder recogerlas
 * despues: por eso el anillo de sstv.c subio de 4 a 12 renglones. Un
 * bloque se llena cada 4,3 renglones, o sea cada 1,9 s en Martin 1.
 *
 * LOS RENGLONES PUEDEN NO VENIR SEGUIDOS
 * --------------------------------------
 * Si la señal se pierde un momento, sstv.c salta numeros. Se lleva la
 * cuenta de cual toca: los que falten se rellenan de negro y los que
 * lleguen tarde -con un numero ya escrito- se tiran. Escribir a ciegas
 * "lo que llegue, detras de lo anterior" daria una foto corrida sin que
 * nada avisara.
 *
 * Y SI LA FOTO SE CORTA
 * ---------------------
 * La cabecera ya dijo "256 filas" cuando se escribio, asi que un fichero
 * a medias mentiria. Al cerrar en corto se REESCRIBE la cabecera con la
 * altura de verdad: son los primeros 4096 bytes, que son un bloque de
 * borrado entero y suyo, asi que se puede rehacer sin tocar un solo punto.
 */
#define GSV_BLOQUE  4096U

static uint8_t  s_gsv_on;          /* hay una foto en curso */
static uint8_t  s_gsv_bits;        /* el formato con el que se abrio */
static uint8_t  s_gsv_buf[GSV_BLOQUE];
static uint16_t s_gsv_n;           /* bytes puestos en el buffer */
static uint16_t s_gsv_fila;        /* la siguiente fila que toca escribir */
static char     s_gsv_n8[8];       /* "SSTV001 " */
static uint16_t s_gsv_hechas;      /* fotos guardadas desde que arranco */
static uint8_t  s_gsv_bloques;     /* cuantos se han escrito en ESTA vuelta */
static uint32_t s_gsv_addr;        /* donde empieza la foto en el chip */
static uint16_t s_gsv_blk;         /* que bloque toca escribir */
static uint16_t s_gsv_blk_n;       /* cuantos tiene reservados */
static uint32_t s_gsv_ms;          /* cuando entro el ultimo renglon */
/*
 * Y lo que distingue una foto de SSTV de una carta de fax, que es lo unico
 * que cambia entre los dos: el tamaño y como se llama el fichero. Todo lo
 * demas -reservar, llenar bloques, cerrar, recortar- es identico, asi que
 * es el MISMO codigo con estos tres datos puestos al abrir. Dos copias de
 * esto es como se acaba arreglando un fallo en una y no en la otra.
 */
static uint16_t s_gsv_ancho;
static uint16_t s_gsv_alto;
static char     s_gsv_pre[4];

/*
 * CUANTO SE ESPERA UN RENGLON ANTES DE DAR LA FOTO POR TERMINADA
 * --------------------------------------------------------------
 * *** 24/09/2026, por el dueno del proyecto: "y la sstv con ruido no
 * saldra?" ***
 *
 * Con ruido, el decodificador puede cerrar UNA linea por casualidad. Si
 * esa cae en la fila 0, abre una foto y reserva 240 kB. Y como
 * sstv_fuerza() deja el estado en SSTV_IMAGEN para siempre, esa foto no
 * se cerraba nunca: cuatro casualidades y el pendrive lleno de ficheros
 * de 240 kB con un renglon dentro.
 *
 * Diez segundos son de sobra para el modo mas lento que hay -Scottie DX
 * manda una linea cada 1,05 s- y lo bastante poco para que una foto
 * cortada se cierre sola mientras sigues mirando.
 */
#define GSV_ESPERA_MS  10000UL
static spi_anade_r_t s_gsv_porque; /* por que se corto, si se corto */
static uint8_t  s_gsv_fallo;

/*
 * Vuelca el bloque lleno en su sitio, que ya esta reservado.
 *
 * Esto es un borrado y un programado y nada mas: ni buscar hueco, ni
 * tocar la FAT, ni el directorio. Por eso cuesta ~130 ms en vez de los
 * 1205 que costaba montando el fichero a trozos.
 */
static void gsv_bloque(void)
{
    s_gsv_bloques++;
    if (s_gsv_blk >= s_gsv_blk_n) {
        /* Mas bloques de los reservados no deberia pasar nunca -la cuenta
         * de filas se para en SSTV_ALTO- pero escribir pasado el final del
         * fichero es pisar al vecino, asi que se comprueba. */
        s_gsv_on = 0U;
        s_gsv_fallo = 1U;
        return;
    }
    spi_flash_write_block_4k(s_gsv_addr + (uint32_t)s_gsv_blk * GSV_BLOQUE,
                             s_gsv_buf);
    s_gsv_blk++;
    s_gsv_n = 0U;
}

/* Mete bytes en el buffer, volcando cada vez que se llena. Un renglon no
 * cabe un numero entero de veces en 4096, asi que parte de el se queda
 * para el bloque siguiente - por eso esto es un bucle y no una copia. */
static void gsv_mete(const uint8_t *d, uint32_t n)
{
    while (n > 0UL && s_gsv_on) {
        uint32_t hueco = GSV_BLOQUE - s_gsv_n;
        uint32_t c = (n < hueco) ? n : hueco;
        uint32_t i;

        for (i = 0UL; i < c; i++) { s_gsv_buf[s_gsv_n + i] = d[i]; }
        s_gsv_n = (uint16_t)(s_gsv_n + c);
        d += c;
        n -= c;
        if (s_gsv_n == GSV_BLOQUE) { gsv_bloque(); }
    }
}

static void gsv_abre(uint16_t ancho, uint16_t alto, uint8_t bits, const char pre[4])
{
    uint32_t idx, total;
    uint8_t  k;

    for (k = 0U; k < 4U; k++) { s_gsv_pre[k] = pre[k]; }
    s_gsv_ancho = ancho;
    s_gsv_alto  = alto;
    idx = spi_flash_dir_max_indice(s_gsv_pre, "BMP") + 1UL;

    if (idx > 999UL) { s_gsv_fallo = 1U; return; }   /* no hay mas nombres */

    for (k = 0U; k < 4U; k++) { s_gsv_n8[k] = pre[k]; }
    s_gsv_n8[4] = (char)('0' + (idx / 100UL) % 10UL);
    s_gsv_n8[5] = (char)('0' + (idx / 10UL) % 10UL);
    s_gsv_n8[6] = (char)('0' + idx % 10UL);
    s_gsv_n8[7] = ' ';

    s_gsv_bits = bits;
    total = bmp_cabecera(s_gsv_buf, ancho, alto, bits);
    /* La reserva va por bloques enteros: si el formato no da un multiplo
     * exacto se pide el siguiente y sobra cola, que el recorte devuelve al
     * cerrar. Un fax de 756 puntos no cuadra a bloque como cuadra SSTV. */
    total = ((total + GSV_BLOQUE - 1UL) / GSV_BLOQUE) * GSV_BLOQUE;

    /* La foto entera de golpe. Si no hay un hueco seguido se dice y no se
     * empieza: mejor que abrir una que no va a caber. */
    if (!spi_flash_reserva(s_gsv_n8, "BMP", total, &s_gsv_addr, &s_gsv_porque)) {
        s_gsv_fallo = 1U;
        debug_print("sstv: no hay sitio seguido para la foto\n");
        return;
    }

    s_gsv_blk = 0U;
    s_gsv_blk_n = (uint16_t)(total / GSV_BLOQUE);
    s_gsv_ms = g_msticks;
    s_gsv_n = GSV_BLOQUE;        /* la cabecera ES un bloque entero */
    s_gsv_fila = 0U;
    s_gsv_fallo = 0U;
    s_gsv_on = 1U;
    gsv_bloque();                /* y se escribe ya, que es el primero */
}

/* Cierra. Si se corto antes de las 256 filas, se rehace la cabecera con la
 * altura de verdad para que el fichero no prometa mas de lo que trae. */
static void gsv_cierra(void)
{
    if (!s_gsv_on) { return; }
    s_gsv_on = 0U;
    if (s_gsv_fila == 0U) { return; }
    if (s_gsv_fila < s_gsv_alto) {
        /*
         * Y se devuelve el sitio que no se llego a usar. Sin esto, una
         * foto cortada se queda con los 61 bloques reservados aunque solo
         * lleve un renglon escrito.
         */
        uint32_t usado = GSV_BLOQUE +
                         bmp_fila_bytes(s_gsv_ancho, s_gsv_bits) * (uint32_t)s_gsv_fila;
        (void)spi_flash_recorta(s_gsv_n8, "BMP", usado);
        /*
         * La cabecera ya dijo "256 filas" al escribirse. Se rehace con la
         * altura de verdad para que el fichero no prometa mas de lo que
         * trae. Es el bloque 0 de la reserva, que es un bloque de borrado
         * entero y suyo, asi que se puede rehacer sin tocar un punto.
         *
         * El fichero SIGUE ocupando los 61 bloques reservados y su entrada
         * de directorio sigue diciendo el tamaño completo. Encogerlo
         * obligaria a reescribir la cadena, y lo que se ganaria es sitio
         * -no correccion-: quien lo abra lee la altura de la cabecera y ve
         * exactamente las filas que llegaron.
         */
        (void)bmp_cabecera(s_gsv_buf, s_gsv_ancho, s_gsv_fila, s_gsv_bits);
        spi_flash_write_block_4k(s_gsv_addr, s_gsv_buf);
    }
    s_gsv_hechas++;
    debug_print("sstv: foto guardada\n");
}

/* Un renglon recien salido del decodificador. Va SIEMPRE, este o no la
 * pantalla pintando: si esto colgara del mismo "if" que el dibujo, abrir
 * el menu a mitad de foto la dejaria con un agujero. */
/*
 * Un renglon para el fichero. `y` es el numero de fila que le toca; los
 * que falten desde la ultima se rellenan de negro y los que lleguen tarde
 * se tiran.
 *
 * La foto tiene que estar ya abierta: quien decide cuando se abre es el
 * poll de cada decodificador, que es quien sabe que significa "empieza una
 * imagen" en su formato -en SSTV es la fila 0, en el fax es que acabe la
 * señal de fase-.
 */
static void gsv_linea(const uint8_t *l, uint16_t y)
{
    static uint8_t conv[WEFAX_ANCHO * 3U];   /* el mas ancho de los dos */
    uint32_t nf;

    if (!s_gsv_on) { return; }
    if (y < s_gsv_fila) { return; }   /* llega tarde: esa fila ya esta escrita */

    nf = bmp_fila_bytes(s_gsv_ancho, s_gsv_bits);

    /*
     * Las que falten, en negro - PERO COMO MUCHO UN BLOQUE POR LLAMADA.
     *
     * Sin el tope esto es un camino de tiempo no acotado dentro del bucle
     * principal: si el numero de renglon pega un salto grande -sincronismo
     * perdido- se rellenarian decenas de filas de golpe, y cada bloque de
     * 4 kB cuesta del orden de 800 ms entre el escaneo de la FAT, el
     * borrado y los tres ciclos de lectura-modificacion-escritura. Un
     * salto de 200 filas serian veinte segundos con la radio muerta.
     *
     * Con el tope, una llamada cuesta lo mismo que una normal y el relleno
     * se va poniendo al dia en las vueltas siguientes. Se pierde la fila
     * que venia en esta -se tira al salir-, que es barato comparado con
     * congelar el aparato.
     */
    while (s_gsv_fila < y && s_gsv_on && s_gsv_bloques == 0U) {
        uint32_t i;
        for (i = 0UL; i < nf; i++) { conv[i] = 0U; }
        gsv_mete(conv, nf);
        s_gsv_fila++;
    }
    if (!s_gsv_on) { return; }
    if (s_gsv_fila < y) { return; }   /* aun por detras: se sigue en la proxima */

    /* A 8 bits el byte del decodificador ES el byte del fichero -ver
     * bmp.h- asi que se mete tal cual, sin pasar por conv[]. */
    if (s_gsv_bits == BMP_8)        { gsv_mete(l, nf); }
    else if (s_gsv_bits == BMP_16)  { bmp_fila565(l, conv, s_gsv_ancho);
                                      gsv_mete(conv, nf); }
    else                            { bmp_fila24(l, conv, s_gsv_ancho);
                                      gsv_mete(conv, nf); }
    s_gsv_fila++;
    s_gsv_ms = g_msticks;

    if (s_gsv_fila >= s_gsv_alto) { gsv_cierra(); }
}

/*
 * Lo que pone el boton: normalmente el formato elegido, y mientras hay una
 * foto en marcha, cual. El detalle de por que fallo va al UART de
 * depuracion y no al boton: ahi no cabe, y un rotulo recortado dice menos
 * que uno corto y claro.
 */
static const char *gsv_rotulo(void)
{
    static char b[16];
    uint8_t i;

    if (s_gsv_fallo) { return k_sstv_gsv_estado[1][idioma()]; }
    if (!s_gsv_on)   { return k_sstv_guarda[s_sstv_guarda][idioma()]; }
    for (i = 0U; i < 10U; i++) { b[i] = k_sstv_gsv_estado[0][idioma()][i]; }
    b[10] = s_gsv_n8[4]; b[11] = s_gsv_n8[5]; b[12] = s_gsv_n8[6];
    b[13] = '\0';
    return b;
}

/* Lo mismo para el fax. Comparte el estado del guardado -solo puede haber
 * una imagen en marcha, y los dos decodificadores no pueden estar
 * encendidos a la vez- y solo cambia de donde sale el rotulo en reposo. */
static const char *gsv_rotulo_fax(void)
{
    static char b[16];
    uint8_t i;

    if (s_gsv_fallo) { return k_sstv_gsv_estado[1][idioma()]; }
    if (!s_gsv_on)   { return k_wfx_guarda[s_wfx_guarda][idioma()]; }
    for (i = 0U; i < 10U; i++) { b[i] = k_sstv_gsv_estado[0][idioma()][i]; }
    b[10] = s_gsv_n8[4]; b[11] = s_gsv_n8[5]; b[12] = s_gsv_n8[6];
    b[13] = '\0';
    return b;
}

static void fax_hdr_draw(void)
{
    wefax_info_t in;
    char *p = s_fax_txt;
    const char *e;
    int32_t ppm;

    wefax_info(&in);
    switch (in.estado) {
    case WEFAX_BUSCA:  e = tr("Esperando el tono de arranque", "Waiting for the start tone"); break;
    case WEFAX_FASE:   e = tr("Señal de fase", "Phasing signal"); break;
    case WEFAX_IMAGEN: e = tr("Recibiendo", "Receiving"); break;
    default:           e = tr("Parado", "Stopped"); break;
    }
    while (*e != '\0') { *p++ = *e++; }
    *p++ = ' '; *p++ = '-'; *p++ = ' ';
    p = hora_u3(p, (uint32_t)in.lpm);
    *p++ = ' '; *p++ = 'l'; *p++ = 'p'; *p++ = 'm';

    ppm = (int32_t)in.ppm;
    if (ppm != 0) {
        *p++ = ' ';
        *p++ = (ppm < 0) ? '-' : '+';
        if (ppm < 0) { ppm = -ppm; }
        if (ppm > 9999) { ppm = 9999; }   /* el ajuste no llega ahi, pero el
                                           * formateador solo tiene cuatro
                                           * cifras y un numero recortado se
                                           * lee mejor que uno corrido */
        p = hora_u4(p, (uint32_t)ppm);
    }
    *p = '\0';

    s_digi.y = (int16_t)SPEC_Y;
    s_digi.h = (int16_t)FAX_HDR_H;
    s_digi.chip = s_fax_txt;
    s_digi.enganchado = (uint8_t)(in.estado == WEFAX_IMAGEN);
    s_digi.btn = tr("Sincroniza", "Sync");
    s_digi.btn2 = gsv_rotulo_fax();
    ui_digi_draw_chip(&s_digi);
    ui_digi_draw_boton(&s_digi);
    ui_digi_draw_boton2(&s_digi);
}

/*
 * Saca las lineas del decodificador y las pinta. Va desde el bucle principal
 * -pintar 756 pixeles por el bus del panel no se hace desde una interrupcion-
 * y por eso el decodificador tiene DOS renglones: ver wefax.c.
 */
static void fax_poll(void)
{
    const uint8_t *l;
    static uint32_t s_ult_hdr;
    static uint8_t  s_tapado;
    static uint8_t  s_era_imagen;
    static uint16_t s_gy;          /* por que renglon del FICHERO vamos */
    uint8_t pinta;
    wefax_info_t in;

    if (!wefax_activo()) {
        gsv_cierra();              /* si se sale del modo con una carta a medias */
        s_era_imagen = 0U;
        return;
    }

    /*
     * Recoger y pintar se separan, por la misma razon que en SSTV: desde
     * que las cartas se GUARDAN, taparlas con el menu no puede dejar un
     * agujero en el fichero.
     *
     * Lo que se pierde al tapar sigue siendo el dibujo, y eso no tiene
     * arreglo: la unica copia de lo pintado estaba en la memoria del panel
     * y el menu la ha pisado. Por eso al destapar se empieza de cero a
     * pintar - pero el fichero sigue su curso sin enterarse.
     */
    pinta = (uint8_t)(!s_menu_open && !s_screen_asleep);
    if (!pinta) { s_tapado = 1U; }
    else if (s_tapado) { s_tapado = 0U; s_fax_limpio = 0U; s_fax_fila = 0U; }

    if (pinta && !s_fax_limpio) {
        rtty_scope_panel_reset();   /* borra la zona entera con el fondo del tema */
        s_fax_limpio = 1U;
        s_ult_hdr = 0UL;
    }

    wefax_info(&in);
    /* Una carta empieza cuando acaba la señal de fase. A diferencia de
     * SSTV, aqui las lineas no traen numero: van seguidas, asi que el
     * renglon del fichero lo cuenta este poll. */
    if (!s_era_imagen && in.estado == WEFAX_IMAGEN) { s_gy = 0U; }
    s_era_imagen = (uint8_t)(in.estado == WEFAX_IMAGEN);

    s_gsv_bloques = 0U;
    while (s_gsv_bloques == 0U && (l = wefax_linea()) != 0) {
        if (!s_gsv_on && s_era_imagen && s_gy == 0U &&
            s_wfx_guarda != WFX_GUARDA_NO) {
            gsv_abre(WEFAX_ANCHO, WFX_ALTO_MAX, BMP_8, "WFAX");
        }
        gsv_linea(l, s_gy);
        s_gy++;
        if (pinta) { fax_panel_linea(l); }
    }

    /* El tono de parada devuelve el estado a BUSCA: ahi acaba la carta. */
    if (s_gsv_on && in.estado != WEFAX_IMAGEN) { gsv_cierra(); }

    if (s_gsv_on && (uint32_t)(g_msticks - s_gsv_ms) > GSV_ESPERA_MS) {
        gsv_cierra();
        debug_print("wefax: carta cerrada por silencio\n");
    }

    /* La cabecera, dos veces por segundo: lo que pone cambia despacio. */
    if (pinta && (uint32_t)(g_msticks - s_ult_hdr) >= 500U) {
        s_ult_hdr = g_msticks;
        fax_hdr_draw();
    }
}

/*
 * El APRS, al mismo panel de texto que el RTTY, el CW y el NAVTEX. Cuarta
 * funcion, misma razon: cuatro anillos, cuatro funciones, un solo destino.
 *
 * Aqui no se acumulan lineas para el UART porque el decodificador ya entrega
 * el texto formateado con su salto de linea al final: una trama es una linea,
 * y no hay tramas a medias -o pasa la comprobacion o no sale-.
 */
static void ax25_poll(void)
{
    char c;
    while (ax25_get_char(&c)) { rtty_text_push(c); }
}

/* ==========================================================================
 * EL PANEL DEL SSTV - etapa 27
 * ==========================================================================
 *
 * Mismo principio que el fax: la imagen la guarda el panel, no la radio. Lo
 * que cambia es que aqui son tres bytes por punto en vez de uno, y que la
 * imagen tiene un tamaño FIJO -320 por 256- en vez de crecer sin fin.
 *
 * Se dibuja a tamaño natural y centrada. Se podria estirar al doble para que
 * llenara mas pantalla, pero 320 por 256 estirado a 640 por 512 no cabe a lo
 * alto (hay 288 px), y estirar solo a lo ancho deformaria la foto. Una foto
 * deformada es una pantalla que miente sobre lo que llego.
 */
#define SSTV_X  (uint16_t)(SPC_TRACE_X + (WEFAX_ANCHO - SSTV_ANCHO) / 2U)
#define SSTV_Y  (uint16_t)(SPEC_Y + FAX_HDR_H + 8U)

static uint8_t s_sstv_limpio;

static void sstv_panel_reinicia(void)
{
    s_sstv_limpio = 0U;
}

static void sstv_panel_linea(const uint8_t *l, uint16_t y)
{
    uint16_t x;

    if (y >= SSTV_ALTO) { return; }
    rm68120_set_window(SSTV_X, (uint16_t)(SSTV_Y + y),
                       (uint16_t)(SSTV_X + SSTV_ANCHO - 1U),
                       (uint16_t)(SSTV_Y + y));
    for (x = 0U; x < SSTV_ANCHO; x++) {
        uint16_t r = l[x * 3U];
        uint16_t g = l[x * 3U + 1U];
        uint16_t b = l[x * 3U + 2U];
        rm68120_dato((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
    }
}

static char s_sstv_txt[40];

static void sstv_hdr_draw(void)
{
    sstv_info_t in;
    char *p = s_sstv_txt;
    const char *e;

    sstv_info(&in);
    if (in.estado == SSTV_IMAGEN && in.modo) {
        e = in.modo;
        while (*e != '\0') { *p++ = *e++; }
        *p++ = ' '; *p++ = '-'; *p++ = ' ';
        p = hora_u3(p, (uint32_t)in.linea);
        *p++ = '/';
        p = hora_u3(p, (uint32_t)SSTV_ALTO);
    } else {
        /* Si ha llegado un codigo que no sabemos hacer, se enseña: es lo que
         * hace falta para decidir si vale la pena anadir ese modo. */
        e = in.vis ? tr("Modo no soportado, VIS ", "Unsupported mode, VIS ")
                   : tr("Esperando una imagen", "Waiting for a picture");
        while (*e != '\0') { *p++ = *e++; }
        if (in.vis) { p = hora_u3(p, (uint32_t)in.vis); }
    }
    *p = '\0';

    s_digi.y = (int16_t)SPEC_Y;
    s_digi.h = (int16_t)FAX_HDR_H;
    s_digi.chip = s_sstv_txt;
    s_digi.enganchado = (uint8_t)(in.estado == SSTV_IMAGEN);
    s_digi.btn = tr("Empezar", "Start");
    s_digi.btn2 = gsv_rotulo();
    ui_digi_draw_chip(&s_digi);
    ui_digi_draw_boton(&s_digi);
    ui_digi_draw_boton2(&s_digi);
}

static void sstv_poll(void)
{
    const uint8_t *l;
    uint16_t y;
    static uint32_t s_ult_hdr;
    static uint8_t  s_tapado;
    static uint8_t  s_era_imagen;
    uint8_t pinta;
    sstv_info_t in;

    if (!sstv_activo()) {
        /* Se ha salido del modo con una foto a medias: se cierra con lo que
         * haya en vez de dejar un fichero que promete 256 filas. */
        gsv_cierra();
        s_era_imagen = 0U;
        return;
    }

    /*
     * RECOGER Y PINTAR SE SEPARAN - 24/09/2026.
     *
     * Antes esto salia por la puerta de atras con el menu abierto o la
     * pantalla dormida, y entonces nadie llamaba a sstv_linea(): las
     * lineas se quedaban en el anillo de sstv.c hasta que las pisaba una
     * nueva. Mientras solo se dibujaban, eso era lo correcto -no hay donde
     * pintarlas-. Desde que ademas se GUARDAN, no: abrir el menu a mitad
     * de foto la dejaria con un agujero en medio, y encima uno que la
     * cuenta de filas rellenaria de negro sin que nada avisara.
     *
     * Asi que ahora se recoge SIEMPRE y lo que se salta es el dibujo.
     */
    pinta = (uint8_t)(!s_menu_open && !s_screen_asleep);
    if (!pinta) { s_tapado = 1U; }
    else if (s_tapado) { s_tapado = 0U; s_sstv_limpio = 0U; }

    if (pinta && !s_sstv_limpio) {
        rtty_scope_panel_reset();
        s_sstv_limpio = 1U;
        s_ult_hdr = 0UL;
    }

    /*
     * UN BLOQUE COMO MUCHO POR VUELTA.
     *
     * El anillo puede tener hasta SAL_N lineas esperando, y vaciarlo del
     * tiron encadenaria tantas escrituras como bloques se llenen. Con la
     * reserva un bloque cuesta ~130 ms, asi que tres seguidos ya son
     * cuatro decimas con la interfaz parada; antes de la reserva eran
     * 1205 ms cada uno y de ahi venia lo de "se ha quedado tostada".
     *
     * Las que queden se recogen en la vuelta siguiente, que llega
     * enseguida: el bucle principal da muchas vueltas por segundo y un
     * bloque son 4,3 renglones, o sea mas de lo que entra mientras se
     * escribe. Se va poniendo al dia solo.
     */
    s_gsv_bloques = 0U;
    while (s_gsv_bloques == 0U && (l = sstv_linea(&y)) != 0) {
        /* Una foto se abre por el principio y no a mitad: engancharse en la
         * fila 137 daria una imagen cuyo tercio de arriba es negro y nadie
         * sabria que ese negro no es de verdad. */
        if (!s_gsv_on && y == 0U && s_sstv_guarda != SSTV_GUARDA_NO) {
            gsv_abre(SSTV_ANCHO, SSTV_ALTO,
                     (uint8_t)((s_sstv_guarda == SSTV_GUARDA_16) ? BMP_16 : BMP_24),
                     "SSTV");
        }
        gsv_linea(l, y);                          /* siempre */
        if (pinta) { sstv_panel_linea(l, y); }    /* solo si se ve */
    }

    /* Y si el decodificador deja de tener imagen -se perdio la señal, o
     * acabo- se cierra lo que hubiera abierto. */
    sstv_info(&in);
    if (s_era_imagen && in.estado != SSTV_IMAGEN) { gsv_cierra(); }
    s_era_imagen = (uint8_t)(in.estado == SSTV_IMAGEN);

    /* O si simplemente ha dejado de llegar nada. Ver GSV_ESPERA_MS. */
    if (s_gsv_on && (uint32_t)(g_msticks - s_gsv_ms) > GSV_ESPERA_MS) {
        gsv_cierra();
        debug_print("sstv: foto cerrada por silencio\n");
    }

    if (pinta && (uint32_t)(g_msticks - s_ult_hdr) >= 500U) {
        s_ult_hdr = g_msticks;
        sstv_hdr_draw();
    }
}

/*
 * Y lo mismo para el NAVTEX, al mismo panel. Tercera funcion por la misma
 * razon: tres anillos, tres funciones, un solo destino.
 */
static void navtex_poll(void)
{
    static char line[80];
    static uint8_t line_len = 0U;
    char c;

    while (navtex_get_char(&c)) {
        rtty_text_push(c);

        if (c == '\r' || c == '\n' || line_len >= (uint8_t)(sizeof(line) - 1U)) {
            if (line_len > 0U) {
                line[line_len] = '\0';
                debug_print("navtex: ");
                debug_print(line);
                debug_print("\n");
                line_len = 0U;
            }
            if (c != '\r' && c != '\n') { line[line_len++] = c; }
        } else {
            line[line_len] = c;
            line_len++;
        }
    }
}

/*
 * Y el FT8 al mismo panel, etapa 30. Pero por LINEAS, no por caracteres, y
 * esa es toda la diferencia con los tres de arriba.
 *
 * El NAVTEX, el RTTY y el CW sueltan letra a letra segun van llegando, asi
 * que su anillo es de caracteres y se vacia entero en cada vuelta. El FT8 no
 * entrega nada durante catorce segundos y despues suelta de golpe entre cero
 * y una docena de mensajes YA COMPLETOS. No hay anillo de caracteres que
 * vaciar: hay una lista de mensajes que puede haber crecido.
 *
 * Y de ahi el contador. ft8_modo_total() dice cuantos van desde que se
 * encendio; lo que este poll hace es mirar cuanto ha crecido desde la ultima
 * vuelta y empujar solo esos, del mas viejo al mas nuevo para que en la
 * pantalla queden en el orden en que llegaron. Preguntar "¿cuantos hay
 * ahora?" en vez de "¿hay algo nuevo?" es lo que hace que esto no se pierda
 * un mensaje si una vuelta del bucle tarda de mas.
 */
static void ft8_poll(void)
{
    static uint32_t s_visto;
    uint32_t total = ft8_modo_total();
    uint32_t nuevos;
    uint8_t  i;

    /* Al arrancar FT8 el contador vuelve a cero. Sin esto, s_visto se
     * quedaria en el valor de la sesion anterior y los mensajes de la nueva
     * no saldrian hasta pasar de ese numero. */
    if (total < s_visto) { s_visto = 0UL; }
    if (total == s_visto) { return; }

    nuevos = total - s_visto;
    if (nuevos > (uint32_t)FT8_MODO_LINEAS) { nuevos = (uint32_t)FT8_MODO_LINEAS; }
    s_visto = total;

    /* Del mas viejo al mas nuevo: ft8_modo_linea(0) es el ultimo que llego. */
    for (i = (uint8_t)nuevos; i > 0U; i--) {
        const char *t = ft8_modo_linea((uint8_t)(i - 1U));
        uint8_t k;

        if (t[0] == '\0') { continue; }
        (void)k;

        /* Al UART y nada mas: la LISTA la pinta el panel de FT8 leyendo
         * directamente de ft8_modo (ver digi_sync()). Esto empujaba antes
         * cada linea a la rejilla de RTTY, que era una segunda copia de los
         * mismos mensajes y con la mitad de renglones. */
        debug_print("ft8: ");
        debug_print(t);
        debug_print("\n");
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
    /*
     * El RDS NO entra aqui, y eso es una correccion del mismo dia.
     *
     * La primera version lo metio en este panel razonando que "tambien es
     * texto que llega por radio". Error: este panel SUSTITUYE al espectro -
     * el bucle principal pinta el osciloscopio de tono EN LUGAR de
     * sdr_spectrum_waterfall_tick()-, y eso tiene sentido cuando estas
     * decodificando teletipo, donde lo que miras es el texto. Con el RDS no:
     * el RDS es un dato de fondo mientras escuchas la FM y miras el
     * espectro. El dueno lo dijo en una linea - "rds activo y el espectro
     * desaparece"- y ademas, como el osciloscopio de tono no tiene datos en
     * FM, lo que quedaba era una pantalla en negro.
     *
     * El RDS tiene su propio panel, mas pequeno, que ocupa solo la cascada.
     * Ver rds_panel_activo().
     */
    /*
     * El WEFAX tambien entra, y eso es a proposito aunque no dibuje nada de
     * lo que dibujan los otros tres. Esta funcion contesta a "¿el espectro ha
     * dejado su sitio?", y la respuesta con el fax puesto es que si: el
     * espectro no se refresca, la regla de frecuencias no vale, y tocar el
     * panel no puede saltar a la frecuencia del pico porque lo que hay debajo
     * del dedo es un mapa, no un espectro. Todo eso cuelga de aqui.
     *
     * Lo que cambia es QUIEN pinta: con RTTY, CW o NAVTEX pinta el
     * osciloscopio de tono mas el texto; con el fax pinta fax_poll(), linea a
     * linea, desde el bucle principal. Ver la rama de mas abajo que lo
     * reparte.
     */
    /*
     * SALE DE LA TABLA, NO DE UNA LISTA A MANO. 28/09/2026.
     *
     * *** Por el dueno del proyecto: "le doy a wspr y la ventana no
     * cambia". ***
     *
     * Aqui habia una lista escrita a mano:
     *
     *     if (m == DEMOD_MODE_NFM) { return ax25_activo(); }
     *     return (m == USB || m == LSB) &&
     *            (rtty || cw || navtex || wefax || sstv || ft8 || hfdl);
     *
     * y se quedo en HFDL. WSPR, AIS y ALE entraron despues y NINGUNO se
     * anadio aqui, tres veces seguidas, porque esta funcion esta a dos mil
     * lineas de donde se anade un modo y nada la senala.
     *
     * EL SINTOMA ERA EL PEOR POSIBLE: el modo se selecciona, el
     * demodulador arranca, el decodificador corre y hasta decodifica - pero
     * el espectro NO cede su sitio, asi que el panel con la tabla y los
     * botones no llega a pintarse nunca. Desde fuera: "le doy y no pasa
     * nada". Nada falla, nada avisa, y lo que hay roto es una linea que no
     * menciona el modo por ningun lado.
     *
     * LA PREGUNTA QUE CONTESTA ESTA FUNCION es "¿el espectro ha dejado su
     * sitio?", y resulta que tiene la MISMA respuesta que "¿es un modo
     * digital?" - porque el corte de las categorias ya se hizo por lo que
     * SALE y no por lo que hay debajo (ver el comentario de MODO_FAM_ANA).
     * Lo que entrega texto o una imagen se lleva el panel; lo que entrega
     * sonido para el oido, no.
     *
     * Asi que se lee de la tabla y se acabo la segunda lista. Este fichero
     * ya tiene escrito lo que pasa con las tablas paralelas -ver el
     * comentario de k_modo_desc[] arriba, que costo que la pantalla de
     * modos dejara de aparecer entera-: "dos tablas que hay que mantener
     * alineadas a mano son una tabla con un fallo pendiente". Esto era
     * exactamente eso, en otra forma.
     *
     * Y ahora anadir un modo digital le da su panel SOLO. Lo vigila ademas
     * tools/panel_check.py, que falla la compilacion si alguien vuelve a
     * escribir la lista a mano.
     */
    uint8_t i;

    (void)m;
    for (i = 0U; i < (uint8_t)DEMOD_MODE_ENTRY_COUNT; i++) {
        if (demod_mode_entry_active(i)) {
            return (uint8_t)(k_demod_modes[i].fam == MODO_FAM_DIG);
        }
    }
    return 0U;
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
    if (ft8_modo_activo()) {
        /*
         * EN FT8 LA RAYA NO ES UN TONO: ES EL BORDE DE LA VENTANA.
         *
         * El RTTY tiene dos tonos y el CW uno, y en los dos casos sintonizar
         * es llevar el pico hasta la raya. En FT8 no hay un tono al que
         * apuntar -son ocho, y se mueven-, pero si hay algo igual de concreto
         * que ensenar: el decodificador SOLO MIRA los primeros
         * FT8_ADAPTER_NUM_BINS bins del audio decimado, o sea una ventana que
         * empieza en 0 Hz y acaba donde esta esta raya. Lo que caiga a la
         * derecha no se decodifica aunque se oiga perfectamente.
         *
         * Esto no es un adorno: es EL ajuste de este modo. Sin la raya, el
         * unico sintoma de tener la senal 500 Hz mas arriba de la cuenta es
         * que no sale ni un mensaje, que es exactamente lo mismo que se ve
         * cuando no hay propagacion.
         */
        /* El ancho lo da ft8_modo.c y no se calcula aqui: las constantes que
         * lo fijan son del adaptador, y main.c no tiene por que conocerlas
         * -ese es justo el trato que promete ft8_modo.h-. */
        uint16_t bor_x = (uint16_t)((ft8_modo_ventana_hz() / nyquist_hz) * (float)MAIN_W);

        if (bor_x < MAIN_W) { gfx_vline(bor_x, bar_y, bar_area_h, color_tema(PAL_ACCENT)); }
    } else if (cw_get_enabled()) {
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
    } else if (ax25_activo()) {
        /*
         * APRS: los dos tonos del AFSK, 1.200 y 2.200 Hz, y no los del RTTY.
         * Los del RTTY son un ajuste del usuario que en paquete no significa
         * nada; estos son fijos por norma y son los MISMOS numeros con los
         * que ax25.c afina sus dos osciladores, sacados de ax25.h. Dos rayas
         * puestas donde no estan las cosas son peor que ninguna raya: se leen
         * como "estas desintonizado" cuando no lo estas.
         */
        mark_x  = (uint16_t)((AX25_MARCA_HZ   / nyquist_hz) * (float)MAIN_W);
        space_x = (uint16_t)((AX25_ESPACIO_HZ / nyquist_hz) * (float)MAIN_W);
        if (mark_x < MAIN_W)  { gfx_vline(mark_x,  bar_y, bar_area_h, color_tema(PAL_ACCENT)); }
        if (space_x < MAIN_W) { gfx_vline(space_x, bar_y, bar_area_h, color_tema(PAL_WARN)); }
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

/*
 * EL SIGNO DEL TONO DE CW, EN UN SOLO SITIO - 23/09/2026.
 *
 * En CW el VFO no se pone en la portadora sino a un tono de distancia, para
 * que se oiga algo. De que lado depende del modo: en USB el audio es
 * (RF - VFO), luego el VFO va POR DEBAJO; en LSB es (VFO - RF) y va por
 * encima. Devuelve -1 o +1 para multiplicar por el tono.
 *
 * Existe porque este convenio lo necesitan dos sitios -engancharse a un
 * pico del espectro y mover el tono con el mando-, y dos copias de un signo
 * son dos copias que un dia dejan de coincidir; ese dia, una de las dos
 * funciones se iria en sentido contrario y costaria horas verlo. Con esto,
 * si el convenio cambia, cambia para los dos a la vez.
 */
static int32_t cw_signo_vfo(demod_mode_t modo)
{
    return (modo == DEMOD_MODE_LSB) ? 1 : -1;
}

/*
 * MOVER EL TONO DE CW SIN PERDER LA ESTACION - 23/09/2026.
 *
 * En CW no se oye la portadora: se oye la diferencia entre ella y el VFO.
 * Si estas copiando a alguien a 600 Hz y subes el tono a 700, el filtro se
 * va a 700 pero la estacion sigue dando 600, o sea que se sale del filtro y
 * SE CALLA. Hasta hoy pasaba justo eso: cw_set_pitch_hz() rehacia los
 * coeficientes y nadie tocaba el VFO. Habia que volver a sintonizar a mano
 * despues de cada cambio de tono, que es lo contrario de para lo que sirve
 * poder cambiarlo.
 *
 * Asi que el VFO va detras. En USB el audio es (RF - VFO), luego para que
 * la MISMA RF siga sonando al tono nuevo el VFO baja lo que suba el tono;
 * en LSB el audio es (VFO - RF) y va al reves. Son los mismos signos que
 * usa spec_tap_tune_to_x() al engancharse a un pico en CW -ahi ya estaba
 * bien-, y por eso estan los dos sitios apuntando el uno al otro: si algun
 * dia cambia el convenio, tienen que cambiar los dos.
 *
 * EFECTO VISIBLE: el numero de la cabecera ES el VFO (ver top_sync()), no
 * la portadora, asi que al mover el tono el numero se mueve lo mismo. No es
 * un defecto de esto: el VFO se ha movido de verdad, y el numero sigue
 * diciendo lo que siempre ha dicho. La alternativa era dejarlo quieto y que
 * la estacion se callara, que es peor. Si algun dia se quiere que en CW la
 * cabecera enseñe la portadora de verdad (VFO + tono en USB, - en LSB),
 * eso es un cambio de convenio que toca tambien la pastilla de banda, las
 * memorias, el toque-para-sintonizar y lo que se guarda en CONFIG.CSV, y
 * merece su propia etapa.
 */
static void cw_tono_mover(float delta_hz)
{
    float antes = cw_get_pitch_hz();
    float ahora;

    cw_set_pitch_hz(antes + delta_hz);
    ahora = cw_get_pitch_hz();      /* puede venir recortado, ver su comentario */

    /* El filtro de audio va detras del tono: si no, se quedaria centrado
     * donde estaba y el pitido se saldria del filtro justo al colocarlo. */
    demod_am_set_cw_filter_hz(ahora);

    if (cw_get_enabled() && ahora != antes) {
        demod_mode_t modo = demod_am_get_mode();
        int32_t      paso = (int32_t)(ahora - antes + ((ahora > antes) ? 0.5f : -0.5f));
        int64_t      f    = (int64_t)s_tune_hz
                            + (int64_t)cw_signo_vfo(modo) * (int64_t)paso;

        if (f < (int64_t)TUNE_MIN_HZ) { f = (int64_t)TUNE_MIN_HZ; }
        if (f > (int64_t)TUNE_MAX_HZ) { f = (int64_t)TUNE_MAX_HZ; }
        if ((uint32_t)f != s_tune_hz) {
            s_tune_hz = (uint32_t)f;
            apply_lo_tune(s_tune_hz);
            freq_display_draw();
        }
    }
}

/*
 * SALIDA DE EMERGENCIA DEL TACTIL - 23/09/2026.
 *
 * Si el tactil se descalibra de verdad, la radio quedaba irrecuperable:
 * el mando SI navega la pantalla de Ajustes y SI puede aplicar celdas, pero
 * ABRIRLA era solo el boton tactil de abajo, y la pulsacion larga solo
 * cierra. O sea que "Calibrar tactil" estaba dentro de una habitacion cuya
 * unica puerta es justo lo que se ha roto. La unica salida era reprogramar
 * el firmware o editar CONFIG.CSV por USB.
 *
 * EL GESTO: TRES PULSACIONES LARGAS SEGUIDAS (mantener el mando ~0,6 s,
 * soltar, y repetir, tres veces, sin dejar pasar mas de tres segundos entre
 * una y otra). Abre el asistente de calibracion desde donde sea.
 *
 * Por que largas y no cortas: la primera version de esto pedia cinco
 * pulsaciones CORTAS, y era un fallo. El boton corto cambia el paso de
 * sintonia y hay OCHO pasos, asi que ir de 100 Hz a 100 kHz son seis
 * pulsaciones rapidas: el rescate habria saltado solo, a media sintonia,
 * y de la peor manera posible -tapando la pantalla con un asistente que no
 * has pedido-. La pulsacion larga, en cambio, hace algo idempotente
 * (devolver el mando a sintonia y cerrar el menu), asi que repetirla no
 * molesta, y nadie la hace tres veces seguidas sin querer.
 *
 * Si saltara igualmente por accidente, tampoco pasa nada: una pulsacion
 * corta cancela el asistente y deja intacta la calibracion que hubiera.
 *
 * No hace falta protegerlo de que salte con la pantalla dormida o el
 * asistente ya abierto: en esos dos casos el bucle principal ni siquiera
 * llama a esta funcion (ver sus ramas en main()).
 */
#define CAL_RESCATE_LARGAS     3U
#define CAL_RESCATE_VENTANA_MS 3000U

static uint8_t  s_cal_rescate_n = 0U;
static uint32_t s_cal_rescate_ms = 0U;

static uint8_t cal_rescate(uint8_t long_press)
{
    if (!long_press) { return 0U; }

    /* Se reinicia la cuenta si ha pasado demasiado desde la anterior: lo
     * que se busca es una rafaga, no tres pulsaciones largas sueltas a lo
     * largo de la tarde saliendo de menus. */
    if ((uint32_t)(g_msticks - s_cal_rescate_ms) > CAL_RESCATE_VENTANA_MS) {
        s_cal_rescate_n = 0U;
    }
    s_cal_rescate_ms = g_msticks;
    s_cal_rescate_n++;

    if (s_cal_rescate_n < CAL_RESCATE_LARGAS) { return 0U; }

    s_cal_rescate_n = 0U;
    if (s_menu_open) { menu_screen_close(); }
    debug_print("tactil: rescate por mando - abriendo la calibracion\n");
    touch_calib_start(touch_calib_done_callback);
    return 1U;
}

static void tune_encoder_poll(void)
{
    int32_t detents    = encoder_take_delta() + s_inject_detents;
    uint8_t press       = (uint8_t)(encoder_take_press() | s_inject_press);
    uint8_t long_press  = encoder_take_long_press();

    s_inject_detents = 0;
    s_inject_press   = 0U;
    uint8_t changed = 0;

    /* Antes que nada: la rafaga de rescate. Va aqui arriba y no dentro de
     * una rama concreta porque tiene que funcionar este el mando donde este
     * enganchado y haya la pantalla que haya - si el tactil se ha roto, no
     * se puede pedir al usuario que llegue antes a ningun sitio. */
    if (cal_rescate(long_press)) { return; }

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
     * shape as the long-press handler right after this block.
     *
     * *** SOLO CUANDO EL MANDO ESTA SINTONIZANDO. 02/10/2026, por un
     * fallo que nos reportaron: "although the volume mode can be
     * selected (light blue highlight), moving the encoder does not
     * change the volume level when in either RTTY mode". ***
     *
     * Y tenian razon. Esta rama estaba ANTES de mirar s_encoder_target,
     * asi que en RTTY se quedaba con TODOS los detentes pasara lo que
     * pasara: le dabas al boton VOL de la barra de abajo, se encendia en
     * azul -porque encenderse solo depende de s_encoder_target, que SI
     * cambiaba- y el mando seguia moviendo mark/space. El boton decia una
     * cosa y el mando hacia otra, que es la peor forma de fallar.
     *
     * Lo que esta rama sustituye es SINTONIZAR, no "todo lo que haga el
     * mando": en RTTY las dos frecuencias son lo que se ajusta al vuelo
     * en vez de la del VFO. En cuanto el usuario pide explicitamente otra
     * cosa -VOL, SQL, PGA, lo que sea- eso manda, y al soltarlo (por el
     * timeout de VOL o por la pulsacion larga) el mando vuelve a TUNE y
     * esta rama vuelve a coger los detentes ella sola.
     *
     * Con el destino en TUNE la pulsacion corta y la larga se las sigue
     * tragando esta rama, como antes. Eso no molesta: la corta cambia el
     * paso de sintonia, que en RTTY no se usa, y la larga significa
     * "devuelveme el mando a TUNE", donde ya esta.
     *
     * *** !s_menu_open added 08/08/2026 *** - without it, the encoder
     * kept nudging mark/space even while MENU/MODE was open, hijacking
     * it away from whatever the open menu screen actually expects
     * (tile navigation, a detail-view value, etc.) - same class of bug
     * as rtty_scope_draw() not checking s_menu_open, just on the input
     * side instead of the display side.
     */
    if (rtty_encoder_grabs_tuning() && !s_menu_open
        && s_encoder_target == ENCODER_TARGET_TUNE) {
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

    /* Con el teclado numerico delante el mando no hace nada. 23/09/2026:
     * no estaba en esta lista, asi que con un ajuste enganchado el giro caia
     * hasta ENCODER_TARGET_AJUSTE y, si era Tema, ajuste_ciclar() llamaba a
     * radio_screen_draw(), que repinta la pantalla de radio ENTERA encima
     * del teclado. El teclado desaparecia de la vista pero seguia activo:
     * veias la radio y tocabas teclas invisibles. */
    if (kbd_activa() && !long_press) {
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
        } else if (!s_menu_open && s_encoder_ajuste == AJ_ENGANCHE_NINGUNO
                   && ((g_msticks - s_volume_target_last_ms) >= VOLUME_TARGET_TIMEOUT_MS)) {
            /* El enganche de Func queda fuera del timeout (23/09/2026).
             * Antes no: engancharse a "Volumen" ponia el destino en VOLUME
             * sin tocar s_volume_target_last_ms, y como al volver de Ajustes
             * ya no hay menu abierto, esta misma rama lo deshacia en la
             * vuelta siguiente -casi siempre al instante, porque el sello es
             * del ultimo uso del boton VOL de la barra-. Se veia como que el
             * enganche no habia entrado nunca: Func encendido con "Volumen"
             * y el mando sintonizando. El timeout es para el boton VOL, que
             * se toca de pasada; un enganche se pide y se suelta a mano. */
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
            cw_tono_mover((float)detents * 10.0f);
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

    if (s_encoder_target == ENCODER_TARGET_FILTRO) {
        /* Como en la escala, el boton NO cambia el paso de sintonia: aqui
         * hace falta para cambiar de corte, que es lo que se toca. */
        if (press) {
            s_fil_ajusta_hi = (uint8_t)(s_fil_ajusta_hi ? 0U : 1U);
            settings_value_redraw();
        }

        if (detents != 0 && cw_get_enabled()) {
            /*
             * En CW el que suena es el paso banda de CW, no los dos
             * cortes, asi que el mando mueve SU ancho - 100 a 800 Hz a
             * pasos de 50, que es lo que lleva un equipo de telegrafia.
             * Antes eran tres valores atados al selector de ancho
             * compartido, que es tener un mando fino y usarlo de
             * interruptor de tres posiciones.
             */
            float a = demod_am_get_cw_bw_hz() + (float)detents * 50.0f;
            if (a < 100.0f) { a = 100.0f; }
            if (a > 800.0f) { a = 800.0f; }
            demod_am_set_cw_bw_hz(a);
            if (s_settings_ready_for_autosave) { settings_mark_dirty(); }
            settings_value_redraw();
            return;
        }

        if (detents != 0) {
            uint16_t lo, hi;
            int32_t  v;

            fil_cortes(&lo, &hi);
            /* 50 Hz por detente: el mismo paso que dan las radios que
             * llevan este mando, y el mas fino que se distingue de oido
             * en el corte de arriba. */
            if (s_fil_ajusta_hi) {
                v = (int32_t)hi + (int32_t)detents * 50;
                if (v < 0) { v = 0; }
                hi = (uint16_t)v;
            } else {
                v = (int32_t)lo + (int32_t)detents * 50;
                if (v < 0) { v = 0; }
                lo = (uint16_t)v;
            }
            /* El recorte de verdad vive en audiofil.c, no aqui: un rango
             * repetido en dos sitios es un rango que un dia deja de
             * coincidir. Se pide, y se vuelve a preguntar que ha salido. */
            fil_pon(lo, hi);
            settings_value_redraw();
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

    if (s_encoder_target == ENCODER_TARGET_AJUSTE) {
        /*
         * El mando enganchado a un ajuste de lista (tema, paleta, estilo,
         * zoom...). Ver ENCODER_TARGET_AJUSTE y ajuste_ciclar().
         *
         * Misma cortesia que el resto: el boton del mando sigue cambiando el
         * paso de sintonia aunque el giro este dedicado a otra cosa.
         */
        if (press) {
            set_tune_step_idx((uint8_t)((s_tune_step_idx + 1U) % TUNE_STEP_COUNT));
            if (!s_menu_open) { step_display_draw(); }
        }
        if (detents != 0 && s_encoder_ajuste != AJ_ENGANCHE_NINGUNO) {
            ajuste_ciclar(s_encoder_ajuste, detents);
            /* La pastilla del mando de la cabecera lleva el valor nuevo, y
             * un tema ya ha repintado la pantalla entera por su cuenta. */
            if (!s_screen_asleep) { top_sync(); ui_top_draw_status(&s_top); }
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
        } else if (widget == &s_btn_func) {
            /*
             * FUNC - 23/09/2026, por el dueno del proyecto, calcado del boton
             * del mismo nombre de su Yaesu: abre la lista de ajustes para
             * ELEGIR cual va a mover el mando; al tocar uno se vuelve a la
             * principal con el mando ya enganchado a el, y desde ahi se
             * cambia girando y viendo el efecto en directo, sin entrar y
             * salir de ajustes.
             *
             * Pulsado con algo ya enganchado, SUELTA. Es la salida natural:
             * el mismo boton que engancha es el que devuelve el mando a la
             * sintonia, sin tener que acordarse de un gesto aparte.
             */
            if (s_encoder_ajuste != AJ_ENGANCHE_NINGUNO) {
                ajuste_soltar();
                act_sync();
                ui_act_draw(&s_act);
                top_sync();
                ui_top_draw_status(&s_top);
                debug_print("func: mando suelto, vuelve a la sintonia\n");
            } else {
                /* El orden importa: la bandera se pone DESPUES de abrir.
                 * Asi cfg_show() y grid_show() pueden apagarla siempre al
                 * entrar, que es lo que impide que el modo sobreviva a
                 * navegar (Func -> Paso -> Ajustes dejaba el selector
                 * encendido en una pantalla con pinta de normal: tocabas
                 * Paleta esperando cambiarla y la enganchaba). act_draw()
                 * otra vez porque cfg_show() ya ha pintado la barra con la
                 * bandera todavia apagada. */
                cfg_show(CFG_AJUSTES);
                s_cfg_func = 1U;
                act_draw();
            }
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
        /* Estos widgets van ocultos (hidden=1) y su etiqueta no se pinta
         * nunca - ver el comentario de arriba-, asi que se deja la
         * espanola y no se vuelve a montar la barra al cambiar de idioma.
         * Lo que SI se ve es s_act.name[], que se rellena en act_fill()
         * con el idioma puesto. */
        *k_act_slots[i].btn = (ui_button_t){0, 0, 0, 0,
                                    k_act_slots[i].nombre[IDIOMA_ES],
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

    /* Ya hay pantalla de verdad: a partir de ahora tema_aplicar() puede
     * repintar. Ver alli. */
    s_pantalla_pintada = 1U;

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
    uint32_t full_span_hz;

    /*
     * Con el analizador puesto, el panel enseña el espectro del AUDIO: un
     * toque ahi no señala ninguna frecuencia de radio, asi que no puede
     * mover el VFO. Sin esto, tocar una raya de 300 Hz te llevaria a
     * 300 Hz de dial, que es un sitio donde no hay nada y del que cuesta
     * volver.
     */
    if (analiz_activo()) { return; }

    full_span_hz = spec_zoom_full_span_hz();
    uint32_t panel_center_hz = s_tune_hz;
    demod_mode_t modo = demod_am_get_mode();
    int32_t px;
    float bin, bin_snap;
    uint8_t enganchado = 0U;
    int64_t off_hz, f;
    int64_t step_hz = (int64_t)k_tune_steps[s_tune_step_idx];

    if (s_spec_zoom == SPEC_ZOOM_1X && demod_am_get_mix_hz() != 0.0f) {
        panel_center_hz = s_lo_hz;
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
        /* Sobre la TRAZA, que es lo que hay dibujado debajo del dedo. */
        bin_snap = spec_snap_bin(s_db_traza, FFT_BINS_IQ, bin, sm, &enganchado);
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
         * inaudible. De que lado lo dice cw_signo_vfo(), que es el mismo que
         * usa cw_tono_mover() - ver su comentario. */
        int64_t pitch = (int64_t)(cw_get_pitch_hz() + 0.5f);
        f += (int64_t)cw_signo_vfo(modo) * pitch;
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
    static uint8_t s_band_tap_active = 0U;
    static uint8_t s_borrar_tap = 0U;
    static uint8_t s_frec_tap = 0U;
    static uint8_t s_salto_tap = 0U;
    static uint8_t s_fmt_tap = 0U;   /* el segundo boton de la cabecera digital */
    static uint8_t s_modo_tap = 0U;  /* la chapa, que abre la rejilla de modos */
    /* El cuerpo del panel de FT8: cambia la lista por el mapa. Ver
     * ft8_mapa_pinta() para por que es un toque y no un boton. */
    static uint8_t s_mapa_tap = 0U;
    /* El arrastre del mapa y los dos mandos del zoom. Ver donde se
     * reparten los toques, mas abajo. */
    static uint8_t s_zoom_tap = 0U;
    static uint8_t s_mapabtn_tap = 0U;
    static int16_t s_mapa_ax = 0, s_mapa_ay = 0;
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

        /* El de frecuencias de HFDL, entre la barra y la chapa. Mismo trato
         * que el de borrar: se marca al pulsar y actua al soltar. */
        s_frec_tap = (uint8_t)(!s_touch_owner_is_menu && !s_menu_open
            && digi_panel_active()
            && ui_digi_boton3_hit(&s_digi, x, y));
        if (s_frec_tap) {
            s_digi.btn3_press = 1U;
            ui_digi_draw_boton3(&s_digi);
        }

        /* Y el de "la siguiente", pegado a la chapa. Mismo trato. Va
         * despues y solo si el de la lista no se lo ha quedado: no se
         * solapan -ui_digi.c recorta la zona de la chapa para dejarle
         * sitio- pero preguntarlo cuesta cero. */
        s_salto_tap = (uint8_t)(!s_frec_tap && !s_touch_owner_is_menu
            && !s_menu_open && digi_panel_active()
            && ui_digi_boton4_hit(&s_digi, x, y));
        if (s_salto_tap) {
            s_digi.btn4_press = 1U;
            ui_digi_draw_boton4(&s_digi);
        }

        /*
         * Y EL DEL MAPA, pegado a la chapa - 28/09/2026, por el dueño: "en
         * los modos que tienen mapa el mapa necesita un boton".
         *
         * No enciende el "pulsado" al tocarlo como los otros: en este,
         * btn5_press quiere decir "el mapa esta puesto", y encenderlo al
         * bajar el dedo diria que ya esta puesto medio segundo antes de que
         * lo este. Se dibuja al soltar, con lo que de verdad haya pasado.
         */
        s_mapabtn_tap = (uint8_t)(!s_frec_tap && !s_salto_tap
            && !s_touch_owner_is_menu && !s_menu_open && digi_panel_active()
            && (ft8_modo_activo() || hfdl_modo_activo() || ais_modo_activo()
                || jtty_modo_activo())
            && ui_digi_boton5_hit(&s_digi, x, y));

        /* El segundo boton, con la misma regla de "la zona la da ui_digi.c".
         * Va despues y solo si el primero no se lo ha quedado: los dos no se
         * solapan, pero preguntarlo aqui cuesta cero y ahorra depender de
         * que no se solapen nunca. */
        s_fmt_tap = (uint8_t)(!s_borrar_tap && !s_touch_owner_is_menu
            && !s_menu_open && digi_panel_active()
            && (sstv_activo() || wefax_activo())
            && ui_digi_boton2_hit(&s_digi, x, y));
        if (s_fmt_tap) {
            s_digi.btn2_press = 1U;
            ui_digi_draw_boton2(&s_digi);
        }

        /* Y la chapa, solo en SSTV: en el resto de modos dice la velocidad
         * o el enganche, que no son cosas que se elijan. */
        /*
         * La chapa se toca en SSTV -abre la rejilla de modos- y desde el
         * 25/09/2026 tambien en FT8, donde hace otra cosa: cuadrar la ranura.
         * Son dos acciones distintas en el mismo sitio, y eso esta bien
         * porque en los dos casos es "lo que se toca de lo que la chapa
         * dice": en SSTV dice el modo, y en FT8 dice el segundo.
         */
        s_modo_tap = (uint8_t)(!s_borrar_tap && !s_fmt_tap && !s_touch_owner_is_menu
            && !s_menu_open && digi_panel_active()
            && (sstv_activo() || ft8_modo_activo() || hfdl_modo_activo())
            && ui_digi_chip_hit(&s_digi, x, y));

        /*
         * EL CUERPO DEL MAPA: arrastrarlo, y los dos mandos del zoom.
         *
         * Hasta el 28/09/2026 este toque PONIA Y QUITABA el mapa, porque
         * -decia el comentario- la cabecera no tenia hueco para un boton.
         * Si lo tenia (ver btn5 en ui_digi.h), asi que el mapa se pone con
         * su boton y esta zona se dedica a lo que pidio el dueño: mover el
         * mapa con el dedo.
         *
         * Va el ultimo de todos y solo si no se lo ha quedado nadie: la
         * zona es grande -800 x 288- y se come cualquier cosa que este
         * dentro, asi que los botones y la chapa preguntan antes.
         */
        {
            uint8_t en_mapa = (uint8_t)(!s_borrar_tap && !s_fmt_tap && !s_frec_tap
                && !s_salto_tap && !s_modo_tap && !s_mapabtn_tap
                && !s_touch_owner_is_menu && !s_menu_open && s_ft8_mapa
                && modo_tiene_mapa()   /* la lista, en un solo sitio */
                && x < (uint16_t)MAIN_W
                && y >= (uint16_t)FT8_MAPA_Y
                && y <  (uint16_t)(FT8_MAPA_Y + FT8_MAPA_H));

            s_zoom_tap = 0U;
            s_mapa_tap = 0U;
            if (en_mapa) {
                /* Donde estan los mandos lo dice mapa.c, que es quien los
                 * dibuja: una copia de esa geometria aqui es como se
                 * consigue que el boton se pinte en un sitio y responda en
                 * otro. */
                s_zoom_tap = mapa_zoom_hit(mapa_caja(), (int16_t)x, (int16_t)y);
                if (!s_zoom_tap) {
                    s_mapa_tap = 1U;                 /* arrastrar */
                    s_mapa_ax = (int16_t)x;
                    s_mapa_ay = (int16_t)y;
                }
            }
        }

        /* La zona es la TRAZA, no el panel entero (23/09/2026). Antes era
         * x < MAIN_W, es decir 0..799, y spec_tap_tune_to_x() recorta px a 0:
         * tocar la canaleta de las etiquetas de dB (40 px a la izquierda)
         * saltaba medio span de golpe -a zoom 1x, 48 kHz-. Tocabas una
         * etiqueta para leerla y perdias la estacion.
         *
         * Y queda fuera con el panel digital delante: ahi la pantalla la
         * ocupan el osciloscopio de tono y el texto decodificado, y el
         * espectro de RF ni siquiera se refresca (el bucle principal pinta
         * rtty_scope_draw() EN LUGAR de sdr_spectrum_waterfall_tick()), asi
         * que el enganche al pico trabajaba sobre un cuadro congelado. Tocar
         * el texto para senalar un indicativo te movia el VFO a una
         * frecuencia sin relacion con nada de lo que se ve. */
        s_spec_drag_active = (uint8_t)(!s_touch_owner_is_menu && !s_borrar_tap
            && !digi_panel_active()
            && x >= SPEC_TRACE_X && x < (uint16_t)(SPEC_TRACE_X + SPEC_TRACE_W)
            && y >= SPEC_Y && y < (uint16_t)(SPEC_Y + SPEC_H));
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

        /* Pastilla de banda (23/09/2026): abre la lista de bandas, que es lo
         * que hacia el boton de la barra de abajo antes de dejarle el sitio a
         * Func. Va aqui, junto a las otras dos zonas de la cabecera, y la
         * geometria la da ui_top.c igual que ellas. */
        s_band_tap_active = ui_top_band_hit(x, y);
        if (s_band_tap_active) { s_freq_tap_active = 0U; }

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

    if (s_menu_qth_active && s_touch_owner_is_menu) {
        /* Ocupa la misma zona que las demas y no puede coexistir con
         * ninguna: qth_show() apaga todas y menu_screen_close() la apaga
         * a ella. */
        qth_touch(x, y, pressed);
    } else if (s_menu_hora_active && s_touch_owner_is_menu) {
        /* La pantalla de sincronizar la hora ocupa la misma zona que las
         * demas y no puede coexistir con ninguna: hora_show() apaga todas y
         * menu_screen_close() la apaga a ella. */
        hora_touch(x, y, pressed);
    } else if (s_menu_detail_active && s_touch_owner_is_menu) {
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
        case UI_TOP_HIT_NCO:
            /* Mismo camino que la casilla de ajustes: una sola funcion que
             * conmuta, reaparca el oscilador si hace falta y repinta. */
            nco_conmutar();   /* el repintado del chip lo hace el */
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

    if (s_band_tap_active && !pressed) {
        accion_pantalla(GRID_BANDAS);
    }

    if (s_time_tap_active && !pressed) {
        menu_time_keypad_show();
    }

    if (s_modo_tap && !pressed) {
        if (ft8_modo_activo()) {
            /*
             * *** Por el dueno del proyecto: "deberia de permitirme reiniciar
             * el contador si le doy a la chapa de la derecha, para poder
             * cuadrar a mano la tanda" - despues de ver que "va desfasada
             * con 6". ***
             *
             * El cero de la ranura pasa a ser ESTE instante. Ver
             * ft8_modo_cuadra(): no toca el reloj, solo el desfase, porque a
             * FT8 no le importa la hora sino el segundo dentro de la ranura.
             *
             * Se repinta la cabecera en el acto y no en el cuadro siguiente:
             * este boton se toca mirando un reloj de verdad, y un numero que
             * tarda en saltar hace dudar de si el toque ha entrado.
             */
            ft8_modo_cuadra();
            digi_sync();
            ui_digi_draw_barra(&s_digi);
            ui_digi_draw_chip(&s_digi);
            debug_print("ft8: ranura cuadrada a mano\n");
        } else if (hfdl_modo_activo()) {
            /*
             * *** Por el dueno del proyecto: "le he dado donde deberia de ir
             * y me salen los modos sstv". ***
             *
             * Y le salian porque este bloque era un if/else de dos ramas:
             * FT8 cuadraba la ranura y TODO LO DEMAS abria la rejilla de
             * SSTV. Cuando anadi HFDL a la lista de modos que responden al
             * toque en la chapa -unas lineas mas arriba- se llevo el "todo lo
             * demas" sin que nadie lo mirara.
             *
             * Un else que significa "SSTV" y no lo dice es una trampa puesta
             * para el siguiente que anada un modo. Ahora cada modo dice el
             * suyo y el else no existe.
             *
             * La chapa de HFDL abre las frecuencias, lo mismo que el boton de
             * al lado: los dos estan en esa esquina y tener que acertar cual
             * es cual con un dedo en un tactil resistivo no aporta nada.
             */
            grid_show(GRID_HFDL);
        } else if (sstv_activo()) {
            /* grid_show() ya pone s_menu_open y el resto de banderas de la
             * pantalla activa. Ponerlo tambien aqui era un segundo sitio donde
             * acordarse. */
            grid_show(GRID_SSTV);
        }
    }

    if (s_fmt_tap && !pressed) {
        s_digi.btn2_press = 0U;
        /* Rueda de tres: no guardar -> 24 bits -> 16 bits -> no guardar. El
         * "no guardar" esta en la rueda a proposito: guardar cada foto que
         * entre sin forma de apagarlo llena el pendrive solo. */
        s_gsv_fallo = 0U;   /* tocar el boton tambien es "vuelve a intentarlo" */
        if (wefax_activo()) {
            s_wfx_guarda = (uint8_t)((s_wfx_guarda + 1U) % WFX_GUARDA_N);
        } else {
            s_sstv_guarda = (uint8_t)((s_sstv_guarda + 1U) % SSTV_GUARDA_N);
        }
        settings_mark_dirty();
        s_digi.btn2 = wefax_activo() ? gsv_rotulo_fax() : gsv_rotulo();
        ui_digi_draw_boton2(&s_digi);
        debug_print("sstv: formato de guardado cambiado\n");
    }

    /*
     * OJO CON EL `return` DE ESTAS DOS RAMAS. 27/09/2026.
     *
     * Se salta la limpieza de latches del final de la funcion, y ahi es
     * donde se baja s_touch_active. Con el latch puesto, el SIGUIENTE
     * toque no entra por `pressed && !s_touch_active`, o sea que no se
     * decide de quien es el gesto ni se arma ningun boton: ese toque se
     * pierde entero y solo funciona el de despues.
     *
     * Se ve como "el primer toque en la lista de frecuencias no hace
     * nada" y como "hay que darle dos veces al boton de saltar". Por eso
     * ahora saltan a `fin`, que es esa misma limpieza: un `goto` hacia
     * delante y no una copia de las veinte lineas, porque esos latches son
     * estaticos DE ESTA FUNCION y no se pueden bajar desde fuera.
     */
    if (s_frec_tap && !pressed) {
        s_frec_tap = 0U;
        s_digi.btn3_press = 0U;
        ui_digi_draw_boton3(&s_digi);
        if (hfdl_modo_activo()) {
            grid_show(GRID_HFDL);
        }
        goto fin;
    }

    if (s_salto_tap && !pressed) {
        s_salto_tap = 0U;
        s_digi.btn4_press = 0U;
        /* El rotulo es la frecuencia, asi que cambia con el salto: hay que
         * volver a apuntarlo ANTES de repintar el boton. */
        if (hfdl_modo_activo()) {
            hfdl_frec_siguiente();
            s_digi.btn4 = hfdl_frec_rotulo();
        } else if (ft8_modo_activo()) {
            ft8_frec_siguiente();
            s_digi.btn4 = ft8_frec_rotulo();
        } else if (wspr_modo_activo()) {
            wspr_frec_siguiente();
            s_digi.btn4 = wspr_frec_rotulo();
        } else if (ais_modo_activo()) {
            ais_frec_siguiente();
            s_digi.btn4 = ais_frec_rotulo();
        } else if (jtty_modo_activo()) {
            jtty_frec_siguiente();
            s_digi.btn4 = jtty_frec_rotulo();
        } else {
            /* nadie mas tiene cuarto boton */
        }
        ui_digi_draw_boton4(&s_digi);
        goto fin;
    }

    /*
     * EL BOTON DEL MAPA: pone la lista o el mapa. Ver btn5 en ui_digi.h.
     */
    if (s_mapabtn_tap && !pressed) {
        s_mapabtn_tap = 0U;
        s_ft8_mapa = (uint8_t)(s_ft8_mapa ? 0U : 1U);
        s_digi.btn5_press = s_ft8_mapa;
        ft8_panel_reinicia();   /* que se repinte entero, lista o mapa */
        ft8_panel_draw();
        ui_digi_draw_boton5(&s_digi);
        goto fin;
    }

    /*
     * LOS DOS MANDOS DEL ZOOM. Se doblan y se parten por la mitad, o sea
     * 1, 2, 4 y 8: los pasos de un tercio se notan como que no ha pasado
     * nada y obligan a dar seis toques para llegar a algun sitio.
     *
     * El ancla es el CENTRO de lo que se ve y no el boton que has tocado:
     * los mandos estan en una esquina, y anclar ahi dejaria el centro del
     * mapa corriendose a cada paso. Con el dedo -ver el arrastre- si se
     * ancla donde tocas, que ahi es lo que se espera.
     */
    if (s_zoom_tap && !pressed) {
        mapa_caja_t *c = mapa_caja();
        uint8_t antes = c->esc;
        uint8_t nueva = (s_zoom_tap == 1U) ? (uint8_t)(c->esc * 2U)
                                           : (uint8_t)(c->esc / 2U);

        (void)mapa_zoom(c, (nueva < 1U) ? 1U : nueva,
                        (int16_t)(c->x + c->w / 2), (int16_t)(c->y + c->h / 2));
        s_zoom_tap = 0U;
        if (c->esc != antes) { ft8_mapa_draw(); }
        goto fin;
    }

    /*
     * Y EL ARRASTRE. Se aplica en el MOVIMIENTO, no al soltar: un mapa que
     * no se mueve hasta que levantas el dedo no se siente como arrastrar,
     * se siente como una espera.
     */
    if (s_mapa_tap && pressed) {
        int16_t dx = (int16_t)((int16_t)x - s_mapa_ax);
        int16_t dy = (int16_t)((int16_t)y - s_mapa_ay);

        /* Dos pixeles de umbral: el panel resistivo tiembla un poco con el
         * dedo quieto, y sin umbral el mapa vibra mientras lo tocas. */
        if (dx > 2 || dx < -2 || dy > 2 || dy < -2) {
            mapa_arrastra(mapa_caja(), dx, dy);
            s_mapa_ax = (int16_t)x;
            s_mapa_ay = (int16_t)y;
            ft8_mapa_draw();
        }
        goto fin;
    }
    if (s_mapa_tap && !pressed) {
        s_mapa_tap = 0U;
        goto fin;
    }

    if (s_borrar_tap && !pressed) {
        s_digi.btn_press = 0U;
        ui_digi_draw_boton(&s_digi);
        if (hfdl_modo_activo()) {
            /* Igual que en FT8: vacia la lista y el contador de rafagas. La
             * rafaga que se este recibiendo sigue su camino y saldra cuando
             * acabe - borrar la lista no para el receptor. */
            hfdl_modo_borra();
            ft8_panel_reinicia();
            debug_print("hfdl: lista borrada\n");
        } else if (jtty_modo_activo()) {
            jtty_modo_borra();
            ft8_panel_reinicia();
            debug_print("jtty: lista borrada\n");
        } else if (ale_modo_activo()) {
            /* Vacia la tabla de estaciones. Util al cambiar de frecuencia:
             * lo que habia era de otro canal. */
            ale_modo_borra();
            ft8_panel_reinicia();
            debug_print("ale: tabla borrada\n");
        } else if (ais_modo_activo()) {
            /* Vacia la tabla de barcos y las cuentas. Util de verdad al
             * cambiar de canal o de sitio: lo que habia era de antes. */
            ais_modo_borra();
            ft8_panel_reinicia();
            debug_print("ais: tabla borrada\n");
        } else if (wspr_modo_activo()) {
            /* Igual que en los otros dos: vacia la lista y el contador de
             * oidas. La captura que este abierta sigue su camino y lo que
             * saque aparecera al cerrarse; borrar la lista no para el
             * receptor. */
            wspr_modo_borra();
            ft8_panel_reinicia();
            debug_print("wspr: lista borrada\n");
        } else if (ft8_modo_activo()) {
            /* *** Por el dueno del proyecto: "y no hay boton borrar?" ***
             * Vacia la lista y ya esta: la captura en curso sigue, asi que
             * lo que se este oyendo ahora saldra al cerrarse la ranura. */
            ft8_modo_borra();
            ft8_panel_reinicia();
            debug_print("ft8: lista borrada\n");
        } else if (sstv_activo()) {
            /*
             * En SSTV el boton EMPIEZA a pintar con el modo mas comun sin
             * esperar la cabecera. Sirve para cuando te enganchas a mitad de
             * foto: la cabecera ya paso y no va a volver hasta la siguiente.
             */
            sstv_fuerza(s_sstv_modo_idx);   /* el que diga la chapa - ver GRID_SSTV */
            sstv_panel_reinicia();
            debug_print("sstv: empezado a mano\n");
        } else if (wefax_activo()) {
            /*
             * En el fax el boton no borra: SINCRONIZA. Es el ajuste grueso
             * para cuando te enganchas a mitad de carta y la señal de fase ya
             * paso: dice "la linea empieza aqui" y se pone a pintar sin
             * esperar a nada.
             *
             * Es el mismo boton porque es el mismo sitio y solo hay uno;
             * lo que hace lo decide el modo, y el rotulo lo dice.
             */
            wefax_sincroniza();
            wefax_fuerza_imagen();
            fax_panel_reinicia();
            debug_print("wefax: sincronizado a mano\n");
        } else {
            /* Borra el contenido y marca para repintar: es la misma funcion
             * que se usa al entrar en un modo digital desde otro, asi que no
             * hay una segunda forma de dejar el panel limpio que un dia se
             * comporte distinto. */
            rtty_text_panel_reset();
            debug_print("texto digital: borrado a mano\n");
        }
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

fin:
    if (!pressed) {
        s_touch_active = 0U;   /* gesture over - the next press re-decides ownership */
        s_spec_drag_active = 0U;
        s_spec_drag_moved = 0U;
        s_freq_tap_active = 0U;
        s_time_tap_active = 0U;
        s_band_tap_active = 0U;
        s_borrar_tap = 0U;
        /* El de frecuencias de HFDL entra en la misma limpieza: si se pulsa
         * y el dedo se sale del boton antes de soltar, no tiene que quedarse
         * armado para el toque siguiente. */
        s_frec_tap = 0U;
        s_digi.btn3_press = 0U;
        s_salto_tap = 0U;          /* y el de "la siguiente", por lo mismo */
        s_digi.btn4_press = 0U;
        s_modo_tap = 0U;
        s_mapa_tap = 0U;
        s_zoom_tap = 0U;
        s_mapabtn_tap = 0U;
        /*
         * *** 24/09/2026, por el dueno del proyecto: "el boton de guardar
         * me parpadea despues de haberle dado", "diria que entre bmp 24 y
         * 16" ***
         *
         * Faltaba esta linea. Esta funcion se llama tambien sin dedo en la
         * pantalla, y la rama de soltar solo mira "el latch esta puesto y
         * no hay pulsacion": si el latch no se baja aqui, esa rama se
         * ejecuta en CADA vuelta a partir del toque. El boton no
         * parpadeaba - estaba rotando de formato decenas de veces por
         * segundo, que desde fuera es lo mismo.
         *
         * Todos los latches de esta zona se bajan aqui, en un solo sitio,
         * justamente para que anadir uno nuevo y olvidarse sea el unico
         * fallo posible. Fue el que cometi.
         */
        s_fmt_tap = 0U;
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
/* El oscilador del recentrado del zoom. Fase propia, separada de la de
 * demod_am.c: son dos cadenas distintas y cada una tiene que ser continua en
 * la suya. Lo derivado se recalcula cuando cambia alguna de sus dos entradas,
 * igual que en demod_am.c y por la misma razon. */
static nco_t s_zoom_nco;
static float s_zoom_mix_hz_puesto = 1e30f;
static float s_zoom_mix_fs_puesto = 0.0f;

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
     * uses. Hasta el 23/09/2026 era la misma rotacion de signos a Fs/4
     * fijos; con el NCO la distancia es cualquiera, asi que se hace con el
     * mismo oscilador que usa demod_am.c - y con SU PROPIA fase, porque son
     * dos cadenas independientes y los diezmadores de aqui llevan memoria:
     * un salto de fase entre bloques les ensuciaria la ventana siguiente.
     * WFM no tiene nada que corregir (el desplazamiento es 0), asi que en
     * ese modo esto se salta entero - la emisora ya esta en el centro. */
    if (demod_am_get_mix_hz() != 0.0f) {
        float fs_zoom = s_nonwfm_use_48k ? 48000.0f : 96000.0f;

        if (demod_am_get_mix_hz() != s_zoom_mix_hz_puesto
            || fs_zoom != s_zoom_mix_fs_puesto) {
            s_zoom_mix_hz_puesto = demod_am_get_mix_hz();
            s_zoom_mix_fs_puesto = fs_zoom;
            nco_freq(&s_zoom_nco, s_zoom_mix_hz_puesto, fs_zoom);
        }
        nco_mezcla(&s_zoom_nco, s_zoom_f_i, s_zoom_f_q, SDR_RX_BLOCK_SAMPLES);
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
/*
 * LA FRANJA DE ARRIBA: RELOJ, MEDIDOR Y CALIBRACION - 25/09/2026.
 *
 * *** Por el dueno del proyecto: "creo que el reloj se atrasa" ***
 *
 * Y se atrasaba, pero no por el RTC. Todo esto vivia DENTRO de
 * sdr_spectrum_waterfall_tick(), con un comentario que decia que corria
 * siempre, "incluso con el menu abierto". Y era verdad con el menu abierto...
 * y mentira en los modos digitales, porque ahi el bucle principal llama al
 * panel de texto EN LUGAR de a esa funcion. O sea que al entrar en RTTY, CW,
 * NAVTEX o FT8 el reloj se paraba y se quedaba con la hora de cuando
 * entraste. En la foto del 25/09: arriba ponia 05:15 y las decodificaciones
 * de FT8, que leen el RTC de verdad, decian 05:17 y 05:18.
 *
 * Un reloj parado no se ve parado. Se ve como un reloj que atrasa, y quien lo
 * mira se cree la hora que pone. Por eso esto sale de ahi: lo de la franja de
 * arriba no tiene nada que ver con pintar el espectro, y colgarlo de quien lo
 * pinta fue lo que lo ato a un modo concreto.
 *
 * Y DE PASO, EL OTRO ATRASO. El repintado se disparaba con
 * "g_msticks / 60000", o sea con los minutos DESDE EL ARRANQUE, mientras que
 * lo que se pinta sale del RTC. Las dos cuentas no comparten el borde de
 * minuto: si arrancas en el segundo 45, el reloj se repinta 45 s tarde cada
 * minuto, y durante 45 de cada 60 segundos ensena el minuto anterior. Ahora
 * el disparo es el minuto del RELOJ, o sea el mismo numero que se pinta, y
 * por construccion no puede ir desacompasado.
 *
 * Se queda con su propio limitador de cuadro para no cambiar la cadencia que
 * ya tenia (uno cada SPECTRUM_FRAME_MS): esto se saca del sitio equivocado,
 * no se acelera.
 */
/*
 * QUIEN EMITE EN LA FRECUENCIA QUE ESTAS OYENDO - 28/09/2026.
 *
 * *** Por el dueño del proyecto: "Bases EiBi/Aoki para Identificación
 * automática de estaciones". ***
 *
 * VA EN EL HUECO DEL RDS, y no es un apaño: es exactamente el mismo hueco
 * para exactamente la misma pregunta. El RDS contesta "que estoy
 * escuchando" en FM ancha; esto la contesta en onda corta y media. Y no
 * pueden coincidir nunca -el RDS solo existe en WFM-, asi que el sitio
 * estaba libre justo cuando hace falta.
 *
 * LA BUSQUEDA NO SE HACE EN CADA CUADRO. Son unas noventa lecturas del bus
 * SPI, que es lento; se rehace solo cuando cambia la frecuencia o cuando
 * cambia el minuto. Girando el mando eso son unas pocas por segundo, y
 * quieto, una por minuto.
 *
 * Y LO QUE SE ENSEÑA ES UNA PISTA, NO UN HECHO. Una lista de emisoras es
 * un horario publicado con meses de antelacion: las emisoras cambian,
 * cierran y se mueven. Por eso el renglon de abajo lleva SIEMPRE el
 * horario previsto al lado del nombre - para que se vea que esto sale de
 * una tabla y no de haber reconocido nada- y por eso, cuando la lista ha
 * caducado, lo dice.
 */
static uint32_t s_emis_hz_visto = 0xFFFFFFFFUL;
static uint32_t s_emis_min_visto = 0xFFFFFFFFUL;

static char *emis_num(char *p, uint32_t v, uint8_t cifras)
{
    char t[8];
    uint8_t n = 0U;

    do { t[n++] = (char)('0' + (v % 10UL)); v /= 10UL; } while (v != 0UL && n < 8U);
    while (n < cifras) { t[n++] = '0'; }
    while (n > 0U) { *p++ = t[--n]; }
    return p;
}

static void emis_pon(uint32_t hz, uint16_t min_local)
{
    emisoras_r_t r[3];
    uint16_t min_utc = min_local;
    uint8_t  dia = EMISORAS_DIA_CUALQUIERA;
    uint8_t  n;
    char *p;
    char tmp[24];

    s_emis_nom[0] = '\0';
    s_emis_txt[0] = '\0';
    if (!emisoras_hay()) { return; }

    /*
     * El dia de la semana sale de la fecha del RTC. Si el cristal no
     * arranco no hay fecha, y entonces NO se filtra por dia: ver
     * EMISORAS_DIA_CUALQUIERA. Enseñar una emisora de mas es mucho menos
     * malo que esconder la que se esta oyendo.
     */
    if (!rtc_hw_lxtal_failed()) {
        rtc_hw_datetime_t dt;

        rtc_hw_get(&dt);
        dia = emisoras_dia_semana(dt.year, dt.month, dt.day);
    }
    emisoras_utc(min_local, (dia == EMISORAS_DIA_CUALQUIERA) ? 0U : dia,
                 &min_utc, &dia);
    if (rtc_hw_lxtal_failed()) { dia = EMISORAS_DIA_CUALQUIERA; }

    n = emisoras_busca(hz, 600UL, min_utc, dia, r, 3U);
    if (n == 0U) { return; }
    if (!r[0].ahora) { return; }   /* la hay, pero a otra hora: no se dice nada */

    emisoras_texto(r[0].nombre, s_emis_nom, (uint8_t)sizeof s_emis_nom);

    p = s_emis_txt;
    emisoras_pais(r[0].pais, tmp, (uint8_t)sizeof tmp);
    { const char *q = tmp; while (*q != '\0') { *p++ = *q++; } }
    if (r[0].sitio != EMISORAS_SIN_TEXTO) {
        const char *q;

        emisoras_texto(r[0].sitio, tmp, (uint8_t)sizeof tmp);
        q = tmp;
        if (*q != '\0') { *p++ = ' '; *p++ = '-'; *p++ = ' '; }
        while (*q != '\0') { *p++ = *q++; }
    }
    if (r[0].idioma != EMISORAS_SIN_TEXTO) {
        const char *q;

        emisoras_texto(r[0].idioma, tmp, (uint8_t)sizeof tmp);
        q = tmp;
        if (*q != '\0') { *p++ = ' '; *p++ = '-'; *p++ = ' '; }
        while (*q != '\0') { *p++ = *q++; }
    }
    *p++ = ' '; *p++ = '-'; *p++ = ' ';
    p = emis_num(p, r[0].ini / 60U, 2U); *p++ = ':';
    p = emis_num(p, r[0].ini % 60U, 2U); *p++ = '-';
    p = emis_num(p, r[0].fin / 60U, 2U); *p++ = ':';
    p = emis_num(p, r[0].fin % 60U, 2U);
    *p++ = ' '; *p++ = 'U'; *p++ = 'T'; *p++ = 'C';
    if (n > 1U) {
        *p++ = ' '; *p++ = '('; *p++ = '+';
        p = emis_num(p, (uint32_t)(n - 1U), 1U);
        *p++ = ')';
    }
    *p = '\0';
}

/*
 * LA MARQUESINA DE LAS EMISORAS - 28/09/2026.
 *
 * *** El dueño, con una foto de la cabecera: "Canarias HFDL (I" y "CNR -
 * -HF - 00:00-2" cortados a media palabra. "O haces mas grande el campo de
 * texto o lo haces marquesina". ***
 *
 * Grande no cabe: el hueco es lo que queda entre la pastilla de banda y el
 * reloj. Asi que los dos renglones corren - PERO SOLO EL QUE NO QUEPA-.
 *
 * Esa condicion es lo importante. El mismo hueco lo usa el RDS, donde el
 * nombre son ocho caracteres y entra de sobra; poner a correr un texto que
 * ya se lee entero no es un adorno, es quitarle al usuario la posibilidad
 * de leerlo de un vistazo. Se mide y se decide, no se mueve por si acaso.
 *
 * Los dos renglones llevan su propio desplazamiento y su propia vuelta:
 * atarlos al mismo haria que el corto esperase al largo, o que el largo se
 * cortase a la mitad. Y van al mismo ritmo que la marquesina del RDS (40
 * ms, 2 pixeles), que es el que ya se ajusto a ojo en su dia.
 */
static int16_t  s_emis_off_nom, s_emis_off_txt;
static int16_t  s_emis_w_nom, s_emis_w_txt;
static uint32_t s_emis_next_ms;

/* Cuanto hay que correr un texto para que se lea entero, o 0 si ya cabe.
 * El +8 es el respiro entre el final y el principio de la vuelta, el mismo
 * que usa el RDS. */
static int16_t emis_recorrido(int16_t w)
{
    int16_t hueco = ui_top_rds_ancho();

    if (hueco <= 0 || w <= hueco) { return 0; }
    return (int16_t)(w + 8);
}

static void emis_marq_poll(void)
{
    int16_t rn, rt;

    if (!emisoras_hay() || demod_am_get_mode() == DEMOD_MODE_WFM
        || s_menu_open || s_screen_asleep || !s_pantalla_pintada) {
        return;
    }
    if (s_emis_nom[0] == '\0' && s_emis_txt[0] == '\0') { return; }

    rn = emis_recorrido(s_emis_w_nom);
    rt = emis_recorrido(s_emis_w_txt);
    if (rn == 0 && rt == 0) { return; }   /* caben los dos: nada que mover */

    if ((int32_t)(g_msticks - s_emis_next_ms) < 0) { return; }
    s_emis_next_ms = g_msticks + RDS_MARQ_MS;

    if (rn != 0) {
        s_emis_off_nom = (int16_t)(s_emis_off_nom + RDS_MARQ_PX);
        if (s_emis_off_nom > rn) { s_emis_off_nom = (int16_t)(-ui_top_rds_ancho()); }
    }
    if (rt != 0) {
        s_emis_off_txt = (int16_t)(s_emis_off_txt + RDS_MARQ_PX);
        if (s_emis_off_txt > rt) { s_emis_off_txt = (int16_t)(-ui_top_rds_ancho()); }
    }
    s_top.rds_nombre = (s_emis_nom[0] != '\0') ? s_emis_nom : 0;
    s_top.rds        = (s_emis_txt[0] != '\0') ? s_emis_txt : 0;
    s_top.rds_nom_off = s_emis_off_nom;
    s_top.rds_off     = s_emis_off_txt;
    ui_top_draw_rds(&s_top);
}

static void emisoras_tick(void)
{
    uint32_t min_ahora;

    if (!emisoras_hay()) { return; }
    min_ahora = reloj_ahora_ms() / 60000UL;
    if (s_tune_hz == s_emis_hz_visto && min_ahora == s_emis_min_visto) {
        emis_marq_poll();
        return;
    }
    s_emis_hz_visto = s_tune_hz;
    s_emis_min_visto = min_ahora;
    emis_pon(s_tune_hz, (uint16_t)(min_ahora % 1440UL));
    if (demod_am_get_mode() != DEMOD_MODE_WFM) {
        /*
         * Se vuelve a empezar SIEMPRE que cambia lo que pone, que aqui es
         * al cambiar de frecuencia o de minuto. No es el caso del RDS -que
         * recibe el texto de cuatro en cuatro caracteres y por eso no puede
         * reiniciarse en cada cambio-: esto sale de una tabla y llega
         * entero de una vez.
         */
        s_emis_w_nom = ui_top_rds_nombre_w(s_emis_nom);
        s_emis_w_txt = ui_top_rds_texto_w(s_emis_txt);
        s_emis_off_nom = 0;
        s_emis_off_txt = 0;
        s_top.rds_nombre = (s_emis_nom[0] != '\0') ? s_emis_nom : 0;
        s_top.rds        = (s_emis_txt[0] != '\0') ? s_emis_txt : 0;
        s_top.rds_nom_off = 0;
        s_top.rds_off     = 0;
        ui_top_draw_rds(&s_top);
    }
}

static void franja_arriba_tick(void)
{
    static uint32_t s_prox_ms = 0UL;
    static uint32_t s_min_visto = 0xFFFFFFFFUL;
    uint32_t min_ahora;

    if ((int32_t)(g_msticks - s_prox_ms) < 0) { return; }
    /* s_frame_ms y no SPECTRUM_FRAME_MS: esa macro se define mas abajo, con
     * el espectro. Es el mismo numero - ver su comentario alli. */
    s_prox_ms = g_msticks + s_frame_ms;

    smeter_draw(smeter_segments_from_peak(demod_am_get_signal_peak()));
    smeter_dbfs_uart_report(demod_am_get_signal_peak()); /* see its own comment - S-meter calibration aid */
    /* *** 01/09/2026: replaces the old snr_update_and_draw() - reads
     * demod_am_get_signal_peak() directly, same as the S-meter bar
     * right above it, so it stays live exactly like the S-meter
     * does. See smeter_dbm_update_and_draw()'s own comment. */
    smeter_dbm_update_and_draw(demod_am_get_signal_peak());
    sam_calib_display_draw(); /* MS5351 PPM calibration readout, 21/08/2026 - see its own comment; needs to update live as the PLL converges, same cadence as the S-meter above */

    min_ahora = reloj_ahora_ms() / 60000UL;
    if (min_ahora != s_min_visto) {
        s_min_visto = min_ahora;
        time_display_draw();
    }
    emisoras_tick();   /* quien emite aqui - ver su comentario */
}

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
/* ---------------------------------------------------------------------
 * EL PRESUPUESTO DE CADA FOTOGRAMA, AJUSTABLE. 25/09/2026.
 * ---------------------------------------------------------------------
 * Era 33 fijo, o sea 30 fotogramas por segundo. Ese numero mandaba mientras
 * dibujar costaba mas que eso -el fotograma tardaba 69 ms y salian 11
 * lineas de cascada por segundo-, pero despues de reescribir el bucle del
 * espectro el trabajo baja a 20 ms y el que manda es este tope.
 *
 * ESE 20 YA NO ES UNA ESTIMACION (25/09/2026). Lo era: lo escribi yo aqui
 * sin pedirle a nadie que mirara el contador. Leido por el dueno en la
 * ventana de informacion, con el espectro corriendo:
 *
 *     antes:  69 ms  =  FFT 0 + espectro 65 + cascada 4
 *     ahora:  20 ms  =  FFT 0 + marco 0 + traza 17 (empuje 0) + cascada 3
 *
 * El espectro de 65 a 17, y el EMPUJE A CERO: eran 10-11 ms de CPU parada
 * mirando el bus y ahora se solapan enteros con el montaje de la otra media
 * banda (ver las dos medias bandas en spectrum.c). No es que el bus vaya
 * mas rapido - es que ya no se le espera.
 *
 * Queda una consecuencia que se ve en el mismo sitio: el contador de FFT
 * marca "1/1", o sea UNA FFT promediada por fotograma. A 33 ms entraban dos
 * o tres. Menos promedio es un suelo de ruido mas inquieto, que es
 * exactamente el precio que este comentario avisaba de que se pagaba.
 *
 * LO QUE CUESTA BAJARLO, dicho claro porque no es gratis: entre fotograma y
 * fotograma se acumulan varias FFT y se dibuja su MEDIA. Menos tiempo,
 * menos FFT promediadas, y el suelo de ruido se ve mas inquieto. Con 33 ms
 * entran dos o tres; con 22, una o dos.
 *
 * Por eso se toca desde la ventana de informacion y NO se guarda en los
 * ajustes: es una preferencia que se mira, no un numero que se calcula. Se
 * arranca en 22 porque es lo que pidio el dueno.
 */
#define SPECTRUM_FRAME_MS        (s_frame_ms)
#define SPECTRUM_MAX_FFT_PER_FRAME 6U

    /* s_db_frame subio a fichero (22/09/2026): es EXACTAMENTE lo que se ve
     * dibujado, y el toque en el espectro necesita mirarlo para caer sobre
     * la señal. Sigue siendo el mismo almacenamiento estatico. */
    static float    s_db_sum[FFT_BINS_IQ];
    static uint32_t s_db_count = 0U;
    /* La foto de media tanda para "Muy rapida" (ver el comentario de k_wf).
     * s_wf_mitad_n a 0 significa "este frame no tiene dos mitades que
     * separar" - es lo que hace que en ZOOM y con el analizador la
     * velocidad se comporte sola como "Rapida", sin un caso aparte. */
    static float    s_wf_mitad[FFT_BINS_IQ] TCMRAM_BSS;
    static uint32_t s_wf_mitad_n = 0U;
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

            /* La foto de media tanda: se hace SIEMPRE, cueste lo que cueste
             * -1 kB de copia por frame- y no solo cuando "Muy rapida" esta
             * puesta, porque cambiar de velocidad no puede depender de si
             * el frame en curso decidio ya guardarla.
             *
             * POR TIEMPO, NO POR CUENTA (25/09/2026). Antes se hacia cuando
             * llegaba la FFT numero SPECTRUM_MAX_FFT_PER_FRAME/2, o sea la
             * tercera, dando por supuesto que en cada frame llegan las seis.
             * Y no llegan: medido en la radio, el frame tarda 26 ms en
             * dibujarse y entre uno y otro solo entran dos o tres bloques.
             * Con tres, la foto se hacia en la ultima y ya no venia ninguna
             * detras, asi que "hay segunda mitad" (s_db_count >
             * s_wf_mitad_n) salia 3 > 3, FALSO; con dos, la foto ni se
             * hacia. En los dos casos "Muy rapida" se caia a una linea por
             * frame en silencio - el dueno lo vio como "pido 60 y salen 26".
             *
             * Partir por TIEMPO no da nada por supuesto: la mitad del frame
             * es la mitad del frame lleguen las FFT que lleguen, y ademas es
             * lo que de verdad significa dibujar dos lineas - la de arriba
             * es la primera mitad del rato y la de abajo la segunda. */
            if ((s_wf_mitad_n == 0U) &&
                ((int32_t)(g_msticks -
                           (s_next_frame_ms - (SPECTRUM_FRAME_MS / 2U))) >= 0)) {
                for (bi = 0; bi < FFT_BINS_IQ; bi++) {
                    s_wf_mitad[bi] = s_db_sum[bi];
                }
                s_wf_mitad_n = s_db_count;
            }
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
    /* La franja de arriba ya no se pinta aqui - ver franja_arriba_tick(). */

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
    /*
     * De donde salen los bins que se van a dibujar.
     *
     * Con el analizador puesto, del AUDIO: la FFT no se calcula aqui dentro
     * de la interrupcion sino en el bucle principal, que es donde estamos, y
     * solo cuando el diezmador ha juntado sus 256 muestras. Si aun no las
     * tiene se deja el cuadro anterior tal cual: con los zoom estrechos una
     * ventana tarda mas de un cuadro en llenarse, y repintar el mismo dato
     * es mejor que parpadear.
     *
     * Sin analizador, la media de todas las FFT de radiofrecuencia que hayan
     * entrado en esta ventana de cuadro, como siempre.
     */
    if (analiz_activo()) {
        if (analiz_listo()) { analiz_bins(s_db_frame); }
    } else {
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
        if (!s_db_traza_lista) {
            /* First frame ever: nothing to blend with yet - seed
             * directly, rather than blending against a zeroed array
             * (which would otherwise show a slow fade-IN from silence
             * on every boot, not just a jitter reduction). */
            for (bi = 0; bi < FFT_BINS_IQ; bi++) {
                s_db_traza[bi] = s_db_frame[bi];
            }
            s_db_traza_lista = 1U;
        } else {
            for (bi = 0; bi < FFT_BINS_IQ; bi++) {
                s_db_traza[bi] = s_spectrum_smooth_alpha * s_db_traza[bi]
                                  + (1.0f - s_spectrum_smooth_alpha) * s_db_frame[bi];
            }
        }
        /* Y AQUI NO SE COPIA DE VUELTA. Antes se volcaba s_db_traza sobre
         * s_db_frame y por eso la cascada heredaba el suavizado - ver el
         * comentario de los dos arrays, arriba del todo. La cascada lee
         * s_db_frame, que se queda crudo. */
        s_db_frame_listo = 1U;
    }

    if (s_spec_agc_enabled) {
        /* La escala de color sale de la TRAZA y no del crudo, a proposito:
         * es la misma que antes, y con ella los colores de la cascada no
         * bailan de un cuadro a otro aunque lo que se pinte si sea nitido. */
        spec_agc_step(s_db_traza, FFT_BINS_IQ, &s_db_min, &s_db_max);
    }

    g_lcd_ciclos = 0UL;   /* ver g_lcd_ciclos: se acumula durante este bloque */
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

        if (s_spec_zoom == SPEC_ZOOM_1X && demod_am_get_mix_hz() != 0.0f) {
            /*
             * Pixeles por hercio por la distancia real. Hasta el 23/09/2026
             * esto era SPEC_TRACE_W/4 escrito a pelo, y era correcto porque
             * la distancia era SIEMPRE Fs/4 y el termino de Fs se cancelaba
             * -por eso valia igual a 96 kHz que a 48-. Con el NCO la
             * distancia es cualquiera, asi que hay que dividir de verdad.
             *
             * Sigue dando SPEC_TRACE_W/4 exacto cuando el desplazamiento
             * vuelve a ser Fs/4: es la misma cuenta, sin el atajo.
             */
            center_mark_offset_px =
                (int16_t)((demod_am_get_mix_hz() * (float)SPEC_TRACE_W)
                          / (float)spec_zoom_full_span_hz());
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

            if (mode == DEMOD_MODE_AM || mode == DEMOD_MODE_SAM
                || mode == DEMOD_MODE_USB || mode == DEMOD_MODE_LSB) {
                /*
                 * DE DONDE SALE EL ANCHO - 25/09/2026, por el dueno del
                 * proyecto: "si pongo lo de bw y elijo un filtro mas pequeno
                 * luego en la ventana principal el espacio entre las dos
                 * lineas verticales deberia de ajustarse".
                 *
                 * Y no se ajustaba. Esto leia k_audio_bw_hz[], que es la
                 * tabla de tres posiciones del boton BW (4K0/2K3/1K8), y el
                 * filtro de verdad ya no es eso: desde que hay dos cortes por
                 * familia -paso alto y paso bajo, ajustables en Ajustes- lo
                 * que suena lo deciden demod_am_get_filtro()/audiofil.c, y el
                 * boton BW no es mas que tres atajos que escriben ahi.
                 *
                 * O sea que la pantalla ensenaba una cosa y la radio hacia
                 * otra. Eso es peor que no ensenar nada: quien mira la banda
                 * sombreada para saber que esta oyendo, se la cree.
                 *
                 * Ahora sale de fil_cortes(), que devuelve los cortes YA
                 * RECORTADOS a lo que audiofil.c puede hacer de verdad (ver
                 * demod_am_set_filtro()), asi que lo sombreado es lo que
                 * suena y no lo que se pidio.
                 *
                 * Y de paso entra el PASO ALTO, que antes se ignoraba: en USB
                 * con 0,3-2,7 la banda empieza 300 Hz por encima de la marca,
                 * no pegada a ella.
                 */
                uint16_t f_lo = 0U, f_hi = 0U;
                uint32_t span = spec_zoom_full_span_hz();
                int16_t lo_px, hi_px;

                fil_cortes(&f_lo, &f_hi);

                /* *** 01/09/2026: rate-aware via spec_zoom_full_span_
                 * hz() *** - see that function's own comment for the
                 * full "why". */
                lo_px = (int16_t)((uint32_t)SPEC_TRACE_W * f_lo / span);
                hi_px = (int16_t)((uint32_t)SPEC_TRACE_W * f_hi / span);

                band_active = 1U;
                if (mode == DEMOD_MODE_USB) {
                    band_lo_offset_px = (int16_t)(center_mark_offset_px + lo_px);
                    band_hi_offset_px = (int16_t)(center_mark_offset_px + hi_px);
                } else if (mode == DEMOD_MODE_LSB) {
                    band_lo_offset_px = (int16_t)(center_mark_offset_px - hi_px);
                    band_hi_offset_px = (int16_t)(center_mark_offset_px - lo_px);
                } else { /* AM y SAM: doble banda lateral */
                    /*
                     * UNA sola region, de -hi a +hi, aunque con paso alto la
                     * banda de paso real sean DOS tiras con un hueco en el
                     * medio. El hueco de un paso alto de 300 Hz mide 5 px a
                     * zoom 1x y esta justo debajo de la marca de
                     * demodulacion, que se pinta encima: partir la region en
                     * dos anadiria codigo para dibujar algo que no se ve.
                     */
                    band_lo_offset_px = (int16_t)(center_mark_offset_px - hi_px);
                    band_hi_offset_px = (int16_t)(center_mark_offset_px + hi_px);
                }
            }
        }

        /* ETAPA 3b: los ejes, por comparacion - ver spec_chrome_tick(). Va
         * ANTES de la traza para que, si la escala ha cambiado en este
         * frame, el numero nuevo y la traza nueva salgan a la vez. */
        {
            /* El marco del espectro (regla, canaletas, panel) aparte de la
             * traza: los dos estaban dentro del mismo cronometro y por eso
             * "espectro: 65 ms" no decia donde mirar. El marco pasa por
             * gfx2_render(), que es el compositor entero con sus mezclas y
             * sus esquinas redondeadas; la traza es un bucle de pixeles.
             * Son dos bichos muy distintos y hay que verlos por separado. */
            uint32_t t0 = DWT->CYCCNT;
            spec_chrome_tick();
            s_chrome_us = (uint16_t)(((DWT->CYCCNT - t0) / (SystemCoreClock / 1000000U)) > 65535U
                                     ? 65535U
                                     : ((DWT->CYCCNT - t0) / (SystemCoreClock / 1000000U)));
        }

        /* Con el analizador puesto son los bins del AUDIO y son menos: la
         * FFT de entrada real da 128 utiles y solo se pinta el 80% -ver
         * analizador.h para por que la banda util acaba ahi-. Y no hay marca
         * de demodulacion ni banda de paso que sombrear, porque en audio no
         * existen. */
        if (analiz_activo()) {
            spectrum_draw(s_db_traza, analiz_bins_utiles(),
                          SPC_TRACE_X, SPC_TRACE_Y,
                          SPC_TRACE_W, SPC_TRACE_H,
                          s_db_min, s_db_max,
                          0, 0U, 0, 0);
        } else {
            spectrum_draw(s_db_traza, FFT_BINS_IQ,
                          SPC_TRACE_X, SPC_TRACE_Y,
                          SPC_TRACE_W, SPC_TRACE_H,
                          s_db_min, s_db_max,
                          center_mark_offset_px,
                          band_active, band_lo_offset_px, band_hi_offset_px);
        }
    }
    t_spec1 = DWT->CYCCNT;

    /* Guardadas aqui, donde los dos cronometros acaban de cerrarse y tienen
     * valor. Fuera del "cada 30 fotogramas" de mas abajo: aquello era para no
     * ahogar el puerto serie, y esto no va al puerto serie sino a una
     * variable que se lee al abrir la ventana de informacion. */
    {
        uint32_t d = SystemCoreClock / 1000000U;
        uint32_t e = (t_spec1 - t_spec0) / d;

        uint32_t g = g_lcd_ciclos / d;

        s_spec_us = (uint16_t)((e > 65535U) ? 65535U : e);
        s_push_us = (uint16_t)((g > 65535U) ? 65535U : g);
        s_fft_us  = (uint16_t)((fft_us > 65535U) ? 65535U : fft_us);
    }

    t_wf0 = DWT->CYCCNT;
    /* Waterfall: one row of WATERFALL_WIDTH px, each column mapped to
     * its FFT bin, colored through the shared (LUT-backed) palette.
     * Uses the frame-averaged dB, so the waterfall inherits the same
     * noise smoothing as the trace. Blitted just inside the panel
     * border, same x origin as the spectrum trace so columns line up
     * vertically between the two views. */
    {
        static TCMRAM_BSS float s_wf_sum[FFT_BINS_IQ];
        uint16_t b;

        uint16_t nbins = analiz_activo() ? analiz_bins_utiles() : (uint16_t)FFT_BINS_IQ;

        /* Las dos lineas por frame solo salen si de verdad hay dos medidas
         * distintas que bajar: el frame llego a guardar su foto de media
         * tanda Y despues siguio acumulando. Con el analizador de audio
         * puesto s_db_sum lleva radiofrecuencia y lo que se dibuja es audio
         * -analiz_bins() escribe sobre s_db_frame-, asi que ahi no vale; en
         * ZOOM s_db_count nunca pasa de 1 y la foto no llega a hacerse. En
         * los dos casos se cae solo al camino normal de una linea. */
        s_tanda_n     = (uint8_t)((s_db_count  > 255U) ? 255U : s_db_count);
        s_tanda_mitad = (uint8_t)((s_wf_mitad_n > 255U) ? 255U : s_wf_mitad_n);

        uint8_t dos = ((k_wf[s_wf_vel].mult == 2U)
                       && !analiz_activo()
                       && (s_wf_mitad_n > 0U)
                       && (s_db_count > s_wf_mitad_n)) ? 1U : 0U;

        if (dos) {
            float inv_a = 1.0f / (float)s_wf_mitad_n;
            float inv_b = 1.0f / (float)(s_db_count - s_wf_mitad_n);

            /* Primera mitad del frame. */
            for (x = 0; x < WATERFALL_WIDTH; x++) {
                uint32_t bin = ((uint32_t)x * nbins) / WATERFALL_WIDTH;
                line[x] = spectrum_colormap_index(s_wf_mitad[bin] * inv_a,
                                                  s_db_min, s_db_max);
            }
            waterfall_push_line(line); s_wf_lineas++;

            /* Y lo que vino despues, que es el acumulado MENOS la foto. */
            for (x = 0; x < WATERFALL_WIDTH; x++) {
                uint32_t bin = ((uint32_t)x * nbins) / WATERFALL_WIDTH;
                line[x] = spectrum_colormap_index(
                              (s_db_sum[bin] - s_wf_mitad[bin]) * inv_b,
                              s_db_min, s_db_max);
            }
            waterfall_push_line(line); s_wf_lineas++;

            /* UN solo volcado para las dos: lo caro es esto, no las lineas
             * (ver el comentario de k_wf). */
            waterfall_blit(SPEC_TRACE_X, WF_Y, spectrum_colormap_lut());
            s_wf_frames = 0U;
        } else {
            /* Camino de siempre: acumular frames enteros y bajar su media.
             * El primero COPIA en vez de sumar, asi que un cambio de
             * velocidad -que pone s_wf_frames a 0- no arrastra lo que
             * hubiera quedado dentro de la velocidad anterior. */
            if (s_wf_frames == 0U) {
                for (b = 0; b < nbins; b++) { s_wf_sum[b] = s_db_frame[b]; }
            } else {
                for (b = 0; b < nbins; b++) { s_wf_sum[b] += s_db_frame[b]; }
            }
            s_wf_frames++;

            if (s_wf_frames >= k_wf[s_wf_vel].div) {
                float inv = 1.0f / (float)s_wf_frames;
                for (x = 0; x < WATERFALL_WIDTH; x++) {
                    uint32_t bin = ((uint32_t)x * nbins) / WATERFALL_WIDTH;
                    line[x] = spectrum_colormap_index(s_wf_sum[bin] * inv,
                                                      s_db_min, s_db_max);
                }
                waterfall_push_line(line); s_wf_lineas++;
                waterfall_blit(SPEC_TRACE_X, WF_Y, spectrum_colormap_lut());
                s_wf_frames = 0U;
            }
        }
    }
    t_wf1 = DWT->CYCCNT;

    /* SOLO si en este fotograma hubo volcado de verdad. Esto estaba mal en
     * la primera version: se guardaba t_wf1-t_wf0 en TODOS los fotogramas, y
     * en las velocidades lentas la mayoria de los fotogramas no vuelcan nada
     * -a "Muy lenta" solo uno de cada ocho-, asi que lo que salia en pantalla
     * dependia de si el ultimo fotograma antes de abrir la ventana habia
     * tocado volcar o no. Una medida que sale distinta segun cuando la mires
     * no es una medida. */
    if (s_wf_lineas != s_wf_lineas_antes) {
        s_wf_ciclos = t_wf1 - t_wf0;
    }
    s_wf_lineas_antes = s_wf_lineas;
    s_fotogramas++;

    /* La ventana de un segundo se cierra con el reloj de verdad (g_msticks)
     * y no contando fotogramas: contar fotogramas daria las lineas por
     * fotograma, que es el numero que ya sabemos y no el que falta.
     *
     * El salto grande se descarta en vez de contarlo. Pasa al cerrar un menu:
     * mientras hay un menu abierto la cascada no se dibuja y este bloque no
     * corre, asi que al volver han pasado varios segundos con las lineas a
     * medio contar. Contarlo daria una primera lectura falsa y baja, justo la
     * que estarias mirando. */
    {
        uint32_t ahora = g_msticks;
        uint32_t va    = (uint32_t)(ahora - s_wf_ms0);

        if (va >= 2000U) {
            s_wf_lineas = 0U;
            s_wf_lineas_antes = 0U;
            s_fotogramas = 0U;
            s_wf_ms0    = ahora;
        } else if (va >= 1000U) {
            s_wf_lps    = s_wf_lineas;
            s_fps       = s_fotogramas;
            if (s_wf_lps != 0U) { s_wf_lps_vivo = s_wf_lps; }
            if (s_fps    != 0U) { s_fps_vivo    = s_fps;    }
            s_wf_lineas = 0U;
            s_wf_lineas_antes = 0U;
            s_fotogramas = 0U;
            s_wf_ms0    = ahora;
        }
    }
    } /* !s_menu_open - see this block's opening comment above */

    /* Frame drawn (or skipped, if the menu covered it): restart the
     * accumulator for the next window either way. La foto de media tanda
     * se invalida aqui mismo: pertenece al frame que acaba de irse. */
    s_db_count = 0U;
    s_wf_mitad_n = 0U;

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
    tics_init();

    if (SysTick_Config(SystemCoreClock / 1000U)) {
        while (1) {
            /* SysTick configuration failed - should never happen */
        }
    }

    /*
     * Y se le sube la prioridad, que SysTick_Config() la deja en la MAS BAJA
     * que existe. Eso deja el reloj de la radio por debajo del procesado de
     * audio, que puede tardar milisegundos: cada vez que el audio se pasa de
     * uno, el tic se retrasa, y si se pasa de dos, se pierde. Ver irq_prio.h.
     *
     * Va DESPUES de SysTick_Config a proposito: esa funcion escribe la
     * prioridad por su cuenta, asi que ponerla antes no serviria de nada.
     */
    NVIC_SetPriority(SysTick_IRQn, IRQ_PRIO_SYSTICK);
}

/*
 * ¿SE ESTAN PERDIENDO TICS?
 *
 * El contador de milisegundos depende de que su interrupcion llegue a correr.
 * El contador de CICLOS del nucleo (DWT) no depende de ninguna interrupcion:
 * es hardware que cuenta solo. Comparando los dos se sabe si el reloj se esta
 * quedando corto, y cuanto.
 *
 * Esto no es un adorno de diagnostico: es la unica forma de comprobar desde
 * fuera que el reparto de prioridades de irq_prio.h hace lo que dice. Sin
 * esto, "el reloj va bien" seria una opinion.
 *
 * El contador de ciclos da la vuelta cada 21 segundos a 200 MHz. Da igual: la
 * resta entre dos lecturas sale bien sola mientras se lea mas a menudo que
 * eso, y esto se llama en cada vuelta del bucle principal.
 */
static uint64_t s_ciclos_tot;
static uint32_t s_ciclos_ant;
static uint8_t  s_tics_listo;

static void tics_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_ciclos_ant = DWT->CYCCNT;
    s_ciclos_tot = 0U;
    s_tics_perdidos = 0U;
    s_tics_listo = 0U;
}

static void tics_poll(void)
{
    extern volatile uint32_t g_msticks;
    uint32_t ahora = DWT->CYCCNT;
    uint64_t ms_reales;

    s_ciclos_tot += (uint64_t)(ahora - s_ciclos_ant);
    s_ciclos_ant = ahora;

    ms_reales = s_ciclos_tot / (uint64_t)(SystemCoreClock / 1000U);

    /* Los primeros dos segundos no cuentan: entre que arranca el contador de
     * ciclos y que arranca el de milisegundos hay un trozo de arranque que no
     * es un tic perdido, es que no habian empezado a la vez. */
    if (!s_tics_listo) {
        if (ms_reales > 2000U) {
            s_tics_listo = 1U;
            s_ciclos_tot = (uint64_t)g_msticks * (uint64_t)(SystemCoreClock / 1000U);
        }
        return;
    }

    if (ms_reales > (uint64_t)g_msticks) {
        uint64_t d = ms_reales - (uint64_t)g_msticks;
        if (d > 0xFFFFFFFFULL) { d = 0xFFFFFFFFULL; }
        s_tics_perdidos = (uint32_t)d;
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
