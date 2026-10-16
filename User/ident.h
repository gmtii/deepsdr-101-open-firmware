#ifndef IDENT_H
#define IDENT_H

#include <stdint.h>
#include "stanag_det.h"

/*
 * IDENT: QUE DEMONIOS ES ESTA SEÑAL - 03/10/2026.
 *
 * Pedido por el usuario.
 *
 * Pedido de madrugada, el 03/10 a la una: un modo Ident hecho con todo lo
 * que el firmware ya sabe medir.
 *
 * Esto es ese boton. El modo STANAG se pone cuando YA SABES lo que oyes;
 * este se pone cuando no lo sabes.
 *
 * DE DONDE SALE "TODO LO QUE YA SABES"
 * ------------------------------------
 * De la noche del 2 al 3 de octubre, buscando un NAVTEX a mano. Delante de
 * cada grabacion hubo que contestar las mismas cuatro preguntas, y de las
 * cuatro respuestas salio siempre el nombre de la señal:
 *
 *   1. CUANTO OCUPA. Una portadora ocupa 18 Hz, un SITOR 205, un STANAG
 *      4285 2531. Es la medida que mas separa y la mas barata.
 *   2. SI ES UNA PORTADORA SOLA. El 57% de la potencia metida en +-8 Hz
 *      del pico decia "aqui no hay modem, hay un heterodino" cuando el
 *      el usuario llevaba media hora intentando decodificarlo.
 *   3. SI SON DOS TONOS Y CUANTO SE SEPARAN. 170 Hz salio SITOR; 850 Hz
 *      habria salido STANAG 4481 FSK.
 *   4. A CUANTOS BAUDIOS. 100,00 clavados. Con eso y los 170 Hz ya no hay
 *      mas que una familia posible.
 *
 * Mas la que ya estaba: CADA CUANTO SE REPITE LA TRAMA, que es la que
 * identifica los STANAG de fase y que es para lo que nacio stanag_det.c.
 *
 * Las cinco las mide stanag_det.c, que es el motor. Aqui solo esta la
 * TABLA que convierte cinco numeros en un nombre.
 *
 * LO QUE ESTE MODO NO VA A HACER NUNCA
 * ------------------------------------
 * Decir un nombre cuando no lo sabe. El catalogo del Signal Identification
 * Wiki tiene 268 señales de onda corta y con cinco medidas NO se separan
 * 268 cosas: hay familias enteras que comparten las cinco. Un SITOR-A, un
 * SITOR-B, un NAVTEX y un DSC son 100 baudios y 170 Hz LOS CUATRO.
 *
 * Asi que cuando cuadran varias filas se dicen TODAS. "SITOR-B / NAVTEX"
 * es una respuesta honrada; elegir una de las dos a cara o cruz y
 * enseñarla con la misma cara de seguridad, no. Y cuando no cuadra
 * ninguna, se dicen los numeros medidos y se dice que no se sabe: con eso
 * el usuario puede buscarlo en el catalogo, que es mas de lo que tenia.
 *
 * COMO SE METE UNA SEÑAL NUEVA
 * ----------------------------
 * Pedido por el usuario.
 *
 * Tres pasos, y ninguno toca logica:
 *   1. el audio a sim/muestras/
 *   2. una fila en k_firmas[] de ident.c
 *   3. una fila en la tabla del banco, sim/identtest.c, diciendo que tiene
 *      que salir de ese audio
 *
 * Y LA REGLA QUE NO SE SALTA: una fila solo entra con su numero
 * comprobado, o contra una grabacion o contra una norma que se pueda
 * citar. El campo `probada` dice cual es cual, y la pantalla lo marca. Una
 * fila puesta de memoria es PEOR que no tenerla, porque sale con la misma
 * cara de seguridad que las que estan medidas.
 */

