#ifndef DSC_H
#define DSC_H

#include <stdint.h>

/*
 * DSC de onda corta (GMDSS) - 07/10/2026.
 *
 * Llamada selectiva digital marítima, ITU-R M.493-16. Es el sistema con el
 * que los barcos y las estaciones costeras se llaman entre sí antes de
 * hablar: dice quién llama, a quién, con qué urgencia y en qué frecuencia
 * seguir. Las alertas de socorro van por aquí.
 *
 * TODO lo que hay en este fichero está LEÍDO del estándar, no de memoria, y
 * además COMPROBADO contra cuatro grabaciones reales. Ver
 * docs/DSC_FORMATO.md, que lleva la cita del documento, la fecha, y las tres
 * llamadas que se decodificaron enteras con el prototipo de
 * tools/dsc_proto.py antes de escribir una línea de esto.
 *
 * LA MODULACIÓN ES LA DEL NAVTEX. 100 Bd, 170 Hz de desplazamiento. Idéntica.
 * Por eso el cañón de delante -dos correladores con media móvil de un bit y
 * lazo de Gardner- es el mismo diseño que navtex.c, que lleva semanas
 * funcionando. No se comparte el código a propósito: lo de detrás no se
 * parece en nada y tocar navtex.c para esto sería arriesgar un decodificador
 * bueno por ahorrar doscientas líneas.
 *
 * LO QUE NO ESTÁ PROBADO EN EL AIRE: el formato de SOCORRO. El dueño no tiene
 * ninguna grabación de una alerta real -y ojalá siga sin tenerla-, así que esa
 * rama se valida con trama sintética construida desde el estándar y punto. Va
 * dicho aquí y en el banco para que nadie le dé más crédito del que tiene.
 */

#define DSC_BAUD       100.0f
#define DSC_SHIFT_HZ   170.0f
/* El centro habitual de las llamadas de HF en banda lateral. Se mide solo
 * por cruces por cero igual que en el NAVTEX, así que esto es solo el punto
 * de partida. */
#define DSC_CENTRO_HZ 1700.0f

#define DSC_LINEAS   6U
#define DSC_LARGO   40U

typedef struct {
    uint8_t  enganchado;   /* 1 = el reloj de bit va enganchado */
    uint8_t  en_mensaje;   /* 1 = se está recibiendo una llamada */
    uint32_t llamadas;     /* llamadas completas con el ECC correcto */
    uint32_t malas;        /* completas con el ECC mal */
    int16_t  desvio_hz;    /* lo que el centro medido se aparta del nominal */
} dsc_info_t;

/*
 * El centro de los dos tonos. Se puede fijar a mano; si no, el decodificador
 * lo busca solo por cruces por cero igual que el NAVTEX.
 *
 * Hace falta porque el DSC no vive en un sitio fijo del audio: depende de
 * donde tenga el dial quien escucha. En las cuatro grabaciones del dueño los
 * centros salieron en 995, 1700 y 2214 Hz.
 */
void    dsc_set_centro_hz(float hz);
float   dsc_get_centro_hz(void);

void    dsc_start(float fs_hz);
void    dsc_stop(void);
uint8_t dsc_activo(void);

/* Audio de banda lateral crudo, a DEMOD_DEC_FS_HZ. Se llama desde la
 * interrupción. Acepta cualquier n: trabaja muestra a muestra. */
void    dsc_process(const float *audio, uint32_t n);

/* Desde el bucle principal: busca los dos tonos en el analizador de
 * sintonia mientras no haya enganche. Ver busca_centro() en dsc.c. */
void    dsc_poll(void);
float   dsc_busca_hz(void);

uint8_t     dsc_lineas(void);
const char *dsc_linea(uint8_t i);
void        dsc_info(dsc_info_t *out);

/* Para el banco: mete un símbolo ya decodificado, sin pasar por el audio.
 * Permite probar el entramado y el parseo -incluido el de SOCORRO, que no
 * tiene grabación- sin depender del cañón. */
void    dsc_mete_simbolo(uint8_t sim, uint8_t valido, uint8_t ranura_rx);

#endif /* DSC_H */
