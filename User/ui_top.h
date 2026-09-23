#ifndef UI_TOP_H_INCLUDED
#define UI_TOP_H_INCLUDED

#include <stdint.h>

/*
 * Cabecera y barra de estado del DEEPSDR 101, dibujadas con gfx2.
 *
 * POR QUE ESTO ES UN MODULO APARTE
 * --------------------------------
 * El dibujo va separado del estado a proposito: main.c rellena una estructura
 * con lo que la radio tiene ahora mismo y llama a ui_top_draw(). Asi este
 * fichero no depende de nada del firmware salvo gfx2, y el simulador de host
 * puede compilar EXACTAMENTE el mismo codigo con valores inventados. Lo que se
 * ve en el PNG es lo que va a salir en el panel, no una reconstruccion
 * parecida.
 *
 * GEOMETRIA
 * ---------
 * Se respeta la que ya tiene el firmware, para no mover nada de lo que hay
 * debajo:
 *
 *   y  0..63   cabecera        (TOP_H = 64)
 *   y 64..103  barra de estado (STATUS_STRIP_Y = 64, STATUS_STRIP_H = 40)
 *
 * Es mas apretado que el diseno original, que usaba 72 + 42. Se ajusta en vez
 * de mover el espectro: durante un port por etapas, mover la frontera entre
 * zonas es exactamente el tipo de cambio que hace imposible saber que etapa
 * rompio que cosa.
 */

#define UI_TOP_H        64
#define UI_STATUS_Y     64
#define UI_STATUS_H     40

/* Que esta ajustando el mando ahora mismo. El firmware actual deja el mando
 * apuntando a "lo ultimo que abriste en un DETAIL view" sin nada en pantalla
 * que lo diga; esto es lo que hace falta para poder mostrarlo. */
typedef enum {
    UI_KNOB_TUNE = 0,
    UI_KNOB_VOLUME,
    UI_KNOB_BACKLIGHT,
    UI_KNOB_SCALE_LO,
    UI_KNOB_SCALE_HI,
    UI_KNOB_SQUELCH,
    UI_KNOB_SMOOTH,
    UI_KNOB_PGA,
    UI_KNOB_NR,
    UI_KNOB_RTTY_SHIFT,
    UI_KNOB_CW_TONE
} ui_knob_t;

typedef struct {
    /* --- cabecera --- */
    const char *freq;        /* ya formateada, p.ej. "7.200,00" */
    int8_t      freq_digit;  /* indice del caracter que mueve el mando, -1 = ninguno */
    const char *mode;        /* "AM", "USB", "WFM"... */
    const char *clock;       /* "HH:MM" */
    uint8_t     batt_pct;    /* 0..100 */
    const char *batt_volts;  /* "3,94 V", o 0 para omitirlo */
    const char *sam_ppm;     /* error de PLL en SAM, "+1,4 PPM"; 0 = no mostrar */

    /* --- barra de estado --- */
    uint8_t     s_units;     /* 0..9 = S1..S9; >9 = por encima de S9 */
    uint8_t     over_db;     /* dB por encima de S9, solo si s_units > 9 */
    int16_t     dbm;         /* lectura calibrada; 1 en dbm_valid si vale algo */
    uint8_t     dbm_valid;   /* 0 si el AGC esta activo y el numero no significa nada */
    ui_knob_t   knob;
    const char *knob_value;  /* "1 kHz", "18", "24,0 dB"... */
    const char *agc;         /* "OFF" / "SLW" / "MED" / "FST" */
    /* ETAPA 4: el ancho del filtro de audio, ya formateado ("4,0 k"), o 0 en
     * los modos donde no lo elige el usuario (NFM/WFM).
     *
     * FALTABA. Al ocultar el boton viejo del BW en la etapa 3a este dato
     * desaparecio de la pantalla y no me di cuenta; peor aun, en la 3b quite
     * la etiqueta de ancho de la regla del espectro razonando que "ya esta en
     * el chip BW de la barra de estado", que era sencillamente falso. */
    const char *bw;
    uint8_t     nr_on;
    uint8_t     ovr;         /* entrada saturada */
    uint8_t     spk_muted;   /* altavoz silenciado */
} ui_top_state_t;

/* Repinta cabecera y barra de estado enteras. */
void ui_top_draw(const ui_top_state_t *st);

/* Solo la barra de estado, para los refrescos periodicos del S-meter. */
void ui_top_draw_status(const ui_top_state_t *st);

/*
 * --- toques en la barra de estado ---------------------------------------
 *
 * Los chips no estan en sitios fijos: se colocan de derecha a izquierda por
 * prioridad y se saltan si no caben (ver el comentario del bucle en
 * ui_top.c), asi que su posicion depende de lo que haya encendido en ese
 * momento. Eso hace imposible declarar sus zonas de toque por adelantado.
 *
 * Asi que la zona de toque la produce el PROPIO dibujo: ui_top.c apunta el
 * rectangulo de cada chip segun lo va pintando, y ui_top_hit() consulta esa
 * lista. Lo que se toca es, por construccion, lo que se ve - no una segunda
 * cuenta que puede desincronizarse de la primera.
 */
typedef enum {
    UI_TOP_HIT_NONE = 0,
    UI_TOP_HIT_AGC,
    UI_TOP_HIT_BW,
    UI_TOP_HIT_NR,
    UI_TOP_HIT_SPK,
    UI_TOP_HIT_OVR
} ui_top_hit_t;

/* Que chip hay bajo (x, y), o UI_TOP_HIT_NONE. */
ui_top_hit_t ui_top_hit(uint16_t x, uint16_t y);

/*
 * 1 si (x,y) cae sobre la LECTURA DE FRECUENCIA de la cabecera - lo que abre
 * la pantalla de meter la frecuencia a mano. La zona acaba donde acaba el
 * "kHz", antes del chip de modo, y la anchura es variable porque la
 * frecuencia lo es. Ver s_freq_x2 en ui_top.c.
 */
uint8_t ui_top_freq_hit(uint16_t x, uint16_t y);

/* 1 si (x,y) cae sobre el RELOJ de la cabecera - lo que abre el teclado de
 * poner la hora. Va alineado a la derecha, asi que su borde izquierdo
 * depende del texto. Ver s_clock_x1/x2 en ui_top.c. */
uint8_t ui_top_clock_hit(uint16_t x, uint16_t y);

#endif /* UI_TOP_H_INCLUDED */
