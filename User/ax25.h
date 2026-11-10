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

/*
 * LOS DOS PAQUETES, QUE SON LA MISMA TRAMA POR DOS CAMINOS - 07/10/2026.
 *
 * Todo lo que hay detras del detector de tonos -NRZI, relleno de bits,
 * banderas, comprobacion y troceado de la trama- no sabe ni le importa a
 * que velocidad ha llegado. Asi que el paquete de onda corta no es un
 * decodificador nuevo: es este mismo con otros tres numeros.
 *
 *   APRS en VHF     1200 baudios, AFSK Bell 202 (1200 y 2200 Hz), y el
 *                   audio sale del discriminador de FM, o sea a la tasa de
 *                   radiofrecuencia (48 o 96 kHz segun el ajuste).
 *   Paquete de HF    300 baudios, FSK de 200 Hz de desplazamiento, y el
 *                   audio sale del camino de banda lateral, a 12 kHz.
 *
 * LA BANDA LATERAL DA IGUAL, y conviene saberlo antes de perder una tarde
 * probando las dos. El NRZI codifica el cero como un CAMBIO de tono y el
 * uno como ausencia de cambio, asi que intercambiar marca y espacio deja
 * los cambios exactamente donde estaban y los bits salen identicos. En USB
 * y en LSB se decodifica lo mismo.
 *
 * LO QUE NO ES IGUAL, y por eso hay un banco que lo mide: en Bell 202 el
 * desplazamiento (1000 Hz) es casi la velocidad (1200), asi que los dos
 * tonos caen casi ortogonales y cada correlador ve poco del otro. A 300
 * baudios con 200 Hz NO lo son -harian falta 300- y los dos correladores
 * se pisan. Eso cuesta sensibilidad, y cuanta cuesta es una medida, no una
 * opinion: ver sim/ax25test.c.
 */
#define AX25_MARCA_HZ   1200.0f
#define AX25_ESPACIO_HZ 2200.0f
#define AX25_BAUD       1200.0f

/* Paquete de HF. El desplazamiento de 200 Hz es lo que fija el modo; que
 * los tonos caigan en 1600 y 1800 es convenio del dial y de los TNC, no
 * del estandar. */
#define PKT_HF_MARCA_HZ   1600.0f
#define PKT_HF_ESPACIO_HZ 1800.0f
#define PKT_HF_BAUD        300.0f
#define PKT_HF_FS_HZ     12000.0f   /* la tasa del audio de banda lateral */

typedef struct {
    uint8_t  enganchado;   /* 1 = se han visto banderas hace poco */
    uint32_t tramas_ok;    /* tramas con la comprobacion correcta */
    uint32_t tramas_mal;   /* tramas completas que no la pasaron */
    uint32_t banderas;
    float    nivel;
} ax25_info_t;

