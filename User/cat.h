#ifndef CAT_H
#define CAT_H

#include <stdint.h>

/*
 * CAT: QUE UN PROGRAMA DEL ORDENADOR MANEJE LA RADIO.
 *
 * *** De lo que publica la radio comercial de referencia sobre su modo
 * "audio digital + CAT": se ve a la vez como tarjeta de sonido y como un
 * transceptor Kenwood TS-2000, y asi funciona con WSJT-X, JTDX y hamlib sin
 * que nadie escriba un driver. ***
 *
 * POR QUE UN TS-2000 Y NO UN PROTOCOLO NUESTRO
 * --------------------------------------------
 * Porque un protocolo nuestro no lo habla nadie. El de Kenwood lo hablan
 * hamlib -y por debajo de hamlib, WSJT-X, JTDX, fldigi, gpredict, CQRLOG y
 * media aficion-, y ademas es ASCII plano:
 *
 *     FA00007110000;     pon el VFO A en 7.110,000 kHz
 *     FA;                ¿en que frecuencia estas?
 *     MD1;               pon LSB
 *     IF;                dame el bloque de estado entero
 *
 * Ordenes en texto, terminadas en punto y coma, sin binario, sin sumas de
 * comprobacion y sin estado entre ordenes. Eso lo hace facil de implementar
 * y, sobre todo, facil de PROBAR: el banco le mete el rigctl de hamlib de
 * verdad por delante en vez de mi lectura del manual.
 *
 * LO QUE NO HACE, Y SE DICE
 * -------------------------
 * Esto es un RECEPTOR. Todo lo que mande transmitir -TX, potencia, VOX,
 * manipulador- se contesta con "?;", que es como un Kenwood dice "esa no la
 * conozco". No se contesta con silencio: un programa al otro lado espera
 * respuesta y el silencio le cuesta un tiempo de espera de varios segundos
 * por orden, asi que callarse es peor que decir que no.
 *
 * SIN DEPENDENCIAS DEL RESTO DE LA RADIO
 * --------------------------------------
 * Aqui dentro no se incluye nada del firmware. Lo que hace falta de la
 * radio -leer y poner frecuencia, modo, volumen, el S y el ancho- entra por
 * una tabla de punteros, cat_radio_t, que la rellena main.c. Asi este
 * fichero compila tal cual en el ordenador y el banco mide el codigo DE
 * VERDAD, no una copia suya.
 */

/* Los modos, en numeracion de Kenwood, que es la que viaja por el cable. */
enum {
    CAT_MODO_LSB = 1,
    CAT_MODO_USB = 2,
    CAT_MODO_CW  = 3,
    CAT_MODO_FM  = 4,
    CAT_MODO_AM  = 5,
    CAT_MODO_FSK = 6,   /* RTTY */
    CAT_MODO_CWR = 7,
    CAT_MODO_FSKR = 9
};

/*
 * ======================================================================
 * LA VUELTA DEL BUCLE POR EL PUERTO, PARA NO MEDIR CON FOTOS - 02/11/2026
 * ======================================================================
 *
 * Seis versiones persiguiendo por que la cascada se frena al girar el
 * mando, y todas las medidas de la radio han llegado leyendo numeros de
 * una foto de la pantalla de Informacion. Eso tiene dos fallos: la fila
 * enseña una media o un record -nunca la vuelta a vuelta- y no se puede
 * mirar mientras se gira, porque en esa pantalla el mando mueve el cursor.
 *
 * Con esto la radio lo cuenta sola por el puerto CAT, que ya existe y ya
 * esta conectado: una orden "DG;" devuelve, de todas las vueltas QUE HAN
 * DIBUJADO desde la consulta anterior, la media de cada uno de los quince
 * tramos del bucle, cuantas vueltas fueron, cuanto se movio el dial
 * -mando o dedo- mientras tanto y la carga de audio. Preguntando dos veces por segundo
 * sale la pelicula entera, girando y sin girar, y no se pierde ni una
 * vuelta: son SUMAS, no muestras.
 *
 * No es una orden de Kenwood. Un Kenwood de verdad contestaria "?;" y
 * hamlib no la manda nunca, asi que no estorba a nadie.
 */
