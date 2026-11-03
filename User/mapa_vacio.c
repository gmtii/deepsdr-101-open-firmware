/*
 * EL MAPA DE SERIE, FUERA DEL FIRMWARE - 02/11/2026, decision del usuario.
 *
 * El firmware llevaba dentro una costa del mundo de 106 lineas y 1.762
 * puntos -Natural Earth 1:110M simplificada- para tener algo que pintar
 * mientras no hubiera un fichero .map grabado en la flash externa. Son
 * 7.268 bytes de la flash del chip, y la flash del chip es justo lo que no
 * hay: la V6.40 no cabia por 204 bytes.
 *
 * Y el mapa de serie no vale gran cosa: cualquiera que use el mapa de
 * verdad graba su .map, que tiene relleno, niveles y bastante mas detalle.
 * El de dentro solo se veia mientras no hubiera ninguno.
 *
 * Asi que se queda fuera de la compilacion del firmware -ver C_SOURCES en
 * reglas.mk- y aqui quedan los dos simbolos a cero. mapa_dibuja() ya sabe
 * no dibujar nada con cero lineas, y la pantalla del mapa pone un cartel
 * que dice que hay que grabar uno.
 *
 * User/mapa_datos.c SIGUE EN EL REPOSITORIO y lo siguen compilando los
 * bancos del simulador: ahi hace falta un mapa conocido contra el que
 * comparar el dibujo. Lo que cambia es que el chip ya no lo lleva.
 */
#include "mapa.h"

const int16_t  k_mapa_pts[] MAPA_DESVAN = { 0 };
const uint16_t k_mapa_n_lineas = 0U;

/* El tope de latitud NO es del mapa de serie: es la geometria de la
 * proyeccion, y la usan px_de_lon()/py_de_lat() para TODO lo que se pinta
 * encima -aviones, emisoras, el cursor-, haya fichero o no. Asi que vale
 * lo mismo que valia: +-71,50 grados. Ver tools/mapa_pack.py. */
const int16_t  k_mapa_lat_tope_c = 7150;
