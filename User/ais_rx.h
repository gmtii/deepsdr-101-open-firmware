#ifndef AIS_RX_H
#define AIS_RX_H

/*
 * AIS: del discriminador de FM a las tramas. 27/09/2026.
 *
 * La mitad de abajo del modo. Come lo que sale del discriminador -o sea la
 * frecuencia instantanea, la misma senal que oye el APRS- y suelta tramas
 * HDLC con el CRC ya comprobado. No sabe lo que es un barco.
 *
 * POR QUE EL DISCRIMINADOR Y NO LA BANDA LATERAL. AIS es GMSK, y GMSK es
 * FSK con las esquinas redondeadas: el transmisor desvia la portadora
 * +-2.400 Hz al ritmo de 9.600 bits por segundo. Un discriminador de FM
 * devuelve justo eso -la desviacion instantanea- asi que un uno es un
 * numero positivo y un cero uno negativo, y no hace falta ni mezclador ni
 * lazo de portadora. Es el mismo enganche que usa el APRS y por el mismo
 * motivo (ver el comentario de ax25_process en demod_am.c).
 *
 * LO QUE SI HACE FALTA ES UN FILTRO DE CANAL MAS ANCHO. El de NFM esta
 * dimensionado para canales de 12,5 kHz (~6,25 kHz de paso) y AIS ocupa,
 * por la regla de Carson, 2*(2400+4800) = 14,4 kHz. Con el filtro de NFM
 * los flancos de la GMSK se redondean hasta que el ojo se cierra. De ahi
 * AIS_CHF_COEFFS en demod_am.c.
 *
 * DIEZ MUESTRAS POR BIT, EXACTAS. La cadena entrega 96.000 muestras por
 * segundo y AIS son 9.600 bits: 96000/9600 = 10 clavado. Eso quita de en
 * medio el interpolador que haria falta con cualquier otra tasa, y por eso
 * el lazo de reloj de aqui es el mismo acumulador de fase del AX.25 y no
 * algo mas gordo.
 *
 * LAS DOS FRECUENCIAS. AIS reparte el trafico entre 161,975 MHz (canal A) y
 * 162,025 MHz (canal B), que estan a 50 kHz una de otra. Con una sola
 * cadena de FI se oye UNA cada vez; el boton de frecuencia conmuta. Los
 * barcos alternan entre las dos, asi que en cualquiera de ellas se ve la
 * mitad del trafico - y en un puerto la mitad sobra.
 */

#include <stdint.h>

/*
 * El tope de una trama, en bits.
 *
 * Un informe de posicion son 168 bits y los datos estaticos del viaje
 * (tipo 5) son 424, que es el mensaje largo que interesa. AIS admite
 * mensajes de hasta cinco ranuras -1.008 bits- pero son binarios de
 * fabricante que no tienen nada que ensenar aqui, asi que el tope se pone
 * donde acaba lo util y las tramas mas largas se tiran al llegar.
 *
 * 448 bits = 56 bytes, con los 16 del CRC dentro. El numero sale de ahi y
 * no de un redondeo: 424 + 16 = 440, y 448 es el multiplo de ocho
 * siguiente. Los ocho bytes que se ahorran frente a 512 se multiplican por
 * el anillo, y esta radio va al 97% de SRAM.
 */
#define AIS_BITS_MAX   448U
#define AIS_BYTES_MAX  (AIS_BITS_MAX / 8U)

typedef struct {
    uint32_t banderas;    /* 0x7E vistas: dice si hay senal aunque no cuadre */
    uint32_t tramas;      /* cerradas con un tamano creible */
    uint32_t buenas;      /* y con el CRC bueno */
    uint32_t largas;      /* tiradas por pasar de AIS_BITS_MAX */
    uint8_t  nivel;       /* 0..99, sin calibrar: sirve para comparar */
} ais_rx_info_t;

void    ais_rx_start(float fs_hz);
void    ais_rx_stop(void);
uint8_t ais_rx_activo(void);

/* Audio del discriminador, desde la interrupcion del DMA. */
void    ais_rx_mete(const float *disc, uint32_t n);

/*
 * Saca la siguiente trama buena que haya, o 0 si no hay ninguna.
 *
 * Los bits salen EN ORDEN DE LLEGADA y empaquetados de mas a menos peso: el
 * primero en el bit 7 de bits[0]. Es el orden que quiere ais_msg.c y no el
 * que monta el AX.25 - ver el comentario de arriba de ais_msg.h, que
 * explica por que mezclarlos sale caro.
 *
 * `nbits_out` NO incluye los 16 del CRC: aqui ya se han comprobado y
 * quitado.
 *
 * Se llama desde el bucle principal. Las tramas se guardan en un anillo
 * pequeno porque quien las cierra es la interrupcion del audio.
 */
uint8_t ais_rx_saca(uint8_t *bits, uint16_t max_bytes, uint16_t *nbits_out);

void    ais_rx_info(ais_rx_info_t *out);
void    ais_rx_cuentas_cero(void);

#endif /* AIS_RX_H */