#define IDENT_LINEAS   9U
/*
 * EL TOPE DE LOS RENGLONES QUE SE ARMAN - 06/10/2026.
 *
 * Esto NO es el ancho de lo que se puede enseñar. Desde que las lineas del
 * panel son punteros (ver s_lin en ident.c), los nombres y las notas de
 * k_firmas[] se pintan ENTEROS midan lo que midan, porque no se copian.
 *
 * Este tope es solo para las lineas que hay que ARMAR: las de numeros, y la
 * del nombre con su "(?)" pegado. Esas si necesitan un buffer, y 48 les
 * sobra: la mas larga que sale de ponf() no pasa de 40.
 *
 * COMO SE LLEGO AQUI. Valia 48 y las lineas se COPIABAN, asi que la nota de
 * 73 caracteres del STANAG 4285 salia cortada: "...a 600 bps y entrela". El
 * primer arreglo fue subirlo a 80, y el enlazador contesto "region RAM
 * overflowed by 192 bytes" - lo cual vino bien, porque el arreglo era otro:
 * no ampliar nueve copias, sino dejar de copiar.
 *
 * sim/identancho.c mide las 27 firmas, en los dos idiomas, contra lo unico
 * que las puede cortar ahora: el ancho del panel en pixeles.
 */
#define IDENT_LARGO   48U

/* Por que criterio se reconoce una firma. */
enum {
    IDENT_POR_TONOS = 0,   /* dos tonos: separacion y baudios */
    IDENT_POR_TRAMA,       /* el periodo de la autocorrelacion */
    IDENT_POR_SEP,         /* dos tonos, pero solo por lo que se separan */
    IDENT_POR_CADENCIA,    /* cada cuanto se enciende y se apaga, en segundos */
    IDENT_POR_SIMBOLO,     /* la velocidad de simbolo de lo que no son dos tonos */
    IDENT_POR_ANCHO,       /* solo el ancho de banda: es el mas flojo */
    IDENT_POR_PICO         /* casi toda la potencia en una raya */
};

/*
 * POR QUE HAY UN "SOLO POR LA SEPARACION" - 05/10/2026.
 *
 * El medidor de velocidad busca rachas de entre 5 y 253 muestras a 3 kHz,
 * o sea que solo sabe medir de 12 a 600 baudios. El APRS de nuestro ax25.c
 * va a 1200 con tonos de 1200 y 2200 Hz: la separacion SE VE perfectamente
 * y la velocidad NO, porque un simbolo son dos muestras y media.
 *
 * Pedirle los baudios a esa firma seria pedirle algo que el instrumento no
 * da, y la fila no cuadraria nunca. Asi que esta puerta mira los tonos y se
 * calla sobre la velocidad.
 */

/*
 * LA CADENCIA: CADA CUANTO SE ENCIENDE Y SE APAGA - 05/10/2026.
 *
 * Pedido por el usuario.
 *
 * Y no podia. La firma del FT8 que habia miraba el ancho de banda: 50 Hz,
 * que son los ocho tonos a 6,25 Hz. Ese numero es correcto PARA UNA SEÑAL
 * SOLA, y en 7.074 no hay una, hay cuarenta: la banda entera de 3 kHz llena
 * de ellas. El ancho medido es el del monton, no el de una, asi que la fila
 * no cuadraba nunca y no iba a cuadrar jamas.
 *
 * Lo que de verdad identifica a FT8 en una banda de FT8 es lo mismo que lo
 * identifica a oido: TODAS EMPIEZAN Y ACABAN A LA VEZ, cada quince
 * segundos. Eso no se mide en el espectro, se mide en el tiempo, y es el
 * unico de los seis criterios que necesita MINUTOS en vez de milisegundos.
 *
 * Por eso va en su propia estructura y no dentro de stanag_det_t: el motor
 * mide ventanas de 341 ms y esto mira los ultimos cuatro minutos. Son dos
 * instrumentos distintos y mezclarlos obligaria a que el de las ventanas
 * supiera esperar.
 *
 *   periodo_ds  cada cuanto se repite el ciclo, en DECIMAS de segundo.
 *               150 son los 15 s del FT8; 1200, los 120 del WSPR.
 *   ocupa_pc    que parte del ciclo esta encendido. 84% el FT8 -12,64 s de
 *               cada 15-, 92% el WSPR -110,6 de 120-.
 */
typedef struct {
    uint8_t  vale;        /* 1 = hay cadencia medida */
    uint16_t periodo_ds;  /* decimas de segundo */
    uint8_t  ocupa_pc;
    uint8_t  firmeza;     /* 0..100, cuanto destaca el ciclo */
    uint16_t segundos;    /* cuanto lleva escuchando, para la pantalla */
} ident_cad_t;

