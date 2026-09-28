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
    /*
     * La banda en la que cae la frecuencia ("40 m", "FM", "OM"...), o 0 si no
     * esta en ninguna de las de la lista - 23/09/2026, por el dueno del
     * proyecto: "me gustaria poner ahi la banda que estas escuchando, y que
     * lo pulses y se te abra la pantalla de bandas".
     *
     * Sube aqui desde la barra de abajo, y de paso deja de ser un boton que
     * solo abre una pantalla para ser ademas un dato: antes, en que banda
     * estabas no se decia en ningun sitio.
     */
    const char *band;
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
    /* Nombre del mando, cuando no vale ninguno de los de ui_knob_t: es el
     * nombre del ajuste que se le ha enganchado con el boton Func
     * (23/09/2026). 0 = usar el nombre que toque por `knob`. Se anade como
     * excepcion y no como otro valor del enum porque los ajustes enganchables
     * son treinta y pico y ponerlos todos ahi seria copiar la lista de
     * ajustes de main.c en este fichero. */
    const char *knob_name;
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
    /*
     * MARQUESINA DE RDS - 24/09/2026, por el dueno: "creo que el texto del
     * rds estaria mejor en otro sitio, como entre el boton de bandas y la
     * hora, en modo marquesina".
     *
     * Y es mejor sitio, si. El primer intento le dio al RDS dos renglones
     * donde va la cascada; funcionaba, pero se comia media pantalla por un
     * dato que se mira de reojo. Aqui el nombre de la emisora y el
     * radiotexto pasan por un hueco que estaba vacio -entre la pastilla de
     * banda y el reloj- y la cascada vuelve entera.
     *
     * El texto va ya montado desde main.c; aqui solo se pinta, recortado al
     * hueco, desplazado `rds_off` pixeles a la izquierda. Quien lo hace
     * correr es quien llama, que es el que sabe a que ritmo se repinta.
     */
    const char *rds;        /* 0 = no hay nada que enseñar */
    int16_t     rds_off;    /* cuanto se ha corrido, en pixeles */
    /* El renglon FIJO de encima: la cadena identificada por su codigo de
     * programa, o el propio codigo si no esta en la lista. Va quieto y en
     * tinta viva porque es la respuesta a "que estoy escuchando", que no
     * deberia haber que esperar a que pase por delante. */
    const char *rds_nombre;
    /*
     * Y SU PROPIO DESPLAZAMIENTO - 28/09/2026.
     *
     * *** El dueño, con una foto de "Canarias HFDL (I" cortado a la mitad:
     * "o haces mas grande el campo de texto o lo haces marquesina". ***
     *
     * Grande no se puede: el hueco es lo que queda entre la pastilla de
     * banda y el reloj, y los dos tienen que estar. Asi que marquesina -
     * pero solo cuando hace falta-.
     *
     * Este renglon nacio para el RDS, donde el nombre son ocho caracteres
     * y siempre cabe; por eso iba quieto, y quieto esta bien: es la
     * respuesta a "que estoy escuchando" y no deberia haber que esperar a
     * que pase por delante. Con las emisoras de onda corta los nombres son
     * "Canarias HFDL (Islas Canarias)" y no caben.
     *
     * A cero se pinta exactamente como antes. Quien lo mueve es quien
     * llama, y solo si ha medido que no cabe.
     */
    int16_t     rds_nom_off;

    uint8_t     nr_on;
    /* NCO de sintonia puesto (23/09/2026, por el dueno: "el nco deberia de
     * tener su indicador igual que tiene el nb, justo al lado"). Va aqui y
     * no en la cabecera porque es un modo que se enciende y se apaga, como
     * NR, y porque se mira de reojo: con el puesto, el panorama no se mueve
     * al sintonizar, y eso hay que poder saberlo sin abrir los ajustes. */
    uint8_t     nco_on;
    uint8_t     ovr;         /* entrada saturada */
    uint8_t     spk_muted;   /* altavoz silenciado */
} ui_top_state_t;

/* --- la franja de la marquesina, para poder repintar SOLO eso --- */
/* La franja lleva DOS renglones desde el 24/09/2026: el nombre fijo arriba y
 * la marquesina debajo. Antes era uno solo y la marquesina iba en el medio. */
#define UI_TOP_RDS_Y      10
#define UI_TOP_RDS_H      44
#define UI_TOP_RDS_NOM_Y  (UI_TOP_RDS_Y + 2)    /* el renglon fijo */
#define UI_TOP_RDS_MAR_Y  (UI_TOP_RDS_Y + 24)   /* la marquesina */
#define UI_TOP_RDS_X2  632   /* hasta aqui: despues viene el reloj */

/* Solo la marquesina. Se llama muchas veces por segundo, asi que tiene su
 * propia franja: repintar la cabecera entera treinta veces por segundo para
 * mover un texto cuatro pixeles seria 800x64 pixeles por el bus cada vez. */
void ui_top_draw_rds(const ui_top_state_t *st);

/* El ancho del hueco disponible, 0 si no cabe nada. Lo necesita main.c para
 * saber cuando ha de volver a empezar el texto. Lo decide ESTE fichero, que
 * es quien dibuja: la pastilla de banda cambia de ancho con el nombre de la
 * banda, y suponerlo desde fuera seria copiar aqui esa geometria. */
int16_t ui_top_rds_ancho(void);

/* Lo que mide un texto con la fuente de la marquesina. Lo pregunta main.c
 * para saber cuando ha dado la vuelta entera; la FUENTE la elige este
 * fichero, que es quien dibuja. */
int16_t ui_top_rds_texto_w(const char *t);
/* Lo mismo para el renglon del nombre, que va en negrita: medirlo con la
 * fuente de la marquesina daria de menos y el texto volveria antes de
 * tiempo. */
int16_t ui_top_rds_nombre_w(const char *t);

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
    UI_TOP_HIT_NCO,
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

/* 1 si (x,y) cae sobre la pastilla de BANDA - lo que abre la lista de bandas.
 * Va detras del chip de modo, asi que su sitio depende de lo ancha que sea la
 * frecuencia y de lo largo que sea el nombre del modo: se anota al dibujar,
 * igual que las otras dos. */
uint8_t ui_top_band_hit(uint16_t x, uint16_t y);

/* Para los bancos del simulador: los anchos que decide ui_top.c, medidos por
 * el mismo. Ver el comentario de su definicion. */
int16_t ui_top_chip_w_dbg(const char *label, const char *value);
int16_t ui_top_txtw_dbg(const char *t, int f);

#endif /* UI_TOP_H_INCLUDED */
