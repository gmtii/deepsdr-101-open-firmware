/*
 * El reloj de la radio, para los bancos del PC.
 *
 * hfdl_modo.c le pide la hora al RTC para ponerla en cada linea. Aqui no hay
 * RTC, asi que se devuelve una hora fija.
 *
 * Se saca a su propio fichero porque ya hacian falta dos copias -el banco de
 * la polaridad y el de las autopruebas- y una tercera habria sido el momento
 * en que las tres empiezan a decir horas distintas.
 *
 * Fija y no "la del PC" a proposito: un banco cuya salida cambia sola cada
 * minuto no se puede comparar con la de ayer.
 */
#include "rtc_hw.h"

void rtc_hw_get(rtc_hw_datetime_t *out)
{
    out->year = 2026; out->month = 9; out->day = 25;
    out->hour = 13;   out->minute = 27; out->second = 0;
}

/*
 * Y SI ALGUIEN LE HA PUESTO LA HORA ALGUNA VEZ - 30/09/2026.
 *
 * Hace falta desde que ft8_modo.c y wspr_modo.c preguntan esto en vez de
 * mirar si la fecha es la del arranque en frio. Pedido por el usuario.
 * Aquel aviso miraba `t.year==2026 && t.month==1 && t.day==1`, que deja de
 * ser cierto en cuanto pasa el 1 de enero, y no tenia nada que ver con si
 * alguien habia puesto la hora.
 *
 * Empieza en "si", que es lo normal en un banco -se mide lo que hace el
 * decodificador, no el aviso-, y se puede cambiar para medir el aviso. Un
 * valor fijo a 1 haria que el banco no pudiera ver nunca el fallo que
 * acabamos de arreglar.
 */
static uint8_t s_sync = 1U;

void rtc_sim_pon_en_hora(uint8_t v) { s_sync = (uint8_t)(v ? 1U : 0U); }

bool rtc_hw_has_ever_synced(void) { return s_sync ? true : false; }