typedef struct {
    const char *nombre;
    uint8_t  criterio;     /* IDENT_POR_* */
    /* Cada rango vale 0,0 cuando no se mira. Una firma cuadra si cuadran
     * TODOS los rangos que no sean 0,0. */
    uint16_t sep_min,   sep_max;      /* separacion de tonos, Hz */
    uint16_t bd_min,    bd_max;       /* baudios x10 */
    uint16_t trama_min, trama_max;    /* periodo, en decenas de us */
    uint16_t ancho_min, ancho_max;    /* ancho a -10 dB, Hz */
    uint8_t  ciclo_min, ciclo_max;    /* % de ciclo de trabajo */
    uint16_t cad_min,   cad_max;      /* periodo de la cadencia, en decimas */
    uint8_t  ocup_min,  ocup_max;     /* y que parte del ciclo ocupa, en % */
    uint16_t sim_min,   sim_max;      /* velocidad de simbolo, en decimas de Bd */
    uint8_t  ton_min,   ton_max;      /* tonos contados (ancho/velocidad) */

    /*
     * EL ALFABETO, PARA CUANDO LA VELOCIDAD NO SE DEJA MEDIR - 08/10/2026.
     *
     * Ver stanag_det.h (cr7_pc). Una fila que ponga cr7_min pide que el
     * flujo de bits sea un codigo de siete bits con cuatro unos -el CCIR
     * 476 del NAVTEX, el SITOR y la cifrada de 200 Hz- y, si pone cr7_bd,
     * que salga a ese baudio y no a otro.
     *
     * Existe porque la fila de la cifrada de 50 Bd / 200 Hz pedia velocidad
     * de simbolo y el medidor le daba la mitad. Un rango no se arregla
     * ensanchandolo hasta que entre: se cambia por una medida que SI
     * distinga. Y esta distingue porque mira lo que lleva dentro la señal,
     * no lo rapido que va.
     *
     * cr7_bd es un valor EXACTO, no un rango: solo hay tres (500, 750 y
     * 1000 decimas) y son los tres que caben a 3 kHz con un numero entero
     * de muestras por bit.
     */
    uint8_t  cr7_min;      /* % minimo de grupos con 4 unos. 0 = no se mira */
    uint16_t cr7_bd;       /* y a que baudio, en decimas. 0 = da igual */
    uint8_t  probada;      /* 1 = comprobada contra una señal de VERDAD: una
                            * grabacion del banco o una medida en antena. El
                            * comentario de la fila dice cual de las dos y
                            * con que numeros. 0 = el numero sale de la
                            * norma y nadie lo ha medido todavia. */
    const char *nota;      /* lo que hay que saber para no confundirse */
    /*
     * LAS DOS EN INGLES - 06/10/2026.
     *
     * Pedido por el usuario. Y llevaba razon: la tabla y
     * los renglones de escribe() eran las unicas cadenas de interfaz de toda
     * la radio que no pasaban por tr(), asi que con la radio en ingles el
     * modo Ident seguia contestando en castellano.
     *
     * Van AL FINAL de la estructura, detras de `nota`, y no al lado de su
     * pareja: asi cada fila solo añade dos cadenas en la cola y los
     * inicializadores posicionales de las veintisiete filas no se tocan. Un
     * campo nuevo en medio habria obligado a reordenar los veinte numeros de
     * cada fila, que es justo donde se cuelan los errores que no compilan
     * mal pero identifican mal.
     *
     * No se traducen los NOMBRES DE PROTOCOLO -"STANAG 4481 FSK (KG-84)" es
     * igual en los dos idiomas-, pero si lo que los rodea: "Portadora sola"
     * o "Podria ser ALE 2G" son frases, no nombres.
     */
    const char *nombre_en;
    const char *nota_en;
} ident_firma_t;

/* El nombre y la nota en el idioma que toque. Ver ident_firma_t. */
const char *ident_nombre(const ident_firma_t *f);
const char *ident_nota(const ident_firma_t *f);

/*
 * A QUIEN SE PARECEN ESTAS MEDIDAS. Rellena `salen` con los indices de las
 * firmas que cuadran, hasta `max`, y devuelve cuantas. Cero significa que
 * no se parece a nada de la tabla, que es una respuesta.
 *
 * ES UNA FUNCION PURA y por eso esta aqui fuera: el banco le puede dar
 * medidas inventadas y comprobar la tabla entera sin fabricar una sola
 * señal. Lo que SI se prueba contra grabaciones de verdad es el motor.
 */
