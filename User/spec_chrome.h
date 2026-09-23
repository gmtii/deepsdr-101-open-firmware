#ifndef SPEC_CHROME_H_INCLUDED
#define SPEC_CHROME_H_INCLUDED

#include <stdint.h>

/*
 * ETAPA 3b: los ejes del espectro y la leyenda del waterfall.
 *
 * QUE ES "CHROME" Y POR QUE VA APARTE
 * -----------------------------------
 * La traza del espectro y el waterfall se repintan 30 veces por segundo y
 * los dibuja spectrum.c pixel a pixel, que esta afinado y no se toca. Lo
 * que hay ALREDEDOR -los numeros de dB, la regla de frecuencia, la barra
 * de color- solo cambia cuando cambia la escala, la sintonia, el zoom o
 * la paleta: unas pocas veces por minuto, no 30 veces por segundo.
 *
 * Asi que va en su propio modulo, con su propia zona de pantalla, y se
 * redibuja solo cuando algo suyo cambia. Ni un acceso al bus por frame.
 *
 * LA CANALETA
 * -----------
 * Un eje necesita sitio propio. Antes la traza ocupaba de x=2 a x=797 y
 * los numeros de dB no tenian donde ir; ahora los 40 px de la izquierda
 * son canaleta y la traza va de x=40 a x=795 (756 px). Se pierde un 4,5%
 * de ancho de traza y se gana poder leer el nivel, que es lo que hace que
 * un espectro sirva para medir y no solo para mirar.
 *
 * La canaleta la comparten espectro y waterfall: las dos vistas siguen
 * empezando en la MISMA columna, que es lo que permite seguir una senal
 * de una a otra con la vista.
 *
 * 760 es divisible por 4, que es un requisito y no una casualidad: el
 * punto de demodulacion en low-IF cae exactamente en +Fs/4, es decir en
 * SPC_TRACE_W/4 = 189 px justos, sin redondeo.
 */

/* --- geometria, la misma que usa main.c ------------------------------- */
#define SPC_Y           104
#define SPC_H           240
#define SPC_RULER_H      26                       /* regla de frecuencia */
#define SPC_GUT_W        40                       /* canaleta izquierda   */
#define SPC_TRACE_X      SPC_GUT_W                /* = 40                 */
#define SPC_TRACE_Y      (SPC_Y + 4)              /* = 108                */
#define SPC_TRACE_W      756
#define SPC_TRACE_H      (SPC_H - 4 - SPC_RULER_H - 2)  /* = 208 */
#define SPC_RULER_Y      (SPC_Y + SPC_H - SPC_RULER_H)  /* = 318 */
#define SPC_WF_PANEL_Y   346
#define SPC_WF_Y         348
#define SPC_WF_ROWS      72

/* Que sideband ocupa la banda de paso. */
typedef enum {
    SPC_BAND_NONE = 0,   /* WFM/NFM: el ancho no lo elige el usuario */
    SPC_BAND_BOTH,       /* AM: doble banda lateral                  */
    SPC_BAND_UPPER,      /* USB                                      */
    SPC_BAND_LOWER       /* LSB                                      */
} spc_band_t;

typedef struct {
    /* --- eje vertical --- */
    float       db_min;
    float       db_max;

    /* --- eje horizontal --- */
    uint32_t    center_hz;      /* frecuencia en el centro del PANEL */
    uint32_t    span_hz;        /* ancho total visible               */
    int16_t     demod_px;       /* desplazamiento del punto de demodulacion
                                 * respecto al centro del panel, en px */

    /* --- banda de paso --- */
    spc_band_t  band;
    uint32_t    band_hz;        /* ancho del filtro de audio */

    /* --- leyenda del waterfall --- */
    const uint16_t *cmap;       /* LUT de 256 entradas RGB565 */
} spec_chrome_t;

/* Repinta canaleta + regla + leyenda. No toca la zona de la traza ni la
 * del waterfall. */
void spec_chrome_draw(const spec_chrome_t *st);

/* Solo la regla de frecuencia, para cuando lo unico que cambia es la
 * sintonia (que es lo que mas cambia). */
void spec_chrome_draw_ruler(const spec_chrome_t *st);

/* Solo el eje de dB y la leyenda de color, que dependen de otra cosa
 * distinta (los limites de escala y la paleta). */
void spec_chrome_draw_axis(const spec_chrome_t *st);

/* Cierto si `a` y `b` producirian una REGLA distinta / un EJE distinto.
 *
 * Existe para que el llamante pueda repintar por comparacion en vez de
 * tener que acordarse de llamar desde cada sitio que cambia algo. Olvidar
 * uno de esos sitios no da error de compilacion: da un eje que miente, que
 * es el peor fallo posible en algo que esta para medir. */
uint8_t spec_chrome_ruler_changed(const spec_chrome_t *a, const spec_chrome_t *b);
uint8_t spec_chrome_axis_changed(const spec_chrome_t *a, const spec_chrome_t *b);

#endif /* SPEC_CHROME_H_INCLUDED */