#define CAT_DIAG_TRAMOS  18U

typedef struct {
    uint16_t vueltas;                    /* vueltas con dibujo desde la ultima consulta */
    /*
     * CUANTO SE MOVIO EL DIAL en ese mismo rato, en pasos. Es lo que
     * separa "moviendo" de "quieto" en tools/cat_vuelta.ps1.
     *
     * CUENTA LAS DOS MANERAS DE MOVERLO: los clics del mando Y los pasos
     * del arrastre con el dedo -03/11/2026-. Antes solo contaba el mando,
     * y entonces un arrastre con el dedo salia en la casilla de "quieto",
     * que es justo la casilla equivocada cuando lo que se persigue es por
     * que la radio se ralentiza con el dedo.
     */
    uint16_t detentes;                   /* pasos de dial: mando + dedo */
    /*
     * El reductor de ruido, encendido o no, porque es la otra mitad de la
     * pregunta: la carga de audio es el divisor del cuadro, y el NR son
     * ocho puntos de esa carga. Sin este bit no se pueden comparar dos
     * tandas -el que mira la tabla no sabe con que estaba puesto-, y con
     * el salen cuatro casillas: quieto/girando por NR si/no.
     */
    uint8_t  nr;                         /* 1 = reductor de ruido encendido */
    uint8_t  carga;                      /* carga de audio, % */
    uint32_t vuelta_us;                  /* media de la vuelta entera */
    uint32_t tramo_us[CAT_DIAG_TRAMOS];  /* media de cada tramo, en orden */
} cat_diag_t;

/*
 * Y el desglose de DENTRO del tramo del dibujo, que el reparto de "DG;"
 * no puede dar: alli `dib` es UN hueco, y la primera tanda con el dedo
 * dijo que es justo donde se va el tiempo. Ver "DD;" en cat.c.
 *
 * Son medias por CUADRO desde la consulta anterior, como las de "DG;".
 * `saltadas` y `bandas` no son tiempo: son el atajo del repintado parcial
 * -cuantas bandas se salto y cuantas habia-, y si saltadas se va a cero el
 * atajo esta muerto y eso solo ya explica un 77 % mas de traza.
 */
#define CAT_DIBUJO_HUECOS  19U

typedef struct {
    uint16_t cuadros;                    /* cuadros desde la ultima consulta */
    /* --- los cinco tiempos, media por cuadro, en us --- */
    uint32_t fft_us;                     /* la transformada */
    uint32_t chrome_us;                  /* la regla, el marco, las etiquetas */
    uint32_t spec_us;                    /* la traza */
    uint32_t push_us;                    /* empujar al panel */
    uint32_t wf_us;                      /* la cascada */
    /* --- el atajo del repintado parcial --- */
    uint32_t saltadas;                   /* bandas saltadas, media por cuadro */
    uint32_t bandas;                     /* bandas que habia */
    uint32_t invalida;                   /* veces que se tiro el atajo */
    /* --- cuantas veces se hizo cada cosa cara, desde la consulta --- */
    uint32_t lecturas;                   /* bajadas de verdad al panel */
    uint32_t frq_n;                      /* repintados del numero del dial */
    uint32_t rgl_n;                      /* repintados de la fila de etiquetas */
    uint32_t chr_n;                      /* repintados de la regla del marco */
    uint32_t lot_us;                     /* reprogramar la sintonia DESDE EL
                                          * ARRASTRE, media por cuadro. El
                                          * tramo `lot` de "DG;" solo cubre
                                          * el mando. */
    uint32_t lot_n;                      /* cuantas veces */
    uint32_t chapa_n;                    /* repintados de la chapa */
    uint32_t arrastre_n;                 /* pasos de arrastre con el dedo */
    uint32_t arr_us;                     /* lo que cuesta spec_drag_tune_apply()
                                          * entera, media por cuadro */
    uint32_t wf_lineas;                  /* lineas volcadas a la cascada */
    /* --- y en que estado estaba la radio, para no tener que preguntarlo --- */
    uint32_t estado;                     /* bit0 NCO, bit1 menu, bit2 dedo,
                                          * bit3 panel entero, bits 4-7 zoom,
                                          * bit8 rotulos de emisoras sobre el
                                          * espectro, bit9 reductor de ruido */
} cat_dibujo_t;

