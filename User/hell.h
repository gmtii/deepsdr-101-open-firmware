#ifndef HELL_H
#define HELL_H

#include <stdint.h>

/*
 * FELD-HELL: TELEGRAFIA POR IMAGEN, DE 1929. Etapa 42, 06/10/2026.
 *
 * Es el modo mas simple de todos los que lleva esta radio, y por una razon
 * que merece explicarse: NO TIENE NADA QUE DECODIFICAR. No hay alfabeto, ni
 * marco de caracter, ni correccion de errores, ni sincronismo. Lo que llega
 * son puntos encendidos y apagados, y las letras las forma el ojo.
 *
 * Rudolf Hell lo invento para una maquina mecanica: una rueda dentada por
 * caracter cerraba un contacto, y en el otro extremo un electroiman empujaba
 * una cinta de papel contra una helice entintada. Sin reloj, sin acuerdo
 * previo, sin nada. Y funciona.
 *
 * LOS NUMEROS (ficha de FELDHELL del decodificador Wavecom, leida el
 * 06/10/2026 - NO de memoria, que la primera vez me sali
 * "122,5 caracteres por minuto" y es falso):
 *
 *   modulacion      manipulacion de portadora (AM/OOK). Se oye como CW.
 *   velocidad       122,5 Bd, y un simbolo es un PAR de puntos
 *   matriz          7 columnas x 14 filas = 98 puntos por caracter
 *                   = 49 pares = 0,4 s por caracter = 150 por minuto
 *   barrido         por COLUMNAS, de izquierda a derecha, y dentro de cada
 *                   columna DE ABAJO ARRIBA
 *   margen          la primera y la ultima columna van en blanco, y la fila
 *                   de arriba y la de abajo tambien: la letra util es de
 *                   5 x 10
 *   recepcion       en CW, USB o LSB
 *
 * POR QUE NO HACE FALTA SINCRONISMO, que es lo que hay que entender para
 * creerse el resto: la pantalla pinta CADA COLUMNA DOS VECES, una encima de
 * otra. Si el ritmo del emisor y el nuestro no coinciden exactamente, el
 * texto sube o baja despacio por la cinta - pero como hay dos copias
 * desplazadas justo una altura de caracter, SIEMPRE hay una entera. Es lo
 * mismo que hacia la helice de la maquina original, que tenia el paso
 * calculado para imprimir doble fila por este motivo exacto.
 *
 * Asi que aqui no se busca una cabecera, ni se mide una fase, ni se decide
 * nada. Se reparte el audio en puntos de 1/245 de segundo y se van sacando
 * columnas. Si la velocidad del otro esta un 1% larga, el texto se inclina y
 * se lee igual.
 *
 * LO QUE SI HAY QUE HACER BIEN es la decision de "encendido o apagado", y
 * por eso el umbral NO es una constante: se saca de la propia señal, con un
 * seguidor de maximo y otro de minimo. Es la leccion que este proyecto lleva
 * aprendida cinco veces -no se mide contra una constante, se mide contra la
 * señal- y aqui ademas es gratis.
 *
 * Y SE SACA EN GRIS, no en blanco y negro. La maquina de Hell era mecanica y
 * solo sabia marcar o no marcar; nosotros tenemos 256 niveles y un punto a
 * medias se lee mucho mejor como gris que forzado a uno de los dos extremos.
 * No cuesta nada: el valor ya esta medido.
 */

/* Una columna son 14 puntos, que es la altura de la matriz. */
#define HELL_ALTO      14U

/* Puntos por segundo: 122,5 pares x 2. De aqui sale todo el reparto. */
#define HELL_PUNTOS_HZ 245.0f

typedef struct {
    uint8_t  activo;
    uint32_t columnas;   /* columnas entregadas desde que arranco */
    float    nivel;      /* amplitud media reciente, para la chapa */
    float    umbral;     /* donde esta cortando ahora mismo */
} hell_info_t;

void    hell_start(float fs_hz);
void    hell_stop(void);
uint8_t hell_activo(void);

/* Audio de banda lateral, crudo. Se llama desde la interrupcion. */
void hell_process(const float *audio, uint32_t n);

/*
 * Si hay una columna terminada, devuelve un puntero a sus HELL_ALTO bytes
 * -de arriba a abajo, ya dada la vuelta- y la da por consumida. 0 si no hay
 * nada. Cada byte es la intensidad del punto, 0 apagado y 255 encendido.
 */
const uint8_t *hell_columna(void);

void hell_info(hell_info_t *out);

#endif /* HELL_H */
