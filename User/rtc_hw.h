/*
 * ======================================================================
 * EL RELOJ DE HARDWARE DEL GD32F450
 * ======================================================================
 *
 * Reescrito entero el 13/10/2026. El fichero que habia aqui venia
 * portado tal cual de otro proyecto, con su licencia, y eso chocaba con
 * la de este repositorio; ver PROCEDENCIA.md. Este no copia nada: habla
 * con el periferico a traves del driver de GigaDevice que ya venia en
 * Firmware/, y la aritmetica del calendario esta en rtc_cal.c, aparte,
 * para que un banco pueda mirarla.
 *
 * POR QUE NO VALE EL RELOJ DE SOFTWARE
 * ------------------------------------
 * reloj.c lleva la hora como un desfase sobre g_msticks, o sea sobre
 * SysTick, que sale del HXTAL. Eso basta para enseñar la hora y NO
 * basta para FT8: sus ranuras son de quince segundos clavadas a UTC, y
 * el HXTAL no sigue al tiempo real lo bastante bien. El cristal de
 * 32.768 Hz que lleva la placa si: unas decenas de ppm, segundos AL
 * DIA, no al rato.
 *
 * Y sobrevive al reinicio. El dominio de respaldo sigue contando
 * mientras haya VDD, asi que reprogramar la radio no le mueve la hora.
 * Esta placa no lleva pila de respaldo, asi que un corte de corriente
 * de verdad si se la lleva por delante, y eso se nota al arrancar.
 *
 * LO QUE ESTE FICHERO NO HACE
 * ---------------------------
 * No sabe de husos. Lo que devuelve es lo que marca el calendario del
 * RTC, y el huso lo pone encima quien lo use -ver husos.c-. Meter aqui
 * una hora local seria repetir el fallo de la V4.27, cuando España
 * estaba escrita en cuatro sitios del firmware.
 * ======================================================================
 */
#ifndef RTC_HW_H
#define RTC_HW_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint16_t year;   /* año completo, 2000..2099 */
    uint8_t  month;  /* 1..12 */
    uint8_t  day;    /* 1..31 */
    uint8_t  hour;   /* 0..23 */
    uint8_t  minute; /* 0..59 */
    uint8_t  second; /* 0..59 */
} rtc_hw_datetime_t;

/*
 * Enciende el dominio de respaldo y el cristal de 32.768 Hz, se lo pone
 * al RTC como reloj y lo arranca. Se llama una vez, pronto, con los
 * demas *_init() de main().
 *
 * SI EL CALENDARIO YA ESTABA EN MARCHA, NO SE TOCA. Se mira el testigo
 * YCM del propio periferico, que el hardware pone al programar el año y
 * solo se borra si el dominio de respaldo pierde la alimentacion. Asi
 * un reinicio normal o una reprogramacion NO pisan una hora que ya era
 * buena. Si el testigo no esta -arranque en frio de verdad- se siembra
 * 2026-01-01 00:00:00, que es una fecha evidentemente falsa a proposito:
 * mas vale que se vea mal a que parezca buena.
 */
void rtc_hw_init(void);

/*
 * Verdadero si el cristal nunca llego a confirmarse estable. No es algo
 * que este fichero pueda arreglar -es el cristal, sus condensadores o la
 * placa- y lo unico sensato es enseñarlo: con esto en verdadero, cada
 * hora que da el RTC viene de un reloj que no se sabe si va.
 */
bool rtc_hw_lxtal_failed(void);

/* Lee el calendario. Si el RTC no arranco, devuelve la fecha semilla y
 * no basura. */
void rtc_hw_get(rtc_hw_datetime_t *out);

/*
 * Pone la hora de golpe. Es un salto: reinicializa el calendario, asi
 * que la hora en pantalla puede dar un brinco visible. Es lo que se
 * quiere la PRIMERA vez -cuando lo que hay es la semilla falsa- y no
 * para corregir deriva, que para eso esta rtc_hw_apply_shift().
 *
 * Una fecha imposible se rechaza y no se escribe. El periferico se
 * tragaria un 31 de febrero sin rechistar y a partir de ahi contaria
 * desde un dia que no existe.
 */
void rtc_hw_set(const rtc_hw_datetime_t *dt);

/*
 * Corrige deriva SIN reiniciar el calendario. No es un "+/- N
 * segundos": el periferico solo sabe sumar un segundo entero y restar
 * una fraccion. Con el prescaler sincrono en 255, la fraccion va de 0 a
 * 255 en pasos de unos 3,9 ms, asi que por llamada se puede mover algo
 * menos de un segundo hacia atras y uno entero hacia delante.
 *
 * Eso cubre de sobra la deriva de un cristal de verdad entre dos
 * sincronizaciones. Si hiciera falta mas, el que llama tiene que usar
 * rtc_hw_set() y asumir el salto.
 *
 * Devuelve falso si el hardware no lo acepto -tipicamente porque el
 * desplazamiento anterior sigue en curso-.
 */
bool rtc_hw_apply_shift(bool add_one_second, uint16_t subsecond_fraction);

/*
 * Verdadero UNA sola vez despues de cada rtc_hw_set(), y luego falso:
 * "el calendario acaba de saltar". Existe porque los repintados
 * periodicos de la interfaz se regulan comparando minutos de marcha, y
 * eso valia mientras el reloj solo avanzaba; desde que puede saltar a
 * cualquier hora en cualquier instante, ese filtro se puede comer el
 * salto durante un minuto entero -o para siempre, en los modos cuya
 * pasada de repintado no corre-.
 *
 * NO ES EL UNICO CONSUMIDOR POSIBLE, y por eso existe tambien
 * rtc_hw_get_set_seq(): el primero que lea esto lo apaga para todos los
 * demas.
 */
bool rtc_hw_consume_dirty(void);

/*
 * Contador que solo sube, uno por cada rtc_hw_set() -nunca por un
 * shift- y que no se borra nunca. A diferencia del testigo de arriba,
 * esto lo pueden mirar VARIOS a la vez: cada uno se guarda el ultimo
 * valor que vio y lo compara. Nadie le roba el aviso a nadie.
 */
uint32_t rtc_hw_get_set_seq(void);

/* Suma, en milisegundos, de todos los desplazamientos que han salido
 * bien: positivo, el reloj se ha movido hacia delante. Es solo para
 * mirar; no lo consume nadie. */
int32_t rtc_hw_get_cumulative_shift_ms(void);

/*
 * "¿A este reloj lo ha puesto en hora alguien alguna vez?" y "¿hace
 * cuanto?". Las dos sobreviven a un reinicio, porque viven en un
 * registro de respaldo y no en RAM: llevandolo en RAM, la radio diria
 * "nunca sincronizado" despues de cada arranque aunque el RTC no
 * hubiera perdido la hora ni un segundo.
 *
 * Cuentan tanto rtc_hw_set() como un rtc_hw_apply_shift() que salga
 * bien: poner en hora y corregir deriva son las dos la misma cosa a
 * estos efectos.
 *
 * PREGUNTA PRIMERO POR has_ever_synced(). Sin ninguna sincronizacion
 * anotada no hay un "hace cuanto" que tenga sentido, y lo que devuelve
 * el otro no es un tiempo transcurrido.
 */
bool rtc_hw_has_ever_synced(void);
uint32_t rtc_hw_get_seconds_since_sync(void);

#endif /* RTC_HW_H */