/*
 * ==========================================================================
 * "DA": EL CAMINO DEL AUDIO, QUE ES DONDE SUENA EL SISEO - 06/11/2026.
 * ==========================================================================
 *
 * POR QUE EXISTE. El usuario oye un siseo en FM que DESAPARECE al entrar en
 * el menu principal. Eso ya dice mucho: lo causa algo que corre en la
 * pantalla principal y no en el menu, o sea la FFT, la cascada, la traza o
 * el empuje de pixeles al panel. El tactil NO entra en la lista: se mira en
 * las dos pantallas (ver la guardia de demo_touch_poll() en main.c, que
 * dice con todas las letras que ahi no se mira s_menu_open).
 *
 * Y LOS CONTADORES QUE LO MEDIRIAN YA EXISTEN TODOS. Lo que no habia era
 * forma de leerlos sin tener la radio delante y un dedo en la pantalla de
 * Informacion. Eso es esto.
 *
 * SON DOS AVERIAS DISTINTAS Y HAY QUE SABER CUAL:
 *
 *   - LA ENTRADA: la interrupcion del audio llega tan tarde que las dos
 *     banderas del DMA estan puestas y se pierde media captura (`tarde`), o
 *     el periferico se queda sin sitio (`over`). Eso son muestras que NO
 *     ESTAN, y suenan.
 *   - LA SALIDA: el I2S se queda sin palabra que mandar (`under`, TXURERR).
 *     El comentario de gd32_i2s.h dice exactamente de quien se sospecha:
 *     "la que sube si algo le roba acceso a la SRAM al DMA del audio - el
 *     blit de la pantalla, el IPA". Que es, palabra por palabra, la
 *     hipotesis que el sintoma propone.
 *
 * Y UNA TERCERA QUE NO ES AVERIA SINO MARGEN: `hmin` y `hmax` dicen donde
 * cae la escritura DENTRO de la mitad del buffer, en palabras de `pal`. Los
 * dos bajos = la interrupcion va sobrada; los dos altos = esta llegando al
 * final de su presupuesto. Un margen que se come la pantalla principal y no
 * el menu es la misma respuesta aunque `under` siga a cero, y llega ANTES
 * de que se rompa nada.
 *
 * LOS CUATRO CONTADORES VAN EN DIFERENCIA desde la consulta anterior, como
 * los de "DD", porque lo que se compara son dos tandas de un minuto. `hmin`
 * y `hmax` no: son extremos y van en crudo, asi que para que signifiquen
 * algo hay que ponerlos a cero antes de cada tanda tocando el renglon de
 * audio de la ventana de Informacion.
 *
 * Y VA `estado` TAMBIEN AQUI, aunque "DD" ya lo lleve. Una tanda de "DA"
 * sola tiene que poder leerse sin la de al lado: el bit del menu es
 * justamente la variable del experimento, y una medida en la que no consta
 * el estado de la radio no puede refutar nada. Esa leccion costo tres
 * tandas en la revision del arrastre.
 */
#define CAT_AUDIO_HUECOS  14U