/*
 * Y EL TERCERO: 9.600 G3RUH, EL DE LOS SATELITES - 09/11/2026.
 *
 * En 145,825 MHz hay mas de una cosa. La ISS repite a 1200 baudios con los
 * tonos de Bell 202 -lo de aqui arriba- pero varios satelites pequenos van
 * a 9.600, y eso es otra forma de onda: FSK directa, +-3 kHz de
 * desviacion, con la banda base pasada por un aleatorizador de 17 etapas.
 * No hay dos tonos que correlar.
 *
 * LO QUE CAMBIA Y LO QUE NO. Cambia la puerta de entrada -ver
 * proceso_directo() en ax25.c- y nada mas: el lazo de reloj, el NRZI, el
 * HDLC, la comprobacion y el troceado de la trama son los mismos de
 * siempre, por lo mismo que el paquete de HF no es un decodificador nuevo.
 *
 * ======================================================================
 * LOS NUMEROS NO SON MIOS: SON LOS DEL MODEM DE G3RUH.
 * ======================================================================
 *
 * El manual del modem de 9600 baudios de G3RUH -el que define esta forma
 * de onda- pide cuatro cosas al receptor, y conviene tenerlas delante
 * porque TRES de ellas deciden por donde se engancha esto:
 *
 *   1. DESVIACION +-3 kHz de pico (maximo). De ahi sale todo lo demas.
 *   2. El aleatorizador es 1 + x^12 + x^17, un registro de 17 etapas con
 *      una sola toma. Es el que deshace descram() en ax25.c.
 *   3. RESPUESTA PRACTICAMENTE HASTA CONTINUA, y lo marca como esencial.
 *      Por eso esto se engancha a la salida del discriminador y no al
 *      camino de audio: alli hay un filtro que empieza por encima de
 *      continua y un silenciador. Y por eso hace falta el seguidor de
 *      continua de proceso_directo(): si la respuesta llega a continua, el
 *      desvio de sintonia tambien llega.
 *   4. NO PEOR DE -4 dB A 4,8 kHz NI DE -10 dB A 7,2 kHz, y un ancho de FI
 *      de 15 kHz; dice que 20 kHz es lo preferible, que 16 vale y que
 *      8 kHz se queda corto.
 *
 * Y AHI ES DONDE EL FILTRO DE NFM NO LLEGA. El de NFM esta dimensionado
 * para un canal de voz de 12,5 kHz, o sea por debajo de los 15 que pide la
 * hoja. El que SI llega ya existe en demod_am.c -el del AIS- y no por
 * casualidad, porque el AIS es el mismo problema:
 *
 *      a 4,8 kHz   -0,02 dB   (la hoja admite hasta -4)
 *      a 7,2 kHz   -0,62 dB   (la hoja admite hasta -10)
 *      a 9,0 kHz   -3,01 dB   -> 18 kHz de FI, entre el 16 que vale y el
 *                                 20 que prefiere
 *
 * Se pide con demod_am_chf_ancho(). Sin eso esto no decodifica nada, y no
 * es una sospecha: es que la hoja dice que no.
 *
 * ======================================================================
 * QUE HAY AHI ARRIBA DE VERDAD - mirado el 09/11/2026.
 * ======================================================================
 *
 * En 145,825 digipiten la ISS, IO-86, NO-44 y SONATE-2, los cuatro a 1200
 * con Bell 202, y KNACKSAT-2 a 9.600 FSK con AX.25 G3RUH. O sea que esta
 * puerta sirve exactamente para UNO de los cinco de esa frecuencia.
 *
 * Fuera de 145,825 pero dentro de lo que esta radio alcanza -TUNE_MAX_HZ
 * son 180 MHz- hay dos digipetidores mas de 9.600 G3RUH: Marina en 145,925
 * y GRBBeta en 145,935, los dos con GFSK, que es esto mismo con los
 * flancos redondeados y le viene mejor todavia a una media movil de un
 * bit.
 *
 * Los demas digipetidores de 9.600 -CroCube, KOSTKA, LASARsat y compania-
 * estan en 436 MHz, o sea FUERA del alcance de esta radio. Conviene
 * saberlo antes de buscarlos.
 */
#define AX25_G3RUH_BAUD  9600.0f

/* El de siempre: 1200 baudios y los tonos de Bell 202. */
void ax25_start(float fs_hz);
/* Y el general, para el de HF. Ver el comentario de los dos de arriba. */
void ax25_start_fsk(float fs_hz, float baud, float marca_hz, float espacio_hz);

/* Y el camino directo de 9.600. Devuelve 0 si la tasa no da para diez
 * muestras por bit -a 48 kHz salen cinco-, en cuyo caso NO arranca: ver su
 * comentario en ax25.c. */
uint8_t ax25_start_g3ruh(float fs_hz);

/* Y si lo que esta corriendo ahora mismo es ese camino. */
uint8_t ax25_g3ruh_activo(void);
void ax25_stop(void);
uint8_t ax25_activo(void);
/* Los dos tonos con los que esta afinado AHORA: 1200/2200 en APRS, 1600/1800
 * en paquete de HF. Los pide el osciloscopio de sintonia, que antes llevaba
 * los del APRS escritos a mano y pintaba las rayas donde no habia nada en
 * cuanto el modo era el otro. Ver su definicion en ax25.c. */
float ax25_marca_hz(void);
float ax25_espacio_hz(void);

/* Audio del discriminador de FM, crudo. Se llama desde la interrupcion. */
void ax25_process(const float *audio, uint32_t n);

/*
 * Saca un caracter del texto ya formateado de la siguiente trama buena, o 0
 * si no hay nada. Las tramas salen como una linea de texto tipo
 * "EA4XYZ-9>APRS,WIDE1-1:!4133.00N/00042.00E>" y un salto de linea.
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