uint16_t ident_casa(const stanag_det_t *m, const ident_cad_t *c,
                    uint8_t *salen, uint16_t max);

/* La tabla, para que el banco la recorra entera. */
const ident_firma_t *ident_firmas(uint16_t *n);

/*
 * CUANTO ENCAJA, EN TANTO POR CIENTO - 08/10/2026.
 *
 * Pedido por el usuario.
 *
 * LO PRIMERO, QUE ES LO QUE NO ES. Esto NO es una probabilidad. No hay con
 * que calcularla: haria falta saber cada cuanto aparece cada modo en la
 * banda, y eso no lo sabe nadie. Un numero que se llame "probabilidad" sin
 * serlo es peor que no tener numero, porque se usa para decidir.
 *
 * Lo que SI se puede medir, y es lo que esto da, es **cuanto encaja**: lo
 * lejos que esta la señal de dejar de cuadrar con esta fila. Tres cosas, y
 * se queda con LA PEOR, no con la media: una cadena vale lo que su eslabon
 * mas flojo, y promediar deja que un numero bueno tape a uno malo.
 *
 *  1. LO CENTRADO QUE CAE EN CADA RANGO. Un valor en mitad del rango vale
 *     100; pegado al borde, 0. Un salto de 215 Hz en una fila que pide de
 *     192 a 215 cuadra, si, pero cuadra por los pelos, y eso el que mira la
 *     pantalla tiene derecho a saberlo.
 *
 *  2. LA CALIDAD DE LA MEDIDA QUE MANDA EN ESA FILA. No es lo mismo
 *     reconocer una trama con el pico al 95% que al 22%. Cada criterio
 *     tiene su numero y aqui se usa el suyo: la calidad de la trama, lo que
 *     destaca el simbolo, la firmeza del ciclo, el alfabeto de siete bits.
 *
 *  3. SI LA FILA ESTA COMPROBADA CONTRA UNA SEÑAL DE VERDAD (`probada`).
 *     Una fila cuyos numeros salen de la norma y que nadie ha visto en
 *     antena no puede dar mas del 70%, por bien que encaje: lo que encaja
 *     es con lo que yo creo que es el modo, no con el modo.
 *
 * Y ADEMAS, SI CUADRA MAS DE UNA FILA, TODAS BAJAN. Con dos nombres
 * delante no se puede estar seguro de ninguno, por bien que encajen los
 * dos. `n_cuadran` es cuantas salieron de ident_casa().
 *
 * Devuelve 0..100. Es una funcion PURA: ni mira estado ni lo cambia, y por
 * eso el banco puede molerla a datos inventados (ver sim/identtest.c).
 */
uint8_t ident_encaje(const stanag_det_t *m, const ident_cad_t *c,
                     uint16_t firma, uint16_t n_cuadran);

/*
 * --- el modo ---
 *
 * Los nombres llevan "_modo_" como los otros ocho (ft8_modo_, hfdl_modo_,
 * ale_modo_...) y no por gusto: tools/panel_check.py reconoce los modos con
 * panel de tabla buscando "<algo>_modo_activo()" dentro de digi_sync() y de
 * ft8_panel_draw(), y exige que el que rellena el panel sea el mismo que lo
 * repinta. Con otro nombre, este modo se quedaria fuera de esa red y el
 * panel se congelaria sin que nada avisara - que es exactamente lo que paso
 * con STANAG el 02/10 y lo que hizo nacer ese comprobador.
 */
uint8_t     ident_modo_start(void);
void        ident_modo_stop(void);
uint8_t     ident_modo_activo(void);
void        ident_modo_mete(const float *audio, uint16_t n);
/*
 * La frecuencia sintonizada, en kilohercios, igual que ale_modo_frec_pon().
 * El unico uso hoy es AIS: ver el comentario de s_khz en ident.c para por
 * que ese modo se reconoce por el dial y no por la forma de onda.
 */
void        ident_modo_frec_pon(uint32_t khz);
void        ident_modo_poll(void);
uint8_t     ident_modo_lineas(void);
const char *ident_modo_linea(uint8_t i);
void        ident_modo_borra(void);

/* Las medidas crudas, para la zona de diagnostico de la pantalla. */
void ident_medidas(stanag_det_t *out);
void ident_cadencia(ident_cad_t *out);

#endif /* IDENT_H */