typedef struct {
    uint32_t tarde;      /* la ISR de entrada llego tarde y se perdio media
                          * captura. DIFERENCIA desde la consulta anterior */
    uint32_t over;       /* RXORERR de entrada. Diferencia */
    uint32_t under;      /* TXURERR de salida: el DMA se quedo sin palabra.
                          * Diferencia. EL SOSPECHOSO */
    uint32_t repes;      /* mitades repetidas del ping-pong. Diferencia */
    uint32_t hmin;       /* donde cae la escritura en la mitad, lo mas bajo
                          * visto, en palabras. EN CRUDO, no diferencia */
    uint32_t hmax;       /* lo mas alto visto. En crudo */
    uint32_t pal;        /* palabras que tiene una mitad (1024 en WFM) */
    uint32_t carga;      /* carga de audio, %. EN FM ANCHA sale de
                          * demod_wfm_get_last_cycles() y no del de AM: el
                          * camino de WFM es otro y el de AM se queda a
                          * cero, que es lo que salio en las dos primeras
                          * tandas - un hueco que miente por omision -. */
    uint32_t estado;     /* los mismos bits que cat_dibujo_t.estado */
    /*
     * Y LOS DOS MANDOS QUE SE ESTAN TOCANDO - 06/11/2026.
     *
     * El siseo de la FM resulto no ser audio perdido -los cuatro
     * contadores de arriba salieron CERO en las dos pantallas, y el
     * margen plano al 45% en las dos- sino algo que escala con el
     * trafico al panel: la velocidad de la cascada lo cambia y el brillo
     * no, probado por el usuario.
     *
     * Y los dos mandos que mueven ese trafico YA EXISTEN y ya se tocan a
     * dedo: la velocidad de la cascada desde Ajustes y el tiempo de dato
     * del bus EXMC desde la ventana de Informacion -ver
     * rm68120_bus_dato_pon()-. Lo que faltaba no era el mando: era que la
     * tanda APUNTARA en que posicion estaba cada uno. Sin eso el CSV no se
     * puede interpretar despues, y con treinta y cinco filas y dos mandos
     * movidos a mano eso es una tanda perdida.
     *
     * Es la misma leccion que el bit del NCO y que el del menu, por
     * tercera vez: una medida en la que no consta el estado de la radio no
     * puede refutar nada.
     */
    uint32_t bus;        /* ciclos de setup de DATO del EXMC (campo DSET) */
    uint32_t wfvel;      /* indice de velocidad de la cascada, 0 = la mas
                          * rapida. Ver k_wf[] en main.c */
    /*
     * Y LOS TRES QUE FALTABAN - 06/11/2026, repasando la tanda del bus.
     *
     * Son los otros tres interruptores de la ventana de Informacion que
     * cambian COMO llega la cascada al panel, y por tanto cuanto trabajo
     * hace la radio por cada volcado:
     *
     *   pend  la pendiente de los pines del bus (0 = 2 MHz, 2 = la de
     *         arranque, 50 MHz). Es el mando del di/dt, y es el que yo
     *         confundi con el tiempo de dato: ese cambia CUANDO ocurren
     *         los flancos, este cuanto empinados son.
     *   ipa   si el volcado va por el acelerador o por el camino de CPU.
     *   dma   si el panel se llena por DMA.
     *
     * Van los tres porque la leccion ya ha costado tres tandas: el bit del
     * NCO, el del menu y el del bus. No se vuelve a tomar una tanda en la
     * que no conste el estado de lo que se esta tocando.
     */
    uint32_t pend;       /* 0..3, nivel de pendiente de los pines del bus */
    uint32_t ipa;        /* 1 = el volcado va por el acelerador */
    uint32_t dma;        /* 1 = el panel se llena por DMA */
} cat_audio_t;

