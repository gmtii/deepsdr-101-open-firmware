#ifndef AX25_H
#define AX25_H

#include <stdint.h>

/*
 * PAQUETE AX.25 / APRS. Etapa 28, 24/09/2026.
 *
 * Tramas de datos que se mandan las estaciones de aficionado en FM, sobre
 * todo en 144,800 MHz en Europa: posiciones de estaciones fijas y moviles,
 * partes meteorologicos, globos sonda, mensajes cortos y repetidores que
 * reenvian lo de los demas.
 *
 * VA EN FM, NO EN BANDA LATERAL, y eso cambia de donde sale el audio: no del
 * camino de banda lateral como el RTTY, el NAVTEX, el fax o el SSTV, sino de
 * la salida del discriminador de FM estrecha. Aqui llega ese audio tal cual,
 * sin filtrar y sin silenciador: un silenciador que corta entre trama y trama
 * se comeria el principio de cada una.
 *
 * QUE SE DECODIFICA, de abajo arriba:
 *
 *   1. AFSK a 1200 baudios: 1200 Hz es un uno y 2200 Hz es un cero (Bell
 *      202). Se mide con dos correlatores integrados sobre UN BIT ENTERO,
 *      igual que en el NAVTEX y por la misma razon.
 *
 *   2. NRZI: lo que significa un bit no es su nivel sino si HA CAMBIADO. Un
 *      cero es un cambio y un uno es quedarse igual. Eso hace que la
 *      polaridad de los tonos deje de importar - si el receptor los oye del
 *      reves, los datos salen igual-, que es justo lo que no pasaba en el
 *      NAVTEX y por lo que alli hizo falta un conmutador.
 *
 *   3. HDLC: las tramas van entre banderas 01111110, y para que ese patron no
 *      pueda aparecer dentro de los datos el transmisor mete un cero forzado
 *      despues de cada cinco unos seguidos. Aqui se quita.
 *
 *   4. La trama: direcciones (destino, origen y hasta ocho repetidores),
 *      control, identificador de protocolo, datos, y dos bytes de
 *      comprobacion.
 *
 * LA COMPROBACION SE EXIGE SIEMPRE. Son dieciseis bits sobre toda la trama:
 * una trama con un solo bit mal no pasa. Eso es lo que permite ser generoso
 * en todo lo de arriba -engancharse a cualquier cosa que parezca una
 * bandera- sin llenar la pantalla de basura: lo que no cuadra, no sale.
 */

/* Lo mas largo que puede ser una trama: 8 direcciones de 7 bytes, control,
 * identificador, 256 de datos y 2 de comprobacion. */
#define AX25_TRAMA_MAX 332U

/* Lo mas corto que puede tener sentido: dos direcciones, control y la
 * comprobacion. */
#define AX25_TRAMA_MIN 17U

#define AX25_MARCA_HZ   1200.0f
#define AX25_ESPACIO_HZ 2200.0f
#define AX25_BAUD       1200.0f

typedef struct {
    uint8_t  enganchado;   /* 1 = se han visto banderas hace poco */
    uint32_t tramas_ok;    /* tramas con la comprobacion correcta */
    uint32_t tramas_mal;   /* tramas completas que no la pasaron */
    uint32_t banderas;
    float    nivel;
} ax25_info_t;

void ax25_start(float fs_hz);
void ax25_stop(void);
uint8_t ax25_activo(void);

/* Audio del discriminador de FM, crudo. Se llama desde la interrupcion. */
void ax25_process(const float *audio, uint32_t n);

/*
 * Saca un caracter del texto ya formateado de la siguiente trama buena, o 0
 * si no hay nada. Las tramas salen como una linea de texto tipo
 * "EA4HEW-9>APRS,WIDE1-1:!4025.12N/00341.45W>" y un salto de linea.
 */
uint8_t ax25_get_char(char *out);

void ax25_info(ax25_info_t *out);

/* ----------------------------------------------------------------------
 * Solo para el banco del simulador.
 * ---------------------------------------------------------------------- */

/* La comprobacion de la trama, tal y como la calcula el decodificador. El
 * banco la llama para CONSTRUIR sus tramas, asi que no hay dos formulas. */
uint16_t ax25_fcs(const uint8_t *d, uint16_t n);

/* El estado del lazo de reloj, para ver si se esta yendo en vez de deducirlo
 * de que las tramas empeoran. */
void ax25_lazo_dbg(float *ppm, float *nivel);

/* Mete una trama ya montada (sin banderas) por la puerta de atras, como si
 * hubiera llegado por la antena y hubiera pasado el HDLC. Sirve para medir el
 * formateo del texto sin tener que fabricar audio. */
void ax25_trama_dbg(const uint8_t *d, uint16_t n);

#endif /* AX25_H */