typedef struct {
    uint32_t (*freq_lee)(void);          /* Hz */
    void     (*freq_pon)(uint32_t hz);
    uint8_t  (*modo_lee)(void);          /* CAT_MODO_* */
    void     (*modo_pon)(uint8_t modo);
    uint8_t  (*smeter_lee)(void);        /* 0..30, como un Kenwood */
    uint8_t  (*vol_lee)(void);           /* 0..255 */
    void     (*vol_pon)(uint8_t v);
    uint16_t (*ancho_lee)(void);         /* Hz */
    void     (*diag_lee)(cat_diag_t *d); /* "DG": la vuelta del bucle */
    void     (*dibujo_lee)(cat_dibujo_t *d); /* "DD": dentro del dibujo */
    void     (*audio_lee)(cat_audio_t *a);   /* "DA": el camino del audio */
} cat_radio_t;

/*
 * Lo mas largo que puede contestar esto ya no es el bloque IF -37- sino
 * "DG": 2 + 4 + 4 + 1 + 3 + 5 + 18 x 5 + ';' = 110. El buffer vive en la
 * pila de usb_cdc_poll(), y el margen de pila medido son 2.048 bytes.
 * "DD" son 2 + 4 + 19 x 5 + ';' = 102, o sea que no mueve el techo.
 * "DA" son 2 + 14 x 5 + ';' = 73, menos todavia.
 */
#define CAT_SALIDA_MAX  112U

/*
 * Y ESTO NO ES DECORACION - 08/11/2026.
 *
 * Esas tres cuentas de arriba eran lo UNICO que separaba este buffer de
 * un desbordamiento, y una de las tres estaba mal: decia "11 x 5 = 58"
 * cuando "DA" lleva catorce huecos desde que se escribio, o sea 73. No
 * rompia nada -73 cabe de sobra en 112- pero el que fuera a anadir
 * huecos nuevos haria la cuenta desde ese 58 y se creeria con 54 bytes
 * de margen cuando tiene 39.
 *
 * Un comentario no para un make. Estas tres lineas si: si alguien anade
 * un tramo a "DG", un hueco a "DD" o un contador a "DA" y la respuesta
 * deja de caber, la compilacion se para AQUI, con el nombre de la que se
 * ha pasado, y no en la pila de usb_cdc_poll() con la radio delante.
 *
 * Las cuentas son las de los emisores de cat.c, hueco a hueco:
 *   DG  "DG" + vueltas(4) + detentes(4) + nr(1) + carga(3) + vuelta(5)
 *       + CAT_DIAG_TRAMOS x 5 + ';'
 *   DD  "DD" + cuadros(4) + CAT_DIBUJO_HUECOS x 5 + ';'
 *   DA  "DA" + CAT_AUDIO_HUECOS x 5 + ';'
 */
_Static_assert((2U + 4U + 4U + 1U + 3U + 5U + (CAT_DIAG_TRAMOS * 5U) + 1U)
               <= CAT_SALIDA_MAX,
               "la respuesta de DG ya no cabe en CAT_SALIDA_MAX: o sube el "
               "tope o quita un tramo");
_Static_assert((2U + 4U + (CAT_DIBUJO_HUECOS * 5U) + 1U) <= CAT_SALIDA_MAX,
               "la respuesta de DD ya no cabe en CAT_SALIDA_MAX");
_Static_assert((2U + (CAT_AUDIO_HUECOS * 5U) + 1U) <= CAT_SALIDA_MAX,
               "la respuesta de DA ya no cabe en CAT_SALIDA_MAX");

void cat_init(const cat_radio_t *radio);

/*
 * Mete un byte de los que llegan del ordenador. Mientras no llegue el punto
 * y coma devuelve 0 y no escribe nada. Con el punto y coma procesa la orden
 * y devuelve cuantos bytes hay que mandar de vuelta, escritos en `salida`
 * (que tiene que medir CAT_SALIDA_MAX).
 *
 * No hay respuesta parcial ni estado a medias: o hay orden entera o no hay
 * nada. Si llega mas basura de la que cabe en una orden, se tira y se
 * empieza de nuevo en el siguiente punto y coma.
 */
uint8_t cat_byte(char c, char *salida);

/* Para las pruebas y para el reinicio del puerto. */
void cat_reset(void);

#endif /* CAT_H */
